#!/usr/bin/env python3
"""Capture the shared pontoon inspection recipe in the actual native application."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import uuid


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--guides", choices=("off", "dimensions", "sockets"), default="off")
    parser.add_argument("--lod", type=int, choices=(0, 1, 2, 3), default=0,
                        help="Initial detail: 0 automatic, 1 near, 2 middle, 3 far")
    parser.add_argument("--views", nargs="+", help="Names from the shared view recipe; omitted means all views")
    parser.add_argument("--recipe", type=Path, help="Shared recipe JSON; default is the individual pontoon inspector")
    parser.add_argument("--config", type=Path, help="Matching scene configuration; default is salvage_asset_fixture.cfg")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    recipe_path = args.recipe.resolve(strict=True) if args.recipe else root / "docs/validation/salvage/ASSET-04/inspection-views.json"
    recipe = json.loads(recipe_path.read_text())
    if args.views:
        assert len(set(args.views)) == len(args.views)
        assert set(args.views) <= {view["name"] for view in recipe["views"]}
    selected_views = [view for view in recipe["views"] if not args.views or view["name"] in args.views]
    registry_path = root / recipe["registry"]
    original = json.loads(registry_path.read_text())
    config_path = args.config.resolve(strict=True) if args.config else root / "salvage_asset_fixture.cfg"
    config_text = config_path.read_text()
    temporary_registry = registry_path.parent / ("inspection-views-" + uuid.uuid4().hex + ".json")
    report = {"schema": 1, "status": "running", "recipe": recipe, "recipe_sha256": sha(recipe_path),
              "runner_sha256": sha(Path(__file__)), "binary": str(binary), "binary_sha256": sha(binary),
              "original_registry_sha256": sha(registry_path), "original_config": str(config_path), "original_config_sha256": sha(config_path),
              "guides": args.guides, "forced_lod": args.lod, "selected_views": args.views,
              "scope": "Native scene captures; guides are technical overlays, not final art or moving-craft acceptance", "views": []}
    try:
        # Keep the trusted bundle base directory unchanged. Only this unique,
        # task-owned selection file is created and removed; source stays intact.
        with temporary_registry.open("x") as file:
            file.write("{}")
        for view in selected_views:
            assert re.fullmatch(r"[a-z-]+", view["name"])
            registry = {**original, "camera": {"eye": view["eye"], "target": view["target"]}}
            registry_bytes = (json.dumps(registry, indent=2) + "\n").encode()
            temporary_registry.write_bytes(registry_bytes)
            (output / (view["name"] + ".registry.json")).write_bytes(registry_bytes)
            config = re.sub(r'^asset_fixture_(guides|lod)\s*=.*\n?', '', config_text, flags=re.M)
            config = re.sub(r'^asset_fixture_registry = .*$',
                            'asset_fixture_registry = ' + json.dumps(str(temporary_registry))
                            + '\nasset_fixture_guides = ' + json.dumps(args.guides)
                            + '\nasset_fixture_lod = ' + json.dumps(('auto', 'near', 'middle', 'far')[args.lod]), config, flags=re.M)
            config = re.sub(r'^sun_direction = .*$', 'sun_direction = ' + json.dumps(view["sun"]), config, flags=re.M)
            config_path = output / (view["name"] + ".cfg")
            config_path.write_text(config)
            png = output / (view["name"] + ".png")
            command = [str(binary), "--config", str(config_path), "--screenshot", str(png), "--screenshot-frames", "120"]
            result = {"name": view["name"], "argv": command, "config_sha256": sha(config_path),
                      "registry_sha256": sha(temporary_registry), "timeout": False}
            print(view["name"], flush=True)
            with (output / (view["name"] + ".log")).open("wb") as log:
                try:
                    run = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=90)
                    result["exit_code"] = run.returncode
                except subprocess.TimeoutExpired:
                    result["timeout"] = True
            text = (output / (view["name"] + ".log")).read_text(errors="replace")
            if png.exists():
                header = png.read_bytes()[:24]
                assert header[:8] == b"\x89PNG\r\n\x1a\n"
                result["dimensions"] = list(struct.unpack(">II", header[16:24]))
                result["png_sha256"] = sha(png)
            result["gpu_or_application_errors"] = [line for line in text.splitlines()
                if "[ERROR]" in line or "Validation Error" in line or "Device lost" in line]
            states = re.findall(r'Asset inspection capture state: (\{[^\n]*\})', text)
            assert len(states) == 1, 'Expected the actual state accompanying this submitted-surface capture'
            state = json.loads(states[0])
            result['state'] = state
            asset = state['assetFixture']
            assert state['ready'] and not state['failed'] and state['bodies'] == 0
            assert asset['forcedLod'] == str(args.lod)
            assert asset['guides'] == ('off', 'dimensions', 'sockets').index(args.guides)
            assert asset['uploads'] == sum(len(bundle['lod_limits']) for bundle in original['bundles'])
            assert asset['prototypeUploads'] == len(original.get('prototypes', []))
            maximum = recipe.get('inspection_expectations', {}).get('maximum_model_draws', 512)
            assert 1 <= asset['draws'] <= maximum + asset['guideBoxes']
            assert len(asset['lods']) == len(original['placements'])
            for placement, level in zip(original['placements'], asset['lods']):
                assert (level is None) == ('prototype' in placement)
                if args.lod and level is not None:
                    assert level == str(args.lod)
            if original['schema'] == 2:
                assert asset['assembly'] == dict(parts=len(original['placements']), connections=len(original['connections']))
            else:
                assert 'assembly' not in asset
            result["passed"] = (result.get("exit_code") == 0 and not result["timeout"]
                and result.get("dimensions") == recipe["physical_viewport"]
                and "Saved screenshot to:" in text and "Main loop ended" in text
                and not result["gpu_or_application_errors"] and sha(binary) == report["binary_sha256"])
            report["views"].append(result)
            (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            assert result["passed"], f"Capture failed: {view['name']}"
        assert sha(registry_path) == report["original_registry_sha256"]
        assert sha(recipe_path) == report["recipe_sha256"]
        report["status"] = "captured; visual review required"
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        temporary_registry.unlink(missing_ok=True)
        report["temporary_registry_removed"] = not temporary_registry.exists()
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
