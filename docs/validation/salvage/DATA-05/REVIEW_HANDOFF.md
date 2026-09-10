# DATA-05 independent transaction review request

This is a review handoff, not a performed review or task acceptance. Root
implemented the current changes. DATA-05/G01 remain unchecked until a separate
reviewer examines the actual code and evidence. No conversation context is
required; the repository and linked records are the authoritative inputs.

## Product and scope

The expedition game is build machines, explore and salvage. Its local
`GameSession` is the sole inventory/build/job authority; the current preparation
adapter is a deliberately fake CPU backend. The static cove does not enable a
working workshop. This task establishes safe bounded transactions and recovery
before physics/compiler/UI integration. No shipping save bytes, native disk or
browser IndexedDB durability, authentication service, multiplayer capacity or
visual quality is certified here.

Read `AGENTS.md`, `README.md`, `GAME_IMPLEMENTATION_TODO.md` Contracts A–D and
DATA-01–06, `docs/validation/salvage/DATA-05/design.md`, and the latest
`stage-g1/README.md` / `summary.json`, then
`../DATA-06/stage-b1/README.md` / `summary.json`. DATA-06 now integrates a private
observation hub into the same authority boundaries. Stage-b1 is the latest
122-case validation of these sources. Earlier stage reports describe their own
frozen source hashes, not necessarily current binaries.

## Source to inspect

- `src/game/expedition/game_session.hpp/.cpp`: two-slot authority, resource and
  candidate reservations, preflight, ordered rejection/commit, exact
  compensation, history/escrow retention, trusted entitlement retirement,
  token closure and construction-time recovery initialization.
- `src/game/expedition/session_transactions.hpp/.cpp`: strong counters, typed
  record schemas, fixed-memory outbox, reserved terminal/control capacity,
  contiguous sequence assignment, exact model-prefix release and new retirement
  / recovery-origin records.
- `src/game/expedition/session_recovery.hpp/.cpp`: coherent capture, owned import
  validation, exact post-coverage replay, fresh-authority factory and bounded
  entitlement-history projection.
- `src/game/construction/build_model.*`, `construction_types.*`, `part_catalog.*`:
  canonical geometry/IDs/provenance and immutable purchase/yield inputs.
- `tests/test_game_session.cpp`, `tests/test_session_transactions.cpp`, and each
  stage's standalone allocation probes. Verify assertions and exercised paths;
  test counts alone do not prove the contracts.

## Required review questions

1. Can any admitted/canceled/stale/retried request spend or materialize twice?
   Check exact intent matching, separate admitted/processed frontiers, receipt
   eviction and the retained canceled second slot while the front is pending.
2. Can a delayed candidate overwrite a newer accepted account, job, build,
   entitlement or history? Check final revision/history/lease validation,
   positive-debit reservations, pending credits and invalidation order.
3. Are all candidate/backend/terminal/control reservations acquired before
   publication, bounded in count and bytes, and released exactly once on every
   exception/rejection/cancellation path? Distinguish logical release from
   backend retirement/fence completion.
4. Does the non-reentrant publication boundary allocate, throw, or call an
   untrusted callback? Examine the real replacement-new probes and the fake
   adapter's noexcept activation/discard contract. Unexpected outbox invariant
   failure must fail stop and never be presented as disk durability.
5. Are buy undo/redo `+C/-C` and dismantle undo/redo `-Y/+Y` exact for both
   currencies? Do full object images and paid/loan provenance survive? Loan
   dismantling and retirement must pay zero. Check overflow/underflow and
   unreachable-branch limitations recorded in stage-c1.
6. Is one logical ID ever both active and dormant, reused after eviction,
   assigned to another role, or revived with an old topology revision? Check
   bounded history reachability, empty-build compensation, issued/reserved
   horizons and the full 32-entry stress fixture.
7. Does imported data reject bad counts/roles/catalogs/history/frontiers before
   unsafe reads/publication? Does replay apply exact validated deltas once,
   without executing intents? Test every split, wrong writers, overlap/holes,
   forged pre-state and a malformed late record after a valid batch prefix.
8. Does recovery finalize old pending requests in order, retain known rejection
   reasons, preserve completed economics, clear local escrow without payout,
   skip the full reserved horizon and advance token/epoch/admission/writer?
   Inspect crash-cut convergence and explicit local lease revocation, including
   imports that try to revive a lease. No old backend ticket may become fresh.
9. Does trusted entitlement retirement reject while active loan geometry
   remains, preserve paid geometry/inventory and unrelated undo/redo, retire
   exactly the affected history/escrow and invalidate every stale whole-state
   candidate? Verify its exact journal delta and retry/capacity behavior.
10. Are limits honest? Receipts remain Volatile; model-prefix release is not
    persistence. Consuming a validated pointer does not enforce latest-disk
    lineage selection. World ownership, fresh adapter lifetime, authenticated
    storage and physical rescue remain explicit integration responsibilities.
11. Do the new observation hooks preserve the transaction boundary under ring
    overrun/invalid publication/exhaustion? Inspect prebuilt notices, state then
    terminal publication, and retirement before ordered invalidations. Creation
    and recovery now require a fresh public incarnation; validation alone uses
    `validateInitialState` without a journal or observer hub. Current actual
    factory failure coverage is 294 cuts per runtime, rather than stage-g1's
    historical 297. The new hub allocation is included in stage-b1's probes.

## Evidence to reproduce

Run the current focused suite in the project Nix shell:

```bash
bazel test -c opt //tests:game_session //tests:session_transactions //tests:session_events --test_output=errors
cmake --build build-salvage-native --target game_session_tests session_transactions_tests session_events_tests -j 8
build-salvage-native/bin/game_session_tests
build-salvage-native/bin/session_transactions_tests
build-salvage-native/bin/session_events_tests
bash docs/validation/salvage/DATA-06/stage-b1/build-standalone.sh
/tmp/salvage-observation-tests
bash docs/validation/salvage/DATA-06/stage-b1/build-probe.sh retirement-guard
/tmp/salvage-observation-retirement-guard
```

For actual CPU/WASM JS exceptions + Asyncify, use the current shared runner and
SDK/Node arguments recorded in DATA-06 stage-b1. Use fresh output directories. Do not
substitute native-WASM exceptions, disable Asyncify, skip tests, raise the fixed
heap/stack, or cite an old source manifest for a new edit.

Prior dedicated stage-c1/d1/e1/f1 probes preserve publication and allocation
failure evidence for their frozen implementations. If reviewing a changed path,
recompile an adapted copy of the applicable probe with all seven current core
sources into a fresh output directory and record the new source hashes. Old
creation/recovery signatures require an explicit fresh incarnation now. Stage-b1
already includes current transaction, retirement, factory and baseline probes.
Do not overwrite history or represent an old probe as current validation.

## Review deliverable and acceptance

Write a separate review report with the reviewed source hashes/commit base,
findings ranked by practical failure, concrete reproduction or reasoning,
validation performed and remaining limitations. Mark resolved findings only
after checking the final fix. State whether DATA-05's acceptance criteria are
met; keep product SAVE/SIM/PLAY work separate. Do not manufacture human review,
physical-fence evidence or a successful gate from this request document.

DATA-06 remains another G01 dependency. A gate commit requires all dependencies
and `bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test` to pass.
