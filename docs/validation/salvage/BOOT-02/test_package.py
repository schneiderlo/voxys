#!/usr/bin/env python3
"""Run destructive negative controls on temporary copies of a real package.

Usage: python3 test_package.py /path/to/staged/package
The input must already pass check_package.py. It is never changed.
"""

import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest

from check_package import ENTRY, ROOT, check


PACKAGE = Path(sys.argv.pop(1)).resolve()


def link_or_copy(source, destination):
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


class PackageNegativeControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = check(PACKAGE, ROOT, "node")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="voxys-package-negative-")
        self.addCleanup(self.temporary.cleanup)
        self.package = Path(self.temporary.name) / "web"
        shutil.copytree(PACKAGE, self.package, copy_function=link_or_copy)
        self.glue = next(self.package.glob("voxy_wasm*.js"))

    @staticmethod
    def replace_file(path, data):
        # Break the temporary hard link before mutation; preserve the real build.
        path.unlink()
        path.write_bytes(data)

    def rejected(self, pattern):
        with self.assertRaisesRegex(ValueError, pattern):
            check(self.package, ROOT, "node")

    def test_missing_playground_script(self):
        (self.package / "lego_playground.js").unlink()
        self.rejected("Missing served asset.*lego_playground.js")

    def test_missing_island_module(self):
        (self.package / "lego_patch_model.mjs").unlink()
        self.rejected("Missing served asset.*lego_patch_model.mjs")

    def test_stale_shader_parity_source(self):
        path = self.package / "shaders/terrain_raycast.wgsl"
        self.replace_file(path, path.read_bytes() + b"\n// stale package\n")
        self.rejected("Stale served shader: terrain_raycast.wgsl")

    def test_missing_route_config(self):
        # Model a valid package that omitted this config, preserving every other
        # file's content and offsets so only the route contract can reject it.
        text = self.glue.read_text()
        start, end = next((int(start), int(end)) for name, start, end in ENTRY.findall(text)
                          if name == "/lego_world.cfg")
        removed = end - start
        object_pattern = re.compile(r"\{" + ENTRY.pattern + r"\},?")

        def repack(match):
            name, first, last = match.groups()
            if name == "/lego_world.cfg":
                return ""
            first, last = int(first), int(last)
            offset = removed if first >= end else 0
            return f'{{filename:"{name}",start:{first - offset},end:{last - offset}}},'

        text = object_pattern.sub(repack, text)
        data_path = self.glue.with_suffix(".data")
        data = data_path.read_bytes()
        self.replace_file(data_path, data[:start] + data[end:])
        self.replace_file(self.glue, text.encode())
        self.rejected("Missing route config in WASM preload: lego_world.cfg")

    def test_corrupt_config_bytes(self):
        start = next(int(start) for name, start, end in ENTRY.findall(self.glue.read_text())
                     if name == "/lego_world.cfg")
        data_path = self.glue.with_suffix(".data")
        data = bytearray(data_path.read_bytes())
        data[start] ^= 1
        self.replace_file(data_path, data)
        self.rejected("Stale/corrupt preload bytes: /lego_world.cfg")

    def test_control_points_to_missing_wasm_export(self):
        symbol = self.baseline["keepalive_exports"]["voxy_lego_action"]
        text = self.glue.read_text()
        # Both readable and Closure glue expose this public Module property.
        pattern = (r'''(Module(?:\._voxy_lego_action|\[["']_voxy_lego_action["']\])'''
                   r'''\s*=\s*[\w$]+)(?:\.''' + re.escape(symbol) +
                   r'''\b|\[["']''' + re.escape(symbol) + r'''["']\])''')
        text, count = re.subn(pattern, r"\1.not_a_wasm_export", text)
        self.assertEqual(count, 1)
        self.replace_file(self.glue, text.encode())
        self.rejected("JS control has no function export: voxy_lego_action")


if __name__ == "__main__":
    unittest.main()
