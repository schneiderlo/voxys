# Expedition checkpoint and journal bytes — envelopes 1 through 3

Status: SAVE-01 component, implemented 2026-09-09. This is the canonical logical
recovery format. It is not yet a user-facing expedition save feature or proof of
a physically restored boat. See `GAME_IMPLEMENTATION_TODO.md`, SAVE-01 through
SAVE-04. Transferable designs use the separate `salvage-blueprint-format.md`.

## Entry points and ownership

`src/game/expedition/session_save.hpp` exposes `SessionSaveCodec`:

- `encodeCheckpoint(ValidatedRecoveryCheckpoint, output, issue)`.
- `decodeCheckpoint(bytes, ExpectedRecoveryIdentity, PartCatalog, issue)`.
- `encodeJournal(JournalIdentity, records, output, issue)`.
- `decodeJournal(bytes, expected JournalIdentity, output, issue)`.

The host supplies expected world/content/writer identities from its trusted save
slot and admitted content manifest. Do not take these expectations from the
file being validated. A SHA-256 digest detects damage; it grants no ownership or
authority. Do not expose checkpoint import as a player command. Only blueprint
imports are currently wired into the browser UI.

The checkpoint decoder returns an owned, validated logical image. It calls no
preparation adapter, restores no bodies, grants no IDs, changes no live world,
and acknowledges no storage. The immutable catalog must outlive the image.
Failure preserves encode outputs and decoded journal outputs. A failed checkpoint
decode returns null and leaves caller-owned state untouched. Allocation failure
reports Capacity; callers must keep their previous good save.

## Envelopes

All widths are in bytes. There is no native structure memory, padding, pointer,
text encoding, JSON number or host-sized integer in these formats.

| Offset | Checkpoint | Journal batch |
| --- | --- | --- |
| 0 | ASCII `SVSC` (4) | ASCII `SVJB` (4) |
| 4 | envelope version, u32, 1–3 | envelope version, u32, 1–3 |
| 8 | logical checkpoint body | journal identity, then record vector |
| last 32 | SHA-256 of every preceding byte | SHA-256 of every preceding byte |

Checkpoint input is bounded to 4 MiB; journal batch input to 2 MiB. Minimum
possible envelope size is 40 bytes. A valid empty journal batch is 76 bytes.
There are no permitted trailing bytes. Invalid magic, unknown envelope versions,
checksum mismatch and truncated input reject. A damaged batch is never treated
as a valid prefix.

A journal identity is world namespace (16 raw bytes), authority epoch (u64),
writer generation (u64). A batch has at most 64 records. Each record carries its
logical schema (u32, currently 1), world (16), epoch (u64), sequence (u64), tick
(u64), then a tagged payload. Sequences are nonzero, contiguous and increasing
without overflow; ticks never decrease. Every record must match the batch's
world and epoch. The independently expected writer must match exactly. Structural
record validation runs before encode and after decode. Exact predecessor/world
semantics are checked by `SessionRecovery::replay`, not inferred from a checksum.

## Scalar and container rules

- All integers are little endian. IDs/counters/frontiers/generations are u64.
  A durable ID is a 16-byte namespace followed by its u64 counter. Content keys
  add a u32 version. Values larger than JavaScript's exact-number range survive.
- Grid coordinates are signed i32 two's-complement. Cube rotation is u8.
  Resource quantities are u64. Socket IDs are u64.
- Metres positions and strength/cargo physical quantities use IEEE-754 f64;
  canonical quaternions use f32 in x/y/z/w order. Nonfinite values reject.
  Writers encode zero with a positive sign. Noncanonical input rejects either
  during domain validation or canonical re-encoding.
- Booleans are exactly u8 0 or 1. Optionals start with that same presence byte
  and contain the value only when present.
- Fixed arrays contain every element without a count. Counted prefixes of fixed
  storage contain a u32 count followed by those elements only. Unused tails do
  not exist on the wire. Vectors use the same u32-count-plus-elements rule.
- Session byte-budget fields, which are `size_t` in RAM, use u64. History's applied
  count and every bounded container count use u32. Decoding checks host capacity
  before narrowing.
- Enums use one byte and their current closed declared range. **Exception:** a
  ModuleSettings kind is retained as a raw byte because a rejected refit request
  must preserve the original malformed intent for exact retry comparison.
  Recovery checks settings in context; accepted build settings remain validated.

The reader charges vector elements and each retained refit payload against the
2 MiB logical recovery limit before growing them. Existing per-request, delta,
record, history and session bounds also apply. Retained-byte accounting is
recomputed for the current ABI; it is not stored. Consequently an extreme image
near a RAM capacity boundary can be refused on a host with larger object sizes.
The wire format itself is architecture independent; tested native/WASM fixtures
are frozen below.

## Checkpoint body, in order

1. Logical schema u32; content identity: manifest ContentKey, 32 digest bytes,
   profile u32 and profile version u32. Currently only profile 1/version 1 is
   admitted; it describes local logical recovery with volatile receipt status.
2. Accepted SessionBootstrap: world; caller participant/token; epoch;
   last-issued ID; revision; tick; inventory; session limits; workshop-enabled;
   builds vector (max 32); starter entitlements (256); cargo definitions (256);
   cargo records (256); jobs (128).
3. Admission state: generation, retired-through, admitted-through,
   processed-through, open. Allocator: issued-through, reserved-through.
4. Journal identity; optional recovery origin; covered-through;
   model-released-through; history-cleared-on-restore; receipt durability.
5. History: generation, applied count (max 32), entries prefix (32), dormant parts
   prefix (64), dormant build headers prefix (8).
6. Pending requests prefix (2); retained receipts prefix (64);
   retained journal prefix (64).

A build stores its ID, revision, owner, optional lease, parts (max 256) and
connections (max 1024). Parts retain identity, exact content version, grid frame,
owner build, health, paint, module settings and paid/loan provenance. Connections
retain identity, endpoints, kind/enabled, damage, strength and length bounds.
Cargo preserves identity, content, owner, position, orientation and optional job;
jobs preserve identity, generation, phase and accepted participant. Inventories,
processed markers, reserved IDs and retired entitlements must never be replaced
with fresh-world defaults during loading.

Pending and receipt records preserve original commands and outcomes, not just
successful edits. The history snapshot exists to validate compensation in the
journal after checkpoint coverage. Fresh authority restoration clears it; saved
undo entries are not a player-granted entitlement.

The complete field-order table is explicitly written in
`src/game/expedition/session_save.cpp`, between the frozen-schema comment and
`using Reader`. Both const writing and mutable reading use that table. Together
with the declared fixed-width types in `construction_types.hpp`, `build_model.hpp`,
`build_refit.hpp`, `session_transactions.hpp` and `session_recovery.hpp`, it is the
normative field reference. Do not serialize a new member implicitly.

## Frozen variant tags

| Kind | Tags, starting at zero |
| --- | --- |
| Command intent | 0 CreateBuild; 1 AddPart; 2 MovePart; 3 RemovePart; 4 AcceptJob; 5 Undo; 6 Redo; 7 DeliverCargo; 8 RefitBuild |
| Object transition | 0 empty; 1 BuildTransition; 2 JobTransition; 3 CargoDeliveryTransition |
| Journal payload | 0 RequestAdmission; 1 RequestDecision; 2 AllocatorLease; 3 AdmissionOpened; 4 AdmissionClosed; 5 RecoveryOpened; 6 EntitlementRetired |

Compile-time variant-table assertions prevent silently changing these meanings.
Unknown tags reject. Refit requests encode presence, parts and welds. Refit deltas
encode presence, build ID, before/after part changes and before/after weld changes.
They are reconstructed through the existing immutable bounded factories. Sorting,
no-op removal or other normalization cannot repair incoming bytes silently: the
result must re-encode exactly to the original input. Full checkpoint admission
then checks duplicate IDs, role aliases, ownership, content, balances, history,
coverage, exact retained decisions and all existing recovery invariants.

## Envelope 2: starter grants and owned part storage

Envelope 1 retains its exact existing bytes. A writer chooses envelope 2 when a
checkpoint contains starter kits, paid storage, a retained RebuildStarter request,
or a retained registration/storage-service record. Journal batches select 2 for
registration, RebuildStarter, or a storage-service decision. Batches without these
features or envelope-3 cuts still emit 1. The reader requires exact canonical re-encoding.
The logical schema, outer SVCE archive, and SVSG storage envelopes are independent;
this extension does not reinterpret an old PartInstance or blueprint.

Changes to the frozen table in envelope 2:

- Append to SessionBootstrap, after jobs: starter-kits vector (max 8), then stored
  PartInstance vector (max 64). Stored parts are paid, remain associated with an
  existing build, and retain exact identity, health, paint and settings. Their
  IDs cannot also be active, connection, cargo, role, or dormant-history IDs.
  Stored parts count toward the world's total part and byte limits.
- StarterKit: build ID; entitlement ID; immutable refit-recipe payload; u32 binding
  count (1–32), then that many durable part IDs in recipe ordinal order. Every
  recipe source ID is zero. Bindings include removed loan instances and survive
  ordinary edits. Current active loans must match their ordinal definition.
- Command intent tag **9 RebuildStarter**: BuildTarget only. The trusted registered
  recipe supplies the replacement; the player cannot attach a new free recipe.
- Journal payload tag **7 StarterKitRegistered**: caller, admission generation,
  before/after session revisions, kit, resource balance, admitted/processed
  frontiers, allocator markers. Registration changes no IDs or inventory, requires
  idle accepted ownership, and cannot replace a different existing policy.
- Append to every BuildTransition in an envelope-2 body: optional shared storage
  payload (presence u8). If present: deposited parts vector (max 32), withdrawn
  parts vector (max 32), optional StarterKit before replacement. Shared pointers
  are encoded as values, never addresses. Their owned payload is limited to
  16 KiB and the entire journal record still must fit 32 KiB.
- HistoryAction **5 Service**: increment generation, no new history entry, and
  an exact list of evicted history-entry IDs. Only entries for the serviced build
  are removed. Rebuild and stock withdrawal cannot be undone to revive retired
  grants or duplicate owned stock. Later ordinary paid edits remain reversible.

A rebuild retires every fitted old loan, issues new loan IDs under the existing
entitlement, deposits fitted paid parts without refunds, and changes the complete
ordinal binding set atomically. Repeating it produces no additional paid stock.
A normal refit may withdraw exact owned stock by source ID with no purchase debit;
its saved withdrawal image preserves the stock configuration before the edit.

Exact forward replay reruns the same planner against the prior canonical stock
and grant, then compares the delta, assigned ID range, economy and history.
Covered-journal validation reverses each transfer and build edit to reconstruct
its predecessor; it validates then-active loans against the previous grant as
well as validating the final grant. This rejects falsified paid condition,
configuration, old loan binding and extra resource credit. Fresh-writer recovery
preserves stock and grants while retaining the existing token/ID retirement rules.

## Envelope 3: exact weld cuts

A writer chooses envelope 3 only when a checkpoint retains a CutWeld request or
cut decision, or when a batch contains one. Envelopes 1 and 2 retain their exact
bytes. A later compacted/new-writer checkpoint can emit an older envelope if it
no longer retains new command/transition fields: disabled connections themselves
already exist in the original build format. Physical SVCE archives have their own
versioning; this logical extension does not activate multi-body physical loading.

- Append command intent tag **10 CutWeld**: BuildTarget (build durable ID and
  expected topology revision), then the exact connection durable ID. No geometry,
  velocity, impact impulse, inventory credit or newly assigned ID is supplied.
- Append to every BuildTransition in an envelope-3 body, after the envelope-2
  storage optional: optional WeldCutTransition (presence u8). Its value is the
  complete **before Connection**, in the existing frozen Connection field order.
  The after-image preserves all fields except enabled=false and damage=10000.
  This payload is exclusive with part images, refit deltas and storage transfers.
- A cut increments topology and session revisions once, preserves all part
  condition/provenance, stock, starter bindings, resources and allocator markers,
  and uses HistoryAction Service to retire only its build's launch undo history.
  It creates no reversible history entry. An exact retry cannot apply it again.

`BuildModel::cutWelds` is shared by session preparation and fracture compilation.
It requires existing, enabled Weld IDs and validates the resulting canonical
build. Authority checks the caller, token, epoch, expected revisions and edit
lease again at publication. Cuts may occur outside the workshop; the trusted
PreparationAdapter must check tool possession/reach and reserve every resulting
physical root before admission. A borrowed CutWeld in PreparationRequest conveys
this operation to that adapter. The current single-body cove adapter refuses it
until fragment physics and physical save integration are ready.

Forward replay requires the retained before-connection to equal the independently
held predecessor, performs the same model mutation, and verifies zero assigned
IDs, unchanged balance and exact history retirement. Covered-journal validation
matches the after-connection, reconstructs and validates the predecessor, then
reruns the same mutation. Neither a checksum nor reverse reconstruction can
authenticate a plausible earlier value erased by the cut (such as old damage)
without a trusted predecessor. Save loading remains a trusted host operation.

Fresh authority restoration cancels pending cuts without invoking preparation,
keeps committed disabled welds and every owned part, and retains existing writer
retirement rules. A logical rebuild after cutting retires all old loan fragments
and stores fitted paid parts exactly once; live fragment rescue still requires
the physical adapter and archive work.

## Replay, compaction and storage ordering

Batches are **immutable complete objects**, not an append stream. A future backend
must keep every older acknowledged batch while staging a new one. Checksum-valid
bytes alone do not prove that a disk write, flush, replacement or IndexedDB
transaction committed.

Replay begins strictly after a checkpoint's covered-through sequence. Applying
an overlapping batch, repeating already covered decisions, leaving a sequence
hole or switching writer identity rejects. A partial valid prefix may only be
represented as a separately encoded complete batch selected by the writer.

The tested compaction model uses `releaseModelWrittenPrefix` in RAM, writes a
checkpoint with its exact coverage/processed markers, decodes it and retires the
old authority before restoring. Old requests cannot pay twice. **This is not yet
a durable compaction implementation.** SAVE-02/03 must perform this ordering:

1. Capture a coherent accepted state and exact journal frontier. Save physical
   state only from its matching certified completed simulation tick (SAVE-04).
2. Stage the new checkpoint and any complete batches without replacing the last
   good generation. Verify checksums and independently expected identities.
3. Atomically publish a generation/manifest that references the complete data;
   acknowledge only after the platform's required flush/transaction completion.
4. Release only the exact acknowledged prefix and later reclaim older batches.
   An interruption before publication retains the old generation and its journal.
5. For fresh authority recovery, first retire the old physical/session owner.
   `SessionRecovery::restore` returns retired parent, new initial image and
   retirement records. Publish their lineage atomically before calling it durable
   or admitting new persistent work. A failed publication must not reopen a
   competing writer or give another fresh-world resource grant.

Current receipt status remains Volatile. Implement a separate storage acknowledgment
boundary when wiring persistence; do not rename RAM release to disk durability.
Certified body/cargo motion, rope/latch state, water time and controlled physical
resume are still SAVE-04 work. Native named-design UI also remains open.

## Compatibility and migration policy

Envelope, logical schema, content key version, profile and profile version are
separate checks. Version 1 has no legacy data migration. Unknown values are
rejected with an explicit issue, while the previous save remains available.

A future migration must have an explicit source-version decoder and deterministic
conversion into the current logical model. Run current admission, re-encode,
stage a new generation, and retain the original until durable publication. Do not
accept a new layout under schema 1, guess a missing content version, remap IDs,
refill resources, remove completed-job markers or silently strip malformed data.
Add frozen source/target fixtures and interruption tests before enabling it.

## Frozen cross-runtime fixtures

These are SHA-256 hashes of the **entire envelope**, including its stored digest.
`GameSessionSave` constructs them from public authority operations. Native and
strict JS-EH/Asyncify WebAssembly must produce the same bytes.

| Fixture | Bytes | Whole-file SHA-256 |
| --- | --- | --- |
| Purchase with ID above 2^53, stock near 2^54 and negative grid coordinate | 2439 | `962e2c472045fdebac6f218cf53a2e0b0e175871fe871e85c5259d3f629c5265` |
| Empty journal batch | 76 | `d6fa134cbb6410c3ec1e99c512a26e7ceeddeab65c49998b1a054639f5273076` |
| Connected refit, paint/settings, undo/redo and rejected malformed intent | 8156 | `60d7019f96108c91d97da4e291a55278c228835eb6fe22c266628aacf30a0ecc` |
| Envelope-3 cut with retained starter grant and paid stock | 2674 | `49704f0e7cfdc07f0d3015eccabd20d8ac7773b77724973996dafc0647d56e17` |
| Envelope-3 admission and decision for the same cut | 957 | `100155e57d6ae7b491e269af2cf0277bf39abc9271e41e2cf3785089e0689452` |

Do not update these hashes merely to make tests pass. An intentional format change
requires an explicit compatibility decision and corresponding version/migration.
