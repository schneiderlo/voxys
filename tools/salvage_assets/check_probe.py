"""Validate this trusted local authoring fixture; not a general glTF validator."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(folder):
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["schema"] == 1 and manifest["asset_id"] == "salvage.authoring_probe"
    for name in ("probe.blend", "probe.glb", "preview.png"):
        assert digest(folder / name) == manifest["outputs"][name]["sha256"], f"changed output: {name}"
        assert (folder / name).stat().st_size == manifest["outputs"][name]["bytes"]
    generator = Path(__file__).resolve().with_name("authoring_probe.py")
    assert digest(generator) == manifest["generator_sha256"], "generator does not match manifest"
    data = (folder / "probe.glb").read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data)
    assert (magic, version, length) == (b"glTF", 2, len(data))
    size, kind = struct.unpack_from("<I4s", data, 12)
    assert kind == b"JSON"
    document = json.loads(data[20:20 + size])
    assert not document.get("extensionsRequired"), "unexpected required extension"
    assert not any(document.get(key) for key in ("animations", "skins", "cameras", "images"))
    assert len(document["meshes"]) == 4 and len(document["materials"]) == 4
    counts = []
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            assert primitive.get("mode", 4) == 4
            assert set(primitive["attributes"]) == {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"}
            counts.append(document["accessors"][primitive["indices"]]["count"] // 3)
    assert sum(counts) == manifest["triangle_count"]
    p = manifest["parameters_m"]
    w, length, h = p["width"], p["length"], p["height"]
    expected = {"probe_body": (0, h / 2, 0),
                "probe_forward_minus_y": (w * .25, h + .05, length * .45),
                "probe_right_plus_x": (w * .48, h / 2, -length * .2),
                "probe_up_plus_z": (-w * .3, h + .18, -length * .25)}
    assert {node["name"] for node in document["nodes"]} == set(expected)
    for node in document["nodes"]:
        assert "matrix" not in node and "rotation" not in node and "scale" not in node
        assert max(abs(a - b) for a, b in zip(node["translation"], expected[node["name"]])) < 2e-6
    picture = Image.open(folder / "preview.png").convert("RGBA")
    assert picture.size == (512, 512)
    assert len(picture.getcolors(512 * 512) or []) > 100, "empty/flat preview"
    return {"folder": str(folder), "glb_sha256": digest(folder / "probe.glb"),
            "preview_pixel_sha256": hashlib.sha256(picture.tobytes()).hexdigest(),
            "triangles": sum(counts), "blender": manifest["blender_version"],
            "blender_build_hash": manifest["blender_build_hash"],
            "generator_sha256": manifest["generator_sha256"], "parameters_m": p}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()
    result = check(args.folder.resolve())
    if args.compare:
        other = check(args.compare.resolve())
        for key in ("glb_sha256", "preview_pixel_sha256", "blender", "blender_build_hash",
                    "generator_sha256", "parameters_m"):
            assert result[key] == other[key], f"regeneration differs: {key}"
        result["compared_folder"] = other["folder"]
    print(json.dumps({"status": "passed", **result}, indent=2))


if __name__ == "__main__":
    main()
