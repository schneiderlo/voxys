# DATA-05 independent transaction acceptance

Reviewed 2026-09-12 against commit `2353dae2bf09d5570ea57c46dc3de055b8b97fd6`
and the current implementation tree. Reviewer: the independent
`presentation_review` code agent. Root independently reviewed the resulting
production correction. This is performed code review and recorded verification,
not a request for review or a claim of human review.

**DATA-05 acceptance is complete for the bounded local authority contract.**
One save-validation defect was found, reproduced and corrected. There are no
unresolved findings in the reviewed transaction scope. This does not close
DATA-06, G01, a SAVE/SIM gate, or the complete game plan. Root owns plan changes,
the required repository suite, application journeys and publication.

## Current scope and source identity

The original [review handoff](../REVIEW_HANDOFF.md) predates connected boat
refits, portable save bytes, paid-part storage, starter recovery and cut builds.
Its statements that Cove has no working workshop or storage are historical.
The current application has a real execution-bound preparation adapter and
native/browser save hosts. Authority unit tests deliberately retain a fake
adapter so failure ordering and economics can be controlled exactly.

Read the handoff, DATA-05 design and stage-g1 evidence, DATA-06 stage-b1 evidence,
plan Contracts A–D and the later refit, cut-authority and save evidence. Reviewed
the actual authority/reservation/publication paths in `game_session.*`, journal
schemas and fixed outbox in `session_transactions.*`, capture/import/replay/
factory in `session_recovery.*`, their event hooks and the immutable construction
catalog/model/refit contracts. Inspected corresponding assertions in the three
session test files. Application review was limited to the trusted adapter,
prepare/poll/stage/observed/publish/discard callbacks, joined execution
confirmation and save/restore admission boundaries; it was not another full
application review.

[Source hashes](source-hashes.json) distinguish final authority/test inputs from
the [pre-fix inspected inputs](source-hashes-before-fix.json). Application/save
controller hashes identify the inspected integration snapshot, not a promise
that unrelated application changes are included in the authority test binary.
[Evidence](evidence.json) records exact commands, manifests and retained failures.

## Finding and correction

**P2 — a valid reward could make the current checkpoint impossible to admit.**
The previous `Validator::historyStep` called checked `resources()` while walking
all hypothetical Undo/Redo states from the current inventory. A real accepted
delivery can fill either currency after a purchase, or fill material after
dismantle → Undo. The next refund must refuse overflow, but that does not make
the accepted game state invalid. The old validator incorrectly returned
`RecoveryError::InvalidHistory` for its checkpoint, blocking save/recovery.

The native reproduction confirmed purchase-Undo failure in both currencies and
dismantle-Redo failure for the catalog's real material yield. In every case the
live inverse already returned `ResourceOverflow` without changing the accepted
world, history, inventory or backend. The initial probe also attempted a
machinery dismantle case; the engine has zero machinery yield, so that unsupported
fixture precondition was removed. No catalog price or yield was changed.

The production fix changes only the validator's hypothetical history balance.
It retains a low limb and positive carry per currency during the bounded
history walk. At most 32 entries are walked twice; no dynamic allocation or
larger accepted account is introduced. Negative hypothetical balances still
refuse, and the walk must return to the exact accepted balance at the current
history cursor. Full geometry, identity, provenance, catalog debit/credit and
history reachability validation remain. Actual journal replay and live
compensation continue to use checked uint64 resource arithmetic.

`GameSessionSave.DeliveryCanBlockInverseCreditWithoutInvalidatingCheckpointOrRecovery`
uses actual accepted delivery commands for all three reachable cases. It proves
unchanged refused inverses/retries, exact checkpoint bytes and replay, fresh
recovery without paying again, and forged history-value refusal. The existing
forged-history test now also rejects an account below the retained dismantle
inverse debit after all journal detail has been compacted. Root independently
reviewed the final carry/borrow and lower-bound behavior before acceptance.

## Reviewed contract conclusions

| Contract | Conclusion and concrete evidence |
|---|---|
| Ordering and retries | Caller/token/epoch/sequence checks precede admission; admitted and processed frontiers remain separate. Exact retries reuse pending/retained results, changed intent conflicts, evicted old requests remain processed. Canceled later slots retain terminal capacity until ordered drain. `GameSessionTransactions` and receipt-eviction cases assert both frontiers and single activation/spend. |
| Delayed preparations | Candidate base session/build/history/lease is checked before activation or staging. Accepted state survives rejected/canceled preparation; another candidate cannot overwrite it. Pending positive debits are reserved against accepted funds; pending credits cannot fund another request. |
| Bounded ownership | Two pending slots, bounded candidate/history/receipt payloads and journal control/terminal reservations are acquired before publication. Generation-tagged discard is idempotent. A throwing begin releases only its own preparation; rejected queue tombstones retain their reserved decision. Backend fence retirement remains the adapter's separate responsibility. |
| Publication | Decisions and observation notices are prepared before activation; trusted activation transfers ownership, then the state swaps and the reserved journal/receipt/events publish. No untrusted observer callback runs inside this boundary. Preflight journal failure prevents commit and fails closed; volatile append is never a disk acknowledgment. Current publication/retirement allocation probes pass. |
| Economy and history | Purchase cycles use exact cost and inverse cost; dismantle cycles use exact yield and inverse yield in both supported currencies. Loans pay zero. Refit deltas retain exact paid/loan images and stored-part identities. Live overflow/underflow refuses atomically. The new reward/refund cases close the old handoff's formerly unreachable-overflow limitation. |
| Durable identities | Canonical role/ID uniqueness includes active objects, dormant history, stored paid parts, grants and retired-token origin. History reachability and 32-entry bounds preserve one materialization and monotonically new revisions. New proposals burn IDs; recovery skips the entire reserved horizon. |
| Capture and replay | Count/byte bounds precede array traversal. Trusted expected content/world identity is distinct from imported data. Replay checks writer, contiguous post-coverage sequence, exact pre-state and deltas on a private copy; it does not resubmit commands or prepare physics. Split, malformed-late-record, forged-value and role-alias tests retain the original checkpoint on refusal. |
| Recovery and retirement | Recovery closes old pending requests in order, retains known rejection reasons, clears local escrow without payout, revokes leases and creates a fresh token/epoch/admission/writer. Trusted entitlement retirement rejects active loans and selectively removes loan history while preserving paid/unrelated value. Factory failure probes preserve the reusable input capability. |
| Observation boundary | State events precede terminal receipts; entitlement retirement precedes its pending invalidations. Bounded private event publication cannot mutate authority. Overrun and baseline tests cover recovery without a second spend. This checks transaction interaction only; DATA-06's full event-schema/consumer acceptance remains separate. |
| Real refit/save integration | The application stages only against its joined owned tick; logical publication waits for exact root/parent execution evidence. Shape uploads can remain pending, and cancellation retains partial owners for retirement. Publish uses prepared swaps. Save/restore waits for its existing durable host acknowledgment before making a restored session playable. Those implementation paths are acknowledged here, not recertified as storage/fence gates. |

## Verification performed and reused

- **Final native:** all **138 GameSession cases** pass through the focused Bazel
  target, no failures/skips/disabled cases. The only subsequent test-source edit
  was the requested comment clarification; production source is identical.
- **Final configured WebAssembly:** all **199 shared authority/save cases** pass,
  no skips or disabled cases, with JS exceptions, Asyncify, fixed 64 MiB heap and
  1 MiB stack. The runner verified final source hashes remained unchanged.
- **Final native UBSan:** all three new overflow/refusal/checkpoint cases pass;
  **305 recovery-allocation failures** preserve the original input, with
  **16 successful optional-allocation fallbacks** and eventual uninjected
  success. Counter-exhaustion preflight makes zero allocation attempts.
- **Publication probes before the arithmetic-only validator correction:** native
  UBSan and configured WASM both pass three refit/Undo/Redo activations with zero
  allocation attempts, plus four entitlement-retirement boundaries with zero
  attempts. The correction does not touch those paths. The same pre-fix WASM
  factory probe passes 305 allocation failures and eventual success. These are
  scoped to their recorded source hashes; the final native factory was rerun.
- Reused the later [198-case shared WASM manifest](../../PLAY-06/two-cargo-r01/shared-wasm-manifest.json)
  and its native 137-case GameSession XML as the pre-fix baseline: all 21 reviewed
  authority/codec/test inputs match those hashes exactly. This is reuse of core
  transaction evidence, not acceptance of the parked two-cargo feature.
- No GPU tests, browser/native game journeys, screenshots, full repository suite,
  source staging, commit or publication were performed by this review.

The unchanged historical factory probe assumed its first successful restore
must be the first uninjected allocation. Current native `std::stable_sort` can
recover from an optional temporary-buffer failure. Diagnostic output confirmed
every authority property was correct; only that harness assumption failed.
The archived [adapted probe](recovery-factory.cpp) continues after successful
fallbacks and tests every later injection point, preserving all original
failure-state assertions. Original failure and corrected logs are retained.
Intermediate diagnostic `printf` and scratch WASM mixed-width `auto` errors
were probe compilation mistakes; neither was a product change or runtime pass.

## Exact plan recommendation and limits

Mark the DATA-05 parent and its remaining independent transaction acceptance
subitem complete, linking this report. Replace that subitem's old “not complete”
wording with the completed scoped review, corrected reward/history finding and
current verification. Update the DATA-05 ledger from “review remains” to
“independently accepted”; preserve all earlier checkpoint counts as historical.
The DATA-06 ledger may say its separate DATA-05 prerequisite is accepted while
leaving DATA-06's own review and G01 open.

This supplies the DATA-05 prerequisite for PLAY-02. PLAY-02 acceptance still
depends on root's final actual native/browser builder journeys and its own
listed requirements. UX-01's completed editor tools may be recorded separately;
this report does not satisfy its outstanding MECH-01/G04 dependency.

Receipts remain explicitly Volatile. Logical prefix release, private recovery
validation and pointer consumption do not independently prove latest-disk
selection, exclusive world ownership, authentication, durable lineage or actual
physics retirement. Current native/browser hosts own those separate contracts.
No human visual approval, multiplayer capacity, general fail-stop recovery,
complete SAVE/SIM/PLAY acceptance or broad gate commit is claimed.
