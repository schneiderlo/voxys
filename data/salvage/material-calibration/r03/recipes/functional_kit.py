"""Authored kit metadata. C++ sidecar/catalog validation remains authoritative."""
from __future__ import annotations

import copy
import math
from pathlib import Path

import pontoon_parameters as pontoon

VISUAL_NAMESPACE = b'voxys-kit-art-v2'.ljust(16, b'\0').hex()
CARGO_NAMESPACE = b'voxys-cargo-v1'.ljust(16, b'\0').hex()
# Stable authored counter ranges, independent of definition/UI iteration order.
VISUAL_IDS = {'beam': 1, 'plate': 4, 'engine': 7, 'propeller': 10, 'helm': 13,
              'winch': 16, 'cradle': 19, 'generator': 22, 'crate': 25}
LOD_LIMITS = ((12000, 24000, 128, 200), (6000, 12000, 64, 60), (3000, 6000, 64, 0))
STRENGTH = dict(tension_newtons=30000, shear_newtons=24000,
                bending_newton_metres=18000, torsion_newton_metres=12000)


def frame(point=(0, 0, 0), rotation=0):
    return dict(translation_ticks=list(point), rotation=rotation)


def box(identifier, center, half):
    return dict(id=str(identifier), frame=frame(center), half_extents_ticks=list(half))


def socket(identifier, point, rotation, family='structural', role='receptacle'):
    bottom = role == 'receptacle'
    return dict(id=str(identifier), family=family, role=role, profile=1,
                frame=frame(point, rotation), connection_capacity=1,
                clearance=dict(minimum_ticks=[-15, -9 if bottom else 0, -15],
                               maximum_ticks=[15, 0 if bottom else 9, 15]),
                strength=STRENGTH.copy())


def mass_properties(boxes, mass):
    """Uniform density across disjoint proxy boxes, full parallel-axis tensor."""
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if all(abs(a['frame']['translation_ticks'][k] - b['frame']['translation_ticks'][k]) <
                   a['half_extents_ticks'][k] + b['half_extents_ticks'][k] for k in range(3)):
                raise ValueError('overlapping mass recipe boxes')
    volumes = [8 * math.prod(v / 50 for v in b['half_extents_ticks']) for b in boxes]
    total = math.fsum(volumes)
    centers = [[v / 50 for v in b['frame']['translation_ticks']] for b in boxes]
    center = [math.fsum(v * c[k] for v, c in zip(volumes, centers)) / total for k in range(3)]
    tensor = [0.] * 9
    for b, v, c in zip(boxes, volumes, centers):
        m = mass * v / total
        h = [n / 50 for n in b['half_extents_ticks']]
        r = [c[k] - center[k] for k in range(3)]
        for k in range(3):
            for j in range(k, 3):
                tensor[k * 3 + j] += m * ((sum(h[t] ** 2 for t in range(3) if t != k) / 3 +
                    sum(r[t] ** 2 for t in range(3))) if k == j else 0) - m * r[k] * r[j]
                tensor[j * 3 + k] = tensor[k * 3 + j]
    return dict(dry_mass_kg=mass, center_of_mass_metres=center, inertia_kg_metres_squared=tensor)


def definitions():
    result = {}

    def part(name, counter, half, mass, module, cost, yield_, boxes=None, cargo=False):
        boxes = boxes or [box(1, (0, 0, 0), half)]
        lower = [min(b['frame']['translation_ticks'][k] - b['half_extents_ticks'][k] for b in boxes) for k in range(3)]
        upper = [max(b['frame']['translation_ticks'][k] + b['half_extents_ticks'][k] for b in boxes) for k in range(3)]
        # Open equipment retains its declared build envelope even though its
        # collision/occupancy and mass proxies no longer fill that envelope.
        if half is not None:
            lower = [-v for v in half]
            upper = list(half)
        cores = []
        for b in boxes:
            core = copy.deepcopy(b)
            core['half_extents_ticks'][1] = min(2, core['half_extents_ticks'][1])
            cores.append(dict(box=core, kind='solid_material'))
        value = dict(key=dict(namespace=CARGO_NAMESPACE if cargo else pontoon.PART_NAMESPACE,
                              counter=str(counter), version=1 if cargo else 2),
                     name_key='salvage.' + ('cargo.' if cargo else 'part.') + name,
                     permitted_rotation_mask=16777215,
                     footprint=dict(minimum_ticks=lower, maximum_ticks=upper),
                     solid_occupancy=copy.deepcopy(boxes), collision=copy.deepcopy(boxes),
                     mass=mass_properties(boxes, mass), buoyancy=cores, sockets=[],
                     strength=STRENGTH.copy(), module=module,
                     cost=dict(salvage_material=str(cost), special_machinery='0'),
                     salvage_yield=dict(salvage_material=str(yield_), special_machinery='0'),
                     material=dict(linear_base_color=[1, 1, 1], roughness=1, metallic=1))
        result[name] = value
        return value

    for name, counter, half, mass, cost, yield_ in (
        ('beam', 1, (100, 8, 25), 90, 12, 8), ('plate', 2, (100, 8, 50), 80, 10, 7)):
        p = part(name, counter, half, mass, dict(type='structure'), cost, yield_)
        for i, x in enumerate((-75, -25, 25, 75)):
            p['sockets'] += [socket(100 + 2 * i, (x, 8, 0), 0, role='plug'),
                             socket(101 + 2 * i, (x, -8, 0), 2)]

    p = part('engine', 4, None, 160, dict(type='engine', shaft='10', maximum_power_watts=18000,
             maximum_torque_newton_metres=500), 30, 18,
             [box(1, (0, 6, 50), (25, 18, 25)), box(2, (0, -20, 12), (25, 4, 37)),
              box(3, (0, -61, 62), (6, 49, 8)), box(4, (0, -14, 40), (8, 2, 8))])
    p['cost']['special_machinery'] = '1'
    p['sockets'] = [socket(2, (0, -24, 0), 2), socket(10, (0, -104, 70), 1, 'drive_shaft', 'plug')]
    p = part('propeller', 5, (25, 24, 16), 35,
             dict(type='propeller', shaft='10', force_frame=frame((0, 0, 0)),
                  maximum_thrust_newtons=2500, required_power_watts=18000), 16, 10)
    p['sockets'] = [socket(10, (0, 0, -16), 3, 'drive_shaft')]
    p = part('helm', 6, (25, 24, 25), 30,
             dict(type='helm', operator_frame=frame((0, 24, 0)), maximum_steering_radians=.6), 10, 6,
             [box(1, (0, -20, 0), (25, 4, 25)),
              box(2, (0, -8, -6), (10, 8, 12)),
              box(3, (0, 7, -4), (21, 7, 15))])
    p['sockets'] = [socket(2, (0, -24, 0), 2)]
    p = part('winch', 7, (25, 48, 25), 140,
             dict(type='winch', line='10', minimum_length_metres=.5, maximum_length_metres=40,
                  reel_speed_metres_per_second=2, maximum_force_newtons=12000), 30, 20,
             [box(1, (0, -44, 0), (25, 4, 25)),
              box(2, (-18, -1, 0), (5, 39, 15)), box(3, (18, -1, 0), (5, 39, 15)),
              box(4, (0, 6, 0), (10, 14, 14)),
              box(5, (-12, 6, 0), (1, 18, 18)), box(6, (12, 6, 0), (1, 18, 18)),
              box(7, (0, 40, -18), (13, 5, 5))])
    p['sockets'] = [socket(2, (0, -48, 0), 2), socket(10, (0, 40, -25), 3, 'tow_line', 'plug')]
    p = part('cradle', 9, (50, 16, 50), 90,
             dict(type='cargo_cradle', latch='10', maximum_cargo_mass_kg=3000,
                  capture_distance_metres=.1, capture_angle_radians=math.pi / 36,
                  capture_speed_metres_per_second=.5, capture_angular_speed_radians_per_second=math.pi / 6), 14, 9,
             [box(1, (0, -12, 0), (38, 4, 50)),
              box(2, (-44, 0, 0), (6, 16, 50)), box(3, (44, 0, 0), (6, 16, 50)),
              box(4, (0, 4, 0), (17, 12, 17))])
    p['name_key'] = 'salvage.part.cargo_cradle'
    p['sockets'] = [socket(2, (0, -16, 0), 2), socket(10, (0, 16, 0), 0, 'cargo_latch')]
    for name, counter, half, mass, value, eye in (
        ('generator', 1, (45, 32, 40), 420, 60, (0, 26, 0)),
        ('crate', 2, (60, 40, 40), 700, 100, (25, 44, 0))):
        p = part(name, counter, half, mass, dict(type='tow_eye', eye='10'), value * 2, value, cargo=True)
        if name == 'crate':
            p['footprint']['maximum_ticks'][1] = 50  # Raised, offset lifting eye.
        p['sockets'] = [socket(10, eye, 3, 'tow_line'),
                        socket(11, (0, -half[1], 0), 2, 'cargo_latch', 'plug')]
    return result


def sidecar(name, source_dir):
    part = definitions()[name]
    asset_counter = VISUAL_IDS[name]
    lods = []
    for lod, (_, _, _, threshold) in enumerate(LOD_LIMITS):
        path = Path(source_dir) / f'{name}-lod-{lod}.glb'
        lods.append(dict(id=str(lod + 1), asset=dict(namespace=VISUAL_NAMESPACE,
            counter=str(asset_counter + lod), version=2), source=dict(file=path.name,
            sha256=pontoon.sha256(path), bytes=path.stat().st_size, frame='exported_gltf',
            to_canonical_rotation=12), minimum_screen_height_pixels=threshold))
    return dict(schema=1, units=dict(length='metres', mass='kilograms', time='seconds', angle='radians'),
                metadata_frame='canonical_y_up_minus_z_forward', placement_lattice_metres=.02,
                part=part, lods=lods, tool_anchors=[dict(id=s['id'],
                name_key=f"salvage.anchor.{name}.socket_{s['id']}", frame=s['frame']) for s in part['sockets']])
