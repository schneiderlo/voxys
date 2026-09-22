#!/usr/bin/env python3
"""Blender background import of a pinned, attributed LDraw model.

Uses TobyLobster/ImportLDraw c306fb777a4e0da85492f09d65daf458767a0aa1.
The source assembly remains editable; the runtime derivative batches materials
and simplifies triangles to fit salvage-rigid-v1 without changing stud scale.
"""
import argparse, hashlib, json, math, os, re, struct, subprocess, sys
from pathlib import Path
from types import SimpleNamespace
import bpy
from mathutils import Vector

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--importer-root',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--proof',action='store_true')
p.add_argument('--model',default='cannon.mpd')
p.add_argument('--stem',default='cannon')
p.add_argument('--asset-id',default='ldraw-cannon-r01')
p.add_argument('--simplify',type=float,default=1.)
p.add_argument('--articulate-cannon',action='store_true',help='Keep 2527 base and 518 barrel as identity-node shared-root meshes')
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
assert bpy.app.background and '--factory-startup' in sys.argv
source=a.source.resolve();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
assert 0<a.simplify<=1 and a.stem.isidentifier()
subprocess.run([sys.executable if 'python' in Path(sys.executable).name else 'python3',str(source/'validate.py')],check=True)
revision=subprocess.check_output(['git','-C',str(a.importer_root),'rev-parse','HEAD'],text=True).strip()
assert revision=='c306fb777a4e0da85492f09d65daf458767a0aa1',revision
sys.path.insert(0,str(a.importer_root.resolve()/'loadldraw'));import loadldraw
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.scene.world=bpy.data.worlds.new('World')
o=loadldraw.Options
o.ldrawDirectory=str(source/'ldraw');o.realScale=125;o.useColourScheme='ldraw';o.resolution='Standard'
o.curvedWalls=False;o.addWorldEnvironmentTexture=False;o.addGroundPlane=False;o.positionCamera=False
o.importCameras=False;o.setRenderSettings=False;o.positionObjectOnGroundAtOrigin=True
# Exact source shapes already include the moulded edges. Avoid extra bevel inflation.
o.addBevelModifier=False;o.realGapWidth=.00008;o.useLogoStuds=False
messages=[]
def report(levels,message):
 messages.append({'levels':sorted(levels),'message':message});print(message,flush=True)
loadldraw.loadFromFile(SimpleNamespace(report=report),str(source/a.model))
objects=[obj for obj in bpy.context.scene.objects if obj.type=='MESH']
source_part_mesh_count=len(objects)
bpy.ops.wm.save_as_mainfile(filepath=str(out/(a.stem+'-assembly.blend')),compress=True)
# LDraw colours are already linear in the importer. Retain plastic, rubber,
# metal classes. The bounded static profile requires opacity: translucent pieces
# retain their colour but are opaque in this first runtime derivative.
classes=[]
for mat in bpy.data.materials:
 color=tuple(mat.diffuse_color);code=mat.name.split('_')[1]
 col=loadldraw.LegoColours.colours.get(int(code),{}) if code.isdigit() else {}
 kind=col.get('material','BASIC');alpha=col.get('alpha',1.)
 mat.node_tree.nodes.clear();bs=mat.node_tree.nodes.new('ShaderNodeBsdfPrincipled');dest=mat.node_tree.nodes.new('ShaderNodeOutputMaterial');mat.node_tree.links.new(bs.outputs['BSDF'],dest.inputs['Surface'])
 bs.inputs['Base Color'].default_value=color;bs.inputs['Alpha'].default_value=1.
 bs.inputs['Roughness'].default_value=.75 if kind=='RUBBER' else .3 if kind in ('CHROME','METAL') else .48
 bs.inputs['Metallic'].default_value=.85 if kind=='CHROME' else .65 if kind=='METAL' else .2 if kind=='PEARLESCENT' else 0.
 mat.use_backface_culling=True
 classes.append({'name':mat.name,'linear_base_color':list(color),'alpha':alpha,'ldraw_class':kind})
# Keep both parts in shared root coordinates for a pivot rotation in gameplay.
# ImportLDraw meshes are already in stud units, with object-local LDraw axes.
articulation=None
if a.articulate_cannon:
 assert len(objects)==2 and a.simplify==1., 'Articulated cannon must preserve both complete source parts'
 base=next(obj for obj in objects if obj.name.endswith('_2527.dat'))
 barrel=next(obj for obj in objects if obj.name.endswith('_518.dat'))
 def canonical(v):return [v.x,v.z,-v.y]
 pivot=barrel.matrix_world@Vector((0,0,0))
 muzzle=barrel.matrix_world@Vector((0,0,-3.5))
 axis=(muzzle-pivot).normalized()
 # Check that the requested source muzzle plane is the actual front ring.
 source_lip=[v.co for v in barrel.data.vertices if abs(v.co.z+3.5)<.001]
 assert len(source_lip)>=16, 'Missing source muzzle ring'
 assert abs(min(v.co.z for v in barrel.data.vertices)+3.5)<.001
 base.name=a.stem+'_00_base';barrel.name=a.stem+'_01_barrel'
 objects=[base,barrel]
 articulation={'schema':1,'coordinate_system':'Y-up; one unit per stud; muzzle faces +Z',
  'mesh_layout':[{'mesh':0,'node':0,'name':base.name,'part':'2527.dat'},
                 {'mesh':1,'node':1,'name':barrel.name,'part':'518.dat'}],
  'node_transforms':'identity; vertices share grounded assembly root coordinates',
  'barrel_pivot':canonical(pivot),'muzzle_origin':canonical(muzzle),
  'muzzle_direction':canonical(axis),'source_elevation_degrees':15.,
  'muzzle_distance_from_pivot':3.5,'bore_radius':.5,
  'barrel_transform':'T(pivot) * Rx(-(elevation_degrees - 15) * pi/180) * T(-pivot)',
  'world_transform':'T(world_origin) * Ry(yaw) * barrel_transform; omit barrel_transform for base',
  'projectile_clearance':'Spawn centre beyond transformed muzzle by ball radius plus a small clearance; test full swept sphere against scenery',
  'source_evidence':'518.dat muzzle rings at (0,0,-70) LDU; pivot pins on local X through origin; 2527c01.dat supplies part transform',
  'muzzle_ring_vertices_checked':len(source_lip)}
bpy.ops.object.select_all(action='DESELECT')
for obj in objects:obj.select_set(True)
bpy.context.view_layer.objects.active=objects[0]
bpy.ops.object.convert(target='MESH')
if not a.articulate_cannon:
 bpy.ops.object.join();objects=[bpy.context.object];objects[0].name=a.stem
original_triangles=0;runtime_triangles=0
for obj in objects:
 bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
 bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
 obj.data.calc_loop_triangles();original_triangles+=len(obj.data.loop_triangles)
 if a.simplify<1.:
  mod=obj.modifiers.new('Runtime triangle reduction','DECIMATE');mod.decimate_type='COLLAPSE';mod.ratio=a.simplify;mod.use_collapse_triangulate=True
  bpy.ops.object.modifier_apply(modifier=mod.name);obj.data.calc_loop_triangles()
 runtime_triangles+=len(obj.data.loop_triangles)
points=[obj.matrix_world@Vector(v) for obj in objects for v in obj.bound_box]
lo=[min(v[i] for v in points) for i in range(3)];hi=[max(v[i] for v in points) for i in range(3)]
for obj in objects:obj.select_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(out/(a.stem+'-runtime.blend')),compress=True)
glb=out/(a.stem+'.glb')
bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
if articulation:
 payload=glb.read_bytes();json_length,chunk_type=struct.unpack_from('<II',payload,12)
 assert chunk_type==0x4e4f534a
 document=json.loads(payload[20:20+json_length])
 assert len(document['meshes'])==len(document['nodes'])==2
 for entry in articulation['mesh_layout']:
  node=document['nodes'][entry['node']]
  assert node['mesh']==entry['mesh'] and node['name']==entry['name'], document['nodes']
  assert all(key not in node for key in ('matrix','translation','rotation','scale')), node
 (out/'articulation.json').write_text(json.dumps(articulation,indent=2)+'\n')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report_data={'schema':1,'asset_id':a.asset_id,'source_sha256':sha(source/a.model),'importer_commit':revision,'blender_version':bpy.app.version_string,'recipe_sha256':sha(Path(__file__)),'stud_pitch':1,'source_part_meshes':source_part_mesh_count,'source_triangles':original_triangles,'runtime_triangles':runtime_triangles,'canonical_bounds':{'minimum':[lo[0],lo[2],-hi[1]],'maximum':[hi[0],hi[2],-lo[1]]},'front':'+Z','glb_sha256':sha(glb),'materials':classes,'modifications':['LDraw to Y-up at 1 unit per 20 LDU stud','Centered on X/Z and grounded at Y=0','Separate base/barrel meshes in shared grounded frame; editable source retained' if articulation else 'Material-batched static derivative; editable source assembly retained',f'Runtime triangle ratio {a.simplify}; source assembly unchanged','No added stud logos or procedural substitute parts','Translucent source pieces are opaque in salvage-rigid-v1; no refraction' ],'import_messages':messages}
if articulation:report_data['articulation']=articulation
(out/'provenance.json').write_text(json.dumps(report_data,indent=2)+'\n')
if a.proof:
 scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=32;scene.cycles.use_denoising=True
 scene.render.resolution_x=1100;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
 scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.35,.35,.35,1);scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.45
 scene.view_settings.view_transform='AgX'
 center=Vector(tuple((lo[i]+hi[i])/2 for i in range(3)));size=max(hi[i]-lo[i] for i in range(3))
 def aim(ob):ob.rotation_euler=(center-ob.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=center+Vector((-.95,-1.4,.9))*size);camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=size*1.6;aim(camera);scene.camera=camera
 for offset,power in [((-1,-1,2),70*size*size),((1,.4,1),30*size*size)]:
  bpy.ops.object.light_add(type='AREA',location=center+Vector(offset)*size);light=bpy.context.object;light.data.energy=power;light.data.size=size;aim(light)
 bpy.ops.mesh.primitive_plane_add(size=size*200,location=(0,0,lo[2]-.03));floor=bpy.context.object;mat=bpy.data.materials.new('studio_floor');mat.diffuse_color=(.35,.35,.35,1);floor.data.materials.append(mat)
 scene.render.filepath=str(out/'preview.png');bpy.ops.render.render(write_still=True)
print('LDRAW_IMPORT_COMPLETE',flush=True)
# Snap Blender can hang in PulseAudio shutdown on headless hosts. All files are
# synchronously saved above; only background batch mode is permitted here.
sys.stdout.flush();os._exit(0)
