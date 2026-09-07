# BOOT-02 — build declarations and browser packaging

Date: 2026-09-07. Executor: `render_architecture`. Base source revision:
`7563f61fd536df7de5d209ff35d3cd2099ebcbb6`; working branch:
`codex/salvage-implementation`. Other agents own the shared CMake/source fixes.

Status: fresh Bazel WASM build and actual HTTP/package checks pass. Fresh CMake
WASM build/package checks also pass in the follow-up below. These reports do not
close BOOT-02 or claim browser GPU startup, frame rate, or memory performance.

**Follow-up:** `cmake-wasm-fresh.md` now records a fresh CMake WASM rebuild and
repeated package/terrain checks after the latest application and DATA-01 source
changes. The earlier CMake package-only evidence below is retained as history.

## Changes

- Root `BUILD` declares `lego_world.cfg` as native runtime data and as a WASM
  preload, linker input, and runtime input.
- `Makefile` copies the complete `web/` tree, including playground controls,
  frame-budget support, and the island-study module. It also stages raw WGSL
  needed by the standalone shader-parity page.
- `make package-wasm` stages existing outputs without rebuilding. Set
  `WASM_PACKAGE_DIR` to keep a dedicated serving directory. Packaging fails
  if any of the JS/WASM/data outputs are missing or empty.
- Make uses a bounded `JOBS=4` default and builds the actual WASM target. It no
  longer deletes the existing output triplet before attempting a rebuild;
  CMake's declared link inputs determine when it must regenerate the package.
- Bazel Emscripten is aligned with the Pages/CMake SDK pin at **6.0.1**.
  This version is present in the [Bazel Central Registry](https://registry.bazel.build/modules/emsdk/6.0.1).
  The [published module definition](https://raw.githubusercontent.com/bazelbuild/bazel-central-registry/main/modules/emsdk/6.0.1/MODULE.bazel)
  requires `rules_cc=0.2.16` and `platforms=1.0.0`; the direct pins now reflect
  those resolved versions. Its rules_cc compatibility extension is exposed to
  the project's remaining WORKSPACE dependencies.
- `tools/BUILD` and `tools/serve_wasm.sh` now declare/stage raw shader text for
  the parity page. This fixes an observed HTTP 404 from the real Bazel server.

## Fresh verification of the existing CMake package

The input triplet at `build-lego-wasm/bin/voxy_wasm.*` was built earlier on
2026-09-07 at 12:48. The following commands freshly staged and checked it;
they are **not evidence of a fresh CMake compilation**:

```bash
make package-wasm WASM_BUILD_DIR=build-lego-wasm WASM_PACKAGE_DIR=/tmp/voxys-boot02-cmake-web
python3 docs/validation/salvage/BOOT-02/check_package.py /tmp/voxys-boot02-cmake-web
python3 docs/validation/salvage/BOOT-02/test_package.py /tmp/voxys-boot02-cmake-web
```

Result: 15 local page/module references, 27 served parity shaders, and all
57 preload entries passed. The preload data contains 57,673,305 bytes.
Every preload byte matches the current corresponding source file. All four
route configs (`voxy.cfg`, `ridgebreak.cfg`, `lego_shore.cfg`,
`lego_world.cfg`) and their nonempty terrain asset references are present.

The checker compiles the actual WASM module with Node and follows each public
JavaScript binding to a real function export. It verified:

| Public API | Actual minified WASM export |
|---|---|
| `voxy_lego_action` | `db` |
| `voxy_get_lego_hud_json` | `eb` |
| `voxy_get_heap_used_bytes` | `fb` |

These source functions already use `EMSCRIPTEN_KEEPALIVE`. Their omission
from the explicit `EXPORTED_FUNCTIONS` string is not a missing-export bug.
No redundant export-list change was made.

Six negative controls passed on temporary copies of the real package. They
reject a missing playground script, missing imported island module, stale
served shader, omitted route config, corrupted config bytes, and a JS control
bound to a nonexistent WASM function. The original package is never modified.
The check reports filesystem package integrity; it does not run application
initialization or GPU work.

Saved output: `make-package.log`, `cmake-package-check.txt` and
`cmake-package-negative-tests.txt` beside this report. The final stage also
includes the new `web/salvage_data.mjs` added concurrently by the DATA-01 worker.

## Bazel build attempts

Commands use the normal-host Nix shell so the existing Bazel/SDK caches and
declared toolchain dependencies are available. Every build uses four jobs.

```bash
nix-shell --run 'bazel build --config=wasm --jobs=4 //:voxy_wasm'
```

1. Changing only the SDK pin first exposed a missing
   `@@cc_compatibility_proxy//:symbols.bzl` repository in the hybrid
   WORKSPACE/Bzlmod mapping. Updated the two required direct dependency pins
   and exposed the rules_cc compatibility extension. No validation was disabled.
2. Analysis then passed and actual compilation stopped at
   `src/app/application.cpp:35`: `terrain/lego_layout_cache.hpp` was not declared
   in `src/terrain/BUILD`. Root added the existing header to that target.
   The failure is retained in `packaging-bazel-missing-header.log`.
3. Resumed compilation, also building the actual local-server launcher:

```bash
nix-shell --run 'bazel build --config=wasm --jobs=4 //:voxy_wasm //tools:serve_wasm'
```

The retry **passed**, taking 83.875 seconds. The subsequent rebuild after adding
the server's shader runfiles also passed (2.420 seconds, cached compilation).
Build output is retained in `packaging-bazel-build.log` and `packaging-bazel-build-final.log`.
The emitted JS is 161,260 bytes, WASM 4,169,979 bytes, and preload data
57,673,305 bytes. Exact SHA-256 hashes and modification times are recorded in
`packaging-bazel-artifacts.json`. These artifacts precede DATA-01's later source
library/test registration; they are the BOOT-02 build, not that feature's gate.

The actual Bazel compiler reports **6.0.1-git**, commit
`25e4e8d6550d392ba9e0c2936bce7cf41ee47cc0`; output is saved in
`packaging-emscripten-version.txt`. Its executable expects Bazel's environment and
`emscripten_toolchain/default_config`: a bare invocation first failed with
`LLVM_ROOT not set`, while the probe with that configuration passed. The
successful builds use those settings through the registered toolchain.

## Server checks

```bash
python3 tools/test_serve_wasm.py
```

All four existing HTTP/telemetry server tests passed using normal-host local
ports. Python 3.14 emitted an existing ResourceWarning while cleaning up the
intentional HTTP-400 negative test; the tests passed.

The generated runfiles mapping explicitly contains `,voxys,_main`. Thus the
server's apparent-name lookup is supported; no speculative namespace rewrite
was made.

Launched the built launcher directly to exercise its actual Bazel runfiles:

```bash
VOXY_HOST=127.0.0.1 VOXY_PORT=18342 bazel-bin/tools/serve_wasm
```

Before the shader staging fix, real HTTP requests returned 200 for `index.html`,
`lego_patch_model.mjs`, and `frame_budget.js`, but **404** for
`shaders/terrain_raycast.wgsl`. Results are saved in `packaging-bazel-http-before.json`.
After the fix/rebuild, the launcher staged its package at
`/tmp/tmp.2DSGtRcYhf` and the following checks passed:

```bash
python3 docs/validation/salvage/BOOT-02/check_package.py /tmp/tmp.2DSGtRcYhf --url http://127.0.0.1:18342
python3 docs/validation/salvage/BOOT-02/test_package.py /tmp/tmp.2DSGtRcYhf
```

All **46 HTTP-delivered files** matched their staged SHA-256 bytes, including
all browser files, 27 parity shaders and the artifact triplet. WASM was served
with `application/wasm`. All four route configs, 57 source-matching preloads,
and six negative controls passed. The server was stopped after verification.
The temporary package path is disposable; rerun the launcher and use its printed
directory when reproducing these checks.

In this fresh Bazel build, the actual WASM export names are unminified:
`voxy_lego_action`, `voxy_get_lego_hud_json`, and `voxy_get_heap_used_bytes`.
The checker validated each public JS binding against an actual function in the
compiled module. It also parsed the generated JS with Node. Saved results:
`packaging-bazel-check.txt` and `packaging-bazel-negative-tests.txt`.

## Remaining evidence

- Merge `cmake-wasm-fresh.md` and the CMake worker's native incremental-staging
  evidence with the main task's acceptance review before closing BOOT-02.
- The main task owns selected native Bazel tests after its DATA-01 registration;
  those also exercise the upgraded rules_cc version on the native path.
- Native/browser startup and visible performance measurements remain separate
  acceptance work; a package pass cannot establish them.
