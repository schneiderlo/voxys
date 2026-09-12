from pathlib import Path
import hashlib,json,struct,copy
R=Path.cwd(); S=R/'build-cove-cargo-art-r01'; O=S/'overlay'; D=O/'data/salvage/toy-art/r05'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def mesh(p):
 b=p.read_bytes(); assert b[:8]==b'VOXYMESH'
 flags,vc,vs,ic,iss,sc,mc,nc,meshes,skin,joint,anim,chan=struct.unpack_from('<13I',b,12)
 vo,io,so,mo,imgo,no=struct.unpack_from('<6Q',b,64)
 assert vs==72 and iss in (2,4) and skin==joint==anim==chan==0
 texture=0
 for i in range(mc):
  m=mo+i*128
  for s in range(4):
   if b[m+100+s]:
    w=struct.unpack_from('<H',b,m+84+s*2)[0]; h=struct.unpack_from('<H',b,m+92+s*2)[0]
    assert w>0 and h>0
    while True:
     texture+=w*h*4
     if w==h==1: break
     w=max(w//2,1);h=max(h//2,1)
 positions=[struct.unpack_from('<3f',b,vo+i*vs) for i in range(vc)]
 low=[min(p[k] for p in positions) for k in range(3)]; high=[max(p[k] for p in positions) for k in range(3)]
 bounds=dict(minimum=[-high[0],low[1],-high[2]], maximum=[-low[0],high[1],-low[2]])
 return dict(vertices=vc,triangles=ic//3,draws=sc,materials=mc,nodes=nc,meshes=meshes,vertex_flags=flags,bounds=bounds,
  gpu_bytes=vc*72+ic*4+mc*64+texture,texture_mip_bytes=texture,encoded_bytes=len(b),sha256=sha(p))
report=dict(schema=1,status='offline candidate; no runtime or image acceptance',parts={},checks={},files=[])
additions=[]; totalold=totalnew=0
for name in ('generator','cradle'):
 original=R/f'data/salvage/functional-kit/r09/{name}'; new=D/name
 before=read(original/'cooked/gameplay.json'); after=read(new/'cooked/gameplay.json'); provenance=read(new/'provenance.json')
 assert {k:v for k,v in before.items() if k!='lods'}=={k:v for k,v in after.items() if k!='lods'}
 assert len(after['lods'])==3
 lods=[]
 for i,(a,b) in enumerate(zip(before['lods'],after['lods'])):
  assert a['id']==b['id'] and a['minimum_screen_height_pixels']==b['minimum_screen_height_pixels'] and a['source']['to_canonical_rotation']==b['source']['to_canonical_rotation']==12
  info=mesh(new/f'cooked/lod-{i+1}.vmesh'); assert info['nodes']==info['meshes']==1 and info['vertex_flags']==0 and info['texture_mip_bytes']==0 and info['draws']<=6
  oldbounds=provenance['baseline_bounds'];bounds=info['bounds']; expansion=max(max(oldbounds['minimum'][k]-bounds['minimum'][k],bounds['maximum'][k]-oldbounds['maximum'][k],0) for k in range(3)); assert expansion<=.020001
  info.update(id=b['id'],asset=b['asset'],maximum_bounds_expansion_metres=expansion)
  assert info['vertices']==provenance['lods'][i]['vertices'] and info['triangles']==provenance['lods'][i]['triangles']
  src=new/'source'/b['source']['file'];assert sha(src)==b['source']['sha256'] and src.stat().st_size==b['source']['bytes']
  lods.append(info)
 old=sum(mesh(original/f'cooked/lod-{i}.vmesh')['gpu_bytes'] for i in (1,2,3));newbytes=sum(x['gpu_bytes'] for x in lods);totalold+=old;totalnew+=newbytes
 report['parts'][name]=dict(lods=lods,canonical_part=after['part']['key'],canonical_non_lod_metadata_equal=True,original_gpu_bytes=old,candidate_gpu_bytes=newbytes,delta_gpu_bytes=newbytes-old,baseline_bounds=provenance['baseline_bounds'],cook_manifest_sha256=sha(new/'cooked/cook-manifest.json'))
 additions.append(dict(source_manifest_sha256=sha(original/'cooked/cook-manifest.json'),bundle=dict(directory=f'toy-art/r05/{name}/cooked',part=after['part']['key'],manifest_sha256=sha(new/'cooked/cook-manifest.json'),lod_limits=[dict(id=str(i+1),vertices=v,triangles=t,texture_dimension=64) for i,(v,t) in enumerate(((24000,12000),(12000,6000),(6000,3000)))])))
 for key,value in provenance['inputs'].items():
  path=O/key if (O/key).is_file() else R/key
  assert sha(path)==value,(key,'provenance mismatch')
 assert sha(R/provenance['original_metadata'])==provenance['original_metadata_sha256']
 assert lods[0]['vertices']>lods[1]['vertices']>lods[2]['vertices'] and lods[0]['triangles']>lods[1]['triangles']>lods[2]['triangles']
for p in sorted(O.rglob('*')):
 if p.is_file():
  assert p.suffix.lower() not in ('.png','.jpg','.jpeg','.webp','.exr','.mp4','.webm')
  report['files'].append(dict(path=str(p.relative_to(O)),bytes=p.stat().st_size,sha256=sha(p)))
# Reject visual identities already used by any installed source or cooked sidecar.
newkeys={(x['asset']['namespace'],x['asset']['counter'],x['asset']['version']) for part in report['parts'].values() for x in part['lods']}
assert len(newkeys)==6
for p in (R/'data/salvage').rglob('*.json'):
 try: doc=read(p)
 except (ValueError,UnicodeError): continue
 if not isinstance(doc,dict): continue
 for lod in doc.get('lods',[]):
  if isinstance(lod,dict) and isinstance(lod.get('asset'),dict):
   a=lod['asset']; assert (a.get('namespace'),a.get('counter'),a.get('version')) not in newkeys,(p,a)
report['checks']=dict(canonical_metadata_equal=True,strictly_decreasing_lods=True,all_solid_no_images_uv_tangents=True,new_unique_visual_ids=6,provenance_inputs_match=True,blender_exports_completed=True,blender_shutdown_exit=130,blender_shutdown_note='All six exports, two source blend saves, manifold/interface/bounds assertions and completion markers finished; sandbox PulseAudio pa_write shutdown then hung and completed process session was interrupted. No authoring rerun or image.',cpu_cooks_exit=[0,0])
report['owner_budget']=dict(baseline_full_cove_with_mechanisms_and_dock_bytes=10753144,original_two_parts_gpu_bytes=totalold,candidate_two_parts_gpu_bytes=totalnew,delta_gpu_bytes=totalnew-totalold,projected_full_cove_bytes=10753144-totalold+totalnew,unchanged_maximum_owner_gpu_bytes=16*1024*1024,formula='Each admitted unique LOD: vertexCount*72 + indexCount*4 + materialCount*64 + per-material texture mip chains. Replace canonical generator/cradle LODs once each; all shared/runtime reservations unchanged.')
assert report['owner_budget']['projected_full_cove_bytes']<16*1024*1024
(S/'candidate-report.json').write_text(json.dumps(report,indent=2)+'\n');(S/'presentation-additions.json').write_text(json.dumps(additions,indent=2)+'\n')
catalog=read(R/'data/salvage/cove-workshop-r05.json');assert len(catalog['presentations'])==7
catalog['presentations']+=additions
(S/'cove-workshop-candidate-r06.json').write_text(json.dumps(catalog,indent=2)+'\n')
(S/'stage-candidates-r01.json').write_text(json.dumps(dict(schema=1,status='scratch-only candidate; not activated',files=report['files']),indent=2)+'\n')
print(json.dumps(dict(parts={k:{x:v[x] for x in ('original_gpu_bytes','candidate_gpu_bytes','delta_gpu_bytes','cook_manifest_sha256')} for k,v in report['parts'].items()},owner_budget=report['owner_budget'],files=len(report['files'])),indent=2))
