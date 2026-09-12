#!/usr/bin/env python3
"""Numeric check of real exported Cove geometry and declared collision; no images."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

EXPECTED={'scenery':{'cove_workshop','cove_wreck','cove_east_beacon','cove_west_beacon','cove_shore'},'gantry':{'cove_gantry'}}


def inspect(path,boxes,kind):
    data=path.read_bytes();length=struct.unpack_from('<I',data,12)[0]
    doc=json.loads(data[20:20+length]);binary=data[28+length:]
    assert not any(doc.get(k) for k in ('animations','skins','images','textures','cameras','extensionsUsed','extensionsRequired'))
    def values(index):
        a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        fmt,size={5126:('f',4),5123:('H',2),5125:('I',4)}[a['componentType']]
        offset=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',width*size)
        return [struct.unpack_from('<'+fmt*width,binary,offset+i*stride) for i in range(a['count'])]
    assert {n['name'] for n in doc['nodes']}==EXPECTED[kind]
    assert set(doc['scenes'][doc.get('scene',0)]['nodes'])==set(range(len(doc['nodes'])))
    points=[];normerror=0
    for node in doc['nodes']:
        assert not node.get('children') and node.get('rotation',[0,0,0,1])==[0,0,0,1]
        assert node.get('scale',[1,1,1])==[1,1,1] and node.get('translation',[0,0,0])==[0,0,0]
        for p in doc['meshes'][node['mesh']]['primitives']:
            assert set(p['attributes'])=={'POSITION','NORMAL'}
            for v in values(p['attributes']['POSITION']):points.append([-v[0],v[1],-v[2]])
            for n in values(p['attributes']['NORMAL']):normerror=max(normerror,abs(math.sqrt(sum(v*v for v in n))-1))
    assert normerror<1e-4
    bounds=[([v*.02 for v in b['minimum']],[v*.02 for v in b['maximum']]) for b in boxes]
    def outside(p,b):return max(*(a-v for a,v in zip(b[0],p)),*(v-a for a,v in zip(b[1],p)),0)
    worst=max(min(outside(p,b) for b in bounds) for p in points)
    assert worst<=.02001,('visual geometry outside declared collision allowance',kind,worst)
    # Every declared face has authored vertex support. This rejects invisible
    # boxes while allowing the small rounded edge and raised molded inlay.
    faces=[]
    for b in bounds:
        local=[p for p in points if outside(p,b)<=.02001];assert local
        errors=[min(abs(p[a]-b[side][a]) for p in local) for a in range(3) for side in range(2)]
        assert max(errors)<=.02001,('unsupported collision face',b,errors)
        faces.append(max(errors))
    return dict(file=path.name,vertices=len(points),nodes=len(doc['nodes']),
        maximum_normal_length_error=normerror,maximum_visual_allowance=worst,
        maximum_collision_face_gap=max(faces),bounds=dict(minimum=[min(p[i] for p in points) for i in range(3)],
        maximum=[max(p[i] for p in points) for i in range(3)]),sha256=hashlib.sha256(data).hexdigest())


def check(path):
    provenance=json.loads((path/'provenance.json').read_text())
    boxes=provenance['collision'];assert 1<=len(boxes)<=48
    for b in boxes:
        lo,hi=b['minimum'],b['maximum'];assert all(isinstance(x,int) and abs(x)<=5000 for x in lo+hi)
        assert all(a<b for a,b in zip(lo,hi))
        # Do not consume the pre-existing dock/boat/cargo passage even below water.
        assert hi[0]<=-450 or lo[0]>=400 or hi[2]<=-3850 or lo[2]>=-2350,b
    # Exact entry support: .5m tread, .32m rise, matching the1.28m dock level.
    steps=[b for b in boxes if b['name'].startswith('entry_step_')]
    assert len(steps)==4
    for i,b in enumerate(steps):
        assert b['minimum']==[400+i*25,32,-2100] and b['maximum']==[425+i*25,80+i*16,-2000]
    # Empty walkable approach above stairs and shed floor at1.7m robot height.
    for lo,hi in (([8.05,1.62,-41.9],[8.45,3.32,-40.1]),([10.35,2.58,-41.5],[12.4,4.28,-39.5])):
        for b in boxes:
            a,c=([v*.02 for v in b[k]] for k in ('minimum','maximum'))
            assert not all(x<q and y>p for x,y,p,q in zip(lo,hi,a,c)),('blocked workshop entry',b)
    rows=[]
    for kind in ('scenery','gantry'):
        for lod in range(3):rows.append(inspect(path/'source'/f'{kind}-lod-{lod}.glb',boxes if kind=='scenery' else provenance['gantry_collision'],kind))
    return dict(status='passed',collision_boxes=len(boxes),scene_origin=provenance['scene_origin'],rows=rows,
        protected_berth=provenance['protected_berth'],entry='Four half-metre treads, .32m rises; front/west workshop approach remains open.',
        limits='Source geometry/collision/normals only; no rendered image, physics completion or final visual approval.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);a=p.parse_args();print(json.dumps(check(a.source),indent=2))
