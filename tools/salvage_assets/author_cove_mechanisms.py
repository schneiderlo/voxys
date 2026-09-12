"""Split the accepted Cove propeller/winch into rigid moving and fixed meshes.

Background authoring only. Current geometry/material recipes are reused; the
winch gets one orange surface mark without changing its shape or clearances.
The two named root meshes are driven by runtime state, not glTF animation clips.
"""
from __future__ import annotations
import argparse
import copy
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0, str(ROOT/'tools/salvage_assets'))
import author_cove_propeller_art as power
import author_cove_machinery_art as machinery
import author_functional_kit as art
import author_cove_toy_art as toy

PARTS = {
    'propeller': dict(recipe=power, role='voxys_propeller_rotor', pivot=(0,0,0), axis=(0,0,1),
                     metadata='data/salvage/functional-kit/r09/propeller/source/propeller.gameplay.json',
                     previous='data/salvage/toy-art/r04/propeller', visual=800),
    'winch': dict(recipe=machinery, role='voxys_winch_drum', pivot=(0,.12,0), axis=(1,0,0),
                 metadata='data/salvage/material-calibration/r04/kit/winch/source/winch.gameplay.json',
                 previous='data/salvage/toy-art/r02/winch', visual=900),
}


def moving(name, obj):
    return obj.name.startswith(('hub', 'prop_blade') if name == 'propeller'
                               else ('cable_drum', 'drum_flange', 'cable_wrap'))


def interfaces(name, model):
    if name == 'propeller':
        blade = math.hypot(.075, .345)
        guard = .380 * math.cos(math.pi/12)
        assert guard-blade > .0139
        return dict(blade_radius_upper_metres=blade, guard_inner_apothem_lower_metres=guard,
                    minimum_blade_guard_gap_metres=guard-blade,
                    blade_guard_arm_axial_gap_metres=.025,
                    blade_guard_boss_axial_gap_metres=.005)
    def xbounds(obj):
        v=[art.geo.canonical(obj.matrix_world@v.co)[0] for v in obj.data.vertices]
        return min(v),max(v)
    cheeks=sorted(xbounds(o) for o in model.objects if o.name.startswith('mast_cheek'))
    flanges=sorted(xbounds(o) for o in model.objects if o.name.startswith('drum_flange'))
    assert len(cheeks)==len(flanges)==2
    gaps=[flanges[0][0]-cheeks[0][1],cheeks[1][0]-flanges[1][1]]
    assert min(gaps) > .0099
    return dict(flange_cheek_gap_metres=gaps, flange_bounds=flanges,
                rotation_preserves_axis_x_bounds=True)


def orange_mark(obj, material):
    # Colour a short sector of the existing flange rim, including its bevel.
    # No geometry is added or moved, so the 10 mm flange/cheek gap is exact.
    slot=len(obj.data.materials)
    obj.data.materials.append(material)
    marked=0
    for face in obj.data.polygons:
        center=art.geo.canonical(face.center)
        normal=art.geo.canonical(face.normal)
        radial=math.hypot(center[1]-.12,center[2])
        if .209 <= abs(center[0]) <= .261 and radial>.33 and abs(normal[0])<.9:
            angle=abs(math.atan2(center[2],center[1]-.12))
            if angle < math.pi/6:
                face.material_index=slot
                marked+=1
    if marked<2:
        raise ValueError('rotation mark must be visible on both flange rims')
    return dict(triangles_recoloured=marked, half_angle_radians=math.pi/6,
                material='coral', geometry_changed=False,
                note='Existing flange rim surface is orange near canonical +Y; no added geometry.')


def split(model,name,spec,materials):
    groups={False:[],True:[]}
    for obj in model.objects:
        groups[moving(name,obj)].append(obj)
    assert all(groups.values())
    result=[]
    checks=[]
    for driven in (False,True):
        group=art.Model('mechanism_moving' if driven else 'mechanism_static',model.lod)
        group.objects=groups[driven]
        obj,check=group.finish(materials,art.shading.ANALYTIC,metric_uv=False)
        if driven and name=='winch':
            check['rotation_mark']=orange_mark(obj,materials['coral'])
        pivot=Vector(art.geo.to_blender(spec['pivot'] if driven else (0,0,0)))
        assert all(abs(obj.matrix_world[r][c]-(1 if r==c else 0))<1e-7 for r in range(4) for c in range(4))
        for vertex in obj.data.vertices:
            vertex.co-=pivot
        obj.location=pivot
        obj.name=spec['role'] if driven else 'voxys_mechanism_static'
        obj['mechanism_role']=obj.name
        result.append(obj)
        checks.append(check)
    bpy.context.view_layer.update()
    return result,checks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup')
    out=args.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        parser.error('output must not exist and parent must exist')
    out.mkdir()
    for name,spec in PARTS.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.preferences.filepaths.file_preview_type='NONE'
        bpy.context.scene.unit_settings.system='METRIC'
        original=json.loads((ROOT/spec['metadata']).read_text())
        metadata=copy.deepcopy(original)
        source=out/name/'source';source.mkdir(parents=True)
        records=[];all_objects=[]
        for lod,binding in enumerate(metadata['lods']):
            model=getattr(spec['recipe'],name)(lod,metadata['part'])
            clearance=interfaces(name,model)
            objects,checks=split(model,name,spec,toy.materials(lod))
            art.geo.activate(objects[0])
            for obj in objects: obj.select_set(True)
            path=source/f'{name}-lod-{lod}.glb'
            bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
                export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,
                export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,
                export_morph=False,export_cameras=False,export_lights=False,export_extras=False,
                export_vertex_color='NONE',export_all_vertex_colors=False)
            doc=art.base.glb_json(path)
            assert len(doc['nodes'])==len(doc['meshes'])==2
            assert {n['name'] for n in doc['nodes']}=={'voxys_mechanism_static',spec['role']}
            for node in doc['nodes']:
                assert set(node)<= {'mesh','name','translation','rotation','scale'}
                assert node.get('rotation',[0,0,0,1])==[0,0,0,1]
                assert node.get('scale',[1,1,1])==[1,1,1]
                expected=spec['pivot'] if node['name']==spec['role'] else (0,0,0)
                assert max(abs(a-b) for a,b in zip(node.get('translation',[0,0,0]),expected))<1e-7
            primitives=[p for mesh in doc['meshes'] for p in mesh['primitives']]
            vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)
            triangles=sum(doc['accessors'][p['indices']]['count']//3 for p in primitives)
            limits=((24000,12000),(12000,6000),(6000,3000))[lod]
            assert vertices<=limits[0] and triangles<=limits[1] and len(primitives)<=8
            assert not doc.get('images') and not doc.get('animations') and not doc.get('skins')
            assert not any('TEXCOORD_0' in p['attributes'] or 'TANGENT' in p['attributes'] for p in primitives)
            binding['asset']=dict(namespace=b'voxys-toy-art-v1'.hex(),counter=str(spec['visual']+lod+1),version=1)
            binding['source'].update(file=path.name,bytes=path.stat().st_size,sha256=art.base.sha256(path))
            points=[art.geo.canonical(obj.matrix_world@v.co) for obj in objects for v in obj.data.vertices]
            bounds=dict(minimum=[min(p[k] for p in points) for k in range(3)],maximum=[max(p[k] for p in points) for k in range(3)])
            records.append(dict(lod=lod,vertices=vertices,triangles=triangles,draws=len(primitives),nodes=doc['nodes'],
                                bounds=bounds,checks=checks,clearance=clearance,source_sha256=art.base.sha256(path)))
            for obj in objects:
                obj.name+=f'_lod_{lod}'
                obj.hide_set(True);obj.hide_render=True
            all_objects.append(objects)
        assert {k:v for k,v in metadata.items() if k!='lods'}=={k:v for k,v in original.items() if k!='lods'}
        (source/f'{name}.gameplay.json').write_text(json.dumps(metadata,indent=2)+'\n')
        for obj in all_objects[0]:obj.hide_set(False);obj.hide_render=False
        bpy.ops.wm.save_as_mainfile(filepath=str(source/f'{name}.blend'),compress=True)
        recipes=[Path(__file__),Path(spec['recipe'].__file__),Path(art.__file__),Path(toy.__file__),
                 Path(art.geo.__file__),Path(art.geo.params.__file__),Path(art.shading.__file__),Path(art.metric.__file__),Path(art.base.__file__)]
        inputs={('tools/salvage_assets/'+p.name if p.resolve()==Path(__file__).resolve() else str(p.relative_to(ROOT))):art.base.sha256(p) for p in recipes}
        previous=ROOT/spec['previous']/'cooked/cook-manifest.json'
        provenance=dict(schema=1,status='scratch articulated presentation candidate; runtime validation pending',
            original_metadata=spec['metadata'],original_metadata_sha256=art.base.sha256(ROOT/spec['metadata']),
            previous_presentation=spec['previous'],previous_manifest_sha256=art.base.sha256(previous),
            canonical_metadata_unchanged=True,inputs=inputs,palette_srgb=toy.PALETTE,
            blender_version=bpy.app.version_string,blender_build_hash=bpy.app.build_hash.decode(),
            mechanism=dict(moving_node=spec['role'],fixed_node='voxys_mechanism_static',canonical_pivot=spec['pivot'],
                           canonical_positive_axis=spec['axis'],source_pivot=spec['pivot'],source_positive_axis=[-x for x in spec['axis']],
                           contract='Two root mesh nodes, no parent/children; identity rotation/scale. Moving vertices pivot-relative, node translation restores phase zero.'),
            lods=records,provenance='Original procedural geometry reused from exact accepted recipes; no external assets, textures or images.',
            limitations=['No previews/screenshots or GPU checks. Runtime state drives rotation; asset contains no animation clips.',
                         'Winch intentional visible-detail change: orange flange-rim surface segment, no shape or clearance change.'])
        (out/name/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
        print('COVE_MECHANISM',name,json.dumps([{k:v for k,v in r.items() if k not in ('checks','nodes')} for r in records]),flush=True)


if __name__=='__main__':main()
