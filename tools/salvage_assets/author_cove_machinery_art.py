"""Original molded Cove helm/winch presentations; background export, no images.

Run from the repository or its isolated overlay. Canonical metadata is copied
from the installed r04 machinery; only presentation identities/sources change.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import sys

import bpy

ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / 'AGENTS.md').is_file() and (p / 'tools/salvage_assets').is_dir())
sys.path.insert(0, str(ROOT / 'tools/salvage_assets'))
import author_functional_kit as art
import author_cove_toy_art as toy

PARTS = ('helm', 'winch')
VISUAL_IDS = {'helm': 400, 'winch': 500}
PALETTE = toy.PALETTE


def helm(lod, part):
    m = art.Model('helm', lod)
    foot = m.box('console_foot', (0, -.40, 0), (.5, .08, .5), 'teal', .022)
    column = m.box('console_column', (0, -.13, -.12), (.19, .22, .24), 'cream', .035)
    # The original keyed mounting well passes through both foot and column.
    m.mounts(part, [foot, column])
    m.box('console_head', (0, .14, -.08), (.42, .13, .30), 'cream', .04)
    m.box('dashboard_teal_inset', (0, .276, -.14), (.31, .006, .18), 'teal', .003)
    for x in (-.15, .15):
        m.cylinder('gauge_bezel', (x, .289, -.14), .085, .014, 1, 'slate')
        m.cylinder('gauge_face', (x, .299, -.14), .067, .009, 1, 'cream')
        if lod < 2:
            m.box('gauge_needle', (x + (.013 if x < 0 else -.013), .305, -.14),
                  (.037, .002, .006), 'coral' if x < 0 else 'teal', .001)
    # Rubber rim and three broad molded spokes retain the installed wheel
    # center, radius and operator clearance. No change to control semantics.
    m.ring('steering_wheel', (0, .25, .30), .18, .027, 2, 'rubber')
    m.cylinder('steering_hub', (0, .25, .25), .060, .15, 2, 'steel')
    m.box('wheel_crossbar', (0, .25, .30), (.16, .018, .018), 'teal', .004)
    m.box('wheel_lower_spoke', (0, .17, .30), (.018, .08, .018), 'teal', .004)
    m.cylinder('wheel_orange_cap', (0, .25, .332), .052, .018, 2, 'coral')
    for side in (-1, 1):
        m.box('console_side_inset', (side * .422, .11, -.08), (.006, .077, .205), 'teal', .002)
        if lod == 0:
            m.cylinder('console_side_boss', (side * .430, .11, -.08), .042, .012, 0, 'coral')
        for z in (-.34, .34):
            m.cylinder('foot_molded_stud', (side * .34, -.313, z), .061, .018, 1, 'cream')
    m.box('column_badge', (0, -.11, .124), (.10, .12, .006), 'coral', .002)
    return m


def winch(lod, part):
    m = art.Model('winch', lod)
    foot = m.box('winch_foot', (0, -.88, 0), (.5, .08, .5), 'teal', .022)
    m.mounts(part, [foot])
    for side in (-1, 1):
        # Exact cheek/drum/flange envelope retains the checked 10 mm gap.
        m.box('mast_cheek', (side * .36, -.03, 0), (.09, .77, .30), 'cream', .025)
        m.box('cheek_teal_inset', (side * .454, -.03, 0), (.006, .59, .235), 'teal', .002)
        m.cylinder('axle_bearing', (side * .467, .12, 0), .100, .030, 0, 'cream')
        m.cylinder('axle_orange_cap', (side * .487, .12, 0), .061, .012, 0, 'coral')
        for z in (-.34, .34):
            m.cylinder('foot_molded_stud', (side * .34, -.793, z), .061, .018, 1, 'cream')
        if lod < 2:
            for y in (-.49, .51):
                m.cylinder('cheek_fastener', (side * .465, y, 0), .029, .008, 0, 'steel')
    m.cylinder('cable_drum', (0, .12, 0), .28, .42, 0, 'rubber')
    for x in (-.235, .235):
        m.cylinder('drum_flange', (x, .12, 0), .36, .05, 0, 'cream')
    if lod < 2:
        count = 7 if lod == 0 else 5
        for i in range(count):
            m.ring('cable_wrap', (-.18 + i * .36 / (count - 1), .12, 0), .28, .018, 0, 'rubber')
    m.box('fairlead_mount', (0, .80, -.36), (.25, .10, .10), 'teal', .025)
    m.ring('fairlead', (0, .80, -.48), .085, .024, 2, 'steel')
    m.box('fairlead_orange_badge', (0, .802, -.254), (.13, .056, .006), 'coral', .003)
    return m


def original_bounds(name):
    low, high = [float('inf')] * 3, [-float('inf')] * 3
    for lod in range(3):
        doc = art.base.glb_json(ROOT / f'data/salvage/material-calibration/r04/kit/{name}/source/{name}-lod-{lod}.glb')
        if any(set(node) - {'mesh', 'name'} for node in doc['nodes']):
            raise ValueError('baseline source requires hierarchy-aware bounds')
        for mesh in doc['meshes']:
            for primitive in mesh['primitives']:
                accessor = doc['accessors'][primitive['attributes']['POSITION']]
                # Declared render-to-canonical rotation 12 negates X and Z.
                a, b = accessor['min'], accessor['max']
                minimum, maximum = [-b[0], a[1], -b[2]], [-a[0], b[1], -a[2]]
                low = [min(a, b) for a, b in zip(low, minimum)]
                high = [max(a, b) for a, b in zip(high, maximum)]
    return dict(minimum=low, maximum=high)


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
    for name in PARTS:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.preferences.filepaths.file_preview_type = 'NONE'
        bpy.context.scene.unit_settings.system = 'METRIC'
        source_name = f'data/salvage/material-calibration/r04/kit/{name}/source/{name}.gameplay.json'
        original = json.loads((ROOT / source_name).read_text())
        metadata = copy.deepcopy(original)
        source = out / name / 'source'
        source.mkdir(parents=True)
        baseline = original_bounds(name)
        records, objects = [], []
        for lod, binding in enumerate(metadata['lods']):
            model = (helm if name == 'helm' else winch)(lod, metadata['part'])
            obj, checks = model.finish(toy.materials(lod), art.shading.ANALYTIC, metric_uv=False)
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
            vertex_limit, triangle_limit = ((24000,12000), (12000,6000), (6000,3000))[lod]
            if vertices > vertex_limit or triangles > triangle_limit or len(doc['meshes']) != 1 or len(primitives) > 6:
                raise ValueError(f'{name} LOD {lod}: geometry/draw budget exceeded')
            if doc.get('images') or any('TEXCOORD_0' in p['attributes'] or 'TANGENT' in p['attributes'] for p in primitives):
                raise ValueError('solid materials must not carry textures, UV seams or tangents')
            points = [art.geo.canonical(obj.matrix_world @ v.co) for v in obj.data.vertices]
            bounds = dict(minimum=[min(p[k] for p in points) for k in range(3)],
                          maximum=[max(p[k] for p in points) for k in range(3)])
            if any(bounds['minimum'][k] < baseline['minimum'][k] - .02
                   or bounds['maximum'][k] > baseline['maximum'][k] + .02 for k in range(3)):
                raise ValueError(f'{name} LOD {lod}: exceeds original bounds plus 2 cm')
            binding['asset'] = dict(namespace=b'voxys-toy-art-v1'.hex(), counter=str(VISUAL_IDS[name]+lod+1), version=1)
            binding['source'].update(file=path.name, bytes=path.stat().st_size, sha256=art.base.sha256(path))
            records.append(dict(lod=lod, vertices=vertices, triangles=triangles, draws=len(primitives),
                                bounds=bounds, geometry=checks, source_sha256=art.base.sha256(path)))
            objects.append(obj)
            obj.hide_set(True)
            obj.hide_render = True
        assert {k:v for k,v in metadata.items() if k!='lods'} == {k:v for k,v in original.items() if k!='lods'}
        for before, after in zip(original['lods'], metadata['lods']):
            assert before['id'] == after['id'] and before['minimum_screen_height_pixels'] == after['minimum_screen_height_pixels']
            assert before['source']['to_canonical_rotation'] == after['source']['to_canonical_rotation']
        (source / f'{name}.gameplay.json').write_text(json.dumps(metadata, indent=2) + '\n')
        objects[0].hide_set(False)
        objects[0].hide_render = False
        bpy.ops.wm.save_as_mainfile(filepath=str(source / f'{name}.blend'), compress=True)
        recipes = (__file__, art.__file__, toy.__file__, art.geo.__file__, art.geo.params.__file__,
                   art.shading.__file__, art.metric.__file__, art.base.__file__)
        provenance = dict(schema=1, status='offline presentation candidate; runtime admission and visual review remain',
            original_metadata=source_name, original_metadata_sha256=art.base.sha256(ROOT / source_name),
            canonical_part_unchanged=True, baseline_bounds=baseline, maximum_bounds_expansion_metres=.02,
            blender_version=bpy.app.version_string, blender_build_hash=bpy.app.build_hash.decode(),
            palette_srgb=PALETTE, lods=records,
            inputs={('tools/salvage_assets/author_cove_machinery_art.py'
                     if Path(p).resolve()==Path(__file__).resolve() else str(Path(p).relative_to(ROOT))):
                    art.base.sha256(Path(p)) for p in recipes},
            provenance='Original procedural geometry and solid PBR factors. No external models, image textures, UV charts or rendered captures.',
            limitations=['Static mechanical presentation; wheel/drum animation is not implemented.',
                         'Geometry checks and cook do not establish visual approval or runtime gameplay acceptance.'])
        (out / name / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        print('COVE_MACHINERY', name, json.dumps([{k:v for k,v in r.items() if k!='geometry'} for r in records]), flush=True)


if __name__ == '__main__':
    main()
