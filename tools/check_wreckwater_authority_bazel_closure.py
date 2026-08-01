#!/usr/bin/env python3
"""Check that the configured headless-authority sources match its allowlist."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


_AUTHORITY_QUERY = (
    'kind("source file", '
    "deps(//src/server:wreckwater_headless_authority) "
    "union //src/server:wreckwater_server_main.cpp)"
)


def _workspace_root(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).resolve()

    environment_root = os.environ.get("BUILD_WORKSPACE_DIRECTORY")
    if environment_root:
        return Path(environment_root).resolve()

    for candidate in Path(__file__).resolve().parents:
        if (
            (candidate / "MODULE.bazel").is_file()
            and (candidate / "wreckwater_authority_content.allowlist").is_file()
        ):
            return candidate
    raise RuntimeError("could not locate the voxys workspace")


def _allowlisted_sources(path: Path) -> set[str]:
    sources: set[str] = set()
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
        if kind == "wgsl":
            continue
        if kind != "source":
            raise RuntimeError(
                f"{path}:{line_number}: unknown input kind {kind!r}"
            )
        if (
            not source_path.startswith("src/")
            or not source_path.endswith((".cpp", ".hpp"))
        ):
            raise RuntimeError(
                f"{path}:{line_number}: invalid source {source_path!r}"
            )
        if source_path in sources:
            raise RuntimeError(
                f"{path}:{line_number}: duplicate source {source_path}"
            )
        sources.add(source_path)
    return sources


def _source_path(label: str) -> str | None:
    if not label.startswith("//src/") or ":" not in label:
        return None
    package, name = label[2:].split(":", maxsplit=1)
    return f"{package}/{name}"


def _configured_sources(
    workspace: Path, bazel: str
) -> set[str]:
    command = [
        bazel,
        "cquery",
        "--noimplicit_deps",
        "--notool_deps",
        _AUTHORITY_QUERY,
        "--output=label",
    ]
    result = subprocess.run(
        command,
        cwd=workspace,
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
        raise RuntimeError("configured Bazel authority query failed")

    sources: set[str] = set()
    for raw_line in result.stdout.splitlines():
        label = raw_line.split(maxsplit=1)[0]
        source_path = _source_path(label)
        if source_path is not None:
            sources.add(source_path)
    return sources


def _print_paths(title: str, paths: set[str]) -> None:
    if not paths:
        return
    print(title, file=sys.stderr)
    for path in sorted(paths):
        print(f"  {path}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--workspace",
        help="repository root; normally detected automatically",
    )
    parser.add_argument(
        "--bazel",
        default=os.environ.get("BAZEL", "bazel"),
        help="Bazel executable (default: bazel)",
    )
    arguments = parser.parse_args()

    try:
        workspace = _workspace_root(arguments.workspace)
        expected = _allowlisted_sources(
            workspace / "wreckwater_authority_content.allowlist"
        )
        configured = _configured_sources(workspace, arguments.bazel)
    except (OSError, RuntimeError) as exception:
        print(f"authority closure check failed: {exception}", file=sys.stderr)
        return 1

    missing = expected - configured
    extra = configured - expected
    _print_paths("Allowlisted but absent from the configured graph:", missing)
    _print_paths("Configured but absent from the allowlist:", extra)
    if missing or extra:
        return 1

    print(
        "WRECKWATER authority Bazel closure matches "
        f"{len(configured)} allowlisted first-party source files."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
