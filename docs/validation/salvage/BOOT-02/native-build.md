# BOOT-02 — native toolchain and build evidence

Date: 2026-09-07. Executor: root. Source base: `7563f61fd536df7de5d209ff35d3cd2099ebcbb6`; branch `codex/salvage-implementation`.

Status: native build and focused tests passed; web and packaging integration still in progress. BOOT-02 is not checked off by this report.

## Environment established

The restricted `nix-shell` invocation failed because the Nix daemon socket was inaccessible. The normal-host invocation succeeded and downloaded the declared development environment through Nix. No repository vendor refresh was needed: required dependency trees were already present.

Actual normal-host tool versions:

| Tool | Observed version |
|---|---|
| Bazel | 8.5.0, through Bazelisk 1.28.1 |
| CMake | 4.1.2 |
| GCC | 15.2.0 |
| Python in Nix | 3.13.13 |
| Node | 22.23.1 |
| Native GPU | AMD Radeon 890M Graphics, RADV STRIX1 |
| Mesa/Vulkan device | Mesa 26.1.2; Vulkan 1.4.348 |

`bazel --version` is rejected by the Nix wrapper's startup-option path; `bazel version` succeeds. The initial multi-command environment probe finished with the compiler's status, so it must not be described as a wholly successful Bazel probe. A second probe explicitly used `bazel version`.

The Vulkan inventory found both hardware Radeon and CPU llvmpipe adapters. Hardware enumeration is now confirmed with normal host permissions; it is not proof of a passing game frame. The loader reported unrelated driver enumeration warnings while the Radeon device was listed successfully.

## Commands executed

From the repository root, using supported normal-host permission for Nix and GPU access:

```bash
nix-shell --run 'cmake -S . -B build-salvage-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_NATIVE=ON -DVOXY_BUILD_TESTS=ON -DVOXY_BUILD_TOOLS=ON && cmake --build build-salvage-native --target voxy_native voxy_tests gltf_vmesh_tool --parallel 4'
```

The new task-specific directory leaves the older build caches intact. The first attempt stopped at generated authority-content validation:

```text
authority content allowlist must be sorted by canonical path
```

`wreckwater_authority_content.allowlist` had `src/terrain/lego_surface.hpp` between two `src/physics/…` entries. Moved that one line into the terrain section; no input was added or removed, and no validation was bypassed.

```bash
python3 tools/test_generate_wreckwater_build_content.py
```

Result: 8 tests passed. The existing tests include rejection of unsorted/aliased/wrong-kind input.

```bash
nix-shell --run 'cmake --build build-salvage-native --target voxy_native voxy_tests gltf_vmesh_tool --parallel 4'
```

Retry passed. Third-party and pre-existing compiler warnings are retained in its log; no warning-suppression change was made to pass the build.

Then executed:

```bash
nix-shell --run 'cmake --build build-salvage-native --target gltf_vmesh_tool_tests --parallel 4 && build-salvage-native/bin/voxy_tests --gtest_filter=ConfigDefaultsTest.*:ConfigFileTest.*:ConfigUtilsTest.*:CommandLineArgsTest.*:LegoSurface.*:LegoPlaygroundGpu.* && build-salvage-native/bin/gltf_vmesh_tool_tests'
```

Result: 70 focused config/LEGO tests passed, including all six GPU playground tests; 19 importer tests passed. No selected tests were skipped. This is not the full repository suite.

```bash
nix-shell --run 'build-salvage-native/bin/voxy_native --config lego_world.cfg --width 1920 --height 1080 --vsync --screenshot /tmp/voxys-salvage-native-startup.png --screenshot-frames 24'
```

Result: exit 0, actual Radeon adapter, captured nonblank terrain/studs/water/sky image, and clean application shutdown. Root visually inspected the image. The requested logical window was 1920×1080; desktop scaling produced a **2400×1350 screenshot**. Do not describe this as the 1080p presentation gate. The focused tests overlapped startup, so startup timing is not an isolated performance measurement.

Durable evidence beside this report: `native-build-initial.log`, `native-build-retry.log`, `native-tests.log`, `toolchain.log`, `native-startup.log`, `native-startup.png`, and `native-artifacts.json` with binary hashes. The original failing log is retained alongside the fix/pass evidence.

## Remaining acceptance

- Native incremental shader/config staging passed; see `native-assets.md` and
  `native-staging.json` for the 36 byte-matched source files and unchanged
  executable on a no-op build.
- Complete Bazel/WASM and CMake parity/packaging checks with exact compiler versions and current source/config/shader inputs.
- Verify actual emitted LEGO exports rather than infer failure from omission in explicit linker lists; source uses `EMSCRIPTEN_KEEPALIVE`.
- Coordinate the packaging worker's report and independently review changes before marking BOOT-02 complete.
- BOOT-03 still owns visible scene/performance/live-memory evidence. This build report cannot satisfy that gate.

## Native Bazel integration

The selected native Bazel command was run with `-c opt --jobs=4` and targets
`//tests:config //tests:lego_surface //tests:lego_playground
//tests:gltf_vmesh_tool_test //tests:construction_types`.

The first attempt rejected an ignored `[[nodiscard]]` result from
`Application::spawnThrowable` in the single-ball input branch. The caller now
handles rejection with a debug log. The second attempt found four existing
implicit numeric conversions in `test_lego_surface.cpp`; explicit casts retain
the intended arithmetic. No warnings were suppressed. Both failures are saved
in `native-bazel-initial.log` and `native-bazel-retry.log`.

The final command passed all five targets: 54 config cases, 10 LEGO-surface
cases, six GPU playground cases, 19 importer cases and 13 construction cases.
Construction/importer targets reused successful Bazel test cache results;
the other three executed on the final invocation. All six GPU cases used the
Radeon 890M and none was skipped. Results and per-target logs are retained as
`native-bazel-final.log`, `native-bazel-*.log`, and DATA-01's
`integrated-bazel.log`. This is focused coverage, not the full hook suite.
