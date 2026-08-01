# WRECKWATER live payload schema

Status: codec plus native authority/client integration foundation

Schema: 5

Schema 5 adds fixed-capacity temporal redundancy to character movement.
Every movement packet carries one current sample and zero through three exact
older, unacknowledged samples. Each sample retains its own target tick,
movement-input sequence, axes, and one-shot flags. The global client request
sequence still orders the outer packet stream.

Action and snapshot record sizes are unchanged. Character input grows from 64
to 144 bytes, or 228 bytes with `PacketCodec`. Schemas 1 through 4 remain
rejected; there is no ambiguous compatibility mode.

`network/wreckwater_protocol.hpp` defines the application payload between the
authoritative WRECKWATER host and a client. The codec is independent from
`PacketCodec`, TCP framing, WebTransport, and WebRTC.

The live native authority uses:

- one authoritative `WebGpuSoft` float world;
- match and physics ticks paired at 60 Hz;
- certified full snapshots emitted at 20 Hz;
- two to three ticks of client interpolation;
- at most three ticks of visual extrapolation.

The codec itself does not schedule those systems.

## Trust boundary

An action request contains only:

- requested application tick;
- client-owned request sequence;
- helm, tow, cut, steal, or bank action;
- signed Q15 throttle and steering for helm input;
- logical cargo ID, generation, and observed revision for cargo actions;
- zero reserved fields.

It has no representation for a player, connection, crew, seat, skiff, match,
world, authority epoch, physics-evidence tick, body, or attachment identity.
The authenticated server ingress must derive and stamp all of those fields
before constructing a `game::MatchCommand`.

The separate character-input request contains:

- the global client request sequence;
- character handle and connection generation;
- one current movement sample;
- zero through three prior movement samples.

Every sample contains its requested application tick, signed Q15 planar
movement, jump/board flags, and per-connection-generation movement sequence.

Player, crew, seat, and skiff ownership still come from the authenticated
server roster. The character handle and generation are stale-packet fences,
not permission for the client to choose another player.

Snapshots contain stable network and logical identities. They never contain
`physics::BodyHandle`, `physics::AttachmentHandle`, buffer offsets, or other
GPU-private identities. A server-owned registry must map network identities to
runtime handles and must validate both generations. The first-slice authority
also publishes exactly four roster-bound character states.

## Integer and float encoding

Every integer and IEEE-754 binary32 value is written field by field in little
endian order. No C++ object representation, implicit padding, `memcpy` ABI, or
host endianness enters the payload.

The encoder:

- rejects nonfinite and out-of-range floats;
- rewrites negative zero to positive zero;
- rewrites subnormal floats to positive zero;
- normalizes each quaternion;
- selects one quaternion sign by examining `w`, then `x`, `y`, `z`;
- sorts entities by `(NetEntityId, netGeneration)`;
- rejects duplicate network or logical identities.

The decoder rejects negative zero, a non-unit quaternion, the noncanonical
quaternion sign, unsorted entities, duplicate IDs, stale zero generations,
unknown enums, broken cargo-to-skiff references, nonzero reserved fields, and
every trailing byte.

## Action request: 64 bytes

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | ASCII `VOXYWREQ` |
| 8 | 4 | schema version = 5 |
| 12 | 4 | payload type = 1 |
| 16 | 4 | encoded bytes = 64 |
| 20 | 4 | reserved = 0 |
| 24 | 8 | requested application tick |
| 32 | 8 | client request sequence |
| 40 | 4 | action |
| 44 | 4 | logical cargo ID |
| 48 | 4 | cargo generation |
| 52 | 4 | observed cargo revision |
| 56 | 2 | helm throttle, signed Q15 |
| 58 | 2 | helm steering, signed Q15 |
| 60 | 4 | reserved = 0 |

The request sequence must be nonzero. A helm request carries no cargo identity.
Its axes are in the canonical inclusive range `[-32767, 32767]`; `-32768` is
rejected. A cargo action requires a nonzero cargo ID, generation, and observed
revision, and both helm axes must be zero. The application tick is bounded by
the match protocol.

## Character input request: 144 bytes

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | ASCII `VOXYWCIN` |
| 8 | 4 | schema version = 5 |
| 12 | 4 | payload type = 3 |
| 16 | 4 | encoded bytes = 144 |
| 20 | 4 | reserved = 0 |
| 24 | 8 | current requested application tick |
| 32 | 8 | global client request sequence |
| 40 | 4 | character handle |
| 44 | 4 | connection generation |
| 48 | 2 | current movement X, signed Q15 |
| 50 | 2 | current movement Z, signed Q15 |
| 52 | 4 | current jump/board flags |
| 56 | 8 | current movement-input sequence |
| 64 | 4 | redundant sample count, 0 through 3 |
| 68 | 4 | reserved = 0 |
| 72 | 72 | three 24-byte redundant sample slots |

Each redundant slot is:

| Relative offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | requested application tick |
| 8 | 8 | movement-input sequence |
| 16 | 2 | movement X, signed Q15 |
| 18 | 2 | movement Z, signed Q15 |
| 20 | 4 | jump/board flags |

The global sequence, character handle, connection generation, and every used
movement sequence are nonzero. Both movement axes use
`[-32767, 32767]`; `-32768` is rejected. Unknown flag bits are rejected.
Unused redundant slots must be all zero.

Used redundant samples are oldest to newest. Their target ticks are
nondecreasing and their movement sequences strictly increase. The current
sample follows the same ordering. The codec rejects a noncanonical bundle, so
the authority can process its fixed array without sorting or allocation.

The value at offset 32 must equal the outer `PacketHeader::sequence`. It
continues across Helm, cargo, and movement traffic and provides packet replay
ordering. The current value at offset 56 starts at one for each authoritative
connection generation, advances only after a successful movement send, and is
the value acknowledged by certified character state.

## Certified full snapshot header: 128 bytes

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | ASCII `VOXYWSNP` |
| 8 | 4 | schema version = 5 |
| 12 | 4 | payload type = 2 |
| 16 | 4 | exact payload bytes |
| 20 | 4 | certified-full flag = 1 |
| 24 | 8 | session ID |
| 32 | 8 | match ID |
| 40 | 8 | world ID |
| 48 | 4 | world epoch |
| 52 | 4 | authority epoch |
| 56 | 8 | snapshot sequence |
| 64 | 8 | application tick |
| 72 | 8 | physics-evidence tick |
| 80 | 4 | phase |
| 84 | 4 | crew-one score |
| 88 | 4 | crew-two score |
| 92 | 4 | outcome |
| 96 | 4 | winner crew |
| 100 | 4 | authoritative match-state hash |
| 104 | 4 | authoritative event-stream hash |
| 108 | 4 | entity count |
| 112 | 4 | character count |
| 116 | 4 | reserved = 0 |
| 120 | 8 | exact serialized-byte hash |

The evidence tick cannot be newer than the application tick or more than 64
ticks behind it. Session, match, world, epochs, and snapshot sequence are
nonzero.

The serialized-byte hash is FNV-1a-64 over the complete payload with bytes
`[120, 128)` treated as zero. It detects corruption and gives replay/client
convergence an exact byte fingerprint. It is not authentication or a
cryptographic integrity proof.

## Entity state: 176 bytes

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | `NetEntityId` |
| 8 | 4 | network generation |
| 12 | 4 | kind: skiff or cargo |
| 16 | 4 | crew |
| 20 | 12 | signed sector XYZ |
| 32 | 12 | sector-local position XYZ |
| 44 | 16 | normalized quaternion XYZW |
| 60 | 12 | linear velocity XYZ |
| 72 | 12 | angular velocity XYZ |
| 84 | 4 | network shape |
| 88 | 12 | dimensions XYZ |
| 100 | 4 | packed material flags |
| 104 | 28 | cargo ID, generation, revision, disposition, owner, towing skiff |
| 132 | 12 | skiff ID, generation, disposition |
| 144 | 16 | logical attachment ID, generation, state |
| 160 | 16 | reserved = 0 |

Sector-local positions are in `[-128, 128)` metres. Linear velocity is bounded
to 500 m/s, angular velocity to 100 rad/s, and dimensions to
`[0.0001, 128]` metres. Sector coordinates are bounded to one million sectors
per axis.

A towed cargo entity must reference an included, active skiff with the exact
logical generation and crew. Its attachment identity is a registry-issued
logical ID, never the resident GPU attachment handle.

## Character state: 96 bytes

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | character handle |
| 4 | 4 | mode plus Active/Connected flags |
| 8 | 8 | player ID |
| 16 | 4 | connection generation |
| 20 | 12 | signed sector XYZ |
| 32 | 12 | sector-local feet position XYZ |
| 44 | 12 | world velocity XYZ |
| 56 | 4 | skiff ID |
| 60 | 4 | skiff generation |
| 64 | 12 | skiff-local feet position XYZ |
| 76 | 12 | skiff-local velocity XYZ |
| 88 | 8 | last applied character movement-input sequence |

The mode is Airborne, OnSkiff, or Swimming. An OnSkiff state must reference an
included skiff generation. Other modes require zero skiff identity and local
platform fields. Character records are sorted by player ID and have unique
player IDs and handles.

## Size and allocation proof

Schema 5 accepts 1 through 64 entities and zero through four character records.
The live 2v2 authority and the replays it records require exactly three
entities and four characters.

```text
snapshot bytes = 128 + entity_count * 176 + character_count * 96
first-slice snapshot = 128 + 3 * 176 + 4 * 96 = 1,040 bytes
outer PacketCodec frame = 1,040 + 84 = 1,124 bytes
realtime MTU headroom = 1,200 - 1,124 = 76 bytes
```

Decode validates magic, version, exact declared size, entity count, reserved
fields, and byte hash before allocating entity or character vectors. The
largest permitted allocations are fixed at 64 entities and four characters.

## Deliberate limits

- Schema 5 carries full snapshots, not deltas.
- Schema 5 describes the two skiffs, cargo, and four characters needed by the
  first slice.
- FNV-1a is not a MAC; transport authentication remains mandatory.
- The codec does not compare duplicated outer-packet identities.
- The codec does not own the identity registry, prediction, interpolation,
  replay retention, resend policy, or transport lifecycle.
- The native authority, bounded client runtime, and non-rendering process probe
  consume this payload. Visible character play remains a separate client layer.

## Client presentation buffer

`WreckwaterClientSnapshotBuffer` consumes already-decoded certified snapshots.
It is a presentation component, not another authority.

- The first snapshot pins session, match, world, world epoch, and authority
  epoch. A different identity is rejected until an explicit reset.
- Snapshot sequence must increase. Application and evidence ticks may remain
  equal, but never regress. A newer sequence at the same physics-evidence tick
  replaces that pose sample without growing history, even when its application
  tick advanced.
- Network, cargo, skiff, and attachment generations cannot regress. Their
  high-water marks live in separate fixed 64-entry lifetime registries, so
  evicting pose history cannot admit a stale generation. An attachment cannot
  move to a different entity at the same generation. Registry exhaustion
  rejects the snapshot; only an explicit identity reset clears the registries.
  A changed network generation or a missing entity is a visual discontinuity
  and snaps.
- History is a preallocated ring: 2 to 32 snapshots, with a configured entity
  cap no greater than the schema's 64 entities and fixed room for four
  characters.
- Visual time is explicitly the certified `physicsEvidenceTick`, not the later
  application tick that transported or applied the pose. Position uses
  sector-relative cubic Hermite interpolation at 60 Hz. It never converts a far
  absolute world position to binary32. Orientation uses shortest-path
  quaternion slerp.
- Visual extrapolation uses certified linear and angular velocity for at most
  three physics ticks. Later samples freeze at that limit and report stale.
- Character feet positions use the same sector-safe Hermite timeline and
  bounded extrapolation. A handle/player, connection generation, mode/flags, or
  OnSkiff platform-generation change is a discontinuity and snaps. Lifecycle,
  platform, and applied-input acknowledgement fields remain one exact
  authoritative record.

Every sampled entity or character deliberately separates:

```text
visualPose          interpolated or bounded-extrapolated presentation motion
authoritativeState  exact fields from one certified snapshot
```

Score, phase, outcome, winner, hashes, cargo disposition, attachment ownership,
shape, dimensions, material flags, and authoritative collision pose are copied
from one complete older or newer snapshot according to the configured policy.
They are never blended or predicted. The default policy is the newer snapshot.

The buffer does not render entities or characters, drive physics, estimate the
server clock, or integrate with the application loop.

## Client runtime boundary

`WreckwaterClientRuntime` composes one serial-aware
`IMultiplayerTransport`, the outer `PacketCodec`, the action, character-input,
and snapshot codecs, and the presentation buffer.

Each bounded `pump()`:

1. calls `transport.service()` exactly once;
2. drains at most the configured frame budget;
3. accepts data only from the configured server peer and connection serial;
4. accepts certified full snapshots only on the realtime lane;
5. verifies the outer session, world, epochs, sequence, and application tick
   against the inner certified snapshot;
6. passes accepted snapshots to the evidence-tick presentation buffer.

A nonzero configured serial pins an already-connected transport. A zero serial
starts inactive: only an ordered `Connected` frame with a nonzero serial
from the configured server peer may bind it. Data never creates or changes a
connection.

An exact `Disconnected` frame makes the runtime inactive. The caller may then
use `replaceTransport(...)` once to supply a replacement transport. That
transport remains inactive until its `Connected` frame supplies a serial
strictly greater than every serial previously bound by this runtime. Old-serial
data and lifecycle frames remain stale. Snapshot history, ACK history, visual
identity high-water marks, the last successful action tick, and the next client
request sequence survive the replacement.

For a deliberate client-side reconnect,
`disconnectForTransportReplacement()` requires an active known serial, closes
and fences exactly that local transport, and permits the same replacement
flow. It does not fabricate a lifecycle frame. Calling it while unbound or
already disconnected fails. A remote reconnect still requires the exact
`Disconnected` frame from the transport.

Helm samples use realtime `PacketPayloadType::Input` packets. Tow, Cut, Steal,
and Bank use reliable-event `PacketPayloadType::Command` packets. Character
movement also uses realtime `Input`.

A successful serial-qualified send commits one nonzero monotonic global client
request sequence and its application tick. A character send also commits its
independent movement-input sequence. Invalid payloads and transport
backpressure commit neither sequence, so the caller may retry the same values.

The caller supplies the application tick and normally sends one fresh Helm
sample per 60 Hz simulation tick. Input collection may coalesce more frequent
device samples before this boundary. Request ticks are nondecreasing.
A regressing tick is rejected without consuming a sequence.

Helm and character movement may target the same tick. For example, a Helm send
with global sequence `N` followed by movement for that tick uses global
sequence `N + 1`. Each payload's global client request sequence must equal its
outer packet sequence. The character movement-input sequence is independent.
The server's per-peer outer replay window observes the one global stream.
Same-tick movement replacement is separate: the bridge retains the highest
movement-input sequence for that player and tick.

Application payloads contain only:

- global client request sequence;
- one requested tick and Helm axes; or
- one requested tick plus cargo ID, generation, and observed revision; or
- character handle, connection generation, and up to four exact movement
  samples with their own ticks, axes, flags, and movement sequences.

The outer packet has routing session/world/epoch fields. No application payload
contains an actor/player, crew, seat, match, world, epoch, evidence tick, body,
or attachment identity. Character input carries only its stale-generation
fences; the server derives ownership from the peer roster.

The client starts the character movement-input sequence at one. Helm and cargo
traffic do not advance it. A transport reconnect alone does not reset it.
Supplying a strictly newer authoritative character connection generation
restarts movement at one; a stale generation or a different handle at the same
generation fails locally. Certified snapshots acknowledge this inner sequence,
not the global packet sequence.

After every successful character send, the runtime retains that exact sample
in a three-slot fixed array. The next packet appends all retained,
unacknowledged samples before its current sample. A logical sample therefore
appears in its original packet and the next three movement packets. When the
array is full, the oldest sample retires only after its fourth transmission.
The current sample is never silently replaced.

An accepted certified snapshot removes retained samples through its local
character's last-applied movement sequence. Disconnect, a newer generation,
or a local identity discontinuity purges the array. A generation change also
restarts the next movement sequence from the certified acknowledgement.

The authority validates the common handle and generation once, then processes
the canonical samples oldest to newest against a staged copy of the fixed
character bridge. Exact inner replays, lower same-tick winners, and already
closed samples are benign deduplication. Any hard failure discards the staged
copy. Jump and board flags remain attached to their original `(generation,
sequence, target tick)` record, so retransmission cannot fire them twice.

For movement packet `P[n]`, logical sample `n` is present in
`P[n]` through `P[n + 3]`. It survives alternating packet loss and every burst
of at most three consecutive movement-packet drops, provided one of those four
packets reaches the authority before the sample's target tick closes. This is
temporal redundancy, not reliability: four consecutive drops, insufficient
target lead, stopped input cadence, or congestion beyond the fixed authority
window can still produce neutral movement for that tick.

Client telemetry distinguishes retransmitted, acknowledged, capacity-retired,
and identity-purged samples. Authority telemetry distinguishes accepted
packets, unique accepted samples, replays, superseded samples, expired
samples, and hard rejections. Every cumulative 64-bit counter saturates at
`UINT64_MAX`; bounded high-water fields never wrap.

The runtime's replication state has fixed capacity after construction: bounded
snapshot history, fixed identity-lifetime registries, scalar telemetry, and no
internal inbound or outbound action queue. Character resend storage and
authority bundle staging are fixed-size and allocation-free. This is not a
zero-allocation steady-state claim for the complete network stack: the current
inner and outer codecs allocate temporary byte vectors on send and decode.
The separately configured transport may allocate, and explicit transport
replacement changes that transport-owned storage. Accepted frame sizes are
capped. Action frames are 148 bytes; character-input frames are 228 bytes.
The three-entity, four-character snapshot is 1,124 bytes framed, leaving
76 bytes of the 1,200-byte realtime MTU proof.

`NativeTcpClientTransport` names its remote server as peer `0` on its client
side. Its authenticated nonzero client peer ID is visible on the server side,
not used as this runtime's `serverPeerId`. It does not auto-redial. The caller
creates a replacement transport and supplies it through
`replaceTransport(...)`; subsequent pumps drive that transport until its
`Connected` lifecycle frame arrives.

The runtime does not:

- create or authenticate a transport connection;
- redial a disconnected native transport;
- encrypt traffic or make TCP suitable for the public Internet;
- estimate server time or choose interpolation delay;
- automatically retry failed reliable actions;
- integrate input devices, UI, rendering, or physics.
