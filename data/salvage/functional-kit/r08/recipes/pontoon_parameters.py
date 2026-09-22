"""Pure bounded parameter/metadata recipe for the original salvage Pontoon v2.

Shared physical invariants are finally checked by the C++ gameplay sidecar tool.
This module supplies authored inputs, not a replacement PartCatalog validator.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import stat

PART_NAMESPACE = bytes('voxys-salvage-v1', 'ascii').hex()
VISUAL_NAMESPACE = bytes('voxys-pontoon-v2', 'ascii').ljust(16, b'\0').hex()
FIELDS = {'schema', 'part_version', 'width_ticks', 'body_height_ticks', 'length_ticks',
          'dry_mass_kg', 'seed', 'palette_srgb'}
PALETTE = {'cream', 'teal', 'coral', 'slate', 'steel'}
LOD_LIMITS = ((6000, 12000, 512, 200), (1200, 2400, 256, 60), (400, 800, 64, 0))
PEG_HEIGHT = .18
WELL_DEPTH = .20
VERSION_TWO_SPEC = {'schema':1,'part_version':2,'width_ticks':50,'body_height_ticks':48,
                    'length_ticks':200,'dry_mass_kg':120,'seed':1979,
                    'palette_srgb':{'cream':'D9C9A2','teal':'2A6767','coral':'CF6548',
                                    'slate':'354852','steel':'869498'}}


def mount_shape(lod, well=False):
    # Coarser wells are deliberately enlarged to contain every finer peg LOD.
    return (((.25, .27, .295)[lod], (.21, .23, .26)[lod], (20, 10, 5)[lod])
            if well else (.24, .20, (20, 10, 5)[lod]))


def keyed_profile(radius, flat, segments):
    alpha = math.acos(flat/radius)
    return [(radius*math.cos(alpha+(2*math.pi-2*alpha)*i/segments),
             radius*math.sin(alpha+(2*math.pi-2*alpha)*i/segments)) for i in range(segments+1)]


def cross_lod_clearances():
    results = []
    for peg_lod in range(3):
        peg = keyed_profile(*mount_shape(peg_lod))
        for well_lod in range(3):
            well = keyed_profile(*mount_shape(well_lod,True))
            clearance = math.inf
            for a,b in zip(well,well[1:]+well[:1]):
                dx,dz = b[0]-a[0],b[1]-a[1]
                for x,z in peg:
                    clearance = min(clearance,(dx*(z-a[1])-dz*(x-a[0]))/math.hypot(dx,dz))
            if clearance <= 0:
                raise ValueError('cross-LOD peg/well interference')
            results.append({'peg_lod':peg_lod,'well_lod':well_lod,
                            'minimum_profile_clearance_metres':clearance,
                            'axial_spare_depth_metres':WELL_DEPTH-PEG_HEIGHT})
    return results


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate parameter: {key}')
        result[key] = value
    return result


def read_spec(path: Path) -> dict:
    flags = os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= 65536:
            raise ValueError('parameter input must be a regular file of at most 64 KiB')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(65537)
        if len(data) != info.st_size:
            raise ValueError('parameter input changed or exceeded its bound')
    finally:
        os.close(fd)
    value = json.loads(data, object_pairs_hook=no_duplicates,
                       parse_constant=lambda token: (_ for _ in ()).throw(ValueError(token)))
    return validate_spec(value)


def validate_spec(value: dict) -> dict:
    if not isinstance(value, dict) or set(value) != FIELDS:
        raise ValueError('unknown or missing pontoon parameter field')
    for name in ('schema', 'part_version', 'width_ticks', 'body_height_ticks', 'length_ticks', 'seed'):
        if type(value[name]) is not int:
            raise ValueError(f'{name}: expected integer')
    if value['schema'] != 1 or not 2 <= value['part_version'] <= 2**32 - 1:
        raise ValueError('unsupported recipe schema or reserved prototype part version')
    for name, low, high, step in (('width_ticks', 50, 80, 2), ('body_height_ticks', 48, 72, 2),
                                 ('length_ticks', 200, 300, 20)):
        if not low <= value[name] <= high or value[name] % step:
            raise ValueError(f'{name}: outside [{low},{high}] or not a multiple of {step}')
    mass = value['dry_mass_kg']
    if type(mass) not in (int, float) or not math.isfinite(mass) or not 20 <= mass <= 500:
        raise ValueError('dry_mass_kg: expected finite mass in [20,500]')
    if not 0 <= value['seed'] <= 2**32 - 1:
        raise ValueError('seed: expected u32')
    colors = value['palette_srgb']
    if not isinstance(colors, dict) or set(colors) != PALETTE:
        raise ValueError('palette_srgb: unknown or missing color')
    for name, color in colors.items():
        if not isinstance(color, str) or len(color) != 6 or any(c not in '0123456789ABCDEF' for c in color):
            raise ValueError(f'palette_srgb.{name}: expected six uppercase hexadecimal digits')
    if value['part_version']==2 and value!=VERSION_TWO_SPEC:
        raise ValueError('version 2 controls are reserved; changed controls require a new explicit part version')
    return value


def half_dimensions(spec):
    return tuple(spec[key] / 100.0 for key in ('width_ticks', 'body_height_ticks', 'length_ticks'))


def section(spec, z):
    x, y, length = half_dimensions(spec)
    t = abs(z) / length
    if t <= .7:
        return x, y, y / 6
    if t <= .9:
        alpha = (t - .7) / .2
        return x * (1 - .16 * alpha), y * (1 - alpha / 6), y * (1 - .5 * alpha) / 6
    alpha = min(1.0, (t - .9) / .1)
    return x * (.84 - .24 * alpha), y * (5 / 6 - alpha / 4), y * (1 - .5 * alpha) / 12


def proxy_recipe(spec):
    _, _, length = half_dimensions(spec)
    half_ticks = round(length * 50)
    # Even boundaries give integral box centers as well as integral extents.
    boundaries = [round(half_ticks * ratio / 2) * 2 for ratio in (0, .7, .8, .9, .94, 1)]
    result = []
    def append(identifier, center, hx, hy, hz):
        result.append({'id': str(identifier), 'frame': {'translation_ticks': [0, 0, center], 'rotation': 0},
                       'half_extents_ticks': [hx, hy, hz]})
    append(1, 0, spec['width_ticks'] // 2, spec['body_height_ticks'] // 2, boundaries[1])
    for sign, start_id in ((-1, 2), (1, 6)):
        for index, (low, high) in enumerate(zip(boundaries[1:-1], boundaries[2:])):
            center = (low + high) // 2
            hx, hy, _ = section(spec, center / 50)
            append(start_id + index, sign * center, round(hx * 50), round(hy * 50), (high - low) // 2)
    return result


def physical_recipe(spec):
    boxes = proxy_recipe(spec)
    volumes = [8 * math.prod(x / 50 for x in b['half_extents_ticks']) for b in boxes]
    volume = math.fsum(volumes)
    inertia = [0.0, 0.0, 0.0]
    for box, v in zip(boxes, volumes):
        mass = spec['dry_mass_kg'] * v / volume
        x, y, z = (n / 50 for n in box['half_extents_ticks'])
        offset = box['frame']['translation_ticks'][2] / 50
        inertia[0] += mass * ((y*y + z*z) / 3 + offset*offset)
        inertia[1] += mass * ((x*x + z*z) / 3 + offset*offset)
        inertia[2] += mass * (x*x + y*y) / 3
    return boxes, volume, inertia


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sidecar(spec, source_dir):
    boxes, _, inertia = physical_recipe(spec)
    h = [spec[key] // 2 for key in ('width_ticks', 'body_height_ticks', 'length_ticks')]
    strength = {'tension_newtons': 30000, 'shear_newtons': 24000,
                'bending_newton_metres': 18000, 'torsion_newton_metres': 12000}
    sockets = []
    for first, z in ((1, 0), (101, -50), (103, 50)):
        for bottom in (False, True):
            sockets.append({'id': str(first + int(bottom)), 'family': 'structural',
                'role': 'receptacle' if bottom else 'plug', 'profile': 1,
                'frame': {'translation_ticks': [0, -h[1] if bottom else h[1], z], 'rotation': 2 if bottom else 0},
                'connection_capacity': 1,
                'clearance': {'minimum_ticks': [-15, -9 if bottom else 0, -15],
                              'maximum_ticks': [15, 0 if bottom else 9, 15]},
                'strength': strength.copy()})
    lods = []
    for lod, (_, _, _, threshold) in enumerate(LOD_LIMITS):
        filename = f'pontoon-lod-{lod}.glb'
        path = Path(source_dir) / filename
        lods.append({'id': str(lod + 1), 'asset': {'namespace': VISUAL_NAMESPACE,
            'counter': str(lod + 1), 'version': spec['part_version']},
            'source': {'file': filename, 'sha256': sha256(path), 'bytes': path.stat().st_size,
                       'frame': 'exported_gltf', 'to_canonical_rotation': 12},
            'minimum_screen_height_pixels': threshold})
    return {'schema': 1, 'units': {'length': 'metres', 'mass': 'kilograms', 'time': 'seconds', 'angle': 'radians'},
        'metadata_frame': 'canonical_y_up_minus_z_forward', 'placement_lattice_metres': .02,
        'part': {'key': {'namespace': PART_NAMESPACE, 'counter': '3', 'version': spec['part_version']},
                 'name_key': 'salvage.part.pontoon', 'permitted_rotation_mask': 16777215,
                 'footprint': {'minimum_ticks': [-v for v in h], 'maximum_ticks': h},
                 'solid_occupancy': boxes, 'collision': boxes,
                 'mass': {'dry_mass_kg': spec['dry_mass_kg'], 'center_of_mass_metres': [0, 0, 0],
                          'inertia_kg_metres_squared': [inertia[0], 0, 0, 0, inertia[1], 0, 0, 0, inertia[2]]},
                 'buoyancy': [{'box': box, 'kind': 'sealed_compartment'} for box in boxes],
                 'sockets': sockets, 'strength': strength, 'module': {'type': 'flotation', 'drag_coefficients': [.9, 1.2, .4]},
                 'cost': {'salvage_material': '24', 'special_machinery': '0'},
                 'salvage_yield': {'salvage_material': '16', 'special_machinery': '0'},
                 'material': {'linear_base_color': [1, 1, 1], 'roughness': 1, 'metallic': 1}},
        'lods': lods, 'tool_anchors': [{'id': str(i), 'name_key': name,
            'frame': {'translation_ticks': point, 'rotation': 0}}
            for i, name, point in ((1, 'salvage.anchor.pontoon.center', [0, 0, 0]),
                                    (2, 'salvage.anchor.pontoon.bow', [0, 0, -h[2]]),
                                    (3, 'salvage.anchor.pontoon.mount', [0, h[1], 0]))]}
