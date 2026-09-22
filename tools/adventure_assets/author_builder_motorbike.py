#!/usr/bin/env python3
"""Original red construction-toy motorcycle, authored in canonical stud units.
Four independent rigid meshes: frame, steering assembly and two spinning wheels.
The supplied owner image is a design reference, never used as a runtime texture.
"""
import argparse, hashlib, json, math, sys
from pathlib import Path
import bpy
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from author_human_adventurer import HumanModel, art

S=2.8
PALETTE={'red':'C9362E','rubber':'202325','black':'303437','silver':'9AA5AB','lens':'E2E8DB','tail':'EE492B'}

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);out=a.output
    if out.exists():p.error('new output required')
    out.mkdir(parents=True);bpy.ops.wm.read_factory_settings(use_empty=True);mats={}
    for name,color in PALETTE.items():
        mat=bpy.data.materials.new(name);mat.use_nodes=True;mat.use_backface_culling=True
        bs=mat.node_tree.nodes.get('Principled BSDF')
        bs.inputs['Base Color'].default_value=tuple(art.metric.linear_channel(int(color[i:i+2],16)) for i in (0,2,4))+(1.,)
        bs.inputs['Roughness'].default_value=.65 if name=='rubber' else .27
        bs.inputs['Metallic'].default_value=.7 if name=='silver' else 0
        mats[name]=mat
    def finish(m):
        obj,_=m.finish(mats,art.shading.ANALYTIC,metric_uv=False)
        # Exporter maps Blender Y to glTF -Z. Negate canonical X/Z now so
        # this static package uses canonical basis zero, like the building kit.
        normals=[n.vector.copy() for n in obj.data.corner_normals]
        for v in obj.data.vertices:v.co=Vector((-v.co.x,-v.co.y,v.co.z))*S
        obj.data.normals_split_custom_set([Vector((-n.x,-n.y,n.z)) for n in normals])
        return obj
    def tube(m,name,a,b,r,c):m.cord(name,[a,b],r,c)
    m=HumanModel('00_frame',0)
    m.box('seat',(0,.79,.22),(.19,.05,.37),'black',.023)
    m.box('rear_red_body',(0,.69,.30),(.23,.055,.42),'red',.035)
    m.ellipsoid('rounded_fuel_tank',(0,.77,-.21),(.24,.16,.25),'red')
    m.box('engine',(0,.43,-.12),(.14,.17,.17),'silver',.025)
    for y in (.36,.42,.48,.54):m.box('engine_cooling_fin',(0,y,-.12),(.19,.012,.19),'silver',.008)
    m.cylinder('crankcase',(.16,.36,.02),.13,.055,0,'silver')
    for side in (-1,1):
        tube(m,'frame', (side*.16,.25,.54),(side*.16,.29,-.45),.036,'black')
        tube(m,'frame', (side*.16,.29,-.45),(side*.16,.87,-.35),.035,'black')
        tube(m,'rear_spring',(side*.18,.31,.55),(side*.18,.67,.36),.055,'silver')
        tube(m,'swingarm',(side*.18,.28,.63),(side*.18,.37,.06),.035,'black')
    m.cylinder('exhaust',(.26,.33,.31),.073,.62,2,'silver')
    m.cylinder('exhaust_tip',(.26,.33,.635),.050,.032,2,'black')
    m.cylinder('footpeg',(0,.48,-.18),.028,.68,0,'black')
    m.box('tail_light',(0,.72,.74),(.13,.045,.018),'tail',.013)
    # Frame stud and red rear mudguard.
    m.cylinder('tank_stud',(0,.927,-.19),.055,.03,1,'red')
    finish(m)
    m=HumanModel('01_steering',0)
    for side in (-1,1):
        tube(m,'fork',(side*.14,.28,-.63),(side*.14,1.02,-.28),.043,'silver')
        tube(m,'fork_sleeve',(side*.14,.72,-.42),(side*.14,1.02,-.28),.065,'red')
        tube(m,'bar_riser',(side*.15,.94,-.28),(side*.22,1.089,-.234),.035,'black')
    tube(m,'handlebar',(-.462,1.089,-.234),(.462,1.089,-.234),.035,'black')
    for side in (-1,1):m.cylinder('rubber_grip',(side*.405,1.089,-.234),.049,.20,0,'rubber')
    m.cylinder('headlight_rim',(0,.94,-.45),.139,.10,2,'silver')
    m.cylinder('round_headlight',(0,.94,-.508),.115,.024,2,'lens')
    # Curved solid mudguard, clear of the tire at full rotation.
    n=20;pts=[]
    for x in (-.18,.18):
        for radius in (.315,.35):
            for i in range(n+1):
                a=math.radians(28+124*i/n);pts.append((x,.28+radius*math.sin(a),-.63+radius*math.cos(a)))
    k=n+1;faces=[]
    for i in range(n):
        faces.extend([(i,i+1,k+i+1,k+i),(2*k+i,3*k+i,3*k+i+1,2*k+i+1),
                      (i,2*k+i,2*k+i+1,i+1),(k+i,k+i+1,3*k+i+1,3*k+i)])
    faces.extend([(0,k,3*k,2*k),(n,2*k+n,3*k+n,k+n)])
    m.mesh('front_red_mudguard',pts,faces,'red');finish(m)
    for name,z in [('02_rear_wheel',.63),('03_front_wheel',-.63)]:
        m=HumanModel(name,0);pts=[];n=40;cross=12
        for i in range(n):
            a=i*2*math.pi/n
            for j in range(cross):
                b=j*2*math.pi/cross;r=.221+.059*math.cos(b)
                pts.append((.079*math.sin(b),.28+r*math.cos(a),z+r*math.sin(a)))
        faces=[(i*cross+j,((i+1)%n)*cross+j,((i+1)%n)*cross+(j+1)%cross,i*cross+(j+1)%cross) for i in range(n) for j in range(cross)]
        m.mesh('rounded_black_tire',pts,faces,'rubber')
        m.cylinder('wheel_rim',(0,.28,z),.18,.095,0,'silver')
        m.cylinder('dark_hub_recess',(0,.28,z),.143,.099,0,'black')
        m.cylinder('axle',(0,.28,z),.054,.145,0,'silver')
        for i in range(8):
            a=i*2*math.pi/8
            tube(m,'spoke',(0,.28+.04*math.cos(a),z+.04*math.sin(a)),(0,.28+.17*math.cos(a),z+.17*math.sin(a)),.022,'silver')
        for i in range(24):
            a=i*2*math.pi/24
            # Individual molded tread blocks on both sides of the crown.
            for side in (-1,1):
                pts=[]
                for x in (side*.045-.025,side*.045+.025):
                    for y in (-.018,.018):
                        for zz in (-.026,.026):
                            pts.append((x,.28+(.274+y)*math.cos(a)-zz*math.sin(a),z+(.274+y)*math.sin(a)+zz*math.cos(a)))
                m.mesh('tire_tread',pts,[(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)],'rubber',False)
        finish(m)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str((out/'motorbike.blend').resolve()),compress=True)
    bpy.ops.export_scene.gltf(filepath=str((out/'motorbike.glb').resolve()),export_format='GLB',use_selection=True,export_yup=True,export_apply=False,export_animations=False,export_skins=False,export_morph=False,export_texcoords=False,export_tangents=False,export_cameras=False,export_lights=False,export_vertex_color='NONE')
    sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest()
    (out/'provenance.json').write_text(json.dumps(dict(asset='original-red-builder-motorbike-r01',author_sha256=sha(__file__),helper_sha256=sha(Path(__file__).with_name('author_human_adventurer.py')),scale=S,wheel_radius=.28*S,wheelbase=1.26*S,mesh_order=['frame','steering','rear_wheel','front_wheel'],glb_sha256=sha(out/'motorbike.glb'),blend_sha256=sha(out/'motorbike.blend')),indent=2)+'\n')
if __name__=='__main__':main()
