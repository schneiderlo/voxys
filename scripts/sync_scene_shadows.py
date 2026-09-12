#!/usr/bin/env python3
"""Embed one shared sun receiver in standalone shipping shaders; --check for CI."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {'mesh_path.wgsl': 2, 'ray_blit.wgsl': 1, 'water_clipmap.wgsl': 1}
START = '// BEGIN GENERATED SCENE SUN SHADOW\n'
END = '// END GENERATED SCENE SUN SHADOW\n'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'shaders/scene_sun_shadow.wgsl.in').read_text()
    stale = []
    for name, group in TARGETS.items():
        block = START + source.replace('SHADOW_GROUP', str(group)).rstrip() + '\n' + END
        path = ROOT / 'shaders' / name
        original = path.read_text()
        if START in original:
            a = original.index(START); b = original.index(END, a) + len(END)
            expected = original[:a] + block + original[b:]
        else:
            expected = block + '\n' + original
        if expected != original:
            stale.append(name)
            if not args.check:
                path.write_text(expected)
    if args.check and stale:
        parser.exit(1, 'Stale scene sun shadows: ' + ', '.join(stale) + '\n')

if __name__ == '__main__':
    main()
