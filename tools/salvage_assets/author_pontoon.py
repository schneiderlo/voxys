"""Generate the original salvage pontoon; run with Blender background factory startup.

All output is an isolated candidate. A successful export is not engine/asset acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pontoon_parameters as params
import material_standards as materials
import metric_materials as metric

REGIONS = {'cream': (.01, .26, .73, .99), 'teal': (.77, .51, .99, .99),
           'coral': (.77, .26, .99, .49), 'slate': (.77, .01, .99, .24),
           'steel': (.01, .01, .24, .24)}


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spec', type=Path, default=Path(__file__).with_name('pontoon.spec.json'))
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--preview', choices=('none', 'quick', 'full'), default='quick')
    parser.add_argument('--material-profile', choices=materials.PROFILES, default=materials.LEGACY)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup; never edits an interactive scene')
    args.spec = args.spec.expanduser().absolute()
    args.parameters = params.read_spec(args.spec)
    output = args.output_dir.expanduser().absolute()
    if not output.parent.is_dir() or output.is_symlink() or output.exists():
        parser.error('output-dir must not exist and its parent must exist')
    output.mkdir()
    args.output_dir = output.resolve()
    return args


def to_blender(point):
    x, y, z = point
    return (-x, z, y)


def canonical(point):
    x, y, z = point
    return (-x, z, y)


def object_mesh(name, points, faces):
    mesh = bpy.data.meshes.new(name + '_mesh')
    mesh.from_pydata([to_blender(p) for p in points], [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def activate(obj):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def apply_modifier(obj, modifier):
    activate(obj)
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def profile(a, b, c):
    return [(-a+c, -b), (a-c, -b), (a, -b+c), (a, b-c),
            (a-c, b), (-a+c, b), (-a, b-c), (-a, -b+c)]


def hull(spec, lod):
    _, _, length = params.half_dimensions(spec)
    stations = {-length, -.9*length, -.7*length, .7*length, .9*length, length}
    if lod < 2:
        for center in (-.35*length, .35*length):
            stations.update(center + offset for offset in (-.025, -.015, .015, .025))
    points = []
    zs = sorted(stations)
    for z in zs:
        a, b, c = params.section(spec, z)
        if lod < 2 and any(abs(z-center) <= .015001 for center in (-.35*length, .35*length)):
            a -= .006; b -= .006
        points.extend((x, y, z) for x, y in profile(a, b, c))
    faces = [tuple(reversed(range(8))), tuple(range(len(points)-8, len(points)))]
    for s in range(len(zs)-1):
        for i in range(8):
            j = (i+1) % 8
            faces.append((s*8+i, s*8+j, (s+1)*8+j, (s+1)*8+i))
    return object_mesh(f'pontoon_shell_lod{lod}', points, faces)


def keyed_solid(name, center_z, low, high, radius, flat, segments):
    ring = params.keyed_profile(radius, flat, segments)
    n = len(ring)
    points = [(x, y, z+center_z) for y in (low, high) for x, z in ring]
    faces = [tuple(range(n)), tuple(reversed(range(n, 2*n)))]
    faces.extend((i, n+i, n+(i+1)%n, (i+1)%n) for i in range(n))
    return object_mesh(name, points, faces)


def boolean(target, cutter, operation):
    modifier = target.modifiers.new('authored_' + operation.lower(), 'BOOLEAN')
    modifier.operation = operation
    modifier.solver = 'EXACT'
    modifier.object = cutter
    apply_modifier(target, modifier)
    bpy.data.objects.remove(cutter, do_unlink=True)


def shell_geometry(spec, lod):
    _, height, _ = params.half_dimensions(spec)
    obj = hull(spec, lod)
    for z in (-1.0, 0.0, 1.0):
        peg = keyed_solid('peg_cutter', z, height-.025, height+params.PEG_HEIGHT, *params.mount_shape(lod))
        boolean(obj, peg, 'UNION')
        well = keyed_solid('well_cutter', z, -height-.04, -height+params.WELL_DEPTH, *params.mount_shape(lod,True))
        boolean(obj, well, 'DIFFERENCE')
    # A molded bow arrow gives a geometric direction cue as well as coral paint.
    _, _, half_length = params.half_dimensions(spec)
    ring = [(-.11, -.75*half_length), (0, -.91*half_length), (.11, -.75*half_length)]
    points = [(x, params.section(spec,z)[1]+offset, z) for offset in (-.015,.015) for x,z in ring]
    arrow = object_mesh('molded_bow_arrow',points,[(0,1,2),(5,4,3),(0,3,4,1),(1,4,5,2),(2,5,3,0)])
    boolean(obj,arrow,'UNION')
    if lod == 0:
        bevel = obj.modifiers.new('molded_edge_bevel', 'BEVEL')
        bevel.width = .010
        bevel.segments = 2
        bevel.limit_method = 'ANGLE'
        bevel.angle_limit = math.radians(18)
        apply_modifier(obj, bevel)
    # Boolean and bevel normals are corrected geometrically, never by double-sided material.
    bm = bmesh.new(); bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-6, edges=list(bm.edges))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    return obj


def classify_face(face, obj, spec, lod=None):
    x, y, z = canonical(obj.matrix_world @ face.center)
    _, half_y, length = params.half_dimensions(spec)
    if -.92*length < z < -.74*length and abs(x)<.12 and y>params.section(spec,z)[1]+.002:
        return 'teal'
    if lod is None:
        # Preserve the frozen candidate's recipe for explicit legacy replays.
        if y < -half_y + .205 and any((x*x + (z-s)**2) < .26**2 for s in (-1, 0, 1)):
            return 'teal'
    else:
        # Each well LOD has a different containment radius. Classify the whole
        # face in that authored volume, including the molded rim, rather than
        # a fixed-radius center test that changes color with tessellation.
        radius = params.mount_shape(lod, True)[0] + .012
        points = [canonical(obj.matrix_world @ obj.data.vertices[i].co) for i in face.vertices]
        if any(all(-half_y-.012 <= py <= -half_y+params.WELL_DEPTH+.012
                   and px*px+(pz-center)**2 <= radius*radius for px,py,pz in points)
               for center in (-1, 0, 1)):
            return 'teal'
    if any(abs(z-center) < .024 for center in (-.35*length, .35*length)):
        return 'teal'
    if z < -.88*length:
        return 'coral'
    if z > .88*length:
        return 'teal'
    if y > half_y+.16:
        return 'cream'
    return 'cream'


def unwrap(obj, spec, lod=None):
    activate(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=.015,
                             area_weight=.5, correct_aspect=False, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.data.uv_layers.active.name = 'UV0'
    for face in obj.data.polygons:
        region = REGIONS[classify_face(face, obj, spec, lod)]
        for index in face.loop_indices:
            uv = obj.data.uv_layers.active.data[index].uv
            u = region[0] + uv.x*(region[2]-region[0])
            v = region[1] + uv.y*(region[3]-region[1])
            # Declared asset-specific UV precision removes subpixel exporter noise.
            uv[:] = (round(u*65536)/65536, round(v*65536)/65536)


def png(path, values):
    height, width, channels = values.shape
    assert channels == 4 and values.dtype == np.uint8
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data) & 0xffffffff)
    rows = b''.join(b'\0'+values[row].tobytes() for row in range(height))
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    path.write_bytes(data + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def textures(spec, size, maps, lod, profile=materials.LEGACY):
    if profile not in (materials.LEGACY, materials.OPAQUE_CALIBRATION):
        raise ValueError('atlas recipe requires legacy or opaque calibration profile')
    yy, xx = np.indices((size,size), dtype=np.uint64)
    noise = ((xx*73856093) ^ (yy*19349663) ^ np.uint64(spec['seed']))
    grain = (noise & 255).astype(np.float64)/255-.5
    base = np.zeros((size,size,4), dtype=np.uint8); base[:,:,:3] = 210; base[:,:,3] = 255
    mr = np.zeros_like(base); mr[:,:,3] = 255; mr[:,:,1] = 125
    normal = np.zeros_like(base); normal[:,:,0] = 128; normal[:,:,1] = 128; normal[:,:,2:] = 255
    roughness = {'cream': .46, 'teal': .55, 'coral': .47, 'slate': .73, 'steel': .36}
    # UV0 origin is top-left in strict glTF. Blender image pixels are top-row source
    # PNGs but glTF export maps UVs correctly; use v=1-row for Blender atlas authoring.
    u = (xx.astype(np.float64)+.5)/size
    v = 1-(yy.astype(np.float64)+.5)/size
    for name, (low_u, low_v, high_u, high_v) in REGIONS.items():
        mask = (u >= low_u-.008) & (u <= high_u+.008) & (v >= low_v-.008) & (v <= high_v+.008)
        color = np.array([int(spec['palette_srgb'][name][i:i+2],16) for i in (0,2,4)])
        for c in range(3):
            base[:,:,c][mask] = np.clip(color[c]+grain[mask]*3,0,255).astype(np.uint8)
        mr[:,:,1][mask] = np.clip((roughness[name]+grain[mask]*.035)*255,0,255).astype(np.uint8)
        mr[:,:,2][mask] = 255 if name == 'steel' else 0
        normal[:,:,0][mask] = np.clip(128+grain[mask]*4,0,255).astype(np.uint8)
        normal[:,:,1][mask] = np.clip(128+(((noise[mask]>>8)&255).astype(np.float64)/255-.5)*4,0,255).astype(np.uint8)
        if profile == materials.OPAQUE_CALIBRATION:
            color_bytes, mr_bytes = materials.region_bytes(name, spec['palette_srgb'][name])
            base[mask] = color_bytes
            mr[mask] = mr_bytes
    outputs = {'base_color': base}
    if profile == materials.OPAQUE_CALIBRATION:
        # Preserve region-specific roughness and conductor identity at Far.
        # No unscaled random normals or grain in this calibration candidate.
        outputs['metallic_roughness'] = mr
    elif lod < 2:
        outputs.update(normal=normal, metallic_roughness=mr)
    result = {}
    for name, values in outputs.items():
        path = maps / f'pontoon-lod-{lod}-{name}.png'
        png(path,values)
        image = bpy.data.images.load(str(path), check_existing=False)
        image.name = f'pontoon_lod{lod}_{name}'
        image.colorspace_settings.name = 'sRGB' if name == 'base_color' else 'Non-Color'
        image.pack()
        result[name] = image
    return result


def material(images, lod, profile=materials.LEGACY):
    if profile not in (materials.LEGACY, materials.OPAQUE_CALIBRATION):
        raise ValueError('atlas recipe requires legacy or opaque calibration profile')
    mat = bpy.data.materials.new(f'pontoon_opaque_atlas_lod{lod}')
    mat.use_nodes = True; mat.use_backface_culling = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (1,1,1,1)
    legacy_far = profile == materials.LEGACY and lod == 2
    shader.inputs['Metallic'].default_value = 0 if legacy_far else 1
    shader.inputs['Roughness'].default_value = .49 if legacy_far else 1
    for name, image in images.items():
        node = mat.node_tree.nodes.new('ShaderNodeTexImage'); node.image=image
        node.interpolation='Linear'; node.extension='REPEAT'
        if name == 'base_color':
            mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
        elif name == 'normal':
            normal = mat.node_tree.nodes.new('ShaderNodeNormalMap'); normal.inputs['Strength'].default_value=1
            mat.node_tree.links.new(node.outputs['Color'],normal.inputs['Color'])
            mat.node_tree.links.new(normal.outputs['Normal'],shader.inputs['Normal'])
        else:
            split = mat.node_tree.nodes.new('ShaderNodeSeparateColor'); split.mode='RGB'
            mat.node_tree.links.new(node.outputs['Color'],split.inputs['Color'])
            mat.node_tree.links.new(split.outputs['Green'],shader.inputs['Roughness'])
            mat.node_tree.links.new(split.outputs['Blue'],shader.inputs['Metallic'])
    return mat


def check_shell(obj, spec):
    bm = bmesh.new(); bm.from_mesh(obj.data)
    volume = bm.calc_volume(signed=True)
    non_manifold = sum(not edge.is_manifold for edge in bm.edges)
    degenerate = sum(face.calc_area() <= 1e-12 for face in bm.faces)
    if non_manifold or degenerate or not math.isfinite(volume) or volume <= 0:
        raise ValueError(f'non-watertight/outward shell: edges={non_manifold}, faces={degenerate}, volume={volume}')
    boxes, proxy_volume, inertia = params.physical_recipe(spec)
    error = abs(proxy_volume/volume-1)
    if error > .05:
        raise ValueError(f'proxy/enclosure volume error {error:.3%} exceeds 5%')
    bm.free()
    return {'enclosure_volume_cubic_metres':volume, 'proxy_volume_cubic_metres':proxy_volume,
            'relative_volume_error':error, 'non_manifold_edges':non_manifold,
            'degenerate_faces':degenerate, 'proxy_inertia_diagonal_kg_m2':inertia,
            'closed_shell_includes':'Actual pegs and bottom wells after boolean/bevel modifiers',
            'collision_profile':'Nine gameplay boxes; visible wells/pegs do not become separate contacts'}


def proxy_surface(spec):
    """Exact exterior quads of the non-overlapping, coaxial gameplay box union."""
    boxes = sorted(params.proxy_recipe(spec), key=lambda b:b['frame']['translation_ticks'][2])
    quads = []
    for index, box in enumerate(boxes):
        x,y,z = (v/50 for v in box['half_extents_ticks'])
        center = box['frame']['translation_ticks'][2]/50
        low,high = center-z,center+z
        quads.extend(([(sign*x,-y,low),(sign*x,y,low),(sign*x,y,high),(sign*x,-y,high)] for sign in (-1,1)))
        quads.extend(([(-x,sign*y,low),(x,sign*y,low),(x,sign*y,high),(-x,sign*y,high)] for sign in (-1,1)))
        for end,neighbor in ((low,index-1),(high,index+1)):
            if not 0 <= neighbor < len(boxes):
                quads.append([(-x,-y,end),(x,-y,end),(x,y,end),(-x,y,end)])
                continue
            other=boxes[neighbor]['half_extents_ticks']; a,b=other[0]/50,other[1]/50
            # Smaller end lies fully inside its neighbor: it is not an exterior face.
            if a>=x and b>=y:continue
            if a>x or b>y:raise ValueError('proxy recipe no longer has nested cross-sections')
            rectangles=[(-x,-y,-a,y),(a,-y,x,y),(-a,-y,a,-b),(-a,b,a,y)]
            for left,bottom,right,top in rectangles:
                if right>left and top>bottom:
                    quads.append([(left,bottom,end),(right,bottom,end),(right,top,end),(left,top,end)])
    return quads


def main_shell_sample(point, spec):
    x,y,z=point; half_y=params.half_dimensions(spec)[1]
    contact = any(x*x+(z-center)**2 <= .31**2 for center in (-1,0,1))
    return not (contact and (y>half_y-.011 or y<-half_y+.211))


def contact_error(obj, spec):
    """Bounded 10 mm surface sampling, explicitly not exact skin collision."""
    step=.01
    vertices=[Vector(canonical(v.co)) for v in obj.data.vertices]
    triangles=[tuple(face.vertices) for face in obj.data.polygons]
    if any(len(face)!=3 for face in triangles):raise ValueError('contact check requires final triangles')
    skin=BVHTree.FromPolygons(vertices,triangles,all_triangles=True)
    quads=proxy_surface(spec)
    proxy=BVHTree.FromPolygons([p for q in quads for p in q],
                             [tuple(range(i*4,i*4+4)) for i in range(len(quads))])
    def maximum(points, tree):
        count=excluded=0; result=0.; at=None
        for point in points:
            if not main_shell_sample(point,spec):excluded+=1;continue
            distance=tree.find_nearest(point)[3]
            if distance is None:raise ValueError('missing contact BVH hit')
            count+=1
            if distance>result:result=distance;at=list(point)
        return {'samples':count,'excluded_contact_detail_samples':excluded,
                'maximum_sampled_distance_metres':result,'at_canonical_metres':at}
    def quad_points():
        for quad in quads:
            a,b,_,d=map(Vector,quad);u=b-a;v=d-a
            nu,nv=math.ceil(u.length/step),math.ceil(v.length/step)
            for i in range(nu+1):
                for j in range(nv+1):yield a+u*(i/nu)+v*(j/nv)
    def triangle_points():
        for face in triangles:
            a,b,c=(vertices[i] for i in face);u=b-a;v=c-a
            count=max(1,math.ceil(max(u.length,v.length,(b-c).length)/step))
            for i in range(count+1):
                for j in range(count+1-i):yield a+u*(i/count)+v*(j/count)
    result={'protocol':'Both exterior surfaces sampled at <=10 mm edge steps; samples are not an exact Hausdorff proof',
            'grid_step_metres':step,'proxy_to_skin':maximum(quad_points(),skin),
            'skin_to_proxy':maximum(triangle_points(),proxy),
            'exclusion':'Within 0.31 m of three mount axes, top y>body_half_height-0.011 and bottom y<-body_half_height+0.211; pegs/wells remain in enclosure volume'}
    if max(result[key]['maximum_sampled_distance_metres'] for key in ('proxy_to_skin','skin_to_proxy'))>.08:
        raise ValueError('sampled main-shell distance exceeds 80 mm')
    return result


def export(obj, source, lod, metric_profile=False):
    activate(obj)
    triangulate = obj.modifiers.new('export_triangles', 'TRIANGULATE')
    apply_modifier(obj,triangulate)
    path=source/f'pontoon-lod-{lod}.glb'
    settings={'export_format':'GLB','use_selection':True,'export_yup':True,'export_apply':True,
              'export_materials':'EXPORT','export_normals':True,'export_texcoords':True,'export_tangents':True,
              'export_animations':False,'export_skins':False,'export_morph':False,'export_cameras':False,
              'export_lights':False,'export_extras':False,'export_vertex_color':'NONE','export_all_vertex_colors':False}
    bpy.ops.export_scene.gltf(filepath=str(path), **settings)
    data=path.read_bytes(); length,kind=struct.unpack_from('<I4s',data,12)
    if kind != b'JSON':raise ValueError('unexpected GLB envelope')
    document=json.loads(data[20:20+length])
    vertices=triangles=0
    for mesh in document.get('meshes',[]):
        for primitive in mesh['primitives']:
            if primitive.get('mode',4)!=4:raise ValueError('non-triangle export')
            vertices+=document['accessors'][primitive['attributes']['POSITION']]['count']
            triangles+=document['accessors'][primitive['indices']]['count']//3
    material_limit=6 if metric_profile else 1
    if len(document.get('meshes',[]))!=1 or not 1<=len(document.get('materials',[]))<=material_limit:
        raise ValueError('unexpected mesh/material count per LOD')
    if any(m.get('doubleSided',False) for m in document['materials']):raise ValueError('double-sided export')
    triangle_limit,vertex_limit,_,_=params.LOD_LIMITS[lod]
    if triangles>triangle_limit or vertices>vertex_limit:
        raise ValueError(f'LOD{lod} budget exceeded: {triangles} triangles/{vertices} vertices')
    return {'file':path.name,'sha256':params.sha256(path),'bytes':len(data),'triangles':triangles,
            'vertices':vertices,'materials':len(document['materials']),'meshes':1,'export_settings':settings,
            'metric_materials':metric.check_export(document,lod) if metric_profile else None}


def texture_density(obj, size):
    world_area=uv_area=0.
    for triangle in obj.data.polygons:
        a,b,c=(obj.data.vertices[i].co for i in triangle.vertices)
        world_area+=(b-a).cross(c-a).length/2
        a,b,c=(obj.data.uv_layers.active.data[i].uv for i in triangle.loop_indices)
        uv_area+=abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2
    return {'mesh_surface_square_metres':world_area,'summed_triangle_uv_area':uv_area,
            'area_weighted_rms_texels_per_metre':size*math.sqrt(uv_area/world_area),
            'qualification':'UV triangle area includes intentional atlas reuse; this is density, not unique texture coverage'}


def metadata_helpers(spec, metadata):
    collection=bpy.data.collections.new('gameplay_metadata_not_exported')
    bpy.context.scene.collection.children.link(collection)
    for kind,rows in (('collision',metadata['part']['collision']),('buoyancy',[v['box'] for v in metadata['part']['buoyancy']])):
        for row in rows:
            helper=bpy.data.objects.new(f"{kind}_{row['id']}",None);collection.objects.link(helper)
            helper.empty_display_type='CUBE';helper.hide_render=True
            helper.location=to_blender([v/50 for v in row['frame']['translation_ticks']])
            helper.scale=[abs(v) for v in to_blender([v/50 for v in row['half_extents_ticks']])]
            helper['canonical_json']=json.dumps(row,sort_keys=True)
    for row in metadata['part']['sockets']:
        helper=bpy.data.objects.new('socket_'+row['id'],None);collection.objects.link(helper)
        helper.empty_display_type='ARROWS';helper.empty_display_size=.18;helper.hide_render=True
        helper.location=to_blender([v/50 for v in row['frame']['translation_ticks']])
        basis=Matrix(((-1,0,0),(0,0,1),(0,1,0)))
        if row['role']=='receptacle':basis=basis @ Matrix(((1,0,0),(0,-1,0),(0,0,-1)))
        helper.rotation_euler=basis.to_euler()
        helper['canonical_json']=json.dumps(row,sort_keys=True)
    collection.hide_viewport=True


def aim(obj, target):
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()


def preview_scene(spec, objects, preview, mode):
    scene=bpy.context.scene
    scene.render.engine='CYCLES'; scene.cycles.device='CPU'; scene.cycles.samples=96
    scene.cycles.use_denoising=False; scene.cycles.seed=0
    scene.render.threads_mode='FIXED'; scene.render.threads=4
    scene.render.resolution_x=640; scene.render.resolution_y=640; scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_mode='RGBA'
    scene.view_settings.view_transform='AgX'; scene.view_settings.exposure=0
    scene.world.use_nodes=True; scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.55,.62,.69,1)
    scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.38
    for obj in objects:
        obj.hide_render=obj!=objects[0]
        obj.hide_set(obj!=objects[0])
    bpy.ops.mesh.primitive_plane_add(size=200,location=to_blender((0,-params.half_dimensions(spec)[1]-.025,0)))
    floor=bpy.context.object; floor.name='preview_floor_not_exported'
    surface=bpy.data.materials.new('preview_backdrop'); surface.diffuse_color=(.14,.18,.20,1); surface.use_nodes=True
    surface.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.14,.18,.20,1)
    surface.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.72
    floor.data.materials.append(surface)
    for name,point,power,size in (('key',(-3,6,-4),1500,5),('fill',(4,3,2),850,4),('rim',(-2,4,5),1100,3),('under',(0,-4,0),300,3)):
        bpy.ops.object.light_add(type='AREA',location=to_blender(point))
        light=bpy.context.object; light.name='preview_'+name; light.data.energy=power; light.data.shape='DISK';light.data.size=size
        aim(light,(0,0,0))
    bpy.ops.object.camera_add(location=to_blender((4,3,-6)))
    camera=bpy.context.object;camera.name='preview_camera';camera.data.type='ORTHO';camera.data.ortho_scale=5.8
    aim(camera,to_blender((0,.10,0)));scene.camera=camera
    if mode=='none': return
    views=[('thumbnail',(4,3,-6)),('front',(0,1,-7)),('right',(7,1,0)),('rear',(0,1,7)),
           ('left',(-7,1,0)),('top',(0,7,0)),('bottom',(0,-7,1))]
    for name,point in views:
        floor.hide_render=name in ('bottom','top')
        camera.location=to_blender(point);aim(camera,to_blender((0,.05,0)))
        scene.render.filepath=str(preview/(name+'.png'))
        bpy.ops.render.render(write_still=True)
    floor.hide_render=True
    camera.location=to_blender((1.4,-1.9,-2.8));aim(camera,to_blender((0,-params.half_dimensions(spec)[1],0)))
    camera.data.ortho_scale=3.0;scene.render.filepath=str(preview/'socket-wells.png');bpy.ops.render.render(write_still=True)
    camera.location=to_blender((1.5,2.5,-2.8));aim(camera,to_blender((0,params.half_dimensions(spec)[1],0)))
    camera.data.ortho_scale=3.1;scene.render.filepath=str(preview/'socket-pegs.png');bpy.ops.render.render(write_still=True)
    # Actual .96 m engaged spacing, plus an exploded view of the same modeled wells/pegs.
    twin=objects[0].copy();twin.data=objects[0].data;bpy.context.collection.objects.link(twin);twin.name='preview_engagement_copy'
    twin.hide_render=False;twin.hide_set(False)
    camera.location=to_blender((4,2.4,-5));aim(camera,to_blender((0,.55,0)));camera.data.ortho_scale=6.1
    for name,spacing in (('engaged',spec['body_height_ticks']/50),('exploded',spec['body_height_ticks']/50+.55)):
        twin.location=to_blender((0,spacing,0));scene.render.filepath=str(preview/(name+'.png'));bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(twin,do_unlink=True)
    floor.hide_render=False;camera.location=to_blender((4,3,-6));aim(camera,to_blender((0,.1,0)));camera.data.ortho_scale=5.8
    for lod in (1,2):
        objects[0].hide_render=True;objects[lod].hide_render=False;objects[lod].hide_set(False)
        scene.render.filepath=str(preview/f'lod-{lod}.png');bpy.ops.render.render(write_still=True)
        objects[lod].hide_render=True;objects[lod].hide_set(True)
    objects[0].hide_render=False
    if mode=='full':
        frames=preview/'turntable-frames';frames.mkdir()
        for frame in range(72):
            objects[0].rotation_euler.z=frame*2*math.pi/72
            scene.render.filepath=str(frames/f'{frame:03d}.png');bpy.ops.render.render(write_still=True)
        objects[0].rotation_euler.z=0
    scene.render.filepath='//../preview/thumbnail.png'


def main():
    args=arguments();spec=args.parameters;out=args.output_dir
    source=out/'source';source.mkdir();maps=source/'maps';maps.mkdir();preview=out/'preview';preview.mkdir()
    (source/'parameters.json').write_text(json.dumps(spec,indent=2)+'\n')
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    objects=[];metrics=[]
    for lod,(_,_,size,_) in enumerate(params.LOD_LIMITS):
        obj=shell_geometry(spec,lod)
        stats=check_shell(obj,spec)
        metric_profile=args.material_profile==materials.TACTILE_METRIC
        obj.data.materials.clear()
        if metric_profile:
            surfaces=metric.create(spec['palette_srgb'],maps,lod,png)
            regions=list(materials.OPAQUE_RESPONSE)
            for region in regions:obj.data.materials.append(surfaces[region])
            for face in obj.data.polygons:face.material_index=regions.index(classify_face(face,obj,spec,lod))
            # Boolean shell n-gons can be slightly nonplanar. Preserve their
            # material domain, then chart the actual exported triangles.
            apply_modifier(obj,obj.modifiers.new('metric_chart_triangles','TRIANGULATE'))
            metric.project(obj)
        else:
            unwrap(obj,spec,lod if args.material_profile != materials.LEGACY else None)
            obj.data.materials.append(material(textures(spec,size,maps,lod,args.material_profile),lod,args.material_profile))
            for face in obj.data.polygons:face.material_index=0
        exported=export(obj,source,lod,metric_profile)
        exported['pretriangulation_enclosure']=stats
        exported['enclosure']=check_shell(obj,spec)
        exported['main_shell_contact_error']=contact_error(obj,spec)
        if metric_profile:exported['metric_uv']=metric.measure(obj,lod)
        else:exported['base_color_texel_density']=texture_density(obj,size)
        metrics.append(exported)
        obj.name=f'pontoon_lod_{lod}';objects.append(obj)
    metadata=params.sidecar(spec,source)
    (source/'pontoon.gameplay.json').write_text(json.dumps(metadata,indent=2)+'\n')
    metadata_helpers(spec,metadata)
    preview_scene(spec,objects,preview,args.preview)
    for image in bpy.data.images:
        if image.source=='FILE':image.pack()
    bpy.ops.wm.save_as_mainfile(filepath=str(source/'pontoon.blend'),compress=True)
    manifest={'schema':1,'status':'candidate; offline authoring only, not engine acceptance',
        'part':metadata['part']['key'],'parameters':spec,'seed':spec['seed'],
        'blender_version':bpy.app.version_string,'blender_build_hash':bpy.app.build_hash.decode(),
        'generator_sha256':params.sha256(Path(__file__)),'parameter_module_sha256':params.sha256(Path(params.__file__)),
        'material_profile':args.material_profile,'material_module_sha256':params.sha256(Path(materials.__file__)),
        'metric_module_sha256':params.sha256(Path(metric.__file__)),
        'input_spec_sha256':params.sha256(args.spec),'lods':metrics,
        'lod_budgets': [{'triangles':t,'exported_vertices':v,'texture_side_pixels':s,
                         'minimum_screen_height_pixels':h} for t,v,s,h in params.LOD_LIMITS],
        'cross_lod_mount_clearance':params.cross_lod_clearances(),
        'authoring_frame':'+Z up, -Y forward; canonical right is Blender -X',
        'export_to_canonical_rotation':12,'uv_quantization':('none; metric repeating charts' if args.material_profile==materials.TACTILE_METRIC else '1/65536 before export tangent generation'),
        'maps':('Linear palette factors, metric repeating normal/MR detail at Near/Middle; Far keeps material identity without detail images'
                if args.material_profile == materials.TACTILE_METRIC else
                'Flat sRGB palette and linear MR at every LOD; no normal/grain until metric UV detail is authored'
                if args.material_profile == materials.OPAQUE_CALIBRATION else
                'Original deterministic procedural sRGB color and separate linear normal/MR; no baked lighting, no external imagery'),
        'provenance':'Original project geometry/script/maps; concept used as art-direction reference only',
        'render':{'engine':'Cycles','device':'CPU','samples':96,'threads':4,'preset':args.preview,'view_transform':'AgX'},
        'outputs':{str(p.relative_to(out)):{'sha256':params.sha256(p),'bytes':p.stat().st_size}
                   for p in sorted(out.rglob('*')) if p.is_file()}}
    (out/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('SALVAGE_PONTOON_CANDIDATE',json.dumps({'output':str(out),'lods':metrics},sort_keys=True))

if __name__=='__main__':
    main()
