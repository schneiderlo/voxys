"""Compose the salvage skiff, dock, loose generator and delivery area.

An optional experimental outboard beam carries the winch beside the port pontoon.
All support and winch sockets remain actual aligned structural connections.
The runtime admits boat/scenery/cargo separately and owns job transactions.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from build_rotation_fixtures import rotations


def compose(root, outboard_winch=False):
    broad = json.loads((root / 'fixture-kit-broad.json').read_text())
    materials = json.loads((root / 'fixture-material-shading-r02.json').read_text())
    cargo = json.loads((root / 'fixture-kit-cargo.json').read_text())
    bundles = copy.deepcopy(broad['bundles'])
    # Preserve physical placement/metadata. Use the verified dry shading candidates.
    bundles[0] = materials['bundles'][0]
    bundles[5] = materials['bundles'][2]
    bundles[6] = materials['bundles'][1]
    bundles.append(cargo['bundles'][1])
    # Canonical origin: (-19, configured water height, -37) in this shoreline.
    skiff = [50, -8, -2700]
    placements = copy.deepcopy(broad['placements'])
    for original, placed in zip(broad['placements'], placements):
        placed['translation_ticks'] = [a+b for a,b in zip(original['translation_ticks'], skiff)]
    groups = {'skiff': list(range(len(placements))), 'dock': [], 'generator': [], 'salvage': []}

    def put(group, bundle, point, rotation=0):
        groups[group].append(len(placements))
        placements.append(dict(bundle=bundle, translation_ticks=list(point), rotation=rotation))

    # Eight 4x2 m deck modules form a 16 m pier. Deck top is 1.28 m above water.
    for z in range(-2850, -2149, 100):
        put('dock', 2, (300, 56, z))
    upright = next(i for i, axes in enumerate(rotations())
                   if axes[0] == (0, 1, 0) and axes[2] == (0, 0, 1))
    # Four authored beams become pilings. Ends sit below the real waterline.
    for x in (225, 375):
        for z in (-2825, -2175):
            put('dock', 1, (x, -52, z), upright)
    put('generator', 8, (300, 96, -2675))
    # At the cove's declared 600 m height scale, the -204 m seabed quantizes
    # to a -203.84 m plate and -203.66 m stud cap. The .64 m cargo half-height
    # therefore needs a root above -203.02 m, not the old smooth-ground -203.36.
    put('salvage', 8, (-225, -150, -2900))
    # Standard beam on the forward deck. Its outer socket puts the cable past
    # the hull; the old over-pontoon winch pulled the load into the hull.
    connections = copy.deepcopy(broad['connections'])
    if outboard_winch:
        # Experimental layout: live-game stability is unaccepted. Preserve it
        # for later builder work; the default retains the proven starter hull.
        put('skiff', 1, (-50, 56, -2750))
        placements[9]['translation_ticks'] = [-125, 112, -2750]
        placements[10]['translation_ticks'] = [25, 80, -2750]
        connections[15] = dict(a=dict(placement=4, socket='100'), b=dict(placement=25, socket='105'))
        connections[16] = dict(a=dict(placement=25, socket='106'), b=dict(placement=10, socket='2'))
        connections.append(dict(a=dict(placement=25, socket='100'), b=dict(placement=9, socket='2')))
    assert len(placements) <= 32 and len(bundles) == 9
    result = dict(schema=6, bundles=bundles, placements=placements, connections=connections,
                  navigation={'spawn': [6, 1.28, -49], 'dock_boarding': [4.5, 1.28, -53], 'boat_boarding': [2.6, 0.96, -53], 'helm_standing': [0.5, 0.96, -53.1], 'look_target': [4.5, 1.8, -53], 'boat_placements': groups['skiff'], 'cargo_placements': groups['salvage'],
                              'delivery': {'center': [.5, 0, -54], 'radius': 5, 'minimum_height': -1, 'maximum_speed': .8, 'maximum_angular_speed': 1}},
                  camera=dict(eye=[-6, 5, -50], target=[2.1, .65, -53.6]))
    return result, dict(schema=1, role='Playable salvage cove composition',
        water_relative=True, groups=groups, skiff_translation_ticks=skiff, experimental_outboard_winch=outboard_winch,
        skiff_connections=connections,
        source_sha256={name: hashlib.sha256((root/name).read_bytes()).hexdigest()
            for name in ['fixture-kit-broad.json','fixture-material-shading-r02.json','fixture-kit-cargo.json']},
        remaining=['Moving-deck character and robot presentation', 'Full dynamic water composition',
                   'Cast/contact shadows', 'Campaign persistence and progression', 'Visual approval'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('data/salvage'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--outboard-winch', action='store_true', help='Unaccepted experimental lifting rig')
    args = parser.parse_args()
    registry, record = compose(args.root, args.outboard_winch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as output:
        output.write(json.dumps(registry, indent=2)+'\n')
    with args.output.with_suffix('.composition.json').open('x') as output:
        output.write(json.dumps(record, indent=2)+'\n')


if __name__ == '__main__':
    main()
