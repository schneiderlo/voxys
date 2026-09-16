#!/usr/bin/env python3
"""Compare matching, repeated same-host creative-core benchmark runs."""
import argparse
import json
import pathlib
import statistics

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
    for key in ("scope", "parts", "frames", "workload", "accepted_placements", "removed_parts",
                "valid_observations", "invalid_observations", "archive_sha256", "terrain_sha256"):
        if baseline[key] != candidate[key]:
            failures.append(f"{source.name}: {key} differs")
    stem = source.name.removesuffix(".metrics.json")
    for suffix in (".jsonl", ".save"):
        if (args.baseline / (stem + suffix)).read_bytes() != (args.candidate / (stem + suffix)).read_bytes():
            failures.append(f"{stem + suffix}: exact output mismatch")
    groups.setdefault((baseline["parts"], baseline["workload"]), []).append((baseline, candidate))

report = []
for (parts, workload), pairs in sorted(groups.items()):
    item = {"parts": parts, "workload": workload, "repeats": len(pairs)}
    for operation in ("update", "serialization"):
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
