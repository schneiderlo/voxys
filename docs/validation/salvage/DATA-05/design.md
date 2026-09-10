# DATA-05 — bounded transactions, recovery markers and compensation

Design preparation by `lego_gameplay`, 2026-09-07. **No DATA-05 implementation
or passing result is claimed here.** Begin implementation after DATA-04 passes.
Root approved two ordered preparations and private bounded undo escrow during
this design. Root owns source/build integration, task acceptance and commits.

## 1. Extend the existing authority

Extend [GameSession](../../../../src/game/expedition/game_session.hpp); do not
introduce another inventory, mission authority or UI-owned build model. Its
current implementation admits one participant, one pending command and a bounded
terminal-receipt window. It stages a private candidate, charges catalog prices,
rechecks revision/lease/funds, then publishes at an explicit fake tick boundary.

Keep the recently corrected participant-reference rule: owners, lease holders
and accepted-job participants must name the admitted participant. An issued ID
below the allocator high-water mark is not a participant registry. The static
cove remains empty, workshop-disabled and unable to activate fake machinery.

DATA-05 adds two queued preparations, exact resource reservations, undo/redo,
checkpointable request/allocator markers, and a bounded journal outbox interface.
It does not add a shipping save format, filesystem/IndexedDB writes, banking,
real GPU assembly publication, network authentication or playable workshop UI.
SAVE-01–03 and SIM-01–06 supply those backends. A memory fake cannot certify
disk durability or a completed GPU submission.

Use the [main plan's Contracts A–D](../../../../GAME_IMPLEMENTATION_TODO.md)
and [DATA-04's reviewed boundaries](../DATA-04/review.md). Canonical IDs,
content versions, integer placements, loans and build validation remain the
DATA-01–03 contracts. No player operation may set a balance, issue an ID,
install a snapshot, manufacture an undo record or assert a storage acknowledgment.

## 2. Fixed initial limits

Use explicit validated limits and smaller test overrides. Check bytes as well
as record counts before allocation, including copied candidates and failed
reservations. Do not let a configurable field raise a hard maximum.

| Resource | Initial hard maximum |
|---|---:|
| Admitted participants in this implementation | 1 |
| Ordered admitted-but-unprocessed commands | 2 |
| Terminal detailed receipts | 64 |
| Accepted active parts | 256 total; retain existing per-build limits |
| Candidate canonical bytes | 512 KiB per preparation |
| Undo/redo entries, combined | 32 |
| History payload bytes, combined | 256 KiB |
| Dormant retained part identities | 64 |
| Dormant retained empty-build identities | 8 |
| Queued journal records | 64 |
| Queued journal payload bytes | 1 MiB |
| One journal record | 32 KiB |
| Recovery image, excluding future world/physics snapshots | 2 MiB |
| IDs in one allocator reservation block | 64 |

Current commands change one part or one empty build. A future connection/bulk
operation whose decision cannot fit the per-record bound must receive a new
bounded transaction representation; never truncate its delta. Limits for
active geometry, dormant history and old GPU resources are separate. History
does not make old in-flight render/shape resources disappear.

A new ordinary edit may stage oldest-history eviction to fit these bounds.
Publish that eviction only with the successful edit; rejected/canceled work
must leave usable history unchanged. Never evict a history/escrow entry reserved
by a pending undo/redo. If no safe eviction is possible, reject with a named
history-capacity error. Queue and journal exhaustion similarly reject before
canonical publication, without silently dropping economic records.

## 3. Request identity and the two sequence frontiers

The request identity is `{world, participant, active token, authority epoch,
request sequence}`. A token has an explicit admission generation; keep that
separate from simulation tick, topology/session revision and adapter generation.
Only trusted admission code can issue/retire tokens.

Track these distinct contiguous counters for the active token:

- `admittedThrough`: the highest request assigned a bounded queue slot.
- `processedThrough`: the highest request with a terminal ordered decision.

Both frontiers start at zero and use a zero-valid strong counter, unlike the
nonzero `RequestSequence` carried by an actual command. Check every frontier,
history/admission generation, journal sequence and allocator increment for
exhaustion; never wrap or manufacture sequence zero as a valid request.

Always require `processedThrough <= admittedThrough`, a difference no greater
than two, and exactly one queue record for every sequence in between. A later
queued rejection/cancellation remains represented until the earlier gap closes.
Never overwrite it with a different retry or skip it during checkpointing.

Accept only `sequence == admittedThrough + 1` into a free slot. A gap returns
`SequenceGap` plus that expected sequence and changes no marker. The initial
reorder buffer is deliberately **zero**: callers resend the missing request.
Do not fabricate rejection records to fill a gap. A full queue is retryable
ingress, not an admitted terminal decision.

Exact retries of either queued command return its existing receipt/progress;
changed intent under the same identity returns `RequestConflict`. Keep the
bounded typed canonical intent, not just a collision-prone hash or raw struct
padding. Terminal retries return their retained decision. After detail eviction,
`sequence <= processedThrough` returns `AlreadyProcessed` and cannot execute.

Use a small internal queue-state enum: `Preparing`, `Ready`,
`RejectReady`, `CancelReady`. Externally these remain `PendingPreparation`
until their ordered terminal decision is published. Progress may say “waiting
for the earlier edit”; do not call a later request durably rejected while its
terminal marker has not been recorded. Only the queue front can commit/reject.
Drain at most two terminal records at a boundary; bounded work stays bounded.

Both requests can prepare from the same accepted base revision. Every candidate
binds session/build revision, inventory reservation, history generation and
entitlement state. At its turn, a stale second candidate **rejects**; do not
silently rebase it or copy its old whole-world snapshot over the first result.
Its caller submits a new sequence against a fresh snapshot if still wanted.

Examples:

| Front request | Second request | Required result |
|---|---|---|
| Commits revision 0→1 | Prepared against 0 | Second terminally rejects stale; processed advances through both |
| Rejects or cancels at revision 0 | Valid prepared candidate against 0 | Second may commit after fresh checks |
| Still pending | User cancels second | Second becomes `CancelReady`; preparation resources discard once; terminal/control reservations and sequence slot remain |
| Later completes after cancellation | Newer generation is active | Ignore stale completion; never publish canceled state |

Cancel is authenticated control of the existing request, not a queued spending
command. Repeated cancel is idempotent; cancel after terminal commitment does
not undo anything. Trusted Leave closes admission and marks both queued
requests canceled in order, using reserved control capacity even if the normal
queue is full. An outbox/storage fault may leave this closure memory-only, but
must not prevent scenic teardown or claim a durable token retirement.

## 4. Reservation ledger and publication

Add a private reservation ledger owned by GameSession. A preparation owns one
generation-tagged reservation covering its worst allowed demands. Reserve:

- Positive resource debits in each currency. Pending credits cannot fund
  another command. `available = accepted balance - sum(reserved debits)` uses
  checked arithmetic, and a commit must preserve reservations held by the
  other queue slot.
- New logical identities, active/dormant record capacity, canonical candidate
  bytes, history/escrow references and any planned history eviction.
- A terminal receipt slot and the exact bounded journal decision payload.
- The fake adapter's declared backend resource demands, including retained
  generations. Actual bodies, child shapes, contacts, draws and staging demands
  remain the later compiler/physics adapter's responsibility.

Undo/redo additionally reserves its exact history entry and escrow identities.
Two requests cannot both reserve the same inverse action or materialize the
same dormant object. Read-only historical references are allowed; ownership of
an escrow transition is exclusive. Reservation failure releases everything
acquired by that attempt, once, without modifying accepted state or history.

Keep resources reserved while preparation is delayed. Cancellation/rejection
releases logical/debit reservations and asks the adapter to discard; the adapter
continues charging in-flight resources until its simulated/real fence retires.
Returning an object to logical escrow is not a GPU lifetime acknowledgment.
The terminal receipt/journal reservation is separate: cancellation of the second
slot retains that capacity until its ordered terminal decision is appended.
Reserve the aggregate worst-case count/bytes for admission, terminal and any
allocator-lease records before admission. Keep one bounded closure reservation
independent of the normal queue. Journal sequence is assigned in actual append
order, never at request reservation time; reserve sequence headroom as well as
slots so a later no-throw append cannot cross exhaustion.

Before the front request publishes:

1. Recheck token/epoch, workshop permission, session/build revision, ownership,
   lease expiry, exact target state, history generation and loan entitlement.
2. Validate the complete changed build through DATA-03; check account arithmetic,
   active/escrow identity ownership, limits and still-held reservations.
3. Finish all candidate/history/receipt/journal allocation and ask the adapter
   whether its reserved candidate can activate. A fake test can deny this.
4. At the explicit boundary, activate the preallocated adapter candidate and
   swap the accepted model, account, history and logical identity ledger.
   Append the already-reserved immutable journal decision and terminal receipt,
   advance `processedThrough`, and retire the old candidate. This section must
   be `noexcept`, non-reentrant and allocation-free.

Do not advance the simulation tick from UI polling. No asynchronous worker may
call back into GameSession or mutate its state; it returns a ticket/result for
the owning thread to poll. Preparation tickets stay scoped to one adapter
lifetime. Never reuse an adapter across sessions while old active/pending/
retiring resources or completions remain; a future shared backend needs explicit
cross-session ticket identity.

Failures before step 4 preserve accepted topology, balances, history, cargo and
jobs. Burned allocator IDs and ordered rejection bookkeeping may change.
An unexpected preparation exception discards acquired reservations and propagates
before publication. The adapter's non-throwing activation contract is essential;
post-submission device failure is a later fail-stop/recovery problem, not a
promise to undo GPU time.

## 5. Undo/redo is an exact new transaction

Add `Undo{historyEntry, expectedHistoryGeneration}` and
`Redo{historyEntry, expectedHistoryGeneration}` to bounded intents. They carry
the usual expected session/build revisions and fresh request sequence. The
caller cannot supply inverse bytes, price, refund, health or provenance.

Initially make CreateBuild, AddPart, MovePart and unconnected RemovePart
reversible. Do not make AcceptJob, bank/reward, token changes, rescue or arbitrary
future actions reversible by default. Ordinary RemovePart is dismantling;
undo is its exact compensation, not another ordinary RemovePart call.

Capture a private `HistoryEntry` when an ordinary edit commits: owning participant
and admission/history generation, target build, before/after images of the
changed logical objects, exact debit/credit, current undo/redo direction and
the required target state. Store only the bounded changed records and relevant
preconditions; do not copy the whole world into every history entry.

Use a linear bounded history for this local workshop. Only the latest applied
entry can be undone and the next undone entry can be redone. Successful undo
advances the cursor; redo advances it back. Any successful new ordinary edit
clears the redo suffix atomically. Failed/canceled compensation leaves the cursor,
escrow and history entry available. Duplicate requests cannot consume it twice.

All compensation checks use the **current** expected revisions supplied with
the new request. Historical pre/post object images additionally protect health,
paint, module settings, placement, connections, ownership and provenance from
being overwritten. Do not require an old numeric build revision to become
current again: every undo/redo advances a fresh revision monotonically. After
each successful history action, record the new validated history cursor state
for the next inverse operation. No revision counter runs backward.

Unrelated accepted job data must survive an undo. A foreign/system edit to a
history target invalidates the affected history instead of letting compensation
erase it. Any later non-workshop construction writer must enter this same
authority path and specify its history invalidation; there is no direct test
snapshot setter. Until such writers exist, the normal two-request stale case
provides the required real revision-conflict test.

### Exact account deltas

Let `C` be the catalog purchase price and `Y` the catalog dismantling yield of
the exact content version captured by the committed edit. Both are resource
pairs; apply each component independently with overflow/underflow checks.

| Original operation | Original balance change | Undo | Redo |
|---|---:|---:|---:|
| Buy a paid part | `-C` | `+C`, move that part to escrow | `-C`, restore that same part |
| Dismantle a paid part | `+Y` | `-Y`, restore that same part | `+Y`, move it to escrow |
| Dismantle a starter loan | `0` | `0`, restore only a still-authorized retained loan | `0` |
| Move a part | `0` | Restore prior placement | Restore later placement |
| Create an empty build | `0` | Retain the now-empty build in escrow | Restore that same empty build |

Undo of a purchase refunds **C**, not Y. Undo of dismantling charges **Y**, not
C. Each closed undo/redo cycle has net zero value. If an inverse debit cannot
be paid, or a refund would overflow, reject without moving the part or changing
the history cursor. Credit amounts come from immutable authority-created
history and are bounded by the original validated operation, never UI input.

### Same logical object, one materialization

Undo escrow retains an existing logical identity privately. A part removed by
dismantling or undo of purchase can be dormant until restoration or history
eviction. Its durable ID is not reassigned or allocated again. Redo restores
that same retained identity, preserving references across a multi-step history.
A dormant object becomes permanently retired only after no surviving history
entry references it; bounded reachability checks cover both applied and redo
entries. Evicting an old entry cannot retire an object still needed by a newer
remove/move entry. Retired counters never return to the allocator.

Maintain a checked logical identity registry across accepted objects, both
pending candidates and escrow/history. The same ID may appear in snapshots of
different revisions of its **one** object. It cannot change role, belong to two
physical objects, be both active and dormant, or be materialized by both queue
slots. Pending copies are proposals, not additional paid inventory. History
images are references/revision data, not a second independently owned part.

Escrow has no public spawn, export, bank or inventory-transfer API. Undo of
CreateBuild is legal only when the retained target is empty and has no live
references; later edits must first be undone in normal order. New physics/render
handles receive fresh generations even when an existing logical object returns.
Each retained build keeps its topology-revision high-water even while dormant.
Removal and restoration each advance it, together with a new session revision;
no absent/present cycle can reuse an old accepted revision. A read-only bounded
history summary supplies entry/generation, target build, current expected
revision and available direction without exposing escrow payloads.

Loan history preserves the original entitlement and loan provenance. It cannot
convert a loan to paid or collect yield. Restoration requires that exact
entitlement still be active and the history generation valid. Later starter
rescue/entitlement replacement must atomically invalidate its affected history
and retire its dormant loan objects before issuing replacement loans. Never
leave an undo entry capable of restoring the earlier loan package afterward.

History is **session-local** in DATA-05. Token retirement and load may clear it.
Clearing retires dormant objects and keeps their issued-ID high-water; it does
not refund resources or dismantle active parts a second time. No persisted
undo/redo promise is made before SAVE-01 explicitly includes it.

## 6. Recovery markers and the persistence seam

Define a versioned, bounded **logical recovery representation** now. It is not
the shipping byte format or a filesystem implementation. The record must bind:

| Field group | Required data |
|---|---|
| World authority | World ID, schema/content/profile identity, authority epoch, accepted session/build revisions and tick |
| Accepted world | Canonical builds, balances, cargo/jobs, active entitlements and their checked references |
| Admission | Admitted participant, active token/admission generation, retired-through generation, admitted and processed sequence frontiers |
| Request detail | Bounded ordered pending/reject-ready/cancel-ready records with full bounded intent, exact identity, base revisions, assigned logical IDs and terminal result where decided; bounded recent terminal receipts |
| Identity allocation | Issued cursor plus independently recorded reserved-through allocator horizon; no rewind after canceled/failed IDs |
| Journal coverage | Contiguous decision-record frontier included in the image; which later records require replay |
| History policy | Explicit `historyClearedOnRestore`; no unadvertised persisted compensation authority |

Backend tickets, GPU handles, pointers and a fake adapter's resident slots are
not recoverable canonical data. Reservations must be reacquired; never serialize
them as proof that the GPU still holds a resource. Unknown schema/content,
duplicate/role-alias IDs, holes in the pending interval, invalid token/frontier
relations, noncanonical data or mismatched accepted-state coverage reject before
publishing any recovered authority.

### Outbox before disk storage

Add an abstract bounded journal outbox. A committed/rejected ordered decision
is appended to its reserved in-memory slot atomically with its canonical result.
The record contains the exact request identity/intent, before/after revisions,
object transitions, account delta, processed-marker advance and resulting
receipt. Admission/pending and token/allocator changes have explicit record
kinds too. Journal sequence is a separate monotonic counter, not a part ID.

The DATA-05 implementation uses memory-only storage and a controllable crash
model. Mark receipts `Volatile`; a fake write completion is test evidence only.
SAVE-01 defines exact canonical bytes/checksums and compaction. SAVE-02/03 connect
real native/browser storage and may enable `DurableAcknowledged` only from a
verified exact contiguous write acknowledgment. UI cannot submit that proof.

This distinction is necessary: appending a record to RAM does not make the
world durable. A future disk failure after a memory commit reports **committed
but unsaved**, freezes further economic admission as needed, preserves the
in-memory world and retries/resolves the same journal identity. It must not
relabel the edit as rejected, execute it again, silently drop its record or
display durable banking success. A full outbox fails before publication.
Uncertain write acknowledgment requires querying/replaying the same logical
record; elapsed time is not proof of either persistence or failure.

Checkpoint export must bind one coherent accepted state and its matching
frontiers/pending interval. Compaction may drop detailed terminal receipts only
after the covered processed marker is in the replacement checkpoint. It must
never drop an unresolved later terminal slot merely because an earlier request
has not finished. Acknowledgments identify the exact world/epoch/journal prefix
and record identities; a count or matching sequence from another world is not
a persistence proof. Until SAVE-01 supplies byte digests, fake tests compare
the full typed canonical record; do not use `std::hash` as a durability identity.

### IDs and token retirement across recovery

Represent allocator leases explicitly: `issuedThrough <= reservedThrough`.
For a future durable backend, acknowledge the next reserved block before
issuing any ID from it. On recovery skip all unused counters through the
previously durable reserved horizon. This prevents reuse even when a candidate
or its final journal decision was lost. The memory-only profile can model
this protocol but cannot advertise crash-safe ID allocation.

Persist only the admitted token/generation plus a monotonic closed-through
generation per admitted participant; do not retain an unbounded list of every
old token. Every request must match the currently admitted token exactly.
Unknown, retired or old-generation tokens never create a new admission by
presenting a low numerical ID. Reopening is a trusted host operation that
allocates a fresh token and advances admission generation with overflow checks.
No player intent can lower or replace these markers.

Initial restore policy: first apply exact validated accepted object/account
deltas after the checkpoint coverage frontier, once and in contiguous journal
order; never execute command/undo intents again. Validate each delta against
its expected accepted pre-state and marker transition. Only after this replay,
advance the authority epoch, close the old admission, deterministically cancel its
unresolved preparations in sequence order, clear session-local history/escrow,
and open a fresh token only after recovery validation. Preserve completed
decisions and balances once; never reapply a committed request. Old callbacks
and old-token requests remain stale. The real storage backend must persist the
closure/new-admission transition before promising that restored admission is
durable. This is deliberate recovery policy, not an automatic replay of physical
preparation against unknown GPU state.

## 7. Implementable API boundary

Keep authority methods on GameSession. New supporting files may be
`session_transactions.hpp/.cpp`, `session_recovery.hpp/.cpp` and
`tests/test_session_transactions.cpp`; root decides the final split. The
following is an interface sketch, not existing callable code:

```cpp
struct RequestKey { DurableId participant, token; AuthorityEpoch epoch;
                    RequestSequence sequence; };
struct AdmissionState { uint64_t generation, retiredThroughGeneration;
                        RequestFrontier admittedThrough, processedThrough; };
struct UndoIntent { BuildTarget target; uint64_t historyEntry, historyGeneration; };
struct RedoIntent { BuildTarget target; uint64_t historyEntry, historyGeneration; };
enum class Durability { Volatile, DurableAcknowledged };

class JournalOutbox {
public:
    // Owned only by the composition root/session. Reservations are generation-tagged.
    virtual ReserveResult reserve(const BoundedDecisionDemand&) = 0;
    virtual void appendReserved(Reservation, PreparedDecision&&) noexcept = 0;
    virtual void discard(Reservation) noexcept = 0;
    // A storage worker reads immutable records, never mutable GameSession state.
};

// Player surface: submit typed intents, cancel own request, query copies/status.
// Trusted recovery surface: validate a complete coherent image before creating
// a closed/recovered session; never mutate a live session through a snapshot setter.
```

Use strong wrappers for production history/admission/journal counters and
generation-tagged reservations rather than interchangeable raw integers.
The recovery factory belongs in a clearly trusted composition/storage header,
not a browser command payload or UI convenience method. Tests create a fresh
session from checked recovery records through that boundary; they do not gain
an “overwrite accepted state” shortcut to manufacture conflicts.

## 8. Required fault and accounting cases

Use strict native and actual configured WASM tests. The project's exception
strategy must support the tested allocation/exception paths; do not infer a
browser pass from a native-only catch. Root registers both build systems and
preserves the unchanged mandatory repository checks.

| Case | Required assertion |
|---|---|
| Two ordinary preparations from one base | Front commits; second rejects stale; first state survives exactly |
| Cancel/reject first, valid second | Ordered processed frontier reaches both; second can commit without filling a fake gap |
| Cancel second while first waits | Later tombstone remains; conflicting retry cannot replace it; discard once |
| Tiny receipt window, then reload modeled state | Old requests remain stale after eviction; no second spend/reward |
| Missing middle queue record or processed > admitted | Recovery rejects atomically |
| Wrong token/epoch/world, retired token after reopen | No admission, new ID, debit or queue-marker change |
| Two pending debits exceed available balance | Second cannot spend first's reservation or pending credit |
| Full history/outbox/backend; withheld retirement | Named failure before publication; old history/world usable; no hidden unbounded queue |
| Failure after each acquisition, including throwing begin | Every acquired reservation discards once; accepted state unchanged |
| Final activation denied | No model/account/history/processed commit; ordered rejection retained |
| Buy → undo → redo repeatedly | Exact -C/+C/-C; same retained logical ID, one active materialization |
| Dismantle → undo → redo | Exact +Y/-Y/+Y; insufficient inverse funds or credit overflow fails atomically |
| Several add/move/remove undos and redos | Stable references, full health/settings/provenance preservation; monotonically new revisions |
| New edit after undo | Redo suffix clears only on commit; dormant objects retire without another payout |
| Two pending inverses target one escrow/history entry | Only one can reserve it; no duplicate active part |
| Loan dismantle/undo, then trusted entitlement retirement fixture | Zero payout; no paid conversion; invalidated old escrow cannot restore another loan package |
| History cleared on token retirement/recovery | No new refund or grant; active world remains; dormant IDs stay burned |
| Wrong-generation or late adapter/journal completion | Cannot activate another candidate or advance another world's persisted prefix |
| Fake crash before/after decision coverage, including uncertain ack | Recovered committed delta applies once; uncovered volatile progress is not called durable |
| Allocator reserved horizon saved ahead of issued cursor | Recovery skips unused leased IDs; no wrap or reuse |
| Publication allocation guard | Force allocations to fail after preparation; successful boundary does not allocate or throw |

The entitlement-retirement fixture must use the same trusted checked lifecycle
operation that invalidates entitlement/history, or construct a new validated
recovery image with retired entitlement. It must not expose a player/test-only
balance or snapshot setter. Live rescue mechanics remain MECH-04.

An independent review must verify the account equations, two-frontier invariant,
identity/escrow transitions, exception cleanup and actual test coverage. Keep
all memory persistence and fake-fence limitations visible in the report.
DATA-05 cannot be marked complete from this design or from a happy-path undo
demonstration alone. Durable disk/save acceptance and real GPU retirement remain
separate required gates.
