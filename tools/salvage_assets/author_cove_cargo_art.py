"""Original molded Cove generator/cradle presentations; background export, no images.

Run from the repository or its isolated overlay. Canonical metadata is copied
from the installed r09 cargo fixtures; only presentation identities/sources change.
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

PARTS = ('generator', 'cradle')
VISUAL_IDS = {'generator': 1000, 'cradle': 1100}
PALETTE = toy.PALETTE


def model(name, lod, part):
    # Inherit the exact installed recipe for every functional surface. Added
    # relief remains outside the open cradle and the generator lift-eye seat.
    m = art.geometry(name, lod, part)
    protected_prefixes = (('tow_eye', 'eye_mount', 'cargo_latch_pin') if name == 'generator'
                          else ('cradle_floor', 'latch_pedestal', 'latch_housing'))
    def signature(obj):
        points = [art.geo.canonical(obj.matrix_world @ v.co) for v in obj.data.vertices]
        return dict(vertices=len(points), faces=len(obj.data.polygons),
                    minimum=[min(p[k] for p in points) for k in range(3)],
                    maximum=[max(p[k] for p in points) for k in range(3)],
                    geometry_sha256=__import__('hashlib').sha256(json.dumps(
                        dict(points=points, faces=[list(f.vertices) for f in obj.data.polygons]),
                        sort_keys=True, separators=(',', ':')).encode()).hexdigest())
    protected = {o.name: signature(o) for o in m.objects if o.name.startswith(protected_prefixes)}
    original_count = len(m.objects)
    for obj in m.objects:
        if name == 'generator' and obj.name.startswith('service_panel'):
            obj['palette_region'] = 'teal'
        elif name == 'generator' and obj.name.startswith('vent_louver'):
            obj['palette_region'] = 'teal'
        elif name == 'cradle' and obj.name.startswith('cargo_runner'):
            obj['palette_region'] = 'cream'
    if name == 'generator':
        for x in (-.8, .8):
            for z in (-.7, .7):
                m.box('cage_orange_cap', (x, .643, z), (.066, .006, .066), 'coral', .002)
        for x in (-.4, .4):
            for z in (-.24, .24):
                m.cylinder('roof_molded_stud', (x, .404, z), .065, .020, 1, 'cream')
        m.box('front_orange_identity_bar', (0, -.37, -.591), (.25, .035, .014), 'coral', .008)
        m.cylinder('service_orange_control', (.717, .12, .12), .065, .022, 0, 'coral')
        m.box('service_cream_inset', (.710, -.12, -.06), (.006, .065, .23), 'cream', .003)
        if lod < 2:
            for y in (-.23, .21):
                for z in (-.29, .29):
                    m.cylinder('service_fastener', (.712, y, z), .025, .012, 0, 'steel')
    else:
        for side in (-1, 1):
            m.box('runner_teal_outer_inset', (side * 1.006, 0, 0), (.006, .205, .74), 'teal', .003)
            for z in (-1.006, 1.006):
                m.box('runner_orange_end', (side * .88, 0, z), (.085, .22, .006), 'coral', .003)
            for z in (-.72, .72):
                m.cylinder('runner_molded_stud', (side * .88, .324, z), .07, .012, 1, 'cream')
    additions = [signature(o) for o in m.objects[original_count:]]
    for name0, before in protected.items():
        assert signature(next(o for o in m.objects if o.name == name0)) == before
    for bounds in additions:
        low, high = bounds['minimum'], bounds['maximum']
        if name == 'cradle':
            if not (high[0] <= -.76 or low[0] >= .76):
                raise ValueError('new cradle detail obstructs the original open interior')
        elif not (high[0] <= -.30 or low[0] >= .30 or high[1] < .465):
            raise ValueError('new generator detail enters the lift-eye aperture/clearance')
    m.interface_evidence = dict(protected_geometry=protected,
        new_detail_count=len(additions), additions=additions,
        cradle_interior_x_metres=[-.76, .76] if name == 'cradle' else None,
        generator_eye_center_metres=[v / 50 for v in part['sockets'][0]['frame']['translation_ticks']] if name == 'generator' else None,
        generator_eye_inner_radius_metres=.055 if name == 'generator' else None)
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
        bpy.context.scene.unit_settings.system = 'METRIC'
        source_name = f'data/salvage/functional-kit/r09/{name}/source/{name}.gameplay.json'
        original = json.loads((ROOT / source_name).read_text())
        metadata = copy.deepcopy(original)
        source = out / name / 'source'
        source.mkdir(parents=True)
        baseline = original_bounds(name)
        records, objects = [], []
        for lod, binding in enumerate(metadata['lods']):
            built = model(name, lod, metadata['part'])
            obj, checks = built.finish(toy.materials(lod), art.shading.ANALYTIC, metric_uv=False)
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
                                bounds=bounds, geometry=checks, interfaces=built.interface_evidence, source_sha256=art.base.sha256(path)))
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
            limitations=['Presentation-only generator and cradle; no new cargo latch gameplay or animation.',
                         'Geometry checks and cook do not establish visual approval or runtime gameplay acceptance.'])
        (out / name / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        print('COVE_CARGO_ART', name, json.dumps([{k:v for k,v in r.items() if k!='geometry'} for r in records]), flush=True)


if __name__ == '__main__':
    main()
