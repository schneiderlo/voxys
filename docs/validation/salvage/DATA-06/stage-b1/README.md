# DATA-06 stage-b1 — live session observation and coherent baselines

Implemented and validated 2026-09-08 on `codex/salvage-implementation`, after
G00 commit `7f28fabfabf3d63726f6cfa1001c3ce1ed557911`. The worktree contains
authorized unfinished DATA/ASSET work. `summary.json` records tested source and
artifact hashes; HEAD alone does not identify these changes.

**This component passes its executed checks. DATA-05, DATA-06 and G01 remain
unchecked pending independent review. No gate commit is claimed.** The cove is
still placeholder scenery; LOOK-01/G02 visual acceptance remains open.

## What changed

GameSession now owns one bounded observation hub. Admission emits one Pending
receipt; ordered completion emits the accepted state summary, when present,
then one terminal receipt. Publication follows the pre-reserved journal,
backend activation and accepted-state/receipt boundary. Notifications allocate
nothing and cannot undo a committed payment when a ring overwrites old records.
Private preparation data and admission tokens never enter observer records.

Exact retries and refused ingress create no new domain records. Seven fixed
saturating diagnostic buckets record authentication, ordering, conflicting retry,
capacity, closure, other refusals and exact retries. Const receipt queries do
not count as new ingress. An invalid/exhausted publication sets sticky
`publicationLost`; it cannot make a committed transaction look rejected.

Trusted entitlement retirement publishes its state/history change before
ordered pending invalidations. Admission closure drains pending terminal
decisions first and publishes once. Repeated closure/destruction adds no second
notice. Bootstrap/recovery do not emit seeded history as fresh transitions.
There are no installed force, attachment, damage, payout, cue or diagnostic-event
producers. The static cove emits only actual closure.

`GameSession::create` and `SessionRecovery::restore` now require a fresh public
128-bit incarnation, separate from the world and admission secret. They reject
invalid identities before allocating or consuming recovery input. The trusted
composition root supplies freshness; bit validation cannot prove global uniqueness.
The application draws separate world/observer entropy. Test identities are
explicit test-only serial fixtures. Checkpoints do not persist observer identity.

`validateInitialState` shares canonical validation without creating a journal,
observer hub, returned live authority or backend preparation. Recovery uses this
read-only validation path. The current complete recovery fixture therefore has
294 allocation-failure cut points; the archived stage-g1 count of 297 describes
its older implementation and is not overwritten.

`eventBaseline` copies accepted builds, inventory, cargo and jobs together with
public participant/active entitlements, workshop availability, admission
frontiers, history choices/counts, up to 64 terminal and two pending receipts,
all lane cursors/statistics and diagnostics at one synchronous owner boundary.
It exposes no token, original command or private inverse images. The complete
temporary copy must succeed before a consumer replaces its model and cursors.
The limit is 8 MiB of fixed object plus vector elements, not allocator overhead,
RSS, save bytes or certified physics state.

`restartEventStream` resets the hub in place with a fresh incarnation. Borrowed
reader views retain their address; stale cursors reject and require a baseline.
State, journal, history, admission and lifetime ingress counts survive. Lane
occupancy/counters and current-stream loss reset without allocation. The owner
must never recycle any earlier incarnation, including after recovery.

## Executed evidence

| Check | Actual result |
|---|---|
| Optimized Bazel | All three focused targets pass; unchanged journal/event targets cached |
| Native CMake | 95 GameSession + 12 journal + 15 event cases pass |
| Strict GCC, undefined-behavior and float-cast-overflow checks | All 122 cases pass, warnings treated as errors |
| WASM with JS exceptions and Asyncify | Same 122 cases pass, zero skips or disabled cases; fixed 64 MiB heap and 1 MiB stack |
| Transaction publication guard, each runtime | Nine boundaries, eight activations, one discard, zero allocation attempts; separate closure guard also passes |
| Entitlement retirement guard, each runtime | Four boundaries, zero allocation attempts, no partial publication |
| Creation failure probe, each runtime | All 26 allocation cuts preserve input; eventual success |
| Baseline failure probe, each runtime | All seven allocation cuts preserve source/cursors; subsequent real decisions produce exactly three events |
| Recovery factory failure probe, each runtime | All 294 cuts preserve reusable input; eventual fresh quiet stream; exhausted preflight allocates nothing |
| In-place restart guard, each runtime | Zero allocation attempts; borrowed reader survives, old cursor rejects |
| Native and shipping WASM application builds | Both pass with the integrated hub and diagnostic JSON |
| Real visible browser lifecycle | Pass on Chrome 152.0.7977.82, AMD RDNA-3 hardware adapter, 1920×1080, no browser/GPU validation errors |

The nine new `GameSessionObservation` tests cover real add/move/remove/job
acceptance, immediate rejection, canceled/out-of-order/stale preparation,
retry spam, compensation including empty-build revival, entitlement retirement,
domain overrun/receipt eviction, bounded baselines and new recovery identity.
Copied records in these tests also pass the exact wire codec round trip.

The real overrun fixture commits 100 moves: 300 domain records, 256 retained,
44 overwritten, oldest sequence 45. Only receipts 99/100 remain in its two-entry
receipt profile. Retrying request 1 remains AlreadyProcessed and spends nothing.
A baseline at sequence 300 resumes with command 101's events 301–303 and exact
inventory. Synthetic transport stress, golden byte fixtures and counter-limit
coverage remain in the unchanged focused event suite and archived stage-a1.

| Measured size | Native | WASM |
|---|---:|---:|
| Fixed EventBaseline | 11,448 bytes | 11,384 bytes |
| Baseline allocation-failure fixture, fixed + elements | 12,216 bytes | 12,128 bytes |
| Hub including metadata | 381,400 bytes | 393,688 bytes |

These are type/owned-element measurements. They are not sampled whole-process
memory or a graphics performance budget result.

The browser journey uses actual walking, Reset button, R while movement is held,
legacy-control isolation, drained Leave/navigation, re-entry and Leave priority
over queued Reset. Both Reset paths retain world/incarnation and quiet queues.
Re-entry changes both identities. Each Leave has exactly one closure notice;
inventory remains zero. The separately loaded legacy terrain study also passes
drop/reset. `browser-package.json` inventories exact copied web/shader/WASM
inputs. The startup and journey PNGs show real placeholder visuals; their FPS
overlay is not a performance acceptance measurement.

## Preserved failures and limits

- `bazel-attempt01.log` failed the strict dangling-else warning after retry
  instrumentation. Explicit braces fixed the branch; attempt02 passes without
  suppression or disabled tests.
- Whole-application WASM compilation retains two existing warnings:
  `WorldPosition`'s implicitly deleted defaulted comparison and signedness in
  `lego_layout_cache.hpp`. The focused observation/core strict build is clean.
- Preparation/activation tests use the existing fake CPU adapter. They prove
  authority publication, not live GPU compound replacement or completed physics.
- Baselines/events remain volatile observation data. No disk/IndexedDB writer,
  storage acknowledgment, authenticated input or multiplayer observer API is
  established by this stage.
- Review has not been performed by a separate reviewer. See
  [DATA-06 review request](../REVIEW_HANDOFF.md) and the updated
  [DATA-05 transaction review request](../../DATA-05/REVIEW_HANDOFF.md).

## Reproduce

From the repository root in the project Nix shell:

```bash
bazel test -c opt //tests:game_session //tests:session_transactions //tests:session_events --test_output=errors
cmake --build build-salvage-native --target game_session_tests session_transactions_tests session_events_tests voxy_native -j 8
build-salvage-native/bin/game_session_tests
build-salvage-native/bin/session_transactions_tests
build-salvage-native/bin/session_events_tests
bash docs/validation/salvage/DATA-06/stage-b1/build-standalone.sh
/tmp/salvage-observation-tests
bash docs/validation/salvage/DATA-06/stage-b1/build-probe.sh transaction-guard
/tmp/salvage-observation-transaction-guard
```

Repeat the last pair for `retirement-guard`, `factory-faults` and
`baseline-faults`. For actual CPU WASM, use the SDK/Node paths in
`wasm-attempt01/manifest.json` and fresh output directories:

```bash
python3 scripts/validate_session_transactions_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/DATA-06/stage-b1/run-wasm-probes.py --shared-validation <fresh-shared-directory> --output <fresh-probes-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8
```

The manifests retain exact compiler commands, hashes and artifacts. Do not
overwrite frozen outputs or change exception policy, fixed heap/stack or tests
to obtain a pass. Final application logs are `native-application-attempt02.log`
and `wasm-application-attempt02.log`.

For the hardware browser journey, outside Nix, copy current `web/`, `shaders/`
and the three built `voxy_wasm.{js,wasm,data}` artifacts to a new directory.
Use `VOXY_SMOKE_GPU=gaming`, `VOXY_SMOKE_WIDTH=1920`, `VOXY_SMOKE_HEIGHT=1080`,
`VOXY_SMOKE_SALVAGE=<fresh-journey-directory>`, `VOXY_SMOKE_REPORT=<fresh-report>`
and `VOXY_SMOKE_SCREENSHOT=<fresh-startup.png>` with
`node scripts/smoke_integrated_wasm.mjs <fresh-package> salvage`. The exact command
used here is retained in `summary.json`; no source/shader override was enabled.

Next: independent DATA-05/DATA-06 review and resolution of findings, followed
by G01's complete acceptance and repository hook tests before its authorized
commit. Independent SIM-01 work has completed DATA-03/ASSET-02 prerequisites.
