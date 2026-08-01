# Server-authoritative networking slice

Phase 10 keeps transport, replication, interest, and physics islands as separate
systems. A static chunk key contains generator/content identity. An interest
cell controls replication. An island and authority epoch control who may mutate
connected physics state.

## Application session boundary

`MultiplayerSession` composes those systems without owning a socket or an
authentication policy. It runs in one of two roles:

- An authoritative server registers two or more authenticated client IDs,
  validates ownership and bounded future input, assigns canonical server
  sequences, advances one fixed-point island, and replicates a per-client
  interest view.
- A client predicts its controlled body, sends four-frame redundant input
  bundles, reconstructs acknowledged deltas, rolls back on a hash mismatch,
  and requests a full snapshot when a baseline is unavailable.

Deployments inject the socket-independent contract in
`network/session_transport.hpp`. Each data frame carries the authenticated
remote peer ID, a connection serial, and one of the existing realtime,
reliable-event, or reliable-control delivery classes. The same queue can expose
ordered `Connected` and `Disconnected` lifecycle frames. `MultiplayerSession`
ignores lifecycle frames, so existing packet/prediction users remain source
compatible. A live game host can consume the serial-aware contract without
depending on the fixed-point session implementation.

The legacy `send(peer, lane, bytes)` targets the currently active connection.
Lifecycle-aware code should use
`send(peer, connectionSerial, lane, bytes)`. That overload fails closed if a
same-ID reconnect happened after the triggering inbound frame was received.
Transports that do not implement connection identity retain serial `0` and the
original API.

Full snapshots use reliable control delivery. A delta uses realtime delivery
only when it is smaller than the full state and fits the conservative MTU.
Interest pressure fails closed: the controlled body is always present, then a
deterministically ranked local subset fills the negotiated per-client cap.
Capacity pressure never turns an interest view into disclosure of the whole
world.

Authority migration is scheduled for an exact future tick. The old epoch
remains active before that tick. At the switch, old-epoch commands are inert,
per-client delta histories reset, and clients receive a full snapshot that
rebases prediction onto the new epoch.

`pump()` is bounded. Its `false` result means at least one frame was rejected;
it does not mean the session died. Duplicate, stale, foreign, and malformed
traffic is counted in telemetry while valid frames continue to be drained.

## Trust boundary

Clients may send bounded movement, impulse, awake, and meteor-fire requests.
They cannot send correction, velocity, spawn, destroy, or authoritative
transform commands. The server validates client ownership, generation, tick
window, command rate/range, island ID, and epoch, then assigns the authoritative
sequence. Stale epochs and stale body generations are inert.

Accepted requests are ordered by server-owned
`(tick, priority, authenticated client ID, client sequence)` before the server
assigns authoritative sequences. Server-derived spawns, physical replay
commands, and correction events are retained in canonical bounded recordings;
oldest entries are evicted with telemetry when the configured cap is reached.

## Packet and transport contract

Packets use explicit little-endian fields for protocol/session/world identity,
world and authority epochs, sequence, 64-packet acknowledgement window, tick,
payload type, payload length, and checksum. Sequence and replay windows are
independent for realtime, reliable-event, and reliable-control lanes, so an
unordered reliable packet cannot age out behind realtime traffic. Realtime
frames are capped at 1200 bytes. Client-to-server frames are capped before
general decoding. Reliable streams add a length prefix because stream writes do
not retain message boundaries.

The bounded WRECKWATER action, character-input, and certified full-snapshot
payloads have a separate versioned contract in
[WRECKWATER live payload schema](wreckwater_wire_protocol.md). Payload schema
5 is integrated with the native authority and client probe. It remains
independent from the outer packet and transport versions.

### Native TCP fallback

`NativeTcpServerTransport` and `NativeTcpClientTransport` provide the first
native socket adapter for local multiplayer proofs. The server binds a real
nonblocking TCP listener, accepts an allowlist of nonzero peer IDs, and maps
authenticated clients to `IMultiplayerTransport` peer IDs. A client always
addresses the server as peer `0`.

The stream envelope is explicit and little endian:

```text
u32 magic | u16 version | u8 kind | u8 delivery | u32 payload bytes | payload
```

Native TCP framing and handshake wire version 3 is explicit. A v1, v2, or
unknown version is rejected. It can carry WRECKWATER payload schema 5; those
version domains are intentionally independent:

```text
ClientHello payload:    u32 peer ID | 32-byte deployment key |
                        32-byte expected build-content digest
ServerAccepted payload: u32 peer ID | u64 connection serial |
                        32-byte server build-content digest
Data payload:           application bytes
```

The server compares the key against its configured allowlist without
data-dependent early exit. Credentials are never written to logs or telemetry.
It also compares all 32 content-digest bytes before moving the socket to the
authenticated phase or admitting the peer to the active roster. A mismatch
closes the pending socket without publishing `Connected`.

The client independently compares all 32 bytes returned by the server before
publishing its own `Connected`. The expected digest must be nonzero in both
endpoint configurations. A dedicated mismatch counter is telemetry only; it
does not expose either the deployment key or any file bytes.

The server owns a nonzero, monotonically increasing 64-bit serial namespace for
the transport lifetime. The accepted message gives the client the same serial;
both endpoints tag every connection-bound data frame with it. The serial does
not need to be repeated in each TCP data envelope because bytes cannot move
between TCP connections. Exhaustion is fail-stop: the maximum serial may be
used, but later accepts are rejected rather than wrapping through zero.

An authenticated connection publishes `Connected` before its first data.
A socket loss preserves completely decoded data before `Disconnected` and
discards only partial framing and unsent writes. A newly authenticated socket
for an existing peer publishes old
`Disconnected`, then new `Connected`. The replacement discards the old
socket's partial read, inbound data, and outbound writes before the new peer can
resume. Serial-qualified sends to the old connection fail.

The SHA-256 value is an unkeyed compatibility identity, not a signature. This
handshake is deliberately only a local/private-network bootstrap. TCP does not
encrypt the key, cryptographically authenticate the server, or prevent
captured-hello replay. An Internet deployment must put this adapter behind an
authenticated encrypted tunnel, or replace it with TLS/mTLS or QUIC and rotate
credentials.

All descriptors are nonblocking. `service()` uses zero-timeout progress with
fixed accept, read, write, and decode budgets. `MultiplayerSession::pump()`
calls it exactly once, then drains at most its configured frame budget.
`poll()` only removes an already-decoded frame; it never hides another socket
service quantum inside each drained frame. `send()` only appends to a bounded
connection queue.
Per-peer output queues, per-peer input shares, and the shared input queue have
both frame and byte caps. Lifecycle events have a separate cap and every
authenticated connection reserves space for its future disconnect event.
Admission fails before replacing a peer if the bounded lifecycle queue cannot
represent the transition. A full output queue makes `send()` return `false`; a
full input queue stops consuming that stream so kernel TCP backpressure
applies. Disconnect,
failed authentication, malformed framing, oversized input, same-ID reconnect,
and close all discard connection-owned partial buffers. Socket writes suppress
`SIGPIPE`.

TCP is a compatibility fallback, not QUIC:

- realtime, reliable-event, and reliable-control frames are all reliable and
  ordered on one byte stream;
- delivery tags still preserve the session's independent packet sequence and
  acknowledgement windows;
- a lost or delayed reliable segment blocks every later lane, so realtime
  traffic has head-of-line latency;
- the realtime payload cap remains 1,200 bytes; a deterministic
  application-frame adversity decorator can test bounded session behavior,
  while a datagram transport and network emulator are still required for
  real packet/congestion behavior;
- the adapter does not automatically redial; the host constructs a fresh
  client endpoint, while the server performs bounded same-ID reconnect cleanup.

### Deterministic adversity decorator

`DeterministicAdversityTransport` decorates `IMultiplayerTransport` without
changing wire identity. Peer ID, connection serial, delivery class, lifecycle
type, and bytes are copied exactly. A seeded SplitMix scheduler advances by one
virtual quantum per `service()` call. Separate incoming and outgoing policies
bound delay/jitter, realtime loss, duplication, and reordering.

Storage is fixed at creation: every configured peer receives a fixed data
queue in each direction plus reserved incoming lifecycle slots. Reliable
output reports backpressure before consuming scheduling state when the queue
is full. If its underlying transport rejects a due reliable frame, the exact
entry remains queued for retry. Realtime frames may be deliberately dropped.
The internal scheduling sequence uses its maximum value once and then refuses
new work rather than wrapping.

An optional fixed one-frame realtime holdback supports terminal convergence.
A later data frame confirms the selected loss and discards the holdback. If
`Disconnected` arrives first, the held frame is delivered before that
lifecycle event. This does not make intermediate realtime traffic reliable;
it protects only the last complete application frame already observed by the
decorator at an ordered shutdown. It cannot recover bytes never delivered by
the underlying transport.

Lifecycle ordering is fenced independently of adversity. A connection's
`Connected` frame is delivered before its retained data, and `Disconnected`
afterward. A strictly newer connection serial removes delayed data belonging
to the replaced connection. `close()` clears every delayed frame before
closing the underlying endpoint; later `service()` and `poll()` calls cannot
deliver anything.

This is application-frame adversity above TCP. It can prove deterministic
retry, replay-window, lifecycle, and convergence behavior. It does not emulate
IP packetization, TCP retransmission or congestion control, UDP/QUIC datagram
semantics, cross-traffic, NAT, or Internet security.

The browser adapter in `web/network_transport.js` prefers WebTransport
datagrams plus a reliable stream. If connection or sending fails, it switches
to three WebRTC DataChannels:

- unordered, zero-retransmit realtime;
- unordered reliable events;
- ordered reliable control.

WebRTC signaling is deliberately injected by the deployment. Credentials,
ICE/TURN policy, authentication, and the WebTransport server endpoint do not
belong in the physics core. The transport choices follow the current
[WebTransport API](https://www.w3.org/TR/webtransport/) and
[WebRTC DataChannel protocol](https://datatracker.ietf.org/doc/html/rfc8831).

## Replication and rollback

Input packets carry the newest four frames. The receiver deduplicates by tick
and sequence, so a later packet can recover an earlier lost frame that was sent
ahead of its target tick. Tick sync chooses the lowest-RTT sample in a bounded
window and recommends a fixed input lead.

Snapshots are full until a client explicitly acknowledges a retained baseline.
Deltas contain only changed canonical bodies and removed IDs; application must
reconstruct the advertised state hash exactly. Missing baselines force a full
snapshot.

Each prediction bubble retains 64 ticks by default. On a matching island hash,
old history is confirmed. On mismatch, it restores the authoritative island at
tick `T`, records correction events, and replays buffered local commands through
the present tick. Presentation smoothing is outside authoritative state.

## Bounded two-client fixture

`TwoClientMeteorBoxSandbox` runs two owned bodies, a fixed box collision proxy,
and server-spawned meteors in one Lockstep island. The test covers:

- two independent predicted clients;
- input redundancy with one omitted packet;
- authoritative snapshots and acknowledged deltas;
- forced island rollback and command replay;
- meteor spawning and interaction;
- server correction recording;
- stale-epoch rejection;
- rejection of a client-authored correction.

The current Lockstep corpus uses sphere collision proxies; the visible box can
use body 3 as its fixed proxy until the strict fixed-point box manifold lands.

`test_multiplayer_session.cpp` exercises the app-facing composition with four
clients. It drops an input packet, reverses and duplicates delivery, recovers
the missing input from redundancy, applies acknowledged deltas, loses a
correction snapshot, converges from the next delta, and verifies an exact
future-tick authority switch. It also delays reliable traffic behind more than
a replay window of realtime traffic. Further cases cover nonzero-tick joins,
same-session reconnects, authority rebases with future predicted input,
fail-closed interest overflow, malformed and oversized client traffic,
deterministic same-tick conflicts, send failures, bounded recording, and
transport move/close ownership. A policy test expands a client fire request
into a server-only spawn command.

```bash
nix-shell --run 'bazel test //tests:networking_ci \
  --runs_per_test=2 --test_output=errors'
nix-shell --run 'bazel test //tests:native_tcp_transport \
  --test_output=errors'
node --check web/network_transport.js
```
