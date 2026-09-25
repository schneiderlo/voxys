#!/usr/bin/env python3
"""Publish portable compute setup captured from this release's real startup.

The CI smoke run is the source of truth for descriptors. Only compute pipelines
whose WGSL matches the production shader files are exported: a SwiftShader-only
compatibility shader and adapter-dependent render targets are not portable.
Runtime requests still match the entire descriptor before reusing any program.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def portable_recipe(recipe, shaders):
    resources = recipe['resources']
    portable = []
    for row in resources:
        desc = row['descriptor']
        if row['kind'] == 'createShaderModule':
            name = desc.get('label', '')
            path = shaders / name
            # This logical module selects different code on SwiftShader. A
            # device-specific saved recipe can warm it; a public recipe cannot.
            portable.append(bool(name and name != 'physics_narrow_phase.wgsl'
                                 and Path(name).name == name and path.is_file()
                                 and path.read_text() == desc['code']))
        else:
            portable.append(True)

    selected, remap = [], {}

    def copy(value):
        if isinstance(value, list):
            return [copy(item) for item in value]
        if not isinstance(value, dict):
            return value
        if '$gpu' in value:
            old = value['$gpu']
            if old not in remap:
                row = resources[old]
                descriptor = copy(row['descriptor'])
                remap[old] = len(selected)
                selected.append({'kind': row['kind'], 'descriptor': descriptor})
            return {'$gpu': remap[old]}
        return {key: copy(item) for key, item in value.items()}

    pipelines = []
    for row in recipe['pipelines']:
        if row['kind'] != 'createComputePipelineAsync':
            continue
        module = row['descriptor']['compute']['module']['$gpu']
        if portable[module]:
            pipelines.append({'kind': row['kind'], 'descriptor': copy(row['descriptor']),
                              'costMs': row.get('costMs', 0)})
    if not pipelines:
        raise ValueError('No portable compute pipelines were captured')
    return {**recipe, 'resources': selected, 'pipelines': pipelines}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('site', type=Path)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--shaders', type=Path, default=Path(__file__).resolve().parents[1] / 'shaders')
    args = parser.parse_args()
    index = args.site / 'index.html'
    html = index.read_text()
    pattern = r"(window\.voxyReleaseFiles = ')([^']+)(';)"
    match = re.search(pattern, html)
    if not match:
        raise SystemExit('Missing release manifest in index.html')
    manifest = json.loads(match[2])
    recipe = json.loads(args.capture.read_text())
    release = ':'.join(manifest[name]['sha256'] for name in ['voxy_wasm.wasm', 'voxy_wasm.data'])
    if (recipe.get('schema') != 1 or recipe.get('release') != release
            or recipe.get('experience') != 'build' or recipe.get('configuration')):
        raise SystemExit('Captured graphics do not match this release/default experience')
    recipe = portable_recipe(recipe, args.shaders)
    body = json.dumps(recipe, separators=(',', ':')).encode()
    if len(body) > 4 * 1024 * 1024:
        raise SystemExit('Graphics recipe exceeds the 4 MiB budget')
    name = 'voxy_graphics.json'
    (args.site / name).write_bytes(body)
    manifest[name] = {'sha256': hashlib.sha256(body).hexdigest(), 'size': len(body)}
    updated = json.dumps(manifest, separators=(',', ':'))
    index.write_text(html[:match.start(2)] + updated + html[match.end(2):])
    print(f"Published {len(recipe['pipelines'])} compute programs in {len(body) / 1024:.1f} KiB")


if __name__ == '__main__':
    main()
