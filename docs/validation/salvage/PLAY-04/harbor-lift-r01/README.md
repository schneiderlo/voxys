# Harbor boat lift — physical and archive preparation

Status: scoped physical/archive preparation verified; live integration is next.
Full PLAY-04/SAVE-04 and gates remain open. This new service is not exposed in the playable cove yet. No
screenshots, simulated user reports or gate commit.

## Purpose and implementation

A delivered generator should unlock useful harbor machinery. Profile 1 prepares
a fixed boat hoist using the actual compiled hull, four force-limited distance
constraints, gravity, unchanged mass and existing GPU buoyancy. It does not
teleport a boat, freeze it into a static substitute or replace its geometry.

`src/game/expedition/cove_harbor_lift.*` prepares ten structural boxes on the
.02 m lattice: four posts, a closed overhead frame and two trolley rails.
The overhead underside is 7.04 m above the harbor datum (22 brick plates).
The same boxes are intended for visible structure and owned static collision.
They currently enter the focused GPU test, not the application scene.

The initial rig requires two aligned sealed pontoon **parts**, each of which
can contain several buoyancy regions. It selects each part's largest central
region, places two sling stations at quarter/three-quarter length and finds
actual solid underside support from that part's collision sources. It derives
COM-frame attachment anchors through `AuthoredBodyFrame`. The accepted boat,
its original 1,035 kg mass and its buoyancy remain unchanged.

Preparation refuses missing delivery data, unsupported pontoon counts/layouts,
boats outside the frame, excessive height or mass above 6,000 kg. That upper
admission limit is not evidence that all boats up to 6,000 kg have passed.
Attachment planning validates distinct live-handle values, finite normalized
motion, fixed base, upright/slow approach, whole-boat frame clearance and all
four endpoint distances before returning any descriptors. The caller still
owns real authority checks, capacity, atomic activation and observed completion.

Each line caps motor force at 30 kN and breaks above 45 kN. Raising is 0.5 m/s;
controlled lowering is 0.25 m/s. Actual minimum length is the greater of 2.5 m
and the boat's height above its lowest sling plus 0.4 m overhead clearance.
The starter's resulting minimum is 3.86 m. Maximum length is 12 m. The current
single-pass rope solver's broader coupled-constraint stability remains open;
this checkpoint must not certify arbitrary speeds, loads or rig layouts.

## Saved-state preparation

`cove_harbor_state.hpp` and `cove_save.*` add a 22-byte, explicitly versioned
physical extension. Profile 0 retains **exact v1 bytes and geometry semantics**.
Profile 1 explicitly installs the new service, even while detached. Attached
and Broken retain four actual lengths and the four-bit break mask, and require
canonical generator banking. No saved flag alone acknowledges storage or grants
power. Force/anchor values and GPU handles cannot enter the file.

`CoveRestoreCandidate` currently refuses every profile-1 archive with
“This build cannot restore a harbor lift world yet.” This is intentional:
accepting the file while omitting four physical constraints or its new static
geometry could drop or corrupt a suspended boat. No application action emits
profile 1 yet. Existing saves and delivery controls still use v1.

The exact layout, canonical rules and migration boundary are in
[the cove format document](../../../../salvage-cove-save-format.md).

## Observed failures and corrections

- The first compilation exposed strict float-conversion warnings. They were
  corrected explicitly; no warning was disabled.
- r03 treated each sealed subvolume as a separate pontoon. Grouping by actual
  part identity corrected that failure.
- r04 revealed the standalone cove test lacked its runtime shader runfiles.
  Its Bazel data declaration now includes the shipping shader group.
- r05's fixed 2.5 m minimum did not reserve clearance for the boat's tallest
  part. One line broke and the boat later capsized. Deriving clearance from
  actual bounds kept all lines intact in r06. No break-force increase was used.
- r06 raised the boat 2.70 m and cleared its entire hull above water by 2.05 m,
  but missed the test's requested three-metre travel. The overhead was raised
  by two brick plates to 7.04 m, preserving the clearance margin.
- r07/r08 raised and held correctly, but lowering at 0.5 m/s overloaded the
  coupled lines and capsized the boat. r08's observations retain break ticks
  881/989/992 and the actual forces. Lowering at 0.25 m/s passes with unchanged
  force/break limits. This is a bounded service tuning choice, not proof that
  the general rope solver is stable at higher payout speed.
- The first shared WASM save run exhausted the temporary-filesystem quota while
  compiling tests; its partial manifest and driver error are preserved. The
  replacement uses a new workspace-disk temporary root, not another run in the
  exhausted temporary filesystem.

## Executed checks and exact scope

- Full affected Bazel targets: **32 cove cases + 9 save cases passed** before
  the explicit installation-marker refinement. Those logs remain historical.
- Final installation/compatibility refinement: **12 focused Bazel cases pass**
  (9 saves, 2 lift preparation/refusal, 1 actual restore preparation).
- Current CMake binary: **4 focused cove cases pass**, including actual GPU
  lift/hold/lower/release and the profile-1 restore refusal.
- Current shared WASM test artifact: **152 cases pass, zero skipped/disabled**,
  including all nine cove save cases. Its manifest records unchanged source
  hashes and includes the new harbor-state header.
- Both native application builds and the WASM application build pass. The WASM
  application build retains the existing `WorldPosition` default-comparison
  warning; it is not suppressed or counted as a new harbor failure.
- `git diff --check` passes. No full gate or required pre-commit gate suite was
  claimed or run for a gate commit.

The hardware lift case uses the actual eleven-part **1,035 kg** skiff on the
shipping GPU backend, real compiled shape, two-body owned admission, unchanged
flat-water buoyancy and gravity at the cove's **−200 m** datum. It creates four
actual rope constraints, reels for 600 ticks, holds for 120, pays out for 1,320
and releases/settles. Observations join the actual body/attachment packet.
The result (both native builds) is root height **−199.680 → −196.343 → −199.680 m**,
**3.337 m** travel, whole-hull clearance about **2.700 m** above the water, upright
factor **0.999999** at the top, all four lines intact and clamped at **3.86 m**.
No mass, hydrostatics, gravity, force or break threshold is replaced for success.
The lowering speed is deliberately 0.25 m/s; failures at 0.5 remain documented.

This is a headless hardware test on **AMD Radeon 890M / RADV STRIX1 / Vulkan**.
It does not load the actual cove terrain/waves, draw the new structure or use
player controls. It is not a displayed-game performance or browser GPU claim.
The new save format is proven in native and real WASM CPU execution; no live
hoist archive has been captured or physically restored yet.

Reproduction from the repository root (fresh output locations recommended):

```sh
nix-shell --run 'bazel test -c opt //tests:cove_player //tests:cove_save --test_output=all'
nix-shell --run 'cmake --build build-native-save-host --target cove_player_tests voxy_native -j8'
nix-shell --run 'build-native-save-host/bin/cove_player_tests --gtest_filter=CoveMovement.HarborLift*:CoveMovement.OwnedRestore*'
nix-shell --run 'bazel build -c opt //:voxy_native'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
```

The portable archive validation command is
`python3 scripts/validate_session_transactions_wasm.py --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node --output /absolute/new/output --exception-mode js --asyncify --expected-tests 152`.
Run it inside Nix with `TMPDIR` set to a new workspace-disk scratch directory
when the temporary filesystem quota is exhausted. The successful output is
`build-harbor-lift-643i_bo4/wasm-save/`; the exact manifest and logs are retained
here. All earlier attempts remain in `/tmp/voxys-harbor-lift-munalzkx` and their
reports are copied here. None of these logs is a deployed preview update.

## Required next work (self-contained)

1. Integrate an explicit profile-1 installation transaction. Preserve old v1
   worlds until the new service geometry has been validated and the profile
   durably published. Check occupancy before admitting new fixed collision;
   do not move existing saved boats/cargo out of its way. Fresh-world bootstrap
   and upgrades from existing real delivery saves need distinct tests.
2. Split the fixed harbor structure from per-craft rig preparation as needed,
   so an incompatible boat receives a clear refusal while the powered harbor
   still exists. Render the actual structure, rails, hoist and believable sling
   paths from the physical recipe. Keep the LEGO terrain and existing boat art.
   Final authored assets/visual approval remain separate work.
3. Connect player-facing Attach/Raise/Lower/Release controls at the dock, only
   after durable generator delivery. Do not automatically raise on unlock.
   Neutralize held input on pause, focus loss, leaving controls or failure.
   Prefer contextual existing controls or clearly labeled buttons; avoid key
   conflicts with H delivery and the existing F/Q/Z boat winch.
4. Preflight/activate all four lines through actual future-tick ownership.
   Partial creation failure must retire all staged lines. Join every line and
   the fixed body to observations/events. A break stops all motors and retains
   exact individual break state; no stale handle may control a later rope.
5. Integrate capture/restore with all four constraints and profile-1 geometry
   before removing the current restore refusal. Validate saved lengths against
   the freshly compiled craft's tighter minimum. Restore motors neutral; retain
   surviving lines and exact breaks at the certified tick. Rebuild actual mass,
   cargo and player as now. Native/browser storage acknowledgments stay exact.
6. Handle Reset, workshop/refit, Undo/Redo, Pause, device loss and drained Leave.
   A suspended craft cannot be replaced while old ropes still reference it.
   The fixed body's resources and every rope need ordinary observed retirement.
   Preserve material, paid identities and the once-only generator reward.
7. Exercise actual dock controls on native and hardware browser using the real
   delivered saves from the prior PLAY-04 checkpoint. This avoids repeating an
   already-passed full generator haul. Install → raise → Pause/Save → actual
   restart → lower/release → board/sail; verify the same four lengths, boat,
   materials and powered state. Exercise pre-delivery refusal, duplicate unlock,
   save failure/retry and wrong-world/unsupported-version refusal. No state
   setters or success injection. No screenshots under D20.
8. Add edited-craft, off-center approach, motion/load, collision, single-break,
   capacity and restore bounds cases as their actual integration is implemented.
   The current starter check is not full service acceptance or a 6-ton rating.

Do not check PLAY-04 or commit a gate from this component alone. The active
objective remains the complete plan through G14, including rescue, second job,
all gameplay/production/network/content/human/performance/release requirements.
