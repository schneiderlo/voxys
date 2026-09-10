# DATA-05 stage-f1 — fresh local authority after recovery

Implementation: root, 2026-09-08. Extends the validated logical checkpoint and
exact replay component from [stage-e1](../stage-e1/README.md). This is a CPU local
GameSession factory with a fake preparation adapter. Shipping storage and scene
physics integration are separate work. DATA-05 and G01 remain open.

## Verified result

| Check | Final result | Evidence |
|---|---|---|
| Bazel | 89 passed, no skips/disabled | `bazel-attempt04.log`, `bazel-final-*.xml` |
| CMake | 77 GameSession + 12 journal passed | `cmake-*-attempt02.xml` |
| Strict native / undefined behavior | 89 passed, no sanitizer diagnostics | `ubsan-attempt02.xml`, `ubsan-build-attempt02.log` |
| Actual configured WASM | 89 passed, JS exceptions + Asyncify | `wasm-attempt02/manifest.json` |
| Actual factory allocation failures | 297 failures preserved the input, followed by success, in native and WASM | `allocation-attempt02.log`, `wasm-allocation-attempt02/summary.json` |
| Counter preflight | Zero allocation attempts, input retained | Same isolated fault logs |
| Native application | Built with current factory/lineage code | `cmake-build-attempt02.log` |
| Shipping WASM application | Built with current factory/lineage code | `wasm-application-attempt02.log` |

[Machine-readable summary](summary.json) records final source/build/binary hashes.
The final WASM manifests' source/tool hashes were rechecked after validation.
This is a scoped component checkpoint; DATA-05/G01 and independent review remain
open, and no gate commit is authorized by this result alone.

## Implemented transition

`SessionRecovery::restore` accepts an owning pointer to a validated, replayed
checkpoint and a fresh preparation adapter. The trusted host must have exclusive
world ownership, retire the old live session/backend, and select the latest
saved lineage. No player intent can install a checkpoint, assign a balance or
reset these counters. The API consumes its validated input only after the
complete result succeeds. A failed allocation or preflight leaves the same input
available for retry.

The factory returns `RecoveredGameSession`, containing:

- A fresh `GameSession` that accepts commands under its new token/epoch.
- The finalized, validated parent checkpoint, retaining its bounded terminal
  decisions and processed marker.
- At most three exact parent-writer records: two ordered terminal decisions and
  one admission closure. Already-closed input requires no extra records.
- The validated initial checkpoint of the new writer, including its exact first
  `RecoveryOpenedRecord` and the link to the finalized parent.

For every still-pending request, recovery preserves a known rejection reason;
otherwise it emits `Canceled`. It never prepares or executes the original
intent. Those terminal decisions retain the old caller, token, epoch, admission
generation and request sequence. Closure clears session-local undo/redo and
dormant objects without paying another refund or dismantling active parts.
History generation advances once on closure, with saturation at exhaustion;
it never wraps. At exhaustion the restored world remains readable, and new
history-bearing edits reject with `HistoryExhausted`.

Only after parent finalization does recovery advance the authority epoch,
admission generation and journal writer generation. It skips the whole previous
reserved-ID horizon, allocates a fresh token and records a new bounded 64-ID
reservation, capped at the last valid counter. New request frontiers start at
zero; old tokens cannot become current by presenting an old sequence number.
Counter exhaustion rejects before allocation. Insufficient journal control
capacity rejects without consuming input or returning a partial session.

Example: with issued counter 102 and reserved horizon 164, the new token is
165 and its new horizon is 228. The first subsequent new part gets 166. Burned
or unused counters through 164 cannot be assigned again in this lineage.

Completed balances, active part images, provenance, cargo, jobs, entitlements,
accepted revisions and the simulation tick are preserved. The sole deliberate
accepted metadata change is the profile-1 lease policy: release old local
participant edit leases. Geometry and topology/session revisions remain the
same; the changed epoch and token invalidate old command preconditions. This
single-participant profile has no new lease issuer. Imported recovered images
with a lease are rejected, so an import cannot silently revive a revoked lease.
A future cooperative lease issuer needs explicit authority records and policy.

The new `RecoveryOrigin` identifies the exact closed parent writer, coverage,
caller/generation, processed frontier, allocator horizon and cleared history
generation. It remains in later checkpoints even after the opening record is
compacted. Origin metadata cannot alias a retired token with a current part,
build, cargo or job. The new opening binds its starting revision, balance,
admission and allocator values. A late recovery opening cannot replace the
identity of an existing replay writer.

The origin depends on the finalized parent, not which intermediate retirement
cut was loaded. Replaying any prefix of the parent retirement records and then
restoring therefore converges to the same new opening record and canonical
state. Repeated recovery from each latest checkpoint advances every identity
domain without creating value.

`GameSession` now stores its actual admission generation and retired-through
marker. Commands, receipts, undo history, summaries, capture and closure use
those values. Normal construction still starts with generation/writer 1.
Construction shares private bootstrap and journal initialization helpers;
recovery sets initial values on an unpublished object rather than modifying a
live authority.

## Storage and adapter boundaries

All records and receipts remain `Volatile`. The returned parent/new-initial
pair and retirement records are a bounded logical persistence seam, not a disk
or IndexedDB transaction. SAVE must authenticate exact bytes, select the latest
lineage and persist the transition before exposing durable recovery success.
Consuming one validated pointer prevents accidental reuse of that capability;
it does not stop a trusted host from reopening an older disk file. Exclusive
world ownership and durable lineage selection remain host responsibilities.

No GPU handle, resident slot, preparation ticket or debit reservation is revived.
Factory success calls no adapter begin/activate/discard method. A fresh adapter
lifetime is mandatory; old epoch results stay stale even when their numerical
preparation generation matches the new request. This does not reconstruct an
active boat in the renderer or physics backend. SIM/BOOT supply that integration.

## Focused evidence and reproduction

The shared suite contains 77 GameSession and 12 journal cases. Nine new restore
cases cover preserved economics and identities, two ordered cancellations,
known rejection preservation, every retirement crash cut, six successive
recoveries, lease release and stale-epoch completion, malformed origin/opening
and lease revival, counter/control-capacity exhaustion, and saturated history.
All previous import/replay/compensation checks run unfiltered as well.

The isolated allocation probe replaces actual global new/new[] and aligned
allocation. Its checkpoint includes a damaged dormant starter loan, entitlement,
active paid geometry, cargo/job and two pending purchases. Each allocation is
failed in turn until restoration succeeds. Every failure must keep the exact
input pointer and its canonical state; success must consume it and return the
correct parent/fresh pair. Counter-exhaustion preflight must allocate nothing.
The replacement allocator is not linked into combined repository tests.

From the repository root, inside `nix-shell`:

```bash
bazel test -c opt //tests:game_session //tests:session_transactions --test_output=errors
cmake --build build-salvage-native --target game_session_tests session_transactions_tests voxy_native -j 8
build-salvage-native/bin/game_session_tests
build-salvage-native/bin/session_transactions_tests
bash docs/validation/salvage/DATA-05/stage-f1/build-standalone.sh
/tmp/salvage-restoration-tests
bash docs/validation/salvage/DATA-05/stage-f1/build-allocation-faults.sh
/tmp/salvage-restoration-allocation
```

The native standalone scripts enforce strict project warnings as errors plus
undefined-behavior and float-cast-overflow checks. WASM uses the shipping JS
exception/Asyncify configuration, fixed 64 MiB heap and 1 MiB stack. See the
recorded SDK/Node paths and exact commands in `wasm-attempt02/manifest.json`:

```bash
python3 scripts/validate_session_transactions_wasm.py \
  --sdk <recorded-sdk> --node <recorded-node> --output <fresh-directory> \
  --exception-mode js --asyncify
python3 docs/validation/salvage/DATA-05/stage-f1/run-wasm-allocation.py \
  --shared-validation <successful-shared-directory> --output <fresh-directory>
```

Each attempt has an exclusive directory and retains its executed runner,
commands, logs, source/tool hashes and generated binaries. The allocation
runner recompiles all six core sources from the successful shared manifest.
Earlier successful attempts precede the final imported-lease revocation guard;
use final attempt-02 manifests and final Bazel attempt-04 evidence for that
source. Existing unrelated GLM/layout-cache application warnings may remain;
strict focused builds must be warning-clean.

## Next implementation handoff

1. Implement trusted starter-entitlement retirement through GameSession. It
   must invalidate affected compensation history and dormant loan objects before
   replacement can be granted. Pending inverse actions must not retain authority
   over a retired entitlement. Keep source-of-truth changes atomic and journaled;
   no test/player snapshot setter or caller-supplied refund.
2. Test loan removal, retirement, attempted old undo/redo, replacement and retry
   behavior with exact provenance and zero duplication/value creation. Define
   the disposition of active loans explicitly rather than silently deleting
   physical geometry outside the preparation adapter.
3. Obtain independent transaction review before DATA-05 acceptance. DATA-06's
   bounded event/telemetry schema remains a separate G01 dependency.
4. SAVE must implement canonical bytes, real native/browser storage and atomic
   lineage publication; SIM/BOOT must reconstruct scene/backend state before
   playable recovery is claimed. Keep visual LOOK-01/G02 acceptance open.
5. Mark only proven plan items complete. Commit when a whole gate passes and
   `bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test` succeeds.
