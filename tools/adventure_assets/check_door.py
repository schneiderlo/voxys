#!/usr/bin/env python3
"""Bounded CPU checks of the actual leaf, hinge transform and frozen frame."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_catalog import ADDON_SOURCE, SOURCE, validated_door, catalog_fingerprint
from check_building_kit import clipped

ROOT = Path(__file__).resolve().parents[2]
FRAME = ROOT/'data/adventure/building-kit-r01/cooked/building-kit-lod0.vmesh'
FRAME_SHA256 = '08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect(path):
    raw = path.read_bytes()
    assert raw[:8] == b'VOXYMESH' and 256 <= len(raw) <= 512*1024
    h = struct.unpack_from('<14I13Q', raw, 8)
    version, _, nv, vs, ni, stride, ns, nm, nn, nmesh, nskin, nj, na, nac = h[:14]
    vo, io, so, mo, images, no, _, _, _, _, strings, _, size = h[14:]
    assert version == 1 and vs == 72 and stride in (2,4) and nn == nmesh == 1
    assert not any((nskin,nj,na,nac)) and images == no
    assert size == len(raw) and nm == ns == 2 and nv <= 4000 and ni <= 12000
    name = lambda off: raw[strings+off:raw.index(0,strings+off)].decode()
    node = struct.unpack_from('<10fiIiI', raw, no)
    assert node[:10] == (0.,0.,0.,0.,0.,0.,1.,1.,1.,1.) and node[10:13] == (-1,0,-1)
    assert name(node[13]) == '15_hinged_door_leaf'
    material_names = [name(struct.unpack_from('<I',raw,mo+i*128+124)[0]) for i in range(nm)]
    assert set(material_names) == {'adventure_door_terracotta','adventure_door_brass'}
    points = [struct.unpack_from('<3f',raw,vo+i*vs) for i in range(nv)]
    normals = [struct.unpack_from('<3f',raw,vo+i*vs+12) for i in range(nv)]
    assert all(math.isfinite(v) for point in points for v in point)
    assert all(abs(sum(v*v for v in normal)-1) < 2.e-4 for normal in normals)
    indices = struct.unpack_from('<'+('H' if stride == 2 else 'I')*ni,raw,io)
    assert ni%3 == 0 and all(i < nv for i in indices)
    submeshes = [struct.unpack_from('<4I',raw,so+i*16) for i in range(ns)]
    ranges = []
    for start,count,material,owner in submeshes:
        assert start%stride == 0 and count%3 == 0 and material < nm and owner == 0
        ranges += list(range(start//stride,start//stride+count))
    assert sorted(ranges) == list(range(ni))
    low, high = [-.64,.04,-.04], [.64,2.20,.04]
    open_low, open_high = [-.68,.04,0.], [-.60,2.20,1.28]
    opened = lambda p: (-.64-p[2],p[1],p[0]+.64)
    assert all(low[a]-1.e-6 <= p[a] <= high[a]+1.e-6 for p in points for a in (0,1))
    assert all(-.060001 <= p[2] <= .060001 for p in points)
    for axis in range(3):
        for face in (low[axis],high[axis]):
            assert any(abs(p[axis]-face) < 2.e-5 for p in points), 'collision face has no corresponding geometry'
    # The same exact integer quarter-turn sends all closed collider corners to
    # the installed open box. Relief may only extend .02m beyond it.
    corners = [opened(tuple((high[a] if bit&(1<<a) else low[a]) for a in range(3))) for bit in range(8)]
    for axis in range(3):
        assert abs(min(p[axis] for p in corners)-open_low[axis]) < 1.e-12
        assert abs(max(p[axis] for p in corners)-open_high[axis]) < 1.e-12
    assert all(open_low[a]-.020001 <= opened(p)[a] <= open_high[a]+.020001 for p in points for a in range(3))
    volume = 0.
    for offset in range(0,ni,3):
        triangle = [points[indices[offset+i]] for i in range(3)]
        a,b,c = triangle
        ab,ac = [b[i]-a[i] for i in range(3)],[c[i]-a[i] for i in range(3)]
        cross = (ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
        assert sum(x*x for x in cross) > 1.e-18, 'degenerate triangle'
        normal = [sum(normals[indices[offset+i]][axis] for i in range(3)) for axis in range(3)]
        assert sum(cross[i]*normal[i] for i in range(3)) > 0, 'winding disagrees with exported normals'
        volume += sum(a[i]*(b[(i+1)%3]*c[(i+2)%3]-b[(i+2)%3]*c[(i+1)%3]) for i in range(3))/6
        assert not clipped([opened(p) for p in triangle],[-.575,.045,-.15],[.675,2.195,.15]), 'opened leaf blocks the doorway corridor'
    assert volume > .20
    return dict(bytes=len(raw),sha256=sha(path),vertices=nv,indices=ni,triangles=ni//3,draws=ns,
        materials=nm,meshes=nmesh,nodes=nn,requested_gpu_bytes=nv*72+ni*4+nm*64,
        bounds={'minimum':[min(p[a] for p in points) for a in range(3)],'maximum':[max(p[a] for p in points) for a in range(3)]},
        collision_faces_supported=True,hinge_transform_checked=True,open_clear_corridor_width_metres=1.25,
        maximum_cosmetic_relief_metres=max(abs(p[2])-.04 for p in points),signed_volume_m3=volume)


def check(directory):
    doc = validated_door()
    assert sha(FRAME) == FRAME_SHA256
    provenance = json.loads((directory/'provenance.json').read_text())
    assert provenance['asset_id'] == doc['asset_id'] and provenance['render_to_canonical'] == 0
    assert provenance['catalog_sha256'] == sha(ADDON_SOURCE)
    assert provenance['closed_leaf_ticks'] == doc['piece']['solids'][3] and provenance['open_leaf_ticks'] == doc['open_leaf']
    records = [dict(lod=lod,**inspect(directory/f'door-leaf-lod{lod}.vmesh')) for lod in (0,1)]
    assert records[1]['vertices'] < records[0]['vertices']
    return dict(status='passed',canonical_basis=0,catalog_sha256=sha(ADDON_SOURCE),catalog_fingerprint=catalog_fingerprint(),
        legacy_catalog_sha256=sha(SOURCE),frame_source=str(FRAME.relative_to(ROOT)),frame_sha256=FRAME_SHA256,
        frame_mesh_index=3,leaf_mesh_index=0,hinge_ticks=doc['hinge'],open_yaw_degrees=-90,
        maximum_composed_draws=4,lods=records,
        limits='CPU geometry/source verification only. No runtime interaction, obstruction sweep, placement or performance acceptance.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.directory),indent=2))
