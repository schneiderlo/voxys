# DATA-05 — bounded undo/redo checkpoint

The local GameSession now compensates CreateBuild, AddPart, MovePart and
unconnected RemovePart. DATA-05 and G01 remain open: logical recovery/replay,
trusted entitlement retirement coverage and independent review are unfinished.
This checkpoint does not add game controls, working physics, or disk durability.

## Implemented authority

`Undo` and `Redo` carry a fresh request sequence, current session/build revisions,
history entry and history generation. Callers cannot supply inverse object data
or account amounts. `GameSession::history()` returns only counts and the current
undo/redo choices. The sole accepted state remains private to GameSession.

Each history entry stores the changed build header/part images, exact debit and
credit, owning participant/token and admission generation. It does not store a
whole world. Undo/redo modifies a newly prepared candidate of the current state;
unrelated jobs/cargo survive. The existing two-command ordering, reservations,
revision/lease revalidation, fake adapter activation and journal decision remain
the publication boundary.

| Original operation | Undo | Redo |
| --- | --- | --- |
| Purchase a paid part for C | Refund C; retain the same part privately | Charge C; restore that part |
| Dismantle a paid part for Y | Charge Y; restore the retained part | Pay Y; retain it again |
| Dismantle a starter loan | Restore with its exact active entitlement; zero payout | Remove with zero payout |
| Move a part | Restore exact before image | Restore exact after image |
| Create an empty build | Retain its current empty header privately | Restore the same build ID |

Part health, paint, module settings, definition/version and provenance are
preserved. Active and dormant materialization are mutually exclusive. History
images are references to that one logical object. Restoration does not allocate
another durable ID. Empty-build removal and restoration advance its retained
topology-revision high-water; revisions never go backward. The adapter now
receives an explicit `removedBuild` ID for empty-build undo, because a null
changed-build pointer also occurs for AcceptJob.

The combined history has at most 32 entries and 256 KiB of logical payload,
64 dormant parts and 8 dormant empty builds; validated smaller overrides are
available. Its fixed arrays are included in each candidate's `sizeof(State)`
charge before copying. The separate history payload budget counts occupied
entry/escrow record bytes, not serialized bytes or driver memory. No dynamically
growing history container exists.

A successful ordinary edit clears the redo suffix and may evict oldest entries
to fit. Both changes occur only in its private candidate until commit. Bounded
reachability over all surviving applied/redo entries keeps a dormant object
needed by a newer entry. Unreachable dormant IDs retire without a payout or
allocator rewind. If the new record itself cannot fit, the edit rejects.
Compensation cannot silently evict its history to fit an undersized escrow.

Reservations protect the exact inverse entry. A competing inverse or an edit
planning to evict that entry cannot acquire it. Either ordering is checked.
Positive inverse debits share the existing account reservation ledger; pending
credits remain unavailable. Cancellation, adapter refusal and exceptions release
only that preparation's reservations and preserve accepted history. A stale
second candidate rejects after the first commit.

History generations advance on successful history changes. New history IDs use
the new generation, so evicted IDs cannot be reused. Strong-counter increments
check exhaustion. Admission closure cancels pending work, revokes all local
history/escrow, advances its generation when possible, and leaves active state,
balances and issued-ID high-water unchanged. Closure's existing journal kind
implies this session-local history-clear policy; no separate economic payout is
generated. Recovery must apply the same policy through its trusted boundary.

## Verification

42 GameSession cases (28 prior, 14 new compensation cases) and 12 journal cases
pass in Bazel and CMake. The same **54 cases pass in actual WASM**, zero skips or
disabled tests, with JS exceptions, Asyncify, a fixed 64 MiB heap and 1 MiB stack.
The standalone strict GCC 15.2 build passes all 54 with undefined-behavior and
float-cast-overflow checks. These are CPU tests; Node does not prove browser
graphics or persistent storage.

New coverage includes repeated exact purchase/dismantle cycles, paid/loan
condition/settings preservation, a multi-edit undo/redo chain, unrelated accepted
jobs, dormant-build revision progression, canceled/invalid new edits, both
reservation/eviction orderings, stale second inverses, reachability after old
purchase eviction, count/byte/escrow limits, actual inverse debit reservation
failure, forged intent fields, adapter refusal/exceptions, journal account/history
transitions and closure without a second payout.

The isolated `no-allocation.cpp` replaces actual global new/new[]/aligned-new
and forbids allocation after preparation. Nine boundaries cover creation,
purchase, undoing both, redoing both, final activation rejection, then undoing
both again. **Zero allocation attempts**, eight activations, one discard; closure
also clears dormant history while allocation is forbidden. This is fake backend
publication, not a GPU fence or simulated out-of-memory device.

Both native and WASM applications compile against the changed interface. Twenty-seven
Bazel application cases and ten cove preview cases also pass without skips; four
existing application GPU cases remain disabled. The cove target includes one
actual GPU metadata/retirement check, separate from the 54 CPU transaction cases. The
WASM application retains existing warnings for the defaulted GLM sector
comparison and LEGO layout-cache signedness; the focused shared test runner and
standalone build use warnings as errors. This component does not change the
rendering path or make the workshop-disabled cove playable.

Artifacts are in this directory: `bazel-{session,journal}.xml`,
`cmake-{session,journal}.xml`, `ubsan.xml`, build/test logs,
`wasm-attempt01/manifest.json`, and `no-allocation-result.log`.
`summary.json` verifies reports and source/binary identities. The WASM manifest
freezes all tested sources/headers and checks that they stayed unchanged.

Preserved failures:

- `bazel-attempt02.log`: the first condition/settings fixture assigned channel 7
  to a passive beam. Bootstrap correctly rejected it. The corrected fixture uses
  an Engine and its default Power settings, with a valid nondefault channel.
- `no-allocation-build.log`: an old helper's pinned Nix store compiler had been
  garbage-collected. The new helper resolves `c++` inside the declared Nix shell;
  `compiler-version.log` records the actual compiler. No project bypass was used.

## Reproduction and next work

From the repository root:

```sh
nix-shell --run 'bazel test -c opt //tests:game_session //tests:session_transactions --test_output=errors'
nix-shell --run 'cmake --build build-salvage-native --target game_session_tests session_transactions_tests -j 8'
nix-shell --run 'build-salvage-native/bin/game_session_tests && build-salvage-native/bin/session_transactions_tests'
nix-shell --run 'bash docs/validation/salvage/DATA-05/stage-c1/build-no-allocation.sh && /tmp/salvage-history-no-allocation'
```

`scripts/validate_session_transactions_wasm.py` takes `--sdk`, `--node`, a fresh
`--output`, `--exception-mode js --asyncify`. Exact successful paths, compiler
flags, commands and hashes are in `wasm-attempt01/manifest.json`. The standalone
sanitizer invocation is recorded in `summary.json`; do not overwrite previous
results when reproducing.

Next read `../design.md` sections 6–8. Implement the bounded logical recovery
image and contiguous exact-delta replay before calling DATA-05 complete. Preserve
processed/admitted frontiers, queue tombstones, receipts, content/world/profile
identity, issued/reserved ID horizon and the explicit history-clear policy.
Reacquire backend resources; never restore a fake ticket as a live reservation.
Exercise trusted entitlement retirement and stale-token/recovery accounting
without a test-only snapshot/balance setter. Actual inverse refund overflow is
guarded but cannot be reached through today's linear edit-only economy without
another accepted balance writer; future reward/account writers need that fault
case and explicit history policy. No disk durability, recovery pass, independent
review, DATA-05 completion or gate commit is claimed here.
