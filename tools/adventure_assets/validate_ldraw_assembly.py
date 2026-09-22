#!/usr/bin/env python3
"""Validate the D2 source, mesh, connector and bounded selection contracts."""
import argparse,hashlib,json,math,struct
from pathlib import Path

LIMITS={'sourceInstances':4096,'sharedGeometry':1024,'selectedParts':64,'meshNodes':64,'bonds':512,'externalBonds':512}
def require(ok,message):
 if not ok:raise ValueError(message)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def point(r,p):return [sum(r['matrixRows'][i][j]*p[j] for j in range(3))+r['matrixRows'][i][3] for i in range(3)]
def normal(r,sign):return [sign*r['matrixRows'][i][1] for i in range(3)]
def connector(r,slot,top):
 sx,sz,h=r['studsX'],r['studsZ'],r['height'];require(type(slot)==int and 0<=slot<sx*sz,'invalid connector slot')
 return point(r,[slot%sx-(sx-1)*.5,0 if top else -h,slot//sx-(sz-1)*.5])
def validate_data(assembly,wall):
 require(assembly['schema']==wall['schema']==1,'unsupported schema')
 require(assembly['sourceMPDSha256']==wall['sourceMPDSha256'],'source hash mismatch')
 require(assembly['budgets']==LIMITS,'unsupported budgets')
 allParts=assembly['parts'];parts=wall['parts'];ids=[p['sourceId'] for p in allParts]
 require(len(ids)<=LIMITS['sourceInstances'] and len(ids)==len(set(ids)),'source capacity or duplicate source ID')
 for sid in ids:require(type(sid)==str and sid.isdecimal() and 0<int(sid)<2**60,'source ID must be a bounded decimal string')
 full={p['sourceId']:p for p in allParts};selected={p['sourceId']:p for p in parts}
 require(0<len(parts)<=LIMITS['selectedParts'] and len(parts)==len(selected),'selected capacity or duplicate')
 require(len({r['geometryKey'] for r in allParts})<=LIMITS['sharedGeometry'],'geometry capacity')
 for r in allParts:
  m=r['matrixRows'];require(len(m)==4 and all(len(row)==4 for row in m),'matrix dimensions')
  require(all(type(v) in (int,float) and math.isfinite(v) for row in m for v in row),'nonfinite transform')
  require(max(abs(m[3][i]-int(i==3)) for i in range(4))<1e-7,'nonaffine transform')
 for p in parts:
  require(p==full[p['sourceId']],'selected source record mismatch')
  require(p['connectorType']=='rectangular-stud-v1' and p['proxyType']=='rectangular-body-envelope-v1','unsupported selected type')
  sx,sz,h=p['studsX'],p['studsZ'],p['height'];require(type(sx)==type(sz)==int and 1<=sx<=16 and 1<=sz<=16 and h in (.4,1.2),'invalid catalog dimensions')
  require(p['partNumber'] in assembly['catalog'],'uncatalogued selected part')
  c=assembly['catalog'][p['partNumber']];require((sx,sz,h)==(c['studsX'],c['studsZ'],c['height']),'catalog dimension mismatch')
  require(max(abs(v-1) for v in p['scale'])<2e-5,'selected nonrigid scale')
  require(abs(sum(x*x for x in p['rotation'])-1)<2e-5,'invalid rotation')
  require(p['bodyBounds']=={'minimum':[-sx*.5,-h,-sz*.5],'maximum':[sx*.5,0,sz*.5]},'proxy mismatch')
  require(p['meshNode']!=assembly['remainderMeshNode'],'selected part still assigned remainder')
 require(len(wall['bonds'])<=512 and len(wall['boundaryBonds'])<=512,'bond capacity')
 bonds=wall['bonds']+wall['boundaryBonds'];bondIds=[b['id'] for b in bonds]
 require(len(set(bondIds))==len(bondIds),'duplicate bond ID')
 used=set()
 for b in bonds:
  require(b['lowerPart'] in full and b['upperPart'] in full and b['lowerPart']!=b['upperPart'],'bond references missing part')
  lo,hi=full[b['lowerPart']],full[b['upperPart']]
  require(lo['connectorType']==hi['connectorType']=='rectangular-stud-v1','unsupported bond endpoints')
  require(math.dist(connector(lo,b['lowerSlot'],True),connector(hi,b['upperSlot'],False))<=.002,'displaced stud connection')
  require(sum(a*b for a,b in zip(normal(lo,1),normal(hi,-1)))<-.9999,'non-opposed connection')
  for key in [(b['lowerPart'],'top',b['lowerSlot']),(b['upperPart'],'bottom',b['upperSlot'])]:
   require(key not in used,'multiply occupied connector');used.add(key)
 for b in wall['bonds']:require(b['lowerPart'] in selected and b['upperPart'] in selected,'external bond in internal graph')
 expectedAnchors=set()
 for b in wall['boundaryBonds']:
  endpoints={b['lowerPart'],b['upperPart']};require(len(endpoints&selected.keys())==1,'invalid boundary bond')
  expectedAnchors|=endpoints&selected.keys()
 require(set(wall['anchoredParts'])==expectedAnchors,'unproven boundary anchor')
 graph={sid:set() for sid in selected}
 for b in wall['bonds']:graph[b['lowerPart']].add(b['upperPart']);graph[b['upperPart']].add(b['lowerPart'])
 actualComponents=set();remaining=set(selected)
 while remaining:
  seen=set();queue=[next(iter(remaining))]
  while queue:
   sid=queue.pop()
   if sid in seen:continue
   seen.add(sid);queue.extend(graph[sid]-seen)
  remaining-=seen;actualComponents.add(frozenset(seen))
 require({frozenset(c['parts']) for c in wall['components']}==actualComponents,'declared graph components do not match bonds')
 covered=[]
 for c in wall['components']:
  known=bool(set(c['parts'])&expectedAnchors)
  require(c['manualReleaseEligible']==known,'unsupported support enabled')
  require(c['supportStatus']==('validated-boundary-stud' if known else 'unsupported-external-support'),'bad support status')
  covered+=c['parts']
 require(len(covered)==len(parts) and set(covered)==selected.keys(),'component partition missing/duplicated source parts')
 return {'sourceInstances':len(allParts),'selectedParts':len(parts),'bonds':len(wall['bonds']),'boundaryBonds':len(wall['boundaryBonds']),'eligibleParts':sum(len(c['parts']) for c in wall['components'] if c['manualReleaseEligible'])}

def validate_asset(root):
 assembly=json.loads((root/'assembly.json').read_text());wall=json.loads((root/'wall.json').read_text());report=validate_data(assembly,wall)
 source=root.parent/'ldraw-blacksmith-r01';require(sha(source/'source/21325-medieval-blacksmith.mpd')==assembly['sourceMPDSha256'],'changed MPD')
 require(sha(source/'blacksmith-assembly.blend')==assembly['sourceBlendSha256'],'changed source blend')
 for name,record in json.loads((root/'manifest.json').read_text())['files'].items():
  path=root/name;require(path.stat().st_size==record['bytes'] and sha(path)==record['sha256'],'changed file '+name)
 blob=(root/'wall-parts.glb').read_bytes();require(struct.unpack_from('<III',blob)==(0x46546c67,2,len(blob)),'invalid glb')
 size,kind=struct.unpack_from('<II',blob,12);require(kind==0x4e4f534a,'missing gltf json');gltf=json.loads(blob[20:20+size])
 require(len(gltf['nodes'])==assembly['counts']['meshNodes']<=64,'node capacity/layout')
 for n in gltf['nodes']:require(not any(k in n for k in ['matrix','translation','rotation','scale']),'nonidentity mesh node')
 for r in wall['parts']:require(gltf['nodes'][r['meshNode']]['mesh']==r['meshIndex'],'mesh index mismatch')
 report['result']='PASS';return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('asset',type=Path);args=p.parse_args();print(json.dumps(validate_asset(args.asset.resolve()),indent=2))
