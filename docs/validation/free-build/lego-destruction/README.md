# Cannon and GPU destruction — D3 candidate, 2026-09-18

The authentic LDraw cannon aims and fires in the existing GPU world. Confirmed
hits now reach the source-aware wall owner and release nearby original pieces.
The actual-world test now proves a local brick knock-out: eight parts release,
the struck piece moves 2.64 studs inward/down, and debris settles. Nearby pieces
mostly remain supported. Browser acceptance and a walk-through opening remain
open. No full D1–D3 gate is complete.

Read [current D3 evidence and remaining work](d3-r01/README.md) and the
[self-contained checklist](../../../design/free-build/lego-destruction/IMPLEMENTATION_TODO.md).
[D2 evidence](d2-r01/README.md) refers to the earlier 39-part upper-facade package,
not the current 20-part ground-wall selection.

## Play the current candidate

1. Load Free Build. Select **Visit cannon** to travel to a checked clear landing
   beside the cannon, or walk within 12 studs and press **C**.
2. **A/D** turns, **W/S** changes elevation, **Space** or **Fire** shoots.
   The default aim targets the stone wall beside the Blacksmith.
3. Wait for the shot and any released pieces to settle. **C / Leave cannon** is
   unavailable while either is pending. Fire also waits for the previous shot.
4. **Rebuild wall** restores the original source assembly. Damage is session-only;
   changed masonry refuses saving until rebuilt.

The cannon is at X/Z `(1240, -1027)`. Its fixed heading is `-pi/2`, initial
relative yaw `-.0360332748563`, and elevation `.0726767584712` radians. The cannon
and selected house still respect saved-construction conflict suppression.
Manual **Release wall** and **Remove support brick** are internal test actions;
they are no longer presented as normal player controls.

The base is anchored and barrel recoil is cosmetic. Spheres use radius .44,
mass 2 game units, speed 48 studs/s, the existing gravity/contact/CCD solver,
a one-second reload and 2.5-second simulation lifetime. Storage retains eight
shot slots, but the current interaction allows only one outstanding shot. Menus
pause physics and shot age. Save reload retires outstanding projectiles.

## Current implementation and limits

- Cannon source and articulation remain the original LDraw base/barrel, with
  2,368 triangles and 182,911 cooked bytes. See its
  [asset record](../../../../data/adventure/ldraw-cannon-r01/README.md).
- The active house package is
  [`ldraw-blacksmith-ground-r01`](../../../../data/adventure/ldraw-blacksmith-ground-r01/README.md).
  It preserves all 2,140 source instances and exposes 20 original ground-wall
  parts: 18 eligible pieces in one connected component and two held plates with
  unsupported external hardware. The graph has 30 internal and 44 boundary bonds.
  Eleven mesh nodes retain authentic part geometry. The fixed remainder uses
  6,409 boxes; this is not whole-house breakable topology.
- Confirmed `ContactHit` records resolve the struck original part through the
  accepted root's authored face. Sparse manifold-history indexing previously
  dropped some real hits; event packing now reads dense solved contacts, with a
  regression reproducing the missed second hit.
- The interim impact response consumes the ball while atomically replacing
  selected roots. It applies a delayed, dissipative, capped impulse. It is **not**
  the planned fully coupled static-to-dynamic collision or an exact conservation
  claim. Source identities, render membership and collision ownership are kept.
- Movement is held during changing wall geometry. Certified settled poses become
  static query geometry; rebuilding restores original collision and saveability.
  This does not prove walking or riding through the current damaged section.
- Moving-target CCD, sequential impacts within one tick, full-house/roof collapse,
  durable damage and final native/browser acceptance remain unfinished.
- Full Application shutdown destroys the runtime, then its borrowed physics world,
  with no intervening simulation. A future scene switch reusing that world must
  explicitly drain authored resources first.

## Historical D1 verification — 2026-09-17

The following table records the earlier static-house implementation. Its 4,054-box /
32-body house compiler and 72-body full scene are historical, not measurements of
the current ground-wall package. Hardware GPU checks ran on AMD Radeon 890M /
Vulkan. Logs are in `d1-r01/`.

| Check | Result |
| --- | --- |
| Source dependency and byte validation | Pass, 27 external dependencies |
| Articulation/runtime asset validation | Pass, two identity meshes, source hinge/muzzle |
| Cannon math and collision compilation | 6 tests passed |
| GPU scene admission/replacement/refusal and shot lifecycle | 3 tests passed |
| Entire authored-shape GPU suite | 55 tests passed, including 8 cannon CCD regressions |
| Existing solver/friction/sleep/wake/terrain/buoyancy/attachment/island selection | 28 tests passed |
| Compiled browser UI tests | 6 tests passed |
| Installed full-world startup/build/movement/save integration | Passed, 24.9 s; synthetic input, headless actual runtime |
| Full-world cannon input + certified GPU submission | Passed, 21.3 s; 72 static bodies, 25 physics ticks, zero GPU errors |
| CMake WASM application build | Passed |
| Required aggregate repository gate | 2,513 passed, 16 skipped, 6 existing salvage fixture failures; terrain importer passed |
| Browser startup + distant Cannon button | Passed; correct walk-near hint; refreshed build reports no warning/error entries; no claim of complete firing journey |

The cannon input test exercises C enter, A/D and W/S aiming without player
movement, Space firing and cooldown, pause stopping ticks and firing, C leave,
then B and valid brick placement. It uses a save/restore setup beside the cannon,
not a claim that the test walked there. The complete nearby scene has 72 static
bodies; the house-only compiler measurement above is 32.

The installed-world test was run directly from the built native test executable
with `VOXY_ADVENTURE_TEST_TERRAIN` and `VOXY_ADVENTURE_TEST_WORKSPACE`, because
Bazel's file sandbox could not read the external decoded terrain fixture. That
initial sandbox failure is not a gameplay failure. This check uses the installed
8,192-square heightmap; it does not fabricate a flat test world.

## Historical D1 independent review

Asset/runtime reviewer `cannon_asset` and GPU reviewer feedback identified and
prompted fixes for: raised-barrel ground penetration; cannon-camera height and
collision; menu actions bypassing cannon ownership; retrying refused body
retirement without losing handles; retained preparation failure reasons; a
quadratic body scan; later collisions after separating from another face of the
same compound; slow-sphere/zero-margin contact; and explicit teardown ownership.

Cannon/scene reviewer `cannon_scene_physics` verified complete admission,
rollback, replacement, lifecycle and resource budgets independently. The GPU
agent reran the complete authored suite and existing relevant regressions after
review fixes. These results do not waive open full D1 items or the normal
repository hook. The required aggregate run reproduced existing salvage
fixture failures in memory-accounting constants and draw-count expectations
(`tests/test_salvage_asset_fixture.cpp`); see `d1-r01/normal-gate-failures.log`.
The completed aggregate run took 1,838 seconds: 2,513 passed, 16 skipped and
6 failed. The two opt-in full-world cases skipped in that sandboxed aggregate
were separately executed and passed above. The terrain importer target passed.
That recorded gate was not green. Its six salvage fixture expectations were later
repaired and 33 focused fixture tests passed; a fresh complete gate is still
required. No destruction gate commit or push is claimed here.

Previous source/import-only evidence is retained in
`d1-r01/previous-import-evidence.md`; its merged-asset byte count is historical.
Current asset rebuild/provenance lives in
[`data/adventure/ldraw-cannon-r01/README.md`](../../../../data/adventure/ldraw-cannon-r01/README.md).

## Reproduce checks

From the repository root in its Nix shell:

```sh
python3 data/adventure/ldraw-cannon-r01/source/validate.py
python3 data/adventure/ldraw-cannon-r01/validate_runtime.py
bazel test //tests:cannon_physics_scene //tests:cannon_physics_scene_gpu
bazel test //tests:gpu_authored_shapes
npm --prefix ui test
cmake --build build-lego-wasm --target voxy_wasm -j3
bazel build //tests:adventure_runtime
```

For the installed-world component checks, set `VOXY_ADVENTURE_TEST_WORKSPACE` to
the absolute repository root and `VOXY_ADVENTURE_TEST_TERRAIN` to the decoded
`td_seed_1234_8192.r16` (134,217,728 bytes), then run
`bazel-bin/tests/adventure_runtime --gtest_filter='FreeBuildRuntimeIntegration.*:SourceHouse/ImportedWallRuntimeIntegration.*'`.
Without that explicit terrain opt-in these expensive installed-world tests skip.
The recorded run used `/tmp/voxys-daynight-native/bin/data/generated/td_seed_1234_8192.r16`.
The ordinary repository gate remains
`bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`.
