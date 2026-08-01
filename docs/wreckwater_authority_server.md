# WRECKWATER headless authority

`wreckwater_server` is the native first-slice authority for one four-player
match. It uses one headless WebGPU device, one `WebGpuSoft` `PhysicsWorld`, the
authoritative match and character cores, and the WRECKWATER payload schema 5
codec.

## Run

Build:

```bash
nix develop -c bazel build //:wreckwater_server
```

Start with four distinct 32-byte bootstrap keys:

```bash
bazel-bin/wreckwater_server \
  --bind 127.0.0.1 \
  --port 7777 \
  --key1 0000000000000000000000000000000000000000000000000000000000000001 \
  --key2 0000000000000000000000000000000000000000000000000000000000000002 \
  --key3 0000000000000000000000000000000000000000000000000000000000000003 \
  --key4 0000000000000000000000000000000000000000000000000000000000000004 \
  --max-ticks 0 \
  --replay-output wreckwater-match.wrpl
```

`--max-ticks 0` runs until `SIGINT` or `SIGTERM`. The loop is nonblocking and
paced from `std::chrono::steady_clock` at 60 Hz. Shutdown prints p50, p95, and
maximum server-loop time. The proof configuration keeps session, match, world,
world epoch, and authority epoch fixed at `1`.

`--replay-output` is optional. It writes only after an exact terminal physics
pose has certified the final match/event hashes. Interrupting a match or using
`--max-ticks` before completion does not emit a falsely finalized archive and
returns an error when replay output was explicitly requested.

The last line is stable and machine-readable:

```text
WRECKWATER_SERVER_FINAL tick=... sequence=... application=... evidence=... phase=... outcome=... winner=... score1=... score2=... state_hash=... event_hash=... serialized_hash=...
```

The snapshot fields describe the exact last snapshot built by the authority,
not a newer simulation tick that may still be waiting for asynchronous
readback.

Shutdown and fail-stop also print one stable diagnostic line:

```text
WRECKWATER_SERVER_RUNTIME tick=... fail_reason=... service_calls=... frames_polled=... lifecycle_frames=... match_fault=... transaction_aborts=... live_apply_failure=... peer1_active=... peer1_serial=... peer1_generation=... ...
```

It includes the atomic physics prepare/commit status, target ticks, operation
counts, replay record/payload/hash status, and each peer's active
serial/generation/sequence. Authentication keys are never included.

Each server and probe also prints the exact generated compatibility identity
at startup:

```text
WRECKWATER_BUILD_CONTENT content_digest=<64 lowercase hex digits> replay_prefix=<u64>
```

The digest is generated from the checked, explicit authority source/WGSL
allowlist. Native TCP framing and handshake version 3 exchanges all 32 bytes
in both directions while carrying WRECKWATER payload schema 5. The server
rejects a mismatch while the socket is still pending, before `Connected` and
before roster admission. The client checks the digest returned in
`ServerAccepted` before its own `Connected`.

Replay schema 2 retains only the generated digest's first eight bytes,
interpreted as a big-endian 64-bit integer. Native compatibility admission does
not use that truncated value.

## Security boundary

The TCP transport is authenticated but plaintext. Use it only on loopback or
a trusted private network. Public deployment requires TLS/mTLS, an encrypted
tunnel, or QUIC. The unkeyed content digest detects incompatible builds; it is
not a signature and does not make plaintext TCP a server-authentication
boundary.

Peer IDs are fixed and allowlisted:

| Peer | Player | Crew | Seat | Skiff |
|---:|---:|---:|---|---:|
| 1 | 1 | 1 | helm | 1 |
| 2 | 2 | 1 | deck | 1 |
| 3 | 3 | 2 | helm | 2 |
| 4 | 4 | 2 | deck | 2 |

The server derives every player, connection, crew, seat, skiff, world epoch,
authority epoch, application tick, and physics-evidence tick. Client action
payloads contain none of those authority identities.

Helm samples are accepted only from helm seats on `Realtime/Input`. Cargo
actions are accepted only on `ReliableEvent/Command`. Character movement is
accepted on `Realtime/Input` only for that peer's exact character handle and
connection generation. Every frame must match the active transport connection
serial and the server session/epoch. A schema-5 movement bundle uses one
common handle/generation fence and at most four canonical samples.

Per-peer sequence windows reject replay across Helm, cargo, and character
packets. Reconnection changes the connection serial and generation, but does
not reset either the outer packet replay high-water or the match-command replay
high-water. A reconnecting client must continue its monotonic global request
sequence. The independent character movement-input sequence restarts at one
for the new authoritative connection generation and is the value acknowledged
inside the character snapshot record.

## Tick and readback boundary

Before all four peers connect, the match and GPU world remain frozen. Each
active authority tick:

1. Drains already completed, contiguous GPU event and pose readbacks.
2. Closes vessel damage from the `ContactHit` packet and pose for the same
   exact physics tick.
3. Enqueues compartment water weight, manages bounded fragments, and
   translates sink, respawn, and attachment breaks to logical match events.
4. Services transport once and performs a bounded frame drain.
5. Enqueues server-derived helm commands for tick `N`.
6. Calls `WreckwaterMatch::step()` so atomic world mutations target tick `N`.
7. Reconciles sunk or respawned skiff bodies and logical bindings.
8. Schedules exactly one fixed GPU tick.
9. Uses checked encoding and submits exactly one command buffer.

Event closure and three-body pose readback are encoded every tick. A pose
becomes certified evidence only after event closure reaches that same exact
tick. This lets an attachment break on a non-snapshot tick retain exact
same-tick physics evidence. Each certified pose expands to seven logical
evidence entries; mounted player aliases copy their skiff position and
availability exactly.

The same certified pose supplies both skiff platform frames to the four-player
character bridge. Buffered movement closes only on that exact pose tick.
Character initialization, lifecycle, or certification disagreement fail-stops
the authority before a snapshot can publish.

The final Live/Overtime boundary has an explicit one-tick settlement. Before
the match may finish, the runtime waits for exact physics evidence through
logical tick `N`, so every sink or break already caused by gameplay can enter
the `N+1` match transaction. The submitted `N+1` physics tick is then
certification-only: its event packet is still structurally validated, but
post-whistle contacts cannot schedule an event after `Finished`. Its pose is
the exact final replay/snapshot boundary.

The native authority allocates 32 event and 32 pose readback slots. Checked
encoding treats both streams as mandatory: if either copy cannot reserve a
slot, the backend fail-stops before retiring the host tick. If a break packet
maps before its matching pose, the runtime pauses simulation, continues
servicing bounded I/O, and submits the break only after that exact pose is
certified.

Each internal GPU event record is 96 bytes. A `ContactHit` carries both
generational body handles, both feature IDs, both body-local contact anchors,
the canonical A-to-B normal, summed manifold normal impulse, and maximum
pre-solve impact speed. The GPU canonicalizes body order and swaps anchors,
features, and normal together. The host stable-sorts the decoded batch before
damage consumes it. A duplicate contact identity, stale generation, malformed
normal, non-finite value, gap, or overflow fail-stops the server.

The event timeout is 16 ticks, strictly below the 32-slot ring. Deterministic
tests hold callbacks for 9 and 16 ticks and require an exact ordered drain,
while 17 ticks must fail-stop before a slot can alias. A long 16-tick-delay run
also proves that at most 17 packets are live, leaving 15 unused ring slots.

Network snapshots remain 20 Hz, publishing certified pose ticks 1, 4, 7, and
so on. They use global stable network IDs 1 and 2 for skiffs and 3 for the
reactor. They contain all three entities, all four roster characters,
canonical sector/local transforms, velocities, shapes, logical match state,
and convergence hashes. The payload is 1,040 bytes; its 1,124-byte outer
realtime frame leaves 76 bytes below the 1,200-byte limit.

Any GPU event gap, overflow, pose gap, readback timeout, unknown broken
attachment, match transaction failure, or checked-encoding failure permanently
fail-stops the authority. Failed network sends are isolated to that peer and
do not reuse a global snapshot sequence.

## Vessel damage, flooding, and lifecycle

The production authority uses `WreckwaterVesselDamageAuthority`. It is a
fixed-capacity, allocation-free simulation core:

- two skiffs;
- four compartments and six structural edges per skiff;
- four exterior edges per skiff, each able to open one breach;
- at most 64 exact contact hits per tick;
- at most eight significant physical fragments total;
- generational compartment, edge, breach, fragment, skiff, and body identity.

An impact is assigned to the nearest intact exterior edge using the exact
body-local contact anchor. Accumulated normal impulse is quantized with
saturation and scaled by the impacting material: timber, steel, or rock. A
broken exterior edge opens a breach and may create one bounded physical
fragment.

Each breach uses orifice flow:

```text
flow = discharge_coefficient * area * sqrt(2 * gravity * depth)
```

Ingress is accumulated at 60 Hz and capped by compartment volume. Every wet
compartment emits a world-down water-weight force at its body-local center.
The checked GPU command applies both force and `r x F`, so asymmetric flooding
produces roll torque.

Crossing the flooded-volume threshold submits a `SkiffSunk` match event. At
the event boundary, any tow is lost through the same authoritative match
transition, helm input is cleared, and the old physical body is destroyed.
After the fixed respawn delay, `SkiffRespawned` advances the logical skiff
generation. The server creates a fresh physical body generation, requires the
same primary body index, replaces the skiff/player bindings, and atomically
resets that vessel's topology. Any mismatch fail-stops.

The body proof is closed:

```text
3 primary bodies + 8 fragments = 11 resident bodies
C(11, 2) = 55 unique body pairs
64 pair/manifold slots >= 55
```

`WebGpuSoft` stores one body-body manifold per unique broad-phase pair.
Water and static terrain contacts use separate stages; the current server does
not attach terrain. If resident bodies, colliders, or the manifold model
change, this capacity proof must be updated before deployment.

This does not change the network snapshot schema. Snapshots still contain
exactly two skiffs and one cargo entity. Structural topology, breaches,
compartment water, and significant fragments are server-authoritative but are
not network-snapshotted or client-visible yet.

The older `WreckwaterSlice` one-dimensional damage model remains a test and
reference fixture. The live server does not use it for production authority.

The fake-boundary orchestration suite and the real headless WebGPU
warmup/tow/break, torque-command, and body-generation integration tests are:

```bash
nix develop -c bazel test \
  //tests:wreckwater_authority_runtime \
  //tests:wreckwater_authority_gpu
```

## Current first-slice limits

- The native process proof uses extraction centers at the two skiff starts
  (`x = -12` and `x = 12`), a 24 m extraction radius, an 18 m tow rest length,
  and a finite 10,000,000-unit break threshold. These are proof-only balance
  values chosen to exercise Tow, Steal, Cut, and a committed Bank in one short
  run; they are not final authored-cove gameplay tuning.
- Vessel topology is a coarse four-compartment, box-body model. Flooding adds
  localized water weight, but does not yet rebuild hull buoyancy volume,
  hydrodynamic drag, or a full fluid center of mass.
- Damage currently comes from physical contact. Player cutting, weapons,
  repair, and authored fracture meshes are not wired.
- Significant fragments collide on the server, but have no snapshot entity,
  renderer mesh, VFX, or client prediction.
- Replay records match lifecycle events, but final compartment, breach,
  topology, and fragment checkpoint coverage is not wired yet.
- Cargo loss outside the authored play volume is not derived yet.
- Extraction centers are static runtime configuration; authored-cove wiring is
  still required.
- `PacketCodec` and WRECKWATER codecs allocate vectors during ingress and
  snapshot encoding.
- The native TCP transport has no TLS.
- Renderer clients are not part of this server binary.
