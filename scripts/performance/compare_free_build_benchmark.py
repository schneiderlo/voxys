#!/usr/bin/env python3
"""Compare matching, repeated same-host creative-core benchmark runs.

Before aggregation, both inputs must have finite positive ordered latency
quantiles, finite positive RSS/throughput, nonnegative integer outcome counts,
and positive integer accepted-edit sample counts (or null for idle workloads).
"""
import argparse
import json
import math
import pathlib
import statistics


def positive_finite(value):
    if type(value) not in (int, float):
        return False
    try:
        return value > 0 and math.isfinite(value)
    except OverflowError:
        return False


def invalid_metrics(metrics):
    # Ratios require strictly positive, finite measurements. Counts must be
    # integers (not JSON booleans); idle accepted-edit timings stay null.
    errors = []
    for operation in ("update", "serialization", "accepted_edit_update"):
        distribution = metrics.get(operation)
        if operation == "accepted_edit_update" and distribution is None:
            continue
        quantiles = ([distribution.get(key) for key in ("p50_us", "p95_us", "p99_us")]
                     if isinstance(distribution, dict) else [])
        if not quantiles or not all(positive_finite(value) for value in quantiles):
            errors.append(f"{operation}: latency quantiles must be finite and positive")
        elif quantiles != sorted(quantiles):
            errors.append(f"{operation}: latency quantiles must be nondecreasing")
    for key in ("peak_rss_kib", "updates_per_second"):
        if not positive_finite(metrics.get(key)):
            errors.append(f"{key} must be finite and positive")
    for key in ("accepted_placements", "removed_parts", "valid_observations", "invalid_observations"):
        value = metrics.get(key)
        if type(value) is not int or value < 0:
            errors.append(f"{key} must be a nonnegative integer")
    return errors


parser = argparse.ArgumentParser()
parser.add_argument("baseline", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
args = parser.parse_args()
failures = []
groups = {}
expected = {f"{parts}-{workload}-{repeat}.metrics.json"
            for parts in (0, 64, 256, 768)
            for workload in ("idle", "edit") for repeat in (1, 2, 3)}
for directory in (args.baseline, args.candidate):
    found = {p.name for p in directory.glob("*.metrics.json")}
    if found != expected:
        raise SystemExit(f"Incomplete or unexpected workload matrix in {directory}: "
                         f"missing={sorted(expected - found)}, extra={sorted(found - expected)}")
files = sorted(args.baseline.glob("*.metrics.json"))
for source in files:
    other = args.candidate / source.name
    if not other.is_file():
        failures.append(f"Missing {other}")
        continue
    baseline, candidate = (json.loads(p.read_text()) for p in (source, other))
    failures_before = len(failures)
    for label, metrics in (("baseline", baseline), ("candidate", candidate)):
        failures.extend(f"{source.name}: {label} {error}" for error in invalid_metrics(metrics))
        accepted_edits = metrics.get("accepted_edit_update")
        if "accepted_edit_update" not in metrics or (metrics["workload"] == "edit" and
                (not isinstance(accepted_edits, dict) or type(accepted_edits.get("count")) is not int
                 or accepted_edits["count"] <= 0)):
            failures.append(f"{source.name}: {label} missing accepted edit timings")
        elif metrics["workload"] == "idle" and accepted_edits is not None:
            failures.append(f"{source.name}: {label} idle workload accepted an edit")
    if isinstance(baseline.get("accepted_edit_update"), dict) and isinstance(candidate.get("accepted_edit_update"), dict):
        if baseline["accepted_edit_update"].get("count") != candidate["accepted_edit_update"].get("count"):
            failures.append(f"{source.name}: accepted edit timing count differs")
    measurements_valid = len(failures) == failures_before
    for key in ("scope", "parts", "frames", "workload", "accepted_placements", "removed_parts",
                "valid_observations", "invalid_observations", "archive_sha256", "terrain_sha256"):
        if baseline[key] != candidate[key]:
            failures.append(f"{source.name}: {key} differs")
    stem = source.name.removesuffix(".metrics.json")
    for suffix in (".jsonl", ".save"):
        if (args.baseline / (stem + suffix)).read_bytes() != (args.candidate / (stem + suffix)).read_bytes():
            failures.append(f"{stem + suffix}: exact output mismatch")
    if measurements_valid:
        groups.setdefault((baseline["parts"], baseline["workload"]), []).append((baseline, candidate))

report = []
for (parts, workload), pairs in sorted(groups.items()):
    if len(pairs) != 3:
        continue  # A rejected repeat cannot contribute to performance medians.
    item = {"parts": parts, "workload": workload, "repeats": len(pairs)}
    for operation in ("update", "serialization", "accepted_edit_update"):
        if operation == "accepted_edit_update" and (workload == "idle" or
                any(not isinstance(metrics.get(operation), dict) for pair in pairs for metrics in pair)):
            continue
        result = {}
        for metric, ratio_limit in (("p50_us", 1.10), ("p95_us", 1.15), ("p99_us", 1.20)):
            before = statistics.median(b[operation][metric] for b, _ in pairs)
            after = statistics.median(c[operation][metric] for _, c in pairs)
            # Small workloads need a fixed noise allowance; ratios near zero
            # are not reliable. Keep limits visible and fixed before changes.
            limit = max(before * ratio_limit, before + 25)
            result[metric] = {"baseline": before, "candidate": after, "ratio": after / before, "limit": limit}
            if after > limit:
                failures.append(f"{parts}/{workload}/{operation}/{metric}: {after:.3f} > {limit:.3f}")
        item[operation] = result
    before_rss = statistics.median(b["peak_rss_kib"] for b, _ in pairs)
    after_rss = statistics.median(c["peak_rss_kib"] for _, c in pairs)
    item["peak_rss_kib"] = {"baseline": before_rss, "candidate": after_rss}
    if after_rss > max(before_rss * 1.10, before_rss + 10240):
        failures.append(f"{parts}/{workload}: peak RSS regression")
    item["updates_per_second"] = {
        "baseline": statistics.median(b["updates_per_second"] for b, _ in pairs),
        "candidate": statistics.median(c["updates_per_second"] for _, c in pairs),
    }
    if item["updates_per_second"]["candidate"] < item["updates_per_second"]["baseline"] * .85:
        failures.append(f"{parts}/{workload}: replay throughput regressed by more than 15%")
    report.append(item)
print(json.dumps({"groups": report, "failures": failures}, indent=2))
raise SystemExit(bool(failures))
