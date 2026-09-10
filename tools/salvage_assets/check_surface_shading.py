#!/usr/bin/env python3
"""Inspect exported winch/helm shading against the frozen flat metric assets.

This checks actual GLB corner normals and retained physical/material data,
independently of Blender's normal-authoring helper. It is not visual acceptance.
Requires Pillow through check_metric_materials.Model.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path

from check_metric_materials import Model, cross, dot, subtract, unit


def canonical(p):
    return (-p[0], p[1], -p[2])


def angle(a, b):
    return math.degrees(math.acos(max(-1., min(1., dot(unit(a), unit(b))))))


def material_geometry(model):
    # Every triangle corner retains its position, UV and material assignment.
    result = Counter()
    for attrs, triangles, material in model.primitives:
        for ids in triangles:
            corners = tuple((attrs['POSITION'][i], attrs['TEXCOORD_0'][i]) for i in ids)
            # Cyclic rotations are equivalent; reversed winding is not.
            result[(material['name'], min(corners[i:]+corners[:i] for i in range(3)))] += 1
    return result


def material_data(model):
    def texture(index):
        image = model.image(index)
        sampler = model.doc['samplers'][model.doc['textures'][index]['sampler']]
        return (image.size, image.tobytes(), sampler)
    result = {}
    for _, _, source in model.primitives:
        material = json.loads(json.dumps(source))
        for record in (material, material.get('pbrMetallicRoughness', {})):
            for key, value in record.items():
                if key.endswith('Texture'):
                    value['index'] = texture(value['index'])
        result[source['name']] = material
    return result


def measurements(model, part, lod):
    checks = {'flat_panel': [], 'flange_barrel': [], 'rubber_torus': [], 'flange_cap': []}
    lengths = []; orthogonality = []; green_alignment = []; facing = []
    vertices = 0
    for attrs, triangles, material in model.primitives:
        vertices += len(attrs['POSITION'])
        for n in attrs['NORMAL']:
            assert all(math.isfinite(x) for x in n)
            lengths.append(abs(math.sqrt(dot(n, n)) - 1))
        for n, t in zip(attrs['NORMAL'], attrs.get('TANGENT', [])):
            assert all(math.isfinite(x) for x in t) and t[3] in (-1., 1.)
            lengths.append(abs(math.sqrt(dot(t[:3], t[:3])) - 1))
            orthogonality.append(abs(dot(n, t[:3])))
        for ids in triangles:
            points = [canonical(attrs['POSITION'][i]) for i in ids]
            a, b, c = [attrs['POSITION'][i] for i in ids]
            e1, e2 = subtract(b, a), subtract(c, a)
            geometric = cross(e1, e2)
            if dot(geometric, geometric) < 4e-20:
                continue
            geometric = unit(geometric)
            for i in ids:
                facing.append(dot(geometric, unit(attrs['NORMAL'][i])))
            if 'TANGENT' in attrs:
                ua, ub, uc = [attrs['TEXCOORD_0'][i] for i in ids]
                du, dv = subtract(ub, ua), subtract(uc, ua)
                determinant = du[0]*dv[1] - du[1]*dv[0]
                increasing_v = tuple((e2[k]*du[0]-e1[k]*dv[0])/determinant for k in range(3))
                for i in ids:
                    n = unit(attrs['NORMAL'][i]); t = attrs['TANGENT'][i]
                    tangent_b = unit(tuple(x*t[3] for x in cross(n, t[:3])))
                    projected_v = unit(tuple(increasing_v[k]-n[k]*dot(n, increasing_v) for k in range(3)))
                    # Blender's glTF UV V is flipped; its normal-map green
                    # basis remains the authored direction. Smooth N changes
                    # the angle, but must not reverse that direction.
                    green_alignment.append(-dot(projected_v, tangent_b))
            panel_axis = None
            if part == 'winch' and 'coral' in material['name']:
                if all(abs(abs(p[0])-.45)<1e-6 for p in points) and max(p[0] for p in points)-min(p[0] for p in points)<1e-6:
                    panel_axis = 0
            if part == 'helm' and 'cream' in material['name']:
                for axis, plane in ((0,.42),(0,-.42),(1,.27),(2,.22),(2,-.38)):
                    if all(abs(p[axis]-plane)<1e-6 for p in points):
                        panel_axis = axis
            barrel = part=='winch' and 'steel' in material['name'] and all(
                .209<abs(p[0])<.261 and abs(math.hypot(p[1]-.12,p[2])-.36)<1e-5 for p in points
            ) and max(p[0] for p in points)-min(p[0] for p in points)>.015
            cap = part=='winch' and 'steel' in material['name'] and all(
                abs(abs(p[0])-.26)<1e-6 for p in points
            ) and max(p[0] for p in points)-min(p[0] for p in points)<1e-6
            for i, p in zip(ids, points):
                n = canonical(attrs['NORMAL'][i])
                if panel_axis is not None:
                    checks['flat_panel'].append(angle(n, canonical(geometric)))
                if barrel:
                    checks['flange_barrel'].append(angle(n, (0,p[1]-.12,p[2])))
                if cap:
                    checks['flange_cap'].append(angle(n, canonical(geometric)))
                if part=='helm' and 'rubber' in material['name']:
                    q = (p[0],p[1]-.25,p[2]-.30);radius=math.hypot(q[0],q[1])
                    expected=(q[0]*(1-.18/radius),q[1]*(1-.18/radius),q[2])
                    checks['rubber_torus'].append(angle(n,expected))
    assert lengths and max(lengths)<.0002
    assert not orthogonality or max(orthogonality)<.0002
    assert facing and min(facing)>0
    assert not green_alignment or min(green_alignment)>0
    return dict(vertices=vertices, maximum_frame_unit_error=max(lengths),
                maximum_normal_tangent_dot=max(orthogonality,default=0),
                minimum_green_projected_alignment=min(green_alignment,default=None),
                minimum_geometric_face_alignment=min(facing),
                surfaces={k:dict(corners=len(v),maximum_degrees=max(v,default=None)) for k,v in checks.items()})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate',type=Path,required=True,help='Candidate root containing kit/winch and kit/helm')
    parser.add_argument('--baseline',type=Path,default=Path('data/salvage/material-calibration/r02'))
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();assert not args.report.exists()
    result=dict(status='running',scope='Exported shading and unchanged material/physical data; no visual gate',checks=[])
    try:
        for part in ('winch','helm'):
            old_dir=args.baseline/'kit'/part/'source';new_dir=args.candidate/'kit'/part/'source'
            old_meta=json.loads((old_dir/f'{part}.gameplay.json').read_text())
            new_meta=json.loads((new_dir/f'{part}.gameplay.json').read_text())
            for key in ('part','tool_anchors'):
                assert old_meta.get(key)==new_meta.get(key),(part,key,'physical data changed')
            for lod in range(3):
                name=f'{part}-lod-{lod}.glb';old=Model(old_dir/name);new=Model(new_dir/name)
                assert old.geometry()==new.geometry(),(part,lod,'triangle positions changed')
                assert material_geometry(old)==material_geometry(new),(part,lod,'UV/material assignment changed')
                assert material_data(old)==material_data(new),(part,lod,'material pixels/factors changed')
                before=measurements(old,part,lod);after=measurements(new,part,lod)
                row=dict(part=part,lod=lod,baseline_sha256=old.sha,candidate_sha256=new.sha,
                         geometry_uv_material_physical_identical=True,before=before,after=after)
                result['checks'].append(row)
                required=('flat_panel','flange_barrel','flange_cap') if part=='winch' else ('flat_panel','rubber_torus')
                for surface in required:
                    measured=after['surfaces'][surface]
                    assert measured['corners']>0,(part,lod,surface,'empty fixture')
                    # Exported float normals are rounded to four decimals.
                    # .03 degrees covers that quantization; no broad-panel
                    # rounding or polygon-sized curve error is acceptable.
                    assert measured['maximum_degrees']<.03,(part,lod,surface,measured)
        result['status']='passed'
    except Exception as error:
        result.update(status='failed',error=str(error));raise
    finally:
        args.report.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':
    main()
