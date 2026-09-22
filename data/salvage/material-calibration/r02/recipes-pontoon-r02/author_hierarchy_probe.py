"""Blender-only ASSET-04 hierarchy, shared-mesh and outward-winding fixture.

Run with --background --factory-startup --python-exit-code 1. Golden geometry
comes from Blender's evaluated world matrices BEFORE export, never from VMESH.
The original translation-only authoring/runtime probes remain unchanged.
"""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authoring_probe as base


def write_json(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n")


def transform(obj, translation, degrees, scale):
    obj.location = translation
    obj.rotation_euler = tuple(math.radians(value) for value in degrees)
    obj.scale = scale


def main():
    if not bpy.app.background or "--factory-startup" not in sys.argv:
        raise RuntimeError("Use --background --factory-startup; no interactive scene is modified")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    output = args.output_dir.absolute()
    if output.is_symlink() or (output.exists() and any(output.iterdir())):
        parser.error("output-dir must be absent or empty")
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    surfaces = [base.material(name, color) for name, color in [
        ("base_white", (.7, .7, .7)), ("positive_x_red", (.75, .025, .025)),
        ("positive_y_green", (.025, .65, .055)), ("forward_minus_z_blue", (.025, .12, .8)),
        ("nested_shared_yellow", (.8, .5, .025)),
    ]]
    for surface in surfaces:
        surface.use_backface_culling = True
    # Full Blender -> canonical mapping is (-x,z,y). glTF exports (x,z,-y);
    # the sidecar supplies only the remaining proper Y rotation, index 12.
    meshes = [
        base.box("axis_base", (0, 0, -.04), (.3, .3, .08), surfaces[0]),
        base.box("axis_positive_x", (-.3, 0, .03), (.6, .04, .04), surfaces[1]),
        base.box("axis_positive_y", (0, 0, .4), (.04, .04, .8), surfaces[2]),
        base.box("axis_forward_minus_z", (0, -.5, .03), (.04, 1., .04), surfaces[3]),
    ]
    root = bpy.data.objects.new("nested_root", None)
    child = bpy.data.objects.new("nested_child", None)
    for obj in (root, child):
        scene.collection.objects.link(obj)
    child.parent = root
    transform(root, (-1.5, 0, .3), (20, 0, 35), (1.25, .75, 1.10))
    transform(child, (.1, -.35, .1), (0, 25, -15), (.8, 1.2, .9))
    leaf_a = base.box("nested_leaf_a", (0, 0, 0), (1, 1, 1), surfaces[4])
    leaf_b = bpy.data.objects.new("nested_leaf_b", leaf_a.data)
    scene.collection.objects.link(leaf_b)
    for obj in (leaf_a, leaf_b):
        obj.parent = child
    transform(leaf_a, (0, 0, .5), (10, 20, 0), (.25, .15, .55))
    transform(leaf_b, (-.65, .3, .35), (-20, 0, 65), (.3, .1, .7))
    meshes += [leaf_a, leaf_b]
    bpy.context.view_layer.update()
    canonical = Matrix(((-1, 0, 0), (0, 0, 1), (0, 1, 0)))
    golden = {"schema": 1, "coordinate_frame": "canonical_y_up_minus_z_forward",
              "origin": "Blender evaluated world geometry captured before glTF export",
              "position_tolerance_metres": .0001, "normal_tolerance": .0002,
              "normal_tolerance_reason": "Blender 5.2.1 glTF exporter rounds local normals to four decimal places before normalization; this fixture permits less than 0.012 degrees after nested transforms",
              "nodes": []}
    for obj in meshes:
        obj.data.calc_loop_triangles()
        matrix = canonical.to_4x4() @ obj.matrix_world
        normal_matrix = matrix.to_3x3().inverted().transposed()
        triangles = []
        for triangle in obj.data.loop_triangles:
            polygon = obj.data.polygons[triangle.polygon_index]
            if polygon.use_smooth:
                raise RuntimeError("This independent flat-normal oracle requires flat faces")
            points = [matrix @ obj.data.vertices[index].co for index in triangle.vertices]
            normal = (normal_matrix @ polygon.normal).normalized()
            if (points[1] - points[0]).cross(points[2] - points[0]).dot(normal) <= 0:
                raise RuntimeError("Authoring fixture contains inward or degenerate triangles")
            triangles.append({"positions": [list(point) for point in points], "normal": list(normal)})
        golden["nodes"].append({"name": obj.name, "mesh_name": obj.data.name,
                                "parent": obj.parent.name if obj.parent else None,
                                "canonical_matrix_rows": [list(row) for row in matrix],
                                "triangles": triangles})
    write_json(output / "golden.json", golden)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes + [root, child]:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    settings = {"export_format": "GLB", "use_selection": True, "export_yup": True,
                "export_apply": True, "export_materials": "EXPORT", "export_normals": True,
                "export_texcoords": True, "export_tangents": True, "export_animations": False,
                "export_cameras": False, "export_lights": False, "export_extras": False}
    if bpy.ops.export_scene.gltf(filepath=str(output / "hierarchy.glb"), **settings) != {"FINISHED"}:
        raise RuntimeError("Export failed")
    document = base.glb_json(output / "hierarchy.glb")
    nodes = {node["name"]: node for node in document["nodes"]}
    if (len(document["meshes"]) != 5 or len(nodes) != 8
            or nodes["nested_leaf_a"]["mesh"] != nodes["nested_leaf_b"]["mesh"]):
        raise RuntimeError("Exporter did not retain five meshes, eight nodes and shared geometry")
    for parent, children in [("nested_root", ["nested_child"]),
                             ("nested_child", ["nested_leaf_a", "nested_leaf_b"])]:
        actual = {document["nodes"][index]["name"] for index in nodes[parent]["children"]}
        if actual != set(children) or not all(key in nodes[parent] for key in ("rotation", "scale")):
            raise RuntimeError("Nontrivial hierarchy was flattened or lost")
    if any(material.get("doubleSided", False) for material in document["materials"]):
        raise RuntimeError("Fixture must require outward-facing geometry")
    # Conservative diagnostic-only box. No promise of playable collision,
    # flotation or economy: this part is never installed in the player catalog.
    source_template = Path(__file__).with_name("fixtures") / "probe.gameplay.json"
    metadata = json.loads(source_template.read_text())
    namespace = b"voxy-hier-probe1"
    assert len(namespace) == 16
    part = metadata["part"]
    part["key"] = {"namespace": namespace.hex(), "counter": "1", "version": 1}
    part["name_key"] = "inspection.hierarchy_probe"
    points = [point for node in golden["nodes"] for tri in node["triangles"] for point in tri["positions"]]
    # Round outwards to even ticks so the center and half extents are integral.
    low = [math.floor(min(point[axis] for point in points) * 25) * 2 - 2 for axis in range(3)]
    high = [math.ceil(max(point[axis] for point in points) * 25) * 2 + 2 for axis in range(3)]
    center = [(a + b) // 2 for a, b in zip(low, high)]
    half = [(b - a) // 2 for a, b in zip(low, high)]
    part["footprint"] = {"minimum_ticks": low, "maximum_ticks": high}
    for index, box in enumerate([part["solid_occupancy"][0], part["collision"][0], part["buoyancy"][0]["box"]]):
        box.update(id=str(index + 1), frame={"translation_ticks": center, "rotation": 0}, half_extents_ticks=half)
    part["mass"]["center_of_mass_metres"] = [value / 50 for value in center]
    lengths = [value / 25 for value in half]
    inertia = [10 * (lengths[(axis + 1) % 3] ** 2 + lengths[(axis + 2) % 3] ** 2) / 12 for axis in range(3)]
    part["mass"]["inertia_kg_metres_squared"] = [inertia[row] if row == col else 0 for row in range(3) for col in range(3)]
    part["sockets"][0]["id"] = "1"
    part["sockets"][0]["frame"]["translation_ticks"] = [0, 0, 0]
    metadata["tool_anchors"] = []
    metadata["lods"] = [{"id": "1", "asset": {"namespace": namespace.hex(), "counter": "2", "version": 1},
                         "source": {"file": "hierarchy.glb", "sha256": base.sha256(output / "hierarchy.glb"),
                                    "bytes": (output / "hierarchy.glb").stat().st_size,
                                    "frame": "exported_gltf", "to_canonical_rotation": 12},
                         "minimum_screen_height_pixels": 0}]
    write_json(output / "hierarchy.gameplay.json", metadata)
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.seed = 0
    scene.cycles.use_denoising = False
    scene.render.threads_mode = "FIXED"
    scene.render.threads = 4
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world.color = (.15, .15, .15)
    scene.view_settings.view_transform = "AgX"
    corners = [obj.matrix_world @ Vector(point) for obj in meshes for point in obj.bound_box]
    center = sum(corners, Vector()) / len(corners)
    bpy.ops.object.camera_add(location=center + Vector((-4, -5, 4)))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.type = "ORTHO"
    base.orient_at(camera, center)
    bpy.context.view_layer.update()
    view = camera.matrix_world.inverted()
    camera.data.ortho_scale = 2.24 * max(abs((view @ point)[axis]) for point in corners for axis in (0, 1))
    scene.camera = camera
    for location, power in [((-1, -3, 7), 1100), ((-4, 0, 4), 550)]:
        bpy.ops.object.light_add(type="AREA", location=location)
        bpy.context.object.data.energy = power
        bpy.context.object.data.size = 5
        base.orient_at(bpy.context.object, center)
    scene.render.filepath = "//preview.png"
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "hierarchy.blend"), compress=True)
    bpy.ops.render.render(write_still=True)
    manifest = {"schema": 1, "asset": "inspection.hierarchy_probe.v1", "blender_version": bpy.app.version_string,
                "blender_build_hash": bpy.app.build_hash.decode(), "export_settings": settings,
                "provenance": "Original procedural geometry; no external references or generated raster inputs",
                "review_status": "Technical diagnostic only; runtime and visual review tracked separately",
                "geometry": {"unique_meshes": 5, "mesh_instances": 6, "nodes": 8,
                             "instanced_triangles": sum(len(node["triangles"]) for node in golden["nodes"])},
                "source_hashes": {str(path.relative_to(Path(__file__).resolve().parents[2])): base.sha256(path)
                                  for path in [Path(__file__).resolve(), Path(base.__file__).resolve(), source_template.resolve()]},
                "outputs": {name: {"sha256": base.sha256(output / name), "bytes": (output / name).stat().st_size}
                            for name in ("hierarchy.glb", "hierarchy.blend", "hierarchy.gameplay.json", "golden.json", "preview.png")}}
    write_json(output / "manifest.json", manifest)
    print("SALVAGE_HIERARCHY_PROBE_OK", json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
