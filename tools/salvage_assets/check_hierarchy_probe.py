"""Check the trusted hierarchy fixture and a second clean author/cook run."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(source, cooked):
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((source / "manifest.json").read_text())
    assert manifest["schema"] == 1 and manifest["asset"] == "inspection.hierarchy_probe.v1"
    for name, expected in manifest["outputs"].items():
        assert sha(source / name) == expected["sha256"], name
        assert (source / name).stat().st_size == expected["bytes"], name
    for name, expected in manifest["source_hashes"].items():
        assert sha(root / name) == expected, name
    data = (source / "hierarchy.glb").read_bytes()
    assert struct.unpack_from("<4sII", data) == (b"glTF", 2, len(data))
    length, kind = struct.unpack_from("<I4s", data, 12)
    assert kind == b"JSON"
    gltf = json.loads(data[20:20 + length])
    assert not any(gltf.get(key) for key in ("extensionsRequired", "animations", "skins", "cameras", "images"))
    assert len(gltf["nodes"]) == 8 and len(gltf["meshes"]) == 5 and len(gltf["materials"]) == 5
    nodes = {node["name"]: node for node in gltf["nodes"]}
    assert nodes["nested_leaf_a"]["mesh"] == nodes["nested_leaf_b"]["mesh"]
    for name, children in [("nested_root", {"nested_child"}), ("nested_child", {"nested_leaf_a", "nested_leaf_b"})]:
        node = nodes[name]
        assert {gltf["nodes"][index]["name"] for index in node["children"]} == children
        assert "rotation" in node and len(set(node["scale"])) == 3
    unique_triangles = 0
    for mesh in gltf["meshes"]:
        for primitive in mesh["primitives"]:
            assert primitive.get("mode", 4) == 4
            assert set(primitive["attributes"]) == {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"}
            unique_triangles += gltf["accessors"][primitive["indices"]]["count"] // 3
    assert unique_triangles == 940
    assert all(not material.get("doubleSided", False) for material in gltf["materials"])
    golden = json.loads((source / "golden.json").read_text())
    assert sum(len(node["triangles"]) for node in golden["nodes"]) == 1128
    picture = Image.open(source / "preview.png").convert("RGBA")
    assert picture.size == (512, 512) and len(picture.getcolors(512 * 512) or []) > 100
    cooked_manifest = json.loads((cooked / "cook-manifest.json").read_text())
    assert cooked_manifest["input_sidecar_sha256"] == sha(source / "hierarchy.gameplay.json")
    assert cooked_manifest["converter"]["profile"] == "salvage-rigid-v1"
    assert len(cooked_manifest["lods"]) == 1
    for entry in [cooked_manifest["normalized_sidecar"], *cooked_manifest["lods"]]:
        assert sha(cooked / entry["file"]) == entry["sha256"]
        assert (cooked / entry["file"]).stat().st_size == entry["bytes"]
    assert cooked_manifest["lods"][0]["source_sha256"] == sha(source / "hierarchy.glb")
    return {"source": str(source), "cooked": str(cooked), "blender": manifest["blender_version"],
            "build": manifest["blender_build_hash"], "source_hashes": manifest["source_hashes"],
            "deterministic_source_hashes": {name: sha(source / name) for name in ("hierarchy.glb", "golden.json", "hierarchy.gameplay.json")},
            "cooked_hashes": {path.name: sha(path) for path in sorted(cooked.iterdir())},
            "preview_pixel_sha256": hashlib.sha256(picture.tobytes()).hexdigest(),
            "container_hashes": {name: sha(source / name) for name in ("hierarchy.blend", "preview.png")}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--cooked", required=True, type=Path)
    parser.add_argument("--repeat-source", required=True, type=Path)
    parser.add_argument("--repeat-cooked", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    first = check(args.source.resolve(), args.cooked.resolve())
    second = check(args.repeat_source.resolve(), args.repeat_cooked.resolve())
    for key in ("blender", "build", "source_hashes", "deterministic_source_hashes", "cooked_hashes", "preview_pixel_sha256"):
        assert first[key] == second[key], f"Clean regeneration differs: {key}"
    report = {"status": "passed", "checker_sha256": sha(Path(__file__)), "first": first, "second": second,
              "scope": "Same Blender build: exact model/golden/sidecar/cooked bytes and decoded pixels; blend/PNG containers recorded, not required equal"}
    with args.report.open("x") as stream:
        stream.write(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
