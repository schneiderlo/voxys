#!/usr/bin/env python3
"""Reduce only the fixed Blacksmith remainder; retain movable mesh nodes."""

import argparse
import os
import sys
from pathlib import Path

import bpy


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--input", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
assert bpy.app.background and "--factory-startup" in sys.argv
assert args.input.resolve() != args.output.resolve()

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(args.input.resolve()))
meshes = sorted(
    (obj for obj in bpy.data.objects if obj.type == "MESH"),
    key=lambda obj: obj.name,
)
assert len(meshes) == 11
assert meshes[0].name == "00_remainder"
before = [(obj.name, len(obj.data.vertices), len(obj.data.polygons)) for obj in meshes]

remainder = meshes[0]
modifier = remainder.modifiers.new("Static remainder detail", "DECIMATE")
modifier.decimate_type = "COLLAPSE"
modifier.ratio = 0.2
modifier.use_collapse_triangulate = True
bpy.context.view_layer.objects.active = remainder
bpy.ops.object.modifier_apply(modifier=modifier.name)
after = [(obj.name, len(obj.data.vertices), len(obj.data.polygons)) for obj in meshes]
assert before[1:] == after[1:]
assert after[0][2] < before[0][2] // 4

args.output.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.export_scene.gltf(
    filepath=str(args.output.resolve()),
    export_format="GLB",
    export_yup=True,
    export_apply=True,
    export_materials="EXPORT",
    export_normals=True,
    export_texcoords=False,
    export_tangents=False,
    export_animations=False,
    export_skins=False,
    export_morph=False,
    export_cameras=False,
    export_lights=False,
    export_extras=False,
    export_vertex_color="NONE",
    export_all_vertex_colors=False,
)
print(f"BLACKSMITH_REMAINDER {before[0][2]} -> {after[0][2]} triangles", flush=True)
# Snap Blender can hang during headless audio shutdown after writing the GLB.
sys.stdout.flush()
os._exit(0)
