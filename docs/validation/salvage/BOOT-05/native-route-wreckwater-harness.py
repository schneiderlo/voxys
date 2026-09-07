#!/usr/bin/env python3
"""Bounded loopback graphical bootstrap check; run only with exclusive GPU use.

Uses existing binaries and fresh in-memory credentials. The graphical process
has no automated-exit route, so this harness explicitly terminates it after an
OS window screenshot attempt. This is not an orderly graphical shutdown test.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import signal
import struct
import subprocess
import time


ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
BIN = ROOT / "build-salvage-native/bin"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUT)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    expected = [f"native-route-wreckwater-{name}.log" for name in
                ("server", "probe2", "probe3", "probe4", "graphical", "os-screenshot")]
    expected += ["native-route-wreckwater.json", "native-route-wreckwater.png"]
    if any((out / name).exists() for name in expected):
        parser.error("existing WRECKWATER evidence found; choose a fresh --output directory")
    keys = [secrets.token_hex(32) for _ in range(4)]
    records = []
    logs = []
    env = dict(os.environ, WGPU_BACKEND="vulkan")
    summary = {"started_unix_seconds": time.time(), "scope":
               "graphical bootstrap only; no performance or orderly-exit claim",
               "environment": {key: env.get(key) for key in
                               ("WGPU_BACKEND", "VOXY_WINDOW_BACKEND", "DISPLAY",
                                "WAYLAND_DISPLAY")}, "processes": []}

    def launch(name, args):
        path = out / f"native-route-wreckwater-{name}.log"
        handle = path.open("w")
        logs.append(handle)
        process = subprocess.Popen(args, cwd=ROOT, env=env, stdout=handle,
                                   stderr=subprocess.STDOUT)
        record = {"name": name, "process": process, "log": path,
                  "args": ["<ephemeral-redacted>" if value in keys else value
                           for value in args], "binary_sha256": sha(Path(args[0]))}
        records.append(record)
        return record

    def read(record):
        return record["log"].read_text()

    def wait_for(record, pattern, seconds=30):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            match = re.search(pattern, read(record))
            if match:
                return match
            if record["process"].poll() is not None:
                raise RuntimeError(f"{record['name']} exited before {pattern}")
            time.sleep(0.1)
        raise TimeoutError(f"{record['name']} never reported {pattern}")

    try:
        server_args = [str(BIN / "wreckwater_server"), "--bind", "127.0.0.1",
                       "--port", "0", "--max-ticks", "1800"]
        for peer, key in enumerate(keys, 1):
            server_args += [f"--key{peer}", key]
        server = launch("server", server_args)
        port = wait_for(server, r"listening=127\.0\.0\.1:(\d+)").group(1)
        for peer in (2, 3, 4):
            launch(f"probe{peer}", [str(BIN / "wreckwater_client_probe"),
                   "--server", "127.0.0.1", "--port", port, "--peer", str(peer),
                   "--key", keys[peer - 1], "--session", "1", "--match", "1",
                   "--world", "1", "--world-epoch", "1", "--authority-epoch",
                   "1", "--max-ticks", "2400"])
        client = launch("graphical", [str(BIN / "voxy_native"), "--config",
                        "voxy.cfg", "--log-level", "debug",
                        "--wreckwater-server", "127.0.0.1",
                        "--wreckwater-port", port, "--wreckwater-peer", "1",
                        "--wreckwater-key", keys[0], "--wreckwater-session", "1",
                        "--wreckwater-match", "1", "--wreckwater-world", "1",
                        "--wreckwater-world-epoch", "1",
                        "--wreckwater-authority-epoch", "1"])
        wait_for(client, r"WRECKWATER connection state: connected")
        for record in records:
            if record["name"].startswith("probe"):
                wait_for(record, r"WRECKWATER_CLIENT_CONNECTED")
        print("Four peers connected; graphical client initialized.", flush=True)
        time.sleep(3)
        screenshot = out / "native-route-wreckwater.png"
        if screenshot.exists():
            raise RuntimeError("Refusing to overwrite existing screenshot")
        with (out / "native-route-wreckwater-os-screenshot.log").open("w") as log:
            try:
                # The OS screenshot utility uses the host GTK libraries, not
                # the Nix libraries needed by the application under test.
                screenshot_env = dict(env)
                screenshot_env.pop("LD_LIBRARY_PATH", None)
                captured = subprocess.run(
                    ["/usr/bin/gnome-screenshot", "--window", "--file", str(screenshot)],
                    cwd=ROOT, env=screenshot_env, stdout=log, stderr=subprocess.STDOUT,
                    timeout=10, check=False)
                summary["os_screenshot_exit"] = captured.returncode
            except subprocess.TimeoutExpired:
                summary["os_screenshot_exit"] = "timeout"
        if screenshot.exists():
            header = screenshot.read_bytes()[:24]
            if header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR":
                summary["png_dimensions"] = struct.unpack(">II", header[16:24])
                summary["png_sha256"] = sha(screenshot)
        summary["graphical_alive_after_capture"] = client["process"].poll() is None
        summary["graphical_initialized"] = "Application initialized successfully" in read(client)
        summary["graphical_connected"] = "WRECKWATER connection state: connected" in read(client)
        summary["frame_loop_evidence"] = [line for line in read(client).splitlines()
                                          if "FPS:" in line]
        summary["graphical_errors_before_teardown"] = [
            line for line in read(client).splitlines()
            if re.search(r"\[(?:ERROR|CRITICAL)\s*\]|Validation Error|Device lost", line,
                         re.IGNORECASE)]
        summary["passed_bootstrap"] = all(summary.get(key, False) for key in
            ("graphical_alive_after_capture", "graphical_initialized", "graphical_connected")) \
            and not summary["graphical_errors_before_teardown"]
    except Exception as error:
        summary["failure"] = str(error)
        summary["passed_bootstrap"] = False
    finally:
        # All are our child processes. Native app has no SIGTERM handler;
        # server/probes do. The actual return codes remain part of the record.
        for record in reversed(records):
            process = record["process"]
            record["harness_sigterm"] = process.poll() is None
            if record["harness_sigterm"]:
                process.send_signal(signal.SIGTERM)
        for record in records:
            process = record.pop("process")
            try:
                record["exit_code"] = process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                record["harness_sigkill"] = True
                record["exit_code"] = process.wait(timeout=5)
            log = record.pop("log")
            content = log.read_text()
            for key in keys:
                content = content.replace(key, "<ephemeral-redacted>")
            log.write_text(content)
            record["log"] = log.name
            summary["processes"].append(record)
        for handle in logs:
            handle.close()
        summary["binary_sha256_after"] = sha(BIN / "voxy_native")
        (out / "native-route-wreckwater.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: value for key, value in summary.items()
                      if key not in ("processes", "environment")}, indent=2), flush=True)
    return 0 if summary["passed_bootstrap"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
