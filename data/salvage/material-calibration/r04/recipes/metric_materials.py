"""Blender material recipe with metric repeating UVs and coherent height normals.

Color is a linear material factor; two small shared source tiles carry detail.
The current renderer may duplicate those tiles per material: count that cost.
"""
import math
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

import material_standards as standards

PERIOD_METRES = .5
TILE_SIZES = (128, 64, 0)
RUBBER_SRGB = '232D30'


def linear_channel(value):
    value /= 255
    return value/12.92 if value <= .04045 else ((value+.055)/1.055)**2.4


def detail_tiles(size):
    """A periodic height field and its analytic derivatives, in physical units.

    Fixed Fourier modes make the source reproducible and tile continuously.
    Slopes derive from the same height; they are not unrelated RGB noise.
    """
    y,x = np.indices((size,size),dtype=np.float64)
    # PNG rows start at the top. Blender's authored V increases upward; its
    # glTF exporter flips UV V but preserves the authored tangent/bitangent.
    # Evaluate both height and derivatives in authored UV coordinates.
    u,v = (x+.5)/size,1-(y+.5)/size
    height=np.zeros_like(u);du=np.zeros_like(u);dv=np.zeros_like(u)
    # Repeat period .5 m; wavelengths span approximately 16–71 mm. The
    # micron-scale amplitudes produce restrained molded-surface variation.
    modes=((5,5,9.,.13),(7,-3,8.,.71),(11,4,6.,.39),(-5,13,5.,.83),
           (17,7,4.,.27),(9,-19,3.,.59),(23,-11,2.,.91),(-17,25,2.,.47))
    for fx,fy,microns,phase in modes:
        angle=2*math.pi*(fx*u+fy*v+phase)
        amplitude=microns*1e-6
        height+=amplitude*np.cos(angle)
        du-=amplitude*(2*math.pi*fx/PERIOD_METRES)*np.sin(angle)
        dv-=amplitude*(2*math.pi*fy/PERIOD_METRES)*np.sin(angle)
    normal=np.stack((-du,-dv,np.ones_like(u)),axis=-1)
    normal/=np.linalg.norm(normal,axis=-1,keepdims=True)
    rgba=np.empty((size,size,4),dtype=np.uint8)
    rgba[:,:,:3]=np.rint((normal*.5+.5)*255).clip(0,255).astype(np.uint8)
    rgba[:,:,3]=255
    mr=np.full_like(rgba,255)
    # Mean .97; per-family material factor compensates for this mean. R/B/A
    # are neutral multipliers, so conductivity is set by the material itself.
    roughness=.97+.025*np.sin(2*math.pi*(3*u-2*v+.31))*np.cos(2*math.pi*(u+4*v+.67))
    mr[:,:,1]=np.rint(roughness*255).astype(np.uint8)
    return rgba,mr


def create(palette, maps: Path, lod, write_png):
    images={}
    size=TILE_SIZES[lod]
    if size:
        for name,values in zip(('normal','metallic_roughness'),detail_tiles(size)):
            path=maps/f'metric-lod-{lod}-{name}.png';write_png(path,values)
            image=bpy.data.images.load(str(path),check_existing=False)
            image.colorspace_settings.name='Non-Color';image.pack();images[name]=image
    result={}
    responses={**standards.OPAQUE_RESPONSE,'rubber':dict(roughness=.78,metallic=0.)}
    colors={**palette,'rubber':RUBBER_SRGB}
    for name,response in responses.items():
        mat=bpy.data.materials.new(f'salvage_metric_{name}_lod{lod}')
        mat.use_nodes=True;mat.use_backface_culling=True
        shader=mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value=tuple(linear_channel(int(colors[name][i:i+2],16)) for i in (0,2,4))+(1.,)
        shader.inputs['Metallic'].default_value=response['metallic']
        shader.inputs['Roughness'].default_value=response['roughness']
        if size:
            normal_image=mat.node_tree.nodes.new('ShaderNodeTexImage');normal_image.image=images['normal']
            normal_image.extension='REPEAT';normal_image.interpolation='Linear'
            normal=mat.node_tree.nodes.new('ShaderNodeNormalMap')
            mat.node_tree.links.new(normal_image.outputs['Color'],normal.inputs['Color'])
            mat.node_tree.links.new(normal.outputs['Normal'],shader.inputs['Normal'])
            mr=mat.node_tree.nodes.new('ShaderNodeTexImage');mr.image=images['metallic_roughness']
            mr.extension='REPEAT';mr.interpolation='Linear'
            split=mat.node_tree.nodes.new('ShaderNodeSeparateColor');split.mode='RGB'
            mat.node_tree.links.new(mr.outputs['Color'],split.inputs['Color'])
            # glTF's supported metallic/roughness factor multiplies this map;
            # Blender exports Math MULTIPLY as that standard factor.
            factor=mat.node_tree.nodes.new('ShaderNodeMath');factor.operation='MULTIPLY'
            factor.inputs[1].default_value=response['roughness']/.97
            mat.node_tree.links.new(split.outputs['Green'],factor.inputs[0])
            mat.node_tree.links.new(factor.outputs[0],shader.inputs['Roughness'])
            conductor=mat.node_tree.nodes.new('ShaderNodeMath');conductor.operation='MULTIPLY'
            conductor.inputs[1].default_value=response['metallic']
            mat.node_tree.links.new(split.outputs['Blue'],conductor.inputs[0])
            mat.node_tree.links.new(conductor.outputs[0],shader.inputs['Metallic'])
        result[name]=mat
    return result


def project(obj):
    """Orthonormal planar charts, one UV unit per half metre, without packing.

    Molded hard edges are chart boundaries. Charts share detail tiles; palette
    changes use material slots, so there is no atlas-region bleed or stretch.
    """
    obj.data.update()
    layer=obj.data.uv_layers.active or obj.data.uv_layers.new(name='UV0')
    layer.name='UV0'
    transform=obj.matrix_world
    normal_transform=transform.to_3x3().inverted().transposed()
    for face in obj.data.polygons:
        normal=(normal_transform@face.normal).normalized()
        reference=Vector((1,0,0)) if abs(normal.x)<.8 else Vector((0,1,0))
        u=(reference-normal*reference.dot(normal)).normalized();v=normal.cross(u)
        for index in face.loop_indices:
            point=transform@obj.data.vertices[obj.data.loops[index].vertex_index].co
            layer.data[index].uv=(point.dot(u)/PERIOD_METRES,point.dot(v)/PERIOD_METRES)


def check_export(document, lod):
    """Reject an exporter that drops or changes this profile's PBR inputs."""
    responses={**standards.OPAQUE_RESPONSE,'rubber':dict(roughness=.78,metallic=0.)}
    rows=[]
    for material in document.get('materials',[]):
        name=material['name'].removeprefix('salvage_metric_').split('_lod')[0]
        response=responses[name];pbr=material['pbrMetallicRoughness']
        roughness=response['roughness']/(.97 if TILE_SIZES[lod] else 1.)
        if abs(pbr.get('roughnessFactor',1)-roughness)>1e-6 or abs(pbr.get('metallicFactor',1)-response['metallic'])>1e-6:
            raise ValueError(f'{name}: exporter changed material factors')
        if material.get('alphaMode','OPAQUE')!='OPAQUE' or material.get('doubleSided',False):
            raise ValueError(f'{name}: invalid opaque material')
        if 'baseColorTexture' in pbr:
            raise ValueError('metric profile must use an unlit palette factor')
        for container,key in ((pbr,'metallicRoughnessTexture'),(material,'normalTexture')):
            if (key in container)!=bool(TILE_SIZES[lod]):
                raise ValueError(f'{name}: unexpected or missing {key}')
            if key in container:
                texture=document['textures'][container[key]['index']]
                sampler=document.get('samplers',[])[texture['sampler']] if 'sampler' in texture else {}
                if sampler.get('wrapS',10497)!=10497 or sampler.get('wrapT',10497)!=10497:
                    raise ValueError(f'{name}: metric detail must repeat')
        rows.append(dict(region=name,roughness_factor=pbr.get('roughnessFactor',1),metallic_factor=pbr.get('metallicFactor',1)))
    if not rows:raise ValueError('metric export has no materials')
    return rows


def measure(obj,lod):
    """Actual triangle Jacobians; report principal stretch and physical density."""
    obj.data.calc_loop_triangles();layer=obj.data.uv_layers.active
    area_sum=density_area=0.;maximum_stretch=1.;minimum=math.inf;maximum=0.;tiny=0
    for triangle in obj.data.loop_triangles:
        a,b,c=[obj.matrix_world@obj.data.vertices[i].co for i in triangle.vertices]
        e1,e2=b-a,c-a;area=e1.cross(e2).length*.5
        if area<1e-10:
            tiny+=1;continue
        axis=e1.normalized();x=e1.length;z=e2.dot(axis);h=(e2-axis*z).length
        ua,ub,uc=[layer.data[i].uv for i in triangle.loops]
        j1=(ub-ua)/x;j2=((uc-ua)-j1*z)/h
        aa=j1.dot(j1);bb=j1.dot(j2);cc=j2.dot(j2)
        delta=math.sqrt(max(0.,(aa-cc)**2+4*bb*bb))
        low=max(0.,(aa+cc-delta)*.5);high=(aa+cc+delta)*.5
        if low<=0:raise ValueError('degenerate metric UV triangle')
        stretch=math.sqrt(high/low);density=math.sqrt(math.sqrt(low*high))*128
        maximum_stretch=max(maximum_stretch,stretch);minimum=min(minimum,density);maximum=max(maximum,density)
        area_sum+=area;density_area+=density*density*area
    if maximum_stretch>1.02:
        raise ValueError(f'metric chart stretch {maximum_stretch} exceeds 1.02')
    return dict(period_metres=PERIOD_METRES,tile_pixels=TILE_SIZES[lod],surface_area_m2=area_sum,
                near_equivalent_texels_per_metre=dict(minimum=minimum,maximum=maximum,rms=math.sqrt(density_area/area_sum)),
                maximum_principal_stretch=maximum_stretch,excluded_tiny_triangles=tiny,
                qualification='Near is 256 texels/metre; Middle 128; Far uses material factors without detail textures. Hard-edge charts may differ in phase.')
