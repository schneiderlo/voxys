#!/usr/bin/env python3
"""Six original forest silhouettes, with attributed LDraw 2417 leaf detail.

Blender background recipe. The four LODs retain similar crown envelopes;
studs, leaf ornaments and trunk seams are removed with distance. Stud pitch and
trunk dimensions stay physical: no random runtime scaling.
"""
import argparse, json, math, os, struct, sys
from pathlib import Path
import bpy, bmesh
from mathutils import Matrix, Vector
sys.path.insert(0, str(Path(__file__).resolve().parent))
from author_building_kit import Model, activate, linear, sha
from author_creative_props import Toy, ring_shell

NAMES = ('oak', 'beech', 'young_oak', 'birch', 'spruce', 'young_fir')
PALETTE = {'bark':'69442F', 'birch':'D6D0B4', 'leaf':'44794B',
           'light':'679653', 'shade':'345E42', 'needle':'2E6250'}
# Lower crown, height, width, depth; authored dimensions in studs.
SIZES = ((7.2,15.4,6.1,5.5), (8.4,18.5,5.5,5.1), (5.8,11.6,4.25,3.8),
         (9.2,19.7,4.25,3.8), (5.8,22.0,4.3,4.1), (4.8,15.5,3.5,3.3))

def foliage(library):
    """Expand only face records; source files and dependency hashes are pinned."""
    cache = {}
    def read(name, stack=()):
        if name in cache: return cache[name]
        assert name not in stack and len(stack)<16
        path=next((library/folder/name for folder in ('parts','p') if (library/folder/name).is_file()),None)
        assert path, name
        points=[];faces=[]
        for line in path.read_text().splitlines():
            row=line.split()
            if not row:continue
            if row[0]=='1':
                v=list(map(float,row[2:14]));child=' '.join(row[14:]).replace('\\','/')
                matrix=Matrix((v[3:6],v[6:9],v[9:12]));offset=Vector(v[:3])
                cp,cf=read(child,(*stack,name));start=len(points)
                points.extend(matrix@p+offset for p in cp)
                faces.extend(tuple(start+i for i in f) for f in cf)
            elif row[0] in ('3','4'):
                v=list(map(float,row[2:]));start=len(points)
                points.extend(Vector(v[i:i+3]) for i in range(0,len(v),3))
                faces.append(tuple(range(start,len(points))))
        cache[name]=points,faces
        return points,faces
    points,faces=read('2417.dat')
    return [Vector((p.x/20,-p.y/20,-p.z/20)) for p in points],faces

def leaf_detail(m, leaf, center, yaw, tilt, color):
    points,faces=leaf
    matrix=Matrix.Rotation(yaw,3,'Y')@Matrix.Rotation(tilt,3,'X')
    ob=Model.mesh(m,'LDraw 2417 / James Jessiman',
                  [tuple(matrix@p+Vector(center)) for p in points],faces,color)
    bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00002)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(ob.data);bm.free();ob.data.update()

def distant_shell(m,name,x,y,z,rx,rz,rings,color):
    # At the horizon a six-sided ring holds the same envelope with fewer
    # vertices than the near eight-sided molded crown.
    outline=((1,0),(.5,-1),(-.5,-1),
             (-1,0),(-.5,1),(.5,1))
    points=[(x+dx*rx*scale,y+height,z+dz*rz*scale)
            for height,scale in rings for dx,dz in outline]
    n=len(outline)
    faces=[tuple(range(n-1,-1,-1)),
           tuple(range((len(rings)-1)*n,len(rings)*n))]
    faces.extend((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i)
                 for j in range(len(rings)-1) for i in range(n))
    m.mesh(name,points,faces,color,0)

def build(kind,lod,materials,leaf):
    m=Toy(f'{kind:02d}_{NAMES[kind]}',lod,materials)
    bottom,height,width,depth=SIZES[kind]
    radius=.38 if kind==3 else .40 if kind in (2,5) else .57
    bark='birch' if kind==3 else 'bark'
    trunk_top=height-2.4 if kind>=4 else bottom+2.0
    # Beyond ~700 units the entire tree is only a few pixels wide. Keep its
    # height and crown width, while removing overlapping interior lobes.
    if lod==3:
        distant_shell(m,'Distant trunk',0,0,0,radius,radius,
                      ((0,1.25),(trunk_top,.66)),bark)
        if kind>=4:
            for i in range(2):
                t=i/2;y=bottom+(height-bottom-2.1)*t
                r=width*(1-t*.88);d=depth*(1-t*.88)
                h=height-y if i==1 else (height-y)*.55
                distant_shell(m,'Distant needle bough',0,y,0,r,d,
                              ((0,1),(.28*h,.84),(h,.13)),'needle' if i==0 else 'shade')
        else:
            distant_shell(m,'Distant crown',0,bottom,0,width,depth,
                          ((0,.58),(.30*(height-bottom),.92),
                           (.65*(height-bottom),1),(height-bottom,.22)),
                          'leaf')
    else:
        # The nearer levels share tapered roots and branching detail.
        ring_shell(m,'Tapered trunk',0,0,0,radius,radius,
                   ((0,1.25),(bottom*.65,1),(trunk_top,.66)),bark,.022)
    if lod!=3 and kind>=4:
        levels=7 if kind==4 else 5
        for i in range(levels):
            t=i/(levels-1);y=bottom+(height-bottom-2.1)*t
            r=width*(1-t*.88);d=depth*(1-t*.88)
            h=(height-y)*.40 if i<levels-1 else height-y
            ring_shell(m,'Layered needle bough',.2*math.sin(i*2),y,.15*math.cos(i*3),r,d,
                       ((0,.68),(.22*h,1),(h,.16)),
                       'needle' if i%3!=1 else 'shade',.025 if lod<2 else 0)
            if lod==0:m.stud(.2*math.sin(i*2),y+h,.15*math.cos(i*3),'needle')
    elif lod!=3:
        # Each lobe is a rounded molded volume, not a stack of broad shelves.
        # Shared ring profiles preserve silhouettes across the three LODs.
        crown_height=height-bottom
        lobes=((-.44,.03,-.14,.54,.55,.66),(.39,.10,.12,.58,.58,.70),
               (-.12,.17,.43,.58,.52,.65),(.08,.22,-.43,.59,.53,.63),
               (-.32,.37,-.18,.51,.54,.59),(.26,.43,.12,.51,.54,.54),
               (-.03,.61,-.04,.43,.44,.39))
        for i,(x,y,z,rx,rz,h) in enumerate(lobes):
            # Distinct asymmetric crowns, including a narrow, taller birch.
            cx=x*width+(kind%2)*.28*math.sin(i*2)
            cz=z*depth;cy=bottom+y*crown_height;ch=h*crown_height
            color='light' if i==6 and kind!=1 else 'shade' if i==0 else 'leaf'
            ring_shell(m,'Rounded crown lobe',cx,cy,cz,rx*width,rz*depth,
                       ((0,.44),(.30*ch,.91),(.64*ch,1),(ch,.36)),color,.045 if lod==0 else 0)
            if i<4 and lod<2:
                m.cylinder('Branch',(0,bottom-1.2,0),(cx,cy+ch*.38,cz),radius*.40,bark,8)
            if lod==0:
                # The full-sized molded leaf lies across the crown shoulder.
                # Most of its footprint overlaps the lobe, keeping LOD changes small.
                if i in (1,3,6):
                    leaf_detail(m,leaf,(cx,cy+ch*.74,cz),i*2.399+kind*.7,.12*math.sin(i),color)
                m.stud(cx,cy+ch,cz,color)
        if kind==3 and lod<2:
            for i in range(7):
                y=.9+i*1.08
                m.box('Birch bark mark',(-.28,y,-radius-.012),(.17,y+.10,-radius+.008),'bark',0)
    # Joining first retains material batching. LDraw decorative surfaces need
    # not satisfy a collision mesh manifold contract; trunks are explicit boxes.
    activate(m.objects[0])
    for ob in m.objects:ob.select_set(True)
    bpy.ops.object.join();ob=bpy.context.object;ob.name=m.name;ob.data.name=m.name
    mod=ob.modifiers.new('Export triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
    bm=bmesh.new();bm.from_mesh(ob.data)
    degenerate=[f for f in bm.faces if f.calc_area()<1e-10]
    if degenerate:bmesh.ops.delete(bm,geom=degenerate,context='FACES')
    bm.to_mesh(ob.data);bm.free();ob.data.update()
    points=[(v.co.x,v.co.z,-v.co.y) for v in ob.data.vertices]
    lo=[min(p[i] for p in points) for i in range(3)];hi=[max(p[i] for p in points) for i in range(3)]
    return {'name':NAMES[kind],'bounds':{'minimum':lo,'maximum':hi},
            'trunk':{'minimum':[-radius,0,-radius],'maximum':[radius,trunk_top,radius]},
            'triangles':len(ob.data.polygons)}

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--lod',type=int,choices=range(4),help='Regenerate only this detail level')
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    assert bpy.app.background and '--factory-startup' in sys.argv
    out=a.output.resolve();source=out/'source'
    manifest=json.loads((source/'ldraw-manifest.json').read_text())
    for entry in manifest['dependencies']:assert sha(source/'ldraw'/entry['path'])==entry['sha256']
    leaf=foliage(source/'ldraw');reports=[]
    for lod in ([a.lod] if a.lod is not None else range(4)):
        bpy.ops.wm.read_factory_settings(use_empty=True);materials={}
        for name,color in PALETTE.items():
            mat=bpy.data.materials.new('forest_'+name);mat.use_nodes=True;mat.use_backface_culling=True
            bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=tuple(linear(int(color[i:i+2],16)) for i in (0,2,4))+(1.,)
            bs.inputs['Roughness'].default_value=.72;materials[name]=mat
        variants=[build(i,lod,materials,leaf) for i in range(len(NAMES))]
        bpy.ops.object.select_all(action='SELECT')
        blend=source/f'forest-lod{lod}.blend';glb=source/f'forest-lod{lod}.glb'
        bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
        bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE')
        raw=glb.read_bytes();n,tag=struct.unpack_from('<II',raw,12);doc=json.loads(raw[20:20+n])
        assert len(doc['nodes'])==len(doc['meshes'])==len(NAMES)
        order=sorted(range(len(NAMES)),key=lambda i:doc['meshes'][i]['name']);mapping={old:new for new,old in enumerate(order)}
        doc['meshes']=[doc['meshes'][i] for i in order]
        for node in doc['nodes']:
            node['mesh']=mapping[node['mesh']];assert not any(k in node for k in ('translation','rotation','scale','children','matrix'))
        doc['nodes'].sort(key=lambda n:n['mesh']);doc['scenes'][0]['nodes']=list(range(len(NAMES)))
        text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);tail=raw[20+n:]
        glb.write_bytes(struct.pack('<III',0x46546c67,2,20+len(text)+len(tail))+struct.pack('<II',len(text),tag)+text+tail)
        reports.append({'lod':lod,'variants':variants,'glb_sha256':sha(glb),'blend_sha256':sha(blend)})
    if a.lod is not None:
        previous=json.loads((out/'provenance.json').read_text())['lods']
        reports=sorted((entry for entry in previous if entry['lod']!=a.lod),key=lambda entry:entry['lod'])+reports
        reports.sort(key=lambda entry:entry['lod'])
    (out/'provenance.json').write_text(json.dumps({'asset_id':'voxys-forest-r02','stud_pitch':1,'render_to_canonical':0,'author_sha256':sha(__file__),'helper_sha256':sha(Path(__file__).with_name('author_creative_props.py')),'ldraw_source':'source/ldraw-manifest.json','attribution':'source/ATTRIBUTION.md','lods':reports},indent=2)+'\n')
    print('FOREST_AUTHOR_COMPLETE',flush=True);os._exit(0)
if __name__=='__main__':main()
