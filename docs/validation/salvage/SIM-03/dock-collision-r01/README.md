# Physical cove dock collision

2026-09-09. Scoped SIM-03 continuation of the first sailing milestone. No
screenshots, visual acceptance, completed parent task or gate is claimed.

The authored dock and generator now collide with the actual skiff. Previously
only the CPU player collided with these parts, so the boat could pass through.

## Implementation

`CoveSceneryCollision` in `cove_boat.*` collects the thirteen non-boat placements'
actual collision proxies. It compiles their exact exterior union, clipping
internal seams and retaining placement/proxy source labels. A local anchor keeps
coordinates small. This fixed scene union is not a welded machine and contributes
no mass or buoyancy to the boat. Its immutable shape has a unit reference mass;
static body admission zeros effective inverse mass and inertia.

`AuthoredBodySpawnDesc::motionType` explicitly distinguishes dynamic and static
admission. Static shapes retain the same exact geometry and lifetime guarantees
as the boat. Initial nonzero velocity and water drivers are refused; later pose,
velocity and kinematic-target commands are refused. Impulses cannot move scenery.
Destroyed/reused slots clear the fixed-state ledger so a subsequent dynamic body
still receives its prepared inertia. Material flags do not encode motion type.

Application admits the fixed shape before the skiff, tracks both body handles,
and destroys both on Leave. Each shape is retired only after its dead-body
observation; rendering/submission ownership continues until both retire. The
existing static models keep their authored poses. Public diagnostics report
`boat.sceneryCollision` and `boat.sceneryProxies`.

The first browser attempt exposed an existing observation backlog: requesting a
boat/deck snapshot every render frame filled the readback ring before callbacks
arrived. The application now permits one pending observation packet, reusing the
last completed pose for the player while the GPU continues simulation. Checked
physics submission still fail-stops on actual encoding failures. This is bounded
presentation observation, not complete SIM-04 frontier or stall-timeout handling.

## Evidence

- `native-tests.log`: sixteen focused tests pass. Two identical authored skiffs
  start at 2 m/s for 180 ticks with gravity/water disabled to isolate collision.
  The skiff aimed into the actual dock stops/rebounds to root X −0.700017 m;
  the skiff in clear water advances to X 5.09801 m. The dock remains fixed,
  two contact constraints are generated and no contact-capacity overflow occurs.
  Source mappings exclude every boat placement. Static admission, rejected
  movement, immunity to force, resource retirement and dynamic slot reuse pass.
  All existing selected live-body/water/cove/navigation tests also pass, including
  actual-hull calm-water stability. This is not high-speed CCD acceptance.
- `browser.json`: hardware Chrome 152 / Radeon 890M passes actual keyboard/button
  boarding, helm use, throttle, steering, moving deck, R recovery, resize and
  drained Leave. All stages report active physical scenery with thirteen source
  proxies. Recovery now explicitly waits for the boat's completed pose at its
  berth, rather than accepting only the player's reset. The sailing observation
  ends near (−8.95, 1.71, −71.19), then recovery observes (−0.499, 1.16, −54.003).
  No browser exceptions or GPU validation errors occur. The retained earlier
  failed browser report documents the readback-ring crash and its correction.
- Both final application builds pass. Shared shader synchronization and
  whitespace checks pass. The delivered package hashes are recorded. No
  unchanged screenshot or image-comparison workflow was run.

## Continue

Proceed toward towing and salvage recovery through SIM-04–08 and the existing
GameSession contracts. The current skiff remains a private scene assembly, so
it must not award inventory or claim a campaign job through diagnostic state.
The fixed dock remains a deliberately bounded scene union; large-world scenery
partitioning, authored CCD/deep containment/multiple-patch completeness,
water epochs/flow, power/fuel/flooding and full character dynamics remain open.
G00 remains the last passed gate. No gate commit or full gate hook was run here.

Reproduce from the repository root with configured dependencies:

```sh
nix-shell --run 'bazel build -c opt //tests:voxy_tests //:voxy_native'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter=GpuAuthoredShapes.Live*:CoveMovement.*:CoveNavigation.*'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8'
```

Package `web/` and the three built `voxy_wasm.{js,wasm,data}` files into one folder.
Only repeat the affected control journey after a relevant change:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/cove-dock.json VOXY_SMOKE_COVE_PLAYER=/tmp/cove-dock-journey node scripts/smoke_integrated_wasm.mjs /path/to/package salvage-cove
```
