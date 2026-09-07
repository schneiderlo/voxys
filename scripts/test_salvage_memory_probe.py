#!/usr/bin/env python3
"""Pure fixture tests: no GPU, browser, /proc mutation or native build required."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from salvage_memory_probe import History, ProcessProbe, aggregate_drm, parse_fdinfo, parse_stat, parse_status, quantity


def stat(pid, parent=1, start=50, name="fixture (worker)"):
    fields = ["S", str(parent)] + ["0"] * 17 + [str(start)]
    return f"{pid} ({name}) " + " ".join(fields)


def drm(pid=10, client="4", device="0000:c6:00.0", total=100, resident=80, fd=5):
    header = "drm-driver:\tamdgpu\n"
    if client is not None:
        header += f"drm-client-id: {client}\n"
    if device is not None:
        header += f"drm-pdev: {device}\n"
    parsed = parse_fdinfo(header + f"drm-total-vram: {total} KiB\ndrm-resident-vram: {resident} KiB\n")
    return {**parsed, "pid": pid, "start_time_ticks": 50, "fd": fd}


class MemoryParserTests(unittest.TestCase):
    def test_units_and_rejection(self):
        self.assertEqual(quantity("123"), 123)
        self.assertEqual(quantity("2\tMiB"), 2 * 1024**2)
        self.assertEqual(quantity("3 KiB"), 3072)
        self.assertEqual(quantity("4 kB", status=True), 4096)
        for raw in ("-1 KiB", "1.5 MiB", "1 GB", "123 junk", "", "+2", "1 kB"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                quantity(raw)

    def test_status_missing_is_not_zero_and_names_can_contain_parentheses(self):
        self.assertEqual(parse_stat(stat(10, 7, 42))["start_time_ticks"], 42)
        self.assertEqual(parse_stat(stat(10))["name"], "fixture (worker)")
        self.assertEqual(parse_status("VmRSS: 12 kB\nVmHWM: 20 kB")["rss_bytes"], 12288)
        self.assertIsNone(parse_status("Name: demo")["rss_bytes"])
        invalid = parse_status("VmRSS: -1 kB\nVmHWM: 20 MB")
        self.assertEqual(len(invalid["errors"]), 2)

    def test_alias_regions_unknowns_and_optional_fields(self):
        parsed = parse_fdinfo("drm-driver: amdgpu\ndrm-memory-gtt: 3 KiB\n"
                              "drm-resident-gtt: 3 KiB\ndrm-engine-gfx: 555 ns\n"
                              "drm-total-vram: 2 MiB\ndrm-active-gtt: 1 KiB\n")
        self.assertEqual(parsed["regions"]["gtt"]["resident_bytes"], 3072)
        self.assertNotIn("requested_bytes", parsed["regions"]["gtt"])
        self.assertEqual(parsed["unknown_fields"]["drm-engine-gfx"], "555 ns")
        legacy = parse_fdinfo("drm-driver: amdgpu\ndrm-memory-vram: 5 KiB")
        self.assertEqual(legacy["regions"]["vram"]["resident_bytes"], 5120)
        mismatch = parse_fdinfo("drm-driver: amdgpu\ndrm-memory-vram: 8 KiB\ndrm-resident-vram: 5 KiB")
        self.assertEqual(mismatch["regions"]["vram"]["resident_bytes"], 5120)
        self.assertTrue(mismatch["errors"])
        bad = parse_fdinfo("drm-driver: amdgpu\ndrm-client-id: nope\ndrm-total-vram: 5 GB")
        self.assertEqual(len(bad["errors"]), 2)
        self.assertIsNone(parse_fdinfo("pos: 0\nflags: 0100000"))

    def test_client_dedup_not_regionwise_maxima_and_other_devices_remain_distinct(self):
        first, duplicate = drm(), drm(pid=11, fd=7, total=150, resident=20)
        result = aggregate_drm([first, duplicate, drm(client="4", device="0000:00:02.0", total=20)])
        self.assertEqual(result["requested_bytes"], 120 * 1024)
        self.assertEqual(result["deduplicated_client_count"], 2)
        self.assertTrue(result["clients"][0]["duplicate_snapshot_changed"])
        self.assertEqual(len(result["clients"][0]["references"]), 2)
        self.assertIsNone(result["shared_bytes"])
        unidentified = aggregate_drm([drm(device=None), drm(pid=11, device=None)])
        self.assertEqual(unidentified["deduplicated_client_count"], 0)
        self.assertEqual(len(unidentified["unidentified_clients"]), 2)
        self.assertIsNone(unidentified["requested_bytes"])

    def test_partial_coverage_cannot_be_reported_as_complete_total(self):
        unknown = drm(client=None)
        result = aggregate_drm([drm(), unknown])
        self.assertIsNone(result["requested_bytes"])
        self.assertEqual(result["requested_known_bytes"], 100 * 1024)
        self.assertEqual(len(result["unidentified_clients"]), 1)
        self.assertIsNone(aggregate_drm([])["resident_bytes"])
        legacy = drm()
        del legacy["regions"]["vram"]["requested_bytes"]
        self.assertIsNone(aggregate_drm([legacy])["requested_bytes"])
        self.assertIsNone(result["pending_retirement_bytes"])
        self.assertIsNone(result["staging_bytes"])

    def test_peak_uses_concurrent_sum_and_outlives_bounded_history(self):
        history = History(1)
        for index, (a, b) in enumerate(((100, 1), (1, 100), (2, 3))):
            history.append({"rss_sum_bytes": a + b, "gpu": aggregate_drm([drm(total=a), drm(client="5", total=b)]),
                            "elapsed_seconds": index, "phase": "test", "processes": []})
        self.assertEqual(history.peaks["gpu_requested_bytes"]["bytes"], 101 * 1024)
        self.assertEqual(history.peaks["rss_sum_bytes"]["bytes"], 101)
        self.assertEqual(len(history.samples), 1)
        self.assertEqual(history.count, 3)

    def test_process_high_water_history_is_bounded_across_respawns(self):
        history = History(1, process_capacity=2)
        for pid in range(4):
            history.append({"rss_sum_bytes": 10, "gpu": aggregate_drm([]), "elapsed_seconds": pid,
                            "phase": "test", "processes": [{"pid": pid, "start_time_ticks": 50,
                                                               "rss_high_water_bytes": 20}]})
        self.assertEqual(len(history.process_high_water), 2)
        self.assertEqual(history.dropped_process_high_water_observations, 2)


class ProcessScopeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def process(self, pid, parent, *, start=50, children="", rss=10):
        base = self.root / str(pid)
        (base / "task" / str(pid)).mkdir(parents=True)
        (base / "fdinfo").mkdir()
        (base / "stat").write_text(stat(pid, parent, start))
        (base / "status").write_text(f"VmRSS: {rss} kB\nVmHWM: {rss + 2} kB")
        (base / "task" / str(pid) / "children").write_text(children)
        return base

    def test_descendants_only_and_shared_fd_not_double_counted(self):
        parent = self.process(10, 1, children="11")
        child = self.process(11, 10)
        self.process(99, 1, rss=900000)
        info = "drm-driver: amdgpu\ndrm-client-id: 4\ndrm-pdev: 0000:c6:00.0\ndrm-total-vram: 8 KiB"
        (parent / "fdinfo" / "5").write_text(info)
        (child / "fdinfo" / "8").write_text(info)
        result = ProcessProbe(10, proc_root=self.root).sample()
        self.assertEqual({p["pid"] for p in result["processes"]}, {10, 11})
        self.assertEqual(result["rss_sum_bytes"], 20 * 1024)
        self.assertEqual(result["gpu"]["requested_bytes"], 8 * 1024)

    def test_root_reuse_and_child_reparenting_are_rejected(self):
        parent = self.process(10, 1, children="11")
        self.process(11, 77, rss=900000)
        probe = ProcessProbe(10, proc_root=self.root)
        sample = probe.sample()
        self.assertEqual([p["pid"] for p in sample["processes"]], [10])
        self.assertTrue(sample["errors"])
        (parent / "stat").write_text(stat(10, 1, 500))
        sample = probe.sample()
        self.assertFalse(sample["root_alive"])
        self.assertEqual(sample["processes"], [])
        self.assertIsNone(sample["rss_sum_bytes"])

    def test_vanished_child_does_not_silently_become_zero_usage(self):
        self.process(10, 1, children="11")
        sample = ProcessProbe(10, proc_root=self.root).sample()
        self.assertTrue(sample["errors"])
        self.assertFalse(sample["gpu"]["scan_complete"])
        self.assertIsNone(sample["rss_sum_bytes"])
        self.assertEqual(sample["rss_sum_known_bytes"], 10 * 1024)

    def test_report_status_requires_rss_and_both_gpu_metrics(self):
        parent = self.process(10, 1)
        probe = ProcessProbe(10, proc_root=self.root)
        probe.sample()
        partial = probe.report(interval_seconds=0.2, stop_reason="fixture")
        self.assertEqual(partial["status"], "partial")
        self.assertIn("gpu_requested_bytes", partial["missing_required_metrics"])
        (parent / "fdinfo" / "5").write_text("drm-driver: amdgpu\ndrm-client-id: 4\n"
            "drm-pdev: 0000:c6:00.0\ndrm-total-vram: 8 KiB\ndrm-resident-vram: 4 KiB")
        probe.sample()
        self.assertEqual(probe.report(interval_seconds=0.2, stop_reason="fixture")["status"], "complete")

    def test_dropped_history_does_not_hide_errors_in_report_status(self):
        parent = self.process(10, 1, children="11")
        (parent / "fdinfo" / "5").write_text("drm-driver: amdgpu\ndrm-client-id: 4\n"
            "drm-pdev: 0000:c6:00.0\ndrm-total-vram: 8 KiB\ndrm-resident-vram: 4 KiB")
        probe = ProcessProbe(10, proc_root=self.root, capacity=1)
        probe.sample()
        (parent / "task" / "10" / "children").write_text("")
        probe.sample()
        report = probe.report(interval_seconds=0.2, stop_reason="fixture")
        self.assertEqual(report["status"], "partial")
        self.assertEqual(report["samples_with_errors"], 1)
        self.assertEqual(report["dropped_history_samples"], 1)
        self.assertFalse(report["samples"][0]["errors"])

    def test_traversal_limits_and_scan_errors_preserve_only_known_subtotals(self):
        parent = self.process(10, 1, children="11 12")
        self.process(11, 10)
        self.process(12, 10)
        info = "drm-driver: amdgpu\ndrm-client-id: 4\ndrm-pdev: 0000:c6:00.0\ndrm-total-vram: 8 KiB"
        for fd in (5, 6):
            (parent / "fdinfo" / str(fd)).write_text(info)
        with patch("salvage_memory_probe.MAX_PROCESSES_PER_SAMPLE", 2), \
             patch("salvage_memory_probe.MAX_FDINFO_PER_SAMPLE", 1):
            sample = ProcessProbe(10, proc_root=self.root).sample()
        self.assertEqual(len(sample["processes"]), 2)
        self.assertTrue(sample["errors"])
        self.assertIsNone(sample["gpu"]["requested_bytes"])
        self.assertEqual(sample["gpu"]["requested_known_bytes"], 8 * 1024)


if __name__ == "__main__":
    unittest.main()
