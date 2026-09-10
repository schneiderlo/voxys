# Browser expedition storage — IndexedDB component

Implemented SAVE-03 component, 2026-09-09. It stores validated-by-host world
payloads and survives tested page/browser interruptions. The playable game is not
yet connected to it. Browser blueprints already have their separate working
library. Full expedition capture, physical restoration, save controls and export/
import remain required by `GAME_IMPLEMENTATION_TODO.md`.

## API and ownership

`web/expedition_store.js` exports `VoxyExpeditionStore` in browsers and CommonJS
for tests. Loading this script opens no database and starts no save operation.

- `openStore(world, validatePayload, options)` returns a promise for one world
  store. `world` is an independently chosen nonzero 32-character lowercase hex
  namespace. The required validator receives an owned copy of the archive bytes
  and must resolve true only for a compatible, coherent game archive.
- `store.load()` returns `{generation: BigInt, payload: Uint8Array, needsRepair}`.
- `store.publish(expectedGeneration, payload)` returns `{generation: BigInt}`
  only after its strict transaction completes successfully.
- `await store.close()` stops new work, attempts to abort an active transaction,
  drains its actual result, closes the database and releases ownership.
- `store.closed` and `store.busy` expose lifecycle state for the future game bridge.

The defaults are database `voxys-expeditions-v1`, database version 1. Options can
supply an environment and a separate database name for isolated tests. Never use
player-controlled database names or substitute a permissive validator in shipping
code. Both checkpoints and outer envelopes remain trusted-host storage surfaces,
not player commands.

The backend requires IndexedDB, Web Crypto and Web Locks. It obtains an exclusive
lock named `<database>:world:<world>` with `ifAvailable: true`; a second owner
receives Busy immediately. It holds that lock for the complete store lifetime.
Close, context termination or browser process death releases it. Lock revocation
fences the old store, including loss while its database is opening. A database
version change closes the old connection/owner. There is no lock-stealing fallback
in production. [Web Locks lifetime and ownership contract](https://w3c.github.io/web-locks/).

The game must stop persistent commands when store ownership is lost and recreate
fresh authority through the normal recovery path. This module cannot independently
stop a GameSession that has not yet been connected to it.

## Portable format and records

The module reads/writes the same SVSG v1 bytes as native storage:

| Offset | Field |
| --- | --- |
| 0 | SVSG magic, 4 bytes |
| 4 | Version u32, 1 |
| 8 | World namespace, 16 bytes |
| 24 | Generation u64, nonzero |
| 32 | Payload size u64 |
| 40 | Payload, 1 through 16 MiB |
| End minus 32 | SHA-256 of the preceding bytes |

All integers are little endian. BigInt and canonical decimal strings preserve
full-width generations; JavaScript Number is rejected. The codec snapshots inputs
before asynchronous hashing, checks identity/size/version/digest and returns owned
bytes. The caller cannot mutate a queued save by changing its original array.

`encodeGeneration` and `decodeGeneration` are also exported for format conformance.
The frozen fixture in `tests/fixtures/save_generation_v1.json` is 75 bytes and uses
generation 18446744073709551615. Its whole-envelope SHA-256 is
`0e0aa18596f322d5b6988fcc25fe1cf97d29794f6ff83ab77c351219b68877b9`.
Native C++, strict WebAssembly, Node and an actual browser all verify that fixture.
See `salvage-native-save-store.md` for the native envelope and file protocol.

IndexedDB contains `current` and `mirror` object stores with out-of-line world
keys. Each value has exactly `{version: 1, generation: decimalString, bytes:
Uint8Array}`. Record layout/version, metadata and envelope generation must agree.
Unknown database/envelope versions refuse loading; they do not silently select an
older supported copy.

There are at most eight stored worlds and one pending operation per store. The
world bound counts the union of keys from both replica stores, using bounded key
reads. Busy rejects overlapping operations; there is no unbounded promise queue.
World listing/removal/export controls are later integration work.

## Read, validation and atomic publication

Load reads both values **and key presence** in one read-only transaction. A present
key holding `undefined` is damaged data, not an empty slot. Only absence of both
keys yields generation zero. The host must still distinguish an explicitly new
world from a missing previously known save: an empty backend is not permission to
refill a known world's resources.

Known-layout payload corruption or a missing copy can recover through the other
checked envelope. Load chooses the highest intact generation and marks differing
copies with `needsRepair`. Equal generations with differing payloads conflict.
Two damaged copies, malformed metadata, unavailable schema or failed archive
validation refuse loading. The archive validator runs outside the transaction on
an owned copy, after envelope checks. A semantically invalid latest archive is not
silently replaced with older world progress.

Publish snapshots the caller's payload, validates the existing save, checks the
expected generation, validates the candidate archive and computes its envelope.
It then creates a read-write transaction over both stores with
`durability: 'strict'`. A browser that does not expose strict transaction durability
is refused. Inside the transaction it rechecks key presence and the exact previous
records/bytes, enforces world capacity, and puts both replicas. This catches
changes made during asynchronous hashing/validation, even by a writer bypassing
the cooperative lock.

Success follows the transaction's completion event, never an individual request's
success. An abort rolls back both stores. The module retains the originating
failure reason when later aborted requests emit generic errors, so a quota error
remains NoSpace. IndexedDB defines transaction completion and strict durability
semantics; physical hardware behavior and power-loss certification are separate
release work. [IndexedDB transaction contract](https://w3c.github.io/IndexedDB/#transaction-durability).

Close attempts transaction abortion, then waits for completion/abortion before
releasing the lock. If completion has already occurred, its real result is drained;
close does not pretend a committed write was canceled. On page or browser death,
the caller may never receive an acknowledgment: the next owner must load and
reconcile the exact stored generation and processed-request ledger.

## Verified behavior and limits

The isolated real-browser suite checks strict completion acknowledgment, ownership,
bounded overlap, input copies, stale requests, archive refusal, quota rollback,
actual transaction abortion, copy corruption/missing-copy repair, conflicting
replicas, unknown versions, two damaged copies, malformed metadata, present
undefined values, changes during asynchronous validation, close/drain, eight-world
capacity, full-width counters, revoked ownership and database upgrades.

A separate real-tab journey verifies competing tabs, Page.reload, actual tab close,
and two browser process kills on a private ext4 profile. One kill occurs after the
first put is queued and before the second; the old generation survives. The other
occurs after the transaction commits but before the caller receives acknowledgment;
the new complete generation survives. The runner records the exact paused source
location, confirms SIGKILL, confirms no acknowledgment was delivered, and restarts
the same browser profile at the same HTTP origin. No screenshots or GameSession
mutations are used. Synthetic archive validation is clearly labeled.

These tests do not prove power-loss behavior, physically exhausted quota, browser
storage eviction immunity, private-mode persistence or physical world restoration.
The module persists by browser profile/origin, not across devices. It does not
request persistent-storage permission or claim cloud backup.

## Next integration

1. Implement a bounded complete session archive containing coherent SVSC/SVJB,
   certified physical state, and retired-parent/new-child lineage. Supply its real
   validator here; never use the test's synthetic validator in the game.
2. Add stable world selection/new-world creation and a single save coordinator.
   Distinguish missing known saves from explicitly new worlds. Never replace
   owned inventory or retired entitlements with fresh-world defaults on resume.
3. Tie journal-prefix release and durable receipts to observed storage completion.
   Handle ownership loss and uncertain/unobserved completion before admitting
   more persistent work. Complete native Windows storage too.
4. Implement SAVE-04 physical capture/reconstruction and actual gameplay save/load
   during launch, towing, latching, release and banking. Add native/browser controls,
   slot management, migration and world export/import with validation before any
   replacement. Keep expedition progress saving visibly unfinished until this
   complete path passes.
