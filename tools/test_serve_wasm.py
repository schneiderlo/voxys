#!/usr/bin/env python3

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
import serve_wasm  # noqa: E402
import read_physics_telemetry  # noqa: E402


class TelemetryServerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.static = root / "static"
        self.static.mkdir()
        (self.static / "index.html").write_text("ready", encoding="utf-8")
        self.latest = root / "latest.json"
        self.history = root / "history.ndjson"
        store = serve_wasm.TelemetryStore(
            self.latest, self.history, maximum_history_bytes=1024)
        self.server = serve_wasm.TelemetryHttpServer(
            ("127.0.0.1", 0), self.static, store)
        self.thread = threading.Thread(
            target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.temporary.cleanup()

    def test_receives_latest_and_serves_static_files(self) -> None:
        payload = {
            "schema_version": 1,
            "captured_at": "2026-07-15T00:00:00.000Z",
            "frame": {"fps": 60.0},
        }
        request = urllib.request.Request(
            self.base + "/api/telemetry",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST")
        with urllib.request.urlopen(request) as response:
            self.assertEqual(response.status, 202)

        stored = json.loads(self.latest.read_text(encoding="utf-8"))
        self.assertEqual(stored["frame"]["fps"], 60.0)
        self.assertEqual(stored["receiver"]["client"], "127.0.0.1")
        self.assertTrue(self.history.exists())

        with urllib.request.urlopen(
                self.base + "/api/telemetry/latest") as response:
            latest = json.load(response)
        self.assertEqual(latest["captured_at"], payload["captured_at"])

        with urllib.request.urlopen(self.base + "/index.html") as response:
            self.assertEqual(response.read(), b"ready")
            self.assertEqual(response.headers["Cache-Control"], "no-store")

    def test_rejects_unknown_schema(self) -> None:
        request = urllib.request.Request(
            self.base + "/api/telemetry",
            data=b'{"schema_version":2}', method="POST")
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request)
        self.assertEqual(raised.exception.code, 400)

    def test_reader_formats_optimization_fields(self) -> None:
        sample = {
            "schema_version": 1,
            "captured_at": "2026-07-15T00:00:00.000Z",
            "frame": {
                "fps": 60.0, "average_ms": 16.67, "cpu_ms": 1.0,
                "gpu_queue": 1, "pacing_skips": 2,
            },
            "physics": {
                "backend": "webgpu_soft", "tick": 4,
                "bodies": {"current": 1300, "capacity": 131072,
                           "high_water": 1300, "overflow": False},
                "active_bodies": {"current": 1300, "capacity": 131072,
                                  "high_water": 1300, "overflow": False},
                "candidate_pairs": {"current": 6118, "capacity": 262144,
                                    "high_water": 6118, "overflow": False},
                "pairs": {"current": 6118, "capacity": 65536,
                          "high_water": 6118, "overflow": False},
                "contacts": {"current": 58, "capacity": 16384,
                             "high_water": 58, "overflow": False},
                "solver_mode": "global",
                "stages": {
                    "total_ms": 12.0,
                    "broad_index_build": 0.5,
                    "broad_index_sort_ranges": 0.75,
                    "broad_pair_count": 0.5,
                    "broad_pair_scatter": 0.25,
                    "broad_pair_sort_unique": 0.75,
                    "broad_lifecycle": 0.25,
                    "dynamic_solver_coloring": 1.0,
                    "dynamic_solver_graph": 1.0,
                    "dynamic_solver_solve": 2.0,
                },
            },
        }
        report = read_physics_telemetry.format_sample(sample, self.latest)
        self.assertIn("60.0 FPS", report)
        self.assertIn("bodies 1300/131072 h1300", report)
        self.assertIn("broad 3.00", report)
        self.assertIn("solver 4.00", report)
        self.assertIn("sort/ranges 0.75", report)

    def test_reader_accepts_legacy_aggregate_stage_fields(self) -> None:
        sample = {
            "schema_version": 1,
            "physics": {
                "stages": {
                    "broad_phase": 3.0,
                    "dynamic_solver": 4.0,
                },
            },
        }

        report = read_physics_telemetry.format_sample(sample, self.latest)

        self.assertIn("broad 3.00", report)
        self.assertIn("solver 4.00", report)


if __name__ == "__main__":
    unittest.main()
