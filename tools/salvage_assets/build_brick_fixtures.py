"""Create CPU stack fixtures from admitted brick recipes; no runtime state edits."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import brick_parts as bricks


def rotations():
    # Canonical table order from construction_types.cpp. The production C++
    # compiler independently verifies the resulting socket frames and builds.
    axes = ((1, 0, 0), (0, 1, 0), (0, 0, 1), (-1, 0, 0), (0, -1, 0), (0, 0, -1))
    result = []
    for x in axes:
        for y in axes:
            if sum(a * b for a, b in zip(x, y)):
                continue
            z = (x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0])
            result.append(tuple(t for row in zip(x, y, z) for t in row))
    return result


def multiply(a, b):
    return tuple(sum(a[r * 3 + k] * b[k * 3 + c] for k in range(3))
                 for r in range(3) for c in range(3))


def generate(root):
    names = tuple(bricks.SIZES)
    parts = [json.loads((root / name / 'source' / f'{name}.gameplay.json').read_text())['part'] for name in names]
    table = rotations()
    bundles = [dict(directory=name + '/cooked', part=part['key'],
        manifest_sha256=hashlib.sha256((root / name / 'cooked/cook-manifest.json').read_bytes()).hexdigest(),
        lod_limits=[dict(id=str(i + 1), vertices=v, triangles=t, texture_dimension=size)
                    for i, (t, v, size, _) in enumerate(bricks.LOD_LIMITS)]) for name, part in zip(names, parts)]

    def socket_frame(placement, socket):
        rotation = table[placement['rotation']]
        p = socket['frame']['translation_ticks']
        return (tuple(placement['translation_ticks'][r] + sum(rotation[r * 3 + k] * p[k] for k in range(3))
                      for r in range(3)), multiply(rotation, table[socket['frame']['rotation']]))

    outputs = []
    for degrees, yaw, point in ((0, 0, [-50, 96, -25]), (90, 21, [-75, 96, 0]),
                                (180, 12, [-50, 96, -25]), (270, 9, [-75, 96, 0])):
        placements = [dict(bundle=kind, translation_ticks=p, rotation=0) for kind, p in
                      ((2, [0, 0, 0]), (1, [-50, 48, 0]), (1, [50, 48, 0]),
                       (0, [-50, 96, -25]), (0, [50, 96, 25]))]
        placements[3].update(rotation=yaw, translation_ticks=point)
        links = []
        for i, placed_a in enumerate(placements):
            for j in range(i + 1, len(placements)):
                placed_b = placements[j]
                for a in parts[placed_a['bundle']]['sockets']:
                    af = socket_frame(placed_a, a)
                    for b in parts[placed_b['bundle']]['sockets']:
                        if a['role'] == b['role'] or a['family'] != b['family'] or a['profile'] != b['profile']:
                            continue
                        bf = socket_frame(placed_b, b)
                        if af[0] == bf[0] and multiply(af[1], table[2]) == bf[1]:
                            links.append(dict(a=dict(placement=i, socket=a['id']), b=dict(placement=j, socket=b['id'])))
        if len(links) != 12:
            raise ValueError(f'{degrees}-degree stack needs twelve welds; found {len(links)}')
        filename = 'fixture-stack.json' if degrees == 0 else f'fixture-stack-yaw-{degrees}.json'
        path = root / filename
        if path.exists():
            raise ValueError(f'output already exists: {path}')
        outputs.append((path, dict(schema=2, prototypes=[], bundles=bundles,
            placements=placements, connections=links, camera=dict(eye=[8, 6, -9], target=[0, 1, 0]))))
    for path, document in outputs:
        path.write_text(json.dumps(document, indent=2) + '\n')
        print(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asset-root', type=Path, required=True)
    generate(parser.parse_args().asset_root)
