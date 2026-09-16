#!/usr/bin/env python3
"""Author the original hinged-door leaf in headless Blender; frozen frame reused."""
import argparse
import json
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from author_building_kit import Model, linear, sha
from generate_catalog import ADDON_SOURCE, validated_door

PALETTE = {'terracotta': ('B85C45', .62, 0), 'brass': ('C69A49', .42, .25)}


def materials():
    result = {}
    for key, (color, roughness, metallic) in PALETTE.items():
        material = bpy.data.materials.new('adventure_door_'+key)
        material.use_nodes = True
        material.use_backface_culling = True
        shader = material.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value = tuple(linear(int(color[i:i+2], 16)) for i in (0, 2, 4))+(1.,)
        shader.inputs['Roughness'].default_value = roughness
        shader.inputs['Metallic'].default_value = metallic
        result[key] = material
    return result


def author(lod):
    model = Model('15_hinged_door_leaf', lod, materials())
    model.box('Molded terracotta leaf', [-.64,.04,-.04], [.64,2.20,.04], 'terracotta', .009)
    # Small molded rails and a graspable-looking brass bar on either face.
    # All relief is <= .019m beyond the .08m collision leaf; no false window.
    for side in (-1, 1):
        lo, hi = sorted((side*.039, side*.048))
        for x in (-.545, .545):
            model.box('Molded upright', [x-.035,.12,lo], [x+.035,2.12,hi], 'terracotta', 0)
        for y in (.165, 1.065, 2.075):
            model.box('Molded cross rail', [-.51,y-.035,lo], [.51,y+.035,hi], 'terracotta', 0)
        lo, hi = sorted((side*.038, side*.052))
        model.box('Handle plate', [.42,1.02,lo], [.50,1.18,hi], 'brass', 0)
        lo, hi = sorted((side*.043, side*.059))
        model.box('Toy handle', [.34,1.092,lo], [.50,1.126,hi], 'brass', 0)
    for y in (.34, 1.65):
        model.box('Molded hinge band', [-.64,y,-.051], [-.604,y+.19,.051], 'brass', .005)
    return model.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup')
    output = args.output_dir.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('requires a new directory with an existing parent')
    definition = validated_door()
    output.mkdir()
    records = []
    for lod in (0, 1):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = 'METRIC'
        obj, geometry = author(lod)
        assert tuple(obj.location) == (0.,0.,0.) and tuple(obj.scale) == (1.,1.,1.)
        blend = output/f'door-leaf-lod{lod}.blend'
        glb = output/f'door-leaf-lod{lod}.glb'
        bpy.ops.wm.save_as_mainfile(filepath=str(blend), compress=True)
        bpy.ops.export_scene.gltf(filepath=str(glb), export_format='GLB', use_selection=True,
            export_yup=True, export_apply=True, export_materials='EXPORT', export_normals=True,
            export_texcoords=False, export_tangents=False, export_animations=False, export_skins=False,
            export_morph=False, export_cameras=False, export_lights=False, export_extras=False,
            export_vertex_color='NONE', export_all_vertex_colors=False)
        records.append(dict(lod=lod, blend=blend.name, glb=glb.name,
                            blend_sha256=sha(blend), glb_sha256=sha(glb), geometry=geometry))
    provenance = dict(schema=1, asset_id=definition['asset_id'], profile='salvage-rigid-v1',
        render_to_canonical=0, catalog_sha256=sha(ADDON_SOURCE),
        blender_version=bpy.app.version_string, author_sha256=sha(__file__),
        helper_sha256=sha(Path(__file__).with_name('author_building_kit.py')),
        leaf_mesh_index=0, leaf_node='15_hinged_door_leaf', hinge_ticks=definition['hinge'],
        closed_leaf_ticks=definition['piece']['solids'][3], open_leaf_ticks=definition['open_leaf'],
        open_yaw_degrees=-90, palette_srgb={k:v[0] for k,v in PALETTE.items()}, lods=records,
        provenance='Original molded construction-toy door leaf. Reuses the unchanged original doorway frame. '
            'No downloaded models, generated images, textures, armature or animation channels.',
        collision='Body .08m thick; molded panels and handle relief extend at most .019m beyond its front/back faces. '
            'No relief extends past the leaf width or height. Closed/open authority is supplied by the session.')
    (output/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    print(json.dumps(dict(source=str(output), lods=len(records))), flush=True)


if __name__ == '__main__':
    main()
