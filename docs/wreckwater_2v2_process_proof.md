# WRECKWATER native 2v2 process proof

This proof launches five independent native OS processes:

- one headless `wreckwater_server`;
- helm clients for peers 1 and 3;
- deck clients for peers 2 and 4.

It uses the real authenticated native TCP transport, outer packet codec,
WRECKWATER payload schema 5, server GPU authority, and client snapshot runtime.
It is a transport and convergence proof. It is not a rendering client.

## Run

```bash
./scripts/run_wreckwater_2v2.sh
```

The script:

1. enters one Nix development shell, then performs one Bazel build of
   `//:wreckwater_server` and `//:wreckwater_client_probe`;
2. creates four distinct per-run 32-byte development keys without printing
   them;
3. starts the server on an ephemeral loopback port;
4. starts the four exact, already-built client binaries together and requires
   all four authenticated `CONNECTED` records before continuing;
5. wraps each client transport in a deterministic, fixed-capacity
   application-frame adversity scheduler;
6. deliberately reconnects peer 1 through a new TCP transport;
7. waits under a bounded process timeout and performs PID-specific cleanup;
8. requires the server and every client to report the same final certified
   snapshot tuple, excluding only the server's separate simulation tick;
9. requires all four peers to prove a stable local character handle,
   nonzero sent/applied movement, nonzero certified feet displacement, and
   one common final four-character roster hash;
10. requires peer 1 to prove generation-2 movement after reconnect and that
    retained generation-1 snapshot history could not unlock local input;
11. requires a transaction-clean server, the exact
   Tow, Steal, Cut, Tow, Bank chain, a 1,000-to-0 score, and exercised
   loss/duplicate/reorder counters;
12. retains all five logs and prints their directory.

`WRECKWATER_PROOF_SERVER_TICKS`, `WRECKWATER_PROOF_TIMEOUT_SECONDS`, and
`WRECKWATER_PROOF_CONNECT_TIMEOUT_SECONDS` may override the bounded defaults.
`WRECKWATER_PROOF_CHAOS_SEED` selects the deterministic seed. The default is
nonzero and runs the required adversity profile; `0` is a no-adversity
diagnostic control.
`WRECKWATER_PROOF_LOG_DIR` may select the retained log directory.
If that directory is already nonempty, the script creates a unique child and
does not overwrite the earlier proof.

If already inside `nix develop`, the script reuses that shell instead of
nested evaluation. No client or server process invokes Nix or Bazel.

## Client probe CLI

```text
wreckwater_client_probe
  [--server ADDRESS] [--port PORT]
  --peer 1..4 --key HEX64
  [--session ID] [--match ID] [--world ID]
  [--world-epoch EPOCH] [--authority-epoch EPOCH]
  [--max-ticks COUNT] [--reconnect-at PUMP_TICK]
  [--chaos-seed SEED]
```

The key parser requires exactly 64 hexadecimal characters, decodes them
directly into 32 fixed bytes, and never logs them. The proof identities match
the server defaults: session, match, world, world epoch, and authority epoch
are all `1`.

`--max-ticks` is a 60 Hz client-pump timeout. Reaching it is an error. A normal
proof client completes when the server closes the authenticated connection
after publishing snapshots. `--reconnect-at 0` disables deliberate reconnect.
`--chaos-seed 0` disables the adversity decorator.

## Deterministic application-frame adversity

The proof profile advances only when the client calls `service()`. Each
service is one virtual scheduler quantum. Both directions receive 5 through
10 quanta of seeded delay: about 83 through 167 milliseconds at the probe's
60 Hz cadence. Realtime snapshot, Helm, and character-input frames have 10%
seeded loss.
Client-to-server traffic also has bounded duplication and reordering.
Reliable cargo Command frames may be delayed, duplicated, or reordered, but
the decorator never intentionally drops their original copy.

Incoming snapshots remain ordered after loss. The WRECKWATER runtime rejects
duplicate or non-monotonic certified snapshot sequences, so silently accepting
those would weaken the client contract. Outgoing reliable duplicates instead
exercise the server's packet replay window; the first exact packet may commit
and its byte-identical duplicate is rejected without aborting the transaction.
The proof profile also reserves one fixed realtime holdback per peer. A later
data frame confirms and discards a selected loss. If `Disconnected` arrives
first, that last frame is delivered before the lifecycle event. Earlier
snapshots still drop, while the server's terminal certified snapshot remains
available for the exact five-process convergence check.

Every peer owns fixed data and lifecycle slots. Full reliable output returns
backpressure without changing the caller's packet, request sequence, or tick.
An underlying reliable send failure keeps the exact scheduled bytes for a
later service. Lifecycle slots are separate: `Connected` precedes retained
data, `Disconnected` follows it, and a strictly newer serial purges delayed
frames from the replaced connection. `close()` purges all slots, and no frame
is delivered afterward.

This layer operates on complete application frames above authenticated TCP.
The TCP stream itself is still reliable and ordered. Therefore this proves
application behavior under deterministic delay, deliberate realtime-frame
suppression, repeated frames, and changed application send order. It does not
model IP packet loss, TCP congestion windows, retransmission timing, UDP or
QUIC datagrams, path MTU, queue disciplines, NAT, or a public Internet path.
It is also not a security boundary.

## Scripted roles

During Live or Overtime, peers 1 and 3 send small deterministic Helm samples
only on realtime Input packets. Their application ticks stay within four
ticks of the latest certified snapshot. They stop producing Helm traffic once
the cargo is Banked or Lost. That bounded quiet tail lets this loopback proof
observe the last server snapshot before terminal TCP close without racing
still-arriving probe input.

All four peers also emit deterministic character movement through
`sendLocalCharacterInput()`. Identity is bound only from the accepted
certified roster for that peer ID; the probe never supplies a raw character
handle or connection generation to a send API. Movement starts during warmup
so the proof can measure deck-local displacement before a steep simulated
skiff sheds its passengers. Each accepted snapshot can produce one movement
sample at `physicsEvidenceTick + 24`. The chaos path
spends 5 through 10 service quanta inbound and another 5 through 10 outbound,
so the sample arrives 4 through 14 ticks ahead of the live certified
character frontier. That fits the existing 16-tick bridge window and the
proof server's 16-tick outer request-lead cap.

The four fixed movement axes are `+X`, `-X`, `+Z`, and `-Z` for peers 1
through 4. Every successful safe-API send creates one new logical sample. Its
schema-5 packet also carries the three most recent exact unacknowledged
samples. A logical input is therefore available in four consecutive movement
packets without changing its target tick, sequence, or one-shot flags.
Certified last-applied sequences remove acknowledged history. Reconnect and
identity discontinuity purge it.

Only transport backpressure retries the same new logical input. Movement owns
its own nondecreasing requested-tick domain, independent of the Helm/cargo
requested-tick domain; monotonic outer packet sequences still order all
traffic together.

During Live or Overtime, peers 2 and 4 inspect the exact authoritative cargo
record:

- peer 2 requests Tow for free cargo;
- peer 2 requests Bank for crew-one cargo;
- peer 2 requests Cut for crew-two cargo;
- peer 4 requests Steal for crew-one cargo;
- peer 4 requests Bank for crew-two cargo.

The two deck probes derive alternating turns from the certified cargo revision.
They never race two commands against the same revision. The proof sequence is
Tow, Steal, Cut, Tow, then Bank, so every cargo command crosses the real TCP
and authority path before the cargo becomes terminal.

These are reliable-event Command packets. No client supplies actor, crew,
seat, skiff, connection, world, epoch, or physics-evidence identity. The
server derives those fields.

Failed transport enqueue leaves the same request tick and sequence pending.
The scripted state commits only after enqueue succeeds. A deliberate
reconnect fences the old serial, creates a new transport, learns a strictly
newer serial from `Connected`, and verifies that request sequence, ACK state,
snapshot history, and identity high-water state survived.

## Convergence records

Each client prints a final machine-readable record:

```text
WRECKWATER_CLIENT_FINAL peer=... sequence=... application=... evidence=... phase=... outcome=... winner=... score1=... score2=... state_hash=... event_hash=... serialized_hash=...
```

It also prints one character certificate. `certified_feet_mm` is measured
only while the character remains linked to its original skiff identity.
Clearing deck-local fields during an air/water transition cannot satisfy it:

```text
WRECKWATER_CLIENT_CHARACTER_FINAL peer=... roster=4 roster_hash=... handle=... stable=1 generation=... sent=... generation_sent=... ack=... certified_feet_mm=... minimum_deck_displacement_mm=50 post_reconnect_sent=... post_reconnect_ack=... old_generation_blocked=... target_lead=24 redundancy=fixed_window redundancy_window=3 retransmitted=... acknowledged=... retired=... history_purges=... max_redundancy=3
```

`ack` is the latest certified authority sequence and must advance. The
`acknowledged` telemetry counts only samples which were still inside the
three-entry resend history when that certification arrived; it can therefore
be zero when the round trip is longer than the resend window. `retired`
records samples which completed all four transmissions before their ACK
arrived.

The server record is checked separately. `character_samples` must exceed
`character_packets`, proving that at least one surviving bundle recovered
multiple unique logical samples. `character_replayed` must be nonzero,
proving repeated history was deduplicated, and `character_rejected` must stay
zero.

The script removes only the peer prefix and requires the remaining tuples from
all four clients to match exactly. It normalizes the server's numeric enum
spellings and requires the same certified snapshot tuple there as well.
It rejects a missing or duplicate character certificate, movement evidence
below 50 mm, repeated local handles, a divergent final roster hash, or an
unexpected connection generation. Peer 1 must additionally report nonzero
generation-2 sent and applied sequences plus stale-history rejection.
Every peer must report a full three-sample resend window, actual redundant
transmissions, and snapshot-driven acknowledgement retirement.
Missing snapshots, timeout, stale frames, runtime faults, failed reconnect,
process failure, or tuple disagreement makes the proof exit nonzero.
The final `WRECKWATER_CLIENT_CHAOS` records must also report zero scheduler
faults and sequence exhaustion. The runner rejects a proof unless realtime
loss, duplication, and actual reordering were all observed.

Operational snapshot logs are bounded: the probe reports the first snapshot,
cargo/phase/outcome/score transitions, and a once-per-simulated-second
heartbeat. A `WRECKWATER_CLIENT_SNAPSHOT_STATS` record reports exact accepted,
logged, and skipped-log totals before the final tuple. A four-minute steady
match therefore produces about 240 heartbeat records per client instead of
4,800 snapshot records.

## Security and allocation limits

The transport authenticates a plaintext TCP bootstrap. The script binds only
to loopback. This is not public-Internet security; deployment needs TLS/mTLS,
QUIC, or an authenticated encrypted tunnel.

The probe has bounded snapshot history, a fixed three-sample movement resend
array, and fixed-capacity adversity queues. The codecs and native transport
still use bounded temporary vectors and socket queues, so this is not a
zero-allocation steady-state claim for the complete networking stack.

## Current limits

This is a short, non-rendering authority and convergence proof. Its default
360-tick world uses a proof-stable finite tow and extraction zones near the
spawn points so Tow, Steal, Cut, and Bank all commit during the run. It does
not establish production rope tuning, map balance, human input, Internet
security, real network packet-loss behavior, congestion response, long-match
soak, client frame time, or the graphical product slice.

The four clients must agree with their server inside one run. Independent runs
may publish a different final snapshot sequence or certified evidence tick
because asynchronous GPU readback can change which eligible 20 Hz publication
is last before server tick 360. The exact final tuple is therefore a per-run
convergence certificate, not a cross-run golden hash.

The post-objective quiet tail is proof orchestration, not a stronger transport
contract. It does not guarantee delivery across an abrupt socket failure,
change `close()` semantics, emulate a graceful application shutdown protocol,
or make the plaintext TCP adapter suitable for a public network.
