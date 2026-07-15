#!/usr/bin/env python3
"""Static WASM server with a localhost-only runtime telemetry receiver."""

from __future__ import annotations

import argparse
import datetime as dt
import functools
import json
import os
from pathlib import Path
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse


MAX_PAYLOAD_BYTES = 1024 * 1024


class TelemetryStore:
    def __init__(self, latest_path: Path, history_path: Path,
                 maximum_history_bytes: int) -> None:
        self.latest_path = latest_path
        self.history_path = history_path
        self.maximum_history_bytes = maximum_history_bytes
        self._lock = threading.Lock()

    def record(self, payload: dict, client: str) -> dict:
        sample = dict(payload)
        sample["receiver"] = {
            "received_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "client": client,
        }
        encoded = json.dumps(
            sample, separators=(",", ":"), sort_keys=True,
            allow_nan=False).encode("utf-8")

        with self._lock:
            self.latest_path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.latest_path.with_name(
                self.latest_path.name + ".tmp")
            temporary.write_bytes(encoded + b"\n")
            os.replace(temporary, self.latest_path)

            if self.maximum_history_bytes > 0:
                self.history_path.parent.mkdir(parents=True, exist_ok=True)
                next_bytes = len(encoded) + 1
                current_bytes = (
                    self.history_path.stat().st_size
                    if self.history_path.exists() else 0)
                if current_bytes + next_bytes > self.maximum_history_bytes:
                    backup = self.history_path.with_name(
                        self.history_path.name + ".1")
                    backup.unlink(missing_ok=True)
                    if self.history_path.exists():
                        os.replace(self.history_path, backup)
                with self.history_path.open("ab") as history:
                    history.write(encoded + b"\n")
        return sample

    def read_latest(self) -> bytes | None:
        with self._lock:
            if not self.latest_path.exists():
                return None
            return self.latest_path.read_bytes()


class TelemetryHttpServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], directory: Path,
                 store: TelemetryStore) -> None:
        self.telemetry_store = store
        handler = functools.partial(
            TelemetryRequestHandler, directory=str(directory))
        super().__init__(address, handler)


class TelemetryRequestHandler(SimpleHTTPRequestHandler):
    server: TelemetryHttpServer

    def log_message(self, message_format: str, *args: object) -> None:
        if urlparse(self.path).path.startswith("/api/telemetry"):
            return
        super().log_message(message_format, *args)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _send_json(self, status: int, value: object) -> None:
        encoded = json.dumps(value, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_POST(self) -> None:
        if urlparse(self.path).path != "/api/telemetry":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400, "Invalid Content-Length")
            return
        if length <= 0 or length > MAX_PAYLOAD_BYTES:
            self.send_error(413, "Telemetry payload is empty or too large")
            return
        try:
            payload = json.loads(self.rfile.read(length))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_error(400, "Telemetry payload is not valid JSON")
            return
        if not isinstance(payload, dict) or payload.get("schema_version") != 1:
            self.send_error(400, "Unsupported telemetry schema")
            return
        try:
            self.server.telemetry_store.record(
                payload, self.client_address[0])
        except (OSError, TypeError, ValueError) as error:
            self.send_error(500, f"Could not store telemetry: {error}")
            return
        self._send_json(202, {"ok": True})

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/telemetry/health":
            self._send_json(200, {
                "ok": True,
                "latest": str(self.server.telemetry_store.latest_path),
                "history": str(self.server.telemetry_store.history_path),
            })
            return
        if path == "/api/telemetry/latest":
            encoded = self.server.telemetry_store.read_latest()
            if encoded is None:
                self._send_json(404, {"ok": False, "error": "no samples"})
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
            return
        super().do_GET()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8081)
    parser.add_argument(
        "--telemetry-file", type=Path,
        default=Path("/tmp/voxys-telemetry.json"))
    parser.add_argument(
        "--history-file", type=Path,
        default=Path("/tmp/voxys-telemetry.ndjson"))
    parser.add_argument(
        "--maximum-history-bytes", type=int, default=8 * 1024 * 1024)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    store = TelemetryStore(
        args.telemetry_file, args.history_file,
        max(args.maximum_history_bytes, 0))
    server = TelemetryHttpServer((args.host, args.port), args.directory, store)
    print(f"Serving WASM at http://{args.host}:{args.port}", flush=True)
    print(f"Latest telemetry: {store.latest_path}", flush=True)
    print(f"Telemetry history: {store.history_path}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
