# First physical sailing milestone

2026-09-09. Scoped implementation after G00 on `codex/salvage-implementation`.
**The authored skiff now floats, drives and steers. No screenshots were captured
or viewed. Parent tasks and gates remain open.**

## Play

Run the native application with `--config salvage_cove.cfg`, or open the normal
browser package at `?experience=salvage-cove`. The local delivered preview is
`http://127.0.0.1:38190/?experience=salvage-cove` while its server is running.
Walk with WASD; Space jumps. Approach the boat and press E to board. Approach
the helm and press E (or the browser interaction button). W/S controls throttle,
A/D steers, E leaves the helm. Return to the dock with E while alongside, or
use R to return both player and boat to the starting berth. Browser Leave waits
for owned scene resources before navigating back to LEGO World.

## Implementation and ownership

- `src/physics/authored_body_frame.*`, PhysicsWorld and GpuPhysicsBackend provide
  typed authored-body admission. The actual compiled shape supplies mass, COM,
  principal inertia and exterior geometry. The broadphase box is only a bound;
  it is hidden from primitive rendering and supplies no substitute flotation.
- The backend retains body shape handles and prepares their uses before physics
  and rendering encode. `submitGpuSubmission` performs the actual shared queue
  submission. Mutation while a ticket is open is refused. Discard after physics
  encoding fail-stops because host scheduling already advanced. A live body's
  shape must remain Ready: destroy and submit the body first, then retire its
  shape after the dead-body observation. Application Leave follows that order.
- `configureAuthoredWaterBody` owns up to 2,048 disjoint root-local displacement
  cells for one live craft. `apply_authored_water` in `physics_ballistic.wgsl`
  divides each box into six tetrahedra, clips against a local sampled water
  tangent plane, and accumulates submerged volume and COM-relative volume
  moment. Those produce vertical buoyancy and restoring torque. The actual
  compiled propeller point/direction, maximum thrust and helm angle produce
  steered force-at-point torque. Implicit drag damps velocity; angular damping
  scales with displaced water mass rather than maximum sealed capacity.
- MeshPath reconstructs the root directly from generation-checked live GPU
  body/shape buffers, then applies exact part/node transforms. Its existing
  96-byte instance record carries body index/generation in former padding.
  Stale bodies are hidden. A bounded one-body observation moves the player's
  deck frame and camera; readbacks never drive buoyancy or thrust.
- `CoveBoatAssembly::equilibriumRootHeight` solves the actual dry displacement
  volume by bisection. Spawn/reset adds the named CPU wave approximation only
  for initial placement. The old inspection pose displaced several times the
  empty craft's weight and launched it upward. The cove now uses triangle
  heightfield terrain, preserving its original resolution. `applySalvageBerth`
  lowers only submerged shallow terrain in the starter berth, leaving dry shore
  and distant sea unchanged, so the outboard has clearance.
- The actual craft has 11 parts, 17 welds, 1,035 kg dry mass and 9.884032 m³ maximum
  displacement. Its transient private scene IDs are not campaign inventory.
  Existing scenery assets, meshes and materials are reused.

## Verified results

`native-tests.log`: all 15 selected tests pass (3 live GPU body/water tests,
10 cove movement/assembly tests, navigation rejection and berth geometry).
The actual empty hull settles in calm water after 360 fixed ticks: root height
0.320451 m, up-vector Y 0.999935, vertical speed below 0.000001 m/s, angular speed
below 0.000003 rad/s. This does not establish loaded or rough-sea stability.

`browser-sailing.json`: actual hardware Chrome 152 / Radeon 890M journey passes.
Real keyboard/button input boards, reaches the helm, throttles and steers, then
leaves the helm, recovers with R, resizes and drains Leave. Net horizontal
movement from helm entry to the steering observation is 19.095 m; speed at that
observation is 4.949 m/s. Boat heading changes and the player follows the deck.
The reset observation initially predates the queued boat teleport; the following
10-tick observation confirms the boat back at its starting berth. Empty economy
state remains unchanged. No browser exceptions or GPU validation errors occur.
This is a short functional journey, not a performance or visual acceptance test.

`mesh-pose-native.log` proves GPU root transforms against an independent matrix,
pose updates and stale-generation rejection using numeric offscreen readbacks.
No image files are produced. Thirteen existing fixture GPU tests also pass.
The two existing release/HDR-depth checks passed alongside the initial failed
mesh test; that failure was a test upload overload receiving a fixed-size span
object rather than its bytes. Explicit dynamic spans fixed it, and the new test
then passed. The actual-hull test first failed to obtain its asynchronous debug
readback; polling completions correctly fixed the harness without loosening its
physical assertions. Retained failure logs distinguish these outcomes.

Final native and WASM builds pass; shared authored/LEGO shader synchronization
and whitespace checks pass. The final delivered browser package is byte-for-byte
identical to the one used for the successful sailing journey (22 files; see
`package-sha256.json`). No unchanged journey or screenshot matrix was repeated.
The mesh diagnostic page's future expected count is updated, but that image
workflow was not run.

## Remaining work and next agent

1. Add physical dock/scenery obstacles for the boat. Current dock/generator
   collision is for the CPU player only; the GPU boat can pass through them.
2. Continue towing/salvage through the plan's existing SIM-04–08 scheduling,
   WaterField and transactional ownership contracts. Do not spend inventory or
   claim campaign progress from the current private scene assembly.
3. Complete authored CCD, deep containment and multiple contact patches, shared
   per-tick water epochs, water-relative velocity/drag, power and fuel networks,
   flooding, loaded handling, winch/cargo mechanics and revision recovery.
4. Complete ACT-03: the current player uses a kinematic moving deck frame;
   inherited jump velocity, impacts, camera collision and arbitrary moving-deck
   transitions remain unfinished. GPU mesh pose is current, but CPU LOD selection
   still uses the original static scene root.

The water driver handles one intact craft, one propeller and one helm. It uses
still-water drag and the render-produced spectral texture across catch-up ticks.
It does not implement authoritative tick-specific ocean velocity. Its GPU cost
is currently charged to the commands/compaction timing segment. No complete
simulation, visual, independent-review, parent-task or gate acceptance is claimed.
No full gate hook or gate commit was run. G00 remains the last passed gate.
Follow owner decision D20: advance playable behavior; stop screenshot loops.

## Reproduce the focused checks

From the repository root with the configured dependencies:

```sh
nix-shell --run 'bazel build -c opt //tests:voxy_tests //tests:mesh_path_test //:voxy_native'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter=GpuAuthoredShapes.Live*:CoveMovement.*:CoveNavigation.*:AuthoredCoveTest.StarterBerthProvidesDraftWithoutMovingDryShoreOrDistantSea'
nix-shell --run 'bazel-bin/tests/mesh_path_test --gtest_filter=MeshPathGPUTest.AuthoredPoseRendersRootFrameAndRejectsStaleBodyGeneration'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
```

Package `web/` plus `build-lego-wasm/bin/voxy_wasm.{js,wasm,data}` into one folder.
Only when a material gameplay change needs rechecking, run the keyboard journey:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/cove-sailing.json VOXY_SMOKE_COVE_PLAYER=/tmp/cove-sailing-journey node scripts/smoke_integrated_wasm.mjs /path/to/package salvage-cove
```
