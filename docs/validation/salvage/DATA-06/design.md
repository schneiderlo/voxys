# DATA-06: bounded session events and telemetry

Design established 2026-09-07; transport and live integration implemented 2026-09-08.
The [wire and reader contract](wire-format.md) and [stage-b1 evidence](stage-b1/README.md)
cover GameSession publication, fresh incarnation provisioning and coherent
baselines. Independent review remains open; no later-mechanic emission is claimed.
This specification extends the accepted
immutable GameSession boundary and DATA-05 transaction implementation.

## Boundary and authority

Events notify observers of facts; they do not own economic or gameplay truth.
Only GameSession changes builds, inventory, cargo, jobs and accepted receipts.
DATA-05's bounded `JournalOutbox` owns immutable accepted transaction decisions;
SAVE-01/02/03 will establish durable storage and acknowledged journal frontiers.
An overwritten notification cannot undo a payment, execute a reward again, or
make an unacknowledged save durable.

The initial implementation emits only actual operations already available:
receipt transitions, accepted logical-state changes and admission closure.
There are no force, attachment, damage, cargo-banking or reward events from the
static cove. Its scenery has no such authoritative mechanics. Factory/bootstrap
does not replay seeded history as if it had just happened.

This is an **outbound observation** boundary. Mandatory physics input evidence
used to decide damage, attachment or cargo state is separate. A missing or
overflowed solver evidence batch must enter SIM-04's fault/recovery policy; it
cannot be silently discarded through a lossy telemetry queue and then treated
as complete authority.

Relevant current seams:

- `src/game/expedition/game_session.cpp`: `submit`, `retain`, `rejectPending`,
  `advanceOneTick` and `closeAdmission`; commit already stages allocations before
  adapter activation and state-pointer swap.
- `game_session.hpp`: typed `SessionRevision`, `Receipt`, copied `SessionSnapshot`
  and const queries. Current receipts are memory-only.
- `src/physics/physics_types.hpp:744`: legacy physics events use generational GPU
  handles and an overflow-marked batch. Those records cannot be rebranded as
  durable part/cargo events without certified tick/topology mappings.
- `src/game/wreckwater_match.hpp:429`: existing explicit event IDs are a useful
  pattern. Keep expedition schemas separate from WRECKWATER match/score rules.

## Identity, version and time

Define explicit numeric event kinds and separate strong `EventSequence` and
`EventStreamIncarnation` types. Do not reuse request, journal or topology counters.

Every record carries:

| Field | Contract |
|---|---|
| Envelope version, kind, payload version, payload length | Explicit integers; no implicit C++ enum ordinals or native structure bytes as a wire format |
| World namespace, authority epoch | Identify the authority whose state is described |
| Public stream incarnation, lane, event sequence | Unique observation identity; sequences start at 1 independently per lane |
| Simulation tick and tick basis | `LogicalBoundary` for current command facts; `CompletedSimulation` only for certified later physical evidence |
| Session revision | Revision relevant to the fact; receipt rejection/pending may share a revision |
| Optional build ID and topology revision | Present together when one build is identified; multi-build transactions use a summary and baseline/journal lookup |

The stream incarnation is a fresh, non-secret 128-bit identity supplied by the
trusted composition root. It is distinct from the world namespace and admission
token, checked nonzero, and never reused for a replacement event hub. An explicit
fixed value is permitted only in isolated tests. Restart/re-entry or an admission
token-generation change creates a new incarnation and requires a new baseline.
No secret token is serialized, hashed into public payloads, or returned in cursors.

DATA-05's full internal `RequestKey` contains participant, token, epoch and
request sequence. A notification uses only a public reference: participant and
request sequence scoped by its header's incarnation/epoch. Under the initial
single-participant profile, rotate incarnation on token-generation change. NET-01
must introduce a public per-admission identity before independently changing
multiple participants' admissions in one shared stream; do not expose the token
to disambiguate them.

Reference DATA-05's single definition of `JournalSequence` when a journal decision
exists; do not create a second incompatible counter type. It is optional for
pending/rejected non-journal facts. A present journal sequence does not imply
durability. Current events say `Volatile`; `DurableAcknowledged` requires the
later exact contiguous storage acknowledgment, never a RAM enqueue or fake write.

Event sequence is **publication order**, not elapsed time. Domain state revisions
are nondecreasing; delayed telemetry may describe an older completed tick and
revision. Consumers must not attach that sample to current topology without a
matching revision. Render frames, UI polling and audio clocks never advance the
authority tick. Logical tick zero in the cove is not proof of completed physics.

## Typed payloads and emission status

Use fixed-size structs and a closed tagged union. No `std::string`, vectors,
borrowed pointers, raw handles or `string_view` fields in queued records. Durable
object/content references and fixed numeric codes replace mutable array indices
and diagnostic text. Map `SessionError`/`BuildError` to explicit stable event error
codes; do not serialize their current implicit ordinal positions.

| Kind ID | Payload contract | First implementation |
|---|---|---|
| `0x0001 ReceiptChanged` | Public request reference, outcome/reason, expected request sequence, optional committed object, optional journal reference, durability | Emit each newly admitted transition once |
| `0x0002 StateCommitted` | Public cause when present, change-bitmask, primary object/build when meaningful, resulting absolute resource balance, optional journal reference | Emit once after accepted publication; not once per part |
| `0x0003 AdmissionClosed` | Typed close reason and current revision | Emit only on open→closed transition |
| `0x0100 ForceSample` | Durable root/part and source kind; applied force N, torque N·m, root-local sample point m, sample interval/substep aggregation and frame/tick identity | Reserved for SIM-04/05/07 |
| `0x0101 AttachmentChanged` | Durable connection/endpoints, old/new attachment phase, reason, rope length/limit data and resulting revisions | Reserved for SIM-08 |
| `0x0102 DamageApplied` | Durable target/connection, old/new bounded health/damage, accepted cause; measured impulse Ns separately from force N | Reserved for MECH-05/06 |
| `0x0103 CargoChanged` | Durable cargo, old/new loose/towed/latched/banked state, owner and attachment/build references, accepted reason | Reserved for SIM-08/PLAY-01 |
| `0x0104 RewardBanked` | Cargo/job/generation identity, accepted resource amount, absolute resulting balance, journal identity and durability | Reserved for real banking and SAVE acknowledgment |
| `0x0200 PresentationCue` | Stable cue/content key and variant, durable emitter or explicit coordinate frame/position, bounded gain/intensity, originating event ID | Reserved until a real UI/audio consumer exists |
| `0x0300 DiagnosticSummary` | Stable diagnostic code, cumulative count, observation interval and bounded scalar values | Emit only measured actual diagnostics |

Reserved kinds have documented draft semantics, but no production constructors,
encoder acceptance or producer that emits fabricated values yet. Their payload
version is assigned when the corresponding mechanic finalizes its schema. A
synthetic codec/ring test must label its fixture as synthetic. The initial live
payload version is 1 for the four implemented kinds only.

Finite values, valid identities, enum ranges and required/unused fields are
validated at producer ingress. Unused encoded fields are zero. NaN/Inf, negative
unsigned quantities and mismatched kind/payload versions reject with a bounded
diagnostic counter. Initial codec tests use explicit little-endian fields and
independent byte fixtures; never `memcpy` a C++ union or padding into a packet.
All 64-bit values cross JavaScript as DATA-01 decimal strings or binary.

For future force data, the producer must distinguish interval-average force from
impulse and name the sampled root/body frame. A force visualization cannot become
a new input force. Internal welded-bond stress is an estimate, not a solver joint
reaction. Attachment/damage notifications follow the accepted physical outcome;
they do not cause a second merge, impulse, fracture or inventory operation.

## Fixed storage and overflow

Use separate fixed-capacity overwrite rings so high-rate diagnostics cannot
evict domain notifications directly:

| Lane | Initial capacity | Policy |
|---|---:|---|
| Domain | 256 records | Overwrite oldest notification; lagging readers must resynchronize |
| Presentation | 256 records | Drop obsolete effects on lag; never replay old sounds after baseline |
| Telemetry | 1,024 records | Overwrite oldest sample; report missing interval and resume explicitly |

Target a maximum **256 bytes per in-memory record**; enforce the actual type size
with `static_assert` and report exact allocation sizes in implementation evidence.
Those capacities imply at most 384 KiB of record slots plus fixed metadata. If
the concrete header/payload representation exceeds the ceiling, revise the
record budget explicitly rather than making this estimate a false guarantee.
Ring storage is allocated once when the session is created; failure rejects
creation. Publish, read and counter updates then perform no heap allocation.

Each lane reports oldest/newest sequence, current occupancy, high-water occupancy,
overwritten-record count, invalid-input count and exhausted-sequence status.
Cumulative diagnostic counters saturate at `UINT64_MAX` with a saturation flag;
they never wrap to zero. Overflow reporting is out-of-band state: it must not
attempt to enqueue an overflow event into the already full queue recursively.

Attempted ingress spam does not get one domain record per failure. Wrong token,
gap/busy ingress and exact retries update fixed diagnostic buckets or return the
existing receipt. Only newly admitted receipt transitions emit `ReceiptChanged`.
Telemetry sampling and per-tick work receive explicit producer budgets; do not
create one force event per decorative stud or allocate an unbounded staging list
before appending to a bounded ring. Initial implementation has no such producer.

At sequence exhaustion, mark the lane exhausted and return a typed publication
status; never wrap or reuse an event ID. Further notification loss is observable
through the sticky status, while accepted state/receipts/journal remain valid.
The owner can restart the hub in place at an explicit boundary with a fresh
incarnation and force baseline resynchronization. Borrowed reader views retain
their owner address; all old cursors reject. No background consumer or UI can
reset it. The current API rejects the current incarnation but relies on the
trusted owner never to recycle any older incarnation.

## Cursors and a consistent baseline

Readers hold value cursors `{incarnation, lane, lastConsumedSequence}`; zero
means no records consumed. The hub does not allocate or retain per-consumer
state, and slow readers never pin producer storage.

`read(cursor, span<Record>) const` copies a bounded batch and returns a proposed
next cursor. It does not mutate the caller's cursor, acknowledge anything or call
consumer callbacks. Advance only through copied records. After cursor identity/schema validation, a zero-length output
returns `NeedCapacity` without changing the cursor. Invalid identity/schema/lane
or a future cursor takes priority over output-capacity errors. Invalid incarnation, future
sequence or unsupported schema returns a typed error; a cursor older than the
retained range returns `Gap` with the available range and overwrite status.
Do not silently jump to newest or return an incomplete domain history as complete.

On a domain gap, the consumer obtains an `EventBaseline`: accepted session state,
admission state, its bounded visible receipts/frontiers, lane cursors and
diagnostic counters captured at **one owner-thread boundary**. State copy and
cursor capture cannot interleave with any publication; no observer callback,
UI dispatch, suspension or other writer runs inside this operation. In the
current single-owner implementation this is one synchronous call. Future
threaded implementations need an equivalent lock or immutable-version protocol.

The implementation caps one baseline at 8 MiB of fixed object plus copied vector
elements. It carries accepted builds/inventory/cargo/jobs, public participant,
active starter entitlements, workshop availability, admission frontiers, bounded
history choices/counts, at most 64 retained terminal and two pending receipts,
all three cursors/lane statistics and seven saturating ingress buckets. It
contains no admission token, request intent or private compensation images.
This is an owned-model accounting bound, not an allocator/RSS or wire-byte limit.

Build the complete baseline in temporary owned storage. Return it only after all
copies succeed. If any allocation fails, return failure/throw without changing
the caller's state or cursor: unread events were not acknowledged. The caller
then replaces its read-only presentation model and all cursors together. Its
next domain read begins strictly after the baseline's recorded publication
boundary, so neither replayed nor newly published changes are lost.

This observer baseline is not a certified physics/save checkpoint. Future saves
still require SIM-04's matching completed pose/event evidence. Detailed receipt
eviction may remove an old notification or toast, but current balances, cargo
banking state, processed frontiers and journal/recovery state remain the source
of truth. A consumer never reruns a payment or increments canonical inventory
because it received `RewardBanked`.

Presentation consumers discard obsolete one-shot sounds/effects after a gap.
Telemetry consumers explicitly choose a new retained-range cursor and mark the
missing interval on a chart; they do not interpolate a missing force spike as
measured data. No event lane imposes an ordering guarantee on another lane.

## Publication and the DATA-05 seam

Prebuild the bounded notification records during transaction preparation.
After DATA-05's pre-reserved journal decision and accepted state/receipt publish
at the commit point, append them using no-throw ring writes before returning to
the caller. Queue overwrite cannot make a committed transaction fail afterward.
Consumers cannot run in the middle of that sequence. The ring is not a
`JournalOutbox` acknowledgment or a substitute for its capacity reservation.

Pending receipt creation emits once. Later commit, rejection or cancellation
emits its one real terminal transition. Cancel after commit and an exact retry
return existing outcomes without new transitions. `closeAdmission()` first
finalizes pending cancellation, then emits one closure notice; repeated close
does nothing. No emission during destruction may require a callback or allocation.

Use separate public reader and private writer capabilities. Only GameSession
owns the domain writer. A later validated physics bridge may supply measured
telemetry through a narrow trusted ingress; it cannot publish accepted damage
or economic facts directly. UI/audio receive readers and command-submission
ports, never mutable session state, a publisher, or adapter activation access.

## Implementation and acceptance

`src/game/expedition/session_events.hpp/.cpp` implements the standalone transport
and codec, with `tests/test_session_events.cpp`. GameSession owns publication and
baseline capture; its focused tests exercise real transactions. Both build
systems register the same sources. The ring/types remain independent of renderer,
WebGPU, network and filesystem dependencies.

`GameSession::create` and `SessionRecovery::restore` require an explicit fresh
public incarnation. Recovery starts with an empty observer stream and does not
replay old receipts as new notifications. `validateInitialState` returns only a
validation issue: it constructs no journal, observer hub or returned live
authority. The application obtains separate world and observer entropy; Reset
retains both and re-entry obtains both anew. In-place stream restart preserves
authority, journal, admission and lifetime ingress counts while clearing lane
occupancy and current-stream loss status.

Current measured acceptance is recorded in stage-b1: all 122 shared cases pass
in both native build systems, strict undefined-behavior checks and configured
WASM; actual allocation guards/failures and real browser lifecycle pass. The
[independent review request](REVIEW_HANDOFF.md) is still outstanding. Earlier
stage reports retain historical source hashes and their original scope.

Required acceptance cases:

1. Real add/move/remove/job acceptance emits truthful pending/terminal and commit
   notifications with the accepted revision/IDs; duplicate/conflicting retries,
   canceled preparations and close do not invent or repeat state changes.
2. Fill tiny rings, use fast and stalled readers, and wrap storage indices many
   times. Verify record order, bounded slots, independent cursors, exact gap
   boundaries, overwrite counters, and no stale-incarnation acceptance.
3. Flood telemetry and invalid ingress without affecting domain storage or
   balances. After domain overrun and detailed receipt eviction, resynchronize
   from one baseline; no command/reward is re-executed and inventory is exact.
4. Inject failure while copying a baseline. The original consumer model and
   cursors remain unchanged; the next successful baseline/read loses no events.
   Reject future/zero-capacity cursors without silently acknowledging records.
5. Exercise counters near maximum, saturation flags, sequence exhaustion and
   fresh-incarnation replacement. No ID reuse or arithmetic wrap occurs.
6. Assert no allocation in postcommit append/read using the test allocation
   guard; validate exact sizes and independent byte fixtures, unknown versions,
   invalid IDs/values, truncated/trailing bytes and lossless JS counters.
7. A consumer can mutate only its own copy. It cannot obtain a domain writer or
   alter the session through any reader operation. No static-cove force,
   attachment, damage, payout or durable-success events are emitted.

The first task can finish without implementing the reserved mechanics. It must
provide typed schemas, bounded observation behavior and actual current-session
notifications. Physical evidence certification, full persistence, replay,
network replication and presentation effects retain their later task gates.
