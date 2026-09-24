#!/usr/bin/env python3
"""Render the installed building-kit triangles into deterministic menu SVGs.

No approximate collision boxes or generated concept art are substituted for the
actual model. Run from any directory; the source and outputs are repository files.
"""
import hashlib
import json
import math
import argparse
from pathlib import Path
import struct
from check_door import inspect as inspect_door

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'data/adventure/building-kit-r01/cooked/building-kit-lod0.vmesh'
DOOR_SOURCE = ROOT / 'data/adventure/door-r01/cooked/door-leaf-lod0.vmesh'
COLORS = {'adventure_cream': 'E8D8B8', 'adventure_teal': '627052',
          'adventure_wood': 'A5784F', 'adventure_coral': 'B76343', 'adventure_slate': '494D48'}
NATIVE = ROOT / 'src/render/generated/adventure_piece_thumbnails.hpp'
RECIPE = 'adventure-installed-mesh-thumbnails-r03'
DOOR_COLORS = {'adventure_door_terracotta': 'B85C45', 'adventure_door_brass': 'C69A49'}

def dot(a, b):
    return sum(x*y for x, y in zip(a, b))

def unit(v):
    length = math.sqrt(dot(v, v))
    return tuple(x/length for x in v)

def camera_basis():
    camera = unit((6, 4.5, -7))
    right = unit((-camera[2], 0, camera[0]))
    # right × camera points toward world +Y. SVG's downward screen Y is
    # applied once, in the projection below; camera × right inverted every icon.
    up = (-right[2]*camera[1], right[2]*camera[0]-right[0]*camera[2], right[0]*camera[1])
    return camera, right, up

def door_triangles(camera, right, up, material_offset):
    # The door picture combines the actual frame triangles with this checked,
    # closed leaf mesh, using the same camera basis as the other pieces.
    inspect_door(DOOR_SOURCE)
    raw = DOOR_SOURCE.read_bytes()
    h = struct.unpack_from('<14I13Q', raw, 8)
    _, _, nv, vs, ni, stride, ns, nm = h[:8]
    vo, io, so, mo, _, _, _, _, _, _, strings = h[14:25]
    names = lambda offset: raw[strings+offset:raw.index(0, strings+offset)].decode()
    materials = [names(struct.unpack_from('<I', raw, mo+i*128+124)[0]) for i in range(nm)]
    colors = [tuple(int(color[i:i+2],16)/255 for i in (0,2,4)) for color in (DOOR_COLORS[name] for name in materials)]
    vertices = [struct.unpack_from('<3f',raw,vo+i*vs) for i in range(nv)]
    normals = [struct.unpack_from('<3f',raw,vo+i*vs+12) for i in range(nv)]
    indices = struct.unpack_from('<'+('H' if stride == 2 else 'I')*ni,raw,io)
    triangles = []
    for submesh in range(ns):
        start,count,material,_ = struct.unpack_from('<4I',raw,so+submesh*16)
        for k in range(start//stride,start//stride+count,3):
            ids=indices[k:k+3];points=[vertices[i] for i in ids]
            normal=unit(tuple(sum(normals[i][axis] for i in ids) for axis in range(3)))
            if dot(normal,camera)<=0:continue
            triangles.append((sum(dot(p,camera) for p in points)/3,
                [(dot(p,right),-dot(p,up)) for p in points],normal,material_offset+material))
    return colors,triangles

def render_outputs():
    raw = SOURCE.read_bytes()
    assert raw[:8] == b'VOXYMESH'
    header = struct.unpack_from('<14I13Q', raw, 8)
    version, _, nv, vs, ni, stride, ns, nm, nn, nmesh, nskin, nj, na, nac = header[:14]
    vo, io, so, mo, _, no, _, _, _, _, strings, _, size = header[14:]
    assert version == 1 and vs == 72 and stride in (2, 4) and nn == nmesh == 14
    assert not any((nskin, nj, na, nac)) and size == len(raw)
    names = lambda offset: raw[strings+offset:raw.index(0, strings+offset)].decode()
    materials = [names(struct.unpack_from('<I', raw, mo+i*128+124)[0]) for i in range(nm)]
    assert set(materials) == set(COLORS), 'Update the named warm palette deliberately when assets change.'
    colors = [tuple(int(COLORS[name][i:i+2], 16)/255 for i in (0, 2, 4)) for name in materials]
    vertices = [struct.unpack_from('<3f', raw, vo+i*vs) for i in range(nv)]
    normals = [struct.unpack_from('<3f', raw, vo+i*vs+12) for i in range(nv)]
    indices = struct.unpack_from('<'+('H' if stride == 2 else 'I')*ni, raw, io)
    subs = [struct.unpack_from('<4I', raw, so+i*16) for i in range(ns)]
    camera, right, up = camera_basis()
    light = unit((-3, 7, -5))
    door_colors,leaf_triangles=door_triangles(camera,right,up,len(colors));colors+=door_colors
    outputs = [];files = {};native_triangles = [];native_pieces = []
    frame_triangles = []
    for mesh in range(15):
        if mesh<14:
            node = struct.unpack_from('<10fiIiI', raw, no+mesh*64)
            assert node[:10] == (0.,0.,0.,0.,0.,0.,1.,1.,1.,1.) and node[10:13] == (-1,mesh,-1)
            name = names(node[13]);assert name.startswith(f'{mesh+1:02d}_')
        else:name='15_hinged_door'
        triangles = []
        for start, count, material, owner in subs:
            if owner != mesh:
                continue
            for k in range(start//stride, start//stride+count, 3):
                ids = indices[k:k+3];points = [vertices[i] for i in ids]
                normal = unit(tuple(sum(normals[i][axis] for i in ids) for axis in range(3)))
                if dot(normal, camera) <= 0:
                    continue
                triangles.append((sum(dot(p,camera) for p in points)/3,
                    [(dot(p,right), -dot(p,up)) for p in points], normal, material))
        if mesh==3:frame_triangles=triangles.copy()
        if mesh==14:triangles=frame_triangles+leaf_triangles
        assert triangles
        xs = [p[0] for tri in triangles for p in tri[1]];ys = [p[1] for tri in triangles for p in tri[1]]
        x0,x1,y0,y1 = min(xs),max(xs),min(ys),max(ys)
        scale = min(112/(x1-x0),88/(y1-y0))
        paths=[];first = len(native_triangles)
        for _, points, normal, material in sorted(triangles, key=lambda t:t[0]):
            shade = .64 + .36*max(0,dot(normal,light))
            color='#'+''.join(f'{round(channel*shade*255):02x}' for channel in colors[material])
            xy=[(64+(x-(x0+x1)/2)*scale,52+(y-(y0+y1)/2)*scale) for x,y in points]
            paths.append('<path fill="'+color+'" d="M'+' L'.join(f'{x:.2f},{y:.2f}' for x,y in xy)+' Z"/>')
            positions = tuple(round(c*256) for p in xy for c in p)
            assert all(0 <= c < 65536 for c in positions)
            rgb = bytes.fromhex(color[1:])
            rgba = int.from_bytes(rgb+b'\xff', 'little')
            native_triangles.append((positions,rgba))
        svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 108">\n'+''.join(paths)+'\n</svg>\n'
        filename=f'adventure_piece_{mesh+1:02d}.svg';output=ROOT/'web'/filename;files[output] = svg
        outputs.append({'pieceKind':mesh+1,'mesh':name,'file':filename,'visibleTriangles':len(paths),
                        'sha256':hashlib.sha256(svg.encode()).hexdigest()})
        native_pieces.append((first,len(paths),name))
    assert len(native_triangles) < 65536
    source_digest=hashlib.sha256(raw).hexdigest()
    door_digest=hashlib.sha256(DOOR_SOURCE.read_bytes()).hexdigest()
    packed=b''.join(struct.pack('<6HI',*positions,color) for positions,color in native_triangles)
    header='''// Generated by tools/adventure_assets/generate_piece_thumbnails.py. Do not edit.
// Actual installed mesh triangles; depth-sorted, backfaces removed. Same view and
// shaded sRGB palette as browser SVGs. Coordinate quantization is 1/256 pixel.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace voxy::render::adventure_thumbnails {
struct Triangle {std::array<uint16_t,6> xy;uint32_t rgba;};
struct Piece {uint16_t first,count;std::string_view mesh;};
inline constexpr uint32_t width=128,height=108,units=256;
'''
    header+=f'inline constexpr std::string_view recipe="{RECIPE}";\n'
    header+=f'inline constexpr std::string_view sourceSha256="{source_digest}";\n'
    header+=f'inline constexpr std::string_view doorSourceSha256="{door_digest}";\n'
    header+=f'inline constexpr std::string_view packedSha256="{hashlib.sha256(packed).hexdigest()}";\n'
    header+=f'inline constexpr uint32_t maximumPieceTriangles={max(p[1] for p in native_pieces)};\n'
    header+='inline constexpr std::array<Piece,15> pieces{{\n'
    header+=''.join(f'    {{{first},{count},"{name}"}},\n' for first,count,name in native_pieces)+'}};\n'
    header+=f'inline constexpr std::array<Triangle,{len(native_triangles)}> triangles{{{{\n'
    header+=''.join('    {{'+','.join(map(str,p))+'},0x'+f'{color:08x}'+'},\n' for p,color in native_triangles)+'}};\n'
    header+='static_assert(sizeof(Triangle)==16);\n} // namespace voxy::render::adventure_thumbnails\n'
    files[NATIVE]=header
    manifest={'source':str(SOURCE.relative_to(ROOT)), 'sourceSha256':hashlib.sha256(raw).hexdigest(),
              'doorSource':str(DOOR_SOURCE.relative_to(ROOT)),'doorSourceSha256':door_digest,
              'doorComposition':{'pieceKind':15,'frameMeshIndex':3,'leafMeshIndex':0,'leafPose':'closed part-local identity'},
              'method':'Orthographic CPU projection of installed mesh triangles with named runtime warm palette.',
              'paletteSrgb':{**COLORS,**DOOR_COLORS},'outputs':outputs,
              'native':{'file':str(NATIVE.relative_to(ROOT)), 'sha256':hashlib.sha256(header.encode()).hexdigest(),
                        'recipe':RECIPE,'coordinateUnitsPerPixel':256,'visibleTriangles':len(native_triangles),
                        'maximumPieceTriangles':max(p[1] for p in native_pieces),
                        'packedSha256':hashlib.sha256(packed).hexdigest()}}
    files[ROOT/'web/adventure_piece_manifest.json']=json.dumps(manifest,indent=2)+'\n'
    return files

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true',help='Verify every generated output against the installed mesh without writing.')
    args=parser.parse_args();files=render_outputs()
    if args.check:
        stale=[str(path.relative_to(ROOT)) for path,text in files.items() if not path.exists() or path.read_text()!=text]
        if stale:raise SystemExit('Out-of-date mesh thumbnails: '+', '.join(stale))
        print('Verified browser and native thumbnails against the installed triangle mesh.')
    else:
        for path,text in files.items():path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
        print('Wrote 15 upright browser thumbnails and the matching compact native triangle table.')

if __name__ == '__main__':
    main()
