#!/usr/bin/env python3
"""Author original reusable home pieces in background Blender; no screenshots.

Canonical geometry is +Y up/-Z forward. Unlike the older Cove exporter, this
recipe maps (x,y,z) to Blender (x,-z,y), so glTF is already canonical (basis 0).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
import bmesh

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_catalog import SOURCE, validated_catalog

PALETTE = {'cream': ('F0DDB2', .42, 0), 'teal': ('197D86', .36, 0),
           'wood': ('AF794E', .56, 0), 'coral': ('ED7942', .38, 0),
           'slate': ('253D53', .55, .1)}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def linear(value):
    v = value / 255
    return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4


def materials():
    result = {}
    for key, (hexcolor, roughness, metal) in PALETTE.items():
        m = bpy.data.materials.new('adventure_' + key)
        m.use_nodes = True
        m.use_backface_culling = True
        p = m.node_tree.nodes.get('Principled BSDF')
        p.inputs['Base Color'].default_value = tuple(linear(int(hexcolor[i:i+2], 16)) for i in (0, 2, 4)) + (1.,)
        p.inputs['Roughness'].default_value = roughness
        p.inputs['Metallic'].default_value = metal
        result[key] = m
    return result


def activate(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


class Model:
    def __init__(self, name, lod, mats):
        self.name, self.lod, self.mats, self.objects = name, lod, mats, []

    def mesh(self, name, points, faces, color, bevel=0):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([(x, -z, y) for x, y, z in points], [], faces)
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(mesh)
        bm.free()
        obj.data.materials.append(self.mats[color])
        if bevel:
            activate(obj)
            mod = obj.modifiers.new('Molded edge', 'BEVEL')
            mod.width = bevel
            mod.segments = 2 if self.lod == 0 else 1
            bpy.ops.object.modifier_apply(modifier=mod.name)
        self.objects.append(obj)
        return obj

    def box(self, name, lo, hi, color, bevel=.012):
        points = [(lo[0] if not i & 1 else hi[0], lo[1] if not i & 2 else hi[1],
                   lo[2] if not i & 4 else hi[2]) for i in range(8)]
        faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4),
                 (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
        thickness = min(hi[i] - lo[i] for i in range(3))
        # Sub-centimetre inset/paint relief needs only six planar faces;
        # spending curved-edge vertices there does not improve its silhouette.
        bevel = 0 if thickness <= .04 else min(bevel, thickness * .2)
        return self.mesh(name, points, faces, color, bevel)

    def stud(self, x, y, z, color):
        n = 16 if self.lod == 0 else 8
        points = [(x + .30 * math.cos(i * 2 * math.pi / n), y + h,
                   z + .30 * math.sin(i * 2 * math.pi / n)) for h in (0, .18) for i in range(n)]
        faces = [tuple(range(n)), tuple(range(n, n*2))]
        faces += [(i, (i+1) % n, (i+1) % n+n, i+n) for i in range(n)]
        return self.mesh('Stud', points, faces, color, .004)

    def finish(self):
        activate(self.objects[0])
        for obj in self.objects:
            obj.select_set(True)
        if len(self.objects) > 1:
            bpy.ops.object.join()
        obj = bpy.context.object
        obj.name = self.name
        obj.data.name = self.name
        tri = obj.modifiers.new('Export triangles', 'TRIANGULATE')
        bpy.ops.object.modifier_apply(modifier=tri.name)
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        degenerate = sum(f.calc_area() < 1.e-12 for f in bm.faces)
        open_edges = sum(not e.is_manifold for e in bm.edges)
        volume = bm.calc_volume(signed=True)
        bm.free()
        assert not degenerate and not open_edges and volume > 0
        points = [(v.co.x, v.co.z, -v.co.y) for v in obj.data.vertices]
        return obj, dict(degenerate_faces=degenerate, nonmanifold_edges=open_edges,
                         summed_component_volume_m3=volume,
                         bounds=dict(minimum=[min(p[k] for p in points) for k in range(3)],
                                     maximum=[max(p[k] for p in points) for k in range(3)]))


def piece_model(piece, lod, mats):
    key = piece['key']
    model = Model(f'{piece["id"]:02d}_{key}', lod, mats)
    main = {'foundation': 'teal', 'pier': 'teal', 'floor': 'wood', 'roof': 'teal',
            'wall': 'cream', 'doorway': 'teal', 'stair': 'wood', 'beam': 'wood',
            'bed': 'wood', 'chest': 'wood', 'workbench': 'wood'}.get(key, 'cream')
    for i, box in enumerate(piece['solids']):
        lo, hi = ([v / 50 for v in box[k]] for k in ('minimum', 'maximum'))
        color = 'teal' if key == 'bed' and i == 4 else main
        model.box('Body', lo, hi, color)
        if key == 'stair':
            # Inset moulding, flush with actual walkable tread. Never a raised stud.
            model.box('Tread edge', [lo[0]+.03, hi[1]-.025, hi[2]-.04],
                      [hi[0]-.03, hi[1]+.002, hi[2]-.008], 'cream', .003)
    low, high = ([v / 50 for v in piece['bounds'][k]] for k in ('minimum', 'maximum'))
    if key in ('wall', 'doorway'):
        # Shallow outer mouldings retain the continuous authoritative body.
        # They extend no more than .012m and cannot cover the doorway opening.
        for side in (-1, 1):
            z = side * .16
            for row in range(3):
                for x in (-.5, .5):
                    if key == 'doorway' and row < 2:
                        continue
                    y0, y1 = row*.96+.018, (row+1)*.96-.018
                    if key == 'doorway':
                        y0 = max(y0, 2.255)
                    model.box('Molded wall panel', [x-.484, y0, z-.008],
                              [x+.484, y1, z+.008], 'cream', .003)
    if key in ('floor', 'roof', 'foundation'):
        top = high[1]
        for x in (-.75, -.25, .25, .75):
            model.box('Top panel', [x-.243, top-.009, -.974],
                      [x+.243, top+.002, .974], 'wood' if key == 'floor' else 'teal', .002)
        for z in (-.997, .997):
            model.box('Edge rail', [-.982, .04, z-.009], [.982, .16, z+.009], 'teal', .003)
    if key.startswith('brick_'):
        for x in range(int(low[0]*2)+1, int(high[0]*2), 2):
            for z in range(int(low[2]*2)+1, int(high[2]*2), 2):
                model.stud(x*.5, high[1], z*.5, 'cream')
        # Plain embossed colour tab distinguishes a toy block from masonry.
        model.box('Molded tab', [low[0]+.08, .11, low[2]-.006],
                  [low[0]+.32, .28, low[2]+.006], 'teal', .003)
    if key == 'bed':
        model.box('Pillow', [-.36, .634, -.80], [.36, .652, -.44], 'cream', .003)
        model.box('Folded cover', [-.45, .628, .13], [.45, .644, .88], 'coral', .003)
    if key == 'chest':
        for x in (-.32, .32):
            model.box('Chest band', [x-.052, .045, -.506], [x+.052, .938, -.494], 'teal', .002)
        model.box('Latch', [-.08, .53, -.512], [.08, .70, -.488], 'coral', .006)
        model.box('Lid seam', [-.49, .746, -.507], [.49, .763, -.493], 'slate', .002)
    if key == 'workbench':
        model.box('Crafting inlay', [-.76, .952, -.30], [.76, .962, .30], 'teal', .002)
        model.box('Tool inset', [-.55, .958, -.08], [.35, .966, .08], 'coral', .002)
    if key == 'pier':
        for y in (.12, .78):
            model.box('Pier band', [-.246, y, -.246], [.246, y+.06, .246], 'cream', .004)
    return model.finish()


def order_glb(path):
    # Exporter ordering is not a content identity. Bind every mesh explicitly to
    # its stable numerical piece prefix, then make the runtime index kind-1.
    raw = path.read_bytes()
    size, kind = struct.unpack_from('<II', raw, 12)
    assert kind == 0x4e4f534a
    doc = json.loads(raw[20:20+size])
    assert len(doc['meshes']) == len(doc['nodes']) == 14
    order = sorted(range(14), key=lambda i: int(doc['meshes'][i]['name'][:2]))
    remap = {old: new for new, old in enumerate(order)}
    doc['meshes'] = [doc['meshes'][i] for i in order]
    for node in doc['nodes']:
        assert 'children' not in node
        node['mesh'] = remap[node['mesh']]
        assert 'rotation' not in node and 'translation' not in node and 'scale' not in node
    doc['nodes'].sort(key=lambda node: node['mesh'])
    doc['scenes'][0]['nodes'] = list(range(14))
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    tail = raw[20+size:]
    path.write_bytes(struct.pack('<III', 0x46546c67, 2, 20+len(text)+len(tail))
                     + struct.pack('<II', len(text), 0x4e4f534a) + text + tail)
    return doc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup')
    out = args.output_dir.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        parser.error('requires a new output directory with existing parent')
    out.mkdir()
    doc = validated_catalog()
    records = []
    for lod in range(2):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = 'METRIC'
        mats = materials()
        pieces = []
        for p in doc['pieces']:
            obj, check = piece_model(p, lod, mats)
            pieces.append(dict(id=p['id'], key=p['key'], mesh_index=p['id']-1, **check))
        bpy.ops.object.select_all(action='SELECT')
        blend = out / f'building-kit-lod{lod}.blend'
        glb = out / f'building-kit-lod{lod}.glb'
        bpy.ops.wm.save_as_mainfile(filepath=str(blend), compress=True)
        bpy.ops.export_scene.gltf(filepath=str(glb), export_format='GLB', use_selection=True,
            export_yup=True, export_apply=True, export_materials='EXPORT', export_normals=True,
            export_texcoords=False, export_tangents=False, export_animations=False, export_skins=False,
            export_morph=False, export_cameras=False, export_lights=False, export_extras=False,
            export_vertex_color='NONE', export_all_vertex_colors=False)
        exported = order_glb(glb)
        primitives = [p for m in exported['meshes'] for p in m['primitives']]
        assert all(len(m['primitives']) <= 4 for m in exported['meshes'])
        assert len(exported['materials']) <= 5
        vertices = sum(exported['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)
        triangles = sum(exported['accessors'][p['indices']]['count']//3 for p in primitives)
        assert vertices <= 22000 and triangles <= 18000
        records.append(dict(lod=lod, glb=glb.name, blend=blend.name, glb_sha256=sha(glb),
                            blend_sha256=sha(blend), vertices=vertices, triangles=triangles,
                            draws=len(primitives), pieces=pieces))
        print(json.dumps(dict(lod=lod, vertices=vertices, triangles=triangles, draws=len(primitives))), flush=True)
    provenance = dict(schema=1, asset_id=doc['asset_id'], profile='salvage-rigid-v1',
        render_to_canonical=0, catalog_sha256=sha(SOURCE), blender_version=bpy.app.version_string,
        author_sha256=sha(__file__), palette_srgb={k:v[0] for k,v in PALETTE.items()}, lods=records,
        provenance='Original procedural home kit. No external models, textures, or generated images. '
                   'Editable Blender and canonical JSON sources. No capture performed.',
        collision=doc['collision'], detail='Near: two bevel segments, sixteen-sided studs. '
            'Far: one bevel segment, eight-sided studs. Both preserve solids, openings and furniture.')
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')


if __name__ == '__main__':
    main()
