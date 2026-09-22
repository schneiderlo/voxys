#!/usr/bin/env python3
"""Blender batch export of a bounded source-preserving Blacksmith wall.

The checked-in original blend and LDraw source are read only. No parts are
invented: mesh 0 is the unchanged remainder and later meshes share actual
imported geometry. Full source identity remains in assembly.json, even for
parts whose connectors are unsupported. See the generated README for limits.
"""
import argparse, hashlib, json, math, os, re, struct, sys
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'data/adventure/ldraw-blacksmith-r01'

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,data): path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
def rows(m): return [[float(v) for v in row] for row in m]
def vector(v): return [float(x) for x in v]
def identifier(text): return str(int(hashlib.sha256(text.encode()).hexdigest()[:15],16))

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--asset-id',default='ldraw-blacksmith-parts-r01')
 p.add_argument('--additional-source-ids',type=Path,help='JSON array of exact catalog source IDs added to the original front wall')
 p.add_argument('--selection-source-ids',type=Path,help='JSON array replacing the original front wall with an exact source selection')
 p.add_argument('--preserve-layout',type=Path,help='Existing wall.json whose shared geometry node order must remain stable')
 a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 assert bpy.app.background
 bpy.ops.wm.open_mainfile(filepath=str(SOURCE/'blacksmith-assembly.blend'))
 objects=sorted((o for o in bpy.context.scene.objects if o.type=='MESH'),key=lambda o:o.name)
 assert len(objects)==2140
 # Source mesh local axes: LDraw +Y down; canonical local +Y up, +Z reversed.
 L=Matrix.Diagonal((1.,-1.,-1.,1.));C=Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
 mpd=(SOURCE/'source/21325-medieval-blacksmith.mpd').read_text();sourceHash=sha(SOURCE/'source/21325-medieval-blacksmith.mpd')
 embedded={};name=None
 for line in mpd.splitlines():
  if line.startswith('0 FILE '):name=line[7:].lower();embedded[name]=[]
  if name:embedded[name].append(line)
 def sourceInfo(name):
  for folder in ['parts','p','parts/s']:
   path=SOURCE/'source/ldraw'/folder/name
   if path.exists():return {'path':str(path.relative_to(SOURCE)), 'sha256':sha(path),'description':path.read_text(errors='replace').splitlines()[0]}
  if name.lower() in embedded:
   txt='\n'.join(embedded[name.lower()])+'\n'
   return {'path':'source/21325-medieval-blacksmith.mpd#'+name,'sha256':hashlib.sha256(txt.encode()).hexdigest(),'description':embedded[name.lower()][1]}
  raise ValueError('No pinned source for '+name)
 catalog={}
 for o in objects:
  name=o.name.split('_',1)[1];info=sourceInfo(name)
  match=re.fullmatch(r'0 (Brick|Plate)\s+(\d+)\s*x\s*(\d+)(?: with Embossed Bricks)?',info['description'])
  if match:
   catalog[name]={'studsX':int(match[3]),'studsZ':int(match[2]),'height':1.2 if match[1]=='Brick' else .4,'source':info}
 # 3023 is a pinned alias of the standard 1x2 plate, not an inferred silhouette.
 for name in ['3023.dat','3023b.dat']:
  if any(o.name.endswith('_'+name) for o in objects):catalog[name]={'studsX':2,'studsZ':1,'height':.4,'source':sourceInfo(name)}
 records=[];byId={};byObject={}
 for o in objects:
  chain=[];cursor=o
  while cursor:chain.insert(0,cursor.name);cursor=cursor.parent
  path='/'.join(chain);part=o.name.split('_',1)[1];info=sourceInfo(part)
  # Preserve arbitrary full rotations. Reflection/shear fails instead of snapping.
  transform=C@o.matrix_world@L;loc,rot,scale=transform.decompose()
  assert all(math.isfinite(v) for row in transform for v in row)
  key=o.data.name;colour=int(re.search(r'\.dat_(\d+)',key).group(1))
  rec={'sourceId':identifier(sourceHash+':'+path),'sourcePath':path,'partNumber':part,'colour':colour,
       'source':info,'geometryKey':key,'translation':vector(loc),'rotation':vector(rot),'scale':vector(scale),'matrixRows':rows(transform),
       'connectorType':'rectangular-stud-v1' if part in catalog else 'unsupported','proxyType':'rectangular-body-envelope-v1' if part in catalog else 'unsupported'}
  if part in catalog:rec.update({k:v for k,v in catalog[part].items() if k!='source'})
  records.append(rec);byId[rec['sourceId']]=rec;byObject[rec['sourceId']]=o
 selected=[r for r in records if r['partNumber'] in catalog and abs(r['translation'][2]-2.461079)<.1 and 8.7<r['translation'][1]<18.1 and -.5<r['translation'][0]<16.1]
 assert len(selected)==39,len(selected)
 assert not (a.additional_source_ids and a.selection_source_ids), 'Choose additive or replacement selection'
 explicit=a.additional_source_ids or a.selection_source_ids
 if explicit:
  if a.selection_source_ids:selected=[]
  additional=json.loads(explicit.read_text())
  assert isinstance(additional,list) and len(additional)==len(set(additional)), 'Expected unique source ID array'
  for sid in additional:
   assert sid in byId and byId[sid]['partNumber'] in catalog, 'Unknown or unsupported additional source ID: '+str(sid)
  selectedIds={r['sourceId'] for r in selected}
  selected.extend(byId[sid] for sid in additional if sid not in selectedIds)
 assert len(selected)<=64, 'Selected source capacity exceeded'
 for r in selected:
  assert max(abs(v-1) for v in r['scale'])<.00002, r['sourcePath']
  transform=Matrix(r['matrixRows']);rot=__import__('mathutils').Quaternion(r['rotation']);loc=Vector(r['translation'])
  assert max(abs(transform[a][b]-(Matrix.Translation(loc)@rot.to_matrix().to_4x4())[a][b]) for a in range(4) for b in range(4))<.00002
 selectedIds={r['sourceId'] for r in selected};selectedObjects={byObject[i] for i in selectedIds}
 # Source catalog connectors describe interface planes, not the top of studs.
 # These match only exact pitch and opposed directions; never AABB overlap.
 def slots(r,upper):
  m=Matrix(r['matrixRows']);normal=(m.to_3x3()@Vector((0,1 if upper else -1,0))).normalized()
  for z in range(r['studsZ']):
   for x in range(r['studsX']):
    point=m@Vector((x-(r['studsX']-1)*.5,0 if upper else -r['height'],z-(r['studsZ']-1)*.5))
    yield z*r['studsX']+x,point,normal
 supported=[r for r in records if r['partNumber'] in catalog]
 tops={}
 for r in supported:
  for slot,pos,normal in slots(r,True):tops.setdefault(tuple(round(float(v),2) for v in pos),[]).append((r,slot,pos,normal))
 bonds=[];anchors=[];external=[]
 for upper in supported:
  for uslot,point,normal in slots(upper,False):
   for lower,lslot,lpoint,lnormal in tops.get(tuple(round(float(v),2) for v in point),[]):
    if lower['sourceId']==upper['sourceId'] or (point-lpoint).length>.002 or normal.dot(lnormal)>-.9999:continue
    aId,bId=lower['sourceId'],upper['sourceId']
    if aId not in selectedIds and bId not in selectedIds:continue
    bond={'id':identifier(aId+':'+str(lslot)+':'+bId+':'+str(uslot)), 'lowerPart':aId,'lowerSlot':lslot,'upperPart':bId,'upperSlot':uslot,'active':True,'point':vector((point+lpoint)*.5)}
    if aId in selectedIds and bId in selectedIds:bonds.append(bond)
    else:external.append(bond);anchors.append(aId if aId in selectedIds else bId)
 # Geometry/material conversion matches the already accepted static importer.
 classes={m['name']:m for m in json.loads((SOURCE/'provenance.json').read_text())['materials']}
 for mat in bpy.data.materials:
  if mat.name not in classes:continue
  info=classes[mat.name];kind=info['ldraw_class'];mat.use_nodes=True;mat.node_tree.nodes.clear()
  bs=mat.node_tree.nodes.new('ShaderNodeBsdfPrincipled');dest=mat.node_tree.nodes.new('ShaderNodeOutputMaterial');mat.node_tree.links.new(bs.outputs['BSDF'],dest.inputs['Surface'])
  bs.inputs['Base Color'].default_value=info['linear_base_color'];bs.inputs['Alpha'].default_value=1.
  bs.inputs['Roughness'].default_value=.75 if kind=='RUBBER' else .3 if kind in ('CHROME','METAL') else .48
  bs.inputs['Metallic'].default_value=.85 if kind=='CHROME' else .65 if kind=='METAL' else .2 if kind=='PEARLESCENT' else 0.
  mat.use_backface_culling=True
 # Separate real wall pieces before joining the remainder. The unchanged source
 # blend is the editable geometry source for every shared geometryKey.
 originals={}
 for o in sorted(selectedObjects,key=lambda ob:ob.name):
  if o.data.name not in originals:originals[o.data.name]=o.data.copy()
 geometryOrder=sorted(originals)
 if a.preserve_layout:
  previous=json.loads(a.preserve_layout.read_text())
  ordered=sorted({(r['meshNode'],r['geometryKey']) for r in previous['parts']})
  assert [i for i,_ in ordered]==list(range(1,len(ordered)+1)), 'Expected contiguous previous shared node layout'
  assert all(key in originals for _,key in ordered), 'Previous geometry absent from additive selection'
  prior=[key for _,key in ordered]
  geometryOrder=prior+[key for key in geometryOrder if key not in prior]
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:
  if o not in selectedObjects:o.select_set(True)
 bpy.context.view_layer.objects.active=next(o for o in objects if o not in selectedObjects)
 bpy.ops.object.convert(target='MESH');bpy.ops.object.join();remainder=bpy.context.object
 remainder.name='00_remainder';bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
 mod=remainder.modifiers.new('Same bounded static simplification','DECIMATE');mod.ratio=.82;mod.use_collapse_triangulate=True;bpy.ops.object.modifier_apply(modifier=mod.name)
 for o in list(bpy.context.scene.objects):
  if o!=remainder:bpy.data.objects.remove(o,do_unlink=True)
 # Remainder-only input for existing multi-axis collision bake, with no hidden
 # copies of the selected wall. Save before adding shared part prototypes.
 bpy.ops.wm.save_as_mainfile(filepath=str(out/'remainder.blend'),compress=True)
 created=[]
 for index,key in enumerate(geometryOrder,start=1):
  data=originals[key];data.transform(C.inverted()@L)
  ob=bpy.data.objects.new(f'{index:02d}_'+key,data);bpy.context.collection.objects.link(ob);created.append(ob)
 for ob in [remainder,*created]:ob.select_set(True)
 bpy.context.view_layer.objects.active=remainder
 glb=out/'wall-parts.glb'
 bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
 payload=glb.read_bytes();length,kind=struct.unpack_from('<II',payload,12);assert kind==0x4e4f534a;gltf=json.loads(payload[20:20+length]);nodes={n['name']:(i,n['mesh']) for i,n in enumerate(gltf['nodes'])}
 assert len(nodes)==len(created)+1<=64
 for n in gltf['nodes']:assert not any(k in n for k in ['matrix','translation','rotation','scale'])
 geometry={ob.data.name:ob for ob in created}
 shared={key:nodes[ob.name] for key,ob in zip(geometryOrder,created)}
 for r in selected:
  node,mesh=shared[r['geometryKey']];r['meshNode']=node;r['meshIndex']=mesh
  sx,sz,h=r['studsX'],r['studsZ'],r['height']
  r['bodyBounds']={'minimum':[-sx*.5,-h,-sz*.5],'maximum':[sx*.5,0,sz*.5]}
  r['studs']=[[x-(sx-1)*.5,0,z-(sz-1)*.5] for z in range(sz) for x in range(sx)]
  r['antiStuds']=[[v[0],-h,v[2]] for v in r['studs']]
  r['meshAsset']='wall-parts.vmesh'
 for r in records:
  if r['sourceId'] not in selectedIds:r['renderMembership']='remainder';r['meshAsset']='wall-parts.vmesh';r['meshNode']=nodes['00_remainder'][0];r['meshIndex']=nodes['00_remainder'][1]
  else:r['renderMembership']='selected-wall'
 report={'schema':1,'assetId':a.asset_id,'coordinateSystem':'Y-up; one unit per stud; local brick top at Y=0; column-vector transforms',
         'sourceMPDSha256':sourceHash,'sourceBlendSha256':sha(SOURCE/'blacksmith-assembly.blend'),'recipeSha256':sha(Path(__file__)),
         'sourceBlend':'../ldraw-blacksmith-r01/blacksmith-assembly.blend','attribution':'../ldraw-blacksmith-r01/source/ATTRIBUTION.md',
         'budgets':{'sourceInstances':4096,'sharedGeometry':1024,'selectedParts':64,'meshNodes':64,'bonds':512,'externalBonds':512},
         'counts':{'sourceInstances':len(records),'sharedSourceGeometry':len({r['geometryKey'] for r in records}),'selectedParts':len(selected),'meshNodes':len(nodes),'bonds':len(bonds),'externalBonds':len(external)},
         'parts':records,'catalog':catalog,'unsupportedPartTypes':sorted({r['partNumber'] for r in records if r['connectorType']=='unsupported'}),
         'remainderMeshNode':nodes['00_remainder'][0],'remainderMeshIndex':nodes['00_remainder'][1],
         'geometryPolicy':'Selected parts share exact source geometry per type/colour. Unselected parts are one 82-percent triangle remainder batch; original geometry keys remain resolvable in sourceBlend. Arbitrary rotations preserved.',
         'proxyPolicy':'Selected rectangular closed body envelopes exclude studs, anti-stud cavities and masonry grooves; collision is explicitly approximate, render geometry is authentic. Other part proxies unsupported.',
         'connectorPolicy':'Only ordinary rectangular stud grids, plates and embossed masonry equivalents. No clips, hinges, SNOT or unknown parts are glued by proximity.'}
 graph={sid:set() for sid in selectedIds}
 for b in bonds:graph[b['lowerPart']].add(b['upperPart']);graph[b['upperPart']].add(b['lowerPart'])
 remaining=set(selectedIds);components=[]
 while remaining:
  seen=set();queue=[min(remaining)]
  while queue:
   sid=queue.pop()
   if sid in seen:continue
   seen.add(sid);queue.extend(graph[sid]-seen)
  remaining-=seen
  known=bool(seen&set(anchors))
  components.append({'parts':sorted(seen),'supportStatus':'validated-boundary-stud' if known else 'unsupported-external-support','manualReleaseEligible':known})
 wall={'schema':1,'sourceMPDSha256':sourceHash,'components':components,'assemblyAsset':a.asset_id,'parts':selected,'bonds':bonds,'anchoredParts':sorted(set(anchors)), 'boundaryBonds':external,
       'anchorPolicy':'Pinned selected nodes connected by exact catalog stud interfaces to non-selected intact house parts. These are boundary supports, not ground anchors. Whole-house ground topology is not validated.',
       'selection':f'{len(selected)} ordinary rectangular source bricks/plates: '+('explicit replacement source ID selection' if a.selection_source_ids else 'original 39 front upper facade parts'+(' plus explicit additive source ID selection' if a.additional_source_ids else ''))+'. Source transforms retained. Decorative window frames, SNOT and arches remain in intact remainder.',
       'manualCutCaveat':'Severing a stud bond alone cannot remove a physical bearing support. This subset does not prove a whole-house support collapse; account for non-selected arches, windows and beam contacts.'}
 write(out/'assembly.json',report);write(out/'wall.json',wall)
 write(out/'export-report.json',{'glbSha256':sha(glb),'sourceInstances':len(records),'selectedParts':len(selected),'sharedWallMeshes':len(created),'bonds':len(bonds),'boundaryBonds':len(external),'remainderBlendSha256':sha(out/'remainder.blend'),'blender':bpy.app.version_string})
 print('EXPORTED',report['counts'],flush=True);sys.stdout.flush();os._exit(0)

if __name__=='__main__':main()
