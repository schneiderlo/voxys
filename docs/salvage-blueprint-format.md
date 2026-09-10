# Saved boat blueprints, version 1

This is the implemented **design-only** format used by the cove's browser library. It is a SAVE-01 / PLAY-02 component. It does not encode an expedition checkpoint, physical ownership, material balance, cargo state or banking receipt. Those require the separate GameSession recovery format and SAVE-02–04 integration.

## Canonical binary payload: SVBP

All integers are little endian. Signed coordinates use two's-complement `int32`. Floating-point strengths use their IEEE-754 binary64 bits. There is no native struct serialization, padding, JSON number conversion or hash-map ordering. The maximum payload is 128 KiB; at most 256 parts and 1,024 connections are admitted. The present cove consumer has a smaller 32-placement/64-total-connection scene limit, including scenery.

| Field | Bytes | Meaning |
|---|---:|---|
| Magic | 4 | ASCII `SVBP` |
| Schema | 4 | `uint32`, exactly 1 |
| Part count | 4 | `uint32`, 1–256 |
| Connection count | 4 | `uint32`, 0–1,024 |
| Part records | count × 59 | Layout below |
| Connection records | count × 70 | Layout below |
| Checksum | 32 | SHA-256 of every preceding byte, including magic/version/counts |

Each 59-byte part record contains, in order: local ordinal (`uint32`); definition namespace (16 bytes); definition counter (`uint64`); definition version (`uint32`); position x/y/z (three `int32`, in .02 m lattice ticks); proper cube rotation index (`uint8`, 0–23); paint RGBA (four `uint8`); settings kind (`uint8`); enabled (`uint8`, exactly 0 or 1); control channel (`uint8`); output limit (`uint16`, per mille); reversed (`uint8`, exactly 0 or 1); and default rope length in millimetres (`uint32`). Typed settings still pass the current part catalog's canonical validator.

Each 70-byte connection record contains: endpoint A part ordinal (`uint32`) and socket ID (`uint64`); endpoint B ordinal/socket in the same layout; connection kind (`uint8`); enabled (`uint8`, exactly 0 or 1); tension, shear, bending and torsion limits (four binary64 values); minimum, maximum and rest rope lengths (three `uint32`, millimetres). Connection rules, socket matching, bounds and strength limits pass the existing canonical validator. Part condition and connection damage are absent by design.

Part ordinals are exactly 1..N. The encoder sorts input parts by ordinal, normalizes endpoint order and sorts connections by endpoint pair. Decode checks the checksum and exact declared byte size before allocating record vectors, then validates against admitted content and compares canonical re-encoding byte for byte. Unknown versions, unavailable definitions/versions, duplicate/gapped ordinals, noncanonical order, invalid typed settings, corrupt/truncated/trailing bytes and oversized counts are rejected. Failed encoding/decoding leaves its caller's output unchanged.

The private temporary IDs used for domain validation never leave the codec. There are no owned build/part/connection IDs, owner tokens, world counters, paid/loan provenance, starter entitlements, health, inventory, jobs or physical handles in this format. `BuildBlueprint` cannot be converted into a physical build or inventory grant. A cove import must produce a design proposal and go through normal GameSession refit admission and costs at Launch.

Paint data is preserved in the blueprint/runtime design overlay and canonical refit. The current cove mesh renderer does not yet apply general paint overrides; this format is not a claim of completed painting controls.

## Export envelope

A `.voxy-design.json` export has exactly three properties:

```json
{"version":1,"name":"Harbor tug","blueprint":"<lowercase hex of the complete SVBP payload>"}
```

The name is trimmed, nonempty, at most 96 UTF-8 bytes, and rejects C0 controls, DEL and unpaired UTF-16 surrogates. Names are mutable user labels, outside the binary design checksum. The version must be 1; unknown properties, invalid names/hex and oversized files reject. The whole text/file admission limit is `2 * 128 KiB + 1024`; native/C++ independently enforces the binary bound and checksum after hex decoding. Definition/socket counters stay binary/hex throughout JavaScript and remain lossless above 2^53. Import validates before adding a new saved row; it does not replace the workshop or current world automatically. Duplicate names require a different name.

Version 1 has no implicit migration, guessed missing content or fallback decoder. Future migration must be explicit, preserve the old record/backup, and validate the migrated design before replacing it. A full expedition save must use a distinct magic/schema and must not interpret a blueprint as proof of ownership.

## Browser store

`web/design_library.js` opens origin-scoped IndexedDB database `voxys-blueprints-v1`, schema 1, with `designs` and `backups` object stores keyed by `id`. Each row contains a random library UUID, name, binary payload as lowercase hex, and a nonzero decimal `uint64` revision string. These are library identities/revisions, unrelated to world IDs and topology revisions. The library allows 32 current designs and one backup per design. Reads request at most 33 rows so over-capacity state is detected with a bounded row count.

A new save, update, rename, duplicate, restore or removal uses one read/write transaction requesting **strict durability**. All byte/content/name/revision checks precede writes. Updating stores the old row as a backup and writes the replacement in the same transaction. The revision must still match the UI's observed value, preventing a stale tab from silently overwriting a newer save. Success is displayed only after `transaction.oncomplete`. Aborts/quota/errors leave the old current row and backup together. Restoring a valid backup can repair damaged design bytes while retaining a valid revision/identity record. Arbitrarily destroyed store keys/revision metadata are not certified recoverable by this component.

Browser library state persists across reloads and Leave. It belongs to the same browser profile and origin; moving the local preview to a new port changes the origin. Export/import is the transfer path. Browser eviction, actual filesystem exhaustion/power-loss testing, native disk storage, and durable expedition receipts remain SAVE-02–04 work. Injected quota tests exercise real transaction rollback, not proof of physically exhausting a device.

## Applying a saved design

`CoveWorkshop::loadBlueprint` decodes against the installed catalog, reserves all exact matches among accepted owned slots, then reuses remaining same-definition owned parts. Desired extras receive unused dynamic slots and become paid additions at Launch. Removed starter-loan slots are never revived by import; their replacement must be bought. An old blueprint cannot reissue its former physical identities or entitlements.

The cove accepts enabled welded layouts with the authored weld strengths; advanced link types and custom weld strengths reject explicitly. The complete candidate passes assembly compilation before replacing the local kept design as one undoable edit. It does not alter the sailing boat or spend inventory. Final Launch still validates navigation and module consumers, reserves the net cost, performs a future-tick physical replacement and publishes only after confirmation. Named designs do not bypass capacity, stock, ownership, collision or launch rules.

See [the verified browser library checkpoint](validation/salvage/PLAY-02/designs-r01/README.md) for commands, exact tested sources/package, fault checks and the actual reload → load → purchase → sail journey.
