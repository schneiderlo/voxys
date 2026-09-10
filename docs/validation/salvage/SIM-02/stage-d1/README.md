# SIM-02 stage-d1 — persistent body layout and prepared mass consumption

Prepared by root on 2026-09-08. The shipping GPU body record now separates
shape-resource identity from material settings, and its force/integration
paths use stored inertia. **This does not activate authored compound bodies,
complete SIM-02 or approve the game's appearance.** Prerequisite review and
G03 remain open.

## Implementation

- Shared `src/physics/gpu/gpu_body_shape.hpp`: 64-byte record aligned to 16 bytes, with
  dimensions/type, principal inverse inertia/material, material coefficients
  and a separate integer shape-reference row. CPU physics, compact rendering,
  culling and all nine WGSL consumers agree on the layout.
- Primitive spawn initializes the reference to zero. Destroy clears it.
  SetMaterial changes only its material fields. The new row reserves shape
  index/generation/format/zero; no public authored-body admission is enabled.
- ForceAtLocalPoint, ballistic integration, dynamic-body preparation and
  static-contact response use the stored principal inverse inertia. Primitive
  inertia is computed at spawn. Existing dynamic-solver and attachment paths
  already consume stored inertia.
- Four added GPU tests run the actual `physics_ballistic.wgsl` entry points.
  Their inputs are controlled GPU tables, including deliberately non-primitive
  mass properties; they do not bypass missing admission and claim gameplay.
- Both build systems include the shared header in the WRECKWATER authority
  closure. The additional cost is 16 bytes per body slot; CPU compact upload
  becomes 96 bytes per body including its 32-byte pose. No new storage binding
  is required by this change.

See [the design](../design.md#stage-d1-persistent-body-layout-and-stored-mass-consumption)
for the ABI, ownership constraints and next integration requirements.

## Checks

The same **12 shared C++ GPU cases** pass in these configurations:

| Configuration | Evidence |
|---|---|
| Bazel native optimized, Radeon 890M / RADV STRIX1 / Vulkan | [Final log](native-attempt03.log), [12-case XML](bazel-gpu_authored_shapes.xml) |
| CMake native Release, same hardware | [Test log](cmake-tests-attempt01.log), [12-case XML](cmake-tests.xml), `native-readbacks/` |
| Native undefined-behavior and float-cast checks, abort on finding | [Log](ubsan-attempt01.log), [12-case XML](ubsan-tests.xml) |
| Actual Chrome 152 hardware WebGPU, AMD RDNA 3 | [Browser report](browser-attempt01/report.json), [test log](browser-attempt01/tests.log), [exact package build](wasm-build-attempt01/build.json) |

No failed, skipped or disabled cases were accepted. The browser package uses
JavaScript exceptions, Asyncify, a fixed 64 MiB heap and 1 MiB stack, assertions
and stack checks. The runner requires every test and all 12 captures, without
uncaptured GPU errors, browser exceptions, device loss or a fallback adapter.

All **12 paired readbacks are byte-identical** on this hardware. The
[comparison](readback-comparison.json) checks defined integer fields exactly
and remaining f32 fields to an absolute 0.00002 tolerance; every numerical
case also checks an independent expected result. This is not cross-vendor
bitwise determinism or a performance claim.

All **123 existing targeted regression cases** pass. With the 12 shared cases,
this is 135 passing native cases across nine executables, not the entire suite:

| Existing target | Passed | Evidence |
|---|---:|---|
| GPU physics | 62 | [XML](bazel-gpu_physics.xml) |
| Broad phase | 20 | [XML](bazel-gpu_broad_phase.xml), [complete benchmark-enabled rerun](broad-phase-complete-attempt01.log) |
| Narrow phase | 4 | [XML](bazel-gpu_narrow_phase.xml) |
| CCD | 3 | [XML](bazel-gpu_ccd.xml) |
| Queries | 8 | [XML](bazel-gpu_queries.xml) |
| Dynamic solver | 7 | [XML](bazel-gpu_dynamic_solver.xml) |
| Primitive culling | 4 | [XML](bazel-primitive_gpu_culling.xml) |
| Primitive rendering | 15 | [XML](bazel-primitive_path.xml) |

The original combined [regression attempt](native-attempt01.log) retained a
primitive upload-size assertion failure and one optional benchmark skip. The
corrected primitive target passes in `native-attempt03.log`. Broad phase was
rerun with its actual shader-directory configuration: all 20 cases, including
the GPU benchmark's overflow checks, then passed with no skips. Its small
fixture timing is not a displayed game performance gate.

Both full applications build: [native](cmake-build-attempt01.log) and
[browser](wasm-app-attempt01.log). The actual generated authority headers from
Bazel and native CMake [match byte for byte](authority-comparison.json).
The [manifest checks](authority-tests-attempt01.log) pass: eight generator and
five content cases, reported by Bazel from unchanged cached test actions.
The changed real build header was separately regenerated. The existing
authority-header target is native-only; no WASM authority header is claimed.

## Full application runtime investigation

The fresh shipping application package is `/tmp/voxys-body-layout-browser-d1`.
[Package hashes](application-packages.json) bind its unchanged web files and
current `voxy_wasm.js`, `.wasm` and `.data` to these attempts. The same record
includes `/tmp/voxys-observation-browser-b1`, the previously passing package
used for the comparison. No shader override or reduced scene was used.

Both current and previous packages initialize on hardware Chrome/Wayland,
render the cove, walk and acknowledge Reset. Both then close Chrome at the
mouse-lock click. The [current report](application-salvage-attempt02.json) and
[previous-build report](application-baseline-attempt01.json) retain the crash
diagnostics, without game-side GPU errors. Their separate journey reports
stop after `reset-button`. This is reproducible on the prior package too;
it is not attributed to the body-layout change and is not a successful
Wayland lifecycle result.

The first failed run also exposed a runner bug: cleanup tried another CDP
request after the socket had closed and could wait forever. It was explicitly
terminated, with [incomplete evidence recorded](application-salvage-attempt01-abandoned.json).
The runner now rejects requests on a closed socket and bounds each request
to 30 seconds. The subsequent crashes correctly write reports and exit failed.

An explicit `gaming-x11` hardware mode was added for the comparison. It retains
1920×1080, the same game package, scene and GPU workload; the report records
the changed window system. Its first run exposed a separate test timing race:
the character had not settled after a fixed 500 ms sleep following respawn.
The [failed journey](application-salvage-x11-journey-attempt01/summary.json)
preserves both positions. The runner now waits for the actual supported camera
position, retaining the same 0.15 m tolerance with a seven-second bound.
This changes no game behavior, physics tolerance or product performance gate.

The final X11 [application report](application-salvage-x11-attempt03.json) and
[journey report](application-salvage-x11-journey-attempt03/summary.json) pass on
the AMD RDNA 3 hardware adapter, with no fallback, uncaptured GPU error or
browser exception. Actual walking, pointer lock, button/keyboard Reset,
legacy-control isolation, Leave, fresh re-entry, single Reset and Leave-over-
Reset ordering all pass. The cove owns 32 bodies, Leave reaches zero, and
re-entry returns to 32 with new world/observer identity. No scenery action
spends inventory or fabricates gameplay events. The separate terrain study
also completes grouped/single switching (265/620 bricks), drop and reset.

Attempt02 passed the state journey, but visual inspection found its legacy
world screenshot still covered by the loading screen. It is retained as
incomplete presentation evidence. The runner now waits for the overlay to
disappear, at least 12 application frames and available retired GPU timestamp
data after navigation. Attempt03 passes this stronger check. Its
[legacy world capture](application-salvage-x11-journey-attempt03/04-legacy-world.png)
was inspected and shows the actual scene; cove/startup/re-entry captures show
the same primitive placeholder art. There is no LOOK-01 acceptance.

The X11 path was visibly slow (the diagnostic overlay shows approximately
1–1.5 FPS in some captures). These instrumented interaction checks do not
establish the cause or pass any performance gate. X11 was explicitly selected
for diagnosis; the default runner remains Wayland. The reproduced Wayland
mouse-lock crash stays open under QA-06, and the X11 result does not substitute
for its resolution. [The machine-readable record](summary.json) binds the
source and artifact hashes, counts and limitations for this checkpoint.

## What the four added cases prove

1. **Prepared force response.** A 5 kg body with COM (0.25, -0.5, 0.75) m
   uses the asymmetric full tensor from stage-a1. At root point (1, -2, 3) m,
   force (2, 3, -4) N applied for 1/60 s produces linear velocity change
   (0.4, 0.6, -0.8)/60 and angular velocity change
   (-1357/3600, 5863/1800, 56/45)/60. The actual command and
   `prepare_dynamic_bodies` kernels must use the supplied principal inertia,
   which differs from that derived from the cube dimensions. This does not
   integrate the pose or activate compound collision.
2. **COM-centered pose.** The actual ballistic integrator advances stored COM
   with velocity (3, 4, 5) m/s and angular velocity (0, 0, 2) rad/s for 1/60 s.
   The resulting quaternion and separately reconstructed root position match
   the closed-form reference within 0.000005. This bounds the one-step
   normalized Euler approximation. It does not certify general gyroscopic
   tumbling or long-duration conservation.
3. **Independent reference lifetime.** A poisoned nonzero reference is cleared
   by primitive spawn and by destroy, with two actual GPU readbacks. Spawn
   retains the correct 0.3 diagonal inverse inertia of a 2 m, 5 kg cube.
4. **Material separation.** SetMaterial updates flags and coefficients while
   preserving the reference and supplied principal inertia. Defined integer
   fields, including high material bits, are compared exactly.

The earlier eight storage/lifetime cases still execute; their meaning is
documented in [stage-c1](../stage-c1/README.md#test-meanings). The body-kernel
fixtures disable terrain and water interactions explicitly to isolate mass
and frame behavior. The separately passing legacy tests cover existing
geometry consumers and actual primitive rendering.

## Reproduction

From the repository root, use the configured Nix/toolchain environment:

```bash
nix-shell
bazel test -c opt //tests:gpu_authored_shapes //tests:gpu_physics \
  //tests:gpu_broad_phase //tests:gpu_narrow_phase //tests:gpu_ccd \
  //tests:gpu_queries //tests:gpu_dynamic_solver \
  //tests:primitive_gpu_culling //tests:primitive_path \
  --test_env=VOXY_BROAD_PHASE_BENCHMARK_SHADER_DIR=shaders --test_output=all
cmake --build build-salvage-native --target gpu_authored_shapes_tests voxy_native -j 8
build-salvage-native/bin/gpu_authored_shapes_tests
bazel test -c opt --config=ubsan --copt=-fsanitize=float-cast-overflow \
  --linkopt=-fsanitize=float-cast-overflow --copt=-fno-sanitize-recover=all \
  //tests:gpu_authored_shapes --test_output=all
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8
```

Build a fresh diagnostic package with the browser GPU port, then run Chrome
outside Nix to preserve the host's hardware browser driver configuration:

```bash
python3 scripts/build_shape_diagnostics.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/shape-body-package-new
/home/modkin/.nix-profile/bin/node scripts/run_shape_diagnostics.mjs \
  /tmp/shape-body-package-new /tmp/shape-body-browser-new
mkdir /tmp/shape-body-native-new
VOXY_SHAPE_CAPTURE_DIR=/tmp/shape-body-native-new \
  build-salvage-native/bin/gpu_authored_shapes_tests
python3 scripts/compare_shape_diagnostics.py \
  /tmp/shape-body-native-new /tmp/shape-body-browser-new \
  /tmp/shape-body-comparison-new.json --body-layout
```

Use fresh evidence paths. Historical failed attempts must remain intact.

For the full application check, stage the unchanged `web/` contents plus
the three current `build-lego-wasm/bin/voxy_wasm.{js,wasm,data}` files into a
fresh directory. Outside Nix, with the host's X11 display available:

```bash
VOXY_SMOKE_GPU=gaming-x11 VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_REPORT=/tmp/body-app-report-new.json \
VOXY_SMOKE_SCREENSHOT=/tmp/body-app-startup-new.png \
VOXY_SMOKE_SALVAGE=/tmp/body-app-journey-new \
  /home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs \
  /tmp/body-app-package-new salvage
```

Set `VOXY_SMOKE_GPU=gaming` with fresh output paths to reproduce the recorded
Wayland case. Preserve the exact hardware/window-system identity and all
failures. The unchanged UI-control suite also passes seven cases in
[its log](preview-controls-test.log); run `node scripts/test_salvage_preview.mjs`.

## Retained failures and corrections

| Attempt | Finding and correction |
|---|---|
| `native-attempt01.log` | Existing compact-upload tests expected the old 80-byte total. Update their expectation to 32-byte pose plus 64-byte shape and the corresponding partial shape upload; the renderer's actual byte accounting was correct. The optional broad-phase benchmark initially skipped without its shader directory; a separate fully configured run passes all 20 cases. |
| `native-attempt02.log` | GCC rejected misleading indentation in the new test shader reader. Split the statements; no warning was suppressed. |
| `application-salvage-attempt01*`, `application-salvage-attempt02*`, `application-baseline-attempt01*` | Chrome/Wayland closes at mouse-lock on both current and previous packages. Attempt01's reporting also hung after closure; closed-socket rejection and bounded CDP calls retain the later failures. No Wayland lifecycle pass is claimed. |
| `application-salvage-x11-attempt01*` | The fixed 500 ms reset delay sampled the character above its resting position. Wait for the actual initial supported pose under the unchanged 0.15 m tolerance and a seven-second limit. |
| `application-salvage-x11-attempt02*` | Lifecycle state passes, but the legacy screenshot was early and showed loading. Require actual completed startup GPU work and a hidden loading overlay before scene captures. Attempt03 passes and its legacy capture was visually inspected. |

The final relevant tests pass with their original analytical tolerances. Native
headless runs retain an `XDG_RUNTIME_DIR` diagnostic but initialize and execute
on the named hardware adapter. Existing application compiler warnings remain
visible in the build logs; no warning policy was relaxed for this checkpoint.

## Still required

Typed authored-body admission, actual backend/application shape-use ownership,
retained assembly revision and provenance, root-local force/attachment adapters,
every authored geometry consumer, device loss/recovery, explicit CPU refusal
and independent acceptance remain open. The existing scene is still a cove
prototype and the appearance gate remains rejected/open. No complete gate
has passed and no gate commit is made for this checkpoint.
