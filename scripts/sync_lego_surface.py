#!/usr/bin/env python3
"""Keep standalone GPU shaders on one LEGO surface definition; --check for CI."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
START = '// BEGIN GENERATED LEGO SURFACE\n'
END = '// END GENERATED LEGO SURFACE\n'
TARGETS = ('terrain_raycast.wgsl', 'ray_blit.wgsl', 'physics_ballistic.wgsl', 'physics_ccd.wgsl', 'physics_narrow_phase.wgsl', 'physics_primitives_compact.wgsl', 'physics_queries.wgsl')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    block = START + (ROOT / 'shaders/lego_surface.wgsl').read_text().rstrip() + '\n' + END
    changed = []
    for name in TARGETS:
        path = ROOT / 'shaders' / name
        source = path.read_text()
        if START in source:
            a = source.index(START)
            b = source.index(END, a) + len(END)
            expected = source[:a] + block + source[b:]
        else:
            expected = block + '\n' + source
        if source != expected:
            changed.append(name)
            if not args.check:
                path.write_text(expected)
    if args.check and changed:
        parser.exit(1, 'Stale LEGO surface: ' + ', '.join(changed) + '\n')
    print('LEGO shader surface: ' + ('updated' if changed else 'consistent'))

if __name__ == '__main__':
    main()
