# SIM-02 stage-c1 — actual GPU shape storage and completion

Prepared by root on 2026-09-08. This checkpoint implements the GPU resource
component and shader decoding/mass helpers. **It does not complete SIM-02 or
activate a playable compound body.** SIM-01 and SIM-02 independent acceptance,
the G03 gate, and LOOK-01 visual approval remain open.

## What is implemented

- One bounded GPU storage buffer with generation-checked descriptors, exact
  exterior collision cells/faces/BVH and the prepared principal-body mass frame.
- Real checked uploads and submissions. The CPU lifetime ledger advances only
  after actual queue completion and successful error scopes. There is no public
  method to inject an estimated completed serial.
- Eight pending operations, one unresolved external submission, bounded
  contiguous range admission, fragmentation refusal and coalesced retirement.
- Preservation of already encoded use and retained CPU readers through retire;
  descriptor invalidation and generation increments before replacement reuse.
- Controlled close/drain and sticky failure that prevents uncertified reuse.
- WGSL accessors and root/body/COM/impulse mappings tested by real compute work.

The complete ABI, memory/thread ownership, submission ordering and limitations
are in [the design](../design.md#stage-c1-actual-gpu-shape-resource).
The main source is `src/physics/gpu/gpu_authored_shapes.{hpp,cpp}`; the shared
shader is `shaders/physics_authored_shapes.wgsl`. Normal CMake/Bazel core builds
include the component. The separate WRECKWATER authority source closure and
legacy body/shape GPU ABI are unchanged by this checkpoint.

## Evidence

The same **eight C++ GPU cases** pass without skipped or disabled cases:

| Configuration | Evidence |
|---|---|
| Bazel native, optimized, Radeon 890M / RADV STRIX1 / Vulkan | [Final log](native-attempt06.log), [8-case XML](bazel-tests.xml), `native-readbacks/` |
| CMake native Release, same hardware | [Final log](cmake-tests-attempt01.log), [8-case XML](cmake-tests.xml) |
| Native undefined-behavior plus float-cast instrumentation, abort on finding | [Final log](ubsan-attempt01.log), [8-case XML](ubsan-tests.xml) |
| Actual Chrome 152.0.7977.82 hardware WebGPU, AMD RDNA 3 | [Browser report](browser-attempt02/report.json), [C++ log](browser-attempt02/tests.log), [exact package build](wasm-build-attempt04/build.json) |

The browser package uses C++ exceptions through JavaScript, Asyncify, a fixed
64 MiB heap and 1 MiB stack, assertions and stack checks. It rejects fallback
adapters, missing/extra readbacks, failed/skipped/disabled cases, uncaptured GPU
errors, browser exceptions and device loss. The browser's C++ exit notification
alone is insufficient: the first failing browser attempt reported exit code 0,
so acceptance also requires the exact GTest pass count and absence of failures.

All seven paired native/browser readbacks have identical bytes on this hardware.
The [reproducible comparison](readback-comparison-reproducible.json) requires
exact defined integer outputs and absolute f32 error at most 0.00002; each suite
also checks the analytical values independently. This is not a cross-vendor
bitwise determinism or performance claim. The initial inline comparison remains
in `readback-comparison.json`; the reusable runner repeats it without changing
either set of original bytes.

Both complete applications build:
[native CMake](cmake-build-attempt01.log),
[browser CMake](wasm-app-attempt01.log). These are build checks; no new live
machine, cove appearance or displayed frame-time result is asserted.

All **78 existing regression cases** also pass without skips: 62 GPU physics,
6 LEGO playground and 10 cove preview cases. See the
[regression build/run log](legacy-regression-attempt01.log) and
`legacy-{gpu_physics,lego_playground,salvage_preview}.xml`. They exercise existing
body behavior and scene lifetimes; they do not certify authored-body integration.

## Test meanings

1. Decode an asymmetric two-cell union: 14 exterior face patches, 3 BVH nodes,
   2 leaves, 8.15 m³ volume and 25.2 m² exterior area. Face provenance sums to
   118 from source labels 7 and 11. Invalid cell/face/node indices return invalid.
2. Reserve and encode a read, then retire the shape before actual submit. It
   remains valid for that GPU work and a retained CPU reader. Capacity refusal
   preserves the caller's replacement. After actual completion and reader
   release, reuse increments generation; the old GPU descriptor is rejected.
3. Discard an unused reservation through a real empty submission/fence. Old
   generation lookup is invalid after retirement; duplicate discard is rejected.
4. Fill all eight pending operation slots without polling. The ninth upload
   refuses without consuming its prepared storage; it succeeds after drain.
5. With six cell slots, fragmented free intervals cannot admit a two-cell shape
   even when two cells are free. Adjacent retirement coalesces space; unaffected
   and replacement shapes retain their distinct actual GPU provenance values.
6. Invalid context/budget, foreign handles, an oversized use list and a foreign
   ticket cannot publish a use or corrupt the CPU ledger.
7. Close waits for both an unresolved ticket and a retained reader; successful
   drain leaves zero CPU charged resources, GPU bytes and pending callbacks.
8. Finish and actually submit an invalid self-copy under real device scopes.
   The store becomes Failed, does not certify that serial, retains its referenced
   shape and rejects new uploads. No mocked completion or failure status is used.

The analytical mass fixture uses mass 5 kg, COM (0.25, -0.5, 0.75) m and the full
root tensor:

```text
88/25    -2/75     8/15
-2/75    161/75    2/5
8/15      2/5    10/3
```

A root point (1, -2, 3) m receives impulse (2, 3, -4) N·s. The GPU must return
linear delta (0.4, 0.6, -0.8) and angular delta
(-1357/3600, 5863/1800, 56/45). Under a 90° world Z rotation, root position
(10, 20, 30) gives COM (10.5, 20.25, 30.75); root velocity (3, 4, 5) with angular
velocity (0, 0, 2) gives COM velocity (2.5, 5, 5). Root-point and pose round trips
are checked. This validates uploaded mass-frame math, not time integration.

## Reproduction

Run from the repository root in the configured Nix/toolchain environment.
Use fresh evidence paths: the browser builder, browser runner and comparison
runner refuse to replace existing output directories/files.

```bash
nix-shell
bazel test -c opt //tests:gpu_authored_shapes --test_output=all
cmake --build build-salvage-native --target gpu_authored_shapes_tests voxy_native -j 8
build-salvage-native/bin/gpu_authored_shapes_tests
bazel test -c opt --config=ubsan --copt=-fsanitize=float-cast-overflow \
  --linkopt=-fsanitize=float-cast-overflow --copt=-fno-sanitize-recover=all \
  //tests:gpu_authored_shapes --test_output=all
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8
```

The browser GPU port is installed in `/tmp/voxys-emsdk/upstream` on the reference
host. The separate frozen Bazel CPU-only SDK cache does not contain that port.
The build script takes an explicit SDK path and preserves compiler commands,
tool/source hashes, logs and package hashes in `build.json`.

```bash
python3 scripts/build_shape_diagnostics.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/shape-gpu-package-new
```

Run Chrome outside Nix to preserve the host's hardware browser driver setup:

```bash
/home/modkin/.nix-profile/bin/node scripts/run_shape_diagnostics.mjs \
  /tmp/shape-gpu-package-new /tmp/shape-browser-evidence-new
```

To capture desktop bytes, create an empty directory and set
`VOXY_SHAPE_CAPTURE_DIR` to its absolute path when running the CMake executable.
For the Bazel test, forward that variable with `--test_env` and use
`--test_strategy=standalone --spawn_strategy=local` to allow capture writes.
Then compare both directories:

```bash
python3 scripts/compare_shape_diagnostics.py \
  /tmp/shape-native-evidence-new /tmp/shape-browser-evidence-new \
  /tmp/shape-comparison-new.json
```

## Retained failures and corrections

| Attempt | Finding and correction |
|---|---|
| Nix startup before native attempt01 | Restricted execution could not connect to the Nix daemon. Tool output recorded that denial; the authorized configured build ran with host access. No build/test result is inferred from the denied invocation. |
| `native-attempt01.log` | Registering the source in Bazel's auto-target list and as an explicit target created a duplicate. Keep the explicit focused target and include the source separately in the aggregate. |
| `native-attempt02.log` | GCC's strict null-dereference check rejected an optimized stream-iterator shader reader. Use a bounded checked-size read; no warning was suppressed. |
| `native-attempt03.log` | WGSL reserves `layout`. Rename the shader local to `sections`. Four actual shader cases failed; the four resource-only cases passed. |
| `wasm-build-attempt01/` | The selected frozen CPU SDK lacked the browser GPU port. Use the already configured browser SDK. |
| `wasm-build-attempt02/` | Clang found a test variable shadowing a browser-only callback descriptor. Rename the test-info variable. |
| `native-attempt04.log`, `wasm-build-attempt03/`, `browser-attempt01/` | Native passed; browser passed seven cases but correctly rejected the eighth test's assumption that an abandoned unfinished encoder must report a validation error. Dawn defers this validation. Finish and submit the deliberately invalid command, then require real error-scope failure on both platforms. No runtime failure behavior, oracle or tolerance was relaxed. |
| `native-attempt05.log` | Bazel has no `local` test strategy. Use its supported `standalone` test strategy with local spawning for readback capture. |

The final native, CMake, sanitizer and browser attempts above all pass.
Historical failed packages and logs remain separate and unmodified.

## Remaining integration

`PhysicsWorld`/`GpuPhysicsBackend` still need typed authored-body admission and
explicit unsupported CPU behavior; retained assembly revision/provenance binding;
actual body pose, force, attachment and render mappings; idle device-loss and
context recovery; live geometry consumers and compound collision/CCD/query
coverage. The resource API's owner must declare every use, balance scopes on
the device-owning thread, and retain matching compiled provenance. The single
atlas cannot independently enforce correct external consumer declarations.

This checkpoint exercises validation failure, not physical device removal or
an allocation-fault/thread-sanitizer proof of GPU driver internals. It does not
change the cove art or certify gameplay. No gate has passed and no commit is
authorized by this checkpoint alone under the plan's acceptance rules.
