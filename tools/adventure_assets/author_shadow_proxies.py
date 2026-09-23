#!/usr/bin/env python3
"""Make coarse far-shadow casters from the installed village collision solids."""
import argparse, hashlib, json, os, struct, sys
from pathlib import Path

import bpy
import bmesh


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def box_mesh(name, boxes, material):
    vertices, faces = [], []
    sides = ((0,2,3,1),(4,5,7,6),(0,1,5,4),
             (2,6,7,3),(0,4,6,2),(1,3,7,5))
    for lower, upper in boxes:
        if any(upper[k] <= lower[k] for k in range(3)):
            continue
        start = len(vertices)
        for i in range(8):
            x = upper[0] if i & 1 else lower[0]
            y = upper[1] if i & 2 else lower[1]
            z = upper[2] if i & 4 else lower[2]
            vertices.append((x,-z,y))
        faces.extend(tuple(start+j for j in side) for side in sides)
    assert vertices
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material)
    return len(faces)*2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--village',type=Path,required=True)
    parser.add_argument('--blacksmith',type=Path,required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    bpy.ops.wm.read_factory_settings(use_empty=True)
    material = bpy.data.materials.new('shadow_solid')
    material.diffuse_color = (.5,.5,.5,1)
    village = json.loads((args.village/'provenance.json').read_text())
    blacksmith = json.loads((args.blacksmith/'collision.json').read_text())
    assert len(village['props']) == 15 and len(blacksmith['boxes']) > 100
    counts = []
    for i, prop in enumerate(village['props']):
        counts.append(box_mesh(f'{i:02d}_{prop["name"]}', prop['solids'], material))
    counts.append(box_mesh('15_blacksmith',blacksmith['boxes'],material))
    output = args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(output/'shadow-proxies.blend'),compress=True)
    path = output/'shadow-proxies.glb'
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',
        use_selection=True,export_yup=True,export_apply=True,
        export_materials='EXPORT',export_normals=True,export_texcoords=False,
        export_tangents=False,export_animations=False,export_skins=False,
        export_morph=False,export_cameras=False,export_lights=False,
        export_extras=False,export_vertex_color='NONE')
    raw = path.read_bytes()
    n, tag = struct.unpack_from('<II',raw,12)
    doc = json.loads(raw[20:20+n])
    assert len(doc['nodes']) == len(doc['meshes']) == 16
    order = sorted(range(16),key=lambda i:doc['meshes'][i]['name'])
    mapping = {old:new for new,old in enumerate(order)}
    doc['meshes'] = [doc['meshes'][i] for i in order]
    for node in doc['nodes']:
        node['mesh'] = mapping[node['mesh']]
        assert not any(key in node for key in ('translation','rotation','scale','children','matrix'))
    doc['nodes'].sort(key=lambda node:node['mesh'])
    doc['scenes'][0]['nodes'] = list(range(16))
    encoded = json.dumps(doc,separators=(',',':')).encode()
    encoded += b' '*(-len(encoded)%4)
    tail = raw[20+n:]
    path.write_bytes(struct.pack('<III',0x46546c67,2,20+len(encoded)+len(tail))
        +struct.pack('<II',len(encoded),tag)+encoded+tail)
    report = {'asset_id':'voxys-far-shadow-proxies-r01',
        'village_provenance_sha256':sha(args.village/'provenance.json'),
        'blacksmith_collision_sha256':sha(args.blacksmith/'collision.json'),
        'recipe_sha256':sha(__file__), 'glb_sha256':sha(path),
        'blend_sha256':sha(output/'shadow-proxies.blend'),
        'triangles':counts}
    (output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    print('SHADOW_PROXIES_COMPLETE',sum(counts),flush=True)
    os._exit(0)


if __name__ == '__main__':
    main()
