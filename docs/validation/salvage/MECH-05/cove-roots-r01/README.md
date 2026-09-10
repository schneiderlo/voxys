# Cove rigid-root preparation and browser terrain correction

This is preparatory work under decision D32 in `GAME_IMPLEMENTATION_TODO.md`.
The live cove still has one boat body and explicitly refuses cuts. No full
MECH-05, PLAY-05 or gate acceptance is claimed. No images were taken.

## Implemented preparation

`CoveBoatAssembly::compileFragments` prepares every canonical rigid root in
one owning build. Each root owns its exact collision shape, buoyancy cells,
propeller frame/output and helm binding. An explicit stable helm part selects
the control root; it need not be root zero. Original single-root compilation
and launch constraints remain enforced. Placement IDs and all parts survive.

`Root::water(body)` returns a description whose cell span borrows that immutable
root. It does not cache a span across vector moves or retain live body handles
in prepared content. Runtime initial boat/cargo admission and launch now use
this shared preparation. Thrust and steering preserve module settings.

Three new `CoveMovement` tests verify:

- Cutting off the lowest-ID pontoon leaves the selected helm on root one.
- Separating every part preserves its shape, flotation and module settings.
- The actual moving 1035 kg starter skiff can be replaced by two GPU bodies.
  Both children receive the parent's post-solve velocity field before the old
  body retires. Expected momentum includes normal gravity and damping in the
  four substeps (0.05 kg m/s tolerance). The detached pontoon settles in a
  20-second water run; the old body is dead, both children survive, no failure
  or overflow is reported, and the shape pool drains during cleanup.

The third test exercises the real GPU backend, not the live transaction owner.
Atomic rollback, live cutter controls and physical fragment recovery are still
required. The prepared roots alone do not make cutting playable.

## Browser collision defect and correction

The generator fell through the LEGO seabed in both the new browser package and
the byte-identical previous recovery-design package. The previous package's
historical passing evidence is unchanged; it failed when rerun here. Native
grounding and the native delivery journey passed.

Actual buffer observations confirmed the 420 kg cargo shape, six exterior
faces, 90-degree principal frame and negative-Y sector. The terrain and shape
uploads matched the packaged inputs. An isolated geometry-helper dispatch
returned stud contacts, but the full shipping `solve_static_contacts` dispatch
returned without publishing its contact cache or applying support.

The correction removes the extra 17-point function-local candidate array from
`authored_terrain_append`. It reads the existing 16-point reservoir in place
and treats the incoming point separately. Coincident-point merging, deepest
point retention, nearest-neighbor removal and tie ordering are preserved.
This fixes the reproduced full-pipeline failure. The exact browser/compiler
fault has not been established; the evidence does not prove a particular
driver defect or a measured scratch-memory threshold.

`physics_authored_terrain.wgsl` is canonical. Run
`python3 scripts/sync_authored_shapes.py` after changes and `--check` before
acceptance; `physics_ballistic.wgsl` embeds the canonical implementation.

The new `scripts/validate_authored_terrain_browser.mjs` runs the full shipping
entry point on a hardware WebGPU adapter. Its small format-1 fixture matches
the cargo dimensions, mass frame, world position and sector from the game.
Five full-solver cases assert actual supported-body/contact counts, completed cache
publication, finite motion, upward support, penetration correction, unaffected
inactive bodies, 129 reversed active IDs across workgroups, open space and
smooth-heightfield compatibility. Stud landing cases must fill and reduce the
reservoir. A sixth case separately requires retention of distant support
patches, the deepest point and the stronger duplicate; it rejects a trivial
“first 16 points only” replacement.

- `browser-terrain-before-fix-r01.json`: expected failure, supported count zero
  and no cache publication. The exact old shader is retained in `reproduction/`.
- `browser-terrain-regression-r03.json`: all six corrected cases pass on
  Chrome 152 / AMD RDNA 3. The 129-body case publishes 516 contacts.
- `checks/native-root-and-terrain-r01.log`: all 88 CMake cases pass (47 authored
  GPU shape/terrain cases and 41 cove/player cases).
- `native-delivery-r01/summary.json`: the current root preparation passes the
  native fresh build, hook, lift, haul, delivery, save, restart and sail-away
  journey (31 stages). This run preceded the shader correction; the 88-case
  native suite above includes the corrected shader.
- `native-delivery-r02/summary.json`: the corrected application also passes
  the complete native journey, with 34 recorded stages including durable
  delivery, process restart and sailing away from the secured load.
- `checks/wasm-build-r02.log` and `checks/cmake-build-r03.log`: corrected WASM
  package and native application/test targets build successfully.

## Full browser journey status

The corrected package keeps the generator on the stud caps at local Y about
-3.025, including after paid boat construction. The first two corrected
journeys stopped at dock walking, before boarding. The second retained the
exact failing state: the player ended 17.6 cm from a 14 cm waypoint. A slow
displayed frame consumes multiple walking ticks per key press, so the test
driver oscillated around this reachable point. Its waypoint radius is now
25 cm; actual boarding, helm access, hooking, delivery and save assertions are
unchanged. The driver also records the failing target and full state.

The subsequent browser run reached boarding, helm and hooking, then exposed
another driver assumption: walking steps advance faster than completed boat
physics on this displayed browser. Counting walking steps released the reel
key too soon. Helm controls now wait for completed physics ticks; walking
continues to count player steps. The old four-minute runner timeout was also
reached. `VOXY_SMOKE_TIMEOUT_MS` now permits an explicit bounded timeout.
The first tick-based run (`browser-recovery-r05`) completed only 18 physics
ticks in its 20-second input deadline, with the rope shortening by 0.5 m.
The driver now budgets up to two seconds per requested helm/physics tick plus
ten seconds, bounded by the overall runner deadline. This does not make the
game faster or change physics. Browser performance is unfinished.

`browser-recovery-r06` is terminal and failed after 12 recorded stages: the
actual lifting cable broke at `lift-7`. This is a gameplay/physics failure,
not an adapter or timeout failure. It retained the isolated browser profile
`/tmp/voxys-startup-nG21wk`; the browser process has exited.

The last recorded tick is 520 while water time is 52.4011 seconds. During
lifting, 50 completed physics ticks advanced water time by 5 seconds, about
six times the corresponding 60 Hz physical duration. The boat rocked and the
cargo moved below its original resting level before the break. This establishes
a timing mismatch and a failed lift, not yet its full causal explanation.
Next investigate shared water/physics epochs and loaded contact stability;
do not increase the break limit, move cargo directly or weaken delivery checks
just to obtain a passing journey. Fixed-tick water and performance gates remain
open. The current user preview remains the old package on port 38206.

Full browser delivery/recovery acceptance is **not passed**. Prepared cove
roots, the controlled native GPU split and the standing-cargo terrain correction
remain separately verified as described above.

## Reproduction

From the repository root, with Node 22+, Chrome and a hardware Vulkan adapter:

```sh
nix-shell --run 'TMPDIR=/tmp VOXY_TEST_CHROME=/usr/bin/google-chrome VOXY_SMOKE_GPU=gaming-x11 node scripts/validate_authored_terrain_browser.mjs /tmp/terrain-fixed.json'
nix-shell --run 'TMPDIR=/tmp VOXY_TEST_CHROME=/usr/bin/google-chrome VOXY_SMOKE_GPU=gaming-x11 node scripts/validate_authored_terrain_browser.mjs /tmp/terrain-before.json docs/validation/salvage/MECH-05/cove-roots-r01/reproduction/physics-ballistic-before-fix.wgsl'
nix-shell --run 'cmake --build build-native-save-host --target voxy_native gpu_authored_shapes_tests cove_player_tests -j8'
nix-shell --run 'ctest --test-dir build-native-save-host --output-on-failure -R "^(gpu_authored_shapes|cove_player)\."'
```

The second browser command is expected to fail on the reproduced affected
browser/adapter combination; another compiler may correctly execute the old
shader. Both reports hash the exact shader and identify the browser/adapter.
The tests do not claim cross-GPU bit-identical floating-point results.

The corrected app package is `build-cove-roots-efmhcp_j/web-r02`. It is built
with `EM_CONFIG=/tmp/voxys-emsdk/.emscripten`, a workspace TMPDIR and
`cmake --build build-lego-wasm --target voxy_wasm -j8`; copy the built
`voxy_wasm.{js,wasm,data}` alongside the current `web/` files. Do not mix files
from different builds. `web-package-r02.json` records package hashes.

For a fresh actual browser journey set `VOXY_SMOKE_NO_SCREENSHOT=1`,
`VOXY_SMOKE_TIMEOUT_MS=1800000`,
`VOXY_SMOKE_REPORT` to a new JSON path and `VOXY_SMOKE_COVE_RECOVERY_DESIGN`
to a new evidence directory, then run
`node scripts/smoke_integrated_wasm.mjs PACKAGE_DIRECTORY salvage-cove` with
the same Chrome/GPU environment. This uses shipped controls and real storage.

## Retained failures and limits

Early root tests had an overstrict accumulated float-volume tolerance and an
artificial drop that had not settled after six seconds. The final tests use
one ppm volume tolerance and the physical equilibrium start plus 20 seconds.
Their earlier logs remain in `checks/`.

Exploratory `browser-static-solve-probe-*` and `browser-fixed-contact-probe-*`
reports use an older diagnostic harness: their `status: passed` means command
completion/readback only, **not correct contacts or physics acceptance**.
Several of those completed with zero support. The new terrain regression
explicitly rejects that outcome. Earlier headless attempts also failed adapter
admission. No failed result is overwritten or counted as passing acceptance.

Changing workgroup size, removing max-mip culling, changing result passing,
avoiding early returns and unrolling duplicate search did not fix the full
dispatch. Dropping reservoir replacement restored support but discarded its
spatial selection behavior, so that experiment was not shipped.

## Required next integration

1. Reserve/upload every root and admit/configure every dynamic body before
   retiring the parent. Handle all failures before logical publication; an
   incomplete replacement must not lose parts or leave a partial live build.
2. Use certified post-solve parent motion from `AssemblyFracturePlan`; await
   old-body retirement and every child observation before atomic publication.
3. Map rendered parts, deck/rider membership, helm, winch and harbor endpoints
   through stable part-to-root bindings. Unsupported situations must refuse
   before mutation. Skip the water driver for zero-buoyancy roots.
4. Extend physical SVCE saves beyond v3's single boat motion to an exact root
   set. Preserve old v1-v3 bytes/readers and reject missing or duplicate roots.
5. Protect the last intact recovery design before cutting. Rescue/rebuild
   must retire every old loan fragment and recover each paid part once,
   including remote fragments. Never clone a paid part or essential cargo.
6. Add real native/browser cut, reload, remote-fragment rescue and repeated
   rebuild journeys. Enable cutter controls only after all owners support the
   transition. Update plan checkboxes truthfully; commit only at a passed gate.
