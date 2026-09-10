# DATA-05 — coherent checkpoint capture and RAM transport

This checkpoint adds the owned logical recovery representation and captures it
from the real GameSession. It also exposes exact journal-prefix release to the
trusted RAM storage model. **There is no checkpoint loader or journal replay
yet. DATA-05/G01 remain open.** No save file, browser database write, crash-safe
acknowledgment or restored GPU resource is claimed.

## Authority and representation

`src/game/expedition/session_recovery.hpp` is the trusted composition/storage
surface. It is separate from player intents and the browser bridge. `capture`
must run on the owning thread between authority operations. Its implementation
lives beside GameSession's private State/Pending definitions in `game_session.cpp`;
it does not gain a writable snapshot setter or execute a command.

The image has a versioned logical schema and contains:

| Group | Captured facts |
| --- | --- |
| Content/profile | Manifest content key and 32-byte digest; local volatile profile/version |
| Accepted authority | World, participant/token, epoch, session/build revisions, tick, account, validated builds, cargo definitions/records, jobs and active starter entitlements |
| Admission | Open/closed generation, retired-through generation, contiguous admitted and processed request frontiers |
| Pending interval | Up to two exact commands in order, Preparing/Ready/RejectReady/CancelReady state, assigned logical IDs, typed error/object details, admission-journal identity and observed next sequence |
| Terminal details | Up to 64 retained exact commands and outcomes, typed errors, resulting revisions/object, observed next sequence, journal identity and explicit Volatile durability |
| Allocation | Issued cursor and reserved-through horizon, including burned/canceled proposal IDs |
| Coverage | Writer identity, covered journal frontier, model-released frontier and the exact still-retained journal prefix |
| Temporary history | Bounded history entries, exact object/account images, cursor/generation, dormant parts and dormant build revision high-water |

The content identity is supplied by the trusted host from its verified manifest.
Capture rejects empty identities and unsupported profile/version; it does not
independently hash a PartCatalog or authenticate a supplied digest. The tests use
explicitly synthetic manifest identities. Production manifest binding remains
ASSET-07/SAVE work, and a future loader must compare with independently expected
content/profile identity before publishing a recovered authority.

History is included to validate later compensating journal deltas. The mandatory
`historyClearedOnRestore` policy prevents this export from promising persistent
undo. A restored session must discard history/escrow before opening its fresh
admission, without another refund or ID reuse. Backend tickets, prepared candidate
models, borrowed pointers, diagnostic string views, fake adapter readiness as a
live reservation, and GPU handles are excluded. Ready means what was observed
before the crash; it never authorizes restored activation.

`ownedBytes` accounts for the fixed checkpoint object plus copied vector elements.
Preflight checks all count-driven storage before allocation, with a hard 2 MiB
maximum and smaller test overrides. It is an in-memory representation, not a
portable byte format, allocator overhead measurement or SAVE checksum. Arrays
bound the request, receipt, journal and history records. Failed allocation leaves
the accepted world, clock, history, ID cursor, receipts and transport unchanged.

Capture checks source frontier/queue/receipt/history/coverage invariants and
refuses a journal-faulted session. These checks protect coherent export of an
already-valid authority; they are **not** validation of an arbitrary imported
image. Unused fixed slots are default-initialized. Callers receive owned copies
and cannot mutate the live session through the returned image.

## Exact RAM transport release

`SessionRecovery::releaseModelWrittenPrefix` delegates to the fixed outbox's
existing full typed-record comparison. A mismatched world, changed account
delta or already-released prefix rejects. A matching prefix releases only
transport capacity. Canonical state, processed markers, detailed receipts and
Volatile status remain unchanged. An uncertain disk acknowledgment is not
resolved by this helper; real storage proof and idempotent persistence recovery
remain SAVE-01–03.

The checkpoint covers all journal records through `coveredThrough`, including
those retained for transport. A future loader must never reapply these as later
decisions. Replay begins strictly after this frontier. The retained suffix is
contiguous from `modelReleasedThrough + 1`; an empty suffix is valid after exact
model release.

## Evidence

**50 GameSession cases plus 12 journal cases pass** in Bazel, CMake, strict GCC
15.2 undefined-behavior/float-cast-overflow checks and actual CPU WASM. There are
zero skips or disabled cases in these 62-case runs. WASM uses the configured JS
exceptions/Asyncify combination, fixed 64 MiB heap and 1 MiB stack.

Eight new cases cover coherent accepted/history capture, a canceled second slot
behind a waiting front, distinct Ready/RejectReady states, dormant part/build
identity, exact/wrong/duplicate model prefix release, receipt eviction without
reexecution, byte/content preflight, closure, and a complete capacity run:
40 purchases followed by 32 undos across repeated journal releases. The latter
keeps 32 history entries, 32 dormant parts, eight active paid parts, two detailed
receipts, exact net account cost and processed frontier 72.

The separate `allocation-faults.cpp` replaces actual global new/new[]/aligned-new
and fails each allocation in sequence. **Seven injected failures**, followed by
success, preserve authority and transport in both native and actual WASM runs.
An undersized byte budget makes **zero allocation attempts**. The fixture includes
an active paid plate, retained damaged starter-loan engine, its entitlement,
cargo, a job, receipts and journal data; successful capture preserves them.

Native and WASM applications also compile against the new header/module. This
does not repeat visual/lifecycle acceptance or provide a loaded recovery path.
The shipping WASM build retains the previously recorded GLM comparison and LEGO
layout-cache signedness warnings; the focused shared WASM and standalone native
checks use warnings as errors.

See `summary.json` for source/binary/report hashes and exact test counts;
`wasm-attempt02/manifest.json` freezes shared inputs and commands;
`wasm-allocation-attempt02/summary.json` records the independent allocation probe.
Native XML, build logs and both original failure attempts are retained here.

Preserved failures were validation-helper issues, not weakened acceptance:

- The long-history fixture first cast a small grid coordinate to int64_t before
  initializing canonical int32_t storage. Bazel and WASM rejected narrowing;
  CMake initially warned. It now uses an explicitly bounded int32_t conversion.
  `bazel-attempt02.log`, `wasm-attempt01/` and the first CMake results remain.
- The first WASM allocation helper tried to reuse objects that Nix had removed
  with the earlier temporary shell. Its error and original runner remain in
  `wasm-allocation-attempt01/`. The corrected helper verifies the prior source/tool
  hashes, recompiles the five shared core objects with their recorded flags, then
  links/runs the allocation probe. It preserves the new objects and their hashes.

## Reproduction

Run from the repository root in Nix. Use fresh output paths to retain old results.

```sh
nix-shell --run 'bazel test -c opt //tests:game_session //tests:session_transactions --test_output=errors'
nix-shell --run 'cmake --build build-salvage-native --target game_session_tests session_transactions_tests -j 8 && build-salvage-native/bin/game_session_tests && build-salvage-native/bin/session_transactions_tests'
nix-shell --run 'bash docs/validation/salvage/DATA-05/stage-d1/build-allocation-faults.sh && /tmp/salvage-checkpoint-allocation'
```

The shared `scripts/validate_session_transactions_wasm.py` takes the installed
SDK and Node paths, `--output <fresh> --exception-mode js --asyncify`. Exact
successful SDK/Node paths and arguments are in `wasm-attempt02/manifest.json`.
Then run `run-wasm-allocation.py --shared-validation <that-directory> --output
<another-fresh-directory>` in Nix. It refuses changed source/tool inputs.

## Next implementation

Read `../design.md` sections 6–8 before adding the loader. Keep DATA-05 unchecked
until the complete recovery and independent review requirements pass.

1. Validate an imported bounded image against expected world/content/profile,
   canonical model data, role-distinct IDs, active/dormant/history references,
   full pending interval, receipts and coherent journal/state coverage. Reject
   malformed counts, holes and contradictory records before publishing anything.
2. Apply exact validated post-checkpoint object/account deltas once in contiguous
   journal order. Check every pre-state and marker transition. Do not execute
   original commands or inverse intents, allocate their IDs again, or activate
   an old preparation ticket. Retained covered records are not replay input.
3. Advance authority epoch, retire the old admission, cancel unresolved commands
   deterministically, clear temporary history and skip the reserved ID horizon.
   Only trusted code may mint a fresh token/admission generation. Preserve old
   committed decisions and balances without a second spend, payout or refund.
4. Add missing/corrupt prefix, receipt-eviction reload, uncertain acknowledgment,
   unused leased-ID horizon, old-token/callback and exact compensation crash cases.
   Exercise trusted entitlement retirement without a player/test snapshot setter.
5. Verify the whole DATA-05 boundary independently, then implement DATA-06 and
   satisfy G01's headless starter-design/undo/serialization/accounting gate.

This is a completed capture component, not a completed recovery subsystem. No
DATA-05/G01/G02 acceptance, gate commit or cove visual approval is claimed.
