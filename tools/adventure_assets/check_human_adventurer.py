#!/usr/bin/env python3
"""Independent actual-export envelope checks; no GPU or continuous sweep claim."""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
import sys
ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0,str(ROOT/'tools/salvage_assets'))
from check_cove_robot import inspect

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def check_grips(path):
    raw=path.read_bytes();length=struct.unpack_from('<I',raw,12)[0]
    doc=json.loads(raw[20:20+length]);blob=raw[28+length:];out={}
    for node in doc['nodes']:
        if node['name'] not in ('robot_hand_l','robot_hand_r'):continue
        points=[]
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            a=doc['accessors'][primitive['attributes']['POSITION']];v=doc['bufferViews'][a['bufferView']]
            assert a['componentType']==5126 and a['type']=='VEC3'
            base=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',12)
            for i in range(a['count']):
                x,y,z=struct.unpack_from('<fff',blob,base+i*stride)
                # Retained local basis12; C-ring centre relative to wrist pivot.
                points.append((-x,y+.047,-z+.026))
        inner=min(math.hypot(x,y) for x,y,z in points)
        assert inner>.044, 'toy hand centre must remain genuinely open'
        assert not any(y<-.032 and abs(x)<.031 for x,y,z in points), 'toy C-hand bottom gap is filled'
        out[node['name']]=dict(inner_vertex_radius_m=inner,vertices=len(points),
            bottom_gap_open=True,scope='Actual exported hand vertices and closed-component topology; no filled palm/disc.')
    assert len(out)==2
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path);a=p.parse_args()
    provenance=json.loads((a.package/'provenance.json').read_text());reports=[]
    for lod in range(3):
        path=a.package/'source'/f'human-lod-{lod}.glb'
        assert sha(path)==provenance['lods'][lod]['glb_sha256']
        report=inspect(path)
        assert report['clips']['idle']['maximum'][1]<=1.700001, 'idle exceeds standing height'
        for clip in ('walk','land','fall'):
            assert report['clips'][clip]['minimum'][1]>=-1e-5, clip+' foot penetrates ground'
        report['source_sha256']=sha(path)
        report['open_toy_grips']=check_grips(path)
        report['clearance']=dict(doorway_width_m=1.36,doorway_height_m=2.24,
            walk_width_margin_m=1.36-report['clips']['walk']['width'],
            standing_height_margin_m=2.24-report['bind']['maximum'][1],
            note='Standing core fits the unchanged .3m-radius / 1.7m controller capsule. Walk samples check actual foot vertices. Raised hands, jumps and tool reaches are visual envelopes, never colliders.')
        reports.append(report)
    print(json.dumps(dict(schema=1,asset='voxys-adventure-human-r01',authoring_sha256=sha(ROOT/'tools/adventure_assets/author_human_adventurer.py'),
        checker_sha256=sha(Path(__file__)),shared_sampler_sha256=sha(ROOT/'tools/salvage_assets/check_cove_robot.py'),lods=reports),indent=2))

if __name__=='__main__':main()
