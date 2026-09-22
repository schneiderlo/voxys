#!/usr/bin/env python3
"""Validate pinned cannon files, articulated mesh layout and muzzle/hinge geometry."""
import hashlib
import json
import math
from pathlib import Path
import struct

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'manifest.json').read_text())
for name, record in manifest['files'].items():
    data = (root / name).read_bytes()
    assert len(data) == record['bytes'], name
    assert hashlib.sha256(data).hexdigest() == record['sha256'], name
blob = (root / 'cannon.glb').read_bytes()
assert struct.unpack_from('<III', blob) == (0x46546c67, 2, len(blob))
length, kind = struct.unpack_from('<II', blob, 12)
assert kind == 0x4e4f534a
model = json.loads(blob[20:20 + length])
binary = blob[28 + length:]
contract = json.loads((root / 'articulation.json').read_text())
assert len(model['meshes']) == len(model['nodes']) == 2
vertices = []
triangles = 0
for entry in contract['mesh_layout']:
    node = model['nodes'][entry['node']]
    assert node['mesh'] == entry['mesh'] and node['name'] == entry['name']
    assert not any(key in node for key in ('matrix', 'translation', 'rotation', 'scale'))
    mesh_positions = []
    for primitive in model['meshes'][entry['mesh']]['primitives']:
        triangles += model['accessors'][primitive['indices']]['count'] // 3
        accessor = model['accessors'][primitive['attributes']['POSITION']]
        assert accessor['componentType'] == 5126 and accessor['type'] == 'VEC3'
        view = model['bufferViews'][accessor['bufferView']]
        start = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
        stride = view.get('byteStride', 12)
        mesh_positions.extend(struct.unpack_from('<3f', binary, start + i * stride)
                              for i in range(accessor['count']))
    vertices.append(mesh_positions)
assert triangles == manifest['mesh']['triangles'] == 2368
assert abs(min(v[1] for v in vertices[0])) < 1e-5
pivot, muzzle, axis = (contract[key] for key in ('barrel_pivot', 'muzzle_origin', 'muzzle_direction'))
assert abs(math.dist(pivot, muzzle) - 3.5) < 1e-5
assert abs(math.sqrt(sum(x*x for x in axis)) - 1) < 1e-6
assert abs(axis[0]) < 1e-6 and abs(math.degrees(math.atan2(axis[1], axis[2])) - 15) < 1e-4
# The muzzle is the extreme front barrel plane, not the closed rear cap.
projection = [sum((p[i] - pivot[i]) * axis[i] for i in range(3)) for p in vertices[1]]
assert abs(max(projection) - 3.5) < 1e-4
assert sum(abs(t - 3.5) < 1e-4 for t in projection) >= 16
# Rotating about the recorded hinge keeps the pivot fixed and muzzle coherent.
for elevation in (5, 15, 45):
    delta = -math.radians(elevation - 15)
    offset = [muzzle[i] - pivot[i] for i in range(3)]
    raised = (offset[0], math.cos(delta)*offset[1]-math.sin(delta)*offset[2],
              math.sin(delta)*offset[1]+math.cos(delta)*offset[2])
    assert abs(math.degrees(math.atan2(raised[1], raised[2])) - elevation) < 1e-4
print('Cannon validated: pinned files, two identity meshes, grounded base, source muzzle and 5/15/45-degree hinge poses.')
