# WRECKWATER character authority bridge

Status: native-authority integration foundation

Date: 2026-07-30

This adapter joins delayed certified GPU skiff poses to
`WreckwaterCharacterMovementAuthority`.

The native headless server now owns this bridge for all four roster players.

## Fixed contract

The bridge owns:

- one movement authority;
- exactly four roster characters;
- exactly two skiff samples per certified tick;
- 17 input slots per player;
- four transition output slots.

The live input window is at most 16 ticks. The extra physical ring slot makes
tick modulo wrap unable to alias any still-live tick.

No bridge operation allocates. Storage addresses remain stable across
successful input, lifecycle, and tick operations.

## Delayed input

Inputs may target:

```text
last certified tick + 1 ... last certified tick + configured lag
```

One winner is retained for each player and tick. The highest valid
movement-input sequence wins, independent of same-tick packet arrival order.

Movement-input sequences are also checked against the nearest retained earlier
and later inputs. Every eventual movement-core submission therefore stays
monotonic and within the core's configured sequence-advance bound. Zero,
`UINT64_MAX`, duplicates, stale ticks, and inputs beyond the future window are
rejected. Helm and cargo packet sequences do not enter this ordering domain.

Inputs reach the movement core only when their exact certified pose tick
closes. Missing input becomes neutral movement.

Schema-5 ingress expands each fixed movement bundle oldest to newest against a
copy of this bridge. Exact redundant replays, an already superseded same-tick
sample, or an already closed target tick are counted and ignored. Any other
failure discards the copy, so one malformed bundle cannot partially mutate the
live bridge.

## Pose closure

A pose frame must:

- be the next exact tick;
- report no readback overflow;
- contain exactly two platform samples.

The bridge copies the movement core, submits that tick's retained winners to
the copy, and closes the pose on the copy. It commits only if every operation
succeeds.

Pose gaps, malformed platform identity, teleport, impossible rotation,
geometry mutation, and state-range failure therefore leave the live movement
state and retained input unchanged. The same tick can be corrected and
retried.

## Lifecycle fencing

Disconnect and reconnect use exact character, player, and connection
generation identity.

Disconnect purges every buffered input for that player. Reconnect requires the
exact next generation and resets the authority's last-applied movement
sequence to zero; the first valid wire input for that generation is one.
Old-generation packets cannot control the retained character.

The server applies transport lifecycle changes against the current certified
boundary before later pose closure. A transport reconnect does not reuse an
old generation's movement-input sequence.

## Output

The bridge exposes:

- canonical player views and buffered tick ranges;
- the read-only movement authority;
- movement and connection transition records;
- bounded counters and high-water marks;
- fixed-storage addresses;
- an FNV-1a state hash.

The convergence hash covers configuration, the full movement-authority hash,
roster identity, lifecycle state, and every input-ring slot. Telemetry and the
transient transition output are excluded.

Telemetry rejection counters may change after a rejected call. Authoritative
state and its hash do not.

## Native runtime wiring

The authority runtime:

1. creates the bridge only after all four authenticated roster peers exist;
2. maps each peer to one stable character handle;
3. expands each validated 144-byte realtime movement bundle into at most four
   canonical samples in the bounded future-tick ring;
4. closes one bridge tick only from the matching certified GPU pose and both
   skiff samples;
5. mirrors disconnect/reconnect generation changes into the bridge;
6. serializes exactly four character states into each first-slice snapshot.

The replay recorder stores those same canonical snapshot bytes. Snapshot or
replay construction requires all four character records; missing or invalid
state fail-stops the authority.

## Deliberate limits

This slice does not:

- poll GPU buffers or independently prove that a caller's frame is certified;
- roll back character state;
- predict or reconcile browser clients;
- render characters;
- serialize bridge transition records as separate replay events;
- schedule lifecycle changes at a future tick;
- add the missing hull, ladder, rail, terrain, or character collision.

The copy-stage-commit path is fixed-size and allocation-free. It deliberately
copies the small four-character authority each certified tick to make invalid
pose closure atomic.
