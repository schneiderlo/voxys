#!/usr/bin/env python3
"""Generate WRECKWATER's checked build-content identity header."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import sys
from typing import Iterable, Sequence


_LEAF_DOMAIN = b"VOXY/WRECKWATER/CONTENT-LEAF/V1"
_ROOT_DOMAIN = b"VOXY/WRECKWATER/CONTENT-MANIFEST/V1"
_ENTRY_DOMAIN = b"VOXY/WRECKWATER/CONTENT-ENTRY/V1"
_END_DOMAIN = b"VOXY/WRECKWATER/CONTENT-END/V1"
_MANIFEST_SCHEMA = 1
_AUTHORITATIVE_ASSET_KIND = 3
_BYTES_VALUE_TYPE = 4
_MAXIMUM_ENTRIES = 128
_MAXIMUM_PATH_BYTES = 127
_MAXIMUM_ENTRY_BYTES = 512 * 1024 * 1024
_MAXIMUM_TOTAL_BYTES = 1024 * 1024 * 1024
_CANONICAL_PATH = re.compile(r"^[a-z0-9._/-]+$")
_QUOTED_INCLUDE = re.compile(
    rb'^[ \t]*#[ \t]*include[ \t]*"([^"\r\n]+)"', re.MULTILINE
)
_DERIVED_QUOTED_INCLUDES = frozenset(
    {"generated/wreckwater_build_content.hpp"}
)


class ManifestError(ValueError):
    """The allowlist or declared build inputs violate the manifest contract."""


def _u32(value: int) -> bytes:
    return struct.pack("<I", value)


def _u64(value: int) -> bytes:
    return struct.pack("<Q", value)


def _validate_canonical_path(path: str) -> None:
    encoded = path.encode("ascii", errors="strict")
    if (
        not path
        or len(encoded) > _MAXIMUM_PATH_BYTES
        or not _CANONICAL_PATH.fullmatch(path)
        or path.startswith("/")
        or path.endswith("/")
        or "\\" in path
    ):
        raise ManifestError(f"non-canonical manifest path: {path!r}")
    parts = path.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ManifestError(f"aliased manifest path: {path!r}")


def parse_allowlist(path: Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 2:
            raise ManifestError(
                f"{path}:{line_number}: expected '<source|wgsl> <path>'"
            )
        kind, logical_path = fields
        _validate_canonical_path(logical_path)
        if kind == "source":
            if not logical_path.startswith("src/") or not logical_path.endswith(
                (".cpp", ".hpp")
            ):
                raise ManifestError(
                    f"{path}:{line_number}: invalid authority source path"
                )
        elif kind == "wgsl":
            if not logical_path.startswith(
                "shaders/"
            ) or not logical_path.endswith(".wgsl"):
                raise ManifestError(
                    f"{path}:{line_number}: invalid authority WGSL path"
                )
        else:
            raise ManifestError(
                f"{path}:{line_number}: unknown entry kind {kind!r}"
            )
        entries.append((kind, logical_path))

    if not entries:
        raise ManifestError("authority content allowlist is empty")
    if len(entries) > _MAXIMUM_ENTRIES:
        raise ManifestError("authority content allowlist exceeds 128 entries")
    paths = [logical_path for _, logical_path in entries]
    if len(set(paths)) != len(paths):
        raise ManifestError("authority content allowlist contains duplicates")
    if paths != sorted(paths):
        raise ManifestError(
            "authority content allowlist must be sorted by canonical path"
        )
    return entries


def _lexical_repo_path(root: Path, declared: str) -> str:
    if any(character in declared for character in "*?[]{}"):
        raise ManifestError(f"glob syntax is forbidden in input: {declared!r}")
    slash_path = declared.replace("\\", "/")
    if slash_path.startswith("/"):
        slash_path = slash_path[1:]
    elif (
        len(slash_path) >= 3
        and slash_path[1] == ":"
        and slash_path[2] == "/"
    ):
        slash_path = slash_path[3:]
    if any(
        segment in ("", ".", "..")
        for segment in slash_path.split("/")
    ):
        raise ManifestError(f"aliased declared input: {declared!r}")
    root_absolute = os.path.abspath(os.fspath(root))
    declared_absolute = os.path.abspath(declared)
    try:
        common = os.path.commonpath((root_absolute, declared_absolute))
    except ValueError as exception:
        raise ManifestError(
            f"input is outside repository root: {declared!r}"
        ) from exception
    if common != root_absolute:
        raise ManifestError(f"input is outside repository root: {declared!r}")
    relative = os.path.relpath(declared_absolute, root_absolute)
    logical_path = Path(relative).as_posix()
    _validate_canonical_path(logical_path)
    return logical_path


def resolve_declared_inputs(
    root: Path, declared_inputs: Sequence[str]
) -> dict[str, Path]:
    if not declared_inputs:
        raise ManifestError("no build inputs were declared")
    resolved: dict[str, Path] = {}
    for declared in declared_inputs:
        logical_path = _lexical_repo_path(root, declared)
        if logical_path in resolved:
            raise ManifestError(f"duplicate declared input: {logical_path}")
        filesystem_path = Path(os.path.abspath(declared))
        if not filesystem_path.is_file():
            raise ManifestError(
                f"declared input is missing or not a file: {logical_path}"
            )
        resolved[logical_path] = filesystem_path
    return resolved


def snapshot_declared_inputs(
    inputs: dict[str, Path],
) -> dict[str, bytes]:
    # Read each declared file exactly once. The same immutable byte snapshot is
    # used for both the root digest and embedded authority WGSL, eliminating a
    # hash/read TOCTOU window inside the generator.
    return {
        logical_path: filesystem_path.read_bytes()
        for logical_path, filesystem_path in inputs.items()
    }


def _source_include_candidates(
    source_path: str, included_path: str
) -> tuple[str, ...]:
    if included_path in _DERIVED_QUOTED_INCLUDES:
        return ()
    if included_path.startswith("generated/"):
        raise ManifestError(
            f"{source_path}: undeclared generated quoted include "
            f"{included_path!r}"
        )
    try:
        _validate_canonical_path(included_path)
    except (ManifestError, UnicodeError) as exception:
        raise ManifestError(
            f"{source_path}: non-canonical quoted include "
            f"{included_path!r}"
        ) from exception

    if included_path.startswith("src/"):
        return (included_path,)

    source_parent = source_path.rsplit("/", maxsplit=1)[0]
    relative = f"{source_parent}/{included_path}"
    rooted = f"src/{included_path}"
    if relative == rooted:
        return (relative,)
    return (relative, rooted)


def validate_source_include_closure(
    entries: Sequence[tuple[str, str]],
    inputs: dict[str, bytes],
) -> None:
    """Require every repository-local quoted include in the source manifest.

    Authority code uses angle brackets for platform and third-party headers.
    Quoted includes are therefore first-party source inputs, apart from the
    generated build-content header. Both source-relative and ``src/`` include
    roots are supported without reading undeclared files from a build sandbox.
    """
    source_paths = {
        logical_path for kind, logical_path in entries if kind == "source"
    }
    failures: list[str] = []

    for source_path in sorted(source_paths):
        source = inputs.get(source_path)
        if source is None:
            # compute_manifest() reports the missing declared input.
            continue
        for match in _QUOTED_INCLUDE.finditer(source):
            try:
                included_path = match.group(1).decode("ascii")
            except UnicodeDecodeError as exception:
                raise ManifestError(
                    f"{source_path}: quoted include is not ASCII"
                ) from exception
            candidates = _source_include_candidates(
                source_path, included_path
            )
            if not candidates:
                continue
            matches = [path for path in candidates if path in source_paths]
            if len(matches) == 1:
                continue
            if len(matches) > 1:
                failures.append(
                    f"{source_path}: ambiguous quoted include "
                    f"{included_path!r} matches {', '.join(matches)}"
                )
                continue
            preferred = (
                candidates[-1] if "/" in included_path else candidates[0]
            )
            failures.append(
                f"{source_path}: quoted include {included_path!r} is not "
                f"manifested (expected source {preferred})"
            )

    if failures:
        raise ManifestError(
            "authority source include closure is incomplete; "
            + "; ".join(failures)
        )


def compute_manifest(
    entries: Sequence[tuple[str, str]],
    inputs: dict[str, bytes],
) -> tuple[bytes, int, int]:
    expected_paths = {logical_path for _, logical_path in entries}
    supplied_paths = set(inputs)
    missing = sorted(expected_paths - supplied_paths)
    extra = sorted(supplied_paths - expected_paths)
    if missing or extra:
        details: list[str] = []
        if missing:
            details.append("missing: " + ", ".join(missing))
        if extra:
            details.append("extra: " + ", ".join(extra))
        raise ManifestError("declared inputs do not match allowlist; " + "; ".join(details))

    validate_source_include_closure(entries, inputs)

    encoded_entries: list[tuple[str, int, bytes]] = []
    total_payload_bytes = 0
    for _, logical_path in entries:
        payload = inputs[logical_path]
        if len(payload) > _MAXIMUM_ENTRY_BYTES:
            raise ManifestError(f"input exceeds 512 MiB: {logical_path}")
        total_payload_bytes += len(payload)
        if total_payload_bytes > _MAXIMUM_TOTAL_BYTES:
            raise ManifestError("authority content exceeds 1 GiB")
        leaf = hashlib.sha256(
            _LEAF_DOMAIN
            + _u32(_BYTES_VALUE_TYPE)
            + _u64(len(payload))
            + payload
        ).digest()
        encoded_entries.append((logical_path, len(payload), leaf))

    root_hash = hashlib.sha256()
    root_hash.update(_ROOT_DOMAIN)
    root_hash.update(_u32(_MANIFEST_SCHEMA))
    root_hash.update(_u32(len(encoded_entries)))
    root_hash.update(_u64(total_payload_bytes))
    for logical_path, payload_bytes, leaf in encoded_entries:
        path_bytes = logical_path.encode("ascii")
        root_hash.update(_ENTRY_DOMAIN)
        root_hash.update(_u32(_AUTHORITATIVE_ASSET_KIND))
        root_hash.update(_u32(_BYTES_VALUE_TYPE))
        root_hash.update(_u32(len(path_bytes)))
        root_hash.update(path_bytes)
        root_hash.update(_u64(payload_bytes))
        root_hash.update(leaf)
    root_hash.update(_END_DOMAIN)
    digest = root_hash.digest()
    replay_prefix = int.from_bytes(digest[:8], byteorder="big")
    if replay_prefix == 0:
        raise ManifestError("generated replay content prefix is zero")
    return digest, replay_prefix, total_payload_bytes


def render_header(
    digest: bytes,
    replay_prefix: int,
    entry_count: int,
    total_payload_bytes: int,
    shader_sources: Sequence[tuple[str, bytes]] = (),
) -> str:
    digest_bytes = ",\n        ".join(
        f"std::byte{{0x{value:02x}u}}" for value in digest
    )
    digest_hex = digest.hex()
    shader_blobs: list[str] = []
    shader_rows: list[str] = []
    for index, (logical_path, source) in enumerate(shader_sources):
        symbol = f"kWreckwaterAuthorityShaderSourceBytes{index}"
        byte_lines = []
        for offset in range(0, len(source), 16):
            byte_lines.append(
                "        "
                + ", ".join(
                    f"0x{value:02x}"
                    for value in source[offset : offset + 16]
                )
            )
        shader_blobs.append(
            f"inline constexpr std::array<char, {len(source)}> "
            f"{symbol}{{{{\n"
            + ",\n".join(byte_lines)
            + "\n}};\n"
        )
        shader_rows.append(
            "        voxy::gpu::ShaderSource{\n"
            f'            "{logical_path}",\n'
            "            std::string_view(\n"
            f"                {symbol}.data(), {symbol}.size())\n"
            "        }"
        )
    rendered_shader_blobs = "\n".join(shader_blobs)
    rendered_shader_rows = ",\n".join(shader_rows)
    return f"""// Generated by tools/generate_wreckwater_build_content.py.
// Do not edit. Change the explicit authority allowlist or one of its inputs.
#pragma once

#include "gpu/shader_source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace voxy::build_content {{

inline constexpr uint32_t kWreckwaterAuthorityManifestSchema = {_MANIFEST_SCHEMA}u;
inline constexpr uint32_t kWreckwaterAuthorityContentEntryCount = {entry_count}u;
inline constexpr uint64_t kWreckwaterAuthorityContentPayloadBytes =
    {total_payload_bytes}ull;
inline constexpr std::array<std::byte, 32>
    kWreckwaterAuthorityContentDigest{{{{
        {digest_bytes},
    }}}};
inline constexpr uint64_t kWreckwaterAuthorityReplayContentHash =
    0x{replay_prefix:016x}ull;
inline constexpr std::string_view kWreckwaterAuthorityContentDigestHex =
    "{digest_hex}";
{rendered_shader_blobs}
inline constexpr std::array<
    voxy::gpu::ShaderSource, {len(shader_sources)}>
    kWreckwaterAuthorityShaderSources{{{{
{rendered_shader_rows}
    }}}};

}} // namespace voxy::build_content
"""


def generate(
    root: Path,
    allowlist_path: Path,
    output_path: Path,
    declared_inputs: Sequence[str],
) -> None:
    entries = parse_allowlist(allowlist_path)
    resolved_inputs = resolve_declared_inputs(root, declared_inputs)
    inputs = snapshot_declared_inputs(resolved_inputs)
    digest, replay_prefix, total_payload_bytes = compute_manifest(
        entries, inputs
    )
    shader_sources: list[tuple[str, bytes]] = []
    for kind, logical_path in entries:
        if kind != "wgsl":
            continue
        shader_bytes = inputs[logical_path]
        try:
            source = shader_bytes.decode("ascii")
        except UnicodeDecodeError as exception:
            raise ManifestError(
                f"WGSL is not ASCII: {logical_path}"
            ) from exception
        if "\x00" in source:
            raise ManifestError(f"WGSL contains NUL: {logical_path}")
        if "\r" in source:
            raise ManifestError(
                f"WGSL must use LF line endings: {logical_path}"
            )
        shader_sources.append((logical_path, shader_bytes))
    header = render_header(
        digest, replay_prefix, len(entries), total_payload_bytes,
        shader_sources,
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name(output_path.name + ".tmp")
    temporary.write_text(header, encoding="utf-8", newline="\n")
    os.replace(temporary, output_path)


def _parse_arguments(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--allowlist", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--inputs", required=True, nargs="+")
    return parser.parse_args(argv)


def main(argv: Iterable[str] | None = None) -> int:
    arguments = _parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        generate(
            arguments.root,
            arguments.allowlist,
            arguments.output,
            arguments.inputs,
        )
    except (ManifestError, OSError, UnicodeError) as exception:
        print(f"wreckwater build-content generation failed: {exception}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
