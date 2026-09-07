#!/usr/bin/env python3
"""Capture ordinary visible native routes from an existing, frozen binary.

Does not build, change configs, disable validation, or exercise WRECKWATER (its
graphical client intentionally forbids screenshot automation). A timeout is a
failure and is recorded separately from the normal application exit.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--routes", nargs="+", default=[
        "lego_world.cfg", "lego_shore.cfg", "voxy.cfg", "ridgebreak.cfg",
        "salvage.cfg"])
    args = parser.parse_args()
    if args.frames < 1 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    before = digest(binary)
    results = []
    for config_name in args.routes:
        config = Path(config_name).resolve(strict=True)
        prefix = output / ("native-route-" + config.stem)
        screenshot = prefix.with_suffix(".png")
        if screenshot.exists():
            raise RuntimeError(f"Refusing to reuse existing screenshot: {screenshot}")
        command = [str(binary), "--config", str(config), "--screenshot",
                   str(screenshot), "--screenshot-frames", str(args.frames)]
        print(f"Launching visible route {config.name}", flush=True)
        started = time.time()
        timed_out = False
        with prefix.with_suffix(".log").open("w") as log:
            try:
                process = subprocess.run(command, stdout=log,
                                         stderr=subprocess.STDOUT,
                                         timeout=args.timeout, check=False)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
                exit_code = None
        log_text = prefix.with_suffix(".log").read_text()
        png_dimensions = None
        if screenshot.exists():
            header = screenshot.read_bytes()[:24]
            if header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR":
                png_dimensions = struct.unpack(">II", header[16:24])
        errors = [line for line in log_text.splitlines()
                  if re.search(r"\[error\]|\[critical\]|Validation Error|Device lost", line,
                               re.IGNORECASE)]
        result = {
            "config": config.name, "config_sha256": digest(config),
            "command": command, "started_unix_seconds": started,
            "wall_seconds": round(time.time() - started, 3),
            "exit_code": exit_code, "timed_out": timed_out,
            "png_dimensions": png_dimensions,
            "png_sha256": digest(screenshot) if screenshot.exists() else None,
            "main_loop_ended": "Main loop ended" in log_text,
            "saved_screenshot": "Saved screenshot to:" in log_text,
            "errors": errors,
            "window_and_adapter": [line for line in log_text.splitlines()
                                   if re.search(r"Window created via|[Aa]dapter", line)],
        }
        result["passed"] = (exit_code == 0 and not timed_out and
                            png_dimensions is not None and not errors and
                            result["main_loop_ended"] and result["saved_screenshot"])
        prefix.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
        results.append(result)
        print(f"{config.name}: exit={exit_code}, PNG={png_dimensions}, "
              f"errors={len(errors)}, passed={result['passed']}", flush=True)
    after = digest(binary)
    summary = {"binary": str(binary), "binary_sha256_before": before,
               "binary_sha256_after": after,
               "environment": {key: os.environ.get(key) for key in
                               ("WGPU_BACKEND", "VOXY_WINDOW_BACKEND", "DISPLAY",
                                "WAYLAND_DISPLAY")},
               "claim": "visible launch and natural screenshot exit only; no performance claim",
               "routes": results, "passed": before == after and
               all(result["passed"] for result in results)}
    (output / "native-route-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
