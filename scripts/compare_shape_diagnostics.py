#!/usr/bin/env python3
"""Compare paired real GPU readbacks; each suite also checks analytical values."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("browser", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--body-layout", action="store_true", help="Include the shipping-kernel mass/layout cases")
    parser.add_argument("--backend-owned", action="store_true", help="Include mass/layout and live backend ownership cases")
    parser.add_argument("--motion-frame", action="store_true", help="Include checked root/COM motion and actual sector readbacks")
    args = parser.parse_args()
    first = sorted(args.native.glob("*.u32"))
    second = sorted(args.browser.glob("*.u32"))
    expected = 16 if args.motion_frame else 14 if args.backend_owned else 12 if args.body_layout else 7
    if len(first) != expected or [p.name for p in first] != [p.name for p in second]:
        raise RuntimeError("Expected the same complete set of named GPU readbacks")
    # Defined integer outputs: validity/counts, source sum, leaves, invalid
    # access results and the shader failure word. All remaining rows are f32
    # outputs or zero padding; NaN/Inf cannot pass the difference comparison.
    integers = set(range(4)) | {4, 7} | set(range(32, 36)) | set(range(60, 64))
    captures = []
    for a, b in zip(first, second):
        raw_a, raw_b = a.read_bytes(), b.read_bytes()
        if len(raw_a) != 256 or len(raw_b) != 256:
            raise RuntimeError("Unexpected GPU readback size")
        words_a, words_b = struct.unpack("<64I", raw_a), struct.unpack("<64I", raw_b)
        floats_a, floats_b = struct.unpack("<64f", raw_a), struct.unpack("<64f", raw_b)
        defined_integers = ({55} | set(range(60,64))) if "-BodyKernel" in a.name else integers
        if "-BodyKernelConverted" in a.name:
            defined_integers |= set(range(32,36))  # Actual signed-sector/flags row.
        delta = [abs(floats_a[i] - floats_b[i]) for i in range(64) if i not in defined_integers]
        if not all(words_a[i] == words_b[i] for i in defined_integers) or not all(d <= 2e-5 for d in delta):
            raise RuntimeError("Native/browser compute mismatch: " + a.name)
        captures.append({"name": a.name, "exact_bytes_equal": raw_a == raw_b,
                         "maximum_f32_difference": max(delta),
                         "native_sha256": hashlib.sha256(raw_a).hexdigest(),
                         "browser_sha256": hashlib.sha256(raw_b).hexdigest()})
    report = {"passed": True, "integer_outputs": "exact", "f32_absolute_tolerance": 2e-5,
              "captures": captures}
    # Preserve prior attempts: callers must choose a fresh evidence filename.
    with args.output.open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print(json.dumps({"passed": True, "captures": len(captures), "output": str(args.output)}))


if __name__ == "__main__":
    main()
