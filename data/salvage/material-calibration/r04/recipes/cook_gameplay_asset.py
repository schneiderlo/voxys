#!/usr/bin/env python3
"""Offline, bounded ASSET-02 cook; physical rules belong to gameplay_sidecar_tool.

Publishes one immutable bundle by an atomic, no-replace directory rename.
Selects the explicit salvage-rigid-v1 converter profile; never retries through
legacy conversion. This does not instantiate an ASSET-04 runtime prefab.
"""
from __future__ import annotations

import argparse
import base64
import ctypes
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

MAX_SIDECAR = 1024 * 1024
MAX_GLB = 64 * 1024 * 1024
MAX_VMESH = 256 * 1024 * 1024
MAX_LODS = 8
MAX_JSON_DEPTH = 32
MAX_JSON_VALUES = 100000
GLTF_PROFILE = 'salvage-rigid-v1'
GLB_JSON = 0x4E4F534A
GLB_BIN = 0x004E4942
EMBEDDED_URI_PREFIXES = (
    'data:application/octet-stream;base64,',
    'data:application/gltf-buffer;base64,',
    'data:image/png;base64,',
    'data:image/jpeg;base64,',
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_bounded(path: Path, maximum: int) -> bytes:
    """Open a regular file without following a final-component symlink."""
    # A FIFO would block during open, before fstat can reject its type.
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
                 | getattr(os, 'O_NONBLOCK', 0))
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= maximum:
            raise ValueError(f'nonregular, empty or oversized input: {path.name}')
        data = stream.read(maximum + 1)
        after = os.fstat(stream.fileno())
    if len(data) != before.st_size or after.st_size != before.st_size or after.st_mtime_ns != before.st_mtime_ns:
        raise ValueError(f'input changed during snapshot: {path.name}')
    return data


def json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def strict_json(data: bytes):
    def reject_constant(_):
        raise ValueError('nonfinite JSON number')
    result = json.loads(data.decode('utf-8'), object_pairs_hook=json_object,
                        parse_constant=reject_constant)
    remaining = MAX_JSON_VALUES
    def visit(value, depth):
        nonlocal remaining
        remaining -= 1
        if depth > MAX_JSON_DEPTH or remaining < 0:
            raise ValueError('JSON depth/value capacity')
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError('nonfinite JSON number')
        if isinstance(value, dict):
            for child in value.values(): visit(child, depth + 1)
        elif isinstance(value, list):
            for child in value: visit(child, depth + 1)
    visit(result, 0)
    return result


def self_contained_glb(data: bytes):
    if len(data) < 20 or len(data) > MAX_GLB:
        raise ValueError('GLB byte capacity')
    magic, version, length = struct.unpack_from('<4sII', data)
    if magic != b'glTF' or version != 2 or length != len(data):
        raise ValueError('GLB header/length')
    chunks = []
    offset = 12
    while offset < len(data):
        if len(data) - offset < 8 or len(chunks) >= 2:
            raise ValueError('GLB chunk capacity/header')
        size, kind = struct.unpack_from('<II', data, offset)
        offset += 8
        if size == 0 or size % 4 or size > len(data) - offset:
            raise ValueError('GLB chunk size')
        chunks.append((kind, data[offset:offset + size]))
        offset += size
    if not chunks or chunks[0][0] != GLB_JSON or (len(chunks) == 2 and chunks[1][0] != GLB_BIN):
        raise ValueError('GLB chunk ordering/type')
    # JSON is deliberately bounded separately from potentially large vertex data.
    if len(chunks[0][1]) > MAX_SIDECAR:
        raise ValueError('GLB JSON byte capacity')
    document = strict_json(chunks[0][1])
    if not isinstance(document, dict) or document.get('asset', {}).get('version') != '2.0':
        raise ValueError('GLB JSON version')
    for category in ('buffers', 'images'):
        records = document.get(category, [])
        if not isinstance(records, list) or len(records) > 4096:
            raise ValueError('GLB dependency record capacity')
        for record in records:
            if not isinstance(record, dict):
                raise ValueError('GLB dependency record type')
            if 'uri' in record:
                uri = record['uri']
                # TinyGLTF treats an unrecognized data: prefix as an external
                # filename. Permit only these recognized, truly embedded forms.
                prefix = next((p for p in EMBEDDED_URI_PREFIXES if isinstance(uri, str) and uri.startswith(p)), None)
                if prefix is None:
                    raise ValueError('external GLB buffer/image URI')
                payload = base64.b64decode(uri[len(prefix):], validate=True)
                if not payload:
                    raise ValueError('empty embedded GLB dependency')
    return document


def source_bindings(document):
    """Only orchestration fields here; C++ validates all physical/schema rules."""
    if not isinstance(document, dict) or document.get('schema') != 1:
        raise ValueError('sidecar schema')
    lods = document.get('lods')
    if not isinstance(lods, list) or not 1 <= len(lods) <= MAX_LODS:
        raise ValueError('LOD capacity')
    bindings = []
    seen = set()
    for lod in lods:
        local_id = lod['id']
        if not isinstance(local_id, str) or not re.fullmatch(r'[1-9][0-9]{0,19}', local_id) or int(local_id) > 2**64 - 1:
            raise ValueError('LOD durable ID')
        if local_id in seen: raise ValueError('duplicate LOD ID')
        seen.add(local_id)
        source = lod['source']
        name = source['file']
        if not isinstance(name, str) or not re.fullmatch(r'(?!\.)[A-Za-z0-9_.-]{1,92}\.glb', name):
            raise ValueError('source GLB basename')
        if not isinstance(source['sha256'], str) or not re.fullmatch('[0-9a-f]{64}', source['sha256']):
            raise ValueError('source SHA256')
        size = source['bytes']
        if type(size) is not int or not 20 <= size <= MAX_GLB:
            raise ValueError('source byte capacity')
        bindings.append((int(local_id), name, source['sha256'], size))
    return sorted(bindings)


def run_tool(args: list[str], output: Path, timeout: float):
    # Disk-backed bounded tool output prevents a bad converter from filling
    # parent memory. Resource limits are applied only to our child process.
    def limits():
        import resource
        resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_VMESH, MAX_VMESH))
        resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
        resource.setrlimit(resource.RLIMIT_CPU, (60, 60))
    with output.open('xb') as log:
        completed = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT,
                                   timeout=timeout, check=False,
                                   preexec_fn=limits if os.name == 'posix' else None)
    if completed.returncode:
        message = read_bounded(output, MAX_SIDECAR).decode('utf-8', errors='replace')[:2000] if output.stat().st_size else ''
        raise ValueError(f'{Path(args[0]).name} failed ({completed.returncode}): {message}')


def publish_no_replace(staging: Path, destination: Path):
    # os.rename on POSIX can overwrite an empty directory. The Linux primitive
    # makes refusal atomic even when another publisher wins after our precheck.
    if sys.platform.startswith('linux'):
        libc = ctypes.CDLL(None, use_errno=True)
        function = getattr(libc, 'renameat2', None)
        if function is None:
            raise OSError(errno.ENOSYS, 'atomic no-replace rename unavailable')
        function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        function.restype = ctypes.c_int
        if function(-100, os.fsencode(staging), -100, os.fsencode(destination), 1):
            code = ctypes.get_errno()
            raise OSError(code, os.strerror(code), str(destination))
    elif os.name == 'nt':
        os.rename(staging, destination)  # Windows rename rejects existing target.
    else:
        raise OSError(errno.ENOSYS, 'no tested atomic no-replace publication on this host')


def cook(sidecar: Path, sources: Path, output: Path, converter: Path, validator: Path):
    output = output.absolute()
    if os.path.lexists(output):
        raise FileExistsError('output already exists; choose a new asset/version directory')
    if not output.parent.is_dir(): raise ValueError('output parent directory must exist')
    sidecar_bytes = read_bounded(sidecar, MAX_SIDECAR)
    document = strict_json(sidecar_bytes)
    bindings = source_bindings(document)
    converter = converter.resolve(strict=True)
    validator = validator.resolve(strict=True)
    converter_digest = digest(read_bounded(converter, MAX_VMESH))
    validator_digest = digest(read_bounded(validator, MAX_VMESH))
    cooker_source = Path(__file__).resolve(strict=True)
    cooker_digest = digest(read_bounded(cooker_source, MAX_SIDECAR))
    stage = Path(tempfile.mkdtemp(prefix='.salvage-cook-', dir=output.parent))
    try:
        private = stage / '.inputs'
        private.mkdir()
        (private / 'sidecar.json').write_bytes(sidecar_bytes)
        manifest_lods = []
        for local_id, filename, source_digest, size in bindings:
            source = read_bounded(sources / filename, MAX_GLB)
            if len(source) != size or digest(source) != source_digest:
                raise ValueError(f'stale source binding: {filename}')
            self_contained_glb(source)
            snapshot = private / f'{local_id}.glb'
            snapshot.write_bytes(source)
            cooked = stage / f'lod-{local_id}.vmesh'
            run_tool([str(converter), '--profile', GLTF_PROFILE, str(snapshot), str(cooked)],
                     private / f'{local_id}.converter.log', 90)
            cooked_bytes = read_bounded(cooked, MAX_VMESH)
            manifest_lods.append({'id': str(local_id), 'source_sha256': source_digest,
                                  'source_bytes': size, 'file': cooked.name,
                                  'sha256': digest(cooked_bytes), 'bytes': len(cooked_bytes)})
        # Validator's stdout is the complete normalized sidecar; on failure
        # the entire private stage is removed and nothing is published.
        run_tool([str(validator), str(private / 'sidecar.json'), str(stage)], private / 'validation.json', 90)
        normalized = read_bounded(private / 'validation.json', 2 * MAX_SIDECAR)
        strict_json(normalized)
        (stage / 'gameplay.json').write_bytes(normalized)
        if (converter_digest != digest(read_bounded(converter, MAX_VMESH))
                or validator_digest != digest(read_bounded(validator, MAX_VMESH))
                or cooker_digest != digest(read_bounded(cooker_source, MAX_SIDECAR))):
            raise ValueError('tool changed during cook')
        manifest = {'schema': 1, 'scope': 'offline gameplay sidecar and unchanged exported-glTF-frame VMESH',
                    'input_sidecar_sha256': digest(sidecar_bytes),
                    'normalized_sidecar': {'file': 'gameplay.json', 'sha256': digest(normalized), 'bytes': len(normalized)},
                    'cooker': {'sha256': cooker_digest, 'source': 'cook_gameplay_asset.py'},
                    'converter': {'sha256': converter_digest, 'profile': GLTF_PROFILE,
                                  'interface': f'--profile {GLTF_PROFILE} input.glb output.vmesh'},
                    'validator': {'sha256': validator_digest, 'interface': 'sidecar.json cooked-directory'},
                    'lods': manifest_lods}
        (stage / 'cook-manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
        shutil.rmtree(private)
        publish_no_replace(stage, output)
        return manifest
    finally:
        if stage.exists(): shutil.rmtree(stage)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sidecar', required=True, type=Path)
    parser.add_argument('--sources', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--converter', required=True, type=Path)
    parser.add_argument('--validator', required=True, type=Path)
    args = parser.parse_args()
    try:
        result = cook(args.sidecar, args.sources, args.output, args.converter, args.validator)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, RecursionError, subprocess.SubprocessError) as error:
        print(f'cook rejected: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'status': 'published', 'output': str(args.output), 'lods': len(result['lods'])}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
