# Session events: observation wire format 1

Implemented by `src/game/expedition/session_events.hpp/.cpp`. This is an outbound
notification codec, **not** an authenticated command, journal, save or replication
protocol. DATA-06 stage-a1 proves the bounded transport/codec; stage-b1 integrates
GameSession publication, baseline capture and application incarnation generation.
Wire version 1 is unchanged. The static cove emits only its actual admission
closure; it does not produce physical, economic or presentation facts.

All integers are unsigned little-endian with exactly the widths below. No C++
structure bytes are serialized. Object counters are scoped by the envelope's
world; all in-memory object IDs must match that world. Every present object,
request, epoch, journal writer and journal sequence is nonzero. Build topology,
logical tick, session revision and baseline frontiers may be zero. An expected
request sequence of zero means exhaustion. JavaScript uses binary BigInt reads
or canonical decimal strings, never a Number conversion for 64-bit values.

## Envelope: 96 bytes

| Byte offset | Width | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `VSEV` |
| 4 | 2 | Envelope version, 1 |
| 6 | 2 | Kind: receipt 0x0001, state 0x0002, closure 0x0003, diagnostic 0x0300 |
| 8 | 2 | Payload version, 1 |
| 10 | 2 | Payload length: 56, 64, 24 or 48 respectively |
| 12 | 1 | Lane: domain 1, presentation 2, telemetry 3 |
| 13 | 1 | Tick basis: logical boundary 1; completed simulation 2 is reserved |
| 14 | 1 | Bit 0: build reference present; every other bit zero |
| 15 | 1 | Reserved, zero |
| 16 | 16 | Nonzero world namespace |
| 32 | 16 | Fresh nonzero public stream incarnation, distinct from world |
| 48 | 8 | Authority epoch |
| 56 | 8 | Per-lane publication sequence, starts at 1 |
| 64 | 8 | Logical observation tick |
| 72 | 8 | Relevant session revision |
| 80 | 8 | Build counter, or zero if absent |
| 88 | 8 | Build topology revision, or zero if absent |

The incarnation must never be copied/derived from an admission secret. The
composition root must supply a fresh public 128-bit identity on re-entry,
recovery, hub replacement or token-generation change. The codec checks nonzero
and distinction from world, but cannot establish global freshness or discover
whether a caller leaked a secret. That is a composition-root invariant. Current
transport fixtures use explicit fixed identities and are not a production ID
source. Multi-participant independent admission changes need NET-01's public
per-admission identity.

Receipt/state/closure payloads require the domain lane. Diagnostic summaries
require telemetry. No implemented payload is accepted into presentation yet.
No implemented payload claims completed-physics evidence. Reserved force,
attachment, damage, cargo, reward and presentation kinds reject without creating
a partially valid record. Future producers require their own versioned schemas.

## Receipt payload: 56 bytes

Offsets below are relative to byte 96.

| Offset | Width | Meaning |
|---:|---:|---|
| 0 | 8 | Public participant counter |
| 8 | 8 | Request sequence, scoped by incarnation/epoch |
| 16 | 1 | Outcome: pending 1, rejected 2, committed 3 |
| 17 | 1 | Durability: volatile 1; other values rejected |
| 18 | 2 | Flags: object present 1, journal present 2 |
| 20 | 2 | Stable session error code |
| 22 | 2 | Stable build error code |
| 24 | 8 | Expected next request sequence, greater than this request or zero if exhausted |
| 32 | 8 | Result object counter or zero |
| 40 | 8 | Journal writer generation or zero |
| 48 | 8 | Journal sequence or zero |

Pending receipts require a journal admission reference and no issue or result
object. Committed receipts have no issue. Rejected receipts require an error
and have no result object. A terminal journal reference is optional for an
explicit fail-stop path; absence never implies durability. Journal references
share the header's world/epoch and do not acknowledge storage.

`EventSessionCode` and `EventBuildCode` in the header freeze explicit 16-bit
values and exhaustive source-enum adapters. None is 0; current nonzero session
codes use 0x1001–0x1024, build codes 0x2001–0x201c. Unknown values reject. The
adapters use named switch cases and never cast current source-enum ordinals into
wire values. For example Session Capacity is 0x1002, Build Capacity is 0x2001.
Adding a source error must receive a deliberate public code and fixture review.

## Accepted state payload: 64 bytes

| Offset | Width | Meaning |
|---:|---:|---|
| 0 | 8 | Optional cause participant counter |
| 8 | 8 | Optional cause request sequence |
| 16 | 2 | Flags: cause 1, primary object 2, journal 4 |
| 18 | 2 | Nonempty changes: builds 1, inventory 2, jobs 4, history 8, entitlements 16 |
| 20 | 1 | Durability: volatile 1 |
| 21 | 3 | Reserved, zero |
| 24 | 8 | Optional primary object counter |
| 32 | 8 | Absolute resulting salvage-material balance |
| 40 | 8 | Absolute resulting special-machinery balance |
| 48 | 8 | Optional journal writer generation |
| 56 | 8 | Optional journal sequence |

No event asks a consumer to add/subtract inventory. Trusted lifecycle changes
such as entitlement retirement can omit a player cause. Multi-object details
remain in canonical state/journal; the summary does not invent per-part events.

## Closure payload: 24 bytes

| Offset | Width | Meaning |
|---:|---:|---|
| 0 | 2 | Reason: host closed 1, journal fault 2 |
| 2 | 2 | Bit 0 journal present; other bits zero |
| 4 | 4 | Reserved, zero |
| 8 | 8 | Optional journal writer generation |
| 16 | 8 | Optional journal sequence |

Closure has no build reference. GameSession publishes this once after pending
terminal decisions; destructor/repeated close does not replay the transition.

## Diagnostic payload: 48 bytes

| Offset | Width | Meaning |
|---:|---:|---|
| 0 | 2 | Code: ingress rejected 1, observer overwrites 2, owner-boundary cost 3 |
| 2 | 2 | Bit 0 count saturated; other bits zero |
| 4 | 4 | Reserved, zero |
| 8 | 8 | Cumulative or sampled-interval count |
| 16 | 8 | Observation interval first logical tick |
| 24 | 8 | Observation interval last logical tick |
| 32 | 8 | IEEE-754 binary64 mean boundary cost, microseconds |
| 40 | 8 | IEEE-754 binary64 maximum boundary cost, microseconds |

The interval must be ordered and end no later than the envelope tick. No build
reference is accepted. Codes 1/2 require both scalar values zero. Code 3 measures
real timed owner boundaries: finite mean/max, max ≥ mean ≥ 0, and zero samples
require zero values. Negative zero is noncanonical. Saturation requires count
UINT64_MAX. No diagnostic event producer is installed through stage-b1; actual
ingress counts are available out of band, and codec/ring tests label their
diagnostic records synthetic. Later measured telemetry can describe an older
logical boundary; it does not imply force, impulse or completed solver evidence.

## Validation and reader behavior

Decode accepts exactly one record: unknown versions/kinds, invalid identities,
invalid codes/values, wrong lane/tick basis, truncated or trailing bytes reject.
Unused fields and reserved flags must be zero. Bounded canonical re-encoding
checks absent-field encodings without serializing native padding. Encode errors
leave the destination untouched; successful encoding leaves any tail untouched.

Three separate overwrite rings provide 256 domain, 256 presentation and 1,024
telemetry slots. Only the owner changes them, synchronously; this is not a
thread-safe cross-thread queue. Hub allocation happens once at creation; failure
returns null. Reader views are borrowed and cannot outlive the owner. Publication,
read, validation, counters and codec operations are no-throw and allocation-free.
The hub's writer is private to GameSession; a consumer can only copy records.
The detail ring is a standalone transport primitive, not access to a live hub.

Cursors contain incarnation, lane, last-consumed frontier and envelope version.
`read` returns a proposed cursor; it never updates caller state or acknowledges
storage. Errors write no records. Zero output capacity returns NeedCapacity;
wrong identity/lane/version or a future cursor rejects. A cursor below oldest−1
returns Gap. Reads advance only through copied records and do not silently skip
a gap. Occupancy/high-water/oldest/newest, overwrite count, invalid-input count
and exhaustion status are available out of band, without recursive events.

Diagnostic counters saturate, with a flag set on attempted overflow. The last
UINT64_MAX event can publish once; later publication returns Exhausted without
wrapping. Domain publication rejects regressed revision or tick; repeated equal
boundaries are permitted. Lanes have no relative order. Telemetry flood cannot
evict domain records. No reader pins memory or resets an exhausted lane.

After a domain gap, `GameSession::eventBaseline` supplies one synchronous
owner-boundary copy of accepted state, public participant/entitlements, workshop
availability, bounded history choices, admission frontiers, visible receipts,
all stream cursors/statistics and diagnostics. Replacing a consumer model and
cursor separately is invalid. The baseline contains no admission token, request
intent or private inverse escrow. At most 66 receipts are copied: retained
terminal decisions followed by the ordered pending interval. A known canceled
later request stays publicly pending until its ordered terminal decision.

The baseline preflights an 8 MiB fixed-object-plus-elements bound, builds owned
temporary storage and returns only on complete success. This is not a portable
serialization size or a physical/save checkpoint. Failure leaves source state,
reader cursors and command accounting unchanged. Current event-baseline data
has no separate wire encoding: use this document only to encode event records.

`restartEventStream` resets all lanes in place using an owner-supplied fresh
incarnation. A borrowed reader remains valid until its GameSession is destroyed,
but all old cursors reject. State, journal, admission and lifetime ingress counts
are preserved. Old physical slot bytes fall outside occupancy and are overwritten
before being readable under the new identity. Invalid/current identities reject
without mutation; the trusted caller must not reuse any earlier incarnation.

[Stage-b1](stage-b1/README.md) proves live publication, baseline failure/overrun
recovery and reader lifetime in native and WASM tests, plus actual application
Reset/Leave/re-entry. Independent review is still required for DATA-06 acceptance.
