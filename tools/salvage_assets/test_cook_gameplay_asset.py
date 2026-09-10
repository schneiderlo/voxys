#!/usr/bin/env python3
"""CPU-only integration tests using the actual existing GLB converter and validator."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

# Bazel provides declared inputs in a runfiles tree. Resolving __file__ first
# follows source symlinks out of that tree and can conceal missing test data.
ROOT = (Path(os.environ['TEST_SRCDIR']) / os.environ['TEST_WORKSPACE']
        if 'TEST_SRCDIR' in os.environ else Path(__file__).resolve().parents[2])
MODULE = ROOT / 'tools/salvage_assets/cook_gameplay_asset.py'
spec = importlib.util.spec_from_file_location('cook', MODULE)
cook = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cook)
CONVERTER = Path(os.environ.get('SALVAGE_CONVERTER', ROOT / 'build-salvage-native/bin/gltf_vmesh_tool')).resolve()
VALIDATOR = Path(os.environ.get('SALVAGE_VALIDATOR', '/tmp/salvage-sidecar-build/gameplay_sidecar_tool')).resolve()


def glb(document, binary=b'\0\0\0\0'):
    text = json.dumps(document, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    body = struct.pack('<II', len(text), cook.GLB_JSON) + text
    if binary: body += struct.pack('<II', len(binary), cook.GLB_BIN) + binary
    return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


def files(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}


class CookTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CONVERTER.is_file() or not VALIDATOR.is_file():
            raise RuntimeError('Build the actual converter and standalone sidecar validator first; these tests must not silently skip.')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='salvage-cook-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sources = self.root / 'sources'
        self.sources.mkdir()
        shutil.copyfile(ROOT / 'data/salvage/authoring_probe/probe.glb', self.sources / 'probe.glb')
        self.sidecar = self.root / 'part.json'
        self.data = json.loads((ROOT / 'tools/salvage_assets/fixtures/probe.gameplay.json').read_text())
        self.write()

    def write(self):
        self.sidecar.write_text(json.dumps(self.data))

    def run_cook(self, output='bundle', converter=CONVERTER, validator=VALIDATOR):
        return cook.cook(self.sidecar, self.sources, self.root / output, converter, validator)

    def assert_clean_failure(self, converter=CONVERTER, validator=VALIDATOR):
        with self.assertRaises((OSError, ValueError, KeyError, TypeError)):
            self.run_cook(converter=converter, validator=validator)
        self.assertFalse((self.root / 'bundle').exists())
        self.assertEqual(list(self.root.glob('.salvage-cook-*')), [])

    def fake_tool(self, body):
        path = self.root / 'fake-tool'
        path.write_text('#!/bin/sh\nset -eu\n' + body + '\n')
        path.chmod(0o755)
        return path

    def test_actual_converter_cooks_identically_and_preserves_metadata(self):
        first = self.run_cook('a'); second = self.run_cook('b')
        self.assertEqual(first, second)
        self.assertEqual(files(self.root / 'a'), files(self.root / 'b'))
        normalized = json.loads((self.root / 'a/gameplay.json').read_text())
        self.assertEqual(normalized, self.data)
        self.assertEqual(normalized['part']['sockets'][0]['id'], '9007199254740995')
        self.assertEqual(normalized['tool_anchors'][0]['frame']['translation_ticks'], [0, 32, -90])
        self.assertEqual(len(list((self.root / 'a').iterdir())), 3)
        for lod in first['lods']:
            data = (self.root / 'a' / lod['file']).read_bytes()
            self.assertEqual(lod['sha256'], cook.digest(data))
            self.assertEqual(lod['bytes'], len(data))
            self.assertTrue(data.startswith(b'VOXYMESH'))
        self.assertEqual(first['normalized_sidecar']['sha256'], cook.digest((self.root / 'a/gameplay.json').read_bytes()))
        self.assertEqual(first['converter']['sha256'], cook.digest(CONVERTER.read_bytes()))
        self.assertEqual(first['converter']['profile'], 'salvage-rigid-v1')
        self.assertEqual(first['converter']['interface'], '--profile salvage-rigid-v1 input.glb output.vmesh')
        self.assertEqual(first['cooker']['sha256'], cook.digest(MODULE.read_bytes()))

    def change_source_document(self, change):
        source = (self.sources / 'probe.glb').read_bytes()
        json_size = struct.unpack_from('<I', source, 12)[0]
        document = json.loads(source[20:20 + json_size])
        binary_size, kind = struct.unpack_from('<II', source, 20 + json_size)
        self.assertEqual(kind, cook.GLB_BIN)
        change(document)
        changed = glb(document, source[28 + json_size:28 + json_size + binary_size])
        (self.sources / 'probe.glb').write_bytes(changed)
        self.data['lods'][0]['source'].update(sha256=cook.digest(changed), bytes=len(changed))
        self.write()

    def test_actual_strict_converter_rejects_unsupported_required_extension(self):
        self.change_source_document(lambda doc: doc.update(
            extensionsUsed=['VOXY_unsupported_appearance'],
            extensionsRequired=['VOXY_unsupported_appearance']))
        self.assert_clean_failure()

    def test_actual_strict_converter_rejects_unsupported_optional_appearance(self):
        def change(document):
            document['materials'][0]['extensions'] = {'KHR_materials_clearcoat': {'clearcoatFactor': 1.0}}
            document['extensionsUsed'] = ['KHR_materials_clearcoat']
        self.change_source_document(change)
        self.assert_clean_failure()

    def test_rejected_profile_never_retries_without_the_profile(self):
        trace = self.root / 'invocations'
        body = ('printf "%s\\n" "$#" "$1" "$2" >> ' + shlex.quote(str(trace))
                + '\nprintf "unsupported profile\\n"\nexit 64')
        tool = self.fake_tool(body)
        self.assert_clean_failure(converter=tool)
        self.assertEqual(trace.read_text().splitlines(), ['4', '--profile', 'salvage-rigid-v1'])

    def test_stale_glb_digest_rejects_even_same_byte_length(self):
        p = self.sources / 'probe.glb'; data = bytearray(p.read_bytes()); data[-1] ^= 1; p.write_bytes(data)
        self.assert_clean_failure()

    def test_exported_node_reordering_requires_rebind_but_never_changes_gameplay_ids(self):
        original = self.run_cook('original')
        source = (self.sources / 'probe.glb').read_bytes()
        json_size = struct.unpack_from('<I', source, 12)[0]
        document = json.loads(source[20:20 + json_size])
        binary_size, kind = struct.unpack_from('<II', source, 20 + json_size)
        self.assertEqual(kind, cook.GLB_BIN)
        binary = source[28 + json_size:28 + json_size + binary_size]
        count = len(document['nodes'])
        self.assertGreater(count, 1)
        document['nodes'].reverse()
        for scene in document['scenes']:
            scene['nodes'] = [count - 1 - index for index in scene['nodes']]
        for node in document['nodes']:
            if 'children' in node:
                node['children'] = [count - 1 - index for index in node['children']]
        changed = glb(document, binary)
        (self.sources / 'probe.glb').write_bytes(changed)
        self.assert_clean_failure()  # Re-export invalidates the old digest.
        self.data['lods'][0]['source'].update(sha256=cook.digest(changed), bytes=len(changed))
        self.write()
        updated = self.run_cook('reordered')
        old_metadata = json.loads((self.root / 'original/gameplay.json').read_text())
        new_metadata = json.loads((self.root / 'reordered/gameplay.json').read_text())
        self.assertEqual(old_metadata['part'], new_metadata['part'])
        self.assertEqual(old_metadata['tool_anchors'], new_metadata['tool_anchors'])
        self.assertEqual(original['lods'][0]['id'], updated['lods'][0]['id'])
        self.assertNotEqual(original['lods'][0]['sha256'], updated['lods'][0]['sha256'])

    def test_source_size_and_symlink_cannot_bypass_binding(self):
        self.data['lods'][0]['source']['bytes'] += 4; self.write(); self.assert_clean_failure()
        self.data['lods'][0]['source']['bytes'] -= 4; self.write()
        path = self.sources / 'probe.glb'; path.unlink(); path.symlink_to(ROOT / 'data/salvage/authoring_probe/probe.glb')
        self.assert_clean_failure()

    def test_fifo_sidecar_and_source_reject_without_waiting_for_a_writer(self):
        fifo = self.root / 'pipe.json'
        os.mkfifo(fifo)
        arguments = [sys.executable, str(MODULE), '--sidecar', str(fifo),
                     '--sources', str(self.sources), '--output', str(self.root / 'bundle'),
                     '--converter', str(CONVERTER), '--validator', str(VALIDATOR)]
        result = subprocess.run(arguments, capture_output=True, timeout=2)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'nonregular', result.stderr)
        arguments[arguments.index('--sidecar') + 1] = str(self.sidecar)
        source = self.sources / 'probe.glb'; source.unlink(); os.mkfifo(source)
        result = subprocess.run(arguments, capture_output=True, timeout=2)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'nonregular', result.stderr)
        self.assertFalse((self.root / 'bundle').exists())
        self.assertEqual(list(self.root.glob('.salvage-cook-*')), [])

    def test_source_path_escape_and_lossy_lod_ids_reject(self):
        self.data['lods'][0]['source']['file'] = '../probe.glb'; self.write(); self.assert_clean_failure()
        self.data['lods'][0]['source']['file'] = 'probe.glb'; self.data['lods'][0]['id'] = 9007199254740997
        self.write(); self.assert_clean_failure()

    def test_external_buffer_and_image_uris_reject_before_converter(self):
        for category in ('buffers', 'images'):
            for uri in ('other.bin', '/tmp/other.bin', 'https://example.invalid/image.png'):
                data = glb({'asset': {'version': '2.0'}, category: [{'uri': uri}]})
                with self.assertRaisesRegex(ValueError, 'external'): cook.self_contained_glb(data)
        embedded = glb({'asset': {'version': '2.0'}, 'buffers': [{'uri': 'data:application/octet-stream;base64,AAAAAA=='}]})
        self.assertIn('buffers', cook.self_contained_glb(embedded))

    def test_hash_bound_external_dependency_is_still_rejected(self):
        data = glb({'asset': {'version': '2.0'}, 'images': [{'uri': 'outside.png'}]})
        (self.sources / 'probe.glb').write_bytes(data)
        self.data['lods'][0]['source'].update(sha256=cook.digest(data), bytes=len(data)); self.write()
        self.assert_clean_failure()

    def test_data_uri_spelling_cannot_fall_back_to_external_file_loading(self):
        for uri in ('data:relative.bin', 'data:application/custom;base64,AAAA',
                    'data:image/png,not-base64', 'data:image/png;base64,%%%!',
                    'data:image/png;base64,'):
            data = glb({'asset': {'version': '2.0'}, 'images': [{'uri': uri}]})
            with self.assertRaises(ValueError): cook.self_contained_glb(data)

    def test_actual_converter_empty_glb_cannot_publish_an_empty_visual(self):
        data = glb({'asset': {'version': '2.0'}}, binary=b'')
        (self.sources / 'probe.glb').write_bytes(data)
        self.data['lods'][0]['source'].update(sha256=cook.digest(data), bytes=len(data)); self.write()
        self.assert_clean_failure()

    def test_malformed_glb_and_json_are_bounded(self):
        good = glb({'asset': {'version': '2.0'}})
        for data in (good[:-1], b'bad', good + b'extra', good[:4] + struct.pack('<I', 1) + good[8:]):
            with self.assertRaises(ValueError): cook.self_contained_glb(data)
        for data in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":1e999}', b'[' * 40 + b'0' + b']' * 40):
            with self.assertRaises(ValueError): cook.strict_json(data)
        self.sidecar.write_bytes(b' ' * (cook.MAX_SIDECAR + 1)); self.assert_clean_failure()

    def test_cpp_validation_rejects_unknown_fields_after_real_cook(self):
        self.data['part']['unknown_future_setting'] = 123; self.write(); self.assert_clean_failure()

    def test_cpp_shared_physical_validation_rejects_invalid_mass(self):
        self.data['part']['mass']['dry_mass_kg'] = -3; self.write(); self.assert_clean_failure()

    def test_partial_converter_failure_does_not_publish(self):
        tool = self.fake_tool('printf partial > "$4"\nexit 7')
        self.assert_clean_failure(converter=tool)

    def test_zero_exit_corrupt_vmesh_is_rejected_by_actual_cpp_reader(self):
        tool = self.fake_tool('printf not-vmesh > "$4"')
        self.assert_clean_failure(converter=tool)

    def test_parsed_vmesh_with_invalid_triangle_range_or_index_is_not_drawable(self):
        self.run_cook('valid')
        original = (self.root / 'valid/lod-9007199254740997.vmesh').read_bytes()
        mutations = []
        data = bytearray(original)
        submeshes_offset = struct.unpack_from('<Q', data, 80)[0]
        struct.pack_into('<I', data, submeshes_offset + 4, 1)  # Valid byte bounds, not a triangle.
        mutations.append(data)
        data = bytearray(original)
        index_stride = struct.unpack_from('<I', data, 28)[0]
        indices_offset = struct.unpack_from('<Q', data, 72)[0]
        struct.pack_into('<H' if index_stride == 2 else '<I', data, indices_offset,
                         65535 if index_stride == 2 else 4294967295)
        mutations.append(data)
        for data in mutations:
            (self.root / 'malformed.vmesh').write_bytes(data)
            tool = self.fake_tool('cp ' + shlex.quote(str(self.root / 'malformed.vmesh')) + ' "$4"')
            self.assert_clean_failure(converter=tool)

    def test_failed_validator_does_not_publish_valid_mesh_without_metadata(self):
        tool = self.fake_tool('printf partial-metadata\nexit 9')
        self.assert_clean_failure(validator=tool)

    def test_existing_bundles_files_and_empty_directories_are_never_overwritten(self):
        self.run_cook(); before = files(self.root / 'bundle')
        with self.assertRaises(FileExistsError): self.run_cook()
        self.assertEqual(files(self.root / 'bundle'), before)
        for name in ('file', 'empty'):
            target = self.root / name
            target.write_text('keep') if name == 'file' else target.mkdir()
            with self.assertRaises(FileExistsError): self.run_cook(name)
        self.assertEqual((self.root / 'file').read_text(), 'keep')
        self.assertEqual(list((self.root / 'empty').iterdir()), [])

    def test_atomic_publication_refuses_a_destination_created_after_precheck(self):
        stage = self.root / 'stage'; stage.mkdir(); (stage / 'ready').write_text('complete')
        destination = self.root / 'bundle'; destination.mkdir()
        with self.assertRaises(FileExistsError): cook.publish_no_replace(stage, destination)
        self.assertEqual((stage / 'ready').read_text(), 'complete')
        self.assertEqual(list(destination.iterdir()), [])

    def test_cpp_cli_independently_bounds_input_without_python_wrapper(self):
        self.sidecar.write_bytes(b' ' * (cook.MAX_SIDECAR + 1))
        result = subprocess.run([str(VALIDATOR), str(self.sidecar), str(self.root)], capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0); self.assertEqual(result.stdout, b'')
        self.write(); self.data['tool_anchors'] *= 129; self.write()
        result = subprocess.run([str(VALIDATOR), str(self.sidecar), str(self.root)], capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0); self.assertEqual(result.stdout, b'')
        self.assertIn(b'capacity', result.stderr)


if __name__ == '__main__':
    unittest.main()
