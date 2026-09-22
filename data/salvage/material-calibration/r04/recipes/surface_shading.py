"""Opt-in manufactured-surface normals; never changes mesh positions.

Broad faces use area/angle weighted normals with authored hard-edge boundaries.
Torus rings use their analytical surface normals. This is a candidate shading
recipe, not asset acceptance; compare exported geometry and real engine views.
"""
import math

import bpy
from mathutils import Vector

FLAT = 'flat'
MANUFACTURED = 'manufactured-v1'
ANALYTIC = 'manufactured-v2'
PROFILES = (FLAT, MANUFACTURED, ANALYTIC)


def apply(obj, profile=MANUFACTURED):
    mesh=obj.data;mesh.update()
    before=[tuple(v.co) for v in mesh.vertices]
    faces_by_edge=[[] for _ in mesh.edges]
    for face in mesh.polygons:
        face.use_smooth=True
        for index in face.loop_indices:
            faces_by_edge[mesh.loops[index].edge_index].append(face)
    torus=obj.get('surface_kind')=='torus'
    threshold=math.cos(math.radians(50))
    for edge,faces in zip(mesh.edges,faces_by_edge):
        edge.use_edge_sharp=not torus and (len(faces)!=2 or faces[0].normal.dot(faces[1].normal)<threshold)
    kind=obj.get('surface_kind')
    analytic=profile==ANALYTIC and kind in ('rounded_box','rounded_cylinder')
    if torus or analytic:
        # Recipe coordinates are canonical (+Y up); map to Blender's proper
        # authoring basis (-X, Z, Y), matching the original torus geometry.
        center=Vector(obj['surface_center']);normals=[]
        axis=int(obj.get('surface_axis',0));axes=[k for k in range(3) if k!=axis]
        for face in mesh.polygons:
            for index in face.loop_indices:
                v=mesh.vertices[mesh.loops[index].vertex_index].co
                p=Vector((-v.x,v.z,v.y))-center
                if kind=='rounded_box':
                    bevel=float(obj['surface_bevel'])
                    if not bevel:
                        normals.append(tuple(face.normal));continue
                    core=[h-bevel for h in obj['surface_half']]
                    n=Vector([p[k]-max(-core[k],min(core[k],p[k])) for k in range(3)])
                else:
                    radius=float(obj['surface_radius']);radial=math.hypot(p[axes[0]],p[axes[1]])
                    if torus:
                        if radial<=0:raise ValueError('torus shading requires a nonzero major radius')
                        n=p.copy()
                        for k in axes:n[k]-=p[k]*radius/radial
                    else:
                        bevel=float(obj['surface_bevel']);half=float(obj['surface_depth'])/2
                        # Blender offsets the polygon's side planes. Convert
                        # that offset to a radial distance at their vertices.
                        core=radius-bevel/math.cos(math.pi/int(obj['surface_segments']))
                        axial=max(0,abs(p[axis])-(half-bevel))
                        if not bevel:
                            # Unbeveled caps and barrel share positions but
                            # have intentionally separate corner normals.
                            if abs(face.normal[(0,2,1)[axis]])>.999:
                                normals.append(tuple(face.normal));continue
                            radial_part=1.;axial=0.
                        else:radial_part=max(0,radial-core)
                        n=Vector((0,0,0));n[axis]=math.copysign(axial,p[axis])
                        for k in axes:n[k]=p[k]*radial_part/radial if radial else 0
                if n.length_squared<1e-16:raise ValueError('undefined analytic surface normal')
                n.normalize();normals.append((-n.x,n.z,n.y))
        mesh.normals_split_custom_set(normals)
        method='analytic '+kind+' normals'
    else:
        modifier=obj.modifiers.new('manufactured_surface_normals','WEIGHTED_NORMAL')
        modifier.mode='FACE_AREA_WITH_ANGLE';modifier.weight=50;modifier.keep_sharp=True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        mesh=obj.data
        method='face area / corner angle weighted; hard edge above 50 degrees'
    mesh.update()
    if before!=[tuple(v.co) for v in mesh.vertices]:raise ValueError('surface shading changed vertex positions')
    if not mesh.has_custom_normals:raise ValueError('surface recipe failed to retain custom normals')
    minimum_dot=1.;maximum_length_error=0.
    for face in mesh.polygons:
        for index in face.loop_indices:
            normal=mesh.corner_normals[index].vector
            maximum_length_error=max(maximum_length_error,abs(normal.length-1))
            minimum_dot=min(minimum_dot,normal.dot(face.normal))
    if maximum_length_error>1e-4 or minimum_dot<=0:
        raise ValueError(f'invalid manufactured shading normals: {minimum_dot}, {maximum_length_error}')
    return dict(component=obj.name,method=method,sharp_edges=sum(e.use_edge_sharp for e in mesh.edges),
                minimum_face_alignment=minimum_dot,maximum_unit_length_error=maximum_length_error,
                positions_unchanged=True)
