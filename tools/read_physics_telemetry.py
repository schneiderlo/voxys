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

BROAD_PHASE_STAGES = (
    "broad_index_build",
    "broad_index_sort_ranges",
    "broad_pair_count",
    "broad_pair_scatter",
    "broad_pair_sort_unique",
    "broad_lifecycle",
)

DYNAMIC_SOLVER_STAGES = (
    "dynamic_solver_coloring",
    "dynamic_solver_graph",
    "dynamic_solver_solve",
)


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


def stage_total(stages: dict, legacy_name: str,
                component_names: tuple[str, ...]) -> float:
    """Read a legacy aggregate or sum the current detailed stage fields."""
    legacy = stages.get(legacy_name)
    if isinstance(legacy, (int, float)):
        return float(legacy)
    return sum(number(stages.get(name)) for name in component_names)


def format_sample(sample: dict, path: Path) -> str:
    frame = sample.get("frame", {})
    physics = sample.get("physics", {})
    stages = physics.get("stages", {})
    receiver = sample.get("receiver", {})
    broad_phase_ms = stage_total(
        stages, "broad_phase", BROAD_PHASE_STAGES)
    dynamic_solver_ms = stage_total(
        stages, "dynamic_solver", DYNAMIC_SOLVER_STAGES)
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
        f" | broad {broad_phase_ms:.2f}"
        f" | narrow {number(stages.get('narrow_phase')):.2f}"
        f" | solver {dynamic_solver_ms:.2f}"
        f" | islands {number(stages.get('islands_sleeping')):.2f}",
    ]
    if any(name in stages
           for name in BROAD_PHASE_STAGES + DYNAMIC_SOLVER_STAGES):
        lines.extend([
            f"Broad: index {number(stages.get('broad_index_build')):.2f}"
            f" + sort/ranges "
            f"{number(stages.get('broad_index_sort_ranges')):.2f}"
            f" | pairs {number(stages.get('broad_pair_count')):.2f}"
            f" + scatter {number(stages.get('broad_pair_scatter')):.2f}"
            f" + sort {number(stages.get('broad_pair_sort_unique')):.2f}"
            f" | lifecycle {number(stages.get('broad_lifecycle')):.2f}",
            f"Solver: coloring "
            f"{number(stages.get('dynamic_solver_coloring')):.2f}"
            f" | graph {number(stages.get('dynamic_solver_graph')):.2f}"
            f" | solve {number(stages.get('dynamic_solver_solve')):.2f}",
        ])
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
