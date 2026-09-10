# Cove root ownership and joined motion

The scoped ownership/ordinary-Launch migration and joined-motion preparation
pass. This continues D32 after the wave-clock correction and native atomic
parent-retirement primitive. Full multi-root gameplay and gates remain open.
Cutting remains disabled. The last owner-facing preview remains unchanged.

## Current implementation

- `PhysicsMutationBatch::joinedBoundary` names the expected owned incarnation
  and completed tick. Actual encoded/submitted/completed ticks must coincide,
  no scheduled tick or owned progress batch may remain, and no submission may
  be unresolved. This admits an accumulator-based owner at an actual joined
  boundary without removing the old unqualified accumulator refusal. A prepared
  token blocks further scheduling until commit/discard. It neither pauses the
  game nor certifies the caller's separate gameplay pose/event evidence.
- Workshop Launch already holds scheduling while GameSession preparation is
  pending; it is not an explicit user Pause. Launch now waits for all owned
  batches/poses/events, reserves the new body/water, and uses a checked joined
  parent-retirement batch. A refused batch cancels every unexecuted child and
  keeps the old boat. The exact next tick is rechecked before commit.
- `CoveRigidRoots` holds at most 32 roots, matching SVCE v4. Records carry the
  least-member part key, temporary shape/body handles, admission tick, spawn
  and observed motion, observed tick and retirement state. The container also
  binds the accepted build/revision and explicit controlling root; it cannot
  be copied. Application owns it beside the immutable compiled assembly.
- Application's former single boat body/shape/motion fields now live in this
  collection. Initial/canonical/restored preparation and Launch publication
  create/swap the collection. Rendered parts resolve their own compiled root.
  Observation range, per-root shape/motion validation, joined pause/save
  predicates and Leave retirement include every root. A dead observation
  before admission is ignored; unexpected death after admission fails closed.
  Read-only diagnostics expose root keys, positions, ticks and joined tick.
- Pending Launch owns a complete candidate root collection, copied upload
  payloads and a bounded borrowed parent set. It retains partial uploads for
  cleanup, prepares every child body/water driver before retirement, and
  cancels all unexecuted children on refusal. Zero-displacement children skip
  the water driver. Publication requires every child observation and every
  parent's dead observation, then swaps the owning collection with the build.
  Cleanup drains all old shapes. Ordinary dock Launch still requires exactly
  one welded root; cutter requests remain rejected.
- `inheritFractureMotion` stages the complete child's motion array from one
  joined parent collection and an exact compiler/cut identity. Invalid/stale
  parents, an incorrect destination or an already admitted child refuse
  without partially changing child motions. Every result is checked in its
  own authored mass frame. Preparation never becomes completed evidence.
  This helper is not yet called by the live cut adapter.

## Validation status

The owner-enhanced authored GPU split passes in `checks/root-owner-r02.xml`:
actual accumulator scheduling, paused cancellation, wrong identity/stale tick
refusal, inherited motion, two floating bodies, per-part body mapping and joined
observations. That run also contains a failed CPU fixture. The final CPU
32-root/mixed-observation fixture passes separately in `root-owner-r04.log`.
The cove's existing scene compiler rejects the 33rd part/root before runtime
ownership can be constructed. Do not claim a runtime 33-root admission test.

The native game passes all 37 actual-control stages in
`native-delivery-r01/summary.json`: paid rig construction/Launch, the 420 kg
generator, loaded return, automatic delivery save, process restart, duplicate
payment refusal and sailing at 2.21 m/s. The final boat retains paid part 35,
1035 kg and one joined live root. Every recorded stage checks unique root keys
and common joined observations; paused stages require a joined body tick.
The executable hash was captured before the journey. Its exact executable is
preserved as `build-cove-live-roots-qxbqt1ib/native-ownership-r01`.
Both native application build paths and the browser application build pass.

Browser `browser-delivery-r02` passes all 29 actual-control stages with the
frozen `web-r01` package and the same root invariants. Paid part 35, the 1035 kg
boat, 96 material and the banked 420 kg generator survive actual reload.
Tick 656 saves and tick 657 restores with the same 10.9167 reported seconds of
water phase. Duplicate delivery pays nothing; sailing reaches 2.42526 m/s.
Chrome 152.0.7977.82 uses the normal X11 hardware profile; browser errors are
empty. The retained isolated profile is `/tmp/voxys-startup-powYJQ`. This is
functional execution evidence, not displayed-frame performance acceptance.
The first attempt rejected an out-of-range runner timeout before gameplay;
its log is retained.

The later inherited-motion helper passes its CPU case with two independently
moving parents becoming four roots. It verifies unchanged destinations on
stale/mixed ticks, nonfinite parent motion and the wrong fracture plan, followed
by correct positions and linear/angular motion for every child. All eleven
authored mass-frame cases also pass, including the new checked fragment-position
translation: near/far local precision, sector carry, nonfinite refusal and both
world limits. The helper reuses this checked path instead of saturating an
out-of-world absolute coordinate. The final combined selection passes all
eleven cases: eight actual GPU transaction/scheduling cases, the 32-root owner
case, the independent-parent motion case, and the actual moving skiff split
using this helper. Both child bodies inherit motion, conserve momentum, float
and drain; root 1 owns the helm. Final Bazel/CMake native application builds
and the WASM application build pass. `checks/final-artifacts-r02.sha256.json`
records those final sources/binaries. The frozen native/browser gameplay artifacts above precede this
helper, which has no active gameplay call site.

Retained failures are fixture/compile failures, not waived runtime assertions:
the initial owner test used invalid scene slots and later omitted the shape
pool identity. The motion helper initially used unavailable aggregate equality
and implicit index conversions; it now uses canonical connection comparison
and explicit conversions. The second actual pontoon cut produces four roots,
not the fixture's assumed three; its expected topology was corrected.

Scratch is `build-cove-live-roots-qxbqt1ib`. Keep GPU checks and playable
journeys sequential. No screenshots. Preserve failed runs and their scope.

## Still required before full root/cut acceptance

1. Complete/exercise all-root reservation under failure: full body, shape,
   water-driver, pose/event and queue capacity, including partial-upload and
   unexecuted-child rollback. The ownership/iteration paths are implemented;
   no all-root live admission acceptance is claimed. The current backend has
   16 water drivers and 2048 cells per driver; capacity must reject the whole
   replacement before touching parents, never discard a fragment.
2. Activate exact certified `AssemblyFracturePlan` motions under the existing
   cut journal and wait for every new body and retired parent before logical
   publication. Complete render publication across the replacement tick too;
   the current accepted draw list switches after joined CPU confirmation.
   Existing cutter requests still refuse in `prepareBuild`.
3. Retarget rider/deck/helm, tow/winch and harbor endpoints by owning part/root.
   The primary-root camera/player/tow/harbor consumers are still single-root
   policies. Do not expose a cut merely because rendering can map each part.
4. Wire all-root physical v4 capture and restore/admission. Current restored
   preparation still admits only a single compiled root and old physical loads;
   no live v4 acceptance has been enabled. Preserve old saves and exact phase.
5. Make rescue/rebuild retire/recover every distant loan/paid root, protect the
   last intact design before cuts, and preserve each paid part and cargo once.
6. Run actual native/browser cut, save/reload, remote-fragment recovery, and
   rejection/rollback journeys before enabling cutter controls or checking
   the parent MECH-05/PLAY-05/gate boxes.

## Reproduction

Run from the repository root using the Nix development shell. Never overlap GPU
checks with a native/browser playable journey. Every journey uses isolated saves
and a new output directory; no images or state injection are used.

```sh
bazel test -c opt --jobs=8 //tests:authored_body_frame
bazel test -c opt --jobs=8 //tests:cove_player --test_filter='CoveMovement.LiveRootOwnerBoundsEverySectionAndRefusesMixedObservations:CoveMovement.CutMotionStagesAllIndependentParentsOrLeavesEveryChildUnchanged:CoveMovement.ActualCutReplacesOneMovingSkiffWithTwoCompleteFloatingGpuBodies'
bazel test -c opt --jobs=8 //tests:voxy_tests --test_filter='GpuPhysicsTest.Prepared*:GpuPhysicsTest.InvalidPreparedTransfer*:GpuPhysicsTest.FullCapacityTransfer*:GpuPhysicsTest.AccumulatorAnd*:CoveMovement.LiveRootOwnerBoundsEverySectionAndRefusesMixedObservations:CoveMovement.CutMotionStagesAllIndependentParentsOrLeavesEveryChildUnchanged:CoveMovement.ActualCutReplacesOneMovingSkiffWithTwoCompleteFloatingGpuBodies'
python3 scripts/validate_native_cove_delivery.py --binary bazel-bin/voxy_native --storage-root <new-save-directory> --output <new-evidence-directory>
```

For browser playback, use Node 22+, `/usr/bin/google-chrome`,
`VOXY_SMOKE_GPU=gaming-x11`, `VOXY_SMOKE_NO_SCREENSHOT=1`,
`VOXY_SMOKE_KEEP_PROFILE=1`, and `VOXY_SMOKE_TIMEOUT_MS=1800000`.
Set `VOXY_SMOKE_COVE_DELIVERY` and `VOXY_SMOKE_REPORT` to new evidence paths;
run `scripts/smoke_integrated_wasm.mjs` with the frozen
`build-cove-live-roots-qxbqt1ib/web-r01` package and `salvage-cove`.
The original failed browser invocation requested a timeout outside the runner's
supported range; it was corrected without changing the runner or game.
`checks/artifacts-r01.sha256.json` identifies that frozen package and the
native journey's historical source/binary state. Later helper edits mean those
historical source digests must not be compared as if they describe current files.
The final 11-case combined selection is a filtered regression run, not the full
repository gate suite. No gate passed and no gate commit was created here.
