# DATA-05 stage A — bounded volatile journal

Component implementation, 2026-09-07. **DATA-05 remains incomplete.**
The authority still has its DATA-04 one-pending-command behavior at this stage;
new journal support is not connected to live session decisions yet.

Implemented strong request/admission/history/journal counters, owned fixed-size
typed accepted-object/account records, fixed count/byte/sequence reservations,
append-order journal IDs, exact prefix copies and explicit RAM-model prefix
release. Storage never overwrites records. A reserved closure slot can coexist
with later pending terminal decisions when normal capacity is full.

Existing public session value/command/receipt types moved to
`session_transactions.hpp`; `game_session.hpp` includes that header. Their
behavior and the existing Application create/call boundary are unchanged.

Validation:

- `build-standalone.sh`, GCC 15.2.0, repository strict warning profile,
  `-Werror`, UBSan and float-cast-overflow: **12/12 journal tests passed**.
- Existing DATA-04 strict runner against the extracted header:
  **21/21 GameSession tests passed** (`legacy-compile.log`, `legacy-tests.log`).
- `build-no-allocation.sh` and isolated `no-allocation.cpp` replace actual C++
  new/new[]/aligned-new. Across 1,000 reserve/append/copy/prefix-release/discard
  cycles: **zero allocation attempts**. Injecting actual factory bad_alloc
  returned Capacity, leaving the existing outbox usable.
- Record size is **1,272 bytes**; complete fixed outbox is **83,048 bytes**.
  Byte accounting describes in-memory storage; it is not a save-wire size.

The first 11-case pass is retained separately; the final twelfth fixture adds
committed asymmetric part/account preservation and full-record proof rejection.
`hashes.json` binds this stage's source and executed binaries.

The journal validates its structural contracts, identity domains, revision/
frontier shape and rejected-decision atomicity. GameSession and the upcoming
recovery validator still must establish full semantic legality/catalog pricing.
A caller cannot treat this outbox as independent gameplay authority.

The exact typed prefix-release API is explicitly for the volatile DATA-05 crash
model. It releases transport capacity only; it has no durable-success flag.
SAVE-01 supplies canonical bytes/checksums and SAVE-02/03 actual storage proof.
No GPU, shared Bazel/CMake build, browser integration or filesystem durability
was exercised or claimed by this component run.
