#!/usr/bin/env python3
"""Three original warm brick residents derived from the frozen player recipe.

Only role geometry/materials are varied. Player files are never rewritten.
Canonical metres/+Y/-Z, existing basis12 and rigid 22-node/8-clip animation ABI.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
import bmesh
from mathutils import Vector

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0,str(Path(__file__).resolve().parent))
import author_human_adventurer as base

RESIDENTS={
    'moss':dict(id=1,name='Moss',role='Builder',palette=dict(cream='EEE0BF',moss='627345',rust='A85C39',leather='75563A',hair='574230'),
        silhouette='Moss work cap with forward bill; cream shirt with broad green apron, apron pocket and crossed shoulder straps.'),
    'rivet':dict(id=2,name='Rivet',role='Outfitter',palette=dict(cream='E8D8B8',moss='A54E35',rust='C39558',leather='684631',hair='34291F'),
        silhouette='Swept dark molded hair; rust waistcoat with cream collar and roomy pockets; rolled travel blanket strapped to the pack.'),
    'lumen':dict(id=3,name='Lumen',role='Beacon keeper',palette=dict(cream='F0E3C7',moss='657E78',rust='B47843',leather='7B6045',hair='B69C68'),
        silhouette='Brimmed cream keeper hat with warm band; blue-moss coat with cream facing and printed brass buttons.'),
}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def player_fingerprint():return {str(p.relative_to(ROOT)):sha(p) for p in sorted((ROOT/'data/adventure/human-r01').rglob('*')) if p.is_file()}

def dome(m,name,color,rx,rz,lower_front,lower_back,top,lod,shift=0):
    n=(28,20,14)[lod];rings=(7,6,5)[lod];points=[]
    for ring in range(rings):
        t=ring/rings
        for i in range(n):
            a=i*2*math.pi/n;front=max(0,-math.sin(a));low=lower_back+(lower_front-lower_back)*front
            points.append((rx*math.cos(t*math.pi/2)*math.cos(a)+shift*math.sin(math.pi*t),
                low+(top-low)*math.sin(t*math.pi/2),rz*math.cos(t*math.pi/2)*math.sin(a)))
    points.append((0,top,0));pole=len(points)-1
    faces=[tuple(reversed(range(n)))]
    faces.extend((r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i) for r in range(rings-1) for i in range(n))
    faces.extend(((rings-1)*n+i,(rings-1)*n+(i+1)%n,pole) for i in range(n))
    return m.mesh(name,points,faces,color)

def patch(m,name,xy,color,z):
    n=len(xy);points=[(x,y,z+d) for d in (-.0005,.0005) for x,y in xy]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
    return m.mesh(name,points,faces,color,False)

def remove_material_geometry(obj,region):
    ids={i for i,m in enumerate(obj.data.materials) if m and f'_{region}_lod' in m.name}
    bm=bmesh.new();bm.from_mesh(obj.data)
    selected=[face for face in bm.faces if face.material_index in ids]
    assert selected, 'expected original material region'
    bmesh.ops.delete(bm,geom=selected,context='FACES')
    bm.to_mesh(obj.data);bm.free();obj.data.update()

def attach(nodes,target,model,mats):
    added,check=model.finish(mats,base.art.shading.FLAT,metric_uv=False)
    original=nodes[target]
    bpy.context.view_layer.update();matrix=original.matrix_world.copy();inverse=matrix.inverted()
    for vertex in added.data.vertices:vertex.co=inverse@vertex.co
    added.matrix_world=matrix
    base.art.geo.activate(original);added.select_set(True);bpy.ops.object.join()
    original.name=target
    return check

def build(key,lod):
    nodes,_=base.build(lod)
    mats={key:bpy.data.materials[f'human_{key}_lod{lod}'] for key in base.PALETTE}
    palette={**base.PALETTE,**RESIDENTS[key]['palette']}
    for region,m in mats.items():
        color=palette[region];shader=m.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value=tuple(base.art.metric.linear_channel(int(color[i:i+2],16)) for i in (0,2,4))+(1.,)
        # Names retain the region marker for explicit authoring operations.
        m.name=f'resident_{key}_{region}_lod{lod}'
    remove_material_geometry(nodes['robot_head'],'hair')
    head=base.HumanModel('resident_headwear',lod)
    if key=='moss':
        dome(head,'molded_work_cap','moss',.183,.177,1.558,1.513,1.687,lod)
        head.loft('forward_cap_bill',[(1.546,0,-.035,.168,.175),(1.558,0,-.035,.173,.177),(1.565,0,-.029,.159,.165)],'moss')
        head.ellipsoid('cap_crown_button',(0,1.687,0),(.013,.013,.013),'moss',detail=.75)
        head.loft('cap_lower_band',[(1.535,0,0,.180,.172),(1.550,0,0,.180,.172)],'leather')
    elif key=='rivet':
        dome(head,'dark_side_swept_hair','hair',.186,.180,1.564,1.494,1.7,lod,shift=.010)
        head.loft('broad_molded_quiff',[(1.555,-.090,-.117,.006,.009),(1.58,-.073,-.148,.023,.016),
            (1.627,-.015,-.129,.047,.025),(1.654,.050,-.081,.044,.021),(1.678,.074,-.017,.016,.018)],'hair')
    else:
        head.loft('broad_keeper_hat_brim',[(1.541,0,0,.206,.187),(1.552,0,0,.217,.194),(1.563,0,0,.208,.187)],'cream')
        dome(head,'keeper_hat_crown','cream',.149,.143,1.553,1.553,1.7,lod)
        head.loft('warm_hat_band',[(1.554,0,0,.150,.144),(1.578,0,0,.148,.142)],'rust')
    attach(nodes,'robot_head',head,mats)
    torso=base.HumanModel('resident_role_clothing',lod)
    # Overlay front regions are thin authored prints; layer separation avoids
    # the coplanar shirt/lapel intersection found in the earlier player proof.
    body=[(-.213,.668),(.213,.668),(.179,1.164),(-.179,1.164)]
    if key=='moss':
        patch(torso,'cream_work_shirt_print',body,'cream',-.132)
        patch(torso,'moss_builder_apron',[(-.194,.673),(.194,.673),(.179,.855),(.113,1.081),(-.113,1.081),(-.179,.855)],'moss',-.134)
        for sign in (-1,1):
            patch(torso,'apron_shoulder_strap',[(sign*.078,1.02),(sign*.098,1.02),(sign*.143,1.159),(sign*.122,1.159)],'moss',-.136)
        torso.box('wide_apron_pocket',(0,.783,-.138),(.104,.047,.002),'leather',0)
        torso.box('pocket_seam',(0,.824,-.141),(.096,.003,.0007),'cream',0)
        # Builder roll at the waist reads as clothing, not a usable world station.
    elif key=='rivet':
        patch(torso,'rust_outfitter_vest',body,'moss',-.132)
        patch(torso,'cream_vest_collar',[(-.074,1.16),(.074,1.16),(.021,1.037),(-.021,1.037)],'cream',-.134)
        patch(torso,'vest_placket',[(-.014,.682),(.014,.682),(.014,1.062),(-.014,1.062)],'leather',-.1355)
        for sign in (-1,1):
            torso.box('outfitter_pocket',(sign*.121,.792,-.137),(.052,.060,.002),'leather',0)
            torso.box('pocket_flap',(sign*.121,.843,-.140),(.047,.012,.001),'rust',0)
        torso.cylinder('rolled_travel_blanket',(0,1.049,.181),.046,.270,0,'cream')
        for x in (-.088,.088):torso.cylinder('blanket_leather_band',(x,1.049,.181),.048,.014,0,'leather')
    else:
        patch(torso,'keeper_coat_front',body,'moss',-.132)
        patch(torso,'cream_coat_facing',[(-.074,.677),(.074,.677),(.071,1.16),(-.071,1.16)],'cream',-.134)
        for sign in (-1,1):
            patch(torso,'coat_lapel',[(sign*.047,.979),(sign*.064,1.146),(sign*.117,1.16),(sign*.088,1.033)],'rust',-.136)
        for y in (.732,.802,.872,.942):torso.box('printed_coat_button',(0,y,-.136),(.007,.007,.0007),'brass',0)
        torso.box('keeper_coat_hem',(0,.668,-.134),(.208,.012,.001),'cream',0)
    attach(nodes,'robot_torso',torso,mats)
    checks=[]
    for name,obj in nodes.items():
        if obj.type!='MESH':continue
        bm=bmesh.new();bm.from_mesh(obj.data)
        degenerate=sum(f.calc_area()<=1e-12 for f in bm.faces);open_edges=sum(not e.is_manifold for e in bm.edges);volume=bm.calc_volume(signed=True)
        bm.free()
        assert degenerate==open_edges==0 and volume>0,(name,degenerate,open_edges,volume)
        checks.append(dict(node=name,degenerate_faces=degenerate,nonmanifold_edges=open_edges,summed_component_volume_m3=volume))
    return nodes,checks,palette

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True)
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    if not bpy.app.background or '--factory-startup' not in sys.argv:p.error('background factory startup required')
    out=a.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():p.error('new output directory required')
    frozen=player_fingerprint();out.mkdir()
    for key,spec in RESIDENTS.items():
        resident=out/key;source=resident/'source';source.mkdir(parents=True);records=[]
        for lod in range(3):
            bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.preferences.filepaths.file_preview_type='NONE'
            scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.render.fps=30;scene.frame_start=0;scene.frame_end=60
            nodes,checks,palette=build(key,lod);base.add_clips(nodes);bpy.ops.object.select_all(action='SELECT')
            path=source/f'character-lod-{lod}.glb'
            bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,export_yup=True,export_apply=False,
                export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=True,
                export_animation_mode='NLA_TRACKS',export_frame_range=False,export_frame_step=1,export_force_sampling=True,
                export_optimize_animation_size=False,export_optimize_animation_keep_anim_object=True,export_skins=False,
                export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
            doc,dropped=base.compatible.prune_export(path);primitives=[p for mesh in doc['meshes'] for p in mesh['primitives']]
            vertices=sum(doc['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)
            triangles=sum(doc['accessors'][p['indices']]['count']//3 for p in primitives)
            assert len(doc['nodes'])==22 and len(doc['meshes'])==15 and len(primitives)<=48
            assert {a['name'] for a in doc['animations']}==set(base.CLIPS)
            assert vertices<=(10500,7500,4500)[lod]
            blend=source/f'character-lod-{lod}.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
            records.append(dict(lod=lod,glb=path.name,glb_sha256=sha(path),blend=blend.name,blend_sha256=sha(blend),
                nodes=len(doc['nodes']),meshes=len(doc['meshes']),draws=len(primitives),vertices=vertices,triangles=triangles,
                clips=[dict(name=a['name'],channels=len(a['channels'])) for a in doc['animations']],removed_constant_rest_channels=dropped,geometry_checks=checks))
            print('RESIDENT_LOD_COMPLETE',key,lod,vertices,triangles,len(primitives),flush=True)
        provenance=dict(schema=1,asset_id=f'voxys-adventure-resident-{key}-r01',resident_key=key,resident_id=spec['id'],name=spec['name'],role=spec['role'],
            author='Voxys original procedural artwork',license='Original project artwork; same project distribution terms. No external character/texture inputs or logos.',
            profile='salvage-animated-rigid-v1',render_to_canonical=12,blender=bpy.app.version_string,
            canonical=dict(up='+Y',forward='-Z',units='metres',height=1.7,root_motion=False),
            clips=list(base.CLIPS),anchors=base.ANCHORS,palette=palette,silhouette=spec['silhouette'],lods=records,
            base_player_files=frozen,base_recipe_inputs=json.loads((ROOT/'data/adventure/human-r01/provenance.json').read_text())['inputs'],
            author_sha256=sha(Path(__file__)),base_author_sha256=sha(Path(base.__file__)),
            reproduction='/snap/blender/current/blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/adventure_assets/author_resident_minifigures.py -- --output-dir <new-directory>',
            scope='Three distinct original rigid toy-person appearances; palette and role geometry are baked per resident. No global tints, new colliders, usable furniture, NPC facts, quests or save edits. Not skinning or cloth simulation.')
        (resident/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    assert frozen==player_fingerprint(),'frozen player package changed during authoring'
    print('RESIDENT_AUTHORING_COMPLETE',out,flush=True)

if __name__=='__main__':main()
