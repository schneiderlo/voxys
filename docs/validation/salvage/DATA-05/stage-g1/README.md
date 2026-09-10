# DATA-05 stage-g1 — trusted starter-entitlement retirement

Implementation: root, 2026-09-08. This implements the checked lifecycle fixture
required by DATA-05's design. DATA-05 and G01 remain open pending independent
transaction review and other G01 dependencies. Live rescue/replenishment,
shipping storage and scene physics integration remain later product work.

## Verified results

The [final machine-readable summary](summary.json) pins sources and artifacts.
All recorded final source hashes were rechecked before recording this result.

| Check | Final result |
|---|---|
| Optimized Bazel | 86 session + 12 journal cases pass |
| Native CMake | Same 98 cases pass; application links without the prior warning |
| Strict native undefined-behavior checks | 98 cases pass |
| Configured JS-exception/Asyncify WASM | Same 98 cases pass; no disabled/skipped cases |
| Actual retirement allocation guard, native and WASM | 4 guarded boundaries each; zero allocation attempts |
| Actual recovery-factory allocation failures, native and WASM | 297 failures preserve input each; eventual success |
| Shipping native and WASM applications | Both build; no new visible game/art acceptance claimed |

Native fixed storage measures 2,208 bytes per journal record and 188,376 bytes
per logical checkpoint header, within their declared bounds. Retained raw logs
and earlier attempts remain available; final results refer to attempt-02 builds
and Bazel attempt-04. The factory probe uses attempt-01 with the final sources.

## Implemented authority operation

`GameSession::retireStarterEntitlement(entitlement, expectedRevision)` is a
trusted host lifecycle surface, not a player intent or snapshot setter. It
requires the exact active entitlement and current session revision. If any
active part still uses that loan entitlement, it returns `ActiveLoans` without
changing state or discarding pending preparation. Such geometry must first go
through the ordinary authority/adapter path. Connected or otherwise protected
parts are not silently deleted by retirement.

After preflight, the authority stages a bounded history projection. It removes
only entries referencing that entitlement's loan parts, adjusts the linear
applied cursor, and prunes objects no longer referenced by surviving entries.
Unrelated paid history, dormant paid parts and usable undo/redo remain intact.
The history generation and session revision each advance once; topology
revisions, accepted geometry, account balances, cargo/jobs and allocator
horizons do not change. Exhausted counters reject rather than wrap.

The operation reserves its own journal slot before publication. Every pending
request already owns its terminal slot; closure owns separate control capacity.
A full outbox rejects retirement before changing history or authority. On
success, entitlement removal, the projected history and its exact control
record publish without allocation or backend activation. The owning thread is
non-reentrant during this boundary.

All outstanding prepared candidates copied the previous whole-state image.
Retirement therefore discards those candidates and drains their reserved
terminal decisions in order. Inverses receive `HistoryConflict`; ordinary
candidates receive `StaleRevision`. Already-known rejection/cancellation reasons
are preserved. No stale candidate can reinstall the old entitlement/history.
Logical release does not certify backend retirement: delayed fake resources
remain charged until their separate completion.

An inactive retry returns `Inactive` with no new revision, journal record or
payout. This means the entitlement is not currently active; it does not claim
an unbounded detailed history of every past retirement. Invalid identity,
closed authority, stale revision, active loans, counter exhaustion and full
journal errors occur before normal publication. An impossible prechecked
outbox invariant failure marks a journal fault and fails stop; it is not a
promise to reverse already-published memory or acknowledge storage.

## Exact journal and recovery behavior

`EntitlementRetiredRecord` records caller/admission identity, the exact
entitlement, before/after session revisions, unchanged balance, both request
frontiers, allocator observations and a bounded `EntitlementHistoryDelta`.
The delta lists invalidated history entry IDs and permanently retired dormant
part/build IDs. Counts, canonical ordering, duplicate/role aliases and unused
fixed slots are validated. No whole build or caller-supplied refund is stored.

The bounded projection is shared by authority and exact-delta validation;
it cannot install state into a live GameSession. Replay checks the active
entitlement, absence of active loan geometry, expected state/frontiers and exact
projected ID lists before applying the delta. Imported post-retirement images
cannot reintroduce the entitlement or its invalidated history while those
retirement records remain covered and retained. Self-consistent tampering after
compaction still requires SAVE's real byte integrity/lineage checks; these
logical checks are not authentication.

The control record may precede the pending requests' terminal records in the
journal. Every tested split replays to the same final accepted state. If recovery
stops after the control record, the revoked loan remains absent and unresolved
old requests are canceled by the normal fresh-session recovery policy. Retrying
or replaying a covered retirement cannot grant value or recreate its old loan.

Retirement is a world-state change even when player request frontiers do not
advance. DATA-06 event consumers must observe its session revision/journal
record rather than infer all state changes solely from player receipt counts.

## Test scope

The final shared suite contains 86 GameSession and 12 journal cases. Nine new
entitlement cases check active-loan and other preflight rejection, selective
history/paid-escrow preservation, unrelated redo, harmless retries, pending
inverse/paid-candidate invalidation with delayed backend retirement, every
journal cut and fresh recovery, forged deltas/reintroduced state, exact outbox
capacity and prefix release, exhausted counters, and replacement-bootstrap
validation across three cycles.

The replacement fixture uses trusted bootstrap admission after normally removing
and retiring each old loan. It allocates fresh IDs, preserves paid parts and
balances, and verifies old compensation cannot become current under the new
authority. This is explicitly a composition validation fixture. It does not
implement a player refill button, rescue placement, connected-assembly disposal,
a durable world-wide starter-right policy or shipping replacement machinery.
Those remain PLAY/SIM integration tasks. No player/test-only state setter was
added to make the fixture pass.

The isolated replacement-new guard checks four actual boundaries: successful
retirement with two pending candidates, inactive retry, full-outbox rejection
and closed-session rejection. Any allocation would throw; success requires
zero allocation attempts and correct canonical/adapter outcomes. The original
fresh-authority allocation-failure probe is also rebuilt with the current six
core sources because this stage extends the journal record and adjusts recovery
bootstrap copying.

## Reproduction

Run from the repository root, inside `nix-shell`:

```bash
bazel test -c opt //tests:game_session //tests:session_transactions --test_output=errors
cmake --build build-salvage-native --target game_session_tests session_transactions_tests voxy_native -j 8
build-salvage-native/bin/game_session_tests
build-salvage-native/bin/session_transactions_tests
bash docs/validation/salvage/DATA-05/stage-g1/build-standalone.sh
/tmp/salvage-entitlement-tests
bash docs/validation/salvage/DATA-05/stage-g1/build-allocation-guard.sh
/tmp/salvage-entitlement-guard
bash docs/validation/salvage/DATA-05/stage-g1/build-factory-allocation.sh
/tmp/salvage-entitlement-factory-allocation
```

The standalone builds use strict project warnings as errors and undefined-
behavior/float-cast-overflow sanitizers without recovery. The shared WASM runner
uses actual JS exceptions plus Asyncify, a fixed 64 MiB heap and 1 MiB stack,
without skips/disabled cases. Obtain SDK/Node paths from the final manifest:

```bash
python3 scripts/validate_session_transactions_wasm.py \
  --sdk <recorded-sdk> --node <recorded-node> --output <fresh-directory> \
  --exception-mode js --asyncify
python3 docs/validation/salvage/DATA-05/stage-g1/run-wasm-allocation.py \
  --shared-validation <successful-shared-directory> --output <fresh-directory>
python3 docs/validation/salvage/DATA-05/stage-g1/run-wasm-factory-allocation.py \
  --shared-validation <successful-shared-directory> --output <fresh-directory>
```

Exclusive attempt directories preserve executed runners, exact commands, logs,
binaries and frozen source/tool hashes. Earlier successful attempt-01 results
precede the final recovery copy-construction cleanup. The optimized GCC build
had warned about generated vector copy assignment into the empty fresh recovery
bootstrap, also present in stage-f1 logs. Recovery now copy-constructs that
bootstrap and moves it into the fresh image; final builds and allocation-failure
checks verify that path without suppressing compiler diagnostics.

## Next handoff

- Perform the [independent transaction review](../REVIEW_HANDOFF.md). That file
  is a review request, not evidence that review occurred. Keep DATA-05 unchecked
  until findings are resolved and its acceptance criteria are confirmed.
- Implement DATA-06's bounded typed presentation/telemetry streams. Include
  lifecycle records such as entitlement retirement in coherent snapshot/event
  ordering; event loss must never lose economic authority.
- Complete G01 only after all dependencies pass. Run the required repository
  pre-commit tests before the gate commit. This component checkpoint alone is
  not a passed gate.
- SAVE must supply canonical authenticated bytes and native/browser storage;
  SIM/PLAY must supply real machine/rescue/replenishment behavior. Visual
  LOOK-01/G02 acceptance remains open.
