#!/usr/bin/env python3
"""Verify the CMake WRECKWATER server's source and binary closure."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


_TARGET_MARKERS = (
    "CMakeFiles/wreckwater_headless_authority.dir/",
    "CMakeFiles/wreckwater_server.dir/",
)

_FORBIDDEN_SYMBOLS = (
    "voxy::client::",
    "voxy::render::",
    "voxy::Window::",
    "voxy::gpu::Context::init(voxy::Window&",
    "voxy::gpu::Context::createSurface(",
    "voxy::gpu::Context::configureSurface(",
    "voxy::gpu::Context::resizeSwapchain(",
    "voxy::gpu::Context::setPresentMode(",
    "voxy::gpu::Context::getCurrentTextureView()",
    "voxy::gpu::Context::present()",
    "voxy::physics::JoltBackend",
    "voxy::physics::Box3DReferenceBackend",
    "JPH::",
)

_REQUIRED_SYMBOLS = (
    "voxy::server::WreckwaterAuthorityRuntime::tick()",
    "voxy::physics::GpuPhysicsBackend::initialize(",
)

_FORBIDDEN_DYNAMIC_DEPENDENCIES = (
    "libglfw",
    "libx11",
    "libwayland-client",
    "libbox2d",
    "libbox3d",
    "libjolt",
    "libzstd",
)


def _allowlisted_translation_units(path: Path) -> set[str]:
    result: set[str] = set()
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 2:
            raise RuntimeError(
                f"{path}:{line_number}: expected '<source|wgsl> <path>'"
            )
        kind, source_path = fields
        if kind not in ("source", "wgsl"):
            raise RuntimeError(
                f"{path}:{line_number}: unknown input kind {kind!r}"
            )
        if kind != "source":
            continue
        if (
            not source_path.startswith("src/")
            or not source_path.endswith((".cpp", ".hpp"))
        ):
            raise RuntimeError(
                f"{path}:{line_number}: invalid source {source_path!r}"
            )
        if not source_path.endswith(".cpp"):
            continue
        if source_path in result:
            raise RuntimeError(
                f"{path}:{line_number}: duplicate TU {source_path}"
            )
        result.add(source_path)
    return result


def _command_text(entry: dict[str, object]) -> str:
    fields = [
        str(entry.get("command", "")),
        str(entry.get("output", "")),
    ]
    arguments = entry.get("arguments")
    if isinstance(arguments, list):
        fields.extend(str(argument) for argument in arguments)
    return " ".join(fields).replace("\\", "/")


def _repository_path(entry: dict[str, object], workspace: Path) -> str:
    raw_file = entry.get("file")
    if not isinstance(raw_file, str) or not raw_file:
        raise RuntimeError("compile_commands entry has no file")
    source = Path(raw_file)
    if not source.is_absolute():
        directory = Path(str(entry.get("directory", ".")))
        source = directory / source
    source = source.resolve()
    try:
        return source.relative_to(workspace).as_posix()
    except ValueError as exception:
        raise RuntimeError(
            f"selected compile input is outside the workspace: {source}"
        ) from exception


def _configured_translation_units(
    path: Path, workspace: Path
) -> set[str]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, list):
        raise RuntimeError("compile_commands.json root is not a list")

    selected: list[str] = []
    for raw_entry in document:
        if not isinstance(raw_entry, dict):
            raise RuntimeError("compile_commands entry is not an object")
        command = _command_text(raw_entry)
        if any(marker in command for marker in _TARGET_MARKERS):
            selected.append(_repository_path(raw_entry, workspace))

    duplicates = {
        source for source in selected if selected.count(source) > 1
    }
    if duplicates:
        raise RuntimeError(
            "authority TUs compile more than once: "
            + ", ".join(sorted(duplicates))
        )
    if not selected:
        raise RuntimeError("no authority compile commands were found")
    return set(selected)


def _run(command: list[str], description: str) -> str:
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        if result.stdout:
            sys.stderr.write(result.stdout)
        if result.stderr:
            sys.stderr.write(result.stderr)
        raise RuntimeError(f"{description} failed")
    return result.stdout


def _check_symbols(binary: Path, nm: str) -> None:
    symbols = _run(
        [nm, "-C", "--defined-only", str(binary)],
        "defined-symbol inspection",
    )
    present = [
        pattern for pattern in _FORBIDDEN_SYMBOLS if pattern in symbols
    ]
    if present:
        raise RuntimeError(
            "forbidden authority symbols: " + ", ".join(present)
        )
    missing = [
        pattern for pattern in _REQUIRED_SYMBOLS if pattern not in symbols
    ]
    if missing:
        raise RuntimeError(
            "required authority symbols absent (binary may be stripped or "
            "the wrong target): " + ", ".join(missing)
        )


def _check_dynamic_dependencies(binary: Path, ldd: str) -> None:
    dependencies = _run(
        [ldd, str(binary)], "dynamic-dependency inspection"
    ).lower()
    present = [
        dependency
        for dependency in _FORBIDDEN_DYNAMIC_DEPENDENCIES
        if dependency in dependencies
    ]
    if present:
        raise RuntimeError(
            "forbidden authority dynamic dependencies: "
            + ", ".join(present)
        )


def _print_paths(title: str, paths: set[str]) -> None:
    if not paths:
        return
    print(title, file=sys.stderr)
    for path in sorted(paths):
        print(f"  {path}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--ldd", default="ldd")
    arguments = parser.parse_args()

    try:
        workspace = Path(arguments.workspace).resolve()
        compile_commands = Path(arguments.compile_commands).resolve()
        binary = Path(arguments.binary).resolve()
        expected = _allowlisted_translation_units(
            workspace / "wreckwater_authority_content.allowlist"
        )
        configured = _configured_translation_units(
            compile_commands, workspace
        )
        missing = expected - configured
        extra = configured - expected
        _print_paths("Manifested but not compiled by CMake:", missing)
        _print_paths("Compiled by CMake but not manifested:", extra)
        if missing or extra:
            return 1
        _check_symbols(binary, arguments.nm)
        _check_dynamic_dependencies(binary, arguments.ldd)
    except (OSError, RuntimeError, json.JSONDecodeError) as exception:
        print(f"CMake authority closure check failed: {exception}",
              file=sys.stderr)
        return 1

    print(
        "WRECKWATER CMake authority closure matches "
        f"{len(configured)} manifested translation units; "
        "binary symbols and dynamic dependencies are clean."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
