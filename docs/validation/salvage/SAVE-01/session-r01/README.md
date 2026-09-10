# SAVE-01 — portable logical expedition save component

2026-09-09, root, branch `codex/salvage-implementation`. Component passed; full
SAVE-01, SAVE-02/03/04 and their gates remain open. No screenshots were taken.

## Delivered

`SessionSaveCodec` now encodes and decodes complete validated logical recovery
checkpoints and immutable journal batches. It covers owned builds/parts/welds,
health/provenance, inventory, cargo, jobs, request history, processed markers,
reserved ID ranges, retired loan entitlements and recovery lineage. Portable
little-endian fields, fixed variant tags, bounded allocation/counts, exact schema
and content checks, SHA-256 and canonical re-encoding protect the format boundary.

Read [the format and continuation contract](../../../../salvage-session-save-format.md)
and `src/game/expedition/session_save.hpp/.cpp`. No GPU handles or native struct
memory enter the bytes. The expected identity comes from the trusted storage
host, not from a user-imported payload. This adds no save UI or storage writes.

Existing valid capture fixtures now cross encode/decode/re-encode before the
recovery tests use them. Existing journal fixtures cross the batch codec before
replay. Thus the earlier pending/cancel/lease/loan/retirement/refit/recovery cases
also exercise portable data. Seven new cases specifically check damaged and
truncated files, forged checksummed fields, duplicate IDs, exact large integers,
closed tags, changed writer identity, malformed rejected refit settings and
compacted reload with completed banking.

A logical purchase/job/delivery fixture replays every journal split through byte
encoding, intermediate checkpoint encoding, decoding and exact-delta replay. Its
balance, cargo, jobs, owned IDs and history agree with the accepted original.
Repeating covered records rejects. A separate model-compacted byte reload retires
the old token; retrying delivery with either the old token or a fresh request
cannot grant the reward again. Adapter activity is explicitly checked.

## Verification

| Check | Result | Evidence |
| --- | --- | --- |
| Optimized native application + combined test target | Pass | `voxys-save-codec-app-build-r04.log` |
| Native construction/session/cove/fixture regressions | 199 passed, no skips | `voxys-save-codec-native-tests-r04.log` |
| Shipping browser application build | Pass | `voxys-save-codec-wasm-app-r01.log` |
| Strict WebAssembly project compile and shared tests | 140 passed, no skips | `wasm/manifest.json`, `wasm/tests.log` |
| Native undefined-behavior checks | 140 passed, no diagnostics | `voxys-save-codec-ubsan-r01.log`, archived executable runner |
| Three frozen complete-envelope hashes | Identical native/WASM/UBSan | `results.json` and `SAVE_GOLDEN` lines |
| Changed-source whitespace check | Pass | `git-diff-check.log` |

The WASM runner proves C++ exceptions are enabled, uses shipping JS exceptions
plus Asyncify, a fixed 64 MiB heap, 1 MiB stack and non-aborting malloc. It records
compiler/runtime/source/artifact hashes and verifies sources did not change
while compiling. Native UBSan executes the same 140 shared cases with
`-fsanitize=undefined -fno-sanitize-recover=all`. No graphics are needed.

## Failures retained and corrected

- Native build r01: the new test read an optional receipt object as a direct ID.
  The test now asserts presence before reading the counter.
- Native tests r02: 112/113 passed. A negative-zero quaternion was correctly
  rejected by existing domain validation before the canonical comparison. The
  expected error now matches that actual validation layer; acceptance was not
  loosened.
- Combined build r03: the save dependency was accidentally added to a different
  test target. It is now attached to the combined game tests; the unintended
  dependency was removed. Combined build r04 passes.

There were no production behavior fixes needed after the initial codec build.
The passed source hashes in `source-hashes.json` identify the final implementation.

## Exact scope and next work

The latest playable preview remains the previously verified design-library
package at `http://127.0.0.1:38198/?experience=salvage-cove`. These backend-only
changes did not justify replaying its already passed screenshot-free UI journey.
Terrain, boat appearance and gameplay controls were not changed by this component.

`releaseModelWrittenPrefix` in the tests is an explicitly RAM-only compaction
model. There is no fsync, native save directory, real expedition IndexedDB commit,
power-loss evidence or durable banking acknowledgment in this component. Codec
allocation failure is handled but exhaustive allocator fault cuts were not rerun
for these new factories. Previous allocation-failure counts remain scoped to their
archived source versions.

Next, implement SAVE-02/03 generation-based storage with retained complete batches,
atomic current/backup publication, a single active writer, bounded queues and
platform error reporting. Keep expected identities outside untrusted save bytes.
Persist restored parent/new-child lineage before calling recovery durable. Release
only the exact acknowledged journal prefix; never refill restored inventory.

SAVE-04 must capture a certified completed tick including physical craft/cargo
motion, rope/latch state, modules and water time, then rebuild and settle the same
world. Keep “expedition progress is not saved yet” in the UI until this whole path
is connected and its interruption/reload/banking checks pass. Native named-design
storage/UI and remaining PLAY-02 controls are also unfinished.

Unknown format versions currently reject; v1 has no historical migration. A future
migration needs an explicit source decoder and conversion, current admission and
a newly staged generation with the old data preserved until publication. The
format document defines this entry-point policy and compaction ordering; durable
compaction and migration fixtures remain unfinished SAVE-01 work.

No full gate passed here, so no gate commit was made. Preserve the existing dirty
branch and all unrelated work. Run the repository's required gate checks and
commit only when every requirement of the corresponding gate passes.
