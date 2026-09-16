#!/usr/bin/env python3
"""One CPU contact sheet from the three installed, unmodified resident sources."""
import argparse
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--package',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
if not bpy.app.background or '--factory-startup' not in sys.argv:p.error('background factory startup required')
if a.output.exists():p.error('new output path required')
bpy.ops.wm.read_factory_settings(use_empty=True);scene=bpy.context.scene
for key,x,yaw in [('moss',-1.,-.15),('rivet',0.,-.48),('lumen',1.,.08)]:
    path=a.package/key/'source/character-lod-0.blend'
    with bpy.data.libraries.load(str(path.resolve()),link=False) as (data_from,data_to):data_to.objects=list(data_from.objects)
    group=bpy.data.collections.new('proof_'+key);scene.collection.children.link(group)
    for obj in data_to.objects:
        if obj is None:continue
        group.objects.link(obj)
        if obj.animation_data:obj.animation_data_clear()
    root=next(obj for obj in data_to.objects if obj and obj.name.startswith('robot_root'))
    root.location=(x,0,0);root.rotation_mode='XYZ';root.rotation_euler=(0,0,yaw)
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.render.resolution_x=1500;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.render.film_transparent=False
scene.world=bpy.data.worlds.new('proof_world');scene.world.use_nodes=True
scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.28,.24,.19,1)
scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.5
scene.view_settings.view_transform='AgX'
def aim(obj,target):obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.003))
floor=bpy.context.object;m=bpy.data.materials.new('proof_warm_neutral');m.diffuse_color=(.20,.17,.13,1);floor.data.materials.append(m)
for position,power,size in [((-3,-4,6),650,5),((4,-1,3),280,4),((1,3,5),500,4)]:
    bpy.ops.object.light_add(type='AREA',location=position);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size;aim(light,(0,0,.9))
bpy.ops.object.camera_add(location=(-.25,-6,2.65));camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=3.45;aim(camera,(0,0,.84));scene.camera=camera
scene.render.filepath=str(a.output.resolve());bpy.ops.render.render(write_still=True)
print('RESIDENT_PROOF_COMPLETE',a.output,flush=True)
