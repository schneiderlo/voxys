#!/usr/bin/env python3
"""Verify native capture refusal before GPU startup or existing-output writes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve(strict=True)
    recipe_path = root / "docs/validation/salvage/ASSET-04/lod-motion.json"
    recipe = json.loads(recipe_path.read_text())
    report = {"status": "running", "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "cases": []}
    with tempfile.TemporaryDirectory(prefix="voxys-native-motion-cli-") as temporary:
        directory = Path(temporary)
        existing = directory / "existing"
        existing.mkdir()
        marker = existing / "owner.txt"
        marker.write_text("Preserve this existing output.\n")
        bad_recipe = directory / "bad.json"
        bad_recipe.write_text(json.dumps({**recipe, "trace_capacity": 8193}))
        mismatch = directory / "mismatch.json"
        mismatch.write_text(json.dumps({**recipe, "registry": "data/salvage/fixture-pontoon-v2.json"}))
        bad_workload = directory / "bad-workload.json"
        workload = dict(parts=6, connections=5, uploads=3, prototype_uploads=1,
                        model_draws=6, tracked_lod_placements=[0, 0])
        bad_workload.write_text(json.dumps({**recipe, "inspection_expectations": workload}))
        bad_index = directory / "bad-index.json"
        bad_index.write_text(json.dumps({**recipe, "inspection_expectations":
            {**workload, "tracked_lod_placements": [4]}}))
        bad_count = directory / "bad-count.json"
        bad_count.write_text(json.dumps({**recipe, "inspection_expectations":
            {**workload, "parts": 1000000}}))
        output = directory / "new-output"
        cases = [
            ("missing recipe value", ["--inspection-motion"], "Both nonempty"),
            ("missing paired output", ["--inspection-motion", str(recipe_path)], "Both nonempty"),
            ("existing output", ["--inspection-motion", str(recipe_path), "--inspection-motion-output", str(existing)], "new directory"),
            ("excess trace capacity", ["--inspection-motion", str(bad_recipe), "--inspection-motion-output", str(output)], "trace capacity"),
            ("wrong actual registry", ["--inspection-motion", str(mismatch), "--inspection-motion-output", str(output)], "actual selected registry"),
            ("conflicting benchmark", ["--inspection-motion", str(recipe_path), "--inspection-motion-output", str(output), "--benchmark"], "without another capture/benchmark"),
            ("duplicate tracked placement", ["--inspection-motion", str(bad_workload), "--inspection-motion-output", str(output)], "must be unique"),
            ("prototype tracked as authored LOD", ["--inspection-motion", str(bad_index), "--inspection-motion-output", str(output)], "authored part"),
            ("unbounded workload", ["--inspection-motion", str(bad_count), "--inspection-motion-output", str(output)], "outside its finite bounds"),
        ]
        for name, value in [("negative detail", -1), ("unavailable detail", 4),
                            ("fractional detail", 1.5), ("string detail", "1")]:
            invalid = directory / (name.replace(" ", "-") + ".json")
            invalid.write_text(json.dumps({**recipe, "forced_lod": value}))
            cases.append((name, ["--inspection-motion", str(invalid), "--inspection-motion-output", str(output)],
                          "Forced detail must be an integer"))
        try:
            for name, options, expected in cases:
                command = [str(binary), "--config", str(root / "salvage_assembly_fixture.cfg"), *options]
                run = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=15)
                log = run.stdout + run.stderr
                passed = run.returncode == 1 and expected in log and "GPU Adapter:" not in log and not output.exists()
                passed = passed and marker.read_text() == "Preserve this existing output.\n" and list(existing.iterdir()) == [marker]
                report["cases"].append({"name": name, "argv": command, "exit_code": run.returncode, "passed": passed, "output": log})
                assert passed, name
            assert hashlib.sha256(binary.read_bytes()).hexdigest() == report["binary_sha256"]
            report["status"] = "passed"
        except Exception as error:
            report.update(status="failed", error=str(error))
            raise
        finally:
            args.report.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
