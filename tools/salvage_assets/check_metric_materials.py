#!/usr/bin/env python3
"""Check actual metric-profile GLBs, including texture orientation and UV scale.

Focused diagnostic for these identity-node Blender exports. Requires Pillow.
Finite height differences and exported tangent frames check the analytic normal
recipe independently of Blender's source-mesh UV report. Not a visual gate.
"""
import argparse
from collections import Counter
import hashlib
from io import BytesIO
import json
import math
from pathlib import Path
import struct

from PIL import Image

from check_material_calibration import cross, dot, subtract


def unit(a):
    length=math.sqrt(dot(a,a))
    return tuple(x/length for x in a)


class Model:
    def __init__(self,path):
        data=path.read_bytes();self.sha=hashlib.sha256(data).hexdigest()
        assert struct.unpack_from('<4sII',data)==(b'glTF',2,len(data))
        size,kind=struct.unpack_from('<II',data,12);assert kind==0x4e4f534a
        self.doc=json.loads(data[20:20+size]);offset=20+size
        count,kind=struct.unpack_from('<II',data,offset);assert kind==0x004e4942
        self.binary=data[offset+8:offset+8+count]
        assert len(self.doc['nodes'])==len(self.doc['meshes'])==1
        assert set(self.doc['nodes'][0])<={'name','mesh'}
        self.primitives=[]
        for p in self.doc['meshes'][0]['primitives']:
            attrs={name:self.accessor(a) for name,a in p['attributes'].items()}
            indices=[a[0] for a in self.accessor(p['indices'])]
            self.primitives.append((attrs,[indices[i:i+3] for i in range(0,len(indices),3)],self.doc['materials'][p['material']]))

    def accessor(self,index):
        a=self.doc['accessors'][index];v=self.doc['bufferViews'][a['bufferView']]
        assert 'sparse' not in a and not a.get('normalized',False)
        fmt='<'+{5123:'H',5125:'I',5126:'f'}[a['componentType']]*{'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        start=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',struct.calcsize(fmt))
        return [struct.unpack_from(fmt,self.binary,start+i*stride) for i in range(a['count'])]

    def image(self,index):
        item=self.doc['images'][self.doc['textures'][index]['source']]
        assert item['mimeType']=='image/png'
        view=self.doc['bufferViews'][item['bufferView']];start=view.get('byteOffset',0)
        return Image.open(BytesIO(self.binary[start:start+view['byteLength']])).convert('RGBA')

    def geometry(self):
        return Counter(tuple(sorted(tuple(a['POSITION'][i]) for i in ids)) for a,triangles,_ in self.primitives for ids in triangles)

    def sample(self,origin,direction):
        # Rotate canonical ray into the identity glTF node frame (rotation 12).
        origin=(-origin[0],origin[1],-origin[2]);direction=(-direction[0],direction[1],-direction[2])
        closest=None
        for attrs,triangles,material in self.primitives:
            for ids in triangles:
                a,b,c=[attrs['POSITION'][i] for i in ids];e1,e2=subtract(b,a),subtract(c,a)
                p=cross(direction,e2);d=dot(e1,p)
                if abs(d)<1e-10:continue
                delta=subtract(origin,a);u=dot(delta,p)/d;q=cross(delta,e1);v=dot(direction,q)/d;t=dot(e2,q)/d
                if min(u,v)<-1e-7 or u+v>1+1e-7 or t<1e-6:continue
                if closest is None or t<closest[0]:closest=(t,material['name'])
        assert closest is not None
        return closest


def height(u,v):
    # Declared physical fixture, expressed in metres. Finite differences below
    # do not call the authoring module or copy its analytic derivatives.
    return sum(a*1e-6*math.cos(2*math.pi*(fx*u+fy*v+phase)) for fx,fy,a,phase in
               ((5,5,9,.13),(7,-3,8,.71),(11,4,6,.39),(-5,13,5,.83),
                (17,7,4,.27),(9,-19,3,.59),(23,-11,2,.91),(-17,25,2,.47)))


def check(model,lod):
    densities=[];stretches=[];alignments=[];areas=[];rows=[];normal_error=0.;checked_pixels=0
    for attrs,triangles,material in model.primitives:
        name=material['name'].removeprefix('salvage_metric_').split('_lod')[0]
        targets={'cream':(.34,0,'D9C9A2'),'teal':(.40,0,'2A6767'),'coral':(.36,0,'CF6548'),
                 'slate':(.73,0,'354852'),'steel':(.30,1,'869498'),'rubber':(.78,0,'232D30')}
        rough,metal,color=targets[name];pbr=material['pbrMetallicRoughness']
        actual=[]
        for x in pbr['baseColorFactor'][:3]:actual.append(round((12.92*x if x<=.0031308 else 1.055*x**(1/2.4)-.055)*255))
        assert actual==[int(color[i:i+2],16) for i in (0,2,4)]
        assert abs(pbr.get('metallicFactor',1)-metal)<1e-6
        assert 'baseColorTexture' not in pbr
        if lod<2:
            mr=model.image(pbr['metallicRoughnessTexture']['index']);normal=model.image(material['normalTexture']['index'])
            size=(128,64)[lod];assert normal.size==mr.size==(size,size)
            rough_values=[p[1]/255*pbr['roughnessFactor'] for p in mr.getdata()]
            assert abs(sum(rough_values)/len(rough_values)-rough)<.001
            assert all(p[0]==p[2]==p[3]==255 for p in mr.getdata())
            # Sample asymmetric rows/columns so an accidental green flip or
            # PNG row inversion cannot satisfy a symmetric height fixture.
            epsilon=1e-6
            for y in range(1,size,7):
                for x in range(2,size,9):
                    u=(x+.5)/size;v=1-(y+.5)/size
                    du=(height(u+epsilon,v)-height(u-epsilon,v))/(epsilon) # 2 eps * .5m
                    dv=(height(u,v+epsilon)-height(u,v-epsilon))/(epsilon)
                    expected=unit((-du,-dv,1));pixel=normal.getpixel((x,y))
                    error=max(abs(pixel[i]/255*2-1-expected[i]) for i in range(3))
                    normal_error=max(normal_error,error);assert error<=1/255+1e-7
                    checked_pixels+=1
        else:
            assert 'normalTexture' not in material and 'metallicRoughnessTexture' not in pbr
            assert abs(pbr['roughnessFactor']-rough)<1e-6
        for ids in triangles:
            a,b,c=[attrs['POSITION'][i] for i in ids];e1,e2=subtract(b,a),subtract(c,a)
            area=math.sqrt(dot(cross(e1,e2),cross(e1,e2)))*.5
            if area<1e-10:continue
            ua,ub,uc=[attrs['TEXCOORD_0'][i] for i in ids]
            axis=unit(e1);length=math.sqrt(dot(e1,e1));offset=dot(e2,axis)
            h=math.sqrt(max(0,dot(e2,e2)-offset*offset))
            j1=tuple(v/length for v in subtract(ub,ua));j2=tuple((v-j*offset)/h for v,j in zip(subtract(uc,ua),j1))
            aa,bb,cc=dot(j1,j1),dot(j1,j2),dot(j2,j2);delta=math.sqrt((aa-cc)**2+4*bb*bb)
            low,high=(aa+cc-delta)*.5,(aa+cc+delta)*.5
            assert low>0
            densities.append((low*high)**.25*128);stretches.append(math.sqrt(high/low));areas.append(area)
            if lod<2:
                du,dv=subtract(ub,ua),subtract(uc,ua);det=du[0]*dv[1]-du[1]*dv[0]
                # World-space direction of increasing exported V. Authored
                # normal-map +green must instead point toward decreasing V.
                bitangent=unit(tuple((e2[i]*du[0]-e1[i]*dv[0])/det for i in range(3)))
                for i in ids:
                    n=attrs['NORMAL'][i];t=attrs['TANGENT'][i]
                    b=unit(tuple(x*t[3] for x in cross(n,t[:3])))
                    alignments.append(-dot(bitangent,b))
        rows.append(name)
    assert max(stretches)<=1.02 and min(densities)>255 and max(densities)<257
    assert not alignments or min(alignments)>.995
    return dict(material_regions=rows,triangles=len(densities),density_near_equivalent_min=min(densities),density_near_equivalent_max=max(densities),
                maximum_principal_stretch=max(stretches),minimum_authored_bitangent_alignment=min(alignments) if alignments else None,
                normal_samples=checked_pixels,maximum_normal_component_error=normal_error)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();assert not args.report.exists()
    root=Path(__file__).resolve().parents[2];rows=[]
    result=dict(status='running',scope='Actual GLB geometry, UV scale, PBR factors/channels and normal orientation; no runtime/visual acceptance',checks=rows)
    try:
        for part,directory,old in [('pontoon','pontoon-r02',root/'data/salvage/material-calibration/r01/pontoon'),
                                  ('winch','kit/winch',root/'data/salvage/functional-kit/r09/winch'),
                                  ('helm','kit/helm',root/'data/salvage/functional-kit/r09/helm')]:
            for lod in range(3):
                path=args.candidate/directory/'source'/f'{part}-lod-{lod}.glb';model=Model(path)
                old_model=Model(old/'source'/path.name);assert model.geometry()==old_model.geometry(),(part,lod,'geometry changed')
                row=dict(part=part,lod=lod,sha256=model.sha,geometry_identical=True,**check(model,lod));rows.append(row)
                if part=='pontoon':
                    samples=[model.sample((0,-.38,z),direction) for z in (-1,0,1) for direction in ((1,0,0),(-1,0,0),(0,0,1),(0,0,-1),(0,1,0))]
                    assert all('metric_teal_' in name for _,name in samples);row['teal_well_rays']=len(samples)
                if part=='winch':
                    distance,name=model.sample((.265,.12,.32),(-1,0,0));assert distance<.02 and 'metric_steel_' in name
                    row['metal_flange_ray']=True
        result['status']='passed'
    except Exception as error:
        result.update(status='failed',error=str(error));raise
    finally:args.report.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
