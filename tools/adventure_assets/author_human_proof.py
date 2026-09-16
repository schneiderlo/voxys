#!/usr/bin/env python3
"""One CPU studio proof of the actual original brick minifigure asset. Never modifies it."""
import argparse
from pathlib import Path
import sys
import bpy
from mathutils import Vector

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
if not bpy.app.background or '--factory-startup' not in sys.argv:p.error('background factory startup required')
if a.output.exists():p.error('new proof output required')
bpy.ops.wm.open_mainfile(filepath=str(a.source.resolve()))
scene=bpy.context.scene
for obj in scene.objects:
    if obj.animation_data:obj.animation_data_clear()
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24
scene.cycles.use_denoising=True
scene.render.resolution_x=1000;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
scene.render.film_transparent=False
scene.world=bpy.data.worlds.new('proof_world');scene.world.use_nodes=True
scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.28,.24,.19,1)
scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.5
scene.view_settings.view_transform='AgX'
# Geometry itself remains the authored bind pose. No lighting/clothing changes.
def aim(obj,target):obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.003))
floor=bpy.context.object;m=bpy.data.materials.new('proof_warm_neutral');m.diffuse_color=(.20,.17,.13,1);floor.data.materials.append(m)
for position,power,size in [((-3,-4,6),450,4),((4,-1,3),220,3),((1,3,5),400,3)]:
    bpy.ops.object.light_add(type='AREA',location=position);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size;aim(light,(0,0,.9))
bpy.ops.object.camera_add(location=(-2.6,-5,2.4));camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=2.04
# Canonical forward -Z is Blender -Y; this is a front three-quarter view.
aim(camera,(0,0,.86));scene.camera=camera
scene.render.filepath=str(a.output.resolve());bpy.ops.render.render(write_still=True)
print('HUMAN_PROOF_COMPLETE',a.output,flush=True)
