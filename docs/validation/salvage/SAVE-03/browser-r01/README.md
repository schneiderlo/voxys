# SAVE-03 — browser world storage component

2026-09-09, root, branch `codex/salvage-implementation`. Component passed; full
SAVE-03 and the save gate remain open. No screenshots were taken.

## Delivered

`web/expedition_store.js` implements world storage with an exclusive Web Lock,
a bounded single operation, full-width generations, independently expected world
identity, mandatory host archive validation and two IndexedDB replicas. Both
copies publish in one strict transaction; acknowledgment follows completion.
Quota/abort errors roll back both copies. Byte-level comparison inside the write
transaction rejects changes made during asynchronous validation or hashing.

The module handles payload damage/missing copies, refuses conflicting/unsupported
or malformed records, fences revoked owners, closes on database upgrades and
drains closing operations before releasing ownership. Key presence is checked
separately from its stored value, so present `undefined` records cannot become a
fresh world. The library is bounded to eight worlds and 16 MiB per payload.

The browser and native storage use the same SVSG v1 envelope. A frozen 75-byte
fixture with the largest u64 generation is checked in native C++, strict WASM,
Node and the actual browser. [Complete API, format, invariants and next integration](../../../../salvage-browser-save-store.md).

`web/index.html` includes the module. It performs no automatic storage work and
adds no save controls. The existing Pages copy rule packages `web/*`, including
the new module. The playable expedition is not yet connected to this backend.

## Verification

| Check | Result | Evidence |
| --- | --- | --- |
| Native outer-envelope cases, including frozen hash | 3 passed | `voxys-browser-native-format-r01.log` |
| Strict JS-exception/Asyncify WASM shared cases | 143 passed, zero skips | `wasm-format/manifest.json`, `wasm-format/tests.log` |
| JavaScript codec cases | 4 passed | `voxys-browser-world-store-unit-r03.log` |
| Existing workshop UI lifecycle | 14 passed | `voxys-browser-world-store-ui-r01.log` |
| Real IndexedDB/Web Locks cases | 19 passed | `browser-r07/report.json` |
| Real tabs/reload/browser termination | 5 passed | Same final report |
| Native/browser exact-envelope interoperability | Pass | `nativeEnvelopeInterop` in final report |
| Final source identity and whitespace | Recorded/pass | `source-hashes.json`, `git-diff-check.log` |

The native and shipping browser game binaries were not rebuilt for JavaScript-only
backend changes. Their previous passing builds remain scoped to SAVE-02. The shared
WASM recovery suite was rebuilt to verify the new native-format golden assertion.
No already passed gameplay/screenshot journey was repeated.

## Real browser evidence

The isolated harness uses a private Chrome profile on an ext4 filesystem, a
loopback HTTP origin and the actual production storage module. The final runtime
is Chrome 152.0.7977.82. Archive validators/data in this harness are synthetic and
clearly labeled; no GameSession state or game commands are injected.

The nineteen cases cover strict completion acknowledgment; a second owner;
overlapping operations/input mutation; stale requests/archive refusal; quota after
the first put; actual transaction abort; corrupt/missing copies and repair;
conflicting copies; future format refusal; two damaged copies; malformed metadata;
present undefined values; a changed record during async validation; close/drain;
eight-world capacity; maximum-width counters/overflow; revoked ownership; and
incompatible database upgrades.

The separate journey uses actual browser targets. A second tab refuses the live
world owner. Page.reload releases ownership and leaves the saved bytes intact;
closing the new owner also permits a fresh owner. Then the private browser is
killed and restarted twice with the same profile and HTTP origin:

1. Paused after the first put is queued, before the second: the previous complete
   generation 1 survives; no partial two-copy save appears.
2. Paused on transaction completion before the library's acknowledgment callback:
   complete generation 2 survives even though the caller never received success.

The runner fetches the paused script, compares it with the checked-in fixture,
records the exact source line, observes SIGKILL termination and asserts that the
save promise did not deliver acknowledgment before the crash. The final normal
browser shutdown exits successfully and its private profile is removed. These are
process-crash tests, not power-removal tests. Quota failure is deliberately injected
inside a real transaction; actual device exhaustion/eviction is not claimed.

## Corrections and retained attempts

- Browser r01 found that aborted request errors replaced the original quota
  failure with a generic Interrupted result. The transaction now preserves its
  first error; rollback still covers both stores. r02 passed the initial suite.
- Additional ownership checks added lock-revocation fencing and database-upgrade
  closure; malformed metadata now refuses before attempting payload repair.
- Browser r04 failed a harness assumption about an anonymous callback's debugger
  function name. The runner now verifies the actual script text and paused source
  line. No production behavior was changed to bypass that check.
- Presence-aware reads and publication checks fix the real IndexedDB distinction
  between a missing key and a key holding undefined. Both malformed values refuse
  fresh-world publication. r06 passed all nineteen cases and five journey stages.
- r07 also persists final process termination/profile cleanup in the report.
  Earlier successful and failed attempts are retained with their source hashes.

## Self-contained next work

The active objective remains all work in `GAME_IMPLEMENTATION_TODO.md` through
G14. This is a storage backend, not complete player-visible expedition saving.

Next implement a versioned complete session archive containing coherent SVSC/SVJB,
certified physical craft/cargo state and atomic parent-retirement/new-child lineage.
Provide its real validator to `openStore`; the isolated fixture's predicate must
never become the shipping validator. Connect one bounded save coordinator to
native and browser storage, actual game lifecycle and journal acknowledgment.

Add stable world selection/new-world creation. A missing known save must not be
replaced with fresh resources. Generate a new world only through the explicit
new-world path. Stop persistent commands on ownership loss; reconcile stored
processed markers and unobserved completion before retries. Preserve paid part
identity, starter entitlement retirement, cargo and completed jobs.

Complete SAVE-04 certified physical reconstruction/settling and actual gameplay
save/reload while launching, towing, latching, releasing and banking. Add native/
browser save UI, slot management, world export/import, migration and Windows
storage. Browser origin/profile persistence, quota, eviction and private-mode
limitations must be reflected accurately in the eventual UX. The eight-world
backend capacity requires corresponding slot management before shipping.

The previous playable preview remains
`http://127.0.0.1:38198/?experience=salvage-cove`. Its saved-design feature is already
usable; expedition progress still is not saved by that game UI. Terrain and
construction behavior were unchanged here. No full gate passed, so no gate commit
was made. Preserve prior dirty work and commit only after a complete gate and its
required repository checks pass.
