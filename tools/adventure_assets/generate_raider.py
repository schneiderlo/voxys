#!/usr/bin/env python3
"""Original brick-person woodland raider, reusing the frozen rigid character ABI.

author/proof run inside background Blender; cook/check use ordinary Python.
Every output must be new. Existing player/resident generators and packages are
read-only inputs. The hand-held staff is cosmetic, never a gameplay collider.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
IDENTITY='voxys-adventure-raider-r01'
PALETTE=dict(skin='F1C94F',cream='70794F',moss='514E39',rust='B66038',leather='68442D',hair='654A31',dark='29231F',brass='B89C61')
INPUTS=('tools/adventure_assets/author_human_adventurer.py','tools/adventure_assets/author_resident_minifigures.py',
        'tools/adventure_assets/check_human_adventurer.py','tools/salvage_assets/check_cove_robot.py',
        'tools/salvage_assets/author_functional_kit.py','tools/salvage_assets/author_cove_robot.py')

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def frozen_files():
    return {str(p.relative_to(ROOT)):sha(p) for folder in ('human-r01','residents-r01')
            for p in sorted((ROOT/'data/adventure'/folder).rglob('*')) if p.is_file()}
def require_new(path):
    if path.exists() or path.is_symlink() or not path.parent.is_dir():raise ValueError('New output path with an existing parent required: '+str(path))
def require_blender():
    import bpy
    if not bpy.app.background or '--factory-startup' not in sys.argv:raise ValueError('Background factory-startup Blender required')
    return bpy

def build(lod):
    import bpy
    import bmesh
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    import author_human_adventurer as base
    import author_resident_minifigures as resident
    nodes,_=base.build(lod)
    mats={key:bpy.data.materials[f'human_{key}_lod{lod}'] for key in base.PALETTE}
    for key,material in mats.items():
        material.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=tuple(
            base.art.metric.linear_channel(int(PALETTE[key][i:i+2],16)) for i in (0,2,4))+(1.,)
        material.name=f'raider_{key}_lod{lod}'
    resident.remove_material_geometry(nodes['robot_head'],'hair')
    resident.remove_material_geometry(nodes['robot_head'],'dark')
    head=base.HumanModel('raider_headwear_face',lod)
    # Hold the full radius until above the yellow cylinder (top 1.57616 m).
    # Shrinking directly from the sloping lower edge exposes yellow corners.
    n=(28,20,14)[lod];rings=(7,6,5)[lod];points=[]
    for ring in range(rings):
        t=max(0,ring-1)/(rings-1)
        for i in range(n):
            angle=i*2*math.pi/n;front=max(0,-math.sin(angle))
            low=1.279+(1.566-1.279)*front
            y=low if ring==0 else 1.585+.115*math.sin(t*math.pi/2)
            scale=1 if ring==0 else math.cos(t*math.pi/2)
            x=.187*scale*math.cos(angle);z=.183*scale*math.sin(angle)
            # Crown triangles remain just inside the existing hemispherical
            # capsule cap; the exact shared 1.7 m pole is added separately.
            if y>1.4:y=min(y,1.4+math.sqrt(.3*.3-x*x-z*z)-.0002)
            points.append((x,y,z))
    points.append((0,1.7,0));pole=len(points)-1
    faces=[tuple(reversed(range(n)))]
    faces.extend((r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i) for r in range(rings-1) for i in range(n))
    faces.extend(((rings-1)*n+i,(rings-1)*n+(i+1)%n,pole) for i in range(n))
    head.mesh('molded_woodland_hood',points,faces,'hair')
    # The sloping open-front hood keeps the complete yellow face readable. Its
    # small brown rim follows the hood edge, not a robot visor or organic face.
    def face(name,xy,color='dark'):
        # Narrow closed strips follow the cylinder across the whole print.
        # One long eyebrow/grin chord would pass through the yellow surface.
        def clip(poly,edge,keep_right):
            result=[]
            for a,b in zip(poly,poly[1:]+poly[:1]):
                inside_a=a[0]>=edge if keep_right else a[0]<=edge
                inside_b=b[0]>=edge if keep_right else b[0]<=edge
                if inside_a:result.append(a)
                if inside_a!=inside_b:
                    t=(edge-a[0])/(b[0]-a[0]);result.append((edge,a[1]+t*(b[1]-a[1])))
            return result
        low=min(p[0] for p in xy);high=max(p[0] for p in xy);steps=math.ceil((high-low)/.007)
        for index in range(steps):
            polygon=clip(clip(xy,low+(high-low)*index/steps,True),low+(high-low)*(index+1)/steps,False)
            clean=[]
            for point in polygon:
                if not clean or math.dist(point,clean[-1])>1e-10:clean.append(point)
            if len(clean)>1 and math.dist(clean[0],clean[-1])<1e-10:clean.pop()
            if len(clean)<3:continue
            radius=.17328;vertices=[(x,y,-math.sqrt((radius+lift)**2-x*x)) for lift in (.0005,.0014) for x,y in clean]
            size=len(clean);sides=[tuple(reversed(range(size))),tuple(range(size,2*size))]
            sides.extend((i,(i+1)%size,(i+1)%size+size,i+size) for i in range(size))
            head.mesh(name,vertices,sides,color,False)
    count=(18,12,8)[lod]
    for sign in (-1,1):
        x=sign*.0615
        face('printed_toy_eye',[(x+.012*math.cos(i*2*math.pi/count),1.414+.020*math.sin(i*2*math.pi/count)) for i in range(count)])
        face('determined_eyebrow',[(sign*.026,1.454),(sign*.086,1.478),(sign*.087,1.490),(sign*.024,1.464)])
    face('crooked_toy_grin',[(-.042,1.320),(.041,1.327),(.038,1.336),(-.040,1.329)])
    resident.attach(nodes,'robot_head',head,mats)

    torso=base.HumanModel('raider_trail_outfit',lod)
    resident.patch(torso,'earthy_trail_jerkin',[(-.211,.670),(.211,.670),(.177,1.162),(-.177,1.162)],'moss',-.132)
    resident.patch(torso,'rust_crossbody_strap',[(-.192,.692),(-.147,.674),(.184,1.157),(.139,1.157)],'rust',-.135)
    for sign in (-1,1):
        resident.patch(torso,'hood_shoulder_flap',[(sign*.051,1.033),(sign*.112,1.142),(sign*.177,1.158),(sign*.178,1.066)],'hair',-.137)
    torso.box('square_strap_buckle',(.018,.941,-.139),(.025,.022,.001),'brass',0)
    torso.box('buckle_centre',(.018,.941,-.141),(.016,.013,.0006),'rust',0)
    torso.box('trail_supply_pouch',(-.118,.767,-.142),(.067,.052,.010),'leather',.003)
    torso.box('pouch_flap',(-.118,.799,-.153),(.061,.014,.001),'rust',0)
    resident.attach(nodes,'robot_torso',torso,mats)

    # C-grips have a Z-axis aperture. The short solid staff passes through the
    # right grip and points forward in bind pose; existing tool motion lifts it.
    # Keeping its X span inside the arms preserves the existing doorway width.
    staff=base.HumanModel('raider_handheld_staff',lod)
    staff.cylinder('solid_wooden_staff',(.215,.653,-.352),.023,.772,2,'leather')
    staff.cylinder('wrapped_grip',(.215,.653,-.026),.028,.100,2,'cream')
    staff.cylinder('blunt_staff_cap',(.215,.653,-.747),.033,.050,2,'brass')
    staff.cylinder('cap_binding',(.215,.653,-.712),.026,.018,2,'rust')
    resident.attach(nodes,'robot_hand_r',staff,mats)
    checks=[]
    for name,obj in nodes.items():
        if obj.type!='MESH':continue
        bm=bmesh.new();bm.from_mesh(obj.data)
        degenerate=sum(face.calc_area()<=1e-12 for face in bm.faces);open_edges=sum(not edge.is_manifold for edge in bm.edges);volume=bm.calc_volume(signed=True)
        bm.free();assert degenerate==open_edges==0 and volume>0,(name,degenerate,open_edges,volume)
        checks.append(dict(node=name,degenerate_faces=degenerate,nonmanifold_edges=open_edges,summed_component_volume_m3=volume))
    return nodes,checks,base

def author(args):
    bpy=require_blender();out=args.output_dir.absolute();require_new(out);frozen=frozen_files();out.mkdir();source=out/'source';source.mkdir();records=[]
    for lod in range(3):
        bpy.ops.wm.read_factory_settings(use_empty=True);bpy.context.preferences.filepaths.file_preview_type='NONE'
        scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.render.fps=30;scene.frame_start=0;scene.frame_end=60
        nodes,checks,base=build(lod);base.add_clips(nodes);bpy.ops.object.select_all(action='SELECT')
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
        assert {clip['name'] for clip in doc['animations']}==set(base.CLIPS) and vertices<=(10500,7500,4500)[lod]
        blend=source/f'character-lod-{lod}.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
        records.append(dict(lod=lod,glb=path.name,glb_sha256=sha(path),blend=blend.name,blend_sha256=sha(blend),
            nodes=len(doc['nodes']),meshes=len(doc['meshes']),draws=len(primitives),vertices=vertices,triangles=triangles,
            clips=[dict(name=c['name'],channels=len(c['channels'])) for c in doc['animations']],removed_constant_rest_channels=dropped,geometry_checks=checks))
        print('RAIDER_LOD_COMPLETE',lod,vertices,triangles,len(primitives),flush=True)
    provenance=dict(schema=1,asset_id=IDENTITY,author='Voxys original procedural artwork',
        license='Original project artwork; same project distribution terms. No external character/texture inputs or logos.',
        profile='salvage-animated-rigid-v1',render_to_canonical=12,blender=bpy.app.version_string,
        canonical=dict(units='metres',up='+Y',forward='-Z',height=1.7,capsule_radius=.3,root_motion=False),
        clips=list(base.CLIPS),anchors=base.ANCHORS,palette=PALETTE,lods=records,
        silhouette='Original open-front brown toy hood, yellow cylinder head with determined printed eyes/grin, moss sleeves, earthy trapezoid jerkin, rust strap, short block legs and C-shaped toy hands.',
        staff=dict(parent='robot_hand_r',geometry='Solid wood cylinder, wrapped grip and blunt cap; 0.806 m full axial span.',
            role='Fixed cosmetic geometry riding the existing rigid right-hand pose. No weapon collider, damage timing or gameplay authority.'),
        animation='Existing eight rigid toy clips unchanged. Tool is a preliminary lifted-staff gesture; authored combat wind-up/recovery timing remains future work.',
        frozen_character_files=frozen,inputs={name:sha(ROOT/name) for name in INPUTS},generator_sha256=sha(__file__),
        scope='Original first trail enemy appearance only. Does not alter player/resident packages, movement, saves, actor IDs, runtime registry or gameplay.')
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    assert frozen==frozen_files(),'frozen player/resident packages changed during authoring'
    print('RAIDER_AUTHORING_COMPLETE',out,flush=True)

def cook(args):
    source=args.source_dir.resolve();out=args.output_dir.absolute();tool=args.tool.resolve();require_new(out)
    provenance=json.loads((source/'provenance.json').read_text());assert provenance['asset_id']==IDENTITY
    for path,expected in provenance['frozen_character_files'].items():assert sha(ROOT/path)==expected,'frozen character changed'
    records=[]
    with tempfile.TemporaryDirectory(prefix='.raider-cook-',dir=out.parent) as temp:
        pending=Path(temp)/'package';pending.mkdir();(pending/'source').mkdir();shutil.copyfile(source/'provenance.json',pending/'provenance.json')
        for lod,filename in enumerate(('character.vmesh','lod1.vmesh','lod2.vmesh')):
            authored=provenance['lods'][lod]
            for ext in ('glb','blend'):
                path=source/'source'/f'character-lod-{lod}.{ext}'
                assert path.is_file() and not path.is_symlink() and path.stat().st_size<=16*1024*1024 and sha(path)==authored[ext+'_sha256']
                shutil.copyfile(path,pending/'source'/path.name)
            result=subprocess.run([str(tool),'--profile','salvage-animated-rigid-v1',str(pending/'source'/f'character-lod-{lod}.glb'),str(pending/filename)],capture_output=True,text=True,timeout=60)
            if result.returncode:raise RuntimeError(f'LOD{lod} strict cook failed: {result.stderr}')
            payload=(pending/filename).read_bytes();assert 256<=len(payload)<=2*1024*1024 and payload[:8]==b'VOXYMESH'
            counts=struct.unpack_from('<12I',payload,16);gpu=counts[0]*72+counts[2]*4+counts[5]*64
            assert counts[6]==22 and counts[7]==15 and 15<=counts[4]<=48 and counts[10]==8 and not counts[8] and not counts[9] and gpu<=1024*1024
            records.append(dict(lod=lod,filename=filename,sha256=sha(pending/filename),bytes=len(payload),gpu_bytes=gpu,
                vertex_count=counts[0],index_count=counts[2],mesh_count=counts[7],node_count=counts[6],expanded_draws=counts[4],
                clips=counts[10],channels=counts[11],source_sha256=authored['glb_sha256'],cook_exit=result.returncode))
        manifest=dict(schema=1,profile='salvage-animated-rigid-v1',asset_id=IDENTITY,render_to_canonical=12,filename='character.vmesh',
            sha256=records[0]['sha256'],maximum_nodes=32,maximum_meshes=24,maximum_draws=48,clips=provenance['clips'],anchors=provenance['anchors'],
            source_sha256=records[0]['source_sha256'],provenance_sha256=sha(pending/'provenance.json'),cooker_sha256=sha(tool),generator_sha256=sha(__file__),lods=records)
        (pending/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');require_new(out);os.rename(pending,out)
    print(json.dumps(dict(output=str(out),manifest_sha256=sha(out/'manifest.json'),lods=records),indent=2))

def check(args):
    sys.path.insert(0,str(ROOT/'tools/salvage_assets'));from check_cove_robot import inspect
    package=args.package.resolve();provenance=json.loads((package/'provenance.json').read_text());assert provenance['asset_id']==IDENTITY
    for path,expected in provenance['frozen_character_files'].items():assert sha(ROOT/path)==expected,'frozen character changed'
    reports=[]
    for lod in range(3):
        path=package/'source'/f'character-lod-{lod}.glb';assert sha(path)==provenance['lods'][lod]['glb_sha256']
        report=inspect(path);assert report['clips']['idle']['maximum'][1]<=1.700001
        for clip in ('walk','land','fall'):assert report['clips'][clip]['minimum'][1]>=-1e-5
        raw=path.read_bytes();length=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+length]);blob=raw[28+length:];grips={}
        for node in doc['nodes']:
            if node['name'] not in ('robot_hand_l','robot_hand_r'):continue
            points=[];staff_vertices=0
            for primitive in doc['meshes'][node['mesh']]['primitives']:
                accessor=doc['accessors'][primitive['attributes']['POSITION']]
                if '_skin_lod' not in doc['materials'][primitive['material']]['name']:
                    staff_vertices+=accessor['count'];continue
                view=doc['bufferViews'][accessor['bufferView']];assert accessor['componentType']==5126 and accessor['type']=='VEC3'
                start=view.get('byteOffset',0)+accessor.get('byteOffset',0);stride=view.get('byteStride',12)
                for i in range(accessor['count']):
                    x,y,z=struct.unpack_from('<fff',blob,start+i*stride);points.append((-x,y+.047,-z+.026))
            inner=min(math.hypot(x,y) for x,y,z in points);assert inner>.044
            assert not any(y<-.032 and abs(x)<.031 for x,y,z in points)
            grips[node['name']]=dict(skin_vertices=len(points),inner_radius=inner,toy_hand_bottom_gap_open=True,staff_vertices=staff_vertices)
        assert grips['robot_hand_r']['staff_vertices']>0 and grips['robot_hand_l']['staff_vertices']==0
        report.update(source_sha256=sha(path),grips=grips,doorway_margin_m=dict(width=1.36-report['clips']['walk']['width'],height=2.24-report['clips']['walk']['maximum'][1]))
        reports.append(report)
    result=dict(schema=1,asset_id=IDENTITY,generator_sha256=sha(__file__),sampler_sha256=sha(ROOT/'tools/salvage_assets/check_cove_robot.py'),
        frozen_character_files_unchanged=True,lods=reports,
        limitation='Actual source geometry and sampled clip envelopes, not continuous weapon sweeps, damage timing, renderer acceptance or gameplay proof. Staff intentionally occupies the right grip; yellow C geometry itself remains open.')
    if args.output:require_new(args.output);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

def proof(args):
    require_blender();import runpy
    source=args.package.resolve()/'source/character-lod-0.blend';require_new(args.output)
    before=sha(source);sys.argv=sys.argv[:sys.argv.index('--')]+['--','--source',str(source),'--output',str(args.output.absolute())]
    runpy.run_path(str(ROOT/'tools/adventure_assets/author_human_proof.py'),run_name='__main__');assert sha(source)==before

def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='mode',required=True)
    a=sub.add_parser('author');a.add_argument('--output-dir',type=Path,required=True)
    c=sub.add_parser('cook');c.add_argument('--source-dir',type=Path,required=True);c.add_argument('--output-dir',type=Path,required=True);c.add_argument('--tool',type=Path,required=True)
    c=sub.add_parser('check');c.add_argument('--package',type=Path,required=True);c.add_argument('--output',type=Path)
    p=sub.add_parser('proof');p.add_argument('--package',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else None)
    globals()[args.mode](args)

if __name__=='__main__':main()
