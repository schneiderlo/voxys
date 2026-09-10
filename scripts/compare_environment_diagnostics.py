#!/usr/bin/env python3
"""Compare actual linear GPU readbacks and request identity across runtimes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def digest(data):
    return hashlib.sha256(data).hexdigest()


def compare(reference, candidate):
    names = sorted(p.name for p in reference.glob("*.f32") if not p.name.endswith(".requests.f32"))
    candidate_names = sorted(p.name for p in candidate.glob("*.f32") if not p.name.endswith(".requests.f32"))
    if len(names) != 5 or names != candidate_names:
        raise ValueError("expected all five matched GPU readbacks")
    rows = []
    for name in names:
        left, right = (reference / name).read_bytes(), (candidate / name).read_bytes()
        request_name = name[:-4] + ".requests.f32"
        requests = (reference / request_name).read_bytes()
        if requests != (candidate / request_name).read_bytes():
            raise ValueError(f"{name}: request data differs")
        if len(left) != len(right) or len(left) != len(requests) or len(left) % 16 or not left:
            raise ValueError(f"{name}: incomplete RGBA float32 readback")
        a, b = struct.unpack(f"<{len(left)//4}f", left), struct.unpack(f"<{len(right)//4}f", right)
        if not all(math.isfinite(v) for v in (*a, *b)):
            raise ValueError(f"{name}: nonfinite output")
        maximum = max(abs(x-y) for x, y in zip(a, b))
        rows.append({"file": name, "rgba_values": len(a)//4, "maximum_absolute_error": maximum,
                     "reference_sha256": digest(left), "candidate_sha256": digest(right),
                     "requests_sha256": digest(requests), "passed": maximum <= .002})
    return {"reference": str(reference), "candidate": str(candidate), "absolute_tolerance": .002,
            "purpose": "cross-runtime transport comparison; analytic C++ checks remain independently required",
            "passed": all(row["passed"] for row in rows), "readbacks": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = compare(args.reference, args.candidate)
    with args.output.open("x") as out:
        json.dump(result, out, indent=2)
        out.write("\n")
    print(json.dumps({"passed": result["passed"], "readbacks": len(result["readbacks"]),
                      "maximum_absolute_error": max(r["maximum_absolute_error"] for r in result["readbacks"])}))
    if not result["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
