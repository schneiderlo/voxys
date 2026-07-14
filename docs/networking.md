# Server-authoritative networking slice

Phase 10 keeps transport, replication, interest, and physics islands as separate
systems. A static chunk key contains generator/content identity. An interest
cell controls replication. An island and authority epoch control who may mutate
connected physics state.

## Trust boundary

Clients may send bounded movement, impulse, awake, and meteor-fire requests.
They cannot send correction, velocity, spawn, destroy, or authoritative
transform commands. The server validates client ownership, generation, tick
window, command rate/range, island ID, and epoch, then assigns the authoritative
sequence. Stale epochs and stale body generations are inert.

Every accepted request, server-derived spawn, physical replay command, and
correction event is retained in canonical `(tick, priority, sequence)` order.

## Packet and transport contract

Packets use explicit little-endian fields for protocol/session/world identity,
world and authority epochs, sequence, 64-packet acknowledgement window, tick,
payload type, payload length, and checksum. Realtime frames are capped at 1200
bytes. Reliable streams add a length prefix because stream writes do not retain
message boundaries.

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

```bash
nix-shell --run 'bazel test //tests:networking_ci \
  --runs_per_test=2 --test_output=errors'
node --check web/network_transport.js
```
