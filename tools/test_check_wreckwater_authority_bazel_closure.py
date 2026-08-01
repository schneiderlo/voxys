#!/usr/bin/env python3

from __future__ import annotations

import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_wreckwater_authority_bazel_closure as checker


class WreckwaterAuthorityBazelClosureCheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.allowlist = self.root / "authority.allowlist"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_allowlist_selects_only_unique_first_party_sources(self) -> None:
        self.allowlist.write_text(
            "wgsl shaders/physics.wgsl\n"
            "source src/game/rule.cpp\n"
            "source src/game/rule.hpp\n",
            encoding="utf-8",
        )
        self.assertEqual(
            checker._allowlisted_sources(self.allowlist),
            {"src/game/rule.cpp", "src/game/rule.hpp"},
        )

        self.allowlist.write_text(
            "source src/game/rule.cpp\n"
            "source src/game/rule.cpp\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            checker._allowlisted_sources(self.allowlist)

        self.allowlist.write_text(
            "source tests/rule.cpp\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(RuntimeError, "invalid source"):
            checker._allowlisted_sources(self.allowlist)

    def test_configured_query_filters_external_and_non_source_labels(
        self,
    ) -> None:
        completed = subprocess.CompletedProcess(
            args=[],
            returncode=0,
            stdout=(
                "//src/game:rule.cpp (configured)\n"
                "//src/game:rule.hpp (configured)\n"
                "//third_party:glm (configured)\n"
                "@repo//pkg:external.cpp (configured)\n"
            ),
            stderr="",
        )
        with mock.patch.object(
            checker.subprocess, "run", return_value=completed
        ) as run:
            self.assertEqual(
                checker._configured_sources(self.root, "bazel-test"),
                {"src/game/rule.cpp", "src/game/rule.hpp"},
            )
        command = run.call_args.args[0]
        self.assertEqual(command[0], "bazel-test")
        self.assertIn("--noimplicit_deps", command)
        self.assertIn("--notool_deps", command)
        self.assertEqual(run.call_args.kwargs["cwd"], self.root)

    def test_configured_query_failure_is_fail_closed(self) -> None:
        completed = subprocess.CompletedProcess(
            args=[],
            returncode=7,
            stdout="",
            stderr="configured query failed\n",
        )
        with mock.patch.object(
            checker.subprocess, "run", return_value=completed
        ), mock.patch.object(checker.sys, "stderr", new=io.StringIO()):
            with self.assertRaisesRegex(
                RuntimeError, "configured Bazel authority query failed"
            ):
                checker._configured_sources(self.root, "bazel-test")


if __name__ == "__main__":
    unittest.main()
