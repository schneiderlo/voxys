#!/usr/bin/env python3
"""Verify the pinned source bytes and resolve the MPD using only this vendored subset."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT / 'manifest.json').read_text())
for entry in [manifest['model'], *manifest['dependencies'], *manifest['ancillary']]:
    data = (ROOT / entry['path']).read_bytes()
    assert len(data) == entry['bytes'], entry['path']
    assert hashlib.sha256(data).hexdigest() == entry['sha256'], entry['path']


def normal(name):
    return name.replace('\\', '/').lower()


embedded = {}
current = None
for line in (ROOT / manifest['model']['path']).read_text().splitlines():
    if line.upper().startswith('0 FILE '):
        current = normal(line[7:].strip())
        assert current not in embedded, current
        embedded[current] = []
    elif line.upper().startswith('0 NOFILE'):
        current = None
    elif current is not None:
        embedded[current].append(line)

library = ROOT / 'ldraw'
index = {normal(str(path.relative_to(library))): path
         for path in library.rglob('*') if path.is_file()}
visited = set()
external = set()


def visit(name):
    if name in visited:
        return
    visited.add(name)
    if name in embedded:
        lines = embedded[name]
    else:
        candidates = [index[prefix + name] for prefix in ('parts/', 'p/', '')
                      if prefix + name in index]
        assert candidates, 'Unresolved dependency: ' + name
        path = candidates[0]
        external.add(str(path.relative_to(ROOT)))
        lines = path.read_text().splitlines()
    for line in lines:
        fields = line.split(maxsplit=14)
        if fields and fields[0] == '1':
            assert len(fields) == 15, line
            visit(normal(fields[14]))


visit(next(iter(embedded)))
assert external == {entry['path'] for entry in manifest['dependencies']}
assert len(external) == manifest['library']['external_dependency_files'] == 742
assert len(visited) - len(external) == manifest['library']['embedded_reachable_files'] == 117
print('PASS: source hashes; 742 external files; 117 embedded files; zero missing dependencies.')
