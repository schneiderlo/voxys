# DATA-04 implementation evidence

**20/20 standalone CPU tests pass.** GCC 15.2 reports no warnings under the
repository warning profile with `-Werror`; undefined-behavior and
float-cast-overflow sanitizers report no errors. A separate `-O3 -DNDEBUG`
compilation of the new implementation also passes with the same warnings.

Author: `simulation_production`, 2026-09-07. Source/API/tests are frozen for
independent review. This report does not mark DATA-04 complete: root owns shared
build registration, Application lifecycle integration and its validation.

## Delivered

- `src/game/expedition/game_session.hpp/.cpp`: renderer-free session authority,
  checked trusted bootstrap, typed CreateBuild/AddPart/MovePart/RemovePart/
  AcceptJob intents, privately staged accepted state, copied query snapshots,
  and a preparation adapter interface.
- `tests/test_game_session.cpp`: 20 tests with a bounded controllable fake
  adapter. The adapter exercises reservation, delayed readiness, cancellation,
  activation failure and withheld retirement; it does not simulate machinery.
- [Design and dependency decisions](design.md), [standalone build recipe](build-standalone.sh),
  [source/dependency/binary hashes](source-hashes.txt),
  [strict compile log](compile.log), [test results](tests.log),
  [optimized translation-unit compile log](compile-opt.log).

The session owns the exact catalog debit/yield, assigned object identity,
paid/loan provenance, build and session revisions, cargo and job records, and
receipts. No player command can set a balance, apply an authoritative snapshot,
grant a loan, change cargo attachment, bank cargo or complete a mission.

Ordinary paid-part removal returns the catalog salvage yield; loan removal
returns zero. This is dismantling, not compensating undo. Connected-part removal
rejects explicitly. Job acceptance produces no reward and changes no cargo.

`closeAdmission() noexcept` is a trusted lifecycle control. It rejects pending
work as canceled, discards its preparation once and permanently closes that
session. New submissions reject without consuming sequence. Receipt queries
remain available; re-entry requires a new session/token. The destructor does not
discard an already canceled ticket again.

## Meaningful test coverage

| Area | Observed assertions |
|---|---|
| Positive construction | Asymmetric/sideways add and move preserve identity, configuration and provenance; exact purchase/yield arithmetic; separate session/build revisions |
| Ownership and ordering | Foreign caller/token/epoch, stale session/build revision, gaps, busy ingress and altered-payload replay reject without a second spend |
| Receipt bounds | A two-entry window evicts details while the contiguous in-memory processed marker still rejects old requests; canceled/committed retries preserve their outcomes |
| Failure atomicity | Insufficient funds, invalid rotation, overlap, adapter capacity, failed readiness and allocation failure preserve accepted builds/accounts/cargo/jobs/revisions |
| Reservation lifetime | Four canceled but unretired fake slots fill the pool; new work rejects until retirement completes; stale-generation completion cannot activate newer work |
| Lease timing | Wrong holder/epoch and expired leases reject; a lease valid at submission but expired at the explicit next boundary cannot commit |
| Cargo/job protection | Available job becomes accepted once without reward; cargo remains unchanged; editing copied snapshots cannot mutate session state |
| Bounds/numerics | Global part/build limits, ID/session/build-revision exhaustion, fixed-tick exhaustion and credit overflow reject rather than wrap |
| Bootstrap | Duplicate object IDs, cross-world ownership, insufficient allocator high-water, unknown cargo content version, nonfinite mass, invalid quaternion/job reference and missing loan entitlement reject |
| Preview lifecycle seam | Disabled workshop has no implicit construction/reward capability; closure cancels once even while pending, cannot reopen, and old tokens cannot submit to a replacement session |
| Exception safety | An injected allocation failure returns a rejection; an unexpected adapter exception releases its reservation and propagates before any publication |

Pending work burns any newly issued IDs; they are never returned to the
allocator. Rejected/canceled operations preserve accepted world/economic state,
but may advance allocator and receipt bookkeeping. Tests compare accepted state
separately from those markers and the trusted fake clock. An unexpected adapter
exception is not an admitted completed request; its retry can run with the same
sequence, using a fresh object ID, because nothing was published.

The fixed boundary driver is deliberately explicit. `pollPreparation()` neither
advances the clock nor commits. Only `advanceOneTick()` can publish a ready fake
transaction, after rechecking revision, ownership, lease and available funds.
The production cove must not call it to imply working machine physics.

## Reproduction

From the repository root, after the existing native GTest libraries are built:

```sh
SALVAGE_CXX=/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++ \
  bash docs/validation/salvage/DATA-04/build-standalone.sh \
  > docs/validation/salvage/DATA-04/compile.log 2>&1
/tmp/salvage-game-session-tests \
  > docs/validation/salvage/DATA-04/tests.log 2>&1
```

The script accepts `SALVAGE_CXX` and `SALVAGE_TEST_BINARY` overrides. It uses
C++20, `-O1 -g`, the repository GCC warning profile, `-Werror`,
`-fsanitize=undefined,float-cast-overflow`, and `-fno-sanitize-recover=all`.
Both commands exited 0. The compile log is empty; the test log records all 20
named cases and no sanitizer diagnostics.

The additional optimized check used the same warning flags with `-O3 -DNDEBUG`,
`-Isrc -c src/game/expedition/game_session.cpp` and output
`/tmp/salvage-game-session-opt.o`. It exited 0 with an empty log. This checks the
new translation unit under optimization; it does not replace an optimized
integrated link/test or WASM compile.

## Preserved first failure

[compile-first.log](compile-first.log) passed. The first [test run](tests-first.log)
passed 17 of 18 cases; its lease test accidentally used expiry tick zero, which
DATA-03 correctly rejects as an invalid lease before a session can be created.
The fixture now uses a valid expiry of 1 with current tick 1 to test expiration.
No production validation was relaxed. Closure and exception-safety regressions
were subsequently added, yielding the current 20 cases.

## Limits and follow-up

- All receipts and processed markers are memory-only. There is no session save
  codec, durable acknowledgment, journal, persistent reward history or token
  retirement database. DATA-05 and SAVE-01/02/03 supply those contracts.
- The current policy admits one local participant and one pending transaction.
  This prevents another accepted command from changing revision during
  preparation. Revalidation exists, but there is no public test-only snapshot
  setter to manufacture concurrent authority changes. DATA-05 must test that
  case when it adds multiple transaction sources/compensation.
- The fake adapter's four slots and part limit are test resources, not a claim
  about body, shape, joint, contact, render or journal capacity. The interface
  supplies the changed build and aggregate logical counts; actual resource
  derivation/reservations belong to the compiler and SIM-06.
- Cargo definitions are trusted loose-cargo bootstrap data. Their finite mass,
  displacement and recovery metadata are owned/readable, but they are not a
  shipping asset schema or buoyancy/attachment implementation. The real static
  cove is bootstrapped with no such fixture cargo or jobs.
- The one-owner API is not network authentication. The catalog and adapter must
  outlive the session, and adapter callbacks must not re-enter it. Real device
  failure after GPU submission needs SIM-04/06 fail-stop/recovery handling.
- No GPU, browser, CMake, Bazel, save, network or human validation was run by this
  worker for DATA-04. No shared build, Application, ledger or Git edits were made.

Current source SHA-256 values:

| File | SHA-256 |
|---|---|
| `game_session.hpp` | `a4c87882f3c6fa2e2d9b4c4c55affce56fb2b6b5af92699c8554e6c42e9844f9` |
| `game_session.cpp` | `6caafb139cc651d89956678dfda76398a1bdfc3a371520ea3a50c0ac273b8360` |
| `test_game_session.cpp` | `e60804c0fab2baa1e081292e4f4951bc71dcc815fea632f36f90f2a6e2eb9d62` |
| Strict test binary | `86228aa2cc7da3d31c57300598436c9e5353ea807fc6fc6c64cef83505310419` |
