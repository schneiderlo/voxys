#!/usr/bin/env python3
"""Record authored WASM source/build inputs, including untracked new code.

Preloaded asset content is checked separately by check_package.py. Vendored
dependency trees and SDK caches are not included in this authored-source hash.
"""

import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
paths = set()
for name in ("src", "shaders", "web", "cmake"):
    directory = ROOT / name
    if directory.is_dir():
        paths.update(path for path in directory.rglob("*") if path.is_file())
paths.update(ROOT.glob("*.cfg"))
for name in (
    "AGENTS.md", "README.md", "BUILD", "MODULE.bazel", "WORKSPACE", "Makefile",
    "CMakeLists.txt", ".bazelrc", ".bazelversion", "shell.nix", "flake.nix",
    "flake.lock", "scripts/fetch_deps.sh", "scripts/check_wasm_lego_assets.py",
    "tools/BUILD", "tools/serve_wasm.sh",
):
    path = ROOT / name
    if path.is_file():
        paths.add(path)
entries = []
for path in sorted(paths):
    data = path.read_bytes()
    entries.append({"path": path.relative_to(ROOT).as_posix(), "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()})
canonical = json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()
result = {
    "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    "scope": "Authored src/shaders/web/cmake trees, root configs and listed build metadata; excludes data/vendor/SDK caches",
    "files": len(entries),
    "content_fingerprint_sha256": hashlib.sha256(canonical).hexdigest(),
    "entries": entries,
}
Path(sys.argv[1]).write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({key: value for key, value in result.items() if key != "entries"}, indent=2))
