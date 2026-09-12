"""Original Cove salvage robot: rigid articulated meshes and actual Blender clips.

No image generation, viewport capture, skinning, physics part or root motion.
Use Blender --background --factory-startup --python-exit-code 1 --python this.py
-- --output-dir <new directory>. Canonical metres: +Y up, -Z forward; the
established exported-glTF-to-canonical rotation is explicitly cube basis 12.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Quaternion, Vector

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0, str(ROOT/'tools/salvage_assets'))
import author_functional_kit as art
import author_cove_toy_art as toy

CLIPS = {'idle': 60, 'walk': 24, 'jump': 15, 'fall': 18,
         'swim': 36, 'helm': 60, 'tool': 30, 'land': 12}
ANCHORS = ['robot_head_anchor', 'robot_hand_l_anchor', 'robot_hand_r_anchor',
           'robot_tool_anchor', 'robot_helm_l_anchor', 'robot_helm_r_anchor']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def empty(name, parent, pivot):
    obj = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(obj)
    obj.parent = parent
    obj.location = Vector(art.geo.to_blender(pivot)) - (Vector(parent['pivot']) if parent else Vector((0,0,0)))
    obj['pivot'] = Vector(art.geo.to_blender(pivot))
    obj.rotation_mode = 'QUATERNION'
    return obj


def build(lod):
    mats = toy.materials(lod)
    nodes = {}
    checks = []
    root = empty('robot_root', None, (0,0,0)); nodes[root.name] = root

    def part(name, parent, pivot, draw):
        model = art.Model(name, lod)
        draw(model)
        obj, check = model.finish(mats, art.shading.ANALYTIC, metric_uv=False)
        obj.name = name
        point = Vector(art.geo.to_blender(pivot))
        for vertex in obj.data.vertices:
            vertex.co -= point
        obj.parent = nodes[parent]
        obj.location = point-Vector(obj.parent['pivot'])
        obj.rotation_mode = 'QUATERNION'
        obj['pivot'] = point
        obj['rest_location'] = obj.location.copy()
        nodes[name] = obj; checks.append({'node': name, **check})

    def pelvis(m):
        m.box('waist_casting',(0,.775,0),(.158,.105,.135),'teal',.035)
        m.cylinder('waist_bearing',(0,.885,0),.12,.05,1,'rubber')
    part('robot_pelvis','robot_root',(0,.76,0),pelvis)

    def torso(m):
        m.box('cream_chest',(0,1.125,0),(.16,.22,.15),'cream',.045)
        m.box('teal_backpack',(0,1.12,.17),(.13,.175,.075),'teal',.035)
        m.box('front_service_panel',(0,1.11,-.156),(.128,.135,.012),'teal',.016)
        m.box('orange_rescue_stripe',(-.078,1.12,-.174),(.024,.116,.009),'coral',.008)
        m.box('status_window',(.065,1.18,-.174),(.055,.022,.008),'slate',.007)
        m.box('status_light',(.065,1.18,-.184),(.035,.009,.004),'coral',.003)
    part('robot_torso','robot_pelvis',(0,1.025,0),torso)

    def head(m):
        m.cylinder('neck_joint',(0,1.405,0),.075,.10,1,'teal')
        # The rounded helmet fits the actual upper capsule hemisphere, not
        # merely its horizontal cylinder. Retain a broad face below the crown.
        count=(32,24,16)[lod]
        latitudes=((-40,-20,0,20,40,60,75),(-40,-10,20,50,72),(-40,0,40,70))[lod]
        points=[]
        for latitude in latitudes:
            angle=math.radians(latitude)
            for i in range(count):
                longitude=i*2*math.pi/count
                points.append((.215*math.cos(angle)*math.cos(longitude),
                               1.475+.19*math.sin(angle),-.01+.155*math.cos(angle)*math.sin(longitude)))
        points.append((0,1.665,-.01));top=len(points)-1
        faces=[tuple(reversed(range(count)))]
        for ring in range(len(latitudes)-1):
            faces.extend((ring*count+i,ring*count+(i+1)%count,
                          (ring+1)*count+(i+1)%count,(ring+1)*count+i) for i in range(count))
        faces.extend(((len(latitudes)-1)*count+i,(len(latitudes)-1)*count+(i+1)%count,top) for i in range(count))
        shell=m.add(art.geo.object_mesh('rounded_helmet_shell',points,faces),'cream',0)
        shell['surface_kind']='machined' # Weighted manufactured normals on the actual curved shell.
        m.box('teal_brow',(0,1.579,-.158),(.135,.015,.025),'teal',.009)
        m.box('dark_face_window',(0,1.515,-.140),(.150,.055,.048),'slate',.016)
        # Unequal optical instruments give the robot its own utility silhouette.
        m.cylinder('large_optic_ring',(-.0725,1.525,-.195),.045,.026,2,'coral')
        m.cylinder('large_optic_lens',(-.0725,1.525,-.214),.029,.012,2,'cream')
        m.box('small_optic',(.080,1.525,-.201),(.035,.017,.010),'cream',.006)
        m.cylinder('side_receiver',(.20,1.50,.02),.050,.050,0,'teal')
        m.cylinder('receiver_tip',(0,1.6845,0),.012,.030,1,'coral')
    part('robot_head','robot_torso',(0,1.425,0),head)

    for side, x in (('l',-.220),('r',.220)):
        def upper(m, x=x):
            m.cylinder('shoulder_axle',(x,1.285,0),.074,.13,0,'steel')
            m.box('upper_arm_shell',(x,1.18,0),(.068,.112,.075),'cream',.026)
        part('robot_arm_'+side,'robot_torso',(x,1.285,0),upper)
        def forearm(m, x=x):
            m.cylinder('elbow_axle',(x,1.045,0),.052,.12,0,'teal')
            m.box('forearm_shell',(x,.955,-.008),(.067,.085,.075),'cream',.023)
            m.box('forearm_guard',(x,.94,.071),(.065,.065,.010),'teal',.008)
        part('robot_forearm_'+side,'robot_arm_'+side,(x,1.045,0),forearm)
        def hand(m, x=x):
            m.box('gripper_palm',(x,.828,-.005),(.066,.047,.055),'teal',.018)
            for dx in (-.045,.045):
                m.box('parallel_grip_jaw',(x+dx,.783,-.012),(.018,.032,.05),'steel',.010)
        part('robot_hand_'+side,'robot_forearm_'+side,(x,.875,0),hand)

    for side, x in (('l',-.135),('r',.135)):
        def thigh(m, x=x):
            m.cylinder('hip_axle',(x,.755,0),.066,.135,0,'steel')
            m.box('thigh_shell',(x,.597,0),(.084,.125,.083),'cream',.025)
        part('robot_thigh_'+side,'robot_pelvis',(x,.76,0),thigh)
        def shin(m, x=x):
            m.cylinder('knee_hinge',(x,.43,0),.061,.145,0,'teal')
            m.box('shin_shell',(x,.283,-.014),(.082,.115,.09),'cream',.025)
            m.box('knee_guard',(x,.438,-.065),(.078,.065,.025),'teal',.015)
        part('robot_shin_'+side,'robot_thigh_'+side,(x,.43,0),shin)
        def foot(m, x=x):
            m.box('boot_sole',(x,.028,-.062),(.115,.028,.158),'rubber',.012)
            m.box('boot_shell',(x,.083,-.068),(.105,.045,.144),'cream',.022)
        part('robot_foot_'+side,'robot_shin_'+side,(x,.13,0),foot)

    for name, parent, pivot in (
        ('robot_head_anchor','robot_head',(0,1.530,-.21)),
        ('robot_hand_l_anchor','robot_hand_l',(-.220,.787,-.015)),
        ('robot_hand_r_anchor','robot_hand_r',(.220,.787,-.015)),
        ('robot_tool_anchor','robot_hand_r',(.220,.762,-.07)),
        ('robot_helm_l_anchor','robot_hand_l',(-.220,.787,-.015)),
        ('robot_helm_r_anchor','robot_hand_r',(.220,.787,-.015))):
        nodes[name] = empty(name,nodes[parent],pivot)
    return nodes, checks


def animation_pose(clip, fraction):
    theta = fraction*2*math.pi
    angles = {}
    offset = Vector((0,0,0))
    def angle(name,x=0,y=0,z=0): angles['robot_'+name] = (x,y,z)
    angle('head',0,.055*math.sin(theta),0)
    for side in ('l','r'): angle('forearm_'+side,.7)
    if clip == 'idle':
        offset.y = .003*math.sin(theta)
        angle('torso',.009*math.sin(theta),0,0)
    elif clip == 'walk':
        offset.y = -.03
        for side,phase in (('l',theta),('r',theta+math.pi)):
            z = -.16*math.cos(phase)
            y = .1305+.07*max(0,math.sin(phase))
            down = .73-y
            distance = math.hypot(down,z)
            hip = math.atan2(-z,down)+math.acos(max(-1,min(1,(.33**2+distance**2-.30**2)/(2*.33*distance))))
            knee = -math.acos(max(-1,min(1,(distance**2-.33**2-.30**2)/(2*.33*.30))))
            angle('thigh_'+side,hip);angle('shin_'+side,knee);angle('foot_'+side,-hip-knee)
            angle('arm_'+side,-.36*math.cos(phase));angle('forearm_'+side,.7)
        angle('torso',0,.035*math.sin(theta),0)
    elif clip == 'jump':
        t = math.sin(fraction*math.pi/2)
        for side in ('l','r'):
            angle('arm_'+side,.8*t);angle('forearm_'+side,.35*t)
            angle('thigh_'+side,.3*t);angle('shin_'+side,-.65*t);angle('foot_'+side,.35*t)
    elif clip == 'fall':
        for side,sign in (('l',-1),('r',1)):
            angle('arm_'+side,.15,0,sign*(.4+.04*math.sin(theta)))
            angle('forearm_'+side,.3);angle('shin_'+side,-.15)
        angle('head',-.10+.035*math.sin(theta))
    elif clip == 'swim':
        angle('pelvis',.38);angle('torso',.28)
        for side,sign in (('l',-1),('r',1)):
            angle('arm_'+side,1.05+.5*math.sin(theta+sign*math.pi/2),0,sign*.2)
            angle('forearm_'+side,.4)
            angle('thigh_'+side,.20*math.sin(theta+sign*math.pi/2))
            angle('shin_'+side,-.2)
    elif clip == 'helm':
        for side in ('l','r'):
            angle('arm_'+side,.78);angle('forearm_'+side,.38)
            angle('hand_'+side,-.15,0,.045*math.sin(theta))
        angle('head',-.06,.07*math.sin(theta),0)
    elif clip == 'tool':
        angle('arm_r',1.0+.075*math.sin(theta));angle('forearm_r',.45)
        angle('hand_r',.20*math.sin(theta));angle('arm_l',.3);angle('forearm_l',.8)
        angle('head',-.13,-.12,0)
    elif clip == 'land':
        bend = math.sin(math.pi*fraction)
        offset.y = -.065*bend
        for side in ('l','r'):
            angle('thigh_'+side,.32*bend);angle('shin_'+side,-.64*bend)
            angle('foot_'+side,.32*bend);angle('arm_'+side,.25*bend)
        angle('torso',.10*bend)
    return angles,offset


def add_clips(nodes):
    animated = [obj for obj in nodes.values() if obj.type == 'MESH']
    for obj in animated:
        obj.animation_data_create()
    for clip, frames in CLIPS.items():
        for obj in animated:
            action = bpy.data.actions.new(clip+'__'+obj.name)
            obj.animation_data.action = action
            for frame in range(frames+1):
                angles,offset = animation_pose(clip,frame/frames)
                x,y,z = angles.get(obj.name,(0,0,0))
                obj.rotation_quaternion = (Quaternion(Vector((-1,0,0)),x)
                    @Quaternion(Vector((0,0,1)),y)@Quaternion(Vector((0,1,0)),z))
                obj.keyframe_insert('rotation_quaternion',frame=frame)
                if obj.name == 'robot_pelvis':
                    obj.location = Vector(obj['rest_location'])+Vector(art.geo.to_blender(offset))
                    obj.keyframe_insert('location',frame=frame)
            for layer in action.layers:
                for strip in layer.strips:
                    for bag in strip.channelbags:
                        for curve in bag.fcurves:
                            for key in curve.keyframe_points: key.interpolation='LINEAR'
            track = obj.animation_data.nla_tracks.new(); track.name = clip
            track.strips.new(clip,0,action); track.mute=True
            obj.animation_data.action = None
            obj.rotation_quaternion = (1,0,0,0); obj.location=Vector(obj['rest_location'])
    bpy.context.scene.frame_set(0)


def prune_export(path):
    """Remove constant rest channels and unit-scale roundoff from NLA baking.

    Geometry and moving authored keys remain exporter output. Blender's matrix
    decomposition adds <=1e-6 unit-scale roundoff, which is rejected above that
    bound and otherwise discarded. This neither synthesizes clips nor motion.
    """
    raw=path.read_bytes(); length,kind=struct.unpack_from('<II',raw,12)
    assert kind==0x4e4f534a
    doc=json.loads(raw[20:20+length]); binary=raw[28+length:]
    def values(index):
        accessor=doc['accessors'][index];view=doc['bufferViews'][accessor['bufferView']]
        width={'SCALAR':1,'VEC3':3,'VEC4':4}[accessor['type']]
        assert accessor['componentType']==5126
        offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
        stride=view.get('byteStride',width*4)
        return [struct.unpack_from('<'+'f'*width,binary,offset+i*stride) for i in range(accessor['count'])]
    dropped=0
    for animation in doc['animations']:
        kept=[]
        for channel in animation['channels']:
            target=channel['target'];node=doc['nodes'][target['node']];prop=target['path']
            sampler=animation['samplers'][channel['sampler']]
            samples=values(sampler['output'])
            default=node.get(prop,{'translation':[0,0,0],'rotation':[0,0,0,1],'scale':[1,1,1]}[prop])
            tolerance=1e-6 if prop=='scale' else 1e-7
            constant=all(all(abs(a-b)<tolerance for a,b in zip(value,default)) for value in samples)
            if constant:
                dropped+=1;continue
            assert prop!='scale', 'Robot scales must never animate'
            assert node['name']!='robot_root', 'Robot root motion is prohibited'
            times=[v[0] for v in values(sampler['input'])]
            assert len(times)>=2 and times[0]==0 and all(a<b for a,b in zip(times,times[1:]))
            assert sampler.get('interpolation','LINEAR') in ('LINEAR','STEP')
            kept.append(channel)
        assert kept, animation['name']+' lost all real motion'
        animation['channels']=kept
        # Remove unused samplers so the profile can reject unsupported data
        # without exceptions for unreachable scale or constant channels.
        used=sorted({c['sampler'] for c in kept})
        animation['samplers']=[animation['samplers'][i] for i in used]
        for channel in kept: channel['sampler']=used.index(channel['sampler'])
    encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4)
    blob=struct.pack('<III',0x46546c67,2,12+8+len(encoded)+8+len(binary))
    blob+=struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary
    path.write_bytes(blob)
    return doc,dropped


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if not bpy.app.background or '--factory-startup' not in sys.argv: parser.error('background factory startup required')
    out=args.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir(): parser.error('new output directory required')
    out.mkdir();source=out/'source';source.mkdir();records=[]
    for lod in range(3):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.preferences.filepaths.file_preview_type='NONE'
        scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.render.fps=30
        scene.frame_start=0;scene.frame_end=60
        nodes,checks=build(lod);add_clips(nodes)
        bpy.ops.object.select_all(action='SELECT')
        path=source/f'robot-lod-{lod}.glb'
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
            export_yup=True,export_apply=False,export_materials='EXPORT',export_normals=True,
            export_texcoords=False,export_tangents=False,export_animations=True,
            export_animation_mode='NLA_TRACKS',export_frame_range=False,export_frame_step=1,
            export_force_sampling=True,export_optimize_animation_size=False,
            export_optimize_animation_keep_anim_object=True,
            export_skins=False,export_morph=False,export_cameras=False,export_lights=False,
            export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
        doc,dropped=prune_export(path)
        primitives=[p for mesh in doc['meshes'] for p in mesh['primitives']]
        vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)
        triangles=sum(doc['accessors'][p['indices']]['count']//3 for p in primitives)
        assert len(doc['nodes'])<=32 and len(doc['meshes'])<=24 and len(primitives)<=48
        assert {a['name'] for a in doc['animations']}==set(CLIPS)
        assert all(not ('TANGENT' in p['attributes'] or 'TEXCOORD_0' in p['attributes']) for p in primitives)
        assert vertices<=((10500,7500,4500)[lod]) and triangles<=((8000,5000,2600)[lod])
        blend=source/f'robot-lod-{lod}.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend))
        records.append(dict(lod=lod,asset_id=f'voxys-cove-robot-r01-lod{lod}',
            glb=path.name,glb_sha256=sha(path),blend=blend.name,blend_sha256=sha(blend),
            nodes=len(doc['nodes']),meshes=len(doc['meshes']),draws=len(primitives),
            vertices=vertices,triangles=triangles,
            clips=[dict(name=a['name'],channels=len(a['channels'])) for a in doc['animations']],
            removed_constant_rest_channels=dropped,geometry_checks=checks))
        print('ROBOT_LOD_COMPLETE',lod,vertices,triangles,len(primitives),flush=True)
    helpers=[Path(__file__),Path(art.__file__),Path(toy.__file__),Path(art.geo.__file__),
             Path(art.base.__file__),Path(art.metric.__file__),Path(art.shading.__file__)]
    provenance=dict(schema=1,asset='original-cove-salvage-robot',author='Voxys original procedural artwork',
        license='Original project asset; same project distribution terms; no external art inputs.',
        blender=bpy.app.version_string,profile='salvage-animated-rigid-v1',render_to_canonical=12,
        canonical=dict(up='+Y',forward='-Z',units='metres',height=1.7,root_motion=False),
        clips=list(CLIPS),anchors=ANCHORS,palette=toy.PALETTE,
        inputs={str(p.resolve().relative_to(ROOT)):sha(p) for p in helpers},lods=records,
        reproduction='blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_robot.py -- --output-dir <new-directory>',
        export_cleanup='Remove rest-equal channels and <=1e-6 unit-scale bake roundoff; actual moving translation/quaternion keys unchanged.',
        scope='Rigid presentation character only; no gameplay parts, sockets, body masses or inventory changes.')
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print('ROBOT_AUTHORING_COMPLETE',str(out),flush=True)


if __name__=='__main__': main()
