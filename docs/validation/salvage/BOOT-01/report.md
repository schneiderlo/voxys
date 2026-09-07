# BOOT-01 — checkout and source baseline

Date: 2026-09-07. Investigator: `lego_gameplay`.

**Status: complete; independently reviewed by root on 2026-09-07.**

Root reproduced the checkout/status and instruction reads, browser config mapping, Bazel/CMake declarations, cached toolchain paths and hook configuration. The confirmed packaging/toolchain differences are assigned to BOOT-02. Review accepts this as a source baseline only; builds and visible measurements remain separate unchecked tasks.

This report records the starting checkout and source-defined launch/build paths. It does not certify a build, a running scene, hardware performance, or a completed expedition feature. BOOT-02 owns build/test reproduction; BOOT-03 owns fresh visible measurements.

## 1. Checkout identity and ownership

Repository: `/home/modkin/workspace/schneiderlo/voxys`.

Source HEAD at the start of this investigation:

```text
7563f61fd536df7de5d209ff35d3cd2099ebcbb6
2026-09-07
Point CI compiler caches at the actual configured directory
```

The source HEAD exactly matches the baseline named in `GAME_IMPLEMENTATION_TODO.md`. Both `git diff --name-status` and `git diff --cached --name-status` were empty. No tracked source change was present.

Initial branch: `main`, displayed as `main...origin/main`. This only describes local Git tracking state; no fetch was performed to establish remote freshness.

Initial untracked files, recorded before this report was created:

```text
GAME_IMPLEMENTATION_TODO.md
docs/brick-salvage-blueprint.md
docs/concepts/build-explore-salvage-prompt.md
docs/concepts/build-explore-salvage.png
```

These are user-visible planning artifacts from the preceding work. Preserve them. They are not disposable build output.

The root agent subsequently created `codex/salvage-implementation` from the same HEAD. A later read confirmed that branch and unchanged tracked/index diffs. The root also updated the active plan and execution ledger. Those changes are outside this agent's file ownership.

Owned output for this task: `docs/validation/salvage/BOOT-01/report.md` only. This agent did not alter implementation, shared plans, build declarations, hooks, or Git history.

### Planning-artifact fingerprint

These SHA-256 values identify the files when sampled during this investigation. The plan is live and root-owned; its fingerprint is a snapshot, not a requirement that later agents leave it unchanged.

| File | Bytes | SHA-256 |
|---|---:|---|
| `AGENTS.md` | 432 | `fe03d4cd0a7c246077216d18d94ceb1980bbad9182a3394204dd89f5ad16f619` |
| `README.md` | 9,704 | `3fb83b228a73a94aa403b97b72e6f918745834d9e546090a46504b7bd40b3ec7` |
| `GAME_IMPLEMENTATION_TODO.md` | 103,844 | `06eabf01bfc847ba9ab7814f858041c520c8d9b190db520a8b4ca0b92553a13e` |
| `docs/brick-salvage-blueprint.md` | 62,953 | `1de6e25e404ff0c9450e54759a6a136448b3ea995073f511ccf597bd1e057e55` |
| `docs/concepts/build-explore-salvage-prompt.md` | 2,043 | `ad5284bc2ea7b4a4d7a9af50c56db2aa788aee5c77a3c48c8debc61ca00d4824` |
| `docs/concepts/build-explore-salvage.png` | 2,509,082 | `822606efb892d871706b97db08e4c7a94485f8318eaaafa943b4d257eb5d7e9a` |

## 2. Instructions read and scope

Read in full during this task:

- Root `AGENTS.md`.
- Root `README.md`.
- All of `GAME_IMPLEMENTATION_TODO.md`, including contracts, every task/gate, verification routing, decision ledger, and execution ledger.

The tracked instruction inventory contains root `AGENTS.md` only. A hidden-file search outside vendored/generated directories found no additional instruction file. Explicit checks found no `AGENTS.md` at `/`, `/home`, `/home/modkin`, `/home/modkin/workspace`, or `/home/modkin/workspace/schneiderlo`.

The relevant repository instructions require careful CPU/GPU engineering and short, readable communication for a highly dyslexic owner. The active implementation plan requires claimed task ownership, preservation of unrelated work, evidence before checking off tasks, both build systems for shared code, and a commit after each verified G00–G14 gate. The root owns the shared plan and gate commits.

No filesystem skill was needed for this source-inventory task. The plan's Blender and image-generation workflow is for later asset work; BOOT-01 did not invoke it.

## 3. Modes and launch routes traced in source

These are source-defined paths, **not freshly executed launches**.

| Experience | Browser selection | Native selection | Source-defined behavior |
|---|---|---|---|
| LEGO World | `/` or `?experience=lego-world` | `--config lego_world.cfg` | Original 8192² heightfield with stepped/studded geometry and grouped materials; WebGPU physics; CPU fallback disabled |
| LEGO Shore | `?experience=lego` | `--config lego_shore.cfg` | 256² crop in `data/lego_shore.ldh`; same surface model; WebGPU physics; CPU fallback disabled |
| Smooth terrain | `?experience=terrain` | No config argument, or `--config voxy.cfg` | Generated 8192² heightfield plus 2048 albedo; WebGPU default with allowed fallback |
| RIDGEBREAK | `?experience=ridgebreak` | `--config ridgebreak.cfg` | Deterministic mixed-biome terrain selected by an empty heightmap path; motocross path |
| Build & break | P / build controls inside a dedicated LEGO scene | P inside a dedicated LEGO scene | Separate bounded playground; 48 bricks, 8 reusable balls, tower/target challenge |
| Standalone LEGO patch | `lego_patch.html` | None | Separate small browser study; it is not the engine expedition game |
| WRECKWATER graphical client | No corresponding main-page URL bootstrap | Native `--wreckwater-*` bootstrap fields | Native client path is wired; all server/port/peer/key/session/match/world/epoch fields are required |
| New salvage expedition | **Not implemented** | **Not implemented** | `salvage.cfg`, `src/game/expedition/`, and `src/game/construction/` were absent |

Route evidence:

- `web/index.html:229–232` chooses `lego-world` when `experience` is absent.
- `web/index.html:998–1001` maps `ridgebreak`, `lego-world`, and `lego` to config arguments. Other strings, including the not-yet-implemented `salvage`, currently select no config override.
- `src/core/config.hpp:224` defaults `configPath` to `voxy.cfg`.
- `src/core/config.cpp:587–603` falls back to default settings when a nondefault config file is missing. A nonexistent `salvage.cfg` is therefore not a usable mode-selection test.
- `src/engine/platform/native/entry.cpp:39–43` and `src/engine/platform/wasm/entry.cpp:1013–1015` derive LEGO/motocross flags from the configured window title.
- `src/app/application.cpp:4928–4999` owns playground actions/HUD. `src/game/lego_playground.hpp:14–25` defines its limits/state.
- `src/engine/platform/native/entry.cpp:23–35,130–150` validates and supplies the WRECKWATER bootstrap. `src/core/config.cpp:567–579` documents the required flags. No credential values were read or copied into this report.
- `src/app/application.cpp:683–691` explicitly rejects that native TCP bootstrap under `VOXY_WASM`.

The `scripts/run_wreckwater_2v2.sh` launcher is a native server/probe process proof. It is not the graphical game route and must not be counted as a completed human-played expedition.

## 4. Build inventory

### Repository declarations

| Area | Current declaration |
|---|---|
| Bazel | Root `BUILD`, `WORKSPACE`, `MODULE.bazel`, `MODULE.bazel.lock`, `.bazelrc`; package files are named `BUILD`, not `BUILD.bazel` |
| Bazel version | `.bazelversion`: `8.5.0` |
| Bazel language/options | C++20; workspace support enabled; GLM zero-to-one depth and `GLM_FORCE_LEFT_HANDED`; sanitizer profiles in `.bazelrc` |
| Bazel WASM toolchain | `MODULE.bazel:7` declares `emsdk` module `4.0.17`; `.bazelrc` selects `@emsdk//:platform_wasm` |
| Native targets | `//:voxy_native`, `//:wreckwater_server`, `//:ridgebreak_server`, `//:wreckwater_client_probe` |
| WASM targets | `//:voxy_wasm_cc` and wrapper `//:voxy_wasm`; fixed 512 MiB memory and 1 MiB stack declared in root `BUILD` |
| CMake | `CMakeLists.txt`: minimum 3.20, C++20, native/tools/tests ON by default; WASM/benchmarks OFF; Emscripten selects the WASM branch |
| Make wrapper | `Makefile`; native build directory defaults to `build`, WASM to `build-wasm`; requires an explicit/available EMSDK for `make wasm` |
| Nix | `shell.nix` supplies compiler/build tools and native graphics libraries; `flake.nix` exposes an `x86_64-linux` shell from `nixos-26.05` |
| Flake lock | `flake.lock` pins nixpkgs revision `fd1462031fdee08f65fd0b4c6b64e22239a77870` |
| Pages | `.github/workflows/pages.yml:20–22`: Emscripten `6.0.1`; CMake build, LEGO shader/asset checks, integrated browser startup, then Pages deployment |
| Dependency setup | `scripts/fetch_deps.sh`: GLM 0.9.9.8, GLFW 3.4, zstd 1.5.5, wgpu-native 22.1.0.5, Jolt 5.5.0, standalone emdawnwebgpu and pinned Box3D |

`shell.nix` uses the current `<nixpkgs>` input when invoked directly; the flake supplies its locked nixpkgs input. Do not describe these as automatically identical environments without recording the actual versions. `nix-shell` avoids copying this asset-heavy checkout to the Nix store.

Shared-code changes must update both build systems. The authority content allowlist/generated identity in root `BUILD` and `CMakeLists.txt` is an additional contract: source changes in that closure must not leave stale content identity.

### Current shell and existing local artifacts

A read-only environment inventory at **2026-09-07 21:21:12 UTC** reported Linux `7.0.0-31-generic`, `x86_64`.

| Executable | Current PATH result before entering Nix |
|---|---|
| `bash`, `git`, `python3` | `/usr/bin/bash`, `/usr/bin/git`, `/usr/bin/python3` |
| `nix-shell`, `nix` | `/nix/var/nix/profiles/default/bin/…` |
| `bazel` | `/home/modkin/.local/bin/bazel` |
| `node` | `/home/modkin/.nix-profile/bin/node` |
| `uv` | `/home/modkin/.local/bin/uv` |
| `cargo`, `rustc` | `/home/modkin/.cargo/bin/…` |
| `blender` | `/snap/bin/blender`; not executed for BOOT-01 |
| `cmake`, `ninja`, `gcc`, `g++`, `clang`, `clang++`, `emcc`, `emcmake`, `vulkaninfo`, `bazelisk` | Not on that shell's PATH |

The missing compiler/build commands are a reason to enter the documented toolchain, not proof that they are unavailable on the host. BOOT-02 must establish an executable environment and actual versions.

Pre-existing CMake caches were found in `build-lego-native`, `build-lego-wasm`, and `build-wasm`. Preserve these directories unless their owner agrees to replacement. Use a new task-specific build directory for reproduction.

- `build-lego-native/CMakeCache.txt` records Release, native/tests/tools ON, WASM OFF, Unix Makefiles, and Nix GCC 15.2.0 wrapper paths.
- `build-lego-wasm/CMakeCache.txt` records Release and `/tmp/voxys-emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`.
- `build-wasm/CMakeCache.txt` records Release and `/home/modkin/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`.
- Both corresponding `emscripten-version.txt` files contained `"6.0.1"`. This is a file read, not a compiler invocation.
- Cached Boolean values are configuration records, not proof of the final effective Emscripten branches or artifact freshness. No existing binary was assumed to match HEAD.

Representative dependency/assets were present: native WebGPU shared library, GLM headers, Jolt headers, Box3D directory, full terrain LDH (31,803,420 bytes), and shore LDH (25,054 bytes). File existence/size does not certify checksums, complete vendor trees, or usable binaries.

### Hook state

Initially `git config --get core.hooksPath` had no value and `.git/hooks/pre-commit` was absent. That query returned exit code 1 because the setting was unset, not because source inspection failed.

The tracked `.githooks/pre-commit` runs:

```bash
bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test
```

During this investigation the root ran `scripts/setup-hooks.sh`. A subsequent read confirmed `file:.git/config` now sets `core.hooksPath` to `.githooks`. This agent did not perform that setup. Hook installation is not evidence that its test command passes.

## 5. Source differences and known limits

### Confirmed documentation/source drift

1. **Browser default:** README's final RIDGEBREAK paragraph says `voxy.cfg` remains the default. That remains true of native config loading, but the browser now defaults to `lego_world.cfg`. Use the route table above.
2. **Main-page controls:** README's physics-sandbox wheel selection and 128-object right-click batch describe the broader sandbox. Dedicated LEGO scenes disable wheel shape selection, cap normal scene bodies, and limit batches to eight; Build & break has separate controls. See `src/app/application.cpp:5014–5015,5086,5169` and `docs/lego-shore.md`.
3. **Graphical WRECKWATER wiring:** `docs/wreckwater_graphical_client_loop.md:3` says the loop is not yet wired into `Application`. The native entry point and `Application::initWreckwaterClient()` now wire it. This does not establish a finished match or a WASM transport.
4. **Build filenames:** the actual root/package declarations are `BUILD`, `tests/BUILD`, and `tools/BUILD`. The active TODO source map was corrected by root during planning; do not recreate nonexistent `BUILD.bazel` files from older references.

### Confirmed declaration differences requiring BOOT-02 attention

1. **LEGO World packaging differs:** root `BUILD` omits `lego_world.cfg` from native data and the WASM preload/additional-input/data lists. `CMakeLists.txt:470` includes it; Pages explicitly checks it. Source routing requests that file for the default browser scene. This is a confirmed declaration mismatch; its emitted-artifact/runtime effect still needs BOOT-02 verification.
2. **WASM versions differ:** Bazel's `emsdk` module declaration is 4.0.17 while Pages/local Emscripten version files use 6.0.1. Establish and record the intended parity policy instead of assuming one tested toolchain covers both.
3. **Make web copy list differs:** `Makefile` copies a fixed subset of web files, omitting the current LEGO playground JS/CSS. Pages copies `web/*`. A `make serve-wasm` artifact needs an actual asset-completeness check before calling it equivalent to Pages.
4. **Coordinate convention needs explicit conversion:** existing global build definitions include `GLM_FORCE_LEFT_HANDED`. The new plan defines an authored right-handed asset contract. DATA-01 must trace explicit camera/math functions and validate conversions; neither deleting the macro nor relabeling existing coordinates is justified by this inventory.

### Investigated suspicion that is not a confirmed defect

Both explicit WASM export lists omit `_voxy_lego_action` and `_voxy_get_lego_hud_json`, but `src/engine/platform/wasm/entry.cpp:1372–1376` marks them `EMSCRIPTEN_KEEPALIVE`. List omission alone therefore does **not** prove missing exports. Verify the actual emitted module during BOOT-02; do not report a failed runtime test that was never run.

### Untested and unavailable evidence

- No compiler, build, unit test, server, browser, game executable, Blender process, network service, or performance workload was run by BOOT-01.
- No remote revision freshness, dependency download integrity, adapter usability, Windows build, or fresh native/WASM route success is certified here.
- Existing benchmark captures remain historical evidence, not results of this task.
- The root reported that a restricted Nix attempt could not access the daemon socket and that a normal-host toolchain retry was underway. This is a root-reported setup limitation; BOOT-02 owns the final result.
- One exploratory `rg` command used nonexistent `src/config*`; the source was subsequently located and read at `src/core/config.*`. No conclusion depends on that failed path lookup.

## 6. Reproduce the inventory without changing the checkout

Start from the repository root. Re-read instructions and the active plan; do not assume a later HEAD or dirty-file list must still equal this snapshot.

```bash
pwd
git rev-parse HEAD
git status --short --branch --untracked-files=all
git diff --name-status
git diff --cached --name-status
git ls-files '*AGENTS.md'
git config --show-origin --get core.hooksPath
```

If the last command returns 1, the setting is unset. Do not change it merely to make an inventory command succeed.

Read `AGENTS.md`, `README.md`, and all of `GAME_IMPLEMENTATION_TODO.md`. Check ancestor instructions and any nested instructions relevant to the files a later task will edit.

```bash
rg -n "experience.*lego-world|lego_world.cfg|lego_shore.cfg|ridgebreak.cfg" web/index.html
rg -n "configPath =|--config" src/core/config.hpp src/core/config.cpp
rg -n "initWreckwaterClient|native TCP bootstrap" src/app/application.cpp
rg -n "lego_world.cfg|preload-file|EXPORTED_FUNCTIONS" BUILD CMakeLists.txt
rg -n "voxy_lego_action|voxy_get_lego_hud_json" src/engine/platform/wasm/entry.cpp web/lego_playground.js
cat .bazelversion MODULE.bazel
rg -n "EMSCRIPTEN_VERSION|emcmake|check_wasm_lego_assets|smoke_integrated" .github/workflows/pages.yml
```

An independent reviewer can reproduce all source conclusions above without downloading dependencies or running a GPU workload.

## 7. BOOT-02 handoff: commands to validate, not commands executed here

Use the repository toolchain before building. Do not overwrite the pre-existing CMake output directories. Dependency fetching may need network access and can modify vendor directories; inspect their status and use the repository script only when needed.

```bash
nix-shell
bazel build -c opt //:voxy_native //tools:gltf_vmesh_tool
bazel test //tests:config //tests:lego_surface //tests:lego_playground //tests:gltf_vmesh_tool_test
```

Those focused test targets exist in `tests/BUILD`: the generic test list generates `config`, `lego_surface`, and `lego_playground`; `gltf_vmesh_tool_test` has an explicit declaration. They are not a substitute for the hook's full suite.

```bash
cmake -S . -B build-salvage-native -DVOXY_BUILD_NATIVE=ON -DVOXY_BUILD_TESTS=ON -DVOXY_BUILD_TOOLS=ON
cmake --build build-salvage-native -j 4
ctest --test-dir build-salvage-native --output-on-failure
```

For WASM, first resolve the toolchain-version and asset-packaging differences above. The plan lists Bazel `//:voxy_wasm`; Pages provides the source-defined CMake/Emscripten path. Record the chosen compiler and inspect the emitted files, then validate default/explicit routes with the real page.

Do not invoke `make clean-*`, reset the working tree, delete user artifacts, reuse private credentials in logs, or treat an old build directory as fresh evidence. BOOT-01 requires independent source review before its checkbox is marked; G00 additionally requires BOOT-02 through BOOT-05.
