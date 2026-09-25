import gzip
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from publish_graphics_recipe import portable_recipe


class GraphicsRecipeTest(unittest.TestCase):
    def test_only_matching_compute_sources_are_portable(self):
        with tempfile.TemporaryDirectory() as folder:
            shaders = Path(folder)
            (shaders / 'physics.wgsl').write_text('production')
            (shaders / 'physics_narrow_phase.wgsl').write_text('hardware')
            recipe = {'resources': [
                {'kind': 'createShaderModule', 'descriptor': {'label': 'physics.wgsl', 'code': 'production'}},
                {'kind': 'createShaderModule', 'descriptor': {'label': 'physics.wgsl', 'code': 'software compatibility'}},
                {'kind': 'createBindGroupLayout', 'descriptor': {'entries': []}},
                {'kind': 'createPipelineLayout', 'descriptor': {'bindGroupLayouts': [{'$gpu': 2}]}},
                {'kind': 'createShaderModule', 'descriptor': {'label': 'physics_narrow_phase.wgsl', 'code': 'hardware'}},
            ], 'pipelines': [
                {'kind': 'createComputePipelineAsync', 'descriptor': {'compute': {'module': {'$gpu': 0}}, 'layout': {'$gpu': 3}}},
                {'kind': 'createComputePipelineAsync', 'descriptor': {'compute': {'module': {'$gpu': 1}}, 'layout': {'$gpu': 3}}},
                {'kind': 'createRenderPipelineAsync', 'descriptor': {'vertex': {'module': {'$gpu': 0}}}},
                {'kind': 'createComputePipelineAsync', 'descriptor': {'compute': {'module': {'$gpu': 4}}, 'layout': {'$gpu': 3}}},
            ]}
            portable = portable_recipe(recipe, shaders)
            self.assertEqual(len(portable['pipelines']), 2)
            self.assertEqual(len(portable['resources']), 4)
            refs = portable['resources']
            desc = portable['pipelines'][0]['descriptor']
            self.assertEqual(refs[desc['compute']['module']['$gpu']]['descriptor']['code'], 'production')
            layout = refs[desc['layout']['$gpu']]
            self.assertEqual(refs[layout['descriptor']['bindGroupLayouts'][0]['$gpu']]['kind'], 'createBindGroupLayout')

    def test_known_software_collision_source_exports_exact_hardware_program(self):
        with tempfile.TemporaryDirectory() as folder:
            shaders = Path(folder)
            (shaders / 'physics_narrow_phase.wgsl').write_text('hardware collision')
            (shaders / 'physics_narrow_phase_compat.wgsl').write_text('software collision')
            recipe = {'resources': [
                {'kind': 'createShaderModule', 'descriptor': {
                    'label': 'physics_narrow_phase.wgsl', 'code': 'software collision'}},
                {'kind': 'createBindGroupLayout', 'descriptor': {'entries': [
                    {'binding': 16, 'visibility': 4, 'buffer': {'type': 'uniform', 'minBindingSize': 128}}]}},
                {'kind': 'createPipelineLayout', 'descriptor': {'bindGroupLayouts': [{'$gpu': 1}]}},
            ], 'pipelines': [
                {'kind': 'createComputePipelineAsync', 'costMs': 5000, 'descriptor': {
                    'compute': {'module': {'$gpu': 0}, 'entryPoint': 'narrow_box_box_128',
                                'constants': {'AUTHORED_PAIR_PASS': pass_id}},
                    'layout': {'$gpu': 2}}} for pass_id in (0, 1)
            ]}
            original = json.dumps(recipe)
            published = portable_recipe(recipe, shaders)
            self.assertEqual(json.dumps(recipe), original, 'publishing must not rewrite the capture')
            expected = json.loads(original)
            expected['resources'][0]['descriptor']['code'] = 'hardware collision'
            self.assertEqual(published, expected, 'preserve every layout, entry point and specialization')

            # Never promote an unknown/stale compatibility source just because
            # its diagnostic label matches the collision module.
            recipe['resources'][0]['descriptor']['code'] = 'outdated software collision'
            with self.assertRaisesRegex(ValueError, 'No portable compute'):
                portable_recipe(recipe, shaders)

    def test_software_capture_matches_all_file_backed_hardware_compute_descriptors(self):
        # Real release captures exercise all 23 collision variants, their
        # bindings, and both authored passes, independently of synthetic data.
        root = Path(__file__).resolve().parents[1]
        evidence = root / 'docs/performance/graphics-startup-20260925/overlap'
        with gzip.open(evidence / 'software-headless-control-recipe.json.gz', 'rt') as stream:
            software = json.load(stream)
        with gzip.open(evidence / 'page-overlap-final-release-graphics-recipe.json.gz', 'rt') as stream:
            hardware = json.load(stream)
        with tempfile.TemporaryDirectory() as folder:
            shaders = Path(folder)
            for row in hardware['resources']:
                if row['kind'] == 'createShaderModule':
                    name = row['descriptor']['label']
                    self.assertEqual(Path(name).name, name)
                    if name.endswith('.wgsl'):
                        (shaders / name).write_text(row['descriptor']['code'])
            narrow = next(row['descriptor']['code'] for row in software['resources']
                          if row['kind'] == 'createShaderModule'
                          and row['descriptor']['label'] == 'physics_narrow_phase.wgsl')
            (shaders / 'physics_narrow_phase_compat.wgsl').write_text(narrow)
            published = portable_recipe(software, shaders)

        def programs(recipe):
            def expand(value):
                if isinstance(value, list):
                    return [expand(item) for item in value]
                if not isinstance(value, dict):
                    return value
                if '$gpu' in value:
                    return expand(recipe['resources'][value['$gpu']])
                return {key: expand(item) for key, item in value.items() if key != 'label'}
            return {json.dumps(expand({'kind': row['kind'], 'descriptor': row['descriptor']}), sort_keys=True)
                    for row in recipe['pipelines'] if row['kind'] == 'createComputePipelineAsync'
                    and recipe['resources'][row['descriptor']['compute']['module']['$gpu']]['descriptor']['label'].endswith('.wgsl')}

        self.assertEqual(len(published['pipelines']), 158)
        self.assertEqual(programs(published), programs(hardware))

    def test_publisher_requires_the_exact_release_and_updates_the_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            site = Path(folder)
            manifest = {'voxy_wasm.wasm': {'sha256': 'a' * 64}, 'voxy_wasm.data': {'sha256': 'b' * 64}}
            index = site / 'index.html'
            index.write_text("<script>window.voxyReleaseFiles = '" + json.dumps(manifest) + "';</script>")
            (site / 'physics.wgsl').write_text('shader')
            recipe = {'schema': 1, 'release': 'a' * 64 + ':' + 'b' * 64, 'experience': 'build',
                      'resources': [{'kind': 'createShaderModule', 'descriptor': {'label': 'physics.wgsl', 'code': 'shader'}}],
                      'pipelines': [{'kind': 'createComputePipelineAsync', 'descriptor': {'compute': {'module': {'$gpu': 0}}}}]}
            capture = site / 'capture.json'
            capture.write_text(json.dumps(recipe))
            command = ['python3', str(Path(__file__).with_name('publish_graphics_recipe.py')), str(site), str(capture), '--shaders', str(site)]
            subprocess.run(command, check=True, capture_output=True)
            self.assertIn('voxy_graphics.json', index.read_text())
            published = (site / 'voxy_graphics.json').read_bytes()
            recipe['release'] = 'stale'
            capture.write_text(json.dumps(recipe))
            failed = subprocess.run(command, capture_output=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertEqual((site / 'voxy_graphics.json').read_bytes(), published)


if __name__ == '__main__':
    unittest.main()
