"""Render the final-size thumbnail from an existing packed, authored Blender file.

Run in background/factory-startup mode. The source and earlier previews are
read-only inputs; the review directory must be new. No image-generation model
is involved and no source .blend is saved or modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if not bpy.app.background or '--factory-startup' not in sys.argv:
        parser.error('requires --background --factory-startup')
    candidate = args.candidate.resolve(strict=True)
    source = candidate / 'source/pontoon.blend'
    before = digest(source)
    output = args.output.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('output must be a new directory with an existing parent')
    output.mkdir()
    bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False, use_scripts=False)
    scene = bpy.context.scene
    if scene.camera is None or 'pontoon_lod_0' not in bpy.data.objects:
        raise RuntimeError('not an authored pontoon scene')
    obj = bpy.data.objects['pontoon_lod_0']
    obj.hide_render = False
    obj.hide_set(False)
    obj.rotation_euler = (0, 0, 0)
    for index in (1, 2):
        bpy.data.objects[f'pontoon_lod_{index}'].hide_render = True
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 96
    scene.cycles.seed = 0
    scene.cycles.use_denoising = False
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 4
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    target = output / 'thumbnail.png'
    scene.render.filepath = str(target)
    bpy.ops.render.render(write_still=True)
    if not target.is_file() or digest(source) != before:
        raise RuntimeError('missing render or modified source')
    record = {
        'schema': 1, 'status': 'offline review artifact; no engine or visual acceptance',
        'blender_version': bpy.app.version_string,
        'blender_build_hash': bpy.app.build_hash.decode(),
        'input_blend': str(source), 'input_blend_sha256': before,
        'renderer_script_sha256': digest(Path(__file__)),
        'command': sys.argv,
        'render': {'engine': 'Cycles', 'device': 'CPU', 'samples': 96, 'seed': 0,
                   'threads': 4, 'width': 512, 'height': 512,
                   'view_transform': scene.view_settings.view_transform},
        'output': {'file': target.name, 'bytes': target.stat().st_size, 'sha256': digest(target)},
        'source_unchanged': True,
    }
    (output / 'thumbnail-provenance.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
