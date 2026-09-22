#!/usr/bin/env python3
"""Original molded scenery kit. Blender background; +Y up, stud-sized geometry.
No external models/textures. All six meshes have an identity root at ground level.
"""
import argparse,json,math,struct,sys
from pathlib import Path
import bpy
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from author_building_kit import Model,linear,sha,activate
PALETTE={'bark':'71462E','wood':'B87842','leaf':'438B51','lime':'80AF55',
         'pine':'296954','stone':'82999C','cream':'FFE7A8','coral':'F17B61'}
NAMES=('broadleaf','pine','flowers','rocks','bench','crate')

def mats():
    out={}
    for name,color in PALETTE.items():
        m=bpy.data.materials.new('creative_'+name);m.use_nodes=True;m.use_backface_culling=True
        p=m.node_tree.nodes.get('Principled BSDF')
        p.inputs['Base Color'].default_value=tuple(linear(int(color[i:i+2],16)) for i in (0,2,4))+(1.,)
        p.inputs['Roughness'].default_value=.66 if name!='stone' else .76
        out[name]=m
    return out

class Toy(Model):
    def finish(self):
        obj,report=super().finish()
        # Join/triangulate can recalculate weighted normals. Restore exact
        # planar normals on the final export topology, not just components.
        mesh=obj.data;mesh.update();normals=[n.vector.copy() for n in mesh.corner_normals]
        for face in mesh.polygons:
            if max(abs(c) for c in face.normal)>.9999:
                for index in face.loop_indices:normals[index]=face.normal.copy()
        mesh.normals_split_custom_set(normals)
        return obj,report
    def mesh(self,*args,**kwargs):
        if self.lod>=2:
            values=list(args)
            if len(values)>4:values[4]=0
            else:kwargs['bevel']=0
            return super().mesh(*values,**kwargs)
        obj=super().mesh(*args,**kwargs)
        # Bevel faces blend around molded edges while large panels remain flat.
        for p in obj.data.polygons:p.use_smooth=True
        activate(obj);mod=obj.modifiers.new('Molded weighted normals','WEIGHTED_NORMAL');mod.keep_sharp=True
        bpy.ops.object.modifier_apply(modifier=mod.name)
        # Flat panels and cylinder caps need exact plane normals. Weighted
        # smoothing alone bends cap normals by ~25 degrees and looks melted.
        mesh=obj.data;mesh.update();normals=[n.vector.copy() for n in mesh.corner_normals]
        for face in mesh.polygons:
            if max(abs(c) for c in face.normal)>.9999:
                for index in face.loop_indices:normals[index]=face.normal.copy()
        mesh.normals_split_custom_set(normals)
        return obj
    def stud(self,x,y,z,color):
        if not self.lod:return super().stud(x,y,z,color)
    def cylinder(self,name,a,b,r,color,n=12):
        a,b=Vector(a),Vector(b);d=(b-a).normalized();u=d.cross(Vector((0,0,1)))
        if u.length<.01:u=d.cross(Vector((1,0,0)))
        u.normalize();v=d.cross(u)
        pts=[tuple(c+r*(u*math.cos(i*math.tau/n)+v*math.sin(i*math.tau/n))) for c in (a,b) for i in range(n)]
        faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        self.mesh(name,pts,faces,color,.018)
    def plate(self,x,y,z,w,d,color):
        self.box('Leaf plate',(x-w/2+.035,y,z-d/2+.035),(x+w/2-.035,y+.32,z+d/2-.035),color,.055)
        # Exposed studs carry scale; all use the terrain's .30 radius/.18 height.
        for ix in range(w):
            for iz in range(d):self.stud(x-w/2+.5+ix,y+.32,z-d/2+.5+iz,color)
    def leaf_cluster(self,x,y,z,color):
        # Cross silhouette, separated lobes, gaps and visible brick courses.
        self.plate(x,y,z,4,2,color)
        self.plate(x,y+.32,z,2,4,color)
        self.plate(x,y+.64,z,2,2,'lime')

def ring_shell(m,name,x,y,z,rx,rz,rings,color,bevel):
    # Chamfered square rings keep the crown recognisably molded and block-built.
    outline=((-1,-.48),(-.48,-1),(.48,-1),(1,-.48),
             (1,.48),(.48,1),(-.48,1),(-1,.48))
    pts=[(x+dx*rx*s,y+height,z+dz*rz*s)
         for height,s in rings for dx,dz in outline]
    n=len(outline);faces=[tuple(range(n-1,-1,-1)),
                         tuple(range((len(rings)-1)*n,len(rings)*n))]
    faces += [(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i)
              for j in range(len(rings)-1) for i in range(n)]
    m.mesh(name,pts,faces,color,bevel)

def crown(m,name,x,y,z,rx,rz,h,color):
    ring_shell(m,name,x,y,z,rx,rz,((0,.68),(.4*h,1),(h,.56)) if m.lod>=2 else ((0,.68),(.25*h,1),(.59*h,1),(h,.56)),color,.065)

def pine_course(m,y,r,h,color):
    ring_shell(m,'Molded conifer skirt',0,y,0,r,r,
               ((0,1),(.20,1),(h,max(.23,.34/r))),color,.045)

def build(kind,lod,materials):
    m=Toy(f'{kind:02d}_{NAMES[kind]}',lod,materials)
    if kind==0:
        for i in range(1 if lod>=2 else 6):m.box('Trunk course',(-.48,i*.96,-.48),(.48,5.76 if lod>=2 else (i+1)*.96-.018,.48),'bark',.045)
        # Overlapping molded clusters give the crown a continuous mass from
        # every approach. Slightly offset courses retain the toy silhouette,
        # rather than four thin horizontal shelves on exposed branch poles.
        clusters=((-1.65,5.75,-.20,2.12,1.60,1.65,'leaf'),
                  (1.45,5.90,.65,1.96,1.82,1.72,'leaf'),
                  (-.05,6.12,-1.62,1.92,1.60,1.77,'pine'),
                  (-.80,6.80,1.18,2.16,1.50,1.65,'leaf'),
                  (-1.30,7.06,-1.03,1.86,1.57,1.72,'leaf'),
                  (1.27,7.18,-.79,1.82,1.66,1.65,'leaf'),
                  (.38,7.62,.83,1.94,1.70,1.62,'leaf'),
                  (-.22,8.36,-.48,1.87,1.82,1.55,'lime'))
        for i,(x,y,z,rx,rz,h,color) in enumerate(clusters):
            if i<4 and lod<2:m.cylinder('Crown branch',(0,4.8,0),(x,y+.40,z),.25,'bark')
            crown(m,'Layered broadleaf crown',x,y,z,rx,rz,h,color)
            # Exposed studs sit on the small horizontal crown cap. Near-only
            # geometry disappears at distance without changing the silhouette.
            for dx,dz in ((-.45,-.40),(.45,-.40),(-.45,.40),(.45,.40)):
                m.stud(x+dx,y+h,z+dz,color)
        for x,z in ((-.65,0),(.65,0),(0,-.65),(0,.65)):
            m.box('Root', (x-.32,0,z-.32),(x+.32,.42,z+.32),'bark',.035)
    elif kind==1:
        m.box('Trunk',(-.37,0,-.37),(.37,6.68,.37),'bark',.045)
        # Dense overlapping skirts read as a conifer at village distance.
        # There is no air gap between the trunk and the final crown course.
        for i,(y,r,h) in enumerate(((2.90,1.95,1.76),(4.18,1.58,1.78),
                                    (5.46,1.08,1.48),(6.48,.68,.90))):
            pine_course(m,y,r,h,'pine' if i%2==0 else 'leaf')
        m.stud(0,7.38,0,'leaf')
    elif kind==2:
        for i,(x,y,z) in enumerate(((-.65,.72,-.1),(.15,1.02,.45),(.6,.57,-.4))):
            m.cylinder('Stem',(x,0,z),(x,y,z),.10,'pine',8)
            m.box('Leaf',(x-.3,y*.35,z-.08),(x+.28,y*.35+.10,z+.08),'pine',.02)
            for j in range(5):
                a=j*math.tau/5;m.cylinder('Petal',(x+.2*math.cos(a),y,z+.2*math.sin(a)),(x+.2*math.cos(a),y+.12,z+.2*math.sin(a)),.17,'coral' if i==1 else 'cream',8)
            m.cylinder('Flower heart',(x,y+.10,z),(x,y+.21,z),.12,'cream',10)
    elif kind==3:
        # Sloped molded rock shells with a usable 1x1 stud on the top.
        for x,z,w,h in ((-.6,0,1.8,1.28),(.9,.25,1.2,.64)):
            pts=[(x+dx*w,y,z+dz*w) for y,f in ((0,.5),(h*.72,.43),(h,.30)) for dx,dz in ((-f,-f),(f,-f),(f,f),(-f,f))]
            faces=[(3,2,1,0),(8,9,10,11)]+[(i*4+j,i*4+(j+1)%4,(i+1)*4+(j+1)%4,(i+1)*4+j) for i in range(2) for j in range(4)]
            m.mesh('Sloped rock',pts,faces,'stone',.03);m.stud(x,h,z,'stone')
    elif kind==4:
        for x in (-1.55,1.55):
            m.box('Leg',(x-.22,0,-.6),(x+.22,1.52,.6),'pine',.035)
            m.box('Back upright',(x-.14,1.45,.45),(x+.14,3.15,.7),'pine',.035)
        for z in (-.5,0,.5):m.box('Seat slat',(-2.25,1.5,z-.22),(2.25,1.78,z+.22),'wood',.045)
        for y in (2.15,2.78):m.box('Back slat',(-2.25,y,.46),(2.25,y+.46,.72),'wood',.045)
        for x in (-1.55,1.55):m.stud(x,1.78,.45,'wood')
    else:
        m.box('Crate body',(-.96,0,-.96),(.96,1.6,.96),'wood',.035)
        for x in (-.85,.85):
            m.box('Corner rib',(x-.09,0,-1),(x+.09,1.55,1),'bark',.025)
        for y in (.18,1.12):
            for z in (-1,1):m.box('Crate slat',(-1,y,z-.07),(1,y+.28,z+.07),'bark',.025)
        for x in (-.5,.5):
            for z in (-.5,.5):m.stud(x,1.6,z,'wood')
    obj,report=m.finish();return dict(kind=kind,name=NAMES[kind],**report)

def order(path):
    raw=path.read_bytes();n,tag=struct.unpack_from('<II',raw,12);doc=json.loads(raw[20:20+n])
    assert len(doc['meshes'])==len(doc['nodes'])==6
    order=sorted(range(6),key=lambda i:doc['meshes'][i]['name']);mapping={old:new for new,old in enumerate(order)}
    doc['meshes']=[doc['meshes'][i] for i in order]
    for node in doc['nodes']:
        node['mesh']=mapping[node['mesh']];assert not any(k in node for k in ('translation','rotation','scale','children'))
    doc['nodes'].sort(key=lambda v:v['mesh']);doc['scenes'][0]['nodes']=list(range(6))
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);tail=raw[20+n:]
    path.write_bytes(struct.pack('<III',0x46546c67,2,20+len(text)+len(tail))+struct.pack('<II',len(text),tag)+text+tail)
    return doc

def main():
    p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--lod',type=int,choices=(0,1,2),default=0);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);out=a.output_dir.resolve()
    assert bpy.app.background and '--factory-startup' in sys.argv and not out.exists();out.mkdir()
    bpy.ops.wm.read_factory_settings(use_empty=True);materials=mats();checks=[build(i,a.lod,materials) for i in range(6)]
    bpy.ops.object.select_all(action='SELECT');blend=out/'creative-props.blend';glb=out/'creative-props.glb'
    bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
    bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
    doc=order(glb);prims=[p for m in doc['meshes'] for p in m['primitives']]
    report=dict(schema=1,lod=a.lod,asset_id='voxys-creative-props-r01',render_to_canonical=0,blender_version=bpy.app.version_string,author_sha256=sha(__file__),helper_sha256=sha(Path(__file__).with_name('author_building_kit.py')),blend_sha256=sha(blend),glb_sha256=sha(glb),palette_srgb=PALETTE,props=checks,vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in prims),triangles=sum(doc['accessors'][p['indices']]['count']//3 for p in prims),draws=len(prims),provenance='Original procedural toy geometry, authored for Voxys. No external images or models; editable Blender source. Decorative bench/crate, no interaction or inventory.')
    (out/'provenance.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
if __name__=='__main__':main()
