#!/usr/bin/env python3
"""Compare exact actual-runtime Frontier replay state, save and HUD pixels."""

import argparse
import gzip
import hashlib
import itertools
import json
from pathlib import Path


def first_difference(before, after, path="$"):
    if type(before) is not type(after):
        return path
    if isinstance(before, dict):
        if before.keys() != after.keys():
            return path + ".keys"
        for key in before:
            difference = first_difference(before[key], after[key], path + "." + key)
            if difference is not None:
                return difference
    elif isinstance(before, list):
        if len(before) != len(after):
            return path + ".length"
        for index, (left, right) in enumerate(zip(before, after)):
            difference = first_difference(left, right, f"{path}[{index}]")
            if difference is not None:
                return difference
    elif before != after:
        return path
    return None


def replay(prefix):
    observations = Path(str(prefix) + ".jsonl")
    if not observations.is_file():
        observations = Path(str(observations) + ".gz")
    return {
        "observations": observations,
        "save": Path(str(prefix) + ".save"),
        "pixels": json.loads(Path(str(prefix) + ".pixels.json").read_text()),
    }


def open_observations(path):
    return gzip.open(path, "rb") if path.suffix == ".gz" else path.open("rb")


def compare(before_prefix, after_prefix):
    before = replay(before_prefix)
    after = replay(after_prefix)
    sentinel = object()
    observation_count = 0
    observation_exact = True
    first_frame = None
    first_path = None
    with open_observations(before["observations"]) as left, open_observations(after["observations"]) as right:
        for frame, (old_line, new_line) in enumerate(itertools.zip_longest(left, right, fillvalue=sentinel)):
            observation_count += 1
            if old_line == new_line:
                continue
            observation_exact = False
            if first_frame is None:
                first_frame = frame
                first_path = "$.observationCount" if old_line is sentinel or new_line is sentinel else (
                    first_difference(json.loads(old_line), json.loads(new_line)) or "$.serialization"
                )
    old_save = before["save"].read_bytes()
    new_save = after["save"].read_bytes()
    old_pixels = before["pixels"]
    new_pixels = after["pixels"]
    expected_frames = list(range(0, 1200, 60)) + [1199]
    pixels_complete = all(
        [entry["frame"] for entry in report["pixelHashes"]] == expected_frames
        for report in (old_pixels, new_pixels)
    )
    configuration_exact = all(
        old_pixels[key] == new_pixels[key]
        for key in ("scope", "nearbyEnemy", "enemyAttackObserved", "frames", "adapter")
    )
    saves_verified = (
        hashlib.sha256(old_save).hexdigest() == old_pixels["archiveSha256"]
        and hashlib.sha256(new_save).hexdigest() == new_pixels["archiveSha256"]
    )
    result = {
        "scope": "Exact native Frontier JSON, archive bytes and actual GPU HUD pixel hashes; no FPS claim",
        "configurationExact": configuration_exact,
        "observationFrames": observation_count,
        "jsonBytesExact": observation_exact,
        "firstDifferingFrame": first_frame,
        "firstDifferingPath": first_path,
        "saveBytesExact": old_save == new_save,
        "saveHashesVerified": saves_verified,
        "pixelFramesComplete": pixels_complete,
        "hudPixelsExact": old_pixels["pixelHashes"] == new_pixels["pixelHashes"],
        "beforeHudUploads": old_pixels["hudUploads"],
        "afterHudUploads": new_pixels["hudUploads"],
        "beforeTriangleUploads": old_pixels["hudTriangleUploads"],
        "afterTriangleUploads": new_pixels["hudTriangleUploads"],
        "beforeSimulationCpu": old_pixels.get("simulationCpu"),
        "afterSimulationCpu": new_pixels.get("simulationCpu"),
        "beforePausedCpu": old_pixels.get("pausedCpu"),
        "afterPausedCpu": new_pixels.get("pausedCpu"),
    }
    result["exact"] = all(
        result[key] for key in (
            "configurationExact", "jsonBytesExact", "saveBytesExact",
            "saveHashesVerified", "pixelFramesComplete", "hudPixelsExact",
        )
    ) and observation_count == 1200
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="Baseline VOXY_FRONTIER_RUNTIME_ORACLE prefix")
    parser.add_argument("after", type=Path, help="Candidate VOXY_FRONTIER_RUNTIME_ORACLE prefix")
    parser.add_argument("--output", type=Path, help="Optional JSON report path")
    arguments = parser.parse_args()
    try:
        result = compare(arguments.before, arguments.after)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {"exact": False, "error": str(error)}
    report = json.dumps(result, indent=2) + "\n"
    if arguments.output:
        arguments.output.write_text(report)
    print(report, end="")
    return 0 if result["exact"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
