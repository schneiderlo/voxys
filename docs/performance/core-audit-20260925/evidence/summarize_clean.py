#!/usr/bin/env python3
"""Summarize completed clean matrices; never launches tests or benchmarks.

Run only after all three comparison JSON files exist and the original aggregate
test has finished. Golden bytes are read once per file, in bounded chunks, while
hashing and comparing all three variants. Output is retained even on a failed
guard or golden check; the process then exits nonzero.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


VARIANTS = ("baseline", "village", "final")
PARTS = (0, 64, 256, 768)
MODES = ("idle", "edit")
QUANTILES = ("p50_us", "p95_us", "p99_us")
PAIRS = (
    ("baseline", "village", "village-comparison.json"),
    ("village", "final", "lookup-incremental-comparison.json"),
    ("baseline", "final", "final-comparison.json"),
)
EXPECTED_KEYS = {(parts, mode) for parts in PARTS for mode in MODES}
STEMS = [f"{parts}-{mode}-{repeat}" for parts in PARTS
         for mode in MODES for repeat in (1, 2, 3)]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text())


def positive(value):
    return type(value) in (int, float) and math.isfinite(value) and value > 0


def distribution_medians(runs, operation):
    if operation == "accepted_edit_update" and runs[0]["workload"] == "idle":
        require(all(run.get(operation) is None for run in runs),
                "Idle replay unexpectedly contains accepted-edit timings")
        return None
    result = {}
    for run in runs:
        values = run.get(operation)
        require(isinstance(values, dict), f"Missing {operation}")
        samples = values.get("count")
        require(type(samples) is int and samples > 0, f"Invalid {operation} count")
        quantiles = [values.get(key) for key in QUANTILES]
        require(all(positive(value) for value in quantiles),
                f"Invalid {operation} quantiles")
        require(quantiles == sorted(quantiles), f"Unordered {operation} quantiles")
    result.update({key: statistics.median(run[operation][key] for run in runs)
                   for key in QUANTILES})
    result["sample_counts_by_repeat"] = [run[operation]["count"] for run in runs]
    return result


def test_summary(path):
    source = path.read_bytes()
    text = source.decode("utf-8", errors="replace")
    total = re.findall(r"^\[=+\]\s+(\d+) tests? from (\d+) test suites? ran\.",
                       text, re.MULTILINE)
    require(total, f"Aggregate test log has no completed GTest footer: {path}")
    result = {"path": str(path), "sha256": hashlib.sha256(source).hexdigest(),
              "total": int(total[-1][0]), "suites": int(total[-1][1])}
    for status in ("PASSED", "FAILED", "SKIPPED"):
        counts = re.findall(r"^\[\s*" + status + r"\s*\]\s+(\d+) tests?\b",
                            text, re.MULTILINE)
        result[status.lower()] = int(counts[-1]) if counts else 0
        if status != "PASSED":
            names = re.findall(r"^\[\s*" + status + r"\s*\]\s+([^\s]+\.[^\s]+)",
                               text, re.MULTILINE)
            result[status.lower() + "_names"] = sorted(set(names))
    disabled = re.findall(r"YOU HAVE (\d+) DISABLED TESTS?", text)
    result["disabled"] = int(disabled[-1]) if disabled else 0
    result["counts_reconcile"] = (
        result["passed"] + result["failed"] + result["skipped"] == result["total"])
    require(result["counts_reconcile"], "Aggregate GTest footer counts do not reconcile")
    require(len(result["failed_names"]) == result["failed"],
            "Aggregate failed-test names do not match footer")
    require(len(result["skipped_names"]) == result["skipped"],
            "Aggregate skipped-test names do not match footer")
    return result


def number(value):
    return f"{value:,.2f}"


def quantiles(value, divisor=1):
    return " / ".join(number(value[key] / divisor) for key in QUANTILES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path("/tmp/voxys-core-audit-20260925"))
    parser.add_argument("--workspace", type=Path, default=Path.cwd())
    parser.add_argument("--aggregate-log", type=Path)
    args = parser.parse_args()
    root, workspace = args.root.resolve(), args.workspace.resolve()

    # All comparisons and test completion must be present before any large
    # golden read. Parsing an incomplete report fails closed.
    comparisons = []
    for before, after, filename in PAIRS:
        report = read_json(root / filename)
        require(isinstance(report.get("failures"), list), f"Incomplete {filename}")
        require(isinstance(report.get("groups"), list), f"Incomplete {filename}")
        keys = [(group.get("parts"), group.get("workload")) for group in report["groups"]]
        require(len(keys) == 8 and set(keys) == EXPECTED_KEYS,
                f"Incomplete or duplicate comparison groups in {filename}")
        comparisons.append({"before": before, "after": after,
                            "path": filename, "report": report})
    aggregate_log = args.aggregate_log or workspace / "bazel-testlogs/tests/voxy_tests/test.log"
    aggregate = test_summary(aggregate_log)

    raw, metrics = [], {}
    for variant in VARIANTS:
        found = {path.name for path in (root / variant).glob("*.metrics.json")}
        expected = {stem + ".metrics.json" for stem in STEMS}
        require(found == expected, f"Incomplete/unexpected metrics in {variant}")
        for stem in STEMS:
            path = root / variant / (stem + ".metrics.json")
            source = path.read_bytes()
            row = json.loads(source)
            parts, mode, repeat = stem.split("-")
            require(row.get("parts") == int(parts) and row.get("workload") == mode
                    and row.get("frames") == 3600, f"Metric identity differs: {path}")
            require(positive(row.get("peak_rss_kib")) and positive(row.get("updates_per_second")),
                    f"Invalid RSS/throughput: {path}")
            metrics[variant, stem] = row
            raw.append({"variant": variant, "parts": int(parts), "workload": mode,
                        "repeat": int(repeat), "path": str(path.relative_to(root)),
                        "sha256": hashlib.sha256(source).hexdigest(), "metrics": row})

    medians, indexed = [], {}
    for parts in PARTS:
        for mode in MODES:
            group = {"parts": parts, "workload": mode, "variants": {}}
            for variant in VARIANTS:
                runs = [metrics[variant, f"{parts}-{mode}-{repeat}"] for repeat in (1, 2, 3)]
                values = {op: distribution_medians(runs, op) for op in
                          ("update", "serialization", "accepted_edit_update")}
                for scalar in ("peak_rss_kib", "updates_per_second", "startup_ms", "replay_us"):
                    values[scalar] = statistics.median(run[scalar] for run in runs)
                values["peak_rss_mib"] = values["peak_rss_kib"] / 1024
                values["outcomes_by_repeat"] = [{key: run[key] for key in
                    ("accepted_placements", "removed_parts", "valid_observations", "invalid_observations")}
                    for run in runs]
                group["variants"][variant] = values
                indexed[parts, mode, variant] = values
            medians.append(group)

    failures = []
    for comparison in comparisons:
        failures.extend(f"{comparison['path']}: {message}"
                        for message in comparison["report"]["failures"])
        # Ensure stale reports cannot describe another set of medians.
        for group in comparison["report"]["groups"]:
            require(group.get("repeats") == 3, "Comparison repeat count is not three")
            for side, variant in (("baseline", comparison["before"]),
                                  ("candidate", comparison["after"])):
                expected = indexed[group["parts"], group["workload"], variant]
                for op in ("update", "serialization", "accepted_edit_update"):
                    if expected[op] is None:
                        continue
                    for key in QUANTILES:
                        require(group[op][key][side] == expected[op][key],
                                f"Stale comparison {comparison['path']}: {op}/{key}")
                for scalar in ("peak_rss_kib", "updates_per_second"):
                    require(group[scalar][side] == expected[scalar],
                            f"Stale comparison {comparison['path']}: {scalar}")

    manifest, pair_checks = [], []
    manifest_index = {}
    for stem in STEMS:
        for suffix in (".jsonl", ".save"):
            paths = [root / variant / (stem + suffix) for variant in VARIANTS]
            stats_before = [path.stat() for path in paths]
            hashes = [hashlib.sha256() for _ in paths]
            sizes = [0, 0, 0]
            equals = {(0, 1): True, (1, 2): True, (0, 2): True}
            with ExitStack() as stack:
                streams = [stack.enter_context(path.open("rb")) for path in paths]
                while True:
                    chunks = [stream.read(1024 * 1024) for stream in streams]
                    if not any(chunks):
                        break
                    for index, chunk in enumerate(chunks):
                        hashes[index].update(chunk)
                        sizes[index] += len(chunk)
                    for pair in equals:
                        equals[pair] = equals[pair] and chunks[pair[0]] == chunks[pair[1]]
            for index, variant in enumerate(VARIANTS):
                stat_after = paths[index].stat()
                require(stat_after.st_size == stats_before[index].st_size == sizes[index]
                        and stat_after.st_mtime_ns == stats_before[index].st_mtime_ns,
                        f"Golden changed during summary: {paths[index]}")
                item = {"variant": variant, "stem": stem, "type": suffix[1:],
                        "path": str(paths[index].relative_to(root)), "bytes": sizes[index],
                        "sha256": hashes[index].hexdigest()}
                manifest.append(item)
                manifest_index[variant, stem, suffix] = item
                expected = metrics[variant, stem]
                if suffix == ".save" and item["sha256"] != expected["archive_sha256"]:
                    failures.append(f"Archive hash does not match metrics: {item['path']}")
                if suffix == ".jsonl" and item["bytes"] != expected["retained_golden_bytes"]:
                    failures.append(f"Golden size does not match metrics: {item['path']}")
            for (left, right), equal in equals.items():
                pair_checks.append({"before": VARIANTS[left], "after": VARIANTS[right],
                                    "stem": stem, "type": suffix[1:], "exact_bytes_equal": equal})
                if not equal:
                    failures.append(f"Golden mismatch: {VARIANTS[left]}/{VARIANTS[right]} {stem}{suffix}")

    repeat_consistency = []
    for variant in VARIANTS:
        for parts in PARTS:
            for mode in MODES:
                for suffix in (".jsonl", ".save"):
                    items = [manifest_index[variant, f"{parts}-{mode}-{repeat}", suffix]
                             for repeat in (1, 2, 3)]
                    equal = len({(item["bytes"], item["sha256"]) for item in items}) == 1
                    repeat_consistency.append({"variant": variant, "parts": parts,
                        "workload": mode, "type": suffix[1:], "hash_and_size_equal": equal})
                    if not equal:
                        failures.append(f"Repeated golden hashes differ: {variant}/{parts}/{mode}/{suffix}")

    golden = {"files": manifest, "pair_checks": pair_checks,
              "repeat_consistency": repeat_consistency,
              "pair_comparison_method": "Exact simultaneous byte comparison in 1 MiB chunks; SHA-256 also recorded"}
    tests = {"original_native_aggregate": aggregate}
    focused = root / "focused-test-results.json"
    if focused.is_file():
        tests["candidate_focused_results"] = read_json(focused)
    importer = workspace / "bazel-testlogs/tools/terrain_diffusion_import_test/test.log"
    if importer.is_file():
        text = importer.read_text(errors="replace")
        counts = re.findall(r"Ran (\d+) tests? in", text)
        tests["terrain_importer"] = {"path": str(importer),
            "tests": int(counts[-1]) if counts else None,
            "unittest_ok_footer": bool(re.search(r"^OK(?:\s*\([^\n]*\))?\s*$", text, re.MULTILINE)),
            "failed_footer": bool(re.search(r"^FAILED\b", text, re.MULTILINE))}

    result = {"scope": "Native creative CPU component; no rendered FPS or input-to-photon latency",
              "source_scope": "Retained benchmark binary snapshots and original aggregate binary; not a claim about later concurrent workspace changes",
              "aggregation": "Median of three per-run quantiles/scalars; never pooled quantiles",
              "accepted_edit_tail_caveat": "About ten accepted edits per run; nearest-rank p95/p99 are observed maxima, not established population tails",
              "raw_metrics_count": len(raw), "raw_metrics": raw, "medians": medians,
              "comparisons": comparisons, "golden_files": len(manifest),
              "golden_pair_checks": len(pair_checks), "test_summary": tests,
              "measurement_and_golden_failures": failures,
              "measurement_and_golden_guards_pass": not failures}
    require(len(raw) == 72 and len(manifest) == 144 and len(pair_checks) == 144,
            "Unexpected output matrix dimensions")

    lines = ["# Clean CPU component measurements", "",
             "Median of three per-run quantiles/scalars. Times are microseconds except accepted-edit rows, which use milliseconds. Throughput includes replay observation bookkeeping. Peak RSS is whole-process peak, including startup and retained goldens. No displayed FPS claim.", "",
             "## Accepted-edit update latency", "",
             "Each triple is p50 / p95 / p99 in ms. Approximately ten samples per run means p95/p99 are observed maxima, not established population-tail estimates.", "",
             "| Parts | Baseline | Village only | Combined | Samples/repeat (B; V; F) |",
             "|---:|---:|---:|---:|---|"]
    for parts in PARTS:
        values = [indexed[parts, "edit", variant]["accepted_edit_update"] for variant in VARIANTS]
        counts = "; ".join("/".join(map(str, value["sample_counts_by_repeat"])) for value in values)
        lines.append(f"| {parts} | " + " | ".join(quantiles(value, 1000) for value in values) + f" | {counts} |")
    lines += ["", "## All-update latency, throughput and memory", "",
              "Latency triples are p50 / p95 / p99 in µs. Arrows show baseline → combined.", "",
              "| Parts | Replay | Baseline latency | Combined latency | Updates/s | Peak RSS MiB |",
              "|---:|---|---:|---:|---:|---:|"]
    for parts in PARTS:
        for mode in MODES:
            before, after = (indexed[parts, mode, variant] for variant in ("baseline", "final"))
            lines.append(f"| {parts} | {mode} | {quantiles(before['update'])} | {quantiles(after['update'])} | "
                f"{number(before['updates_per_second'])} → {number(after['updates_per_second'])} | "
                f"{number(before['peak_rss_mib'])} → {number(after['peak_rss_mib'])} |")
    lines += ["", "## Serialization latency", "",
              "Triples are p50 / p95 / p99 in µs.", "",
              "| Parts | Replay | Baseline | Combined |", "|---:|---|---:|---:|"]
    for parts in PARTS:
        for mode in MODES:
            before, after = (indexed[parts, mode, variant] for variant in ("baseline", "final"))
            lines.append(f"| {parts} | {mode} | {quantiles(before['serialization'])} | {quantiles(after['serialization'])} |")
    lines += ["", "## Oracle and tests", "",
              f"72 metric records; 144 golden files; 144 exact cross-variant file comparisons. Measurement/golden guard failures: {len(failures)}.", "",
              f"Original native aggregate: {aggregate['total']} tests; {aggregate['passed']} passed, {aggregate['failed']} failed, {aggregate['skipped']} skipped; {aggregate['disabled']} disabled.", ""]
    for name in aggregate["failed_names"]:
        lines.append(f"- Failed: `{name}`")
    for name in aggregate["skipped_names"]:
        lines.append(f"- Skipped: `{name}`")
    if failures:
        lines += ["", "Measurement/golden failures:", ""] + ["- " + message for message in failures]
    lines += ["", "These measurements describe the retained benchmark binary snapshots and original aggregate binary; they do not establish behavior of later concurrent workspace changes. Candidate focused results, including any failures, remain separate in test-summary.json. Full raw per-run values, all intermediate medians and all comparator failures are in clean-summary.json; golden hashes and exact byte-comparison results are in golden-manifest.json.", ""]
    for filename, payload in (("clean-summary.json", result), ("golden-manifest.json", golden),
                              ("test-summary.json", tests)):
        (root / filename).write_text(json.dumps(payload, indent=2, allow_nan=False) + "\n")
    (root / "clean-summary.md").write_text("\n".join(lines))
    print(json.dumps({"raw_metrics": len(raw), "golden_files": len(manifest),
                      "golden_pair_checks": len(pair_checks), "guard_failures": len(failures),
                      "native_failed": aggregate["failed"], "output": str(root / "clean-summary.md")}))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
