# SIM-02 stage-f1 — checked root/body motion and point conversion

Prepared by root on 2026-09-08. `AuthoredBodyFrame` now converts between a
machine's authored root and its COM/principal body frame, using the same packed
mass transform supplied to GPU consumers. **This is checked motion preparation
and GPU kernel conformance, not live authored-body admission or a completed
gameplay/visual gate.** SIM-01 review, SIM-02 acceptance and G03 remain open.

## Contract

`src/physics/authored_body_frame.hpp` defines separate `AuthoredRootMotion`
and `AuthoredBodyMotion` records. Their named origin/center velocities prevent
implicit interchange. Angular velocity remains in world axes. The fixed-size
`AuthoredBodyFrame` owns a copy of the validated `PackedShapeMass`; it can
outlive the source shape value, but carries no world handle, geometry ownership,
revision, provenance or activation rights. Callers must retain the matching
shape/revision separately.

For world root rotation `Q`, packed root-local COM `c` and principal rotation
`R`, body orientation is `Q R`, body center is `rootPosition + Q c` and
center velocity is `originVelocity + omega × Q c`. The inverse reconstructs
the root orientation and subtracts its current COM offset and velocity shift.
An attachment/force point uses `R^T (rootPoint - c)` exactly once. Point maps
use the packed quaternion's quantization, following the GPU helper convention;
they do not decompose the original tensor again.

Positions retain signed integer sectors plus canonical f32 local coordinates
in `[-128,128)`. Conversions perform the small local shift in double and check
integer carries before narrowing. No far absolute coordinate is formed.
Rounding a local value just below +128 to f32 +128 carries into the next sector;
if that would exceed INT32_MAX, the operation rejects. Both ends of the world
reject overflow without saturation. Legacy primitive canonicalization is
unchanged.

Noncanonical/nonfinite positions, nonfinite velocities/points, invalid
orientations and out-of-f32-range results return typed errors and no partial
output. Quaternion squared norm must be within .001 of one; accepted values
are normalized, with a canonical sign and positive zero components. Arbitrary
scaled/zero quaternions are not silently repaired. Ordinary f32 rounding remains
part of the format. This is a range/representation check, not configured speed,
point/profile, tick, shape-identity or backend-capability admission.

The implementation uses fixed-size values and no allocation calls. The tests
exercise 1,000 dense world orientations with an independent Rodrigues reference;
this is not an allocation-instrumentation or frame-time measurement. The named
numerical tolerances below belong to these fixtures, not a universal accumulated
error bound for arbitrary machines and simulation duration.

## Actual checks

All **10 CPU frame cases and 16 shared GPU cases** pass in each configuration:

| Configuration | Evidence |
|---|---|
| Bazel optimized native, Radeon 890M / RADV STRIX1 / Vulkan | [Final action log](native-tests-attempt03.log), [CPU XML](bazel-authored_body_frame.xml), [GPU XML](bazel-gpu_authored_shapes.xml); full final output in `bazel-*.log` |
| CMake Release native, same hardware | [CPU log](cmake-frame-tests-attempt01.log), [GPU log](cmake-gpu-tests-attempt01.log), [CPU XML](cmake-frame-tests.xml), [GPU XML](cmake-gpu-tests.xml) |
| Native undefined-behavior and float-cast instrumentation, abort on finding | [Final log](ubsan-attempt02.log), [CPU XML](ubsan-authored_body_frame.xml), [GPU XML](ubsan-gpu_authored_shapes.xml) |
| Actual Chrome 152 hardware WebGPU, AMD RDNA 3 | [26-case test log](browser-attempt01/tests.log), [browser report](browser-attempt01/report.json), [exact package build](wasm-build-attempt02/build.json) |

These final test actions all executed after the explicit precision-conversion
correction. No failed, skipped or disabled cases are accepted. The browser
package compiles the same ten CPU cases alongside the sixteen real GPU cases;
it is not a separate JavaScript reimplementation. It retains JavaScript
exceptions, Asyncify, assertions, a fixed 64 MiB heap and 1 MiB stack. The
runner requires a hardware adapter, all cases/captures and no uncaptured GPU
error, device loss or browser exception.

All **16 native/browser GPU readbacks are byte-identical** on this hardware.
[Comparison](readback-comparison.json) treats defined integer fields, including
the new signed-sector/flags row, exactly and checks f32 fields within 0.00002.
Each numerical fixture also checks its analytical expectation. This does not
establish cross-vendor bitwise determinism or product performance.

The CPU cases cover rotated COM/velocity, near versus far sector precision,
both world limits, the rounding carry, invalid pose/motion/points, finite f32
overflow, point placement, quaternion sign/norm conventions and 1,000 dense
orientations. The independent dense reference checks center shift within
0.00001 m, velocity within 0.000002 m/s and restored root position within
0.00002 m for the stated fixture.

Two added GPU cases execute the shipping `physics_ballistic.wgsl` command and
integration entry points with inputs produced by the new converter:

1. **Rotated force and full tensor.** The 5 kg asymmetric stage-a1 mass has COM
   `(0.25,-0.5,0.75)` and principal moments `(2,3,4)`. Rotate its root 90° around
   world Z. A world force `(-3,2,-4)` N at root point `(1,-2,3)` m produces
   `delta v=(-0.6,0.4,-0.8)/60` and
   `delta omega=(-5863/1800,-1357/3600,56/45)/60` after one force tick. Actual
   outputs match within 0.000002, and GPU sector labels remain exact.
2. **Motion, sector carry and root reconstruction.** The same rotated frame
   starts at root local `(127.46875,-127.875,127.1875)` with sector
   `(1000000,-1000000,2000000000)`, origin velocity `(3,4,5)` and world omega
   `(0,0,2)`. COM velocity is `(2.5,5,5)`. The real GPU step carries COM into
   the next X/Z sectors. Converting the actual readback restores the root to
   the original sectors and matches the independent rotating-offset position
   and velocity within 0.00002. This bounds the one-step normalized Euler
   approximation; it does not certify long-term gyroscopic motion.

Both fixtures explicitly disable terrain/water interactions and use controlled
GPU tables. They do not claim authored collision or route through a missing
compound spawn API. The earlier fourteen shared cases still execute; their
resource, kernel and backend ownership meanings remain documented in
[stage-c1](../stage-c1/README.md), [stage-d1](../stage-d1/README.md) and
[stage-e1](../stage-e1/README.md).

## Build and integration boundary

The normal physics library and combined/focused tests include the helper in
both build systems. Full [native](cmake-build-attempt02.log) and
[browser](wasm-app-attempt02.log) application builds pass. This helper is not
called by current application routes; it does not cause per-frame CPU pose
readback. Future direct rendering must continue to reconstruct roots on the
GPU using the matching mass data.

The narrow WRECKWATER authority does not yet call/include this helper, so its
source allowlist is unchanged. When typed body admission uses it there, extend
the real authority closure and generated identity together; do not resolve
that future link by adding the full graphical physics dependency tree.

No new full-application lifecycle, art or performance result is claimed.
The [earlier application investigation](../stage-d1/README.md#full-application-runtime-investigation)
still records the open Chrome/Wayland pointer-lock crash and the slow X11
diagnostic path. The compute browser run does not exercise pointer lock.

## Reproduction

Use fresh output paths; earlier failed attempts remain part of the record.
From the repository root in the configured Nix environment:

```bash
nix-shell
bazel test -c opt //tests:authored_body_frame //tests:gpu_authored_shapes --test_output=all
cmake --build build-salvage-native --target authored_body_frame_tests \
  gpu_authored_shapes_tests voxy_native -j 8
build-salvage-native/bin/authored_body_frame_tests
mkdir /tmp/frame-native-new
VOXY_SHAPE_CAPTURE_DIR=/tmp/frame-native-new \
  build-salvage-native/bin/gpu_authored_shapes_tests
bazel test -c opt --config=ubsan --copt=-fsanitize=float-cast-overflow \
  --linkopt=-fsanitize=float-cast-overflow --copt=-fno-sanitize-recover=all \
  //tests:authored_body_frame //tests:gpu_authored_shapes --test_output=all
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8
python3 scripts/build_shape_diagnostics.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/frame-browser-package-new
```

Run hardware Chrome outside Nix to retain the host browser/driver environment:

```bash
/home/modkin/.nix-profile/bin/node scripts/run_shape_diagnostics.mjs \
  /tmp/frame-browser-package-new /tmp/frame-browser-new
python3 scripts/compare_shape_diagnostics.py \
  /tmp/frame-native-new /tmp/frame-browser-new \
  /tmp/frame-comparison-new.json --motion-frame
```

## Retained failures and remaining work

| Attempt | Finding and correction |
|---|---|
| [First native test build](native-frame-attempt01.log) | GCC rejected an unbraced `if` around a GTest assertion because the macro contains control flow. Add explicit braces; no warning policy change. |
| [First browser diagnostic build](wasm-build-attempt01/compile-authored_body_frame.log) | Strict Clang checks rejected five implicit float-to-double promotions in quaternion unpacking and the f32 range constant. Make the intended double conversions explicit. Final native, sanitizer and browser checks rerun successfully. |

Earlier passing native/sanitizer XML is preserved with the
`pre-browser-correction-` prefix. Initial application builds also remain in
their attempt01 logs. Existing native `XDG_RUNTIME_DIR` and application GLM
comparison warnings are retained; the diagnostic's previously documented
GLM comparison exception is unchanged. No new suppression was introduced.

Root reviewed the frame conventions, narrowing/carry bounds, typed failures
and build integration against the executed checks. This is self-review;
independent prerequisite/implementation acceptance is still outstanding.
[The machine-readable summary](summary.json) binds final sources, binaries,
artifacts and case counts.

Still required: typed authored-body admission and capability refusal, matching
assembly/revision/provenance ownership, automatic shape-use declaration at the
application queue boundary, integration of these conversions at all commands,
attachments, snapshots and render/debug consumers, complete compound geometry,
certified tick frontiers, device loss/recovery and independent acceptance.
No full repository hook, parent task, visual or gameplay gate passes here;
the implementation goal remains active and no gate commit is made.
