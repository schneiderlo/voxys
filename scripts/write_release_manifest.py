#!/usr/bin/env python3
"""Write the release file manifest into a Pages deployment's index.html.

The page downloads the WASM and data packs under their content hashes and
keeps the packs in Cache Storage, so it needs each file's SHA-256 and size
before any script runs. The core pack is what every visitor downloads before
the default experience starts; --max-core-mib fails the release when it grows
past the agreed budget, so new content is a deliberate decision.
"""

import argparse
import hashlib
import json
from pathlib import Path

PLACEHOLDER = "__VOXY_RELEASE_FILES__"


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            sha.update(chunk)
    return sha.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", type=Path, help="Deployment directory")
    parser.add_argument("--max-core-mib", type=float, required=True)
    args = parser.parse_args()

    files = sorted([args.site / "voxy_wasm.wasm", *args.site.glob("voxy_*.data")])
    manifest = {}
    for path in files:
        size = path.stat().st_size
        if size == 0:
            raise SystemExit(f"ERROR: empty release file {path.name}")
        manifest[path.name] = {"sha256": digest(path), "size": size}
    if "voxy_wasm.data" not in manifest:
        raise SystemExit("ERROR: core data pack voxy_wasm.data is missing")

    core_mib = manifest["voxy_wasm.data"]["size"] / (1 << 20)
    for name, entry in manifest.items():
        print(f"{name:24} {entry['size'] / (1 << 20):9.2f} MiB  {entry['sha256']}")
    if core_mib > args.max_core_mib:
        raise SystemExit(
            f"ERROR: voxy_wasm.data is {core_mib:.2f} MiB, over the {args.max_core_mib} MiB "
            "startup budget. Put experience-specific content in an optional pack, "
            "or raise the budget in pages.yml deliberately.")

    index = args.site / "index.html"
    html = index.read_text()
    if html.count(PLACEHOLDER) != 1:
        raise SystemExit(f"ERROR: expected one {PLACEHOLDER} in {index}")
    # Embedded in a single-quoted JS string: JSON here has no quotes or
    # backslashes other than the double quotes around keys and hex values.
    index.write_text(html.replace(PLACEHOLDER, json.dumps(manifest, separators=(",", ":"))))


if __name__ == "__main__":
    main()
