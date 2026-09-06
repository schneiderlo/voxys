#!/usr/bin/env python3
"""Load the actual WASM preload terrain with the built engine's WASM objects.

Requires the completed WASM build, em++ and Node. No graphics adapter or Python
packages are needed. This gate runs before the Pages artifact is published.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
ASSETS = (
    "/data/lego_shore.ldh",
    "/data/generated/td_seed_1234_8192.ldh",
)
# Emscripten emits quoted keys before Closure and unquoted keys afterwards.
ENTRY = re.compile(
    r'''["']?filename["']?\s*:\s*["']([^"']+)["']\s*,\s*'''
    r'''["']?start["']?\s*:\s*(\d+)\s*,\s*'''
    r'''["']?end["']?\s*:\s*(\d+)'''
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, nargs="?", default=ROOT / "build-wasm")
    parser.add_argument("--emxx", default="em++")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    build = args.build.resolve()
    manifest = (build / "bin/voxy_wasm.js").read_text()
    package = (build / "bin/voxy_wasm.data").read_bytes()
    entries = ENTRY.findall(manifest)

    with tempfile.TemporaryDirectory(prefix="check-lego-assets-", dir=build) as tmp:
        directory = Path(tmp)
        extracted = []
        for name in ASSETS:
            matches = [(int(start), int(end)) for path, start, end in entries if path == name]
            if len(matches) != 1:
                raise RuntimeError(f"Expected one preload entry for {name}; got {len(matches)}")
            start, end = matches[0]
            if not 0 <= start < end <= len(package):
                raise RuntimeError(f"Preload range out of bounds for {name}: {start}:{end}")
            data = package[start:end]
            if data != (ROOT / name.lstrip("/")).read_bytes():
                raise RuntimeError(f"Packaged terrain differs from the checked-in asset: {name}")
            path = directory / Path(name).name
            path.write_bytes(data)
            extracted.append(str(path))

        # Link the same compression/logging objects and Zstd used by voxy_wasm,
        # rather than implementing a second decoder or checking only filenames.
        checker = directory / "check_lego_assets.js"
        subprocess.run([
            args.emxx, "-std=c++20", "-O3", f"-I{ROOT / 'src'}",
            str(ROOT / "tests/check_lego_assets.cpp"),
            str(build / "lib/libvoxy_core.a"), str(build / "lib/libzstd.a"),
            "-sENVIRONMENT=node", "-sNODERAWFS=1", "-sEXIT_RUNTIME=1",
            "-sINITIAL_MEMORY=512MB", "-sALLOW_MEMORY_GROWTH=0", "-sSTACK_SIZE=1MB",
            "-o", str(checker),
        ], check=True, cwd=ROOT)
        subprocess.run([args.node, str(checker), *extracted], check=True, cwd=ROOT)
    print("PASS: shipped WASM terrain package loads with the built engine", flush=True)


if __name__ == "__main__":
    main()
