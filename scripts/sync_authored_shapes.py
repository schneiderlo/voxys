#!/usr/bin/env python3
"""Embed shared authored geometry into standalone shipping WGSL; --check for CI."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('physics_authored_shapes.wgsl', 'physics_authored_queries.wgsl')
TARGETS = {
    'mesh_path.wgsl': ('physics_authored_shapes.wgsl',),
    'physics_queries.wgsl': SOURCES,
    'physics_ccd.wgsl': SOURCES,
    'physics_narrow_phase.wgsl': SOURCES,
    'physics_ballistic.wgsl': ('physics_authored_shapes.wgsl', 'physics_authored_terrain.wgsl'),
}
START = '// BEGIN GENERATED AUTHORED GEOMETRY\n'
END = '// END GENERATED AUTHORED GEOMETRY\n'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    stale = []
    for name, sources in TARGETS.items():
        block = START + '\n'.join((ROOT/'shaders'/source).read_text().rstrip() for source in sources) + '\n' + END
        path = ROOT/'shaders'/name
        source = path.read_text()
        if START in source:
            a = source.index(START); b = source.index(END, a) + len(END)
            expected = source[:a] + block + source[b:]
        else:
            expected = block + '\n' + source
        if expected != source:
            stale.append(name)
            if not args.check:
                path.write_text(expected)
    if args.check and stale:
        parser.exit(1, 'Stale authored geometry: ' + ', '.join(stale) + '\n')
    print('Authored shader geometry: ' + ('updated' if stale else 'consistent'))

if __name__ == '__main__':
    main()
