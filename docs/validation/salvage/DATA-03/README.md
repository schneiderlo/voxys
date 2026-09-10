# DATA-03 — canonical builds and occupancy

**Current focused result: 23 of 23 CPU tests pass with strict GCC warnings and
runtime sanitizers, including the independent full wire golden fixture.**
Root's subsequent optimized Bazel and CMake integration also passes all 23
cases, and the canonical library compiles for native and WASM. See
[integrated validation](integration/report.md) and its retained logs/hashes.
The preceding 22-case runs and review below are retained separately.
No sanitizer diagnostics occurred. The worker did not change a checkbox or
commit; root recorded DATA-03 completion after the integrated checks.
This is canonical construction data, not a
running workshop, inventory service, save journal or machine simulation.

Executor: `lego_gameplay`. Date: 2026-09-07. Base revision:
`7563f61fd536df7de5d209ff35d3cd2099ebcbb6`; branch `codex/salvage-implementation`.
The three source/test files below were uncommitted additions during validation.
They are deliberately outside the completed G00 checkpoint (`7f28fab`).

## Current full wire fixture validation

The new [golden-fixture report](golden-fixture.md) records an independently
authored 509-byte SVBM fixture, its Python recipe, complete schema offsets,
exact encoder comparison and decoder semantic checks. No production encoder
generated the expected bytes. Current test-source SHA-256 is
`36f69f12d5ee451013cc7d9a45795d9f87ad10568d016c68b32003ed00da29bf`;
production model source hashes are unchanged. The run uses the final catalog
dependency `24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3`.
[Current compile log](golden-compile.log) has no diagnostics;
[current test log](golden-tests.log) records 23/23 pass. The embedded fixture
needs no runtime data-file registration in Bazel or CMake.

## Author's run and reproduction

This first table and dependency list identify the author's final focused run,
before the later catalog checked-pointer correction. They are preserved as
historical evidence and are not relabeled as the independent rerun below.

| File | Tested SHA-256 |
|---|---|
| `src/game/construction/build_model.hpp` | `e99cf8d8b6a409276195a49d1887d6ee4518683b0cc4fa5550af08a7d3746f19` |
| `src/game/construction/build_model.cpp` | `5307ee045a24f3974ec109f914eb5f8d5a4c65755a32f7286ff6308be9cd508f` |
| `tests/test_build_model.cpp` | `8de3c9ea0c3c3c5bd8ecca390dbe44160c83af77e0bbde1f45000eefd88aacdf` |
| `/tmp/salvage-build-model-tests-ubsan` | `d3c7c25df909c4b78995dac3650784408f1228fc16b51482487346b3d495ea69` |

Dependencies are the already validated DATA-01/02 implementations. Their source
hashes at this run were `construction_types.hpp` `fb1aca2e406802452d0595fac47162822d26d4af3ec343859cbf13f8957912e8`,
`construction_types.cpp` `4aa7ffdd6e608765b5245c94c53c2b54920705533283d270e5b6b4eee84ac4c8`,
`part_catalog.hpp` `8c107f72da01b065f811db557b38de9a3a014e5717b3348951cfe21a07bc8c13`,
and `part_catalog.cpp` `12ed95f3e466da436144e8e63137065c6a25279c4771228b44e0031f673ef942`.

Host: the Linux x86_64 reference described in BOOT-04. Compiler: GCC 15.2.0.
The standalone recipe uses the existing native GoogleTest archives under
`build-lego-native/lib/`; these archives must exist first. It compiles only the
three construction modules and this task's test file, with no renderer or GPU.

Actual commands from the repository root:

```sh
SALVAGE_CXX=/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++ \
  bash docs/validation/salvage/DATA-03/build-standalone.sh \
  > docs/validation/salvage/DATA-03/compile-ubsan.log 2>&1
/tmp/salvage-build-model-tests-ubsan \
  > docs/validation/salvage/DATA-03/tests-ubsan.log 2>&1
```

For another compatible Nix shell, omit `SALVAGE_CXX` to use its `g++`. The
[standalone script](build-standalone.sh) contains the exact warning list,
`-Werror`, `-O1`, undefined-behavior and float-cast-overflow sanitizer flags.
[Final compile log](compile-ubsan.log) is empty because the compiler exited zero
without diagnostics. [Final test log](tests-ubsan.log) records all 22 cases.

Failures are retained. [First compilation](compile-first.log) found an implicit
char-to-byte signedness conversion in the codec magic; a typed byte array fixes
it without suppressing warnings. [First test run](tests-first.log) passed 18 of
19 cases: the clearance fixture shortened the side outside the actual solid
intersection and incorrectly expected rejection. It now shortens the intersecting
side. Root's independent review then found same-edit part/connection ID reuse;
both directions are rejected with new atomic-state regressions in the final run.

## Independent final dependency review

`simulation_production` independently reviewed the complete implementation and
ran the same strict standalone recipe after the final catalog correction.
**All 22 cases pass**, with no compiler or sanitizer diagnostics. Model/header/
test hashes remain the three source identities above. The new `part_catalog.cpp`
dependency SHA is
`24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3`;
the independent binary SHA is
`ff5fd14d1cd6db6a96bad436f60fd19bdd289f878ca72033fa37c9fbf92366a4`.

See [independent review and exact command](review.md),
[independent compile log](review-compile.log), and
[independent test log](review-tests.log). The review identified no remaining
correctness defect. The winch-default authority ambiguity was resolved as
documented below. Its non-blocking recommendation is to add a complete externally
authored wire golden fixture before promising public schema-1 compatibility.
No source change, inventory integration or runtime acceptance is implied by
this independent report update; root's later integration is recorded above.

## API and authority boundary

`BuildModel::create(draft, catalog, issue)` validates an entire `BuildSnapshot`
and publishes a private, owned copy. The constructor is private. Parts and
connections are sorted by durable ID; each connection's endpoints are also
ordered. The input can be changed or destroyed without altering the model.
Const snapshot, solid and socket views are available for immediate CPU queries.
Successful replacement invalidates earlier views.

`replace(expectedRevision, draft, catalog)` is a trusted model operation for the
future GameSession. It requires the current and supplied revision to match the
expected revision, validates a private replacement and advances the revision
once. Invalid geometry, capacity, content, IDs or fields leave canonical and
derived state unchanged. Allocation exceptions also leave the old model intact.
Revision exhaustion rejects the edit.

An existing part ID cannot change its definition/version, owning build or
paid/loan provenance. An existing connection ID cannot change endpoints/kind.
A removed connection cannot become a part with the same ID in that edit, and
vice versa. IDs are unique across the build, parts and connections; physical
records share the build's world namespace. Stable content IDs use their own
catalog namespace.

**This API does not authenticate a player or spend inventory.** The caller must
authorize ownership and edit leases and allocate IDs through the single-owner
durable allocator. The model records an owner and optional lease holder, epoch
and expiration tick, but does not compare them to a live authenticated session.
It has no historical tombstone log: reuse after earlier removals must be rejected
by GameSession's durable allocation/high-water policy. Physical decoding is not
an untrusted blueprint import or permission to materialize inventory.

## Parts, configuration and provenance

Every part records its owning build, exact content ID/version, integer placement,
proper rotation, health in 0–10000 and sRGB RGBA8 paint. Lookup requires the exact
catalog version, and placement respects its permitted rotation mask.

Module configuration is typed and bounded. It does not change catalog mass,
forces, strength or other physical limits. All settings include an enabled bit.

| Module family | Stored settings |
|---|---|
| Engine / propeller | Control channel 0–15, power limit 0–1000 permille, reversed bit |
| Helm | Control channel 0–15, steering limit 0–1000 permille |
| Winch | Control channel 0–15, default unattached payout length in millimetres within catalog limits |
| Other existing modules | Passive settings; other fields must be zero |

The winch field is `defaultLineLengthMillimetres`: its value is the unattached
cable default. Once a rope connection exists, that connection's
`restLengthMillimetres` is the authored length authority. Applying configuration
must not reset the connected cable to its module default. Both values round-trip
independently; the fixture preserves a 500 mm default and a 5000 mm connected
length. Root approved this clarification after independent review. Wire offsets
and schema are unchanged by the field rename.

`defaultModuleSettings` selects matching defaults. Winch defaults round the
authored minimum up to a whole millimetre; a catalog interval without any valid
millimetre setting cannot produce a valid configured instance. These settings
are authored limits/intent, not live throttle, steering, cable tension or input.

Paid parts require a zero entitlement field. Starter loans require a valid
same-world starter-entitlement ID that does not alias a physical record in this
snapshot. Multiple loan parts may share one entitlement. No rescue/restoration,
salvage credit or paid-part issuance is implemented here; later inventory work
must enforce one active starter loan and zero bankable yield for loan material.

## Geometry, sockets and connections

All placement uses the DATA-01 isotropic `.02 m` integer lattice. Checked signed-
permutation transforms produce exact bounds and reject overflow, including
`INT32_MIN`. `coarsePlacementOffset` converts local X/Z stud counts and local Y
plate counts to 50/16/50 ticks and then rotates them. It does not round an existing
fine socket offset onto a coarser grid.

Solid occupancy is the union of each part's analytical, half-open boxes. A face
touch is allowed; positive-volume overlap between different parts is rejected.
Overlapping proxies within one catalog part remain one conceptual union. There
is no dense tiny-cell grid. Catalog collision and buoyancy are distinct records;
this model does not compile external collision or calculate displaced water.

Socket endpoints use `(part ID, socket ID)` and expose exact transformed frames,
clearance bounds and slot usage. All authored connections, including disabled
ones, reserve both slots. Removing a link frees its reservation. Capacity checks
cover Weld, Rope and Latch. Duplicate endpoint pairs, missing/self endpoints,
unknown kinds and mismatched family/profile/roles reject the draft.

Only Weld requires coincident socket origins, opposing local +Y normals and
aligned local +X keys: mate rotation is `CubeRotation{2}` relative to the first
socket. This preserves sideways/asymmetric mounts and `.96 m` full-brick engaged
spacing. Stud insertion is not an additional `.18 m` gap.

Only endpoints of **enabled welds** reserve mating insertion clearance. An
unconnected socket's volume is query data, not a global keep-empty solid. A
reserved volume may penetrate the paired part only where that intersection
lies inside the mate's clearance. It cannot penetrate a third part or exempt
any solid-solid overlap. A disabled weld does not provide this exception.

Rope/Latch records describe intended endpoints and limits. They do not require
coincident authored frames, merge bodies or certify a real latch capture. Live
continuous root poses, rope solver state and cargo attachment/momentum checks
belong to later simulation/session tasks. There is no hinge placeholder.

Connections store enabled state, damage 0–10000, and four finite positive strength
limits bounded by both sockets **and** both definitions. Rope records additionally
store minimum/maximum/rest length in millimetres, bounded by a 1000 m format limit
and any endpoint winch's limits/maximum force. Weld/Latch rope fields must be zero.

## Independent bounds

| Resource | Limit |
|---|---:|
| Physical parts per build | 256 |
| Connections per build | 1024 |
| Encoded input/output | 256 KiB |
| Derived solid proxies | 2048 |
| Derived socket records | 8192 |
| Broadphase/clearance candidate checks per validation | 262144 |

All limits reject before publishing. Record/byte bounds are checked before
count-driven decoding allocation. A sorted X-axis sweep skips distant solids.
The work cap counts attempted candidate checks, including repeated checks for
different mating endpoints; it is not a claim of unique physical pairs. A valid
but excessively dense/complex authoring draft can reach this cap and must be
simplified or have a later explicitly measured policy revision. These limits
are initial CPU authoring budgets, not final game performance certification.

## Frozen physical wire format, schema 1

Magic is ASCII `SVBM`. All integers are little-endian. Doubles use their exact
IEEE-754 binary64 bits; nonfinite/nonpositive connection strength is rejected.
Booleans accept only byte 0 or 1. IDs are 16 namespace bytes followed by a u64
counter; no JS Number conversion is involved. Grid coordinates are signed i32
bits and rotation is the frozen DATA-01 u8 ID.

| Fixed record | Fields in byte order | Bytes |
|---|---|---:|
| Header | Magic, u32 schema, build ID, u64 revision, owner ID, u8 lease-present, lease holder ID, u64 lease epoch, u64 lease expiry tick, u32 part count, u32 connection count | 113 |
| Part | ID, definition ID/u32 version, i32 X/Y/Z/u8 rotation, owning-build ID, u16 health, RGBA bytes, settings, u8 origin, entitlement ID | 130 |
| Settings within part | u8 kind, u8 enabled, u8 channel, u16 limit permille, u8 reversed, u32 line length mm | 10 |
| Connection | ID, A part ID/u64 socket, B part ID/u64 socket, u8 kind, u8 enabled, u16 damage, four f64 strengths, u32 min/max/rest length mm | 136 |

Absent lease fields must all be zero. Unknown schemas, enums, noncanonical
ordering, invalid fields, count/size mismatches, truncation and trailing bytes
are rejected. `encodeBuild` sorts a validated copy; `decodeBuild` rejects
noncanonical ordering rather than silently repairing it. Both preserve output
on failure. IDs above 2^53 through UINT64_MAX round-trip exactly.

Exact catalog key/version lookup detects missing/mismatched definitions. This
module does not compute or authenticate a cooked-content digest: the catalog
contract forbids silently changing metadata under an existing key/version.
Full content-manifest compatibility and save authentication remain later tasks.

## Design-only duplication

`duplicateDesign(validatedModel)` returns a separate `BuildBlueprint` type with
local part ordinals starting at one. It keeps content keys, placements, paint,
configuration and connection design parameters. It omits physical build/part/
connection IDs, owner/lease/revision, condition/damage and paid/loan provenance.
Content IDs remain because the design must identify required definitions.

The function accepts no allocator or inventory, cannot spawn/store paid parts,
and never mutates the source. No physical-import overload exists. Duplicating
a damaged or loan craft yields design intent only; constructing it later must
allocate new physical identities and debit real resources through GameSession.
Blueprint file parsing/import validation belongs to later save/content work.

## Acceptance coverage and remaining work

The current 23 tests cover all proper orientations, sideways/asymmetric sockets, exact
engaged spacing, normal/key mismatch, half-open boundaries/overflow, paired
clearance and third-party rejection, forbidden overlap, flexible-link intent,
all socket slot kinds, unknown/dangling/duplicate IDs/links, strength limits,
content/owner/provenance/config validation, atomic failed/stale/exhausted edits,
cross-kind reuse, source ownership, deterministic record ordering, lossless
large IDs, every truncation of a valid fixture, malformed bytes and output
atomicity, every independent capacity, non-authoritative blueprint copies, and
the complete independently authored schema-1 byte layout.

Required next actions: shared Bazel/CMake registrations by root and appropriate
optimized/integrated checks. The independent review's full-fixture recommendation
has been implemented; its original 22-case evidence remains unchanged.
No GPU, browser BuildModel codec, Windows, full-suite, live economy, command
receipt, physics, durable save or performance claim is made by this evidence.
