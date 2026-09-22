#!/usr/bin/env python3
"""Independent Blender/source versus exported glTF geometry/pose validation."""
import argparse,json,struct,sys,os
from pathlib import Path
import bpy
from mathutils import Matrix,Vector
from mathutils.kdtree import KDTree
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--asset',type=Path,required=True)
a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);root=a.asset.resolve();assembly=json.loads((root/'assembly.json').read_text());wall=json.loads((root/'wall.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str((root/assembly['sourceBlend']).resolve()))
objects={o.name:o for o in bpy.context.scene.objects if o.type=='MESH'}
blob=(root/'wall-parts.glb').read_bytes();size,kind=struct.unpack_from('<II',blob,12);assert kind==0x4e4f534a
gltf=json.loads(blob[20:20+size]);offset=20+size;binSize,binType=struct.unpack_from('<II',blob,offset);assert binType==0x004e4942;binary=blob[offset+8:offset+8+binSize]
C=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)));L=Matrix.Diagonal((1.,-1.,-1.,1.))
checked=0;maxError=0.
for r in assembly['parts']:
 o=objects[r['sourcePath'].rsplit('/',1)[-1]];wanted=C@o.matrix_world@L;got=Matrix(r['matrixRows'])
 assert max(abs(wanted[i][j]-got[i][j]) for i in range(4) for j in range(4))<1.e-7
for r in wall['parts']:
 o=objects[r['sourcePath'].rsplit('/',1)[-1]];expected=[L@v.co for v in o.data.vertices];actual=[]
 for prim in gltf['meshes'][r['meshIndex']]['primitives']:
  ac=gltf['accessors'][prim['attributes']['POSITION']];view=gltf['bufferViews'][ac['bufferView']];assert ac['componentType']==5126 and ac['type']=='VEC3'
  start=view.get('byteOffset',0)+ac.get('byteOffset',0);stride=view.get('byteStride',12)
  actual.extend(Vector(struct.unpack_from('<3f',binary,start+i*stride)) for i in range(ac['count']))
 for first,second in [(expected,actual),(actual,expected)]:
  tree=KDTree(len(first))
  for i,point in enumerate(first):tree.insert(point,i)
  tree.balance()
  for point in second:
   _,_,distance=tree.find(point);maxError=max(maxError,distance);assert distance<.000002,(r['sourcePath'],distance)
 checked+=1
report={'sourceTransformsChecked':len(assembly['parts']),'selectedMeshesChecked':checked,'maximumLocalVertexErrorStuds':maxError,'result':'PASS'}
(root/'geometry-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True);sys.stdout.flush();os._exit(0)
