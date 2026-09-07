# BOOT-02 — fresh CMake WASM build

Date: 2026-09-07. Executor: `render_architecture`.

**Passed:** fresh CMake WASM compilation/link, package checks, six negative
controls, and the real engine's packaged-terrain loader. This supersedes the
earlier CMake package-only evidence in `packaging-build.md`. No browser or GPU
was launched for this check, and no shared source was changed by this worker.

## Build and source identity

Reused `build-lego-wasm` after verifying its Emscripten toolchain path. Explicitly
reconfigured Release/WASM settings with four build workers. The build log
confirms fresh compilation of both `src/app/application.cpp` and
`src/game/construction/construction_types.cpp`, followed by library and final
WASM linking. Thus this result includes the application's single-throw failure
handling and DATA-01's construction-type registration.

| Tool | Observed version |
|---|---|
| CMake | 4.1.2 |
| Emscripten | 6.0.1, commit `25e4e8d6550d392ba9e0c2936bce7cf41ee47cc0` |
| Package-check Node | 22.23.1 |
| Package-check Python | 3.14.4 |

Base Git commit: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6` with the current
uncommitted work. A content manifest covers **320 authored files**, including
untracked construction code, source/build declarations, shaders, browser files,
and configs. Its before/after manifests are identical:

```text
SHA-256: 4b1659945758ecb82edd3811a1819267bc41e8c4d6a048367ca9ee3776a26ad3
```

This fingerprint excludes vendor/SDK caches and terrain/material data; all
preloaded data bytes are separately compared with source by the package gate.
Full source-file hashes are retained in `cmake-wasm-inputs-before.json` and
`cmake-wasm-inputs-after.json`.

Commands executed from the repository root:

```bash
python3 docs/validation/salvage/BOOT-02/fingerprint_wasm_inputs.py docs/validation/salvage/BOOT-02/cmake-wasm-inputs-before.json
nix-shell --run 'source /tmp/voxys-emsdk/emsdk_env.sh && emcc --version && cmake --version && emcmake cmake -S . -B build-lego-wasm -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_NATIVE=OFF -DVOXY_BUILD_WASM=ON -DVOXY_BUILD_TOOLS=OFF -DVOXY_BUILD_TESTS=OFF && cmake --build build-lego-wasm --target voxy_wasm --parallel 4'
python3 docs/validation/salvage/BOOT-02/fingerprint_wasm_inputs.py docs/validation/salvage/BOOT-02/cmake-wasm-inputs-after.json
cmp docs/validation/salvage/BOOT-02/cmake-wasm-inputs-before.json docs/validation/salvage/BOOT-02/cmake-wasm-inputs-after.json
```

Normal-host Nix permissions were used for the declared toolchain and existing
SDK caches. Build exit: **0**. Log: `cmake-wasm-build.log`.
The existing deleted-comparison warning in `physics_types.hpp` and signedness
warning in `lego_layout_cache.hpp` remain; no suppression was added.

## Fresh package verification

```bash
make package-wasm WASM_BUILD_DIR=build-lego-wasm WASM_PACKAGE_DIR=/tmp/voxys-boot02-cmake-fresh-web
python3 docs/validation/salvage/BOOT-02/check_package.py /tmp/voxys-boot02-cmake-fresh-web
python3 docs/validation/salvage/BOOT-02/test_package.py /tmp/voxys-boot02-cmake-fresh-web
nix-shell --run 'source /tmp/voxys-emsdk/emsdk_env.sh && python3 scripts/check_wasm_lego_assets.py build-lego-wasm'
```

Results:

- All 57 preloads, totaling **57,673,305 bytes**, match their source files.
- All four route configs and their nonempty terrain references are present.
- All 15 local page/module references and 27 staged parity shaders pass.
  The complete staged web tree includes `salvage_data.mjs`.
- Node parses the generated JS and compiles the actual WASM module. Public
  `voxy_lego_action`, `voxy_get_lego_hud_json`, and `voxy_get_heap_used_bytes`
  bindings resolve to real function exports `db`, `eb`, and `fb`.
- All six negative controls pass on temporary package copies.
- The actual engine decoder loads the shipped 256² shore and 8192² landscape.
  Every one of the shore's 65,536 samples matches its source crop.

Saved outputs: `cmake-wasm-fresh-stage.log`, `cmake-wasm-fresh-package.txt`,
`cmake-wasm-fresh-negative-tests.txt`, and `cmake-wasm-terrain-check.log`.
These CMake checks inspect the staged filesystem; actual HTTP delivery was
separately established for the Bazel server in `packaging-build.md`.

## Artifacts

Final outputs were written at approximately **21:41 UTC** on 2026-09-07.

| Artifact | Bytes |
|---|---:|
| `build-lego-wasm/bin/voxy_wasm.js` | 65,759 |
| `build-lego-wasm/bin/voxy_wasm.wasm` | 4,192,484 |
| `build-lego-wasm/bin/voxy_wasm.data` | 57,673,305 |
| `build-lego-wasm/lib/libvoxy_core.a` | 7,989,318 |

Exact SHA-256 values, timestamps, tool versions and the source fingerprint are
stored in `cmake-wasm-fresh-artifacts.json`. The independently computed hashes
are also retained in `cmake-wasm-fresh-sha256.txt`:

```bash
sha256sum build-lego-wasm/bin/voxy_wasm.js build-lego-wasm/bin/voxy_wasm.wasm build-lego-wasm/bin/voxy_wasm.data build-lego-wasm/lib/libvoxy_core.a
```

The disposable package ready for the next browser startup check is
`/tmp/voxys-boot02-cmake-fresh-web`. No process is using it. Browser presentation,
GPU validation, visible timing, and live memory measurements remain separate
checks. This report makes no full-test-suite or gameplay-completion claim.
