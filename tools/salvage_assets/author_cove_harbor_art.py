"""Original brick-built Cove scenery and exact-envelope harbor gantry.

Blender background only; no preview images. Geometry/collision are authored
from the same bounded lattice solids. Existing game terrain, parts and saves
are inputs, never outputs. Export basis12 is the established canonical bridge.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
sys.path.insert(0,str(ROOT/'tools/salvage_assets'))
import author_functional_kit as art
import author_cove_toy_art as toy

PALETTE={**toy.PALETTE,'stone':'75878B','green':'466F57'}
GANTRY=[([-210,0,-210],[-190,352,-190]),([190,0,-210],[210,352,-190]),
        ([-210,0,190],[-190,352,210]),([190,0,190],[210,352,210]),
        ([-210,352,-210],[210,372,-190]),([-210,352,190],[210,372,210]),
        ([-210,352,-190],[-190,372,190]),([190,352,-190],[210,372,190]),
        ([-190,352,-43],[190,368,-27]),([-190,352,27],[190,368,43])]


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def ticks(v):
    result=[round(n*50) for n in v]
    assert all(abs(a/50-b)<1e-7 for a,b in zip(result,v))
    return result


def materials(lod):
    result=toy.materials(lod)
    for name in ('stone','green'):
        mat=bpy.data.materials.new('cove_'+name);mat.use_nodes=True;mat.use_backface_culling=True
        p=mat.node_tree.nodes.get('Principled BSDF')
        p.inputs['Base Color'].default_value=tuple(art.metric.linear_channel(int(PALETTE[name][i:i+2],16)) for i in (0,2,4))+(1.,)
        p.inputs['Roughness'].default_value=.56 if name=='stone' else .46
        result[name]=mat
    return result


def build_scenery(lod):
    colliders=[];groups=[]
    def group(name,draw):
        m=art.Model('cove_'+name,lod)
        def solid(label,lo,hi,color,bevel=.018):
            lo,hi=list(lo),list(hi);assert all(a<b for a,b in zip(lo,hi))
            colliders.append(dict(group=name,name=label,minimum=ticks(lo),maximum=ticks(hi)))
            m.box(label,tuple((a+b)/2 for a,b in zip(lo,hi)),tuple((b-a)/2 for a,b in zip(lo,hi)),color,bevel)
        draw(m,solid)
        obj,check=m.finish(materials(lod),art.shading.ANALYTIC,metric_uv=False)
        obj.name='cove_'+name;groups.append((obj,check))
    def workshop(m,solid):
        # Entry faces the real dock end: four .32m steps from its1.28m deck.
        solid('workshop_foundation',(10,.64,-42),(16,2.56,-36),'stone')
        for i in range(4):
            solid('entry_step_'+str(i),(8+i*.5,.64,-42),(8.5+i*.5,1.6+i*.32,-40),'cream')
        for x in (10,15.68):
            for z in (-42,-36.32):solid('portal_post',(x,2.56,z),(x+.32,5.76,z+.32),'teal')
        solid('east_wall',(15.76,2.56,-41.68),(16,5.44,-36.32),'cream')
        solid('rear_wall',(10.32,2.56,-36.24),(15.68,5.44,-36),'cream')
        # A visibly assembled stepped shed roof, with true collision per layer.
        for lo,hi in (((9.84,5.76,-42.16),(16.16,6.08,-35.84)),
                      ((10.48,6.08,-41.92),(15.52,6.40,-36.08)),
                      ((11.44,6.40,-41.68),(14.56,6.72,-36.32))):solid('stepped_roof',lo,hi,'teal')
        solid('service_bench',(12.8,2.56,-37.2),(15.4,3.52,-36.4),'teal')
        # Open front and west sides: all trim stays on existing solid faces.
        m.box('orange_front_fascia',(13,5.94,-42.168),(2.96,.10,.008),'coral',.006)
        m.box('cream_roof_ridge',(13,6.72,-39),(1.35,.008,2.36),'cream',.006)
        for x in (10.12,15.84):
            for y in (2.88,3.52,4.16,4.8,5.44):
                m.box('post_join',(x,y,-42.008),(.105,.025,.008),'cream',.005)
        if lod<2:
            for x in (11.2,12.4,13.6,14.8):
                m.box('rear_panel_joint',(x,4,-36.246),(.012,1.24,.006),'teal',.004)
            for x in (13.2,14.2,15.0):
                m.box('bench_drawer',(x,3.04,-37.208),(.32,.22,.008),'cream',.008)
                m.box('bench_pull',(x,3.10,-37.216),(.12,.025,.004),'coral',.002)
    group('workshop',workshop)
    def wreck(m,solid):
        # West of the protected berth: a genuinely broken inert vessel landmark.
        # Open center/rib gaps remain holes; no purchasable or fake moving modules.
        solid('buried_keel',(-16.2,-5.76,-65),(-10.8,-4.48,-57),'slate')
        for lo,hi in (((-16.5,-4.48,-64.8),(-16.18,-1.28,-59.2)),
                      ((-10.82,-4.48,-64.4),(-10.5,-.64,-61.2)),
                      ((-10.82,-4.48,-59.8),(-10.5,-1.6,-57.2))):solid('broken_hull_side',lo,hi,'cream')
        for z,y in ((-64.0,-.32),(-61.4,.32),(-58.2,-1.28)):
            solid('exposed_rib',(-16.18,-4.48,z),(-15.86,y,z+.32),'teal')
        solid('broken_bridge',(-15.86,-.64,-61.4),(-12.98,.32,-61.08),'teal')
        solid('signal_mast',(-14.2,-4.48,-63.2),(-13.88,2.56,-62.88),'cream')
        for y in (-.32,.64,1.60):m.box('mast_safety_band',(-14.04,y,-63.208),(.16,.08,.008),'coral',.003)
        m.box('starboard_identification',(-10.492,-1.2,-62.8),(.008,.24,1.2),'teal',.007)
        if lod<2:
            for z in (-64,-62.8,-60.8):m.box('hull_mold_seam',(-16.508,-2.7,z),(.008,.9,.014),'teal',.003)
    group('wreck',wreck)
    def beacon(name,x,z,base,top):
        def draw(m,solid):
            solid('beacon_foot',(x-.64,base,z-.64),(x+.64,base+1.28,z+.64),'stone')
            solid('beacon_column',(x-.24,base+1.28,z-.24),(x+.24,top-.64,z+.24),'teal')
            solid('beacon_crown',(x-.48,top-.64,z-.48),(x+.48,top,z+.48),'cream')
            # Sculpted color masses and navigation silhouette; no fake light bloom.
            for side in (-1,1):
                m.box('crown_orange_face',(x+side*.488,top-.32,z),(.008,.20,.32),'coral',.007)
            m.box('column_cream_band',(x,top-1.2,z-.248),(.20,.10,.008),'cream',.006)
        group(name,draw)
    beacon('east_beacon',20,-46,-.64,4.16)
    beacon('west_beacon',-17,-42,-1.28,3.52)
    def shore(m,solid):
        # Low staggered stone/green brick forms on the landward terraces.
        for x,z,y in ((18,-38,1.60),(20,-36,2.24),(22,-39,1.60)):
            solid('stone_planter',(x-.64,y,z-.64),(x+.64,y+.64,z+.64),'stone')
            solid('green_crown',(x-.48,y+.64,z-.48),(x+.48,y+1.28,z+.48),'green')
            m.box('green_top_panel',(x,y+1.288,z),(.30,.008,.30),'teal',.004)
        for x,z,y in ((-.5,-40,.32),(1.5,-39,.64),(3.5,-38,.96)):
            solid('shore_edge',(x-.80,y,z-.32),(x+.80,y+.64,z+.32),'stone')
            if lod<2:m.box('stone_join',(x,y+.32,z-.328),(.02,.23,.008),'slate',.004)
    group('shore',shore)
    assert len(colliders)<=48
    return groups,colliders


def build_gantry(lod):
    m=art.Model('cove_gantry',lod)
    for i,(lo,hi) in enumerate(GANTRY):
        a,b=([v*.02 for v in p] for p in (lo,hi));center=tuple((x+y)/2 for x,y in zip(a,b));half=tuple((y-x)/2 for x,y in zip(a,b))
        m.box('gantry_structural_'+str(i),center,half,'teal' if i<4 else 'cream',.018)
        # Segmented molded shells within the existing ten boxes, no new braces
        # across the clear lift opening and no fake drive/drum animation.
        if i<4:
            for y in (1.28,2.56,3.84,5.12,6.40):
                m.box('post_segment', (center[0],y,a[2]-.006),(half[0]-.025,.045,.006),'cream',.006)
            for y in (.32,.64,.96):m.box('safety_band',(center[0],y,b[2]+.006),(half[0]-.025,.08,.006),'coral',.006)
        elif i in (4,5):
            m.box('cream_orange_header',(center[0],center[1],a[2]-.006),(3.76,.08,.006),'coral',.004)
            if lod<2:
                for x in (-3.2,-1.6,0,1.6,3.2):m.box('header_seam',(x,center[1],b[2]+.004),(.012,.15,.004),'teal',.002)
        elif i>=8:
            m.box('exposed_trolley_track',(center[0],a[1]-.006,center[2]),(3.65,.006,.06),'steel',.003)
    obj,check=m.finish(materials(lod),art.shading.ANALYTIC,metric_uv=False);obj.name='cove_gantry'
    return [(obj,check)]


def export(objects,path):
    bpy.ops.object.select_all(action='DESELECT')
    for obj,_ in objects:obj.select_set(True)
    bpy.context.view_layer.objects.active=objects[0][0]
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
        export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,
        export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,
        export_morph=False,export_cameras=False,export_lights=False,export_extras=False,
        export_vertex_color='NONE',export_all_vertex_colors=False)
    document=art.base.glb_json(path);primitives=[p for mesh in document['meshes'] for p in mesh['primitives']]
    vertices=sum(document['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)
    triangles=sum(document['accessors'][p['indices']]['count']//3 for p in primitives)
    if vertices>18000 or triangles>18000 or len(primitives)>45:raise ValueError('bounded scenery geometry exceeded')
    points=[art.geo.canonical(obj.matrix_world@v.co) for obj,_ in objects for v in obj.data.vertices]
    return dict(vertices=vertices,triangles=triangles,meshes=len(document['meshes']),nodes=len(document['nodes']),draws=len(primitives),
        bounds=dict(minimum=[min(p[i] for p in points) for i in range(3)],maximum=[max(p[i] for p in points) for i in range(3)]),
        geometry=[check for _,check in objects])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:parser.error('requires background factory startup')
    out=args.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():parser.error('new output directory required')
    out.mkdir();(out/'source').mkdir();records=[];collision=None
    for kind in ('scenery','gantry'):
        for lod in range(3):
            bpy.ops.wm.read_factory_settings(use_empty=True)
            bpy.context.preferences.filepaths.file_preview_type='NONE';bpy.context.scene.unit_settings.system='METRIC'
            if kind=='scenery':
                objects,boxes=build_scenery(lod)
                if collision is None:collision=boxes
                else:assert collision==boxes
            else:objects=build_gantry(lod)
            glb=out/'source'/f'{kind}-lod-{lod}.glb';checks=export(objects,glb)
            blend=out/'source'/f'{kind}-lod-{lod}.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
            records.append(dict(kind=kind,lod=lod,glb_sha256=sha(glb),blend_sha256=sha(blend),**checks))
            print('COVE_ENVIRONMENT_SOURCE',kind,lod,json.dumps({k:checks[k] for k in ('vertices','triangles','meshes','draws','bounds')}),flush=True)
    inputs=[Path(__file__),Path(art.__file__),Path(art.geo.__file__),Path(art.geo.params.__file__),Path(art.shading.__file__),Path(art.metric.__file__),Path(toy.__file__)]
    provenance=dict(schema=1,profile='salvage-rigid-v1',asset_id='voxys-cove-environment-r01',render_to_canonical=12,
        blender_version=bpy.app.version_string,palette_srgb=PALETTE,lods=records,collision=collision,
        gantry_collision=[dict(minimum=a,maximum=b) for a,b in GANTRY],
        scene_origin=[-19,-200,-37],protected_berth=dict(minimum=[-9,-77],maximum=[8,-47]),
        fixed_inputs={name:sha(ROOT/name) for name in ('data/lego_shore.ldh','data/salvage/fixture-cove-r01.json','src/game/expedition/cove_harbor_lift.cpp')},
        inputs={('tools/salvage_assets/author_cove_harbor_art.py' if p==Path(__file__) else str(p.relative_to(ROOT))):sha(p) for p in inputs},
        provenance='Original solid-color molded geometry; no external assets, generated textures or preview images; explicit inert wreck scenery and actual powered-gantry presentation only.')
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print('COVE_ENVIRONMENT_COMPLETE',len(collision),'static collision boxes',flush=True)


if __name__=='__main__':main()
