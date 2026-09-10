# DATA-04: local session authority and preparation boundary

Status: implementation design, 2026-09-07. This is not evidence that gameplay,
durable saves, machine physics or multiplayer exists. Root approved the bounded
foundation below; shared build and Application integration remain root-owned.

## Existing constraints

- `GAME_IMPLEMENTATION_TODO.md`, contracts C–D and DATA-04/05, require one
  authority, failure-atomic preparation and separately bounded histories.
- `construction/build_model.hpp:128` provides validated, privately owned builds.
  Its `replace` operation is explicitly trusted; it does not authorize a player,
  charge inventory or prevent historical ID reuse. Never expose it to the UI.
- `construction/part_catalog.hpp:159` supplies exact content versions, authored
  costs, salvage-yield ceilings and physical/module limits. Commands cannot
  substitute their own prices, mass, strength or paid/loan provenance.
- `construction/construction_types.hpp:126` supplies a single-owner, monotonic
  allocator. Its high-water mark cannot be rolled back after issuing an ID.
- `expedition/salvage_preview.cpp:44` creates 32 static decorative bodies.
  `Application::salvagePreviewAction` currently calls their lifecycle directly
  (`src/app/application.cpp:5004`). Those bodies have no paid inventory, jobs,
  cargo definitions or compiled machinery.
- Preview Reset/Leave already depend on completed generation metadata
  (`salvage_preview.cpp:146`), not merely accepted destroy requests. The retained
  metadata allocation in `src/app/salvage_preview_readback.hpp` must survive the
  empty-world interval. Preserve this behavior when inserting session authority.
- WRECKWATER has its own network/authority and small fixed world
  (`src/server/wreckwater_authority_runtime.hpp:23`). Reuse ideas, not its scope,
  GPU headers, match rules, peer barrier or authority instance.

## Smallest accepted slice

Implement a renderer-free `GameSession`, a narrow preparation interface and a
controllable fake adapter used by headless tests. The session owns builds,
resource accounts, trusted seeded jobs/cargo, participant identity, revision,
epoch, request ordering and receipts. All public queries are const or copies.

Initially support creating an empty owned build, adding, moving and removing an
unconnected part, and accepting a trusted seeded available job. Parts are bought
at the exact catalog cost; ordinary removal returns the catalog salvage yield
for a paid part and zero for a starter loan. This is dismantling, not undo.
Connected-part removal rejects until a connection-aware command is implemented;
there is no silent deletion of links. Move changes placement only. Health,
definition, settings, ownership and provenance remain unchanged.

The factory accepts a trusted bootstrap for headless fixtures. It validates all
IDs, namespaces, ownership, exact content references, bounds and cross-record
references before publishing a session. This is not a player import API. The
actual cove profile starts with empty builds, accounts, cargo and jobs. It cannot
purchase parts or activate a job. Fixture seeding must never be installed as
shipping starter entitlements or campaign content.

Cargo is session-owned data, initially loose and read-only. Its identity,
definition, owner, physical/recovery metadata and optional job association come
from trusted content/bootstrap, not commands. Do not accept attach, tow, latch,
release, bank, repair, rescue, free-grant or mission-completion commands yet.
These operations need their later physical/economic predicates. A job can be
accepted once; that grants no reward and does not complete its objective.

## State, IDs and revisions

Use a new strong `SessionRevision` for the entire accepted logical world. Keep
it separate from each build's `TopologyRevision`, `SimulationTick`,
`AuthorityEpoch`, request sequence and adapter ticket generation. Every accepted
state-changing transaction advances session revision once; an edited build also
advances its own revision once through `BuildModel::replace`. A new build begins
at build revision zero. Rejections, duplicate queries and canceled preparations
do not advance either revision. Check overflow before preparing any change.

`SessionView` contains accepted state only. Pending work is visible through its
receipt, never through half-applied builds or reserved inventory. Candidate
builds and resulting balances are privately staged. Keep pointers/references to
internal state out of the public mutation interface; a copied bounded snapshot
has simple lifetime semantics for tests and UI.

Allocate new object IDs only after cheap command, identity, budget and inventory
validation. Issued IDs are burned even if later geometry/preparation fails or a
command is canceled. Never roll the allocator back, and never issue them again.
This bounded high-water bookkeeping is allowed to change along with rejection
receipts; the promised unchanged state is accepted topology, accounts, cargo,
jobs, ownership and revisions. Tests compare these explicitly, and separately
assert monotonic allocator behavior. Do not publish pending object IDs as owned
objects. DATA-05/SAVE-01 must carry the high-water mark in durable authority
metadata; restoring it below any previously issued/exposed ID is forbidden.

## Intent and public API shape

Proposed files: `src/game/expedition/game_session.hpp/.cpp`,
`tests/test_game_session.cpp`, with small expedition record headers only if they
reduce the public header substantially. Dependencies are C++20 standard library,
DATA-01/02/03; no GLM, renderer, PhysicsWorld, WebGPU or network dependency.

```cpp
// Sketch: names may tighten during implementation; semantics below are fixed.
struct CallerContext { DurableId participant; DurableId sessionToken; };
struct CommandHeader {
    AuthorityEpoch epoch;
    RequestSequence sequence;
    SessionRevision expectedSessionRevision;
};
// Targeted build intents also carry expectedBuildRevision.
using Intent = std::variant<CreateBuild, AddPart, MovePart, RemovePart, AcceptJob>;
struct Command { CommandHeader header; Intent intent; };

class GameSession {
public:
    static /* owned result */ create(const SessionBootstrap&, const PartCatalog&,
                                    PreparationAdapter&);
    Receipt submit(CallerContext, const Command&);
    Receipt cancel(CallerContext, RequestSequence pendingRequest);
    void pollPreparation();
    bool advanceOneTick(); // Trusted fake fixed boundary; polling cannot commit.
    void closeAdmission() noexcept; // Trusted Leave/teardown, no reopening.
    bool admissionOpen() const noexcept;
    SessionView snapshot() const;
    // Read-only receipt/status query; no player-supplied accepted snapshot.
};
```

The local bridge supplies `CallerContext` from its admitted participant/token.
Payload ownership fields are never trusted. The initial profile admits one
participant; bootstrap owner/lease/job participant references must name that
participant, not merely a counter below the allocator high-water mark; foreign participants, worlds, tokens or epochs reject. This is an
architectural local authority boundary, not network authentication. NET-01/02
later supply transport admission and authentication.

Intent payloads contain targets and choices only: exact definition key and
placement for AddPart, existing part ID and placement for MovePart, existing
part ID for RemovePart, and job ID for AcceptJob. The session constructs IDs,
owning-build fields, initial health/default configuration and paid provenance.
There is no `SetInventory`, `ApplySnapshot`, `SetCargoState` or
`CompleteMission` player operation.

## Requests, receipts and cancellation

Use `Rejected`, `PendingPreparation`, `Committed` receipts with typed reasons.
Include request identity, resulting session/build revision when applicable and
committed object IDs. All DATA-04 receipts are explicitly **memory-only**;
`Committed` never means a durable save or bank acknowledgment.

Start with one pending economic/topology command and a fixed terminal receipt
window (proposed 64, configurable downward in tests). Admit only the next
contiguous request sequence. A gap returns `SequenceGap` and the expected next
sequence; it does not advance any marker. Unknown/invalid admission similarly
does not consume another participant's sequence. Busy returns a retryable
ingress result, not an admitted terminal receipt.

An admitted invalid command is terminally rejected and consumes its sequence.
Exact retries return the stored receipt, including the same pending operation.
Reuse of the same identity with different bounded intent data returns
`RequestConflict`; never execute either a second time. Compare typed canonical
intent fields, not padding bytes or an unstable `std::hash` value.

The in-memory contiguous processed high-water mark outlives receipt eviction.
An older request without its detailed receipt returns `AlreadyProcessed`, never
re-executes. This safety property belongs in the small foundation now; DATA-05
adds checkpointable markers, token retirement history, larger admission policy,
and compensation. Do not accumulate an unbounded retired-receipt vector.

Cancellation is an authenticated control operation on the caller's current
pending request, not a second spending request that deadlocks behind it. It
finalizes the original request as rejected/canceled, releases its reservation
and asks the adapter to discard. Repeated cancel queries return the terminal
outcome. Cancel after commitment cannot undo the operation. Late or duplicate
adapter completions cannot activate canceled work.

## Preparation adapter and atomic publication

The adapter receives a bounded, read-only candidate change and a unique ticket
tag containing session epoch, preparation generation and expected revision. It
returns pending/ready/failure; tests control delay, failure, capacity and stale
completion delivery. It never receives a mutable `GameSession&` or authority to
debit accounts or publish builds.

Preparation may reserve resources but cannot replace the accepted world. Before
publication, recheck caller/token/epoch, expected session and build revisions,
ownership, edit lease and inventory. A lease is exclusive when present: it must
name the caller/current epoch and be unexpired at the acceptance boundary.
Define its interval explicitly: `tick < expiresAfter`; expiration does not
silently grant edit access. Removing/reissuing a lease is future authority work.

Preallocate all candidate state and terminal receipt capacity. Check adapter
readiness before the commit point. Final activation and ownership swaps must be
non-throwing and require no new allocation. Any injected failure is handled
before that point. A backend/device failure after real submission is a later
fail-stop/recovery problem, not a promise to roll GPU time backward.

The fake adapter maintains ticket generations and distinct pending/active/
retiring records. Tests can withhold retirement; discarded or old active records
remain counted against capacity until their simulated fence completes. Never
pretend a late cancellation immediately releases memory still referenced by a
submission. A preparation ticket is not a durable body identity.

Represent resource demand as checked counts for canonical records, bodies,
shape children, joints, contact/event slots, render mappings and staging/journal
bytes. Canonical part/connection limits use DATA-03 caps. The fake adapter uses
declared fixture costs for backend resources; it does not estimate one body per
part or claim to compile welded assemblies. SIM-01/03 provide actual derived
costs, SIM-06 reserves all consumers atomically. Until then this adapter cannot
admit playable machine spawns.

## Time and future mechanics

DATA-04 does not advance game time from a browser timer or wall-clock callback.
The fake test driver can expose explicit monotonic fixed boundaries; polling
preparation alone does not advance `SimulationTick`. The static preview can keep
gameplay tick zero while its presentation continues. SIM-04 owns the real
scheduled/submitted/completed frontier and 60 Hz catch-up policy.

Real mutation scheduling must target an unsubmitted future tick, with compatible
topology/poses/mappings published together. A certified completed evidence tick
is not automatically an editable future tick. Do not use the preview's 10 Hz
status JSON or lifetime metadata as construction or cargo-motion evidence.

The interface must allow later immutable `CompiledAssembly` data without putting
handles into canonical records. SIM-01 calculates welded roots and full inertia;
SIM-05 supplies per-tick water; SIM-08 supplies authoritative cargo capture and
merged/released momentum. DATA-04 must neither generate its own fake inertia nor
accept a caller's assertion that a distant/fast cargo is latched. One physical
body retains one solver owner. Saves/replay wait for their certified evidence
and storage work; the fake adapter proves control flow only.

## BOOT-05 integration owned by root

Application remains the composition root. It owns session lifetime, presentation
and an adapter/coordinator around `SalvagePreview`. Both native R and WASM
Reset/Leave exports submit session lifecycle intent through that coordinator.
No UI callback calls `BuildModel::replace`, adjusts balances or changes jobs.

Lifecycle control is separate from build transactions: Reset rebuilds disposable
scene fixtures and recenters the camera; it cannot mint resources, restore loan
entitlements or reset progress. Leave closes admission, cancels pending work,
waits for owned scene retirement and only then reports the route as inactive.
Leave supersedes Reset; repeated Reset while pending must reuse the current
request or be rejected busy. Keep a reserved control path so a full economic
queue cannot prevent Leave. Root can add a narrow typed lifecycle command/status
bridge once this session API freezes; do not force scenic destruction into the
no-side-effects build preparation contract.

The implemented core supplies `closeAdmission() noexcept`: it finalizes and
discards pending preparation once, rejects all future submissions without
consuming their sequences, and has no reopening operation. Receipt queries
remain available. The coordinator calls it before beginning Leave retirement.

Preserve current completion semantics and failure handling: accepted destroy is
not retirement; old generations and the retained metadata source remain live
until acknowledgment; timeout/device failure closes the enclosing world safely.
Scene reset failure can leave presentation unavailable and must report failure,
not a committed successful reset. Re-enter with a fresh token; no old callback
or receipt may affect the new session. Existing preview JSON fields may remain
as presentation diagnostics; all new 64-bit authority fields use DATA-01 strings.

## Acceptance evidence for DATA-04

1. A headless session buys, moves and dismantles an asymmetric part. Check exact
   catalog debit/yield, full identity/provenance preservation, and one revision
   advance per accepted change. A loan dismantles for zero resources.
2. Duplicates while pending and after commit spend once; changed-payload reuse
   conflicts; after a tiny receipt window evicts details, retry remains stale.
   Gaps, wrong token/participant/epoch, exhausted counters and malformed targets
   do not bypass ordering or authority.
3. Delayed preparation leaves accepted snapshots unchanged. Cancel, adapter
   rejection, full capacity, geometry overlap, invalid rotations and insufficient
   funds preserve accepted builds/accounts/cargo/jobs/revisions. Burned IDs are
   never reused. Repeated stale completions cannot publish rejected work.
4. Expired/wrong-owner leases reject; expiry during preparation is revalidated.
   Inject a revision change through a test-controlled authority boundary, not a
   public snapshot setter, to prove stale preparation cannot overwrite it.
5. Seed validation rejects duplicate/cross-world IDs, bad owner/reference/content
   versions and overflow. Job acceptance changes only that job once. Cargo
   mutation/banking/reward forgery remains unavailable. Snapshot copies cannot
   mutate session state.
6. Bounded tests fill pending/receipt/resource pools and withhold retirement;
   count all reservations/retiring generations, then release each exactly once.
   No unbounded bookkeeping or per-part-body capacity claim.
7. Root integration retains preview ownership tests plus actual native/browser
   Reset, Leave-over-Reset, R, failed retirement and re-entry. Status polling
   changes no economic state and does not drive the authoritative simulation.

Use strict standalone CPU compilation and targeted tests first; root registers
the lean module and focused test in both build systems. Do not count mock
evidence as GPU scheduling, gameplay, persistence, network or human validation.

## Dependency boundary

DATA-04 includes the minimum in-memory prepare/receipt/reservation path required
by its own acceptance; deferring all of it to DATA-05 would be circular.
DATA-05 owns full transaction composition, compensation/undo/redo, durable marker
representation and journal/resource reservations. SAVE-01/02/03 supply durable
encoding and storage, including allocator/processed markers and reward history.
SIM-04/06 replace the fake boundary with the actual GPU frontier and complete
resource publication. PLAY-01/02 supplies real jobs and workshop interaction;
SIM-08 supplies tow/latch predicates. These tasks extend the same sole authority,
not parallel state machines that independently alter inventory or topology.
