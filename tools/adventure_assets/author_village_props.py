#!/usr/bin/env python3
"""Original warm brick village props, authored as editable canonical Blender meshes."""
import argparse,hashlib,json,math,struct,sys
from pathlib import Path
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from author_building_kit import Model,linear,sha

PALETTE={'wood':'A5784F','bark':'705038','clay':'B76343','cream':'E8D8B8',
         'leaf':'627B45','light_leaf':'8DA557','soil':'4B3A29','flower':'EAC369'}
SIZES=((4.6,1.28,4.6),(.9,.7,.9),(2.6,4.2,2.6),(.64,.08,.64))
NAMES=('roof','planter','tree','path_tile')

def mats():
    result={}
    for name,color in PALETTE.items():
        mat=bpy.data.materials.new('village_'+name);mat.use_nodes=True;mat.use_backface_culling=True
        node=mat.node_tree.nodes.get('Principled BSDF')
        node.inputs['Base Color'].default_value=tuple(linear(int(color[i:i+2],16)) for i in (0,2,4))+(1.,)
        node.inputs['Roughness'].default_value=.70;node.inputs['Metallic'].default_value=0
        result[name]=mat
    return result

def build(kind,lod,materials):
    m=Model(f'{kind:02d}_{NAMES[kind]}',lod,materials)
    if kind==0:
        # Eight physically solid stepped brick courses. The exact same eight
        # boxes define village roof collision; no invisible sloped room volume.
        for step in range(4):
            h=.32*(4-step)
            for side in (-1,1):
                xa,xb=sorted((side*step*.575,side*(step+1)*.575))
                m.box('Roof course',(xa,0,-2.3),(xb,h,2.3),'clay',.008)
                # Shallow inset seams communicate individual tiles without
                # replacing support geometry or exceeding the collider top.
                for segment in range(8):
                    z=-2.3+segment*.575
                    m.box('Tile face',(xa+.014,h-.012,z+.012),(xb-.014,h+.003,z+.561),'clay',0)
        m.box('Ridge cap',(-.12,1.266,-2.29),(.12,1.283,2.29),'clay',0)
    elif kind==1:
        m.box('Brick pot',(-.45,0,-.45),(.45,.50,.45),'clay',.018)
        m.box('Soil',(-.37,.497,-.37),(.37,.503,.37),'soil',0)
        for x,z in ((-.22,-.18),(.18,-.16),(0,.18)):
            m.box('Stem',(x-.025,.50,z-.025),(x+.025,.66,z+.025),'leaf',0)
            m.box('Leaf',(x-.15,.555,z-.075),(x+.14,.58,z+.075),'light_leaf',.005)
            m.box('Flower',(x-.08,.64,z-.08),(x+.08,.70,z+.08),'flower',.005)
        for y in (.13,.30):
            m.box('Pot course seam',(-.45,y,-.451),(.45,y+.01,-.447),'cream',0)
    elif kind==2:
        m.box('Trunk',(-.22,0,-.22),(.22,2.40,.22),'bark',.012)
        # A generous toy-block crown matches its bounded physical envelope.
        m.box('Leaf crown',(-1.3,2.4,-1.3),(1.3,4.2,1.3),'leaf',.025)
        for x in (-.78,0,.78):
            for z in (-.78,0,.78):
                # Inset flat LEGO leaf plates: no additional collider height.
                m.box('Leaf plate',(x-.31,4.183,z-.31),(x+.31,4.204,z+.31),'light_leaf',0)
        for x in (-1.301,1.291):
            m.box('Leaf edge',(x,2.8,-.7),(x+.01,3.8,.7),'light_leaf',0)
        m.box('Bark mark',(-.075,.40,-.222),(.075,1.65,-.215),'wood',0)
    else:
        m.box('Path plate',(-.32,0,-.32),(.32,.08,.32),'cream',.009)
        m.box('Inset stone',(-.245,.074,-.245),(.245,.081,.245),'wood',0)
    obj,report=m.finish()
    size=SIZES[kind];lo=report['bounds']['minimum'];hi=report['bounds']['maximum']
    assert all(lo[a]>=(-size[a]/2 if a!=1 else 0)-.01 and hi[a]<=(size[a]/2 if a!=1 else size[a])+.01 for a in range(3))
    return dict(kind=kind,name=NAMES[kind],size=size,**report)

def order(path):
    raw=path.read_bytes();n,tag=struct.unpack_from('<II',raw,12);assert tag==0x4e4f534a
    doc=json.loads(raw[20:20+n]);assert len(doc['meshes'])==len(doc['nodes'])==4
    order=sorted(range(4),key=lambda i:doc['meshes'][i]['name']);mapping={old:new for new,old in enumerate(order)}
    doc['meshes']=[doc['meshes'][i] for i in order]
    for node in doc['nodes']:
        node['mesh']=mapping[node['mesh']]
        assert not any(key in node for key in ('translation','rotation','scale','children'))
    doc['nodes'].sort(key=lambda v:v['mesh']);doc['scenes'][0]['nodes']=list(range(4))
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);tail=raw[20+n:]
    path.write_bytes(struct.pack('<III',0x46546c67,2,20+len(text)+len(tail))+struct.pack('<II',len(text),tag)+text+tail)
    return doc

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output-dir',type=Path,required=True)
    a=parser.parse_args(sys.argv[sys.argv.index('--')+1:]);out=a.output_dir.resolve()
    assert bpy.app.background and '--factory-startup' in sys.argv and not out.exists();out.mkdir()
    records=[]
    for lod in (0,1):
        bpy.ops.wm.read_factory_settings(use_empty=True);materials=mats()
        checks=[build(i,lod,materials) for i in range(4)]
        bpy.ops.object.select_all(action='SELECT');blend=out/f'village-props-lod{lod}.blend';glb=out/f'village-props-lod{lod}.glb'
        bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
        bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,
            export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,
            export_tangents=False,export_animations=False,export_skins=False,export_morph=False,
            export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
        doc=order(glb);prims=[p for m in doc['meshes'] for p in m['primitives']]
        vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in prims)
        triangles=sum(doc['accessors'][p['indices']]['count']//3 for p in prims)
        assert len(prims)<=16 and vertices<=18000 and triangles<=14000
        records.append(dict(lod=lod,blend_sha256=sha(blend),glb_sha256=sha(glb),vertices=vertices,triangles=triangles,draws=len(prims),props=checks))
    (out/'provenance.json').write_text(json.dumps(dict(schema=1,asset_id='voxys-adventure-village-props-r01',
        profile='salvage-rigid-v1',render_to_canonical=0,blender_version=bpy.app.version_string,
        author_sha256=sha(__file__),helper_sha256=sha(Path(__file__).with_name('author_building_kit.py')),
        palette_srgb=PALETTE,lods=records,provenance='Original procedural brick props. Editable Blender/GLB sources; no external images or models. Physical roof courses, trunk/crown, pot and path footprints are paired with VillageLayout. Thin leaf/flower/inset accents are cosmetic.'),indent=2)+'\n')
    print(json.dumps(records),flush=True)
if __name__=='__main__':main()
