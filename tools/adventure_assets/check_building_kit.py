#!/usr/bin/env python3
"""Check cooked kit dimensions, true openings, face coverage and piece IDs."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_catalog import SOURCE, validated_catalog, generate, OUTPUT


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def clipped(poly, lo, hi):
    for axis in range(3):
        for edge, sign in ((lo[axis], 1), (hi[axis], -1)):
            if not poly:
                return []
            result = []
            a = poly[-1]
            inside_a = (a[axis]-edge)*sign >= 0
            for b in poly:
                inside_b = (b[axis]-edge)*sign >= 0
                if inside_a != inside_b:
                    t = (edge-a[axis]) / (b[axis]-a[axis])
                    result.append(tuple(a[k]+t*(b[k]-a[k]) for k in range(3)))
                if inside_b:
                    result.append(b)
                a, inside_a = b, inside_b
            poly = result
    return poly


def inspect_mesh(path, pieces):
    raw = path.read_bytes()
    assert raw[:8] == b'VOXYMESH' and 256 <= len(raw) <= 2*1024*1024
    header = struct.unpack_from('<14I13Q', raw, 8)
    version, flags, nv, vs, ni, stride, ns, nm, nn, nmesh, nskin, nj, na, nac = header[:14]
    vo, io, so, mo, images, no, skins, anims, channels, channeldata, names, namesize, size = header[14:]
    assert version == 1 and vs == 72 and stride in (2,4) and nn == nmesh == 14
    assert not nskin and not nj and not na and not nac and images == no
    assert size == len(raw) and nm <= 5 and ns <= 56
    assert nv <= 22000 and ni <= 54000
    verts = [struct.unpack_from('<3f', raw, vo + i*vs) for i in range(nv)]
    normals = [struct.unpack_from('<3f', raw, vo + i*vs+12) for i in range(nv)]
    assert all(all(math.isfinite(x) for x in p) for p in verts)
    assert all(abs(sum(x*x for x in n)-1) < 2.e-4 for n in normals)
    ix = struct.unpack_from('<' + ('I' if stride == 4 else 'H')*ni, raw, io)
    assert all(i < nv for i in ix)
    subs = [struct.unpack_from('<4I', raw, so+i*16) for i in range(ns)]
    records = []
    for mesh, piece in enumerate(pieces):
        node = struct.unpack_from('<10fiIiI', raw, no+mesh*64)
        assert node[:10] == (0.,0.,0.,0.,0.,0.,1.,1.,1.,1.)
        assert node[10:13] == (-1, mesh, -1)
        namepos = names + node[13]
        name = raw[namepos:raw.index(0, namepos)].decode()
        assert name == f'{piece["id"]:02d}_{piece["key"]}'
        selected = [s for s in subs if s[3] == mesh]
        assert 1 <= len(selected) <= 4
        triangles = [tuple(verts[ix[k+j]] for j in range(3)) for start, count, _, _ in selected
                     for k in range(start//stride, start//stride+count, 3)]
        assert triangles
        solids = [([v/50 for v in b['minimum']], [v/50 for v in b['maximum']]) for b in piece['solids']]
        coverage = [[False]*6 for _ in solids]
        for tri in triangles:
            for point in tri:
                supported = False
                for n, (lo, hi) in enumerate(solids):
                    if all(lo[k]-.02001 <= point[k] <= hi[k]+.02001 for k in range(3)):
                        supported = True
                        for axis in range(3):
                            coverage[n][axis*2] |= abs(point[axis]-lo[axis]) < .02001
                            coverage[n][axis*2+1] |= abs(point[axis]-hi[axis]) < .02001
                if piece['key'].startswith('brick_'):
                    lo, hi = solids[0]
                    supported |= (hi[1] <= point[1] <= hi[1]+.18001 and
                                  all(lo[k] <= point[k] <= hi[k] for k in (0, 2)))
                assert supported, f'{piece["key"]}: visual exceeds solid/decor envelope: {point}'
            if piece['key'] == 'doorway':
                assert not clipped(list(tri), [-.675,.005,-.2], [.675,2.235,.2]), 'doorway filled by a visual triangle'
        assert all(all(faces) for faces in coverage), f'{piece["key"]}: invisible collision face'
        records.append(dict(id=piece['id'], mesh_index=mesh, name=name, draws=len(selected),
                            triangles=len(triangles), solid_boxes=len(solids),
                            collision_faces_supported=True, visual_envelope_passed=True))
    return dict(bytes=len(raw), sha256=sha(path), vertices=nv, indices=ni,
                materials=nm, meshes=nmesh, draws=ns, requested_gpu_bytes=nv*72+ni*4+nm*64,
                pieces=records)


def check(directory):
    doc = validated_catalog()
    assert OUTPUT.read_text() == generate(), 'C++ catalog differs'
    provenance = json.loads((directory/'provenance.json').read_text())
    assert provenance['catalog_sha256'] == sha(SOURCE)
    assert provenance['render_to_canonical'] == 0
    records = []
    for lod in (0, 1):
        records.append(dict(lod=lod, **inspect_mesh(directory/f'building-kit-lod{lod}.vmesh', doc['pieces'])))
    assert records[1]['vertices'] < records[0]['vertices']
    assert sum(r['requested_gpu_bytes'] for r in records) <= 3*1024*1024
    doorway = doc['pieces'][3]
    assert doorway['solids'][1]['minimum'][0]-doorway['solids'][0]['maximum'][0] == 68
    assert doorway['solids'][2]['minimum'][1] == 112
    stair = doc['pieces'][5]
    assert [b['maximum'][1] for b in stair['solids']] == [8,16,24,32,40,48]
    return dict(status='passed', catalog_sha256=sha(SOURCE), canonical_basis=0,
                door_clearance_metres=[1.36,2.24], stair_riser_metres=.16,
                cosmetic_studs='Bricks only, .18m above .96m authoritative body; no terrain/floor treads changed.', lods=records)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.directory), indent=2))
