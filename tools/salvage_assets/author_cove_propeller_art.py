"""Original molded Cove propeller presentation with measured blade/guard clearance; background export, no images.

Run from the repository or its isolated overlay. Canonical metadata is copied
from the installed r09 machinery; only presentation identities/sources change.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path
import sys

import bpy

ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / 'AGENTS.md').is_file() and (p / 'tools/salvage_assets').is_dir())
sys.path.insert(0, str(ROOT / 'tools/salvage_assets'))
import author_functional_kit as art
import author_cove_toy_art as toy

PARTS = ('propeller',)
VISUAL_IDS = {'propeller': 710}
PALETTE = toy.PALETTE


def propeller(lod, part):
    m = art.Model('propeller', lod)
    # The steel hub retains both installed shaft endpoints exactly. The guard
    # and outer bounds stay exact. Blade corners fit inside even the coarsest
    # guard polygon: sqrt(.075^2 + .345^2) < .380*cos(pi/12), gap > 13 mm.
    m.ring('prop_guard', (0, 0, .06), .425, .045, 2, 'teal')
    m.ring('guard_cream_front_molding', (0, 0, .109), .425, .018, 2, 'cream')
    m.cylinder('hub', (0, 0, 0), .12, .64, 2, 'steel')
    m.cylinder('hub_molded_sleeve', (0, 0, .06), .126, .36, 2, 'cream')
    m.cylinder('hub_orange_cap', (0, 0, .323), .105, .014, 2, 'coral')
    for angle in (0, 2 * math.pi / 3, 4 * math.pi / 3):
        blade = m.box('prop_blade', (0, .205, .07), (.075, .140, .04), 'steel', .025)
        # Canonical Z rotation is Blender +Y, matching the existing part recipe.
        blade.rotation_euler.y = angle
    for side in (-1, 1):
        m.box('guard_arm', (side * .34, 0, -.05), (.08, .035, .055), 'teal', .008)
        if lod < 2:
            m.cylinder('guard_orange_boss', (side * .34, 0, .017), .027, .016, 2, 'coral')
    return m


def original_bounds(name):
    low, high = [float('inf')] * 3, [-float('inf')] * 3
    for lod in range(3):
        doc = art.base.glb_json(ROOT / f'data/salvage/functional-kit/r09/{name}/source/{name}-lod-{lod}.glb')
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
        bpy.context.preferences.filepaths.file_preview_type = 'NONE'
        bpy.context.scene.unit_settings.system = 'METRIC'
        source_name = f'data/salvage/functional-kit/r09/{name}/source/{name}.gameplay.json'
        original = json.loads((ROOT / source_name).read_text())
        metadata = copy.deepcopy(original)
        source = out / name / 'source'
        source.mkdir(parents=True)
        baseline = original_bounds(name)
        records, objects = [], []
        for lod, binding in enumerate(metadata['lods']):
            model = propeller(lod, metadata['part'])
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
            inputs={(f'tools/salvage_assets/{Path(p).name}' if Path(p).resolve()==Path(__file__).resolve()
                     else str(Path(p).relative_to(ROOT))):art.base.sha256(Path(p)) for p in recipes},
            provenance='Original procedural geometry and solid PBR factors. No external models, image textures, UV charts or rendered captures.',
            limitations=['Static mechanical presentation; propeller rotation and engine animation are not implemented.',
                         'Geometry checks and cook do not establish visual approval or runtime gameplay acceptance.'])
        (out / name / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        print('COVE_POWER_MACHINERY', name, json.dumps([{k:v for k,v in r.items() if k!='geometry'} for r in records]), flush=True)


if __name__ == '__main__':
    main()
