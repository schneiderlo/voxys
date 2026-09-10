# DATA-05 stage-e1 — imported checkpoint validation and exact journal replay

Implementation: root, 2026-09-08. This is a bounded logical recovery component.
DATA-05 and G01 remain open. No restored GameSession, shipping save format,
storage durability, physical assembly recovery or visual approval is claimed.

## Verified result

| Check | Final result | Evidence |
|---|---|---|
| Bazel | 80 passed, no skips/disabled | `bazel-attempt05.log`, `bazel-final-*.xml` |
| CMake | 68 GameSession + 12 journal passed | `cmake-*-attempt02.xml` |
| Strict native / undefined behavior | 80 passed, no sanitizer diagnostics | `ubsan-attempt02.xml`, `ubsan-build-attempt02.log` |
| Actual configured WASM | 80 passed, JS exceptions + Asyncify | `wasm-attempt02/manifest.json` |
| Actual allocation failures, native | 96 admission + 172 replay failures preserved input/authority; eventual success | `allocation-attempt02.log` |
| Actual allocation failures, WASM | Same 96 + 172; zero preflight allocation attempts | `wasm-allocation-attempt02/summary.json` |
| Native application | Built with current recovery component | `cmake-build-attempt02.log` |
| Shipping WASM application | Built with current recovery component | `wasm-application-attempt02.log` |

[Machine-readable summary](summary.json) records final source/build/binary hashes.
All final WASM source/tool hashes were rechecked after validation. This is a
scoped component checkpoint, not a completed DATA-05 task or gate commit.

## Contract now implemented

`SessionRecovery::admit` takes an independent expected world/content identity,
the trusted immutable catalog and an owned logical checkpoint. It rejects
unsupported schemas/profiles, oversized record counts and inconsistent byte
accounting before count-driven reads. It checks canonical builds and catalog
versions, participant/token roles, active and dormant identities, exact loan
entitlements, request frontiers, receipt ordering, allocator horizons and
retained journal coverage. It validates original history images and catalog
purchase/yield amounts, walks the applied history backward and the full history
forward in private records, and checks geometry and account arithmetic.

The result is a `ValidatedRecoveryCheckpoint` owning a private copy. Changing
the caller's input cannot change the validated copy. Its catalog reference must
outlive it. No live authority or backend is published. Validation reuses a
private temporary GameSession bootstrap and BuildModel checks; its adapter
cannot activate and receives no preparation requests.

`SessionRecovery::replay` takes that validated image, the exact journal writer
identity and at most 64 records. Records must start immediately after its covered
frontier, remain contiguous and keep the same world, epoch and writer generation.
Overlap and duplicate replay against the advanced result reject. Splitting a
valid suffix at any record boundary produces the same final canonical result.

Every record is checked against the private current pre-state. Decisions must
match the pending request, accepted revision, account, history generation and
exact changed object images. New identities match admission-assigned IDs;
allocator observations cannot rewind. Undo/redo binds the actual retained entry,
its direction, exact images and compensation amounts. Ordinary history eviction
must match the bounded redo deletion/oldest-entry eviction order. Build and part
materialization checks both local and global active limits. No original intent
is submitted, no new ID is allocated, and no GPU preparation or activation runs.

Replay preserves a journal-only unresolved admission as `JournalAdmitted`.
Admission records do not contain preparation progress or a rejection reason;
replay must not invent those details. A captured known cancellation/rejection
requires the same terminal decision. The later recovery policy will cancel
remaining unresolved work in order rather than reconstruct backend tickets.

After each applied record, the complete resulting image is validated again.
Old already-covered transport records may be compacted in this private output
to maintain its configured count/byte limit. This does not release the live
outbox and is not a storage acknowledgment. A malformed record or allocation
failure rejects the entire batch and leaves the input image and live session
unchanged. All receipts remain `Volatile`.

`ownedBytes` counts this runtime's logical object plus vector elements. Native
struct layout is not a portable save format: a future byte decoder must enforce
its own length limits and recompute this accounting on the destination runtime.
Expected manifest identity comes from trusted composition. These checks do not
authenticate a self-consistent altered checkpoint; SAVE/ASSET must supply real
byte integrity and verified manifest loading.

## Tests and reproducibility

The final suite contains 68 GameSession and 12 journal cases. The new 18 cases
cover damaged imports, every split of a mixed purchase/move/undo/redo/dismantle/
job/create-build history, dormant build revision high-water, damaged loans,
captured canceled second slots, admission without an assigned ID, burned
unadmitted IDs, forged late deltas, wrong writers, holes/overlap, bounded eviction
and transport compaction, closure without payout, journal counter exhaustion,
and global active-part capacity across multiple builds.

The isolated `allocation-faults.cpp` replaces actual global new/new[] and aligned
allocation. It injects failure at each allocation until admission and replay
succeed, checks exact input/live-state preservation, and checks zero allocation
attempts for malformed-count and wrong-writer preflight rejection. Its fixture
includes active geometry, a damaged dormant starter loan, entitlement, cargo,
job and undo history. This is separate from repository combined tests so the
replacement allocator cannot affect unrelated test binaries.

Run from the repository root, inside the project Nix shell:

```bash
bazel test -c opt //tests:game_session //tests:session_transactions --test_output=errors
cmake --build build-salvage-native --target game_session_tests session_transactions_tests -j 8
build-salvage-native/bin/game_session_tests
build-salvage-native/bin/session_transactions_tests
bash docs/validation/salvage/DATA-05/stage-e1/build-standalone.sh
/tmp/salvage-recovery-tests
bash docs/validation/salvage/DATA-05/stage-e1/build-allocation-faults.sh
/tmp/salvage-recovery-allocation
```

The standalone scripts enforce strict project warnings as errors and
`undefined,float-cast-overflow` sanitizers with no recovery. The shared WASM
runner enforces actual JS exceptions plus Asyncify, 64 MiB fixed heap and 1 MiB
stack, with no skipped/disabled tests. Use the recorded SDK and Node paths in
`wasm-attempt02/manifest.json`:

```bash
python3 scripts/validate_session_transactions_wasm.py \
  --sdk <recorded-sdk> --node <recorded-node> --output <fresh-directory> \
  --exception-mode js --asyncify
python3 docs/validation/salvage/DATA-05/stage-e1/run-wasm-allocation.py \
  --shared-validation <successful-shared-directory> --output <fresh-directory>
```

Each WASM directory is exclusive, preserves its exact runner/commands/logs,
binary hashes and frozen source/tool hashes. The allocation runner recompiles
all six core sources into its own retained object files using the previously
verified flags; it does not rely on temporary Nix build objects surviving.

## Preserved failures and limits

- The earlier imported-checkpoint fixture contained a split `bootstrap` token;
  it was corrected before `bazel-admission-attempt02.log` passed 69 cases.
- `bazel-replay-attempt02.log` preserves a strict compiler rejection of a test's
  potentially empty-vector indexing. The fixture now checks its count and uses
  checked access to construct the one-record corrupted closure batch.
- Earlier successful 69/77/79-case attempts are historical. Final acceptance
  requires the final 80-case source manifest and logs; their hashes must not be
  substituted for later edits.
- A final code review added a global active-part limit check before allocating
  the added part record, plus the cross-build capacity regression. Final reruns
  include that change.
- Application builds may retain existing GLM defaulted-comparison and layout
  cache signedness warnings. Focused strict builds must be warning-clean.
- These are CPU/native and CPU/WASM recovery checks. They do not exercise disk,
  IndexedDB, a live boat/GPU restore, displayed performance or final art.

## Next implementation handoff

1. Add a trusted recovery factory after successful admission/replay. Advance
   authority epoch and admission generation with overflow checks; retire the old
   admission; produce ordered cancellations for unresolved requests; clear local
   history/escrow without payout; skip the entire recorded reserved-ID horizon;
   allocate a fresh token and start a fresh writer. Preserve accepted economics
   and prior completed decisions. Do not reset counters in an existing session.
2. `GameSession` currently hardcodes admission generation 1 in its request keys,
   history entries, summary and closure. Replace those with state initialized by
   trusted creation/recovery. `IdAllocator` is not assignable: construct a fresh
   authority from the validated horizon. Never expose a player snapshot setter.
3. Keep old-token commands and old-epoch preparation completions stale. The new
   adapter must have a fresh lifetime. Existing edit leases must receive an
   explicit checked recovery policy; blindly reviving old epoch leases is unsafe.
4. Test token/epoch/generation/ID exhaustion, repeated recovery and crash cuts
   through the recovery transition. New storage records must state exactly which
   old decisions and pending cancellations they cover. Memory-only transitions
   remain volatile until SAVE verifies actual storage acknowledgments.
5. Implement trusted entitlement retirement and its history invalidation cases.
   Obtain independent transaction review. DATA-06 and other G01 dependencies
   remain separate work. Commit only when an entire plan gate passes and the
   required repository pre-commit tests succeed.
