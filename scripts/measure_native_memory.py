#!/usr/bin/env python3
"""Launch one native command and collect bounded process/DRM memory observations.

Example: python3 scripts/measure_native_memory.py --output /tmp/native-memory.json
         --duration 45 -- build-salvage-native/bin/voxy_native --config lego_shore.cfg
This instrumented run is separate from an uninstrumented performance baseline.
"""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import time

from salvage_memory_probe import ProcessProbe, write_report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--max-samples", type=int, default=2048)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("provide a command after --")
    if not 0.1 <= args.interval <= 0.25 or not 0 < args.duration <= 3600:
        parser.error("interval must be 0.1..0.25 seconds; duration must be 0..3600 seconds")
    if not 1 <= args.max_samples <= 10000:
        parser.error("max-samples must be 1..10000")
    process = subprocess.Popen(command, start_new_session=True)
    probe = None
    stopped = False
    reason = "child_exited"
    usage = None
    stop_at = None
    exit_code = None

    def stop(_signal=None, _frame=None):
        nonlocal stopped, stop_at
        if not stopped:
            stopped = True
            stop_at = time.monotonic()
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        probe = ProcessProbe(process.pid, capacity=args.max_samples)
        while True:
            probe.sample("native-run" if not stopped else "shutdown")
            child, status, child_usage = os.wait4(process.pid, os.WNOHANG)
            if child:
                exit_code = os.waitstatus_to_exitcode(status)
                process.returncode = exit_code
                usage = child_usage
                break
            if not stopped and time.monotonic() - probe.started >= args.duration:
                reason = "duration_elapsed"
                stop()
            elif stopped and reason != "duration_elapsed":
                reason = "stop_requested"
            if stop_at is not None and time.monotonic() - stop_at > 3:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            time.sleep(args.interval)
    finally:
        if exit_code is None:
            stop()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        if probe is not None:
            report = probe.report(interval_seconds=args.interval, stop_reason=reason)
            report["child_exit_code"] = exit_code
            report["child_wait4_maxrss_bytes"] = int(usage.ru_maxrss) * 1024 if usage else None
            report["child_wait4_maxrss_scope"] = "Linux wait4 child usage; not a sum of process high-water marks"
            # Do not persist command arguments: launch flags can contain credentials.
            report["executable"] = Path(command[0]).name
            write_report(args.output, report)
    print(f"Memory report: {args.output}")
    return 0 if stopped else (exit_code if exit_code is not None and exit_code >= 0 else 1)


if __name__ == "__main__":
    raise SystemExit(main())
