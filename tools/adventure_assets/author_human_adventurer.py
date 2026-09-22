#!/usr/bin/env python3
"""Original warm brick minifigure person, using the bounded rigid character ABI.

The robot_* node labels are an animation ABI, not the visible character. This
source uses a cylindrical yellow head, printed-style face, trapezoid torso,
block legs and open C-shaped toy hands at the existing 1.7 m avatar scale.
No skinning, cloth simulation, external meshes, texture images or root motion.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Quaternion, Vector

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0, str(ROOT/'tools/salvage_assets'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import builder_reference_shapes as reference
import author_functional_kit as art
import author_cove_robot as compatible

BUILDER = False
CLIPS = compatible.CLIPS
ANCHORS = compatible.ANCHORS
PALETTE = {'skin':'F1C94F','cream':'E9DCC1','moss':'60714B','rust':'AA5034',
           'leather':'80583B','hair':'513827','dark':'29231F','brass':'BBA06A'}
PARAMETERS = dict(height=1.7, capsule_radius=.3, pelvis_y=.57225, knee_y=.30,
                  ankle_y=.10, hip_x=.108, hip_y=.55, shoulder_x=.215, shoulder_y=1.135,
                  elbow_y=.82, wrist_y=.70, head_bottom=1.20176, head_cylinder_top=1.57616,
                  visible_knees=False, visible_elbow_split=False,
                  source_basis='(-X, Z, Y)', render_to_canonical=12)

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def materials(lod):
    out={}
    for key,color in PALETTE.items():
        m=bpy.data.materials.new(f'human_{key}_lod{lod}');m.use_nodes=True
        m.use_backface_culling=True
        p=m.node_tree.nodes.get('Principled BSDF')
        p.inputs['Base Color'].default_value=tuple(art.metric.linear_channel(int(color[i:i+2],16)) for i in (0,2,4))+(1.,)
        p.inputs['Roughness'].default_value=(.22 if key=='hair' else .25 if key=='skin' else .30) if BUILDER else (.42 if key=='skin' else .58)
        p.inputs['Metallic'].default_value=.35 if key=='brass' else 0
        if key=='socket':
            p.inputs['Roughness'].default_value=.55
        out[key]=m
    return out

class HumanModel(art.Model):
    def mesh(self,name,points,faces,color,smooth=True):
        obj=self.add(art.geo.object_mesh(name,points,faces),color,0)
        for face in obj.data.polygons:face.use_smooth=smooth
        return obj

    def ellipsoid(self,name,center,radius,color,tilt=0,detail=1):
        n=max(8,int((16,12,8)[self.lod]*detail));rings=max(4,int((8,6,4)[self.lod]*detail))
        def transformed(x,y,z):
            return (center[0]+x*math.cos(tilt)-y*math.sin(tilt),
                    center[1]+x*math.sin(tilt)+y*math.cos(tilt),center[2]+z)
        pts=[transformed(0,-radius[1],0)]
        for r in range(1,rings):
            lat=-math.pi/2+r*math.pi/rings
            for i in range(n):
                a=2*math.pi*i/n
                pts.append(transformed(radius[0]*math.cos(lat)*math.cos(a),radius[1]*math.sin(lat),radius[2]*math.cos(lat)*math.sin(a)))
        pts.append(transformed(0,radius[1],0));top=len(pts)-1
        faces=[(0,1+(i+1)%n,1+i) for i in range(n)]
        for r in range(rings-2):
            a=1+r*n;b=a+n
            faces.extend((a+i,a+(i+1)%n,b+(i+1)%n,b+i) for i in range(n))
        faces.extend((top,top-n+i,top-n+(i+1)%n) for i in range(n))
        return self.mesh(name,pts,faces,color)

    def loft(self,name,rings,color,n=None):
        # Cross sections: (Y, centreX, centreZ, radiusX, radiusZ).
        # Closed cloth forms overlap slightly at joints so no metal hinges or
        # open cuts are exposed by the supported walk/interaction motion.
        n=n or (16,12,8)[self.lod];pts=[]
        for y,x,z,rx,rz in rings:
            for i in range(n):
                a=2*math.pi*i/n
                pts.append((x+rx*math.cos(a),y,z+rz*math.sin(a)))
        faces=[tuple(reversed(range(n)))]
        for r in range(len(rings)-1):
            faces.extend((r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i) for i in range(n))
        faces.append(tuple(range(len(pts)-n,len(pts))))
        obj=self.mesh(name,pts,faces,color)
        obj.data.polygons[0].use_smooth=False;obj.data.polygons[-1].use_smooth=False
        return obj

    def cord(self,name,points,radius,color):
        # A real swept tube with capped ends; used for small seams and eyebrows.
        n=(8,6,5)[self.lod];verts=[]
        for i,p in enumerate(points):
            tangent=Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])
            tangent.normalize();u=tangent.cross(Vector((0,0,1)))
            if u.length<.01:u=tangent.cross(Vector((0,1,0)))
            u.normalize();v=tangent.cross(u)
            for k in range(n):verts.append(tuple(Vector(p)+radius*(math.cos(k*2*math.pi/n)*u+math.sin(k*2*math.pi/n)*v)))
        faces=[tuple(reversed(range(n)))]
        faces.extend((i*n+k,i*n+(k+1)%n,(i+1)*n+(k+1)%n,(i+1)*n+k) for i in range(len(points)-1) for k in range(n))
        faces.append(tuple(range(len(verts)-n,len(verts))))
        return self.mesh(name,verts,faces,color)

def build(lod):
    mats=materials(lod);nodes={};checks=[]
    root=compatible.empty('robot_root',None,(0,0,0));nodes[root.name]=root
    def part(name,parent,pivot,draw):
        m=HumanModel(name,lod);draw(m)
        obj,check=m.finish(mats,art.shading.ANALYTIC if BUILDER else art.shading.FLAT,metric_uv=False)
        if BUILDER and name.startswith('robot_thigh_'):
            # Baked cavity occlusion belongs only on the deep, flat floor.
            # The visible blue rim, tapered walls and inset ledge keep plastic.
            slot=len(obj.data.materials);obj.data.materials.append(mats['socket'])
            for face in obj.data.polygons:
                if abs(face.center.y+.053)<1e-5 and face.normal.y>.999:
                    face.material_index=slot
        def proportion(point):
            x,y,z=point
            if name=='robot_head':return (x*(1.368 if BUILDER else 1.14),1.7+(y-1.7)*(1.65 if BUILDER else 1.44),z*(1.368 if BUILDER else 1.14))
            if name=='robot_torso':return (x*(1.38 if BUILDER else 1.08),y*(1.12 if BUILDER else 1.18)-(.337 if BUILDER else .365),z*(1.25 if BUILDER else 1))
            if name=='robot_pelvis':return (x*(1.42 if BUILDER else 1.08),y*.75+.00225,z*(1.25 if BUILDER else 1))
            return (x*(1.22 if 'arm' in name or 'hand' in name else 1.30),y,z*1.25) if BUILDER else point
        normals=[n.vector.copy() for n in obj.data.corner_normals] if BUILDER else []
        obj.name=name;p=Vector(art.geo.to_blender(proportion(pivot)))
        for v in obj.data.vertices:
            v.co=Vector(art.geo.to_blender(proportion(art.geo.canonical(v.co))))-p
        if BUILDER:
            # Preserve analytic plastic normals through the baked nonuniform proportions.
            sx,sy,sz=(1.368,1.65,1.368) if name=='robot_head' else (1.38,1.12,1.25) if name=='robot_torso' else (1.42,.75,1.25) if name=='robot_pelvis' else (1.22 if 'arm' in name or 'hand' in name else 1.30,1,1.25)
            obj.data.normals_split_custom_set([Vector((n.x/sx,n.y/sz,n.z/sy)).normalized() for n in normals])
        obj.parent=nodes[parent];obj.location=p-Vector(obj.parent['pivot'])
        obj.rotation_mode='QUATERNION';obj['pivot']=p;obj['rest_location']=obj.location.copy()
        nodes[name]=obj;checks.append({'node':name,**check})
    def pelvis(m):
        m.box('minifigure_hip_block',(0,.818 if BUILDER else .786,0),(.190,.060 if BUILDER else .087,.125),'moss',.010 if BUILDER else .013)
        if BUILDER:reference.hip_bridge(m)
        if not BUILDER:m.box('printed_leather_belt',(0,.841,-.127),(.181,.023,.002),'leather',0)
        if not BUILDER:m.box('printed_buckle',(0,.841,-.130),(.022,.018,.001),'brass',0)
    part('robot_pelvis','robot_root',(0,.76,0),pelvis)
    def torso(m):
        # Original classic toy trapezoid silhouette, wider at the waist.
        points=[(sign*x,y,z) for y,x in ((.869,.207),(1.307,.145 if BUILDER else .161)) for z in (-.125,.125) for sign in (-1,1)]
        body=m.add(art.geo.object_mesh('trapezoid_toy_torso',points,
            [(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3)]),'rust' if BUILDER else 'moss',.010)
        # These are physical submillimetre color patches, not floating plaques.
        def print_patch(name,xy,color,z=-.1256):
            pts=[(x,y,z+d) for d in (-.0006,.0006) for x,y in xy];n=len(xy)
            faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
            faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
            m.mesh(name,pts,faces,color,False)
        if BUILDER:return reference.torso(m,print_patch)
        print_patch('linen_shirt_print',[(-.046,.885),(.046,.885),(.041,1.276),(-.041,1.276)],'cream')
        print_patch('left_waistcoat_lapel',[(-.054,.955),(-.081,1.236),(-.068,1.282),(-.024,1.260)],'leather',-.1272)
        print_patch('right_waistcoat_lapel',[(.054,.955),(.081,1.236),(.068,1.282),(.024,1.260)],'leather',-.1272)
        print_patch('scarf_tail_print',[(-.016,1.11),(.020,1.105),(.035,1.247),(-.026,1.274)],'rust',-.1288)
        m.cylinder('neck_stud',(0,1.331,0),.064,.075,1,'skin')
        m.loft('molded_scarf_collar',[(1.294,0,-.001,.084,.070),(1.316,0,-.001,.091,.071),(1.333,0,-.001,.080,.064)],'rust')
        if not BUILDER:m.box('brick_daypack',(0,1.10,.166),(.114,.139,.040),'leather',.012)
        if not BUILDER:m.box('daypack_flap',(0,1.179,.207),(.101,.048,.003),'leather',.003)
        if lod<2:
            for x in (() if BUILDER else (-.104,.104)):print_patch('printed_pack_strap',[(x-.011,.955),(x+.011,.955),(x+.011,1.270),(x-.011,1.270)],'leather')
            for y in (.988,1.055):m.box('printed_shirt_button',(0,y,-.127),(.004,.004,.0006),'brass',0)
    part('robot_torso','robot_pelvis',(0,1.025,0),torso)
    def head(m):
        if BUILDER:return reference.head(m)
        radius=.152
        m.cylinder('yellow_cylindrical_head',(0,1.520 if BUILDER else 1.484,0),radius,.260,1,'skin')
        if BUILDER:m.cylinder('exposed_head_stud',(0,1.675,0),.082,.050,1,'skin')
        def face_patch(name,xy,color):
            # A closed, very thin patch follows the actual head cylinder.
            # Its visible color behaves like a print without external textures.
            points=[]
            for lift in (.0004,.0012):
                for x,y in xy:points.append((x,y,-math.sqrt((radius+lift)**2-x*x)))
            n=len(xy);faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
            faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
            m.mesh(name,points,faces,color,False)
        n=(20,14,10)[lod]
        for x in (-.054,.054):
            face_patch('printed_friendly_eye',[(x+.013*math.cos(i*2*math.pi/n),(1.540 if BUILDER else 1.497)+.017*math.sin(i*2*math.pi/n)) for i in range(n)],'dark')
        angles=[-1.03+i*2.06/12 for i in range(13)]
        smile=[((.043+d)*math.sin(a),(1.510 if BUILDER else 1.475)-(.043+d)*math.cos(a)) for d,series in ((-.0025,angles),(.0025,list(reversed(angles)))) for a in series]
        face_patch('printed_friendly_smile',smile,'dark')
        if BUILDER:return
        # Molded hair, with a rounded crown compatible with the capsule dome.
        n=(28,20,14)[lod];ring_count=(7,6,5)[lod];points=[]
        for ring in range(ring_count):
            t=ring/ring_count
            for i in range(n):
                a=i*2*math.pi/n;front=max(0,-math.sin(a));y0=1.546+.065*front+.007*math.sin(2*a)*front
                y=y0+(1.7-y0)*math.sin(t*math.pi/2);scale=math.cos(t*math.pi/2)
                wave=1+.013*math.sin(5*a+1.8*t)*math.sin(math.pi*t)
                points.append((.166*scale*math.cos(a)*wave,y,.160*scale*math.sin(a)*wave))
        points.append((0,1.7,0));top=len(points)-1
        faces=[tuple(reversed(range(n)))]
        faces.extend((r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i) for r in range(ring_count-1) for i in range(n))
        faces.extend(((ring_count-1)*n+i,(ring_count-1)*n+(i+1)%n,top) for i in range(n))
        m.mesh('molded_swept_hair',points,faces,'hair')
        m.loft('molded_forelock',[(1.573,-.084,-.107,.007,.009),(1.600,-.062,-.136,.023,.021),
            (1.633,-.009,-.110,.046,.026),(1.656,.037,-.063,.043,.025),(1.669,.057,-.026,.017,.017)],'hair')
    part('robot_head','robot_torso',(0,1.365,0),head)
    for side,sign in (('l',-1),('r',1)):
        x=sign*.215
        def upper(m,x=x):
            if BUILDER:return reference.sleeve(m,sign)
            # One molded toy arm from shoulder to cuff, with no elbow seam.
            m.loft('single_molded_sleeve',[(.736,x,-.022,.052,.054),
                (.79,x,-.016,.059,.060),(.965,x+.010*sign,0,.067,.067),
                (1.098,x,0,.071,.072),(1.159,x-.008*sign,0,.055,.060)],'rust' if BUILDER else 'cream',n=(16,12,8)[lod])
        part('robot_arm_'+side,'robot_torso',(x,1.084 if BUILDER else 1.135,0),upper)
        wrist_x=sign*.298 if BUILDER else x
        wrist_z=-.051 if BUILDER else 0
        def lower(m,x=wrist_x):
            # The retained forearm node carries only a small covered wrist.
            m.cylinder('yellow_toy_wrist',(x,.710 if BUILDER else .720,wrist_z-.023),.040 if BUILDER else .032,.090 if BUILDER else .048,1,'skin')
        part('robot_forearm_'+side,'robot_arm_'+side,(wrist_x,.82,wrist_z),lower)
        def hand(m,x=wrist_x):
            if BUILDER:return reference.hand(m,x,wrist_z-.026)
            n=(24,18,12)[lod];cross=(8,6,5)[lod];points=[]
            for i in range(n+1):
                a=math.radians(-45+270*i/n)
                for j in range(cross):
                    b=j*2*math.pi/cross;r=(.083 if BUILDER else .060)+(.029 if BUILDER else .014)*math.cos(b)
                    points.append((x+r*math.cos(a)/(1.55 if BUILDER else 1),.653+r*math.sin(a),wrist_z-.026+(.039 if BUILDER else .014)*math.sin(b)))
            faces=[tuple(reversed(range(cross)))]
            faces.extend((i*cross+j,i*cross+(j+1)%cross,(i+1)*cross+(j+1)%cross,(i+1)*cross+j) for i in range(n) for j in range(cross))
            faces.append(tuple(range(n*cross,(n+1)*cross)))
            m.mesh('open_yellow_c_hand',points,faces,'skin')
        part('robot_hand_'+side,'robot_forearm_'+side,(wrist_x,.637 if BUILDER else .70,wrist_z),hand)
    for side,x in (('l',-.108),('r',.108)):
        def thigh(m,x=x):
            if BUILDER:return reference.thigh(m,x)
            # Whole continuous brick leg. The internal shin node is retained
            # for the ABI but never bends, so there is no visible knee joint.
            m.box('short_stout_block_leg',(x,.327,0),(.098,.227,.104),'moss',.009)
        part('robot_thigh_'+side,'robot_pelvis',(x,.48 if BUILDER else .55,0),thigh)
        def shin(m,x=x):
            if BUILDER:return reference.shin(m,x)
            m.box('printed_boot_front',(x,.199,-.105),(.089,.086,.001),'leather',0)
        part('robot_shin_'+side,'robot_thigh_'+side,(x,.30,0),shin)
        def foot(m,x=x):
            if BUILDER:return reference.foot(m,x)
            m.box('block_boot_foot',(x,.060,-.029),(.098,.060,.140),'leather',.010)
            m.box('boot_toe_print',(x,.043,-.170),(.085,.014,.001),'dark',0)
        part('robot_foot_'+side,'robot_shin_'+side,(x,.10,0),foot)
    for name,parent,pivot in (
        ('robot_head_anchor','robot_head',(0,1.40336,-.17556)),
        ('robot_hand_l_anchor','robot_hand_l',(-.215,.653,-.026)),
        ('robot_hand_r_anchor','robot_hand_r',(.215,.653,-.026)),
        ('robot_tool_anchor','robot_hand_r',(.215,.653,-.026)),
        ('robot_helm_l_anchor','robot_hand_l',(-.215,.653,-.026)),
        ('robot_helm_r_anchor','robot_hand_r',(.215,.653,-.026))):
        if BUILDER and 'head' not in name:
            pivot=(math.copysign(.298,pivot[0])*1.22,.590,(pivot[2]-.051)*1.25)
        nodes[name]=compatible.empty(name,nodes[parent],pivot)
    return nodes,checks

def animation_pose(clip,fraction):
    theta=fraction*2*math.pi;angles={};offset=Vector((0,0,0))
    def angle(name,x=0,y=0,z=0):angles['robot_'+name]=(x,y,z)
    angle('head',0,.032*math.sin(theta),0)
    # Internal knee/ankle/forearm nodes remain rigid by default. These are toy
    # limbs: walking uses hip rock with a sole-preserving root lift, not a human
    # knee gait drawn over a minifigure texture.
    if clip=='idle':angle('torso',0 if BUILDER else .005*math.sin(theta),0,0)
    elif clip=='walk':
        swing=.32*math.cos(theta)
        offset.y=max(0,(.48 if BUILDER else .55)*(math.cos(swing)-1)+(.214 if BUILDER else .171)*abs(math.sin(swing)))+.0008
        for side,sign in (('l',1),('r',-1)):
            angle('thigh_'+side,sign*swing);angle('arm_'+side,-sign*.28*math.cos(theta))
        angle('torso',0,.018*math.sin(theta),0)
    elif clip=='jump':
        t=math.sin(fraction*math.pi/2)
        for side in ('l','r'):angle('arm_'+side,.70*t);angle('thigh_'+side,.18*t)
    elif clip=='fall':
        for side,sign in (('l',-1),('r',1)):angle('arm_'+side,.12,0,sign*(.28+.02*math.sin(theta)))
        angle('head',-.07+.02*math.sin(theta))
    elif clip=='swim':
        angle('pelvis',.28);angle('torso',.20)
        for side,sign in (('l',-1),('r',1)):
            angle('arm_'+side,.9+.35*math.sin(theta+sign*math.pi/2),0,sign*.12)
            angle('thigh_'+side,.14*math.sin(theta+sign*math.pi/2))
    elif clip=='helm':
        for side in ('l','r'):angle('arm_'+side,.70);angle('hand_'+side,0,0,.025*math.sin(theta))
        angle('head',-.04,.04*math.sin(theta),0)
    elif clip=='tool':
        angle('arm_r',.85+.16*math.sin(theta));angle('hand_r',.12*math.sin(theta))
        angle('arm_l',.23);angle('head',-.08,-.06,0)
    elif clip=='land':
        # A short toy-body settle with planted continuous block legs.
        b=math.sin(math.pi*fraction)
        angle('torso',.065*b)
        for side in ('l','r'):angle('arm_'+side,.18*b)
    return angles,offset

def add_clips(nodes):
    animated=[o for o in nodes.values() if o.type=='MESH']
    for obj in animated:obj.animation_data_create()
    for clip,frames in CLIPS.items():
        for obj in animated:
            action=bpy.data.actions.new(clip+'__'+obj.name);obj.animation_data.action=action
            for frame in range(frames+1):
                angles,offset=animation_pose(clip,frame/frames);x,y,z=angles.get(obj.name,(0,0,0))
                obj.rotation_quaternion=Quaternion(Vector((-1,0,0)),x)@Quaternion(Vector((0,0,1)),y)@Quaternion(Vector((0,1,0)),z)
                obj.keyframe_insert('rotation_quaternion',frame=frame)
                if obj.name=='robot_pelvis':
                    obj.location=Vector(obj['rest_location'])+Vector(art.geo.to_blender(offset));obj.keyframe_insert('location',frame=frame)
            for layer in action.layers:
                for strip in layer.strips:
                    for bag in strip.channelbags:
                        for curve in bag.fcurves:
                            for key in curve.keyframe_points:key.interpolation='LINEAR'
            track=obj.animation_data.nla_tracks.new();track.name=clip;track.strips.new(clip,0,action);track.mute=True
            obj.animation_data.action=None;obj.rotation_quaternion=(1,0,0,0);obj.location=Vector(obj['rest_location'])
    bpy.context.scene.frame_set(0)

def main():
    global BUILDER
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--builder',action='store_true')
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);BUILDER=a.builder
    if BUILDER:
        PALETTE.update(moss='1F579F',rust='D93326',skin='FFD500',hair='603520',shirt='B9BEC2',dark='191919',socket='10294A')
        PARAMETERS.update(capsule_radius=.4,art_envelope_margin=.02,torso_width_multiplier=1.38,pelvis_width_multiplier=1.42,arm_width_multiplier=1.22,leg_width_multiplier=1.30,
            body_depth_multiplier=1.25,head_width_multiplier=1.20,exposed_head_stud=False,backpack=False,head_bottom=1.18025,head_cylinder_top=1.52675,shoulder_y=1.084,
            hip_y=.48,wrist_y=.637,hand_center_y=.590,reference_revision="owner-rear-figurine-20260916",rear_leg_sockets=2,surface_shading="manufactured-v2")
    if not bpy.app.background or '--factory-startup' not in sys.argv:p.error('background factory startup required')
    out=a.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():p.error('new output directory required')
    out.mkdir();source=out/'source';source.mkdir();records=[]
    for lod in range(3):
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.preferences.filepaths.file_preview_type='NONE'
        scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.render.fps=30;scene.frame_start=0;scene.frame_end=60
        nodes,checks=build(lod);add_clips(nodes);bpy.ops.object.select_all(action='SELECT')
        path=source/f'human-lod-{lod}.glb'
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
            export_yup=True,export_apply=False,export_materials='EXPORT',export_normals=True,
            export_texcoords=False,export_tangents=False,export_animations=True,export_animation_mode='NLA_TRACKS',
            export_frame_range=False,export_frame_step=1,export_force_sampling=True,export_optimize_animation_size=False,
            export_optimize_animation_keep_anim_object=True,export_skins=False,export_morph=False,export_cameras=False,
            export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
        doc,dropped=compatible.prune_export(path)
        prims=[p for mesh in doc['meshes'] for p in mesh['primitives']]
        vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in prims)
        triangles=sum(doc['accessors'][p['indices']]['count']//3 for p in prims)
        assert len(doc['nodes'])==22 and len(doc['meshes'])==15 and len(prims)<=48
        assert {a['name'] for a in doc['animations']}==set(CLIPS)
        assert vertices<=((11800,8300,5400) if BUILDER else (10500,7500,4500))[lod] and triangles<=((16600,11000,6800) if BUILDER else (11000,7500,4500))[lod],(vertices,triangles)
        blend=source/f'human-lod-{lod}.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
        records.append(dict(lod=lod,asset_id=f"{'voxys-free-build-builder-r01' if BUILDER else 'voxys-adventure-human-r01'}-lod{lod}",glb=path.name,glb_sha256=sha(path),
            blend=blend.name,blend_sha256=sha(blend),nodes=len(doc['nodes']),meshes=len(doc['meshes']),draws=len(prims),
            vertices=vertices,triangles=triangles,clips=[dict(name=a['name'],channels=len(a['channels'])) for a in doc['animations']],
            removed_constant_rest_channels=dropped,geometry_checks=checks))
        print('HUMAN_LOD_COMPLETE',lod,vertices,triangles,len(prims),flush=True)
    helpers=[Path(__file__),Path(reference.__file__),Path(compatible.__file__),Path(art.__file__),Path(art.geo.__file__),Path(art.base.__file__),Path(art.metric.__file__),Path(art.shading.__file__)]
    provenance=dict(schema=1,asset='original-warm-brick-minifigure',author='Voxys original procedural artwork',
        variant='classic-builder' if BUILDER else 'adventurer',
        license='Original project asset; same project distribution terms; no external art inputs.',
        blender=bpy.app.version_string,profile='salvage-animated-rigid-v1',render_to_canonical=12,
        canonical=dict(up='+Y',forward='-Z',units='metres',height=1.7,root_motion=False),parameters=PARAMETERS,
        clips=list(CLIPS),anchors=ANCHORS,palette=PALETTE,inputs={str(f.relative_to(ROOT)):sha(f) for f in helpers},lods=records,
        reproduction='/snap/blender/current/blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/adventure_assets/author_human_adventurer.py -- --output-dir <new-directory>'+(' --builder' if BUILDER else ''),
        export_cleanup='Shared strict prune of constant rest channels and <=1e-6 unit-scale export roundoff; authored moving keys retained.',
        scope=('Reference-led creative builder: swept brown hair, expressive yellow head, red printed jacket, blue molded legs, thick open grips and smooth plastic normals. ' if BUILDER else 'Original warm brick minifigure person: cylindrical yellow head, printed-style eyes/smile, molded hair, trapezoid clothed torso, block legs and open C hands. No logos, external model/texture inputs, robotic glow or organic fingers. Rigid articulated animation, not skinning. Compatibility node names retained; no gameplay/collision/save changes.') )
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print('HUMAN_AUTHORING_COMPLETE',out,flush=True)

if __name__=='__main__':main()
