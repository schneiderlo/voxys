# SIM-02 stage-e1 — backend-owned authored shape resources

Prepared by root on 2026-09-08. `PhysicsWorld` now exposes an opt-in, backend-owned
shape resource interface. Callers can prepare shapes and register their actual
GPU use without downcasting to the GPU backend. **This does not enable authored
bodies, complete SIM-02 or approve the game's appearance.** Independent review,
the SIM-01 prerequisite and G03 remain open.

## Ownership and behavior

- `src/physics/authored_shape_resources.hpp` defines `IAuthoredShapeResources`
  and its typed errors, limits, states and submission tickets. The existing GPU
  store implements it; old GPU-specific names remain aliases. The atlas format,
  generation checks and stage-c1 lifetime implementation are unchanged.
- `PhysicsWorld::enableAuthoredShapeResources()` accepts defaults or explicit
  limits. Successful GPU allocation publishes an Initializing interface;
  polling real scopes and completion determines Ready. Reconfiguration returns
  `AlreadyConfigured`. Failed enable leaves no published interface.
- Uninitialized and moved-from worlds return `NotInitialized`. Jolt and Box3D
  return `Unsupported`, including explicitly requested CPU fallback. No shape
  is converted into a primitive and no authored-body capability is advertised.
- The backend owns the store. Moving the world preserves its object and
  borrows. World updates, including zero delta, poll it. Persistent GPU memory
  accounting includes the actual atlas until drained, including retirement.
  The store separately exposes CPU charges, pending work and cumulative uploads.
- Every backend allocation burns a fresh process-local, nonzero 64-bit pool
  identity, including failed allocation attempts. The monotonic allocator never
  wraps. Restart can reuse a slot/generation pair without accepting the old
  world's handle. Low-level explicit-identity factories still require callers
  to supply a unique identity; handles are not durable IDs or security tokens.
- Declare all used handles **before encoding**. `submit` performs the actual
  queue submission; it is not an acknowledgment after another submit. Discard
  requires releasing all associated encoders/commands first. Compiled assembly
  revision and source-label provenance remain separately owned by the caller.
- Normal shutdown requires closing/draining resources and releasing dependent
  commands and borrows. Forced shutdown reports abandonment and ends borrows;
  it does not certify unfinished GPU work. Existing independent callback state
  survives and the buffer is released without destroying encoded references.
  Submitting old world commands after shutdown is outside this contract.

The facade header forward-declares resource types, avoiding heavy geometry
includes in ordinary renderer/platform code. Callers using resource methods
include `physics/authored_shape_resources.hpp` explicitly. See the
[design and remaining integration](../design.md#stage-e1-backend-ownership-and-facade-access).

## What the new checks prove

1. **Real shared submission.** A 32-body-capacity GPU backend owns an uploaded
   asymmetric shape. Its atlas probe and an actual primitive-body physics tick
   share one checked queue submission. The test immediately retires the shape,
   reads its two cells, fourteen exterior patches, three BVH nodes and source
   labels, and checks the completed primitive position `(2+1/60, 3, 4)` within
   0.00002 m. Gravity and damping are explicitly zero. The shape is a GPU
   resource read, not the primitive's collision geometry.
2. **Backend restart.** After actual drain/shutdown/reinitialization, a new
   shape has the same index/generation but a different pool identity. Old
   get/retain/retire/submission requests fail without changing the new ledger.
   A real GPU read returns the new shape's expected source labels.
3. **Facade move and CPU refusal.** The resource borrow follows a moved GPU
   world, zero-delta updates complete initialization, and normal close/drain
   precedes shutdown. Separate CPU cases preserve terrain and memory accounting
   while explicitly refusing resources; unavailable GPU and allowed CPU
   fallback paths return their appropriate typed errors.

The earlier twelve GPU cases still execute, including actual upload/retirement,
range fragmentation, retained readers, discarded work, validation failure and
shipping-kernel mass behavior. Their meanings remain in
[stage-c1](../stage-c1/README.md#test-meanings) and
[stage-d1](../stage-d1/README.md#what-the-four-added-cases-prove).

## Validation

The shared GPU suite contains **14 cases**. Evidence is configuration-specific:

| Configuration | Evidence |
|---|---|
| Bazel optimized native, Radeon 890M / RADV STRIX1 / Vulkan | [Final log](native-tests-attempt04.log), [14-case XML](bazel-gpu_authored_shapes.xml) |
| CMake Release native, same GPU | [Fresh test log](cmake-tests-attempt01.log), [14-case XML](cmake-tests.xml), `native-readbacks/` |
| Strict native undefined-behavior and float-cast checks, abort on finding | [Fresh test log](ubsan-attempt01.log), [14-case XML](ubsan-tests.xml) |
| Actual Chrome 152 hardware WebGPU, AMD RDNA 3 | [Report](browser-attempt01/report.json), [test log](browser-attempt01/tests.log), [exact package build](wasm-build-attempt02/build.json) |

The native target selection also passes **63 GPU physics and 54 physics-world
cases**, including the new facade checks: **131 cases across three executables**,
not the whole repository suite. See [GPU physics](bazel-gpu_physics.xml) and
[CPU/facade XML](bazel-physics_world.xml). No failed, skipped or disabled cases
are accepted in these records.

The final Bazel action rebuilt the backend after the explicit wasm32 accounting
cast, then reused the same successful 14- and 63-case test actions. Those suites
actually executed in [attempt03](native-tests-attempt03.log), before the cast;
their original XML is retained as `pre-wasm32-cast-*.xml`. The 54-case world
suite executed in attempt04. The final CMake 14-case suite and hardware browser
suite executed after the cast. Cached actions are not described as fresh runs.

All **14 paired GPU readbacks are byte-identical** on this hardware. The
[comparison](readback-comparison.json) checks defined integer fields exactly
and f32 fields within 0.00002; numerical cases also check independent expected
values. This is not cross-vendor determinism or a performance result.

The browser diagnostic compiles the real backend and needed physics sources,
uses the shipping shader files, JavaScript exceptions and Asyncify, and retains
a fixed 64 MiB heap and 1 MiB stack. First-party files use strict warnings and
errors, with the previously documented unrelated GLM defaulted-comparison
exception unchanged. The runner requires all fourteen cases and captures, a
hardware adapter, no uncaptured GPU errors, no device loss and no browser
exceptions. This diagnostic does not exercise application pointer lock.

Both full applications build: [native](cmake-build-attempt02.log) and
[WASM](wasm-app-attempt01.log). No application route calls the new opt-in API yet.
No fresh complete-application journey or visual acceptance is claimed here.
The prior [application investigation](../stage-d1/README.md#full-application-runtime-investigation)
retains the open Chrome/Wayland mouse-lock crash and slow X11 diagnostic path.
Those failures have not been fixed by this resource integration.

## Authority build integration

The native headless backend now links shape preparation, pooling, exact box
geometry and the GPU store. Its explicit Bazel/CMake source lists and content
allowlist include their thirteen additional files. It does not link the normal
renderer/window/Jolt/Box3D implementation closure to satisfy those symbols.

- [Bazel server/header build](bazel-authority-build-attempt01.log) passes.
- [Actual Bazel configured graph](bazel-authority-closure-attempt01.log) matches
  all **88 first-party source/header files** in the allowlist.
- [Actual CMake server check](cmake-authority-closure-attempt01.log) matches
  **37 translation units**, with required symbols present and forbidden binary
  symbols/dynamic dependencies absent. This is a separately executed check
  target, not an assumption from successful linking.
- The newly generated native [authority headers](authority-comparison.json)
  match byte for byte between build systems. No WASM authority header is claimed.
- [Eight generator and five manifest tests](authority-tests-attempt01.log)
  pass from unchanged cached test actions. Real closure and header generation
  above separately validate the changed input list.

## Reproduction

From the repository root, using the configured Nix environment:

```bash
nix-shell
bazel test -c opt //tests:gpu_authored_shapes //tests:gpu_physics \
  //tests:physics_world --test_output=all
cmake --build build-salvage-native --target gpu_authored_shapes_tests \
  voxy_native wreckwater_server_closure_check -j 8
build-salvage-native/bin/gpu_authored_shapes_tests
bazel test -c opt --config=ubsan --copt=-fsanitize=float-cast-overflow \
  --linkopt=-fsanitize=float-cast-overflow --copt=-fno-sanitize-recover=all \
  //tests:gpu_authored_shapes --test_output=all
bazel build -c opt //:wreckwater_server //:wreckwater_build_content_header
python3 tools/check_wreckwater_authority_bazel_closure.py --workspace .
bazel test -c opt //tools:generate_wreckwater_build_content_test \
  //tests:wreckwater_content_manifest --test_output=all
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm \
  --target voxy_wasm -j 8
```

For a new browser package, use fresh output paths. Run the hardware Chrome
process outside Nix to retain the host browser/driver environment. Run the
native binary within Nix when collecting its paired captures.

```bash
python3 scripts/build_shape_diagnostics.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/shape-owner-package-new
/home/modkin/.nix-profile/bin/node scripts/run_shape_diagnostics.mjs \
  /tmp/shape-owner-package-new /tmp/shape-owner-browser-new
mkdir /tmp/shape-owner-native-new
VOXY_SHAPE_CAPTURE_DIR=/tmp/shape-owner-native-new \
  build-salvage-native/bin/gpu_authored_shapes_tests
python3 scripts/compare_shape_diagnostics.py \
  /tmp/shape-owner-native-new /tmp/shape-owner-browser-new \
  /tmp/shape-owner-comparison-new.json --backend-owned
```

## Preserved failures and limits

| Attempt | Finding and correction |
|---|---|
| [First native build](native-build-attempt01.log) | Including the full resource/geometry headers through `PhysicsWorld` exposed enum `None` to X11's macro in an application translation unit. Keep the facade header light with forward declarations and a separate no-argument overload. No global macro change or warning suppression was used. |
| [First browser build](wasm-build-attempt01/compile-gpu_physics_backend.log) | Strict wasm32 compilation rejected implicit `uint64_t` to `size_t` memory-accounting conversion. Use an explicit cast justified by the resource contract's hard 32 MiB atlas cap. The final diagnostic and full application build pass. |

Initial focused native checks are retained in `native-test-attempt02.log`;
the earlier native application build is `cmake-build-attempt01.log`. Native
headless runs retain the existing `XDG_RUNTIME_DIR` diagnostic while executing
on the named hardware. Existing application warnings remain visible in logs.

Still required: typed authored-body admission, all root/COM consumers, matching
assembly/revision/provenance retention, complete compound geometry capabilities,
automatic live shape-use declaration at the application submission boundary,
certified tick frontiers, idle device loss/recovery and independent acceptance.
No exhaustion test executes 2^64 allocations. No final asset, human, performance,
full-suite or gate acceptance is implied. The implementation goal stays active;
no gate commit is made for this partial checkpoint.

Root reviewed the interface, ownership, teardown, identity and build-boundary
changes against the linked tests and design. This is implementation self-review,
not the outstanding independent acceptance. [The machine-readable record](summary.json)
binds final sources, binaries, diagnostic package, result counts and retained
attempts to this checkpoint.
