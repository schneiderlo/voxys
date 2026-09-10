#!/usr/bin/env python3
"""Inspect actual exported GLB color/MR at socket walls and winch flanges.

This focused authoring diagnostic uses Pillow and reads identity-node GLBs
from these recipes. It is not a general importer or a runtime visual gate.
"""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import struct

from PIL import Image


def subtract(a, b):
    return tuple(x-y for x, y in zip(a, b))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


class ExportedModel:
    def __init__(self, path):
        data = path.read_bytes()
        assert struct.unpack_from('<4sII', data) == (b'glTF', 2, len(data))
        size, kind = struct.unpack_from('<II', data, 12)
        assert kind == 0x4e4f534a
        doc = json.loads(data[20:20+size])
        offset = 20+size
        bin_size, bin_kind = struct.unpack_from('<II', data, offset)
        assert bin_kind == 0x004e4942
        binary = data[offset+8:offset+8+bin_size]
        assert len(doc['nodes']) == len(doc['meshes']) == 1
        assert set(doc['nodes'][0]) <= {'name', 'mesh'} and doc['nodes'][0]['mesh'] == 0
        assert len(doc['meshes'][0]['primitives']) == 1
        primitive = doc['meshes'][0]['primitives'][0]

        def accessor(index):
            a = doc['accessors'][index]; view = doc['bufferViews'][a['bufferView']]
            assert 'sparse' not in a and not a.get('normalized', False)
            components = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
            fmt = '<' + {5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']] * components
            stride = view.get('byteStride', struct.calcsize(fmt))
            start = view.get('byteOffset', 0)+a.get('byteOffset', 0)
            return [struct.unpack_from(fmt, binary, start+i*stride) for i in range(a['count'])]

        # Identity export nodes, followed by the declared canonical rotation 12.
        self.positions = [(-x, y, -z) for x,y,z in accessor(primitive['attributes']['POSITION'])]
        self.uvs = accessor(primitive['attributes']['TEXCOORD_0'])
        indices = [v[0] for v in accessor(primitive['indices'])]
        self.triangles = [indices[i:i+3] for i in range(0, len(indices), 3)]
        self.material = doc['materials'][primitive['material']]
        self.pbr = self.material.get('pbrMetallicRoughness', {})

        def texture(name):
            if name not in self.pbr:
                return None
            entry = self.pbr[name]; assert entry.get('texCoord', 0) == 0
            image = doc['images'][doc['textures'][entry['index']]['source']]
            view = doc['bufferViews'][image['bufferView']]
            start = view.get('byteOffset', 0)
            return Image.open(BytesIO(binary[start:start+view['byteLength']])).convert('RGBA')

        self.base = texture('baseColorTexture')
        self.mr = texture('metallicRoughnessTexture')
        self.sha256 = hashlib.sha256(data).hexdigest()

    def sample(self, origin, direction):
        closest = None
        for indices in self.triangles:
            a,b,c = [self.positions[i] for i in indices]
            e1,e2 = subtract(b,a),subtract(c,a)
            p = cross(direction,e2); determinant = dot(e1,p)
            if abs(determinant) < 1e-10:
                continue
            tvec = subtract(origin,a); u = dot(tvec,p)/determinant
            q = cross(tvec,e1); v = dot(direction,q)/determinant
            t = dot(e2,q)/determinant
            if u < -1e-7 or v < -1e-7 or u+v > 1+1e-7 or t < 1e-6:
                continue
            if closest is None or t < closest[0]:
                uv = tuple(sum(w*self.uvs[i][k] for w,i in zip((1-u-v,u,v),indices)) for k in (0,1))
                closest = (t,uv)
        assert closest is not None, (origin,direction)
        distance,uv = closest

        def texel(image):
            return image.getpixel((int((uv[0]%1)*image.width),int((uv[1]%1)*image.height)))

        base = texel(self.base) if self.base else (255,255,255,255)
        mr = texel(self.mr) if self.mr else (255,255,255,255)
        return dict(origin=origin,direction=direction,distance=distance,uv=uv,
                    color=base,roughness=mr[1]/255*self.pbr.get('roughnessFactor',1),
                    metallic=mr[2]/255*self.pbr.get('metallicFactor',1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pontoon', required=True, type=Path)
    parser.add_argument('--winch', required=True, type=Path)
    parser.add_argument('--old-pontoon', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    assert not args.report.exists()
    result = dict(status='running',scope='Exported GLB ray/nearest base-level texel samples; no engine or filtering acceptance',checks=[])
    try:
        old_failures = 0
        for kind, directory in [('old',args.old_pontoon),('candidate',args.pontoon)]:
            for lod in range(3):
                file = directory/f'pontoon-lod-{lod}.glb'
                model = ExportedModel(file)
                samples = [model.sample((0,-.38,z),direction) for z in (-1,0,1)
                           for direction in [(1,0,0),(-1,0,0),(0,0,1),(0,0,-1),(0,1,0)]]
                wrong = sum(max(abs(x-y) for x,y in zip(s['color'][:3],(42,103,103))) > 3 for s in samples)
                if kind == 'candidate':
                    assert wrong == 0, (lod,wrong,samples)
                    assert model.mr is not None and 'normalTexture' not in model.material
                else:
                    old_failures += wrong
                result['checks'].append(dict(kind=kind,part='pontoon',lod=lod,source=str(file),source_sha256=model.sha256,wrong_colors=wrong,samples=samples))
        assert old_failures > 0, 'Retained candidate must reproduce the palette defect'
        for lod in range(3):
            file = args.winch/f'winch-lod-{lod}.glb'; model = ExportedModel(file)
            sample = model.sample((.265,.12,.32),(-1,0,0))
            assert sample['distance'] < .02 and sample['metallic'] == 1
            assert abs(sample['roughness']-.30) <= 1/255
            assert model.mr is not None and 'normalTexture' not in model.material
            result['checks'].append(dict(kind='candidate',part='winch',lod=lod,source=str(file),source_sha256=model.sha256,samples=[sample]))
        result.update(status='passed',old_wrong_color_samples=old_failures,candidate_well_samples=45,candidate_metal_samples=3)
    except Exception as error:
        result.update(status='failed',error=str(error));raise
    finally:
        args.report.write_text(json.dumps(result,indent=2)+'\n')


if __name__ == '__main__':
    main()
