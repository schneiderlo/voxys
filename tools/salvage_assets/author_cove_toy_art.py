"""Original molded Cove structural art; background Blender, no image capture.

Copies exact installed gameplay metadata. Only hash-bound LOD sources and visual
asset keys change. The separate presentation loader must certify compatibility.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_functional_kit as art

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    'pontoon': 'data/salvage/material-calibration/r02/pontoon-r02/source/pontoon.gameplay.json',
    'beam': 'data/salvage/functional-kit/r09/beam/source/beam.gameplay.json',
    'plate': 'data/salvage/functional-kit/r09/plate/source/plate.gameplay.json',
}
VISUAL_IDS = {'pontoon': 100, 'beam': 200, 'plate': 300}
PALETTE = {'cream': 'F0DDB2', 'teal': '197D86', 'coral': 'ED7942',
           'slate': '253D53', 'steel': 'B5C3BE', 'rubber': '25363A'}


def materials(lod):
    result = {}
    for region, color in PALETTE.items():
        mat = bpy.data.materials.new(f'cove_molded_{region}_lod{lod}')
        mat.use_nodes = True
        mat.use_backface_culling = True
        shader = mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value = tuple(
            art.metric.linear_channel(int(color[i:i + 2], 16)) for i in (0, 2, 4)) + (1.,)
        shader.inputs['Roughness'].default_value = .72 if region == 'rubber' else .32
        shader.inputs['Metallic'].default_value = .8 if region == 'steel' else 0
        result[region] = mat
    return result


def deck(name, lod, part):
    m = art.Model(name, lod)
    half_z = .5 if name == 'beam' else 1.
    # A continuous structural core retains the exact mounting wells. Relief is
    # confined to the outer centimetre; seams are molded panel divisions.
    core = m.box('structural_core', (0, -.005, 0), (2, .155, half_z), 'teal', .018)
    m.mounts(part, [core])
    rows = (0.,) if name == 'beam' else (-.5, .5)
    for column, x in enumerate((-1.5, -.5, .5, 1.5)):
        for z in rows:
            m.box('molded_top_panel', (x, .155, z), (.490, .005, .488), 'cream', .002)
        for z in (-half_z, half_z):
            m.box('side_panel', (x, -.025, z), (.476, .10, .006),
                  'coral' if column == 0 else 'cream', .004)
            if lod < 2:
                m.cylinder('molded_side_boss', (x, -.025, z * (1 + .009 / half_z)),
                           .055, .012, 2, 'teal')
    return m


def pontoon(lod, part):
    spec = json.loads((ROOT / 'data/salvage/material-calibration/r02/pontoon-r02/source/parameters.json').read_text())
    m = art.Model('pontoon', lod)
    shell = m.add(art.geo.shell_geometry(spec, lod), 'cream', 0)
    shell['surface_kind'] = 'machined'
    # Slim side moldings and alternating one-stud panels articulate the hull
    # without changing its functional end taper or any keyed interface.
    for side in (-1, 1):
        for i, z in enumerate((-1.5, -.5, .5, 1.5)):
            # Follow the real bow/stern taper instead of putting a straight
            # floating board across the narrowing hull.
            stations = sorted({z - .455, z, z + .455} |
                              {s for s in (-1.8, -1.4, 1.4, 1.8) if z - .455 < s < z + .455})
            points = []
            for depth in stations:
                x = side * (art.geo.params.section(spec, depth)[0] - .006)
                points.extend((x + dx, -.06 + y, depth)
                              for dx, y in ((-.012, -.14), (.012, -.14), (.012, .14), (-.012, .14)))
            faces = [(3, 2, 1, 0), tuple(range(len(points) - 4, len(points)))]
            faces.extend((n * 4 + k, n * 4 + (k + 1) % 4, (n + 1) * 4 + (k + 1) % 4, (n + 1) * 4 + k)
                         for n in range(len(stations) - 1) for k in range(4))
            panel = m.add(art.geo.object_mesh('hull_side_panel', points, faces),
                          'coral' if i == 0 else 'teal', .005)
            panel['surface_kind'] = 'machined'
        for z in (-.5, .5):
            m.box('top_molded_grip', (side * .36, .481, z),
                  (.04, .007, .455), 'teal', .004)
    return m


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
    for name, source_name in SOURCES.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = 'METRIC'
        original = json.loads((ROOT / source_name).read_text())
        metadata = copy.deepcopy(original)
        source = out / name / 'source'
        source.mkdir(parents=True)
        records, objects = [], []
        for lod, binding in enumerate(metadata['lods']):
            model = pontoon(lod, metadata['part']) if name == 'pontoon' else deck(name, lod, metadata['part'])
            # Solid PBR factors need no UV seams or tangents. Preserve shared
            # vertices and manufactured normals within the live GPU budget.
            obj, checks = model.finish(materials(lod), art.shading.ANALYTIC, metric_uv=False)
            art.geo.activate(obj)
            path = source / f'{name}-lod-{lod}.glb'
            bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
                export_yup=True, export_apply=True, export_materials='EXPORT', export_normals=True,
                export_texcoords=False, export_tangents=False, export_animations=False, export_skins=False,
                export_morph=False, export_cameras=False, export_lights=False, export_extras=False,
                export_vertex_color='NONE', export_all_vertex_colors=False)
            doc = art.base.glb_json(path)
            primitives = [p for mesh in doc['meshes'] for p in mesh['primitives']]
            vertices = sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)
            triangles = sum(doc['accessors'][p['indices']]['count'] // 3 for p in primitives)
            vertex_limit, triangle_limit = ((24000, 12000), (12000, 6000), (6000, 3000))[lod]
            if vertices > vertex_limit or triangles > triangle_limit or len(doc['meshes']) != 1 or len(primitives) > 6:
                raise ValueError(f'{name} LOD {lod}: geometry/draw budget exceeded')
            binding['asset'] = dict(namespace=b'voxys-toy-art-v1'.hex(),
                                    counter=str(VISUAL_IDS[name] + lod + 1), version=1)
            binding['source'].update(file=path.name, bytes=path.stat().st_size, sha256=art.base.sha256(path))
            points = [art.geo.canonical(obj.matrix_world @ v.co) for v in obj.data.vertices]
            records.append(dict(lod=lod, vertices=vertices, triangles=triangles, draws=len(primitives),
                bounds=dict(minimum=[min(p[k] for p in points) for k in range(3)],
                            maximum=[max(p[k] for p in points) for k in range(3)]), geometry=checks))
            objects.append(obj)
            obj.hide_set(True)
            obj.hide_render = True
        assert {k: v for k, v in metadata.items() if k != 'lods'} == {k: v for k, v in original.items() if k != 'lods'}
        (source / f'{name}.gameplay.json').write_text(json.dumps(metadata, indent=2) + '\n')
        objects[0].hide_set(False)
        objects[0].hide_render = False
        bpy.ops.wm.save_as_mainfile(filepath=str(source / f'{name}.blend'), compress=True)
        provenance = dict(schema=1, status='authored candidate; runtime compatibility still required',
            original_metadata=source_name, original_metadata_sha256=art.base.sha256(ROOT / source_name),
            blender_version=bpy.app.version_string, palette_srgb=PALETTE, lods=records,
            inputs={str(Path(p).relative_to(ROOT)): art.base.sha256(Path(p)) for p in
                    (__file__, art.__file__, art.geo.__file__, art.geo.params.__file__, art.shading.__file__)},
            provenance='Original procedural models and solid PBR material factors; no external model, texture, UV chart or image capture')
        (out / name / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        print('COVE_TOY_ART', name, json.dumps(records), flush=True)


if __name__ == '__main__':
    main()
