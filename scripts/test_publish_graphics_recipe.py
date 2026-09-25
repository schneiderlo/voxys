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
            self.assertEqual(len(portable['pipelines']), 1)
            self.assertEqual(len(portable['resources']), 3)
            refs = portable['resources']
            desc = portable['pipelines'][0]['descriptor']
            self.assertEqual(refs[desc['compute']['module']['$gpu']]['descriptor']['code'], 'production')
            layout = refs[desc['layout']['$gpu']]
            self.assertEqual(refs[layout['descriptor']['bindGroupLayouts'][0]['$gpu']]['kind'], 'createBindGroupLayout')

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
