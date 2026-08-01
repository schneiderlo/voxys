#!/usr/bin/env python3

from __future__ import annotations

import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, os.fspath(Path(__file__).resolve().parent))
import generate_wreckwater_build_content as generator


class WreckwaterBuildContentGeneratorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "src/game").mkdir(parents=True)
        (self.root / "shaders").mkdir()
        self.source = self.root / "src/game/rule.cpp"
        self.shader = self.root / "shaders/authority.wgsl"
        self.source.write_bytes(b"rule-source\x00\xff")
        self.shader.write_bytes(b"@compute fn main() {}\n")
        self.allowlist = self.root / "authority.allowlist"
        self.allowlist.write_text(
            "wgsl shaders/authority.wgsl\n"
            "source src/game/rule.cpp\n",
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def entries(self) -> list[tuple[str, str]]:
        return generator.parse_allowlist(self.allowlist)

    def inputs(self) -> dict[str, bytes]:
        return generator.snapshot_declared_inputs(
            generator.resolve_declared_inputs(
                self.root,
                [os.fspath(self.shader), os.fspath(self.source)],
            )
        )

    def digest(self) -> bytes:
        return generator.compute_manifest(self.entries(), self.inputs())[0]

    def test_cross_implementation_golden_and_header(self) -> None:
        digest, prefix, payload_bytes = generator.compute_manifest(
            self.entries(), self.inputs()
        )
        self.assertEqual(
            digest.hex(),
            "2574dc92ecbb416181a9a0db7f481f6cd70d3ae35f2087ef00b30359d546c53e",
        )
        self.assertEqual(prefix, 0x2574DC92ECBB4161)
        self.assertEqual(payload_bytes, 35)
        header = generator.render_header(
            digest, prefix, len(self.entries()), payload_bytes,
            [(
                "shaders/authority.wgsl",
                self.shader.read_bytes(),
            )],
        )
        self.assertIn(digest.hex(), header)
        self.assertIn("0x2574dc92ecbb4161ull", header)
        self.assertIn("kWreckwaterAuthorityContentEntryCount = 2u", header)
        self.assertIn("kWreckwaterAuthorityShaderSources", header)
        self.assertIn("kWreckwaterAuthorityShaderSourceBytes0", header)
        self.assertIn('"shaders/authority.wgsl"', header)

    def test_every_source_and_wgsl_byte_affects_digest(self) -> None:
        baseline = self.digest()
        for path in (self.source, self.shader):
            original = path.read_bytes()
            for byte_index in range(len(original)):
                changed = bytearray(original)
                changed[byte_index] ^= 0x01
                path.write_bytes(changed)
                self.assertNotEqual(
                    self.digest(), baseline, f"{path}:{byte_index}"
                )
            path.write_bytes(original)
        self.assertEqual(self.digest(), baseline)

    def test_one_immutable_snapshot_drives_digest_and_embedding(self) -> None:
        snapshot = self.inputs()
        baseline = generator.compute_manifest(
            self.entries(), snapshot
        )[0]
        original_shader = snapshot["shaders/authority.wgsl"]
        self.shader.write_bytes(b"mutated after snapshot\n")

        digest, prefix, payload_bytes = generator.compute_manifest(
            self.entries(), snapshot
        )
        header = generator.render_header(
            digest, prefix, len(self.entries()), payload_bytes,
            [("shaders/authority.wgsl", original_shader)],
        )
        self.assertEqual(digest, baseline)
        self.assertNotIn("mutated after snapshot", header)
        for byte in original_shader:
            self.assertIn(f"0x{byte:02x}", header)

    def test_missing_extra_duplicate_and_glob_inputs_fail(self) -> None:
        with self.assertRaisesRegex(generator.ManifestError, "missing"):
            generator.compute_manifest(
                self.entries(),
                generator.snapshot_declared_inputs(
                    generator.resolve_declared_inputs(
                        self.root, [os.fspath(self.source)]
                    )
                ),
            )

        extra = self.root / "src/game/extra.cpp"
        extra.write_bytes(b"extra")
        with self.assertRaisesRegex(generator.ManifestError, "extra"):
            generator.compute_manifest(
                self.entries(),
                generator.snapshot_declared_inputs(
                    generator.resolve_declared_inputs(
                        self.root,
                        [
                            os.fspath(self.source),
                            os.fspath(self.shader),
                            os.fspath(extra),
                        ],
                    )
                ),
            )

        with self.assertRaisesRegex(generator.ManifestError, "duplicate"):
            generator.resolve_declared_inputs(
                self.root,
                [
                    os.fspath(self.source),
                    os.fspath(self.source),
                    os.fspath(self.shader),
                ],
            )
        with self.assertRaisesRegex(generator.ManifestError, "glob"):
            generator.resolve_declared_inputs(
                self.root, [os.fspath(self.root / "src/**/*.cpp")]
            )
        source_alias = (
            os.fspath(self.source.parent)
            + "/../game/rule.cpp"
        )
        with self.assertRaisesRegex(generator.ManifestError, "aliased"):
            generator.resolve_declared_inputs(
                self.root, [source_alias, os.fspath(self.shader)]
            )

    def test_allowlist_rejects_unsorted_aliases_and_wrong_kinds(self) -> None:
        self.allowlist.write_text(
            "source src/game/rule.cpp\n"
            "wgsl shaders/authority.wgsl\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(generator.ManifestError, "sorted"):
            generator.parse_allowlist(self.allowlist)

        self.allowlist.write_text(
            "source src/game/../game/rule.cpp\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(generator.ManifestError, "aliased"):
            generator.parse_allowlist(self.allowlist)

        self.allowlist.write_text(
            "source src//game/rule.cpp\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(generator.ManifestError, "aliased"):
            generator.parse_allowlist(self.allowlist)

        self.allowlist.write_text(
            "wgsl src/game/rule.cpp\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(generator.ManifestError, "WGSL"):
            generator.parse_allowlist(self.allowlist)

    def test_quoted_source_include_must_be_in_manifest_closure(self) -> None:
        local_header = self.root / "src/game/rule.hpp"
        rooted_header = self.root / "src/game/shared.hpp"
        local_header.write_bytes(b"#pragma once\n")
        rooted_header.write_bytes(b"#pragma once\n")
        self.source.write_bytes(
            b'#include "rule.hpp"\n'
            b'#include "game/shared.hpp"\n'
            b'#include "generated/wreckwater_build_content.hpp"\n'
        )

        with self.assertRaisesRegex(
            generator.ManifestError,
            r"closure is incomplete.*src/game/rule.hpp",
        ):
            generator.compute_manifest(self.entries(), self.inputs())

        self.allowlist.write_text(
            "wgsl shaders/authority.wgsl\n"
            "source src/game/rule.cpp\n"
            "source src/game/rule.hpp\n"
            "source src/game/shared.hpp\n",
            encoding="utf-8",
        )
        inputs = generator.snapshot_declared_inputs(
            generator.resolve_declared_inputs(
                self.root,
                [
                    os.fspath(self.shader),
                    os.fspath(self.source),
                    os.fspath(local_header),
                    os.fspath(rooted_header),
                ],
            )
        )
        generator.compute_manifest(self.entries(), inputs)

    def test_quoted_include_rejects_alias_and_ambiguous_resolution(self) -> None:
        local_header = self.root / "src/game/common.hpp"
        local_header.write_bytes(b"#pragma once\n")
        self.source.write_bytes(b'#include "../game/common.hpp"\n')
        self.allowlist.write_text(
            "wgsl shaders/authority.wgsl\n"
            "source src/game/common.hpp\n"
            "source src/game/rule.cpp\n",
            encoding="utf-8",
        )
        inputs = generator.snapshot_declared_inputs(
            generator.resolve_declared_inputs(
                self.root,
                [
                    os.fspath(self.shader),
                    os.fspath(local_header),
                    os.fspath(self.source),
                ],
            )
        )
        with self.assertRaisesRegex(generator.ManifestError, "non-canonical"):
            generator.compute_manifest(self.entries(), inputs)

        rooted_header = self.root / "src/common.hpp"
        rooted_header.write_bytes(b"#pragma once\n")
        self.source.write_bytes(b'#include "common.hpp"\n')
        self.allowlist.write_text(
            "wgsl shaders/authority.wgsl\n"
            "source src/common.hpp\n"
            "source src/game/common.hpp\n"
            "source src/game/rule.cpp\n",
            encoding="utf-8",
        )
        inputs = generator.snapshot_declared_inputs(
            generator.resolve_declared_inputs(
                self.root,
                [
                    os.fspath(self.shader),
                    os.fspath(rooted_header),
                    os.fspath(local_header),
                    os.fspath(self.source),
                ],
            )
        )
        with self.assertRaisesRegex(generator.ManifestError, "ambiguous"):
            generator.compute_manifest(self.entries(), inputs)

    def test_only_the_derived_content_header_may_escape_manifest(self) -> None:
        self.source.write_bytes(
            b'#include "generated/other.hpp"\n'
        )
        with self.assertRaisesRegex(
            generator.ManifestError,
            "undeclared generated quoted include",
        ):
            generator.compute_manifest(self.entries(), self.inputs())


if __name__ == "__main__":
    unittest.main()
