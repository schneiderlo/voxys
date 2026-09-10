# SAVE-02 — Linux native file storage component

2026-09-09, root, branch `codex/salvage-implementation`. Component passed; the full
SAVE-02 task and SAVE gate remain open. No screenshots or gameplay injections.

## Delivered

`NativeSaveStore` creates a private absolute save slot, holds a process-exclusive
writer lock, reads complete checksummed generations and publishes two independent
copies before acknowledging. File writes, file flush/close, atomic replacement
and directory flush are explicit. Revision conflicts, exhausted counters, invalid
paths, corrupt files, unsupported versions and storage errors refuse publication.
A failure after replacement may have started reports uncertainty and prevents
further writes until the host closes/reopens and reconciles the stored state.

The latest acknowledged data exists in both `current` and `mirror`; a new update
stages `next` and preserves the prior copy until its replacement is complete.
Loading chooses the highest intact generation, reports differing copies through
`needsRepair`, and never interprets two damaged copies as a fresh world. Matching
generations with differing payloads reject. Staging links cannot redirect a write
into an unrelated target.

SVSG v1 is a shared native/WASM envelope for an independently validated payload:
world ID, full-width generation, bounded size, complete payload and SHA-256.
See [the exact API, file protocol, failure handling and continuation requirements](../../../../salvage-native-save-store.md).

## Verified evidence

| Check | Result | Evidence |
| --- | --- | --- |
| Optimized native application and combined tests | Build passes | `voxys-native-store-app-build-r02.log` |
| Native construction/session/cove/storage regressions | 211 passed, zero skips | `voxys-native-store-ext4-tests-r02.log` |
| Independent CMake storage target | 12 passed, zero skips | `voxys-native-store-cmake-build-r03.log`, `voxys-native-store-cmake-ext4-r03.log` |
| Strict JS-exception + Asyncify WebAssembly | 143 shared tests passed | `wasm-r02/manifest.json`, `wasm-r02/tests.log` |
| Shipping browser application | Build passes | `voxys-store-generation-wasm-app-r02.log` |
| CMake authority manifest consistency and target | Pass after missing source restored | `voxys-native-store-cmake-config-r03.log`, `voxys-native-store-cmake-authority-r01.log` |
| Final source identity and whitespace | Recorded/pass | `source-hashes.json`, `git-diff-check.log` |

The nine native storage cases cover mirrored generations and stale requests,
latest-copy corruption recovery, process exclusion, link safety, failures at all
16 I/O boundaries, actual SIGKILL at all 16 write boundaries, injected ENOSPC/
EDQUOT/EACCES/EROFS, conflicting/wrong-world replicas, and actual encoded-session
close/reopen. The last case purchases a part through GameSession, writes its SVSC
checkpoint, closes the file store and session, reloads real files, restores a new
local authority, verifies the exact paid part and balance, and rejects the old
purchase request. It does not publish new physical/recovery lineage to the game.

Each of the final native and CMake suites performs 16 killed-writer cuts and 16
I/O-failure cuts. Every recovered file is a complete old or complete new generation;
reopening also proves that a dead process does not leave a live writer lock.
Every repaired publication ends with matching intact replicas. The checkpoint
purchase test preserves economic data across actual file close/reopen.

Final file tests ran with `VOXY_STORE_TEST_ROOT` under the workspace's **ext4**
mount; see `filesystem.txt`. Tests remove their own fresh directories. Earlier
runs used `/tmp`, which is **tmpfs** here; their logs are retained but are not disk
persistence evidence. SIGKILL tests process death, not power removal. Space/quota/
permission failures were injected at the real I/O boundary; the drive was not
filled and no hardware flush guarantee is claimed.

## Corrections and preserved failures

- Focused native build r01: GCC could not prove vector bounds in two malformed-file
  tests. Checked access now makes those test mutations explicit. Production parsing
  was not loosened. Focused r02 passed 9 storage + 3 envelope cases.
- WASM r01: overloaded read/write integer helpers became ambiguous with 32-bit
  `size_t`. Distinct helper names fix the portability error. Strict and shipping
  WASM r02 pass; earlier failed logs remain.
- CMake r01: the old local build cache referred to a Nix make/compiler path that
  no longer exists. A fresh configured build uses the current toolchain.
- Fresh CMake r02 identified an existing source-list mismatch: the authority
  manifest already required `physics/authored_body_frame.cpp`, but the CMake
  headless source list omitted it. That source is now included. Configuration,
  the focused storage target and the headless authority target all pass. No
  manifest entry was removed to bypass the check.

## Self-contained continuation

The active goal is still the entire `GAME_IMPLEMENTATION_TODO.md` through G14.
This checkpoint implements real **Linux local filesystem storage**, not a complete
expedition persistence system. Windows returns UnsupportedPlatform. The browser
currently persists blueprints only. The portable SVSG wrapper is available to the
next browser world-store implementation, but no new world-save control is wired.

Next work:

1. Implement equivalent browser generation storage with IndexedDB transactions,
   bounded writes, exclusive world ownership, revision checks and error reporting.
2. Define the domain-validating complete session archive inside SVSG. Preserve
   coherent SVSC/SVJB data, independently expected world/content/writer identities,
   certified physical state and parent-retirement/new-child lineage together.
3. Connect a single bounded save worker to actual game lifecycle and journal
   acknowledgment. Preserve initial resources only for newly created worlds;
   never refill a restored world. Uncertain publication must reconcile before
   more persistent commands are admitted.
4. Add Windows storage and native save/load/error controls. Implement SAVE-04
   physical reconstruction/settling and actual launch/tow/latch/bank/save/reload
   journeys. Until these pass, keep expedition progress saving visibly unfinished.

The native adapter's payload is deliberately opaque; it cannot certify game
semantics on its own. Its mirrored latest-generation protocol does not implement
historical saves or immutable journal-file compaction by itself. Durable journal
receipts, migration fixtures, physical restore, exhaustive allocation-failure
cuts, actual disk exhaustion, power-loss and Windows validation remain open.

The previous playable browser package remains at
`http://127.0.0.1:38198/?experience=salvage-cove`. Terrain and workshop gameplay
were unchanged, so their already passed UI journeys were not repeated. No full
gate passed and no gate commit was made. Preserve all prior dirty work; commit
only after a full gate and its required repository checks pass.
