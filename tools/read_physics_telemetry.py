#!/usr/bin/env python3
"""Read the latest browser physics telemetry sample."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time


DEFAULT_PATH = Path(os.environ.get(
    "VOXY_TELEMETRY_FILE", "/tmp/voxys-telemetry.json"))


def read_sample(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or value.get("schema_version") != 1:
        raise ValueError("unsupported telemetry schema")
    return value


def usage(value: dict) -> str:
    current = value.get("current", 0)
    capacity = value.get("capacity", 0)
    high = value.get("high_water", 0)
    overflow = " overflow" if value.get("overflow", False) else ""
    return f"{current}/{capacity} h{high}{overflow}"


def number(value: object) -> float:
    return float(value) if isinstance(value, (int, float)) else 0.0


def format_sample(sample: dict, path: Path) -> str:
    frame = sample.get("frame", {})
    physics = sample.get("physics", {})
    stages = physics.get("stages", {})
    receiver = sample.get("receiver", {})
    lines = [
        f"Telemetry: {path}",
        f"Captured: {sample.get('captured_at', 'unknown')}"
        f" | received {receiver.get('received_at', 'unknown')}",
        f"Frame: {number(frame.get('fps')):.1f} FPS"
        f" ({number(frame.get('average_ms')):.2f} ms)"
        f" | CPU {number(frame.get('cpu_ms')):.2f} ms"
        f" | queue {frame.get('gpu_queue', 0)}/4"
        f" | skips {frame.get('pacing_skips', 0)}",
        f"Physics: {physics.get('backend', 'unknown')}"
        f" | tick {physics.get('tick', 0)}"
        f" | bodies {usage(physics.get('bodies', {}))}"
        f" | active {usage(physics.get('active_bodies', {}))}"
        f" | sleeping {physics.get('sleeping_bodies', 0)}",
        f"Pairs: candidates {usage(physics.get('candidate_pairs', {}))}"
        f" | unique {usage(physics.get('pairs', {}))}"
        f" | contacts {usage(physics.get('contacts', {}))}",
        f"Solver: {physics.get('solver_mode', 'unknown')}"
        f" | compact {physics.get('compact_contacts', 0)} contacts"
        f" / {physics.get('compact_bodies', 0)} bodies"
        f" | colors {physics.get('graph_colors', 0)}",
        f"GPU: {number(stages.get('total_ms')):.2f} ms"
        f" | broad {number(stages.get('broad_phase')):.2f}"
        f" | narrow {number(stages.get('narrow_phase')):.2f}"
        f" | solver {number(stages.get('dynamic_solver')):.2f}"
        f" | islands {number(stages.get('islands_sleeping')):.2f}",
    ]
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", type=Path, default=DEFAULT_PATH)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--wait", type=float, default=0.0,
                        help="wait this many seconds for the first sample")
    parser.add_argument("--interval", type=float, default=1.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    deadline = time.monotonic() + max(args.wait, 0.0)
    previous_mtime = -1
    while True:
        try:
            stat = args.file.stat()
            if stat.st_mtime_ns != previous_mtime:
                sample = read_sample(args.file)
                if previous_mtime != -1 and args.watch:
                    print()
                if args.json:
                    print(json.dumps(sample, indent=2, sort_keys=True))
                else:
                    print(format_sample(sample, args.file))
                previous_mtime = stat.st_mtime_ns
                if not args.watch:
                    return 0
        except (FileNotFoundError, json.JSONDecodeError, ValueError) as error:
            if not args.watch and time.monotonic() >= deadline:
                print(f"No usable telemetry at {args.file}: {error}",
                      file=sys.stderr)
                return 1
        if not args.watch and time.monotonic() >= deadline:
            print(f"No telemetry received at {args.file}", file=sys.stderr)
            return 1
        time.sleep(max(args.interval, 0.05))


if __name__ == "__main__":
    raise SystemExit(main())
