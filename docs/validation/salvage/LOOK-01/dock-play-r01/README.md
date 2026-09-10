# Playable dock and moored boat — 2026-09-08

The `salvage-cove` route now starts on the authored dock. It supports walking,
jumping, collision with the generator, nearby boarding, walking on the boat,
using/leaving the helm, returning to the dock, surface swimming and R recovery.
This is a stationary first-person milestone. No sailing or mission success is
simulated by these interactions. LOOK-01 and all later gates remain open.

## Play

Native: `nix-shell --run 'bazel run -c opt //:voxy_native -- --config salvage_cove.cfg'`.
Browser: build the ordinary WASM application and open `?experience=salvage-cove`.

- WASD/arrows: camera-relative walking; click the scene to look.
- Space: jump. Walk beside the generator toward the boat.
- E: board when nearby; use/leave the helm; return at the boarding point.
- R: return to the safe dock spawn. Escape releases the mouse.
- Browser interaction buttons invoke the same C++ action as E.
- Native contextual prompts are currently in the window title and console.

## Implementation and boundaries

`src/game/expedition/cove_player.*` consumes the admitted authored collision
boxes through the exact placement/basis transforms. The current scene has 54
collision boxes; admission caps the collection at 1,024. It does not use visual
bounds or decorative studs as collision and performs no whole-world GPU readback.
Collision uses a conservative upright box, radius .30 m and height 1.70 m,
axis sweeps, .005 m skin and .36 m steps. This is not a production capsule or
certification of cliffs, general slopes, moving decks or camera collision.

The local controller advances in 1/60 s steps, with a .25 s catch-up bound.
Walking is 3.6 m/s; surface swimming is 2 m/s at feet Y=-.8 relative to the water
datum. Eye height is 1.55 m. The terrain callback samples the actual loaded cove.
Water-wave coupling and synchronization with future authoritative player ticks
remain ACT/SIM integration work. No GameSession inventory/build/cargo state is
changed by this presentation milestone.

Registry schema 3 adds `navigation`: spawn, dock and boat boarding feet, helm
standing feet, look target, and unique boat placement ordinals. Coordinates are
metres in the same local frame as the rendered parts; the cove's actual origin
is (-19, -200, -37). Collision and clear standing room validate every action
destination before initialization. The boarding gap is constrained to .5–3 m;
the player must be within .9 m of its endpoint and grounded. Interactions are
queued, rechecked at a fixed tick, and cleared on Reset. Landing on authored
boat collision determines deck membership even when the player jumps aboard.
The helm explicitly remains moored. Shader/mesh inspection routes keep their
existing controls; only a water-anchored registry with navigation uses this path.

`Application` owns the controller alongside the cove fixture. It stops player
updates during preparation, Reset, Leave or loss; only an active fixture can
accept interactions. Camera position derives from local feet through the existing
sector conversion. Browser UI displays short nearby-action prompts. The new
code is registered in Bazel, CMake and the combined test source collections.

## Executed checks

All reports below refer to the working branch `codex/salvage-implementation`
after G00 commit `7f28fabfabf3d63726f6cfa1001c3ce1ed557911`; source hashes and
served-file identities are recorded in `summary.json` and `package-final.json`.

- Native application build passed. Seven focused player cases passed, covering
  the complete board/helm/return route, seams, solid obstruction, large-frame
  tunneling, jump/landing, jumping aboard/off, swimming/recovery, render-rate
  independence, short-frame jump retention and malformed navigation.
- The final `bazel test -c opt //tests:cove_player` also passed all seven cases
  against declared packaged inputs. Its test-only byte provider follows trusted
  Bazel runfile links while retaining content-hash admission; production
  no-follow directory loading remains unchanged.
- Nine existing fixture-registry cases passed, including old inspection and
  assembly admission. Eight browser UI lifecycle cases passed.
- CMake WASM application build passed. Its existing GLM comparison warning is
  retained in `wasm-build.log`; it was not suppressed. Native CMake execution
  and the full gate hook suites were not run for this scoped change.
- `browser-final.json` and `browser-final/summary.json` passed using the normal
  application package, real keyboard/mouse events and readable state only.
  Nine recorded stages cover spawn, jump/land, approach, boarding, helm use,
  return, swimming, R recovery and resize. Leave then drained and navigated to
  the default LEGO route. Four accepted interactions occurred, with unchanged
  asset generation/uploads and no inventory or cargo creation. No browser GPU
  errors occurred. No screenshot was captured or reviewed during this work.

Reproduce focused native checks:

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:cove_player //tests:fixture_registry'
nix-shell --run './bazel-bin/tests/cove_player && ./bazel-bin/tests/fixture_registry'
node scripts/test_salvage_preview.mjs
```

For a staged normal browser package, run:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_COVE_PLAYER=/tmp/cove-gameplay-check \
VOXY_SMOKE_REPORT=/tmp/cove-gameplay-report.json \
node scripts/smoke_integrated_wasm.mjs /path/to/normal-web-package salvage-cove
```

## Retained failures and next work

The first build attempt hit sandbox access to the Nix daemon; the authorized
build environment worked. A duplicate test registration was corrected. The
first route test queried interaction while the character was still landing
from the engine mounting plate; the corrected test waits for landing. The first
browser route completed all boat interactions but its recovery waypoint was
shallow enough to stand; the final route uses the actual deeper seaward end.
The packaged test initially exposed a GCC diagnostic in its iterator-based file
reader; an explicit bounded read corrected it without suppressing warnings.
These failed attempts are retained, not reported as passing results.

Next implement the live physical skiff: complete the outstanding SIM admission
and lifetime integration, connect assembly render poses, then buoyancy/thrust
and loaded recovery. Preserve the working dock/boarding route. Final robot,
third-person camera, moving-deck physics, controller parity, native HUD, durable
rescue policy, jobs, performance, visual approval and independent reviews are
unfinished. No gate passed and no gate commit was created here. Follow D20:
advance playable functionality; do not resume repeated screenshot matrices.
