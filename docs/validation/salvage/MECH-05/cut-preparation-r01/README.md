# MECH-05: cut preparation and fragment motion

Recorded 2026-09-10 on `codex/salvage-implementation`, based on G00 commit
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911` plus the existing implementation tree.
This report verifies the preparation component. It does **not** complete
MECH-05, PLAY-05 or a gate. Live cutting is not enabled in the playable cove.
No screenshot, render capture or human acceptance is claimed.

## Why this is the next prerequisite

PLAY-05 requires loss/recovery after cutting. The later mechanics phase depends
on G06, which includes PLAY-05. D32 explicitly schedules the necessary cut and
fragment foundations early, retaining all later mechanics, physics, authority,
recovery and gate criteria. The existing `AssemblyMassPlan` already discovers
rigid components; the playable `CoveBoatAssembly` still admits exactly one root.
Do not fork the older prototype `gameplay::StructuralAssembly` into a second
canonical graph or silently discard fragments to satisfy that one-root adapter.

## Implemented contract

`src/game/construction/assembly_fracture.*` adds immutable
`AssemblyFracturePlan::prepare`. It takes a canonical build snapshot, expected
topology revision, a bounded nonempty list of exact connection IDs, selected
catalog and the existing compile profile.

- Normalize cut ID order; reject duplicates, unknown links, disabled links,
  non-weld connections, invalid builds, stale/exhausted revisions and excess
  request size. Cut only enabled welds. A cut sets enabled=false and damage=10000;
  it retains the connection ID, endpoints, strength and reserved socket slots.
- Preserve every `PartInstance` exactly: ID, definition, owning build, placement,
  health, paint, settings and paid/loan provenance. Preserve build identity,
  owner and lease. One successful batch increments topology revision once.
  No inventory, refund, grant, new part ID or fragment lifetime is introduced.
- Use the existing `BuildModel` and full `CompiledAssembly` before/after pipeline.
  Every resulting root has mass/full inertia, exterior collision, buoyancy,
  modules and remapped socket/connection bindings. Disabled bonds remain saved
  canonical records. A redundant weld may be cut without causing a split.
- Own both immutable compilations and the final canonical build. Each after-root
  maps to exactly one previous root and a checked translation in its axes.
  Verify all part-to-root mappings. A cut cannot merge two previous roots.
- Honor the compiler's configured limits. The shared maximum remains 256 parts
  and 64 rigid roots. Reject an over-limit result before publishing anything;
  never drop a paid, loaned, functional or small piece to make a cut fit.
  Backend capacity must also be reserved before the later live transaction.
- Validation failures and allocation exceptions leave the caller's input intact.
  Preparation catches allocation failure and returns a capacity issue. This
  checkpoint does not add allocation-fault injection coverage.

`inheritMotion` consumes a whole `AssemblyMotionSource`, keyed by build,
pre-cut revision, completed tick and exact old root IDs. Root observations may
arrive in any order. Missing, duplicate or unknown roots are rejected, as are
wrong identity/tick, nonfinite motion, reflected/scaled/nonorthogonal matrices
and nonfinite computed output. Matrices must be proper rotations within 1e-9;
callers converting GPU quaternions must normalize in double precision first.

The caller supplies **post-solve** origin velocities and angular velocities.
For each new root, rotate its local offset to world axes, shift its pose by that
offset, and set `v_new = v_old + omega_old cross offset`; copy angular velocity.
The method accepts no force/impact argument and does not reapply the breaking
impulse. Independent pre-existing roots retain their own motion. All poses use
one caller-selected reference/sector frame in double precision; converting to
world sectors and packed GPU values remains backend admission.

This is a checked value API, **not** a GPU-fence certificate or player authority.
Do not feed it client-asserted motion or mismatched observation ticks. The later
runtime must obtain a certified completed snapshot, retain its source identity,
and hold the publication boundary while shapes/bodies/joints are prepared.

## Evidence

Bazel passes **126 cases**: 88 shared construction/compiler/fracture checks and
38 cove mapping/movement/physics checks. The final fracture result is cached
from its preceding passing run; other final target cache status is retained in
`native-tests-r03.log`. Eight new shared fracture cases cover:

1. Exact paid/loan parts, damaged condition, paint and lease survive a cut;
   the disabled bond round-trips through the existing canonical build codec.
2. Cutting one of two alternate welds keeps one body; cutting the remaining
   path or cutting both in one batch creates two roots.
3. An analytic spinning parent gives the displaced child the correct origin
   velocity, including the angular cross term.
4. Full tensor/offset-COM mass, linear/angular momentum and kinetic energy are
   conserved through a three-piece split for all 24 cube rotations and an
   arbitrary proper rotation. The independent aggregate includes spin plus
   orbital angular momentum; tolerances are in the test source.
5. A pre-existing separate root keeps its own pose and velocity when another
   root splits; unordered source observations are accepted.
6. A 256-part build splits into 64 complete four-part roots. A 65th root is
   rejected without altering input or dropping any part.
7. Stale/empty/duplicate/unknown requests, exhausted revision and reduced root
   capacity refuse without changing the original canonical bytes.
8. Mixed identities/ticks, missing/duplicate roots, invalid rotation, nonfinite
   motion and overflowing derived velocity refuse without partial output.

The new **actual cove content** case creates a real purchased pontoon through
normal refit preparation, cuts its attached weld(s), and verifies both resulting
roots. All 12 original parts remain; dry mass stays 1155 kg; the paid pontoon
is a one-part root. Both compiled exterior shapes pass `AuthoredShape::prepare`
with their own full mass tensors, and motion preparation produces two states.
This is CPU shape preparation using actual installed models, not live GPU
body replacement or an automated player cutting journey.

CMake builds the native app and both affected test targets, then passes all
**8 fracture cases plus the actual cove cut case**. The Bazel native app builds.
The configured WASM application builds, and the actual JS-exception/Asyncify
Node runtime passes **all 88 shared construction/compiler/fracture cases** with
no skipped/disabled tests, a fixed 64 MiB heap and 1 MiB stack. Its manifest
records the source hashes, compiler/runtime versions, arguments and unchanged
inputs across compilation. The runner's previous expected count of 78 was stale:
the existing construction suite now contains 80 cases, plus eight fracture cases.

## Reproduction

Run GPU-containing cove tests separately from real GPU browser/native journeys.
No screenshot workflow is needed for this preparation component.

```sh
nix-shell --run 'bazel test -c opt //tests:assembly_fracture //tests:build_model //tests:assembly_compiler //tests:assembly_collision //tests:assembly_buoyancy //tests:assembly_functions //tests:compiled_assembly //tests:cove_player'
nix-shell --run 'bazel build -c opt //:voxy_native'
nix-shell --run 'cmake --build build-native-save-host --target voxy_native assembly_fracture_tests cove_player_tests -j8'
nix-shell --run 'build-native-save-host/bin/assembly_fracture_tests'
nix-shell --run 'build-native-save-host/bin/cove_player_tests --gtest_filter=CoveMovement.CuttingAddedPontoonPreparesTwoCompleteRootsWithoutLosingOwnedParts'
```

WASM validation (use a new output directory):

```sh
nix-shell --run 'TMPDIR=/home/modkin/workspace/schneiderlo/voxys/build-fracture-hmp07z4w python3 scripts/validate_assembly_compiler_wasm.py --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node --output /absolute/new-wasm-evidence --exception-mode js --asyncify'
```

The SDK cache is copied into isolated scratch; the runner never changes the SDK
or Bazel's shared cache. `wasm-r01/manifest.json` retains exact executed commands.
Native build/test logs and final binaries are identified by `manifest.json`.

## Retained failures

`tests-r01.log` records initial strict test compilation errors: optional rotation
access, mixed-size array deduction and misleading one-line indentation. They
were corrected without changing production behavior. `cmake-build-r01.log`
records discovery registration before `include(GoogleTest)`; registration now
sits with the other discovery calls. During the next multi-target invocation,
CMake regenerated the makefiles but the already-running top-level make did not
know the new target. Reissuing the build against the generated target succeeded;
`cmake-build-r02/r03.log` retain both outcomes. No failed test was disabled.

## Required next integration — leave parent tasks/gates open

1. Add an authorized cut command and exact replay/journal semantics. It must
   preserve paid/loan parts, clear incompatible build history selectively, and
   publish the exact cut plan under one revision without refunds or new grants.
2. Generalize the live cove owner from one boat body to multiple compiled roots.
   Reserve all shapes/bodies and water-driver capacity before retiring the old
   body; retarget live ropes/modules/player support by part/socket identity.
   Primary/control root must follow functional part membership, not index zero.
   Map renders/collision to each root and invalidate old contact/cache handles.
3. Obtain completed post-solve source motion at the held transaction boundary.
   Convert normalized root rotations to the common double frame, call
   `inheritMotion`, then admit every actual child body. Never simulate old and
   replacement bodies together or apply the breaking contact impulse again.
4. Extend the physical archive to save every dynamic root by stable root key and
   topology revision. Current SVCE v1–v3 and `CoveBoatAssembly` intentionally
   remain one-body paths. Retain legacy admission and exact native/browser saves.
5. Protect the last intact launch design before cutting, so recovery can rebuild
   a valid connected machine. The current backup writer refuses disabled welds;
   do not silently turn a broken multi-root machine into a connected free copy.
   Starter recovery must retire/recover every old loan fragment and store every
   owned paid part once, including fragments away from the primary hull.
6. Add actual reachable cutter/tool controls, clear damage/repair feedback,
   workshop reattachment with exact costs, bounded abandoned objects and the
   required native/browser loss/reload/exploit journeys. Keep the LEGO terrain.
7. Continue the full mechanics plan: load warnings/estimation, articulated
   machinery, repair, flooding, save/replay and the human/performance gates.

The last playable build is still the verified recovery-design package on port
38206. This component changes no terrain, art or player UI and needs no new
preview origin. Do not mark MECH-05 or any full gate complete from these tests.
