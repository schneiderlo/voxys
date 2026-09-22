#!/usr/bin/env python3
"""Blender background import of the pinned, attributed LDraw 21325 assembly.

Uses TobyLobster/ImportLDraw c306fb777a4e0da85492f09d65daf458767a0aa1.
The source assembly remains editable; the runtime derivative batches materials
and simplifies triangles to fit salvage-rigid-v1 without changing stud scale.
"""
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path
from types import SimpleNamespace
import bpy
from mathutils import Vector

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--importer-root',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--proof',action='store_true')
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
assert bpy.app.background and '--factory-startup' in sys.argv
source=a.source.resolve();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
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
loadldraw.loadFromFile(SimpleNamespace(report=report),str(source/'21325-medieval-blacksmith.mpd'))
objects=[obj for obj in bpy.context.scene.objects if obj.type=='MESH']
bpy.ops.wm.save_as_mainfile(filepath=str(out/'blacksmith-assembly.blend'),compress=True)
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
bpy.ops.object.select_all(action='DESELECT')
for obj in objects:obj.select_set(True)
bpy.context.view_layer.objects.active=objects[0]
bpy.ops.object.convert(target='MESH');bpy.ops.object.join();obj=bpy.context.object;obj.name='21325_Medieval_Blacksmith'
bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM');bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
obj.data.calc_loop_triangles();original_triangles=len(obj.data.loop_triangles)
mod=obj.modifiers.new('Runtime triangle reduction','DECIMATE');mod.decimate_type='COLLAPSE';mod.ratio=.82;mod.use_collapse_triangulate=True
bpy.ops.object.modifier_apply(modifier=mod.name);obj.data.calc_loop_triangles()
points=[obj.matrix_world@Vector(v) for v in obj.bound_box]
lo=[min(v[i] for v in points) for i in range(3)];hi=[max(v[i] for v in points) for i in range(3)]
bpy.ops.wm.save_as_mainfile(filepath=str(out/'blacksmith-runtime.blend'),compress=True)
glb=out/'blacksmith.glb'
bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report_data={'schema':1,'asset_id':'ldraw-21325-blacksmith-r01','source_sha256':sha(source/'21325-medieval-blacksmith.mpd'),'importer_commit':revision,'blender_version':bpy.app.version_string,'recipe_sha256':sha(Path(__file__)),'stud_pitch':1,'source_part_meshes':len(objects),'source_triangles':original_triangles,'runtime_triangles':len(obj.data.loop_triangles),'canonical_bounds':{'minimum':[lo[0],lo[2],-hi[1]],'maximum':[hi[0],hi[2],-lo[1]]},'front':'+Z','glb_sha256':sha(glb),'materials':classes,'modifications':['LDraw to Y-up at 1 unit per 20 LDU stud','Centered on X/Z and grounded at Y=0','Material-batched static derivative; editable source assembly retained','18 percent triangle reduction for bounded runtime cooker','No added stud logos or procedural substitute parts','Translucent source pieces are opaque in salvage-rigid-v1; no refraction' ],'import_messages':messages}
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
