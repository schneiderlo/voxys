"""Individual Cove bricks on the canonical .02 m construction lattice.

This recipe is ordinary installed part metadata, with no special build rules.
The hollow underside admits the previous layer's studs at .96 m spacing.
"""
from __future__ import annotations

import copy
import math
from pathlib import Path

import functional_kit as kit
import pontoon_parameters as pontoon

PART_NAMESPACE = b'voxys-bricks-v1'.ljust(16, b'\0').hex()
VISUAL_NAMESPACE = b'voxys-brick-art1'.ljust(16, b'\0').hex()
# Names use conventional rows x columns; columns run along canonical X.
SIZES = {'brick_1x2': (2, 1), 'brick_2x2': (2, 2), 'brick_2x4': (4, 2)}
PART_IDS = {'brick_1x2': 1, 'brick_2x2': 2, 'brick_2x4': 3}
VISUAL_IDS = {'brick_1x2': 1, 'brick_2x2': 4, 'brick_2x4': 7}
PART_VERSION = 2
# Exact canonical mating frames for yaw 0/90/180/270 degrees. These are
# alternate frames of the same round underside opening, not extra geometry.
RECEPTACLE_FRAMES = (2, 23, 14, 11)
COLORS = {'brick_1x2': (0.72, 0.08, 0.045, 1),
          'brick_2x2': (0.025, 0.20, 0.58, 1),
          'brick_2x4': (0.88, 0.54, 0.025, 1)}
LOD_LIMITS = ((6000, 12000, 64, 180), (3000, 6000, 64, 55), (1500, 3000, 64, 0))


def stud_centers(name):
    columns, rows = SIZES[name]
    return [(25 * (2 * x + 1 - columns), 25 * (2 * z + 1 - rows))
            for z in range(rows) for x in range(columns)]


def shell_boxes(name):
    columns, rows = SIZES[name]
    x, z = columns * 25 - 1, rows * 25 - 1
    # Five disjoint boxes: .12 m roof and .08 m side walls. The cavity is
    # open at the bottom; do not give it sealed-pontoon displacement.
    return [kit.box(1, (0, 21, 0), (x, 3, z)),
            kit.box(2, (-x + 2, -3, 0), (2, 21, z)),
            kit.box(3, (x - 2, -3, 0), (2, 21, z)),
            kit.box(4, (0, -3, -z + 2), (x - 4, 21, 2)),
            kit.box(5, (0, -3, z - 2), (x - 4, 21, 2))]


def mass_properties(name, mass):
    """Uniform-density shell boxes and round studs, including parallel axes."""
    components = []
    for b in shell_boxes(name):
        h = [n / 50 for n in b['half_extents_ticks']]
        center = [n / 50 for n in b['frame']['translation_ticks']]
        components.append((8 * math.prod(h), center,
                           [sum(h[j] ** 2 for j in range(3) if j != k) / 3
                            for k in range(3)]))
    radius, height = .30, .18
    for x, z in stud_centers(name):
        components.append((math.pi * radius ** 2 * height, [x / 50, .57, z / 50],
                           [(3 * radius ** 2 + height ** 2) / 12,
                            radius ** 2 / 2, (3 * radius ** 2 + height ** 2) / 12]))
    volume = math.fsum(v for v, _, _ in components)
    center = [math.fsum(v * c[k] for v, c, _ in components) / volume for k in range(3)]
    inertia = [0.] * 9
    for v, c, diagonal in components:
        m = mass * v / volume
        r = [c[k] - center[k] for k in range(3)]
        for k in range(3):
            for j in range(3):
                inertia[k * 3 + j] += m * (diagonal[k] + sum(t * t for t in r)
                                           if k == j else 0) - m * r[k] * r[j]
    return dict(dry_mass_kg=mass, center_of_mass_metres=center,
                inertia_kg_metres_squared=inertia)


def definition(name):
    columns, rows = SIZES[name]
    counter = PART_IDS[name]
    solids = shell_boxes(name)
    buoyancy = [dict(box=copy.deepcopy(b), kind='solid_material') for b in solids]
    sockets = []
    for i, (x, z) in enumerate(stud_centers(name)):
        # Three disjoint rectangles approximate each round stud inside its
        # .30 m radius. The .02 m overlap with our own roof avoids an artificial
        # collision seam; between-part solid overlaps remain forbidden.
        for j, (offset, hx, hz) in enumerate(((0, 7, 13), (-10, 3, 7), (10, 3, 7))):
            identifier = 100 + 3 * i + j
            solids.append(kit.box(identifier, (x + offset, 28, z), (hx, 5, hz)))
            # Conservative disjoint submerged volumes, excluding the roof.
            buoyancy.append(dict(box=kit.box(identifier, (x + offset, 29, z),
                                             (hx, 4, hz)), kind='solid_material'))
        sockets += [kit.socket(100 + 2 * i, (x, 24, z), 0, role='plug'),
                    kit.socket(101 + 2 * i, (x, -24, z), 2)]
        for turn, frame in enumerate(RECEPTACLE_FRAMES[1:], 1):
            sockets.append(kit.socket(1000 + 4 * i + turn, (x, -24, z), frame))
    return dict(key=dict(namespace=PART_NAMESPACE, counter=str(counter), version=PART_VERSION),
                name_key='salvage.part.' + name, permitted_rotation_mask=16777215,
                footprint=dict(minimum_ticks=[1 - columns * 25, -24, 1 - rows * 25],
                               maximum_ticks=[columns * 25 - 1, 33, rows * 25 - 1]),
                solid_occupancy=solids, collision=copy.deepcopy(solids),
                mass=mass_properties(name, columns * rows * 6), buoyancy=buoyancy,
                sockets=sockets, strength=kit.STRENGTH.copy(), module=dict(type='structure'),
                cost=dict(salvage_material=str((2, 3, 5)[counter - 1]), special_machinery='0'),
                salvage_yield=dict(salvage_material=str((1, 2, 3)[counter - 1]), special_machinery='0'),
                material=dict(linear_base_color=[1, 1, 1], roughness=1, metallic=1))


def sidecar(name, directory):
    first_asset = VISUAL_IDS[name]
    lods = []
    for lod, (_, _, _, threshold) in enumerate(LOD_LIMITS):
        path = Path(directory) / f'{name}-lod-{lod}.glb'
        lods.append(dict(id=str(lod + 1), asset=dict(namespace=VISUAL_NAMESPACE,
            counter=str(first_asset + lod), version=1), source=dict(file=path.name,
            sha256=pontoon.sha256(path), bytes=path.stat().st_size,
            frame='exported_gltf', to_canonical_rotation=12), minimum_screen_height_pixels=threshold))
    part = definition(name)
    return dict(schema=1, units=dict(length='metres', mass='kilograms', time='seconds', angle='radians'),
                metadata_frame='canonical_y_up_minus_z_forward', placement_lattice_metres=.02,
                part=part, lods=lods, tool_anchors=[dict(id=s['id'],
                    name_key=f"salvage.anchor.{name}.socket_{s['id']}", frame=s['frame']) for s in part['sockets']])
