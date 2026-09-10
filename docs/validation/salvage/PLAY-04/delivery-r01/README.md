# Durable generator delivery

Verified browser component of PLAY-04/SAVE-04. Native integration builds, but
its actual H → automatic disk save → process restart journey remains open.
No full task/gate, independent review, human approval or gate commit is claimed.
Baseline HEAD: `7f28fabfabf3d63726f6cfa1001c3ce1ed557911`, with the authorized
uncommitted implementation tree. Exact source/runtime/evidence hashes are in
`manifest.json`. No screenshots were captured or reviewed.

## Player behavior

Accept the generator job, build a craft that lifts the load clear of its hull,
hook it, reel up, and steer the loaded craft into the harbor. Deliver becomes
available only when the actual cargo is inside the zone, high enough and moving
slowly enough. Delivery secures the observed cargo in place and releases its
tow. The game pauses while the resulting expedition is saved. It shows the
60-material reward and unlocks Resume only after storage confirms publication.

A failed save keeps the delivery pending and the expedition frozen. Retry
with Save expedition (browser) or F10 (Linux). Closing/reloading can recover
only a complete stored checkpoint; unacknowledged work may be absent. Never
claim success from the in-memory inventory value alone. Bookmark the resulting
browser world address on the same origin/profile; there is no world picker yet.

## Application and host contract

1. Both storage hosts register through trusted action 6. Delivery admission
   requires a registered host; the player cannot invoke this registration.
2. The existing scene transaction verifies real cargo pose/motion and a future
   completed tick. It stages a static cargo at that observed pose, retires the
   dynamic cargo and rope, and waits for matching physical/event evidence before
   canonical banking. Cargo remains independent scenery, not extra hull mass.
3. An admitted delivery blocks player actions and neutralizes locomotion, helm
   and winch. Canonical completion requests the existing Pause boundary. All
   submitted work, final neutral tick, body/rope observations, ordered events
   and session tick must join before capture.
4. Action 1 captures the whole SVCE archive, including completed job, inventory,
   retired-parent lineage when present, and Banked physical cargo. During a
   pending delivery it retains SHA-256 of those exact bytes. It does not write
   storage or release the reward.
5. The browser publishes through exclusive Web Locks/strict IndexedDB with
   current/mirror in one completed transaction. Native uses the existing
   single-operation disk worker and waits for mirrored/fsynced publication.
   Only afterward does that host call action 7 with the exact whole-SVCE digest.
6. Action 7 requires the same healthy, fully paused, secured delivery and exact
   captured digest. It sets the success message before clearing pending state,
   so an allocation failure cannot partially unlock the expedition. This is a
   trusted host acknowledgment, not independent proof that disk was written.
7. Browser `job.savePending`/`job.durable` distinguish securing, saving and
   durable success. Pause/Resume, Leave and job controls are disabled while
   pending. Native polls `salvageDeliveryNeedsSave()` without serializing JSON
   every frame. Both hosts make one automatic attempt, then require an explicit
   retry after failure. Closure revokes ownership; late callbacks cannot resume
   the old world. Wrong acknowledgment revokes/fences that owner.
8. Reload validates the saved completed job and Banked cargo, publishes the
   fresh authority/retired parent, then activates exactly one neutral tick.
   Banked cargo remains static with no buoyancy driver. Repeated Deliver cannot
   recreate logical cargo or pay another reward.

The checkpoint contains the accepted logical transaction and its retained
state. This does not implement a separate append-only durable journal, receipt
compaction, general autosave, latching, all native/browser live fault recovery,
Windows storage or the final harbor lift/capability unlock.

## Actual browser result

Final run: `journey-r07/summary.json`, **22 stages passed**, Chrome
152.0.7977.82, hardware WebGPU/Vulkan, reported AMD RDNA-3 nonfallback adapter,
960×540. This is a headless control/state journey, not visible frame-rate or
visual-quality acceptance. All stages retain LEGO terrain and check for device
errors. Browser errors and uncaptured GPU errors were empty.

World: `008a81e14a8515d38dff311f3fc3e4a5` in an isolated temporary browser profile.
The runner closes/removes that private profile. It is not a user save address.

| Step | Actual result |
| --- | --- |
| Fresh starter | 11 parts, 1,035 kg, 48 material |
| Build and launch lifting rig | Remove loan cradle, buy beam for 12; move winch; 11 parts, 1,035 kg, paid ID 35, 36 material |
| Hook/lift/return | Independent 420 kg generator; real reel/throttle/steering inputs; intact cable |
| Eligible delivery | Cargo `[-2.49001, .301949, -54.7585]`, harbor distance 3.08471 m; authoritative Deliver enabled |
| Automatic delivery save | Completed/secured/durable; no logical cargo; 96 material; tick 1250, epoch 1 |
| Actual page reload | Same world, paid ID, boat mass, secured cargo and 96 material; tick 1251, epoch 2 |
| Resume and press H again | No second reward; cargo remains banked; 96 material |
| Sail away | Boat 2.13767 m/s; banked cargo position unchanged; 96 material |

All construction and gameplay actions use actual mouse/keyboard controls.
Read-only application JSON supplies observations to the driver; it never sets
game state or injects completion. Reverse steering uses observed boat heading
and cargo position. Delivery thresholds, rope strength, physics and default
terrain were not weakened to make this journey pass.

The successful rig is one tested solution, not a required design or a proven
onboarding flow. Its exact integer lattice placements (0.02 m/tick) are:

- Remove Cargo cradle and Keep (loan refund zero).
- Move Winch temporarily to `[75,96,-2750]` and Keep.
- Add a paid Beam at `[-50,56,-2750]` and Keep.
- Move Winch to `[-125,112,-2750]`, Keep, then Launch.
- Walk the dock through `[4.5,-49]` and `[4.5,-53]`, board, walk to the actual
  helm and use it. Hook with F. Reel until the cargo clears the water; continued
  winding can overturn the craft. Reverse/steer toward the harbor and slow down
  until Deliver becomes enabled.

## Focused validation

| Check | Result |
| --- | --- |
| Bazel optimized native application | Passed, final action-7 ordering |
| CMake native application | Passed, final action-7 ordering |
| CMake WASM application | Passed, final action-7 ordering |
| Native cove/session/storage regressions | 264 passed, no skips |
| Browser save coordinator cases | 12 passed, including delayed write, retry, closure and rejected digest |
| Preview UI behavior cases | 16 passed; saving and durable rewards distinguished |
| Actual browser delivery/reload/duplicate/sailing | Passed, 22 records |

Node's combined reporter lists 13 entries because the sixteen procedural preview
cases are one file-level entry alongside twelve named coordinator tests. The
native 264-case run precedes the final application-only exception-order guard;
the relevant domain/store code did not change, and both native builds plus the
actual WASM journey include the guard. This was not the full repository suite.

Reproduce from the repository with the Nix toolchain and configured WASM tree:

```sh
nix-shell
bazel build -c opt //:voxy_native //tests:voxy_tests
bazel-bin/tests/voxy_tests --gtest_filter='CoveSave.*:*Cove*:*Session*:NativeSaveStore.*:NativeSaveWorker.*'
cmake --build build-native-save-host --target voxy_native -j8
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8
node --test scripts/test_cove_saves.mjs scripts/test_salvage_preview.mjs
```

For the real browser journey, use a fresh packaged web directory containing
the matching `web/` files and final `build-lego-wasm/bin/voxy_wasm.*` outputs.
The successful package was `/tmp/voxys-durable-haul-nnz3sh1t/web-r02`;
manifest hashes identify its runtime. Choose a new evidence directory each run:

```sh
VOXY_TEST_CHROME=/usr/bin/google-chrome \
VOXY_SMOKE_GPU=hardware VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_COVE_DELIVERY=/absolute/fresh/journey-directory \
VOXY_SMOKE_REPORT=/absolute/fresh/browser-report.json \
node scripts/smoke_integrated_wasm.mjs /absolute/matching-web-package salvage-cove
```

## Retained failures

The original reports/logs remain here; none count as a passing journey.

- An initial invocation accidentally used a pre-existing stale temporary web
  directory after its creation failed. Its log is retained as a packaging/test
  setup failure, not evidence about the current runtime.
- `journey/`: an incorrect driver waypoint entered water instead of the actual
  dock boarding position. The corrected waypoints above follow authored space.
- r02: the original winch above a pontoon pulled the cargo into the hull and
  broke its rope. The player-built beam creates lifting clearance.
- r03: extra low pontoon and excessive reeling overturned the craft after the
  load was already raised. The driver now stops lifting when clear of water.
- r04: the heavier/lower arrangement remained outside the harbor on return.
- r05: the lighter beam-only craft moved but straight reverse kept its cargo
  outside the zone. The driver now steers from actual observed heading/position.
- r06: delivery actually saved, then the driver checked DOM text before its next
  UI refresh and failed before reload. A bounded read-only receipt-text wait
  fixes that test race. r07 completes all remaining reload/duplicate/sail checks.
- The first UI test run expected a completed in-memory job to show +60 without
  durable acknowledgment. The updated fixture requires the durable flag and
  tests pending/undurable states explicitly. Final checks pass.

## Next implementation contract

1. Verify native H → automatic save → process restart → no second reward with
   real controls. Native code is integrated and built, but the previous native
   manual construction/restart journey is not evidence of this automatic path.
2. Cover live failures at physical securing, canonical commit and storage
   publication. Keep uncertain/conflicting owners fenced; retain retry for
   definite prepublication failures. Coordinator fault tests use controlled
   asynchronous doubles; they are not actual disk-full/power-loss certification.
3. Implement the generator's visible harbor lift/power upgrade and useful
   capability unlock, driven only by durable completed state and reconstructed
   after reload. Then progress to rescue and the second job. Do not fabricate
   success scenery or add an unearned upgrade before storage acknowledgment.
4. Improve the first-haul help/controls using the actual construction problem:
   the load needs hull clearance and steering on the loaded return. Do not make
   the validated test blueprint the only permitted design. Native/controller
   UI, picking, latching and general construction remain unfinished.
5. Continue remaining SAVE scope and independent reviews. Keep parent tasks and
   gates open. Run required repository checks and commit only on a full gate.
