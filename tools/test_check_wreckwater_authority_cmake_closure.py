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
import check_wreckwater_authority_cmake_closure as checker


class WreckwaterAuthorityCMakeClosureCheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        (self.root / "src/game").mkdir(parents=True)
        (self.root / "src/server").mkdir(parents=True)
        self.allowlist = self.root / "authority.allowlist"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_allowlist_selects_only_unique_translation_units(self) -> None:
        self.allowlist.write_text(
            "wgsl shaders/physics.wgsl\n"
            "source src/game/rule.cpp\n"
            "source src/game/rule.hpp\n",
            encoding="utf-8",
        )
        self.assertEqual(
            checker._allowlisted_translation_units(self.allowlist),
            {"src/game/rule.cpp"},
        )

        self.allowlist.write_text(
            "source src/game/rule.cpp\n"
            "source src/game/rule.cpp\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            checker._allowlisted_translation_units(self.allowlist)

        self.allowlist.write_text(
            "source tests/rule.cpp\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(RuntimeError, "invalid source"):
            checker._allowlisted_translation_units(self.allowlist)

    def test_compile_database_selects_only_authority_targets(self) -> None:
        database = self.root / "compile_commands.json"
        database.write_text(
            "["
            "{"
            f"\"directory\": \"{self.root}\","
            "\"file\": \"src/game/rule.cpp\","
            "\"output\": "
            "\"src/CMakeFiles/wreckwater_headless_authority.dir/"
            "game/rule.cpp.o\","
            "\"command\": \"c++ -c src/game/rule.cpp\""
            "},"
            "{"
            f"\"directory\": \"{self.root}\","
            "\"file\": \"src/server/main.cpp\","
            "\"arguments\": [\"c++\", \"-c\", \"src/server/main.cpp\","
            "\"-o\", \"CMakeFiles/wreckwater_server.dir/main.cpp.o\"]"
            "},"
            "{"
            f"\"directory\": \"{self.root}\","
            "\"file\": \"src/game/ignored.cpp\","
            "\"output\": \"src/CMakeFiles/voxy_core.dir/ignored.cpp.o\""
            "}"
            "]",
            encoding="utf-8",
        )
        self.assertEqual(
            checker._configured_translation_units(database, self.root),
            {"src/game/rule.cpp", "src/server/main.cpp"},
        )

    def test_compile_database_rejects_duplicates_and_empty_selection(
        self,
    ) -> None:
        database = self.root / "compile_commands.json"
        entry = (
            "{"
            f"\"directory\": \"{self.root}\","
            "\"file\": \"src/game/rule.cpp\","
            "\"output\": "
            "\"src/CMakeFiles/wreckwater_headless_authority.dir/rule.o\""
            "}"
        )
        database.write_text(f"[{entry},{entry}]", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "more than once"):
            checker._configured_translation_units(database, self.root)

        database.write_text(
            "[{\"file\":\"src/game/rule.cpp\","
            "\"command\":\"c++ -c src/game/rule.cpp\"}]",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(RuntimeError, "no authority"):
            checker._configured_translation_units(database, self.root)

    def test_binary_inspections_fail_closed(self) -> None:
        binary = self.root / "wreckwater_server"
        required = "\n".join(checker._REQUIRED_SYMBOLS)

        with mock.patch.object(
            checker,
            "_run",
            return_value=required + "\nvoxy::render::Renderer::draw()\n",
        ):
            with self.assertRaisesRegex(RuntimeError, "forbidden"):
                checker._check_symbols(binary, "nm-test")

        with mock.patch.object(checker, "_run", return_value=""):
            with self.assertRaisesRegex(RuntimeError, "required"):
                checker._check_symbols(binary, "nm-test")

        with mock.patch.object(checker, "_run", return_value=required):
            checker._check_symbols(binary, "nm-test")

        with mock.patch.object(
            checker,
            "_run",
            return_value="libGLFW.so.3 => /tmp/libGLFW.so.3\n",
        ):
            with self.assertRaisesRegex(RuntimeError, "libglfw"):
                checker._check_dynamic_dependencies(binary, "ldd-test")

        failed = subprocess.CompletedProcess(
            args=[], returncode=2, stdout="", stderr="inspection failed\n"
        )
        with mock.patch.object(
            checker.subprocess, "run", return_value=failed
        ), mock.patch.object(checker.sys, "stderr", new=io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "failed"):
                checker._run(["inspect"], "binary inspection")


if __name__ == "__main__":
    unittest.main()
