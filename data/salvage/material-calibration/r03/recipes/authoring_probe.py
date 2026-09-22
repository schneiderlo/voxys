"""Run inside Blender. Generate one reproducible, asymmetric pipeline probe.

This is an authoring fixture, not a production pontoon or gameplay sidecar.
Always use --background --factory-startup --python-exit-code 1.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Vector


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--width", type=float, default=2.0)
    parser.add_argument("--length", type=float, default=4.0)
    parser.add_argument("--height", type=float, default=0.64)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    for name in ("width", "length", "height"):
        value = getattr(args, name)
        if not math.isfinite(value) or not 0.1 <= value <= 20.0:
            parser.error(f"{name} must be finite and between 0.1 and 20 metres")
    output = args.output_dir.expanduser().absolute()
    if output.is_symlink() or (output.exists() and any(output.iterdir())):
        parser.error("output-dir must be absent or empty; use a fresh directory to regenerate")
    output.mkdir(parents=True, exist_ok=True)
    args.output_dir = output.resolve()
    return args


def material(name, color, roughness=0.38):
    result = bpy.data.materials.new(name)
    result.use_nodes = True
    result.diffuse_color = (*color, 1.0)
    shader = result.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1.0)
    shader.inputs["Metallic"].default_value = 0.0
    shader.inputs["Roughness"].default_value = roughness
    return result


def box(name, center, dimensions, surface):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.data.name = name + "_mesh"
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    obj.data.materials.append(surface)
    obj.data.uv_layers.active.name = "UV0"
    bevel = obj.modifiers.new("authored_bevel", "BEVEL")
    bevel.width = min(dimensions) * 0.07
    bevel.segments = 3
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    triangulate = obj.modifiers.new("export_triangles", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=triangulate.name)
    # Bevel's interpolated UVs can vary by one float ULP across clean runs.
    # Freeze this probe's UVs before MikkTSpace tangent generation. This is a
    # fixture-specific precision choice, not the final game's texture standard.
    for loop in obj.data.uv_layers.active.data:
        loop.uv = tuple(round(float(value) * 65536) / 65536 for value in loop.uv)
    return obj


def orient_at(obj, point):
    obj.rotation_euler = (Vector(point) - obj.location).to_track_quat("-Z", "Y").to_euler()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def glb_json(path):
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data)
    if magic != b"glTF" or version != 2 or length != len(data):
        raise RuntimeError("export did not produce a valid GLB 2 envelope")
    size, kind = struct.unpack_from("<I4s", data, 12)
    if kind != b"JSON":
        raise RuntimeError("GLB first chunk is not JSON")
    return json.loads(data[20:20 + size])


def main():
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Run with --background --factory-startup; interactive scenes are not modified")
    args = arguments()
    # Factory startup is required at invocation; explicit deletion also makes
    # this script independent of the factory scene's default cube/light/camera.
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"
    cream = material("probe_cream", (0.72, 0.60, 0.32))
    coral = material("probe_forward_coral", (0.8, 0.12, 0.06))
    teal = material("probe_right_teal", (0.04, 0.40, 0.37))
    slate = material("probe_up_slate", (0.12, 0.17, 0.23))
    w, length, h = args.width, args.length, args.height
    meshes = [
        box("probe_body", (0, 0, h / 2), (w, length, h), cream),
        box("probe_forward_minus_y", (w * 0.25, -length * 0.45, h + 0.05),
            (w * 0.2, length * 0.08, 0.1), coral),
        box("probe_right_plus_x", (w * 0.48, length * 0.2, h / 2),
            (w * 0.08, length * 0.1, h * 0.5), teal),
        box("probe_up_plus_z", (-w * 0.3, length * 0.25, h + 0.18),
            (0.15, 0.15, 0.36), slate),
    ]
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.seed = 0
    scene.cycles.use_denoising = False
    scene.render.threads_mode = "FIXED"
    scene.render.threads = 4
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.world.color = (0.15, 0.15, 0.15)
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.exposure = 0
    bpy.context.view_layer.update()
    corners = [obj.matrix_world @ Vector(point) for obj in meshes for point in obj.bound_box]
    low = Vector(tuple(min(point[axis] for point in corners) for axis in range(3)))
    high = Vector(tuple(max(point[axis] for point in corners) for axis in range(3)))
    center = (low + high) / 2
    extent = max(high - low)
    offset = Vector((1.3, -1.6, 1.8)).normalized() * extent * 3
    bpy.ops.object.camera_add(location=center + offset)
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.type = "ORTHO"
    orient_at(camera, center)
    bpy.context.view_layer.update()
    view = camera.matrix_world.inverted()
    # Fit every actual mesh corner, including tall variants and axis markers.
    camera.data.ortho_scale = 2.24 * max(abs((view @ point)[axis]) for point in corners for axis in (0, 1))
    scene.camera = camera
    for name, location, power, size in [
        ("preview_key", (1, -3, 7), 1100, 5),
        ("preview_fill", (-4, 0, 4), 550, 4),
    ]:
        bpy.ops.object.light_add(type="AREA", location=location)
        lamp = bpy.context.object
        lamp.name = name
        lamp.data.energy = power
        lamp.data.shape = "DISK"
        lamp.data.size = size
        orient_at(lamp, (0, 0, h / 2))
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    output = args.output_dir
    export_settings = {
        "export_format": "GLB", "use_selection": True, "export_yup": True,
        "export_apply": True, "export_materials": "EXPORT", "export_normals": True,
        "export_texcoords": True, "export_tangents": True, "export_animations": False,
        "export_cameras": False, "export_lights": False, "export_extras": False,
    }
    exported = bpy.ops.export_scene.gltf(filepath=str(output / "probe.glb"), **export_settings)
    if exported != {"FINISHED"}:
        raise RuntimeError(f"GLB export failed: {exported}")
    document = glb_json(output / "probe.glb")
    if len(document.get("meshes", [])) != len(meshes) or document.get("animations"):
        raise RuntimeError("unexpected exported mesh/animation content")
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            if primitive.get("mode", 4) != 4:
                raise RuntimeError("non-triangle primitive in export")
    scene.render.filepath = "//preview.png"
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "probe.blend"), compress=True)
    bpy.ops.render.render(write_still=True)
    manifest = {
        "schema": 1, "asset_id": "salvage.authoring_probe", "asset_version": 1,
        "purpose": "Authoring fixture only; not a gameplay metadata sidecar or approved game asset",
        "parameters_m": {"width": w, "length": length, "height": h},
        "seed": 0, "blender_version": bpy.app.version_string,
        "blender_build_hash": bpy.app.build_hash.decode(),
        "generator": "tools/salvage_assets/authoring_probe.py",
        "generator_sha256": sha256(Path(__file__)), "export_settings": export_settings,
        "probe_uv_quantization": "1/65536 before tangent generation",
        "authoring_frame": "+Z up, -Y forward; metre scale applied",
        "blender_to_gltf": "(x,y,z) -> (x,z,-y); exporter applies this once",
        "gltf_to_canonical_remaining": [[-1, 0, 0], [0, 1, 0], [0, 0, -1]],
        "canonical_note": "Remaining proper 180-degree Y rotation is metadata only here; ASSET-02/04 implement and verify the engine bridge. Do not apply the raw Blender conversion to this GLB.",
        "materials": "Inline PBR factors, one UV set, no external textures, no painted lighting",
        "mesh_count": len(meshes), "triangle_count": sum(len(obj.data.polygons) for obj in meshes),
        "provenance": "Original procedural geometry authored for this project; no external reference or generated raster inputs",
        "review_status": "authoring smoke only; game import/material/socket checks are later tasks",
        "outputs": {name: {"sha256": sha256(output / name), "bytes": (output / name).stat().st_size}
                    for name in ("probe.blend", "probe.glb", "preview.png")},
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("SALVAGE_AUTHORING_PROBE_OK", json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
