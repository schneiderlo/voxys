# Adventure saves — first implementation

Baseline: `acd9d43d224734a633c3af5db5d6d519e6bc3e23`. This record covers the canonical adventure snapshot and storage adapters. The containing implementation commit identifies final source. Runtime bootstrap, save feedback and ordinary restart journeys require separate integrated evidence; this record alone does not close SAVE-A01 or G-A.

## Coherent snapshot

[AdventureSaveCodec](../../../../../src/game/adventure/adventure_save.hpp) owns a distinct **VXADHOME, schema 1** format. The maximum complete image is 1 MiB. Numeric fields are explicit little-endian bytes; 64-bit counters never pass through JavaScript numbers. Doubles use finite canonical encodings. A SHA-256 trailer detects damage; it does not grant game authority.

The image contains the namespace, installed content digest, epoch/revision/request and durable-ID high-water marks, starter entitlement marker, player pose/health, backpack/equipment, all structures/parts and functional components, chest contents, registered bed/recovery pose and depleted installed resource IDs. Item spending and resulting buildings always belong to one image.

Decode receives expected world identity and installed content independently. It validates bounded counts before allocation, canonical order, unique IDs, live component references, slot bounds, known content IDs and exact trailing length. Invalid input preserves the destination. A Cove payload is rejected by the new magic/schema; it is never interpreted as an adventure migration.

## Linux storage

[NativeAdventureSaves](../../../../../src/engine/platform/native/native_adventure_saves.hpp) uses the existing [NativeSaveStore](../../../../../src/engine/platform/native/save_store.hpp) and its worker. Data lives under `<root>/adventure-v1/<32-character-world-id>/`, separate from Cove storage. One worker owns the exclusive world lock. Startup may wait before gameplay; accepted saves publish immutable byte copies on that worker.

The underlying store writes and flushes two complete generations before acknowledgement. Failed publication triggers an asynchronous reopen/reconciliation. Exact newly published bytes can be confirmed only when both required replicas match. A recovered new primary with an older mirror remains visibly unconfirmed; the next Save uses the reconciled generation and repairs both copies before acknowledgement. A competing owner or unresolvable state refuses further writes visibly.

Default startup selects the most recently written existing adventure replica from a bounded scan. Either `current` or `mirror` establishes a saved world; losing `current` alone must not start a fresh adventure. Explicit new worlds preserve old worlds. Empty directories from unsaved starts do not count toward the 32-confirmed-world limit. An explicitly selected missing save refuses rather than granting new starter supplies.

Windows support remains unavailable, matching the existing native transport boundary. No Linux test is a Windows durability claim.

## Browser storage

[adventure_saves.js](../../../../../web/adventure_saves.js) reuses [expedition_store.js](../../../../../web/expedition_store.js) with the separate IndexedDB database **voxys-adventure-v1**. Existing Web Locks, strict transactions, paired copies and eight-world capacity remain in force. The optional shortcut key is `voxys-adventure-current-v1`; Cove shortcuts are never consulted.

Before the WASM world exists, bootstrap checks only the adventure magic/schema, payload bound, exact namespace and SHA-256. It then stages those bytes for full runtime restore. **Publishing remains disabled** until the runtime installs and passes the complete canonical validator, including validation of loaded bytes. Header integrity is not represented as semantic authority.

After a confirmed publication, the wrapper remembers the world if optional shortcut storage is available. Denied shortcut metadata does not change the durable outcome. Input and returned byte arrays are copied. On failure the actual generation is reloaded before another publication is permitted; loss of reconciliation disables saving. Page closure releases the owned world.

## Checks and limits

[Initial check results and historical source hashes](checks/summary.json). [Final native fault-check results, source hashes and retained artifacts](checks/native-storage-r04-summary.json).

[Native adapter tests](../../../../../tests/test_native_adventure_saves.cpp) use actual temporary directories and the actual save worker. **Six cases passed in 52 ms** after compilation. They cover paired asynchronous publication/exact reopen, foreign/missing/busy worlds, independent new worlds/default continuation, damaged-primary and missing-primary recovery with content mismatch, a real refused staging write followed by retry, and failure after publishing the new primary but before its mirror. The last case verifies the actual mismatched on-disk generations, an unconfirmed completion, and a later ordinary Save that repairs and acknowledges both copies. The injected failure uses the existing native I/O observer; production calls keep its default null observer.

[Browser adapter tests](../../../../../scripts/test_adventure_saves.mjs) passed **10 cases** with Node 22.23.1. They cover bootstrap damage/bounds, missing selected saves, canonical validation gating, independent new worlds, default continuation, failed publication/retry, denied metadata, copied ownership, invalid selectors and page closure. Their fake transport isolates adapter behavior; they are not real browser storage or gameplay journeys. The retained underlying transport tests establish its existing mechanics, while the new runtime still needs its ordinary-control restart check.

The shared [authority record](../../ITEM-A01/authority-r01/README.md) describes the 19 passing inventory/session/codec cases, including the bounded four-home, 1,024-part, 32-component workload and its exact **31,448-byte** round trip. Existing general storage crash/fault tests are reused instead of rerunning every historical failure matrix for a new payload adapter.

Manual save is the initial checkpoint policy. Accepted building, transfers, crafting and movement remain in memory until the host confirms one complete snapshot. Leaving does not imply autosave. The live runtime must show dirty progress and report failures without claiming that an accepted click was durable.
