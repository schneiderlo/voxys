# Hull–seabed contacts and cove boat preparation

2026-09-08, after G00 on `codex/salvage-implementation`. **The playable boat
is still stationary.** This is a scoped SIM-03 checkpoint, not a gate pass.

## Changes

`shaders/physics_authored_terrain.wgsl` clips the compiled hull's exposed faces
against the actual two terrain triangles in each overlapping heightfield cell.
It detects an obstacle under the middle of a face, even when its corners are
clear. Empty space between pontoons stays empty. Root geometry is reconstructed
from the body's COM and principal orientation in the terrain's sector frame.

The shipping `solve_static_contacts` entry point consumes these contacts. Max
height mips reject empty work; each body has an 8,192-cell work limit. A bounded
16-point reservoir preserves the deepest point and spatial coverage before the
existing four-contact solver reduction. The solver applies linear/angular
impulses and positional correction. Face ordinals retain part provenance, but
are not unique terrain anchors: authored warm starting is deliberately disabled
until an appropriate anchor cache exists.

Invalid geometry, exceeded cell work and unsupported LEGO terrain return explicit
status, discard partial contacts, zero motion and increment failure telemetry.
Core telemetry now occupies 32 words. The new static-contact layout uses eight
storage bindings, including the world-owned shape atlas. Query/narrow-phase
inputs also refresh that atlas when its allocation changes. This does **not**
yet supply automatic live-body submission ownership or completed-tick failure
handling.

The cove registry had copied all eleven skiff models but omitted their seventeen
welds. Schema 4 and `compose_cove.py` now preserve those connections alongside
navigation. Placements, meshes, materials and navigation coordinates are unchanged.
`src/game/expedition/cove_boat.*` selects the boat independently of the dock,
validates its build, compiles one mass/collision/flotation/function assembly,
prepares its GPU hull and retains the placement-to-part mapping. The application
owns that preparation with the scene. Disconnected boats are rejected.

Actual cove preparation: **11 parts, 17 welds, 1,035 kg dry mass and
9.884032 m³ maximum displacement**. Displacement is geometric capacity, not
evidence of live buoyancy. These temporary scene IDs do not enter campaign
inventory. Runtime status explicitly reports `boat.prepared=true` and
`boat.active=false`; there is still no authored boat body in the live world.

## Checks performed

- Four new native GPU cases pass: a central seabed peak, the pontoon gap,
  rotated COM/principal frames in distant sectors, and explicit invalid/LEGO/
  work-limit failure. See `terrain-native.log`.
- The same four cases pass in headless hardware Chrome on AMD RDNA 3. Six
  256-byte numeric readbacks, zero GPU errors, exceptions or skipped tests.
  See `terrain-browser.json`, `terrain-browser-tests.log` and `web-build.json`.
- Three existing backend terrain/water/resource-lifetime cases and the backend
  shape-submission case pass. See `backend-native.log`.
- Nine cove-player/boat cases and nine registry cases pass in Bazel. These
  include compiling the actual boat, restoring all part transforms, checking
  mass and source mappings, and rejecting disconnected geometry. The registry
  test now declares its cove assets and resolves trusted test runfiles before
  calling the production no-follow reader. See the cove logs.
- Native and ordinary WASM application builds pass. The existing GLM defaulted
  comparison warning remains in the WASM build log.
- One headless startup of the ordinary cove application passes. It loads the
  prepared boat, reaches the walking state, and reports no browser GPU errors.
  See `cove-startup.json` and its 22-file package identity. This was a startup
  check, not another full dock journey or a performance/visual gate.
- Both shared-shader synchronization checks and scoped whitespace checks pass.
  **No screenshots were captured or viewed.**

## Continue from here

1. Connect typed authored-body admission, conservative COM bounds and automatic
   shape/revision use tracking to actual application queue submission. Bind the
   existing `CoveBoatAssembly` rather than recompiling unrelated scenery or
   substituting a primitive box for the hull.
2. Complete required CCD and containment/multiple-patch behavior; propagate
   incomplete terrain ticks to the world owner. The old `integrate_bodies`
   prototype entry point still has primitive terrain/water logic. Use the
   production split stages; do not advertise that legacy entry as authored support.
3. Drive rendered part transforms from the same root/body pose. Preserve the
   compiled revision and collision-source mapping until all consumers finish.
4. Feed compiled, non-overlapping displacement regions into buoyancy and
   water-relative drag, and use compiled propulsion/helm frames for forces.
   Attach helm/player movement to the moving boat. The next playable milestone
   remains boarding, steering and sailing this actual skiff.

SIM-02/SIM-03, review and all product gates remain open. No gate commit or full
gate-hook run was made for this checkpoint. Continue following owner decision
D20: gameplay implementation takes priority over diagnostic or visual expansion.

## Reproduce

From the repository root, with the existing Nix/EMSDK environment:

```sh
python3 scripts/sync_authored_shapes.py --check
python3 scripts/sync_lego_surface.py --check
nix-shell --run 'bazel build -c opt //tests:gpu_authored_shapes //:voxy_native'
nix-shell --run './bazel-bin/tests/gpu_authored_shapes --gtest_filter=GpuAuthoredShapes.Terrain*'
nix-shell --run 'bazel test -c opt //tests:cove_player //tests:fixture_registry --test_output=errors'
python3 scripts/build_shape_diagnostics.py --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node --output /tmp/new-terrain-web
VOXY_SHAPE_SUITE=terrain node scripts/run_shape_diagnostics.mjs /tmp/new-terrain-web /tmp/new-terrain-results
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/new-cove-startup.json node scripts/smoke_integrated_wasm.mjs /path/to/normal-web-package salvage-cove
```

Retained failures include a WGSL reserved identifier, two C++ warning fixes,
the missing weld data, and missing Bazel fixture inputs. A temporary build-cache
quota exhaustion was resolved by removing disposable compiler scratch caches;
the SDK, built packages, repository assets and evidence were retained.
