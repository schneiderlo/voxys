#!/usr/bin/env python3
"""Compare actual native/browser RGBA readbacks, preserving hashes and differences."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--browser", type=Path, required=True)
    parser.add_argument("--native-binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.output.exists():
        parser.error("output must be new")
    browser = json.loads((args.browser / "report.json").read_text())
    assert browser["passed"] and browser["result"]["exitCode"] == 0
    xml = ET.parse(args.native / "tests.xml").getroot()
    assert int(xml.attrib["tests"]) == 6 and int(xml.attrib["failures"]) == 0
    assert all(int(s.attrib.get("skipped", 0)) == 0 for s in xml.iter("testsuite"))
    build = json.loads((Path(browser["package"]) / "build.json").read_text())
    assert build["sources_unchanged"]
    assert all(sha(root / p) == h for p, h in build["sources"].items())
    assert all(sha(Path(browser["package"]) / p) == h["sha256"]
               for p, h in browser["artifacts"].items())
    captures = sorted(args.native.glob("*.rgba"), key=lambda p: int(p.name.split("-")[0]))
    assert len(captures) == len(browser["result"]["captures"]) == 18
    report = {"schema": 1, "passed": False, "maximum_allowed_channel_delta": 2,
              "purpose": "Actual rendered pixels, not timing, art acceptance or broad hardware coverage",
              "runner_sha256": sha(Path(__file__)), "shared_sources": build["sources"],
              "native_binary": {"path": str(args.native_binary.resolve()), "sha256": sha(args.native_binary)},
              "native_tests_sha256": sha(args.native / "tests.xml"),
              "browser_report_sha256": sha(args.browser / "report.json"),
              "browser_build_sha256": sha(Path(browser["package"]) / "build.json"), "captures": []}
    for native in captures:
        browser_file = args.browser / native.name
        a, b = native.read_bytes(), browser_file.read_bytes()
        assert len(a) == len(b) == 64 * 64 * 4
        recorded = next(c for c in browser["result"]["captures"] if c["name"] == native.name)
        assert sha(browser_file) == recorded["sha256"]
        native_metadata = json.loads(native.with_suffix(".json").read_text())
        assert native_metadata == recorded["metadata"]
        delta = [abs(x-y) for x, y in zip(a, b, strict=True)]
        report["captures"].append({"name": native.name, "native_sha256": sha(native),
            "browser_sha256": sha(browser_file), "identical": a == b,
            "maximum_channel_delta": max(delta), "mean_channel_delta": sum(delta)/len(delta),
            "channels_over_tolerance": sum(d > 2 for d in delta),
            "center_rgb": list(a[(32*64+32)*4:(32*64+32)*4+3]), "metadata": native_metadata})
    report["passed"] = all(c["channels_over_tolerance"] == 0 for c in report["captures"])
    report["all_pixels_identical"] = all(c["identical"] for c in report["captures"])
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    assert report["passed"], "Native/browser pixel comparison failed; details preserved"
    print(f"18 readbacks passed; identical bytes: {report['all_pixels_identical']}")


if __name__ == "__main__":
    main()
