"""Headless Blender recipe for original studded Cove bricks; no image capture."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_functional_kit as author
import brick_parts as bricks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup')
    out = args.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        parser.error('output must not exist and its parent must exist')
    out.mkdir()
    for name, (columns, rows) in bricks.SIZES.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = 'METRIC'
        directory = out / name
        source = directory / 'source'
        source.mkdir(parents=True)
        material = bpy.data.materials.new(name + '_molded_plastic')
        material.use_nodes = True
        shader = material.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value = bricks.COLORS[name]
        shader.inputs['Roughness'].default_value = .32
        shader.inputs['Metallic'].default_value = 0
        records, objects = [], []
        for lod, (triangle_limit, vertex_limit, _, _) in enumerate(bricks.LOD_LIMITS):
            model = author.Model(name, lod)
            half = (columns * .5 - .02, .48, rows * .5 - .02)
            body = model.box('hollow_brick_shell', (0, 0, 0), half, bevel=.012)
            cutter = author.Model('cavity', lod).box('underside_cavity', (0, -.26, 0),
                (half[0] - .08, .62, half[2] - .08), bevel=0)
            author.geo.boolean(body, cutter, 'DIFFERENCE')
            body['surface_kind'] = 'machined'
            for i, (x, z) in enumerate(bricks.stud_centers(name)):
                model.cylinder(f'round_stud_{i}', (x / 50, .57, z / 50), .30, .18)
            obj, checks = model.finish(material, author.shading.ANALYTIC)
            author.geo.activate(obj)
            path = source / f'{name}-lod-{lod}.glb'
            bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
                export_yup=True, export_apply=True, export_materials='EXPORT', export_normals=True,
                export_texcoords=True, export_tangents=True, export_animations=False, export_skins=False,
                export_morph=False, export_cameras=False, export_lights=False, export_extras=False,
                export_vertex_color='NONE', export_all_vertex_colors=False)
            doc = author.base.glb_json(path)
            vertices = sum(doc['accessors'][p['attributes']['POSITION']]['count']
                           for mesh in doc['meshes'] for p in mesh['primitives'])
            triangles = sum(doc['accessors'][p['indices']]['count'] // 3
                            for mesh in doc['meshes'] for p in mesh['primitives'])
            if len(doc['meshes']) != 1 or len(doc['materials']) != 1 or vertices > vertex_limit or triangles > triangle_limit:
                raise ValueError(f'{name} LOD {lod} exceeds the brick budget')
            # Canonical coordinates, measured from actual post-modifier mesh.
            points = [author.geo.canonical(obj.matrix_world @ v.co) for v in obj.data.vertices]
            bounds = dict(minimum=[min(p[k] for p in points) for k in range(3)],
                          maximum=[max(p[k] for p in points) for k in range(3)])
            if abs(bounds['maximum'][1] - .66) > 1e-5 or abs(bounds['minimum'][1] + .48) > 1e-5:
                raise ValueError('brick height or stud height changed')
            records.append(dict(lod=lod, vertices=vertices, triangles=triangles,
                                bounds_metres=bounds, geometry=checks,
                                sha256=author.base.sha256(path)))
            objects.append(obj)
            obj.hide_render = True
            obj.hide_set(True)
        (source / f'{name}.gameplay.json').write_text(json.dumps(bricks.sidecar(name, source), indent=2) + '\n')
        objects[0].hide_render = False
        objects[0].hide_set(False)
        bpy.ops.wm.save_as_mainfile(filepath=str(source / f'{name}.blend'), compress=True)
        (directory / 'provenance.json').write_text(json.dumps(dict(schema=1,
            status='offline candidate; gameplay admission and controls require separate validation',
            part=bricks.definition(name)['key'], blender_version=bpy.app.version_string,
            lods=records, stud_pitch_metres=1, engaged_stack_spacing_metres=.96,
            stud_height_metres=.18, provenance='Original procedural geometry and solid PBR material; no external assets',
            recipes={Path(p).name: author.base.sha256(Path(p)) for p in
                     (__file__, bricks.__file__, author.__file__, author.geo.__file__, author.shading.__file__)},
            files={str(p.relative_to(directory)): dict(bytes=p.stat().st_size, sha256=author.base.sha256(p))
                   for p in sorted(directory.rglob('*')) if p.is_file()}), indent=2) + '\n')
        print('COVE_BRICK', name, json.dumps(records), flush=True)


if __name__ == '__main__':
    main()
