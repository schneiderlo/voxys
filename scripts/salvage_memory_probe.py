#!/usr/bin/env python3
"""Bounded Linux memory observations for one explicitly launched process tree.

DRM semantics: https://dri.freedesktop.org/docs/drm/gpu/drm-usage-stats.html
RSS is a separate view, not additive with GPU memory on shared-memory devices.
No cumulative allocation counter is reported as live memory.
"""

from __future__ import annotations

import argparse
from collections import deque
from itertools import islice
import json
from pathlib import Path
import re
import signal
import time


MEMORY_FIELDS = ("requested", "resident", "shared", "active", "purgeable")
MAX_PROCESSES_PER_SAMPLE = 256
MAX_THREADS_PER_PROCESS = 512
MAX_FDINFO_PER_SAMPLE = 8192
MAX_CHILDREN_PER_THREAD = 1024
MAX_PROCESS_HIGH_WATER_RECORDS = 2048
PREFIXES = {"total": "requested", "resident": "resident", "shared": "shared",
            "active": "active", "purgeable": "purgeable", "memory": "legacy_resident"}
LIMITATIONS = [
    "Peaks are maxima of observed samples; short transients between samples may be missed.",
    "DRM totals include requested buffers still attributed to each client, not all hidden driver allocations.",
    "Distinct DRM clients can share buffer objects; their summed bytes are not unique physical memory.",
    "RSS sums can count shared mappings repeatedly. RSS, JS, WASM and GPU views are not additive.",
    "Staging and pending-retirement categories cannot be separated by this observer; unavailable is not zero.",
    "Missing fields, inaccessible files and vanished processes are recorded, not replaced with zero.",
    "Traversal and per-process high-water history have explicit caps; truncation is reported as incomplete coverage.",
]


def quantity(text: str, *, status: bool = False) -> int:
    match = re.fullmatch(r"([0-9]+)(?:\s+([A-Za-z]+))?", text.strip())
    if not match:
        raise ValueError("invalid unsigned memory quantity")
    units = {None: 1, "kB": 1024} if status else {None: 1, "KiB": 1024, "MiB": 1024**2}
    if match[2] not in units:
        raise ValueError("unsupported memory unit")
    return int(match[1]) * units[match[2]]


def parse_stat(text: str) -> dict:
    opening, closing = text.find("("), text.rfind(")")
    if opening < 1 or closing < opening:
        raise ValueError("invalid process stat")
    tail = text[closing + 1:].split()
    if len(tail) < 20:
        raise ValueError("truncated process stat")
    return {"pid": int(text[:opening]), "name": text[opening + 1:closing],
            "parent_pid": int(tail[1]), "start_time_ticks": int(tail[19])}


def parse_status(text: str) -> dict:
    result = {"rss_bytes": None, "rss_high_water_bytes": None, "errors": []}
    names = {"VmRSS": "rss_bytes", "VmHWM": "rss_high_water_bytes"}
    for line in text.splitlines():
        key, sep, value = line.partition(":")
        if sep and key in names:
            try:
                result[names[key]] = quantity(value, status=True)
            except ValueError as error:
                result["errors"].append({"field": key, "error": str(error), "raw": value.strip()})
    return result


def parse_fdinfo(text: str) -> dict | None:
    fields = {}
    for line in text.splitlines():
        key, sep, value = line.partition(":")
        if sep and key.startswith("drm-"):
            fields[key] = value.strip()
    if not fields:
        return None
    result = {"driver": fields.get("drm-driver"), "device": fields.get("drm-pdev"),
              "client_id": None, "regions": {}, "unknown_fields": {}, "errors": []}
    client = fields.get("drm-client-id")
    if client is not None:
        if re.fullmatch(r"[0-9]+", client):
            result["client_id"] = str(int(client))
        else:
            result["errors"].append({"field": "drm-client-id", "error": "invalid client identity"})
    for key, raw in fields.items():
        if key in ("drm-driver", "drm-pdev", "drm-client-id"):
            continue
        match = re.fullmatch(r"drm-(total|resident|shared|active|purgeable|memory)-(\S+)", key)
        if not match:
            result["unknown_fields"][key] = raw
            continue
        region = result["regions"].setdefault(match[2], {})
        try:
            region[PREFIXES[match[1]] + "_bytes"] = quantity(raw)
        except ValueError as error:
            result["errors"].append({"field": key, "error": str(error), "raw": raw})
    for name, region in result["regions"].items():
        if "legacy_resident_bytes" in region:
            legacy = region.pop("legacy_resident_bytes")
            if "resident_bytes" in region and region["resident_bytes"] != legacy:
                result["errors"].append({"field": name, "error": "resident alias disagrees"})
            if "resident_bytes" not in region:
                region["resident_bytes"] = legacy
                region["resident_source"] = "drm-memory alias"
            else:
                region["resident_source"] = "drm-resident (legacy alias not added)"
    return result


def aggregate_drm(entries: list[dict]) -> dict:
    """Deduplicate full client snapshots, never independently maximize fields."""
    clients = {}
    unidentified = []
    for entry in entries:
        reference = {key: entry[key] for key in ("pid", "start_time_ticks", "fd")}
        if entry["client_id"] is None or not entry["device"] or not entry["driver"]:
            unidentified.append({**entry, "deduplication_status": "device, driver or client identity unavailable"})
            continue
        key = (entry["device"], entry["client_id"])
        if key not in clients:
            clients[key] = {**entry, "references": [reference], "duplicate_snapshot_changed": False}
        else:
            chosen = clients[key]
            chosen["references"].append(reference)
            chosen["duplicate_snapshot_changed"] |= chosen["regions"] != entry["regions"]
    rows = list(clients.values())
    regions = [region for row in rows for region in row["regions"].values()]
    result = {"clients": rows, "unidentified_clients": unidentified,
              "deduplicated_client_count": len(rows), "coverage": {},
              "staging_bytes": None, "pending_retirement_bytes": None,
              "category_status": "staging and retirement are not observable through fdinfo"}
    for field in MEMORY_FIELDS:
        key = field + "_bytes"
        values = [region[key] for region in regions if key in region]
        known = sum(values) if values else None
        complete = bool(rows) and bool(regions) and len(values) == len(regions) \
            and all(row["regions"] for row in rows) and not unidentified
        result[key] = known if complete else None
        result[field + "_known_bytes"] = known
        result["coverage"][field] = {"regions_with_value": len(values),
                                     "observed_regions": len(regions), "complete_for_observed_clients": complete}
    return result


class History:
    def __init__(self, capacity: int, process_capacity: int = MAX_PROCESS_HIGH_WATER_RECORDS):
        if not 1 <= capacity <= 10000:
            raise ValueError("history capacity must be 1..10000")
        if not 1 <= process_capacity <= MAX_PROCESS_HIGH_WATER_RECORDS:
            raise ValueError("process history capacity is outside its supported bound")
        self.samples = deque(maxlen=capacity)
        self.count = 0
        self.peaks = {}
        self.process_high_water = {}
        self.process_capacity = process_capacity
        self.dropped_process_high_water_observations = 0
        self.error_sample_count = 0
        self.unidentified_gpu_sample_count = 0

    def append(self, sample: dict) -> None:
        self.count += 1
        self.samples.append(sample)
        self.error_sample_count += bool(sample.get("errors") or
            any(process.get("errors") for process in sample["processes"]) or
            any(client.get("errors") for client in sample["gpu"]["clients"]))
        self.unidentified_gpu_sample_count += bool(sample["gpu"]["unidentified_clients"])
        candidates = {"rss_sum_bytes": sample["rss_sum_bytes"],
                      "rss_sum_known_bytes": sample.get("rss_sum_known_bytes"), **{
            "gpu_" + key: value for key, value in sample["gpu"].items()
            if key.endswith("_bytes") and key not in ("staging_bytes", "pending_retirement_bytes")}}
        for key, value in candidates.items():
            if value is not None and (key not in self.peaks or value > self.peaks[key]["bytes"]):
                self.peaks[key] = {"bytes": value, "elapsed_seconds": sample["elapsed_seconds"],
                                   "phase": sample["phase"]}
        for process in sample["processes"]:
            key = f"{process['pid']}:{process['start_time_ticks']}"
            value = process["rss_high_water_bytes"]
            if value is not None:
                if key in self.process_high_water or len(self.process_high_water) < self.process_capacity:
                    self.process_high_water[key] = max(value, self.process_high_water.get(key, 0))
                else:
                    self.dropped_process_high_water_observations += 1


class ProcessProbe:
    def __init__(self, root_pid: int, *, proc_root: Path = Path("/proc"), capacity: int = 2048):
        self.proc = proc_root
        self.root_pid = root_pid
        self.root_identity = parse_stat((self.proc / str(root_pid) / "stat").read_text())
        self.started = time.monotonic()
        self.started_unix_seconds = time.time()
        self.history = History(capacity)

    @staticmethod
    def error(path: Path, error: Exception) -> dict:
        return {"path": str(path), "error": type(error).__name__, "message": str(error)}

    def sample(self, phase: str = "process") -> dict:
        began = time.monotonic()
        errors, processes, drm = [], [], []
        todo, visited = [(self.root_pid, None)], set()
        remaining_fdinfos = MAX_FDINFO_PER_SAMPLE
        root_alive = True

        def bounded_entries(path: Path, limit: int) -> list[Path]:
            entries = list(islice(path.iterdir(), limit + 1))
            if len(entries) > limit:
                errors.append({"path": str(path), "error": "traversal_limit", "limit": limit})
            return sorted(entries[:limit], key=lambda p: int(p.name))

        while todo:
            pid, parent = todo.pop(0)
            if pid in visited:
                continue
            visited.add(pid)
            base = self.proc / str(pid)
            try:
                identity = parse_stat((base / "stat").read_text())
                if pid == self.root_pid and identity["start_time_ticks"] != self.root_identity["start_time_ticks"]:
                    raise ValueError("root PID was reused; refusing unrelated process")
                if parent is not None:
                    parent_now = parse_stat((self.proc / str(parent[0]) / "stat").read_text())
                    if identity["parent_pid"] != parent[0] or parent_now["start_time_ticks"] != parent[1]:
                        raise ValueError("child ancestry changed; refusing unrelated process")
                # Only children obtained from this scoped parent's task files are followed.
                children = []
                for task in bounded_entries(base / "task", MAX_THREADS_PER_PROCESS):
                    child_path = task / "children"
                    try:
                        child_ids = child_path.read_text().split()
                        if len(child_ids) > MAX_CHILDREN_PER_THREAD:
                            errors.append({"path": str(child_path), "error": "traversal_limit",
                                           "limit": MAX_CHILDREN_PER_THREAD})
                        for child in child_ids[:MAX_CHILDREN_PER_THREAD]:
                            child_pid = int(child)
                            if child_pid not in children:
                                if len(children) >= MAX_PROCESSES_PER_SAMPLE:
                                    errors.append({"path": str(child_path), "error": "child_collection_limit",
                                                   "limit": MAX_PROCESSES_PER_SAMPLE})
                                    break
                                children.append(child_pid)
                    except (OSError, ValueError) as error:
                        errors.append(self.error(child_path, error))
                status = parse_status((base / "status").read_text())
                processes.append({**identity, **status})
                descriptors = bounded_entries(base / "fdinfo", remaining_fdinfos)
                remaining_fdinfos -= len(descriptors)
                for descriptor in descriptors:
                    try:
                        entry = parse_fdinfo(descriptor.read_text())
                        if entry is not None:
                            drm.append({**entry, "pid": pid, "start_time_ticks": identity["start_time_ticks"],
                                        "fd": int(descriptor.name)})
                    except (OSError, ValueError) as error:
                        errors.append(self.error(descriptor, error))
                # A process can disappear/reuse its PID during the non-atomic scan.
                after = parse_stat((base / "stat").read_text())
                if identity["start_time_ticks"] != after["start_time_ticks"]:
                    raise ValueError("PID changed during sample")
                for child in children:
                    if child in visited or any(queued[0] == child for queued in todo):
                        continue
                    if len(visited) + len(todo) >= MAX_PROCESSES_PER_SAMPLE:
                        errors.append({"path": str(base), "error": "process_traversal_limit",
                                       "limit": MAX_PROCESSES_PER_SAMPLE})
                        break
                    todo.append((child, (pid, identity["start_time_ticks"])))
            except (OSError, ValueError) as error:
                errors.append(self.error(base, error))
                processes[:] = [process for process in processes if process["pid"] != pid]
                drm[:] = [entry for entry in drm if entry["pid"] != pid]
                if pid == self.root_pid:
                    root_alive = False
                    break
        rss = [process["rss_bytes"] for process in processes if process["rss_bytes"] is not None]
        gpu = aggregate_drm(drm)
        gpu["scan_complete"] = not errors
        if errors:
            for field in MEMORY_FIELDS:
                gpu[field + "_bytes"] = None
                gpu["coverage"][field]["complete_for_observed_clients"] = False
        sample = {"elapsed_seconds": began - self.started, "scan_seconds": time.monotonic() - began,
                  "phase": phase[:80], "root_alive": root_alive, "processes": processes,
                  "rss_sum_bytes": sum(rss) if rss and len(rss) == len(processes) and not errors else None,
                  "rss_sum_known_bytes": sum(rss) if rss else None, "gpu": gpu, "errors": errors}
        self.history.append(sample)
        return sample

    def report(self, *, interval_seconds: float, stop_reason: str) -> dict:
        required = ("rss_sum_bytes", "gpu_requested_bytes", "gpu_resident_bytes")
        available = [key for key in required if key in self.history.peaks]
        status = "unavailable" if not self.history.peaks else "partial"
        if len(available) == len(required) and not (self.history.error_sample_count or
                self.history.unidentified_gpu_sample_count or self.history.dropped_process_high_water_observations):
            status = "complete"
        return {"schema_version": 1, "kind": "instrumented process-tree memory observations",
                "status": status,
                "status_scope": "RSS and identified DRM requested/resident samples; excludes unobservable driver/category coverage",
                "missing_required_metrics": [key for key in required if key not in available],
                "samples_with_errors": self.history.error_sample_count,
                "samples_with_unidentified_gpu_clients": self.history.unidentified_gpu_sample_count,
                "root": self.root_identity, "started_unix_seconds": self.started_unix_seconds,
                "interval_seconds": interval_seconds, "stop_reason": stop_reason,
                "sample_count": self.history.count, "retained_sample_count": len(self.history.samples),
                "dropped_history_samples": self.history.count - len(self.history.samples),
                "sampled_peaks": self.history.peaks,
                "kernel_rss_high_water_by_process": self.history.process_high_water,
                "dropped_process_high_water_observations": self.history.dropped_process_high_water_observations,
                "limits": {"processes_per_sample": MAX_PROCESSES_PER_SAMPLE,
                           "threads_per_process": MAX_THREADS_PER_PROCESS,
                           "fdinfo_files_per_sample": MAX_FDINFO_PER_SAMPLE,
                           "children_per_thread": MAX_CHILDREN_PER_THREAD,
                           "process_high_water_records": self.history.process_capacity},
                "samples": list(self.history.samples), "limitations": LIMITATIONS,
                "sources": ["https://dri.freedesktop.org/docs/drm/gpu/drm-usage-stats.html",
                            "https://www.kernel.org/doc/html/latest/filesystems/proc.html"]}


def write_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n")
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root-pid", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--max-samples", type=int, default=2048)
    parser.add_argument("--duration", type=float, default=600)
    parser.add_argument("--phase-file", type=Path)
    args = parser.parse_args()
    if not 0.1 <= args.interval <= 0.25 or not 0 < args.duration <= 3600:
        parser.error("interval must be 0.1..0.25 seconds; duration must be 0..3600 seconds")
    probe = ProcessProbe(args.root_pid, capacity=args.max_samples)
    running = True

    def stop(_signal, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    print(json.dumps({"ready": True, "root": probe.root_identity}), flush=True)
    reason = "stop_requested"
    while running:
        phase = "process"
        if args.phase_file:
            try:
                phase = args.phase_file.read_text().strip()[:80]
            except OSError:
                phase = "phase_unavailable"
        sample = probe.sample(phase)
        if not sample["root_alive"]:
            reason = "root_exited_or_reused"
            break
        if sample["elapsed_seconds"] >= args.duration:
            reason = "duration_elapsed"
            break
        time.sleep(args.interval)
    write_report(args.output, probe.report(interval_seconds=args.interval, stop_reason=reason))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
