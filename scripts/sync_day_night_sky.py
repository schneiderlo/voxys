#!/usr/bin/env python3
"""Keep the live sky identical in view, water and mesh shaders; --check for CI."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
START = '// BEGIN GENERATED DAY NIGHT SKY\n'
END = '// END GENERATED DAY NIGHT SKY\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    block = START + (ROOT / 'shaders/day_night_sky.wgsl.in').read_text().rstrip() + '\n' + END
    stale = []
    for name in ('ray_blit.wgsl', 'water_clipmap.wgsl', 'mesh_path.wgsl'):
        path = ROOT / 'shaders' / name
        original = path.read_text()
        if START in original:
            a = original.index(START)
            b = original.index(END, a) + len(END)
            expected = original[:a] + block + original[b:]
        else:
            expected = block + '\n' + original
        if expected != original:
            stale.append(name)
            if not args.check:
                path.write_text(expected)
    if args.check and stale:
        parser.exit(1, 'Stale day/night sky: ' + ', '.join(stale) + '\n')


if __name__ == '__main__':
    main()
