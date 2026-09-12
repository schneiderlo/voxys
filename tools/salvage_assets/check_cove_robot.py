#!/usr/bin/env python3
"""Read actual exported robot keys and vertices; report numeric pose envelopes.

Independent CPU source-art check, not GPU completion or visual approval. Current
runtime recomputes bounds each pose, so these samples are never a culling bound.
"""
import argparse
import json
import math
from pathlib import Path
import struct


def identity():return [[float(i==j) for j in range(4)] for i in range(4)]
def mul(a,b):return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def point(m,p):return [sum(m[i][j]*p[j] for j in range(3))+m[i][3] for i in range(3)]
def transform(t,q):
    x,y,z,w=q
    return [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),t[0]],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),t[1]],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),t[2]],[0,0,0,1]]
def slerp(a,b,t):
    def unit(q):
        n=math.sqrt(sum(v*v for v in q));q=[v/n for v in q]
        return [-v for v in q] if q[max(range(4),key=lambda i:abs(q[i]))]<0 else q
    a,b=unit(a),unit(b);dot=sum(x*y for x,y in zip(a,b))
    if dot<0:b=[-v for v in b];dot=-dot
    if dot>.9995:return unit([x+(y-x)*t for x,y in zip(a,b)])
    angle=math.acos(min(1,dot));denom=math.sin(angle)
    return [(x*math.sin((1-t)*angle)+y*math.sin(t*angle))/denom for x,y in zip(a,b)]


def inspect(path):
    raw=path.read_bytes();length=struct.unpack_from('<I',raw,12)[0]
    doc=json.loads(raw[20:20+length]);binary=raw[28+length:]
    def values(index):
        a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        fmt,size={5126:('f',4),5123:('H',2),5125:('I',4)}[a['componentType']]
        base=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',width*size)
        return [struct.unpack_from('<'+fmt*width,binary,base+i*stride) for i in range(a['count'])]
    meshpoints=[];normal_error=0
    for mesh in doc['meshes']:
        positions=[]
        for primitive in mesh['primitives']:
            positions+=values(primitive['attributes']['POSITION'])
            for n in values(primitive['attributes']['NORMAL']):
                normal_error=max(normal_error,abs(math.sqrt(sum(v*v for v in n))-1))
        meshpoints.append(positions)
    assert normal_error<1e-4
    parents={child:i for i,n in enumerate(doc['nodes']) for child in n.get('children',[])}
    def pose(animation,time):
        ts=[n.get('translation',[0,0,0]) for n in doc['nodes']]
        qs=[n.get('rotation',[0,0,0,1]) for n in doc['nodes']]
        if animation:
            for c in animation['channels']:
                s=animation['samplers'][c['sampler']];times=[v[0] for v in values(s['input'])];vs=values(s['output'])
                left=max(0,min(len(times)-2,sum(v<=time for v in times)-1));right=left+1
                t=max(0,min(1,(time-times[left])/(times[right]-times[left])))
                if s.get('interpolation','LINEAR')=='STEP':t=float(time>=times[right])
                node=c['target']['node'];prop=c['target']['path']
                if prop=='rotation':qs[node]=slerp(vs[left],vs[right],t)
                elif prop=='translation':ts[node]=[a+(b-a)*t for a,b in zip(vs[left],vs[right])]
                else:raise ValueError('animated scale')
        matrices={}
        def world(i):
            if i not in matrices:
                local=transform(ts[i],qs[i]);matrices[i]=mul(world(parents[i]),local) if i in parents else local
            return matrices[i]
        result={}
        for i,n in enumerate(doc['nodes']):
            if 'mesh' in n:result[n['name']]=[[-p[0],p[1],-p[2]] for p in (point(world(i),v) for v in meshpoints[n['mesh']])]
        return result
    def bounds(poses):
        low=[math.inf]*3;high=[-math.inf]*3;radius=0;core_radius=0
        for points in poses:
            for name,positions in points.items():
                for p in positions:
                    low=[min(a,b) for a,b in zip(low,p)];high=[max(a,b) for a,b in zip(high,p)]
                    r=math.hypot(p[0],p[2]);radius=max(radius,r)
                    if not any(piece in name for piece in ('arm','hand')):core_radius=max(core_radius,r)
        return dict(minimum=low,maximum=high,width=high[0]-low[0],horizontal_radius=radius,core_horizontal_radius=core_radius)
    reports={}
    for animation in doc['animations']:
        duration=max(values(s['input'])[-1][0] for s in animation['samplers'])
        count=121 if animation['name']=='walk' else 17
        samples=[pose(animation,duration*i/(count-1)) for i in range(count)]
        reports[animation['name']]=dict(duration=duration,samples=count,
            **bounds(samples),node_horizontal_radius={name:max(math.hypot(p[0],p[2])
                for sample in samples for p in sample[name]) for name in samples[0]})
    neutral=pose(None,0)
    bind=bounds([neutral])
    radii={name:max(math.hypot(p[0],p[2]) for p in positions) for name,positions in neutral.items()}
    # Exact point-to-segment distance for the real upright capsule, including
    # its upper/lower hemispheres. Hands and animated stride have an explicit
    # wider visual envelope; the stationary torso/helmet never use that waiver.
    core_capsule={name:max(math.sqrt(p[0]**2+p[2]**2+(p[1]-max(.3,min(1.4,p[1])))**2)
        for p in neutral[name]) for name in ('robot_pelvis','robot_torso','robot_head')}
    assert abs(bind['maximum'][1]-1.7)<.001 and bind['minimum'][1]>=-1e-6
    assert max(radii['robot_torso'],radii['robot_head'],radii['robot_pelvis'])<.30
    assert max(core_capsule.values())<=.300001, core_capsule
    assert reports['walk']['width']<=.60 and reports['walk']['minimum'][1]>=-1e-5
    return dict(file=str(path),maximum_normal_length_error=normal_error,
        bind=bind,bind_node_horizontal_radius=radii,core_capsule_distance=core_capsule,clips=reports,
        limitation='Sampled actual vertices; not a continuous sweep or visual/runtime acceptance.')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('package',type=Path)
    args=parser.parse_args()
    print(json.dumps(dict(lods=[inspect(args.package/'source'/f'robot-lod-{lod}.glb') for lod in range(3)]),indent=2))


if __name__=='__main__':main()
