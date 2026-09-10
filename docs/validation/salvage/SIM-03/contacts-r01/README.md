# Authored hull queries and contacts

2026-09-08. SIM-03 preparation under D18/D20. **The cove boat is still
stationary.** This checkpoint connects compiled geometry to the shipping query
and narrow-phase pipelines; it does not enable authored body spawning.

## Implemented

- Rays, sphere overlaps, sphere sweeps and capsule sweeps traverse the authored
  BVH and clipped exterior rectangles. The empty space beside an appendage
  remains empty. Whole-segment distance minimization detects thin rails between
  the legacy capsule sampling positions. Flat minima use an interval midpoint
  to avoid a spurious edge normal from floating-point cancellation.
- Sphere/capsule contacts use that exterior. Box/compound contacts traverse
  child geometry and clip contact polygons to exposed patches. Solver anchors
  return to each parent COM/principal frame. Exterior face IDs retain source
  identity through the same immutable shape revision.
- An actual narrow-phase → dynamic-solver submission moves and turns the
  complete asymmetric body when a projectile hits its appendage. The test
  checks linear momentum and non-increasing kinetic energy.
- Narrow-phase class counts/offsets move from storage to a 128-byte GPU-copied
  uniform during collision dispatch. The atlas fits within eight storage
  bindings. A 32-byte empty atlas preserves primitive callers; queries also
  own a 32-byte fallback. Bind-group caching includes atlas identity.
- Contact reduction explicitly guards sentinel/underflow array indices. This
  fixes lost compound manifolds exposed by the rotated sector-boundary case.
  Box face clipping now walks the polygon perimeter instead of crossing it.
- `sync_authored_shapes.py` embeds shared geometry into both shipping shaders.
  Both existing native/browser CI geometry checks now verify synchronization.
- The compute runner takes **no screenshots** and uses the existing project's
  native-Vulkan flags in a headless hardware browser. It rejects software
  adapters, skipped cases and zero-test success. Focused suites are selectable.

## Results

[Native log](native.log): native application build, 12 new query/contact cases,
8 existing query/readback cases, and all 4 narrow-phase cases passed. The latter
include the existing Box3D differential test and all three workgroup sizes.

[Browser result](browser.json) and [test output](browser-tests.log): the same
12 new cases passed on AMD RDNA 3 hardware WebGPU, with zero GPU errors,
exceptions, skipped cases or screenshots. The tests consume actual readbacks;
this is numeric acceptance, not byte-parity or rendered-game evidence.

[Browser application build](wasm-app-build.log) passed. The existing GLM
defaulted-comparison warning remains. [Build record](web-build.json) retains
the browser test compiler inputs and artifacts; [summary](summary.json)
identifies this checkpoint's sources and logs.

## Remaining integration — required before sailing

1. Typed body admission must derive a conservative **COM-centered** broad-phase
   bound from the authored mass/geometry. Test fixtures use the packed COM
   radius; their generic dimensions are never used as narrow-phase geometry.
2. Complete authored terrain contacts, CCD, render/root mapping, body/revision
   ownership and automatic shape-use declarations at the application's actual
   submission boundary. Retain provenance until all readers finish and clear
   contact history on incompatible revisions. Current fixtures declare shape
   use through `IAuthoredShapeResources`; the application does not yet do so.
3. Current box/compound support covers shallow exterior contacts. A deeply
   embedded box can select a buried cell face and produce no exterior patch;
   recovery/containment handling is still needed. The legacy one-normal,
   16-candidate/four-point reduction also needs explicit multi-patch and overflow
   treatment. Cylinder/LEGO combinations reuse the existing polygonal path and
   are not newly certified by this checkpoint. Do not enable general authored
   bodies or claim SIM-03/SIM-09 acceptance from these tests.
4. Complete tick-specific buoyancy/water, engine force placement, steering,
   moving-deck/helm integration and the physical cove journey. No sailing,
   cargo gameplay, performance, visual, independent-review or gate pass is
   claimed here.

Missing/retired/malformed atlas references produce query status bit 2 or an
invalid-manifold count, rather than primitive fallback. Query status bit 1 is
hit capacity; bit 4 is cast nonconvergence. Any nonzero query status is
incomplete. The existing backend API currently aggregates these into its
`overflow` flag; keep that conservative behavior until richer status is exposed.

## Reproduce

From the repository root:

```sh
python3 scripts/sync_authored_shapes.py --check
python3 scripts/sync_lego_surface.py --check
nix-shell --run 'bazel build -c opt //tests:gpu_authored_shapes //tests:gpu_queries //tests:gpu_narrow_phase //:voxy_native'
nix-shell --run './bazel-bin/tests/gpu_authored_shapes --gtest_filter=GpuAuthoredShapes.Query*:GpuAuthoredShapes.Contact*'
nix-shell --run './bazel-bin/tests/gpu_queries && ./bazel-bin/tests/gpu_narrow_phase'
python3 scripts/build_shape_diagnostics.py --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node --output /tmp/new-compound-web
VOXY_SHAPE_SUITE=consumers node scripts/run_shape_diagnostics.mjs /tmp/new-compound-web /tmp/new-compound-results
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
```

The SDK/build directories are local prerequisites, not committed deliverables.
Do not reuse an existing output directory for a new evidence run.

The retained `attempts/` logs include the initial Nix sandbox refusal, C++
span/test warning corrections, missing shader constant, missing test readback
usage, thin-rail precision and compound reduction failures. Browser attempt 1
closed on the old Wayland route; attempt 2 lacked a hardware adapter. The final
headless native-Vulkan launch passed without relaxing the hardware requirement.
