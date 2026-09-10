#!/usr/bin/env python3
"""Run and verify continuous camera motion in the actual native application."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def jpeg_size(data):
    assert data[:2] == b"\xff\xd8"
    offset = 2
    while offset < len(data):
        assert data[offset] == 255
        while data[offset] == 255:
            offset += 1
        marker = data[offset]
        offset += 1
        length = int.from_bytes(data[offset:offset + 2], "big")
        assert length >= 2 and offset + length <= len(data)
        if marker in (0xc0, 0xc2):
            return [int.from_bytes(data[offset + 5:offset + 7], "big"),
                    int.from_bytes(data[offset + 3:offset + 5], "big")]
        assert marker not in (0xda, 0xd9), "Missing JPEG frame dimensions"
        offset += length
    raise ValueError("Truncated JPEG")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--recipe", type=Path, default=Path("docs/validation/salvage/ASSET-04/lod-motion.json"))
    parser.add_argument("--config", type=Path, default=Path("salvage_assembly_fixture.cfg"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve(strict=True)
    recipe_path = args.recipe.resolve(strict=True)
    config_path = args.config.resolve(strict=True)
    recipe = json.loads(recipe_path.read_text())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    captures = output / "captures"
    sources = [root / p for p in ["src/app/application.cpp", "src/app/application.hpp",
        "src/engine/platform/native/entry.cpp", "src/engine/platform/native/inspection_motion.hpp",
        "src/core/config.cpp", "src/core/config.hpp", "src/render/salvage_asset_fixture.cpp",
        "src/render/mesh_path.cpp", "shaders/mesh_path.wgsl", recipe["registry"]]]
    sources += [Path(__file__).resolve(), recipe_path, config_path]
    manifest = {"status": "running", "binary": str(binary), "binary_sha256": sha(binary),
                "source_hashes": {str(p): sha(p) for p in sources}}
    command = [str(binary), "--config", str(config_path), "--inspection-motion", str(recipe_path),
               "--inspection-motion-output", str(captures)]
    manifest["argv"] = command
    try:
        with (output / "native.log").open("wb") as log:
            run = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                 timeout=recipe["duration_seconds"] + 100)
        manifest["exit_code"] = run.returncode
        text = (output / "native.log").read_text(errors="replace")
        manifest["errors"] = [line for line in text.splitlines()
            if "[ERROR]" in line or "Validation Error" in line or "Device lost" in line]
        assert run.returncode == 0 and "Main loop ended" in text and not manifest["errors"]
        native_report = captures / "native-report.json"
        report = json.loads(native_report.read_text())
        assert report["status"] == "captured; visual review required"
        assert report["recipe_sha256"] == sha(recipe_path)
        assert report["registry_sha256"] == sha(root / recipe["registry"])
        assert sha(captures / "trace.json") == report["trace"]["sha256"]
        tracked=recipe.get("inspection_expectations",{}).get("tracked_lod_placements",[0,1,2,3])
        assert report["tracked_lod_placements"] == tracked
        forced = recipe.get("forced_lod", 0)
        sequence = [str(forced)] if forced else ["1", "2", "3", "2", "1"]
        assert report["lod_sequences"] == [sequence] * len(tracked)
        assert 30 <= len(report["frames"]) <= recipe["frame_capacity"]
        for frame in report["frames"]:
            assert re.fullmatch(r"frame-[0-9]{4}\.jpg", frame["filename"])
            image = captures / frame["filename"]
            frame["dimensions"] = jpeg_size(image.read_bytes())
            assert frame["dimensions"] == recipe["physical_viewport"]
            frame["sha256"] = sha(image)
        assert sha(binary) == manifest["binary_sha256"]
        assert all(sha(Path(p)) == digest for p, digest in manifest["source_hashes"].items())
        report["native_report_sha256"] = sha(native_report)
        report["verification_runner_sha256"] = sha(Path(__file__))
        (captures / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        manifest.update(status="captured and verified; visual review required",
                        native_report_sha256=sha(native_report), report_sha256=sha(captures / "report.json"),
                        sources_unchanged=True)
    except Exception as error:
        manifest.update(status="failed", error=str(error))
        raise
    finally:
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
