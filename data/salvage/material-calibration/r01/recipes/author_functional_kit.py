"""Blender background recipe for the original first salvage kit; isolated candidates only."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

import bpy
import bmesh
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_pontoon as geo
import authoring_probe as base
import functional_kit as kit


class Model:
    def __init__(self, name, lod):
        self.name, self.lod, self.objects = name, lod, []

    def add(self, obj, color, bevel=.015):
        bm = bmesh.new(); bm.from_mesh(obj.data)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(obj.data); bm.free()
        if bevel and self.lod < 2:
            modifier = obj.modifiers.new('molded_edges', 'BEVEL')
            modifier.width = bevel
            modifier.segments = 2 if self.lod == 0 else 1
            geo.apply_modifier(obj, modifier)
        obj['palette_region'] = color
        self.objects.append(obj)
        return obj

    def box(self, name, center, half, color='cream', bevel=.015):
        x, y, z = half
        points = [(center[0] + a*x, center[1] + b*y, center[2] + c*z)
                  for a,b,c in ((-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),
                                (-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1))]
        return self.add(geo.object_mesh(name, points,
            [(3,2,1,0),(4,5,6,7),(0,1,5,4),(2,3,7,6),(0,4,7,3),(1,2,6,5)]), color, min(bevel, min(half)*.4))

    def cylinder(self, name, center, radius, depth, axis=1, color='steel'):
        n = (24, 16, 10)[self.lod]
        axes = [k for k in range(3) if k != axis]
        points = []
        for sign in (-1, 1):
            for i in range(n):
                p = list(center)
                p[axis] += sign * depth / 2
                p[axes[0]] += radius * math.cos(i*2*math.pi/n)
                p[axes[1]] += radius * math.sin(i*2*math.pi/n)
                points.append(p)
        faces = [tuple(range(n)), tuple(reversed(range(n, 2*n)))]
        faces += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
        return self.add(geo.object_mesh(name, points, faces), color, min(.006,depth*.2,radius*.2))

    def ring(self, name, center, radius, thickness, axis=2, color='steel'):
        n, m = ((32,8),(20,6),(12,4))[self.lod]
        axes = [k for k in range(3) if k != axis]
        points = []
        for i in range(n):
            a = i*2*math.pi/n
            for j in range(m):
                b = j*2*math.pi/m
                p = list(center)
                p[axis] += thickness * math.sin(b)
                p[axes[0]] += (radius + thickness * math.cos(b)) * math.cos(a)
                p[axes[1]] += (radius + thickness * math.cos(b)) * math.sin(a)
                points.append(p)
        faces = [(i*m+j, ((i+1)%n)*m+j, ((i+1)%n)*m+(j+1)%m, i*m+(j+1)%m)
                 for i in range(n) for j in range(m)]
        return self.add(geo.object_mesh(name, points, faces), color, 0)

    def mounts(self, part, bases):
        for s in part['sockets']:
            if s['family'] != 'structural':
                continue
            x,y,z = [v/50 for v in s['frame']['translation_ticks']]
            if s['role'] == 'plug':
                obj = geo.keyed_solid('keyed_peg_'+s['id'], z, y-.01, y+.18,
                                      *geo.params.mount_shape(self.lod))
                obj.location.x = -x
                geo.activate(obj)
                bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
                self.add(obj, 'cream', 0)
            else:
                cutter = geo.keyed_solid('keyed_well_'+s['id'], z, y-.02, y+.20,
                                         *geo.params.mount_shape(self.lod, True))
                cutter.location.x = -x
                geo.activate(cutter)
                bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
                # Each supplied base intersects this well; duplicate cutters for booleans.
                for obj in bases:
                    clone = cutter.copy(); clone.data = cutter.data.copy()
                    bpy.context.collection.objects.link(clone)
                    geo.boolean(obj, clone, 'DIFFERENCE')
                bpy.data.objects.remove(cutter, do_unlink=True)

    def finish(self, material):
        interface_checks = {}
        if self.name == 'winch':
            # Measure the actual authored vertices after modifiers, before the
            # parts are joined. This catches visible flange/cheek interference
            # independently of the box-only gameplay metadata.
            def x_bounds(obj):
                values = [-(obj.matrix_world @ v.co).x for v in obj.data.vertices]
                return min(values), max(values)
            cheeks = sorted(x_bounds(o) for o in self.objects if o.name.startswith('mast_cheek'))
            flanges = sorted(x_bounds(o) for o in self.objects if o.name.startswith('drum_flange'))
            if len(cheeks) != 2 or len(flanges) != 2:
                raise ValueError('winch requires two cheeks and two flanges')
            drum = next(x_bounds(o) for o in self.objects if o.name.startswith('cable_drum'))
            gaps = [flanges[0][0]-cheeks[0][1], cheeks[1][0]-flanges[1][1]]
            if min(gaps) < .009:
                raise ValueError('winch flange must clear its cheek by at least .009 m')
            if drum[0] < flanges[0][1] - 1e-6 or drum[1] > flanges[1][0] + 1e-6:
                raise ValueError('winch drum must fit between the flange inner faces')
            for obj in self.objects:
                if obj.name.startswith('cable_wrap'):
                    low, high = x_bounds(obj)
                    if low < flanges[0][1] or high > flanges[1][0]:
                        raise ValueError('cable wrap intersects its flange')
            interface_checks = dict(flange_cheek_clearance_metres=gaps,
                                    flange_x_bounds=flanges, drum_x_bounds=drum)
        for obj in self.objects:
            bm = bmesh.new(); bm.from_mesh(obj.data)
            bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
            bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=list(bm.edges))
            bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
            if any(not e.is_manifold for e in bm.edges):
                raise ValueError(f'{self.name} LOD{self.lod}: open component {obj.name}')
            bm.to_mesh(obj.data); bm.free()
            geo.activate(obj)
            bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
            bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.02,
                                     correct_aspect=False, scale_to_bounds=True)
            bpy.ops.object.mode_set(mode='OBJECT')
            obj.data.uv_layers.active.name = 'UV0'
            low_u,low_v,high_u,high_v = geo.REGIONS[obj['palette_region']]
            for loop in obj.data.uv_layers.active.data:
                loop.uv = (round((low_u+loop.uv.x*(high_u-low_u))*65536)/65536,
                           round((low_v+loop.uv.y*(high_v-low_v))*65536)/65536)
            obj.data.materials.clear(); obj.data.materials.append(material)
        geo.activate(self.objects[0])
        for obj in self.objects:
            obj.select_set(True)
        bpy.ops.object.join()
        obj = bpy.context.object; obj.name = f'{self.name}_lod_{self.lod}'
        triangulate = obj.modifiers.new('export_triangles','TRIANGULATE')
        geo.apply_modifier(obj, triangulate)
        bm = bmesh.new(); bm.from_mesh(obj.data)
        bad_faces = sum(f.calc_area() <= 1e-12 for f in bm.faces)
        bad_edges = sum(not e.is_manifold for e in bm.edges)
        volume = bm.calc_volume(signed=True); bm.free()
        if bad_faces or bad_edges or volume <= 0:
            raise ValueError(f'{obj.name}: invalid closed components: {bad_faces}, {bad_edges}, {volume}')
        return obj, dict(degenerate_faces=bad_faces, nonmanifold_edges=bad_edges,
                         summed_component_volume_m3=volume,
                         interface_checks=interface_checks,
                         volume_note='Decorative components can intersect; this sum is not displacement.')


def geometry(name, lod, part):
    m = Model(name,lod)
    if name in ('beam','plate'):
        h = (2,.16,.5 if name == 'beam' else 1)
        body = m.box(name+'_body', (0,0,0), h)
        m.mounts(part, [body])
        # Edge rails and recessed-looking broad side strips retain a clear silhouette.
        for z in (-h[2], h[2]):
            m.box('edge_trim',(0,0,z), (1.88,.065,.012),'teal',.005)
        if name == 'plate':
            for z in (-.72,.72):
                m.box('deck_grip',(0,.161,z),(1.82,.008,.10),'slate',.005)
    elif name == 'engine':
        body = m.box('engine_cover',(0,.12,1),( .5,.36,.5),'cream',.06)
        mount = m.box('transom_bracket',(0,-.4,.24),(.5,.08,.74),'steel')
        m.mounts(part,[mount])
        m.box('lower_leg',(0,-1.22,1.24),(.12,.98,.16),'teal',.035)
        m.box('bracket_neck',(0,-.28,.8),(.16,.04,.16),'steel')
        m.cylinder('shaft',(0,-2.08,1.34),.10,.12,2)
        m.box('coral_cap',(0,.47,1),(.40,.013,.36),'coral',.012)
        for x in (-.50,.50):
            m.box('side_panel',(x,.13,1),(.012,.23,.36),'teal')
            for z in (.76,1,1.24):
                m.box('vent',(x*1.028,.13,z),(.01,.16,.035),'slate',.005)
    elif name == 'propeller':
        m.ring('prop_guard',(0,0,.06),.425,.045,2,'teal')
        m.cylinder('hub',(0,0,0),.12,.64,2)
        for angle in (0,2*math.pi/3,4*math.pi/3):
            blade=m.box('prop_blade',(0,.22,.07),(.09,.19,.04),'steel',.025)
            # Canonical rotation about Z is Blender rotation about +Y.
            blade.rotation_euler.y=angle
        for x in (-.34,.34):
            m.box('guard_arm',(x,0,-.05),(.08,.035,.055),'teal',.008)
    elif name == 'helm':
        mount=m.box('console_foot',(0,-.40,0),(.5,.08,.5),'teal')
        column=m.box('console_column',(0,-.13,-.12),(.19,.22,.24),'cream',.035)
        # The .20 m well extends through the foot into the column above it.
        m.mounts(part,[mount,column])
        m.box('console_head',(0,.14,-.08),(.42,.13,.30),'cream',.04)
        m.box('instrument_face',(0,.282,-.14),(.25,.014,.14),'slate')
        for x in (-.13,.13):
            m.cylinder('dial',(x,.31,-.14),.075,.012,1,'teal' if x<0 else 'coral')
        m.ring('steering_wheel',(0,.25,.30),.18,.027,2,'slate')
        m.cylinder('steering_hub',(0,.25,.25),.06,.15,2)
        m.box('wheel_spoke',(0,.25,.30),(.16,.018,.018),'steel',.003)
    elif name == 'winch':
        mount=m.box('winch_foot',(0,-.88,0),(.5,.08,.5),'teal')
        m.mounts(part,[mount])
        for x in (-.36,.36):
            m.box('mast_cheek',(x,-.03,0),(.09,.77,.30),'coral',.025)
        m.cylinder('cable_drum',(0,.12,0),.28,.42,0,'slate')
        for x in (-.235,.235):
            m.cylinder('drum_flange',(x,.12,0),.36,.05,0,'steel')
        if lod < 2:
            for i in range(7):
                m.ring('cable_wrap',(-.18+i*.06,.12,0),.28,.018,0,'slate')
        m.box('fairlead_mount',(0,.80,-.36),(.25,.10,.10),'teal')
        m.ring('fairlead',(0,.80,-.48),.085,.024,2,'steel')
    elif name == 'cradle':
        mount=m.box('cradle_floor',(0,-.24,0),(.76,.08,1),'teal')
        column=m.cylinder('latch_pedestal',(0,-.08,0),.34,.36,1,'teal')
        m.mounts(part,[mount,column])
        for x in (-.88,.88):
            m.box('cargo_runner',(x,0,0),(.12,.32,1),'coral')
        m.cylinder('latch_housing',(0,.21,0),.35,.22,1,'steel')
        # A real keyed female seat, matching the cargo underside pin.
        housing=m.objects[-1]
        geo.boolean(housing,geo.keyed_solid('latch_seat',0,.12,.36,
                    *geo.params.mount_shape(lod,True)),'DIFFERENCE')
    else:
        h=[v/50 for v in part['collision'][0]['half_extents_ticks']]
        if name == 'generator':
            m.box('generator_core',(0,-.06,0),(.68,.46,.58),'cream',.065)
            for x in (-.80,.80):
                for z in (-.70,.70):
                    m.box('cage_upright',(x,0,z),(.075,.64,.075),'teal')
                for y in (-.565,.565):
                    m.box('cage_rail',(x,y,0),(.075,.075,.625),'teal')
            m.box('vent_panel',(0,.02,-.60),(.5,.28,.018),'slate')
            if lod < 2:
                for y in (-.18,-.08,.02,.12,.22):
                    m.box('vent_louver',(0,y,-.625),(.47,.018,.018),'steel',.004)
            m.box('service_panel',(.69,-.02,0),(.018,.29,.38),'coral')
        else:
            m.box('crate_shell',(0,0,0),(h[0]-.04,h[1]-.05,h[2]-.04),'cream',.04)
            for x in (-.78,.78):
                m.box('cargo_strap',(x,0,0),(.045,h[1],h[2]),'teal',.006)
            m.box('cargo_mark',(0,.15,-h[2]+.015),(.28,.24,.016),'coral')
            if lod < 2:
                for y in (-.45,0,.45):
                    m.box('panel_join',(0,y,-h[2]+.02),(h[0]-.1,.01,.018),'slate',.003)
        eye=part['sockets'][0]['frame']['translation_ticks']
        x,y,z=[v/50 for v in eye]
        m.box('eye_mount',(x,y-.105,z),(.14,.02,.09),'steel',.006)
        m.ring('tow_eye',(x,y,z),.08,.025,2,'steel')
        pin=geo.keyed_solid('cargo_latch_pin',0,-h[1]-.18,-h[1]+.01,*geo.params.mount_shape(lod))
        m.add(pin,'steel',0)
    return m


def preview(obj, out, part):
    scene=bpy.context.scene
    scene.render.engine='CYCLES'; scene.cycles.device='CPU'; scene.cycles.samples=24
    scene.cycles.use_denoising=False; scene.cycles.seed=0
    scene.render.threads_mode='FIXED'; scene.render.threads=4
    scene.render.resolution_x=512; scene.render.resolution_y=512; scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_mode='RGBA'
    scene.view_settings.view_transform='AgX'
    scene.world.use_nodes=True
    scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.55,.62,.69,1)
    scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.4
    bounds=part['footprint']; low=[v/50 for v in bounds['minimum_ticks']]; high=[v/50 for v in bounds['maximum_ticks']]
    target=[(a+b)/2 for a,b in zip(low,high)]
    for name,point,power,size in (('key',(-3,6,-4),1100,5),('fill',(4,3,2),800,4),('rim',(-2,4,5),900,3)):
        bpy.ops.object.light_add(type='AREA',location=geo.to_blender(point))
        light=bpy.context.object; light.name=name; light.data.energy=power; light.data.size=size
        base.orient_at(light,geo.to_blender(target))
    bpy.ops.object.camera_add(location=geo.to_blender((4,3,-6)))
    camera=bpy.context.object; scene.camera=camera; camera.data.type='ORTHO'
    camera.data.ortho_scale=max(b-a for a,b in zip(low,high))*1.5+.3
    base.orient_at(camera,geo.to_blender(target))
    scene.render.film_transparent=False
    scene.render.filepath=str(out/'preview.png'); bpy.ops.render.render(write_still=True)
    scene.render.filepath='//../preview.png'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--parts',nargs='+',choices=list(kit.definitions()),default=list(kit.definitions()))
    parser.add_argument('--material-profile',choices=geo.materials.PROFILES,default=geo.materials.LEGACY)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup')
    out=args.output_dir.expanduser().absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        parser.error('output must not exist; parent must exist')
    if len(set(args.parts)) != len(args.parts):
        parser.error('duplicate part')
    out.mkdir()
    for name in args.parts:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        scene=bpy.context.scene; scene.world=bpy.data.worlds.new('studio_world')
        scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=1
        directory=out/name; directory.mkdir(); source=directory/'source'; source.mkdir(); maps=source/'maps'; maps.mkdir()
        part=kit.definitions()[name]; metrics=[]; objects=[]
        for lod,(tri_limit,vertex_limit,size,threshold) in enumerate(kit.LOD_LIMITS):
            images=geo.textures(geo.params.VERSION_TWO_SPEC,size,maps,lod,args.material_profile)
            obj,checks=geometry(name,lod,part).finish(geo.material(images,lod,args.material_profile))
            geo.activate(obj)
            settings=dict(export_format='GLB',use_selection=True,export_yup=True,export_apply=True,
                          export_materials='EXPORT',export_normals=True,export_texcoords=True,export_tangents=True,
                          export_animations=False,export_skins=False,export_morph=False,export_cameras=False,
                          export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
            path=source/f'{name}-lod-{lod}.glb'
            bpy.ops.export_scene.gltf(filepath=str(path),**settings)
            doc=base.glb_json(path)
            vertices=triangles=0
            for mesh in doc['meshes']:
                for p in mesh['primitives']:
                    vertices+=doc['accessors'][p['attributes']['POSITION']]['count']
                    triangles+=doc['accessors'][p['indices']]['count']//3
            if len(doc['meshes'])!=1 or len(doc['materials'])!=1 or vertices>vertex_limit or triangles>tri_limit:
                raise ValueError(f'{name} LOD{lod} exceeds kit budget: {vertices}/{triangles}')
            metrics.append(dict(lod=lod,vertices=vertices,triangles=triangles,geometry=checks,
                                source_sha256=base.sha256(path),export_settings=settings))
            objects.append(obj); obj.hide_render=True; obj.hide_set(True)
        metadata=kit.sidecar(name,source)
        (source/f'{name}.gameplay.json').write_text(json.dumps(metadata,indent=2)+'\n')
        objects[0].hide_render=False; objects[0].hide_set(False)
        preview(objects[0],directory,part)
        bpy.ops.wm.save_as_mainfile(filepath=str(source/f'{name}.blend'),compress=True)
        manifest=dict(schema=1,status='offline candidate; not runtime or visual acceptance',
                      part=part['key'],blender_version=bpy.app.version_string,
                      material_profile=args.material_profile,
                      blender_build_hash=bpy.app.build_hash.decode(),lods=metrics,
                      cross_lod_clearances=geo.params.cross_lod_clearances(),
                      provenance='Original procedural geometry/maps; no generated image or third-party model input',
                      recipes={Path(p).name:base.sha256(Path(p)) for p in
                               (__file__,kit.__file__,geo.__file__,geo.params.__file__,geo.materials.__file__,base.__file__)},
                      render=dict(engine='Cycles',device='CPU',samples=24,threads=4,seed=0),
                      files={str(p.relative_to(directory)):dict(sha256=base.sha256(p),bytes=p.stat().st_size)
                             for p in sorted(directory.rglob('*')) if p.is_file()})
        (directory/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print('SALVAGE_KIT_PART',name,json.dumps(metrics),flush=True)


if __name__=='__main__':
    main()
