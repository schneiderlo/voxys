# WRECKWATER certified replay playback

Status: build-integrated playback and camera-interest core; not integrated
into the client runtime.

## What this proves

`WreckwaterReplayPlayback` loads a finalized, internally consistent certified
replay.

Loading may allocate. After loading, `seek`, `stepSubticks`, and
`wreckCamSelection` do not grow or move prepared storage.

Playback time is the certified physics-evidence timeline, not the application
timeline. Time uses a whole tick plus an unsigned Q16 sub-tick. Playback does
not accumulate a floating-point clock.

At an exact snapshot boundary:

- every authoritative field is the exact decoded snapshot value;
- every entity pose is exact;
- `exactSnapshot()` exposes the complete certified snapshot, including its
  four character states.

Between snapshots:

- the older certificate owns all authoritative state and topology;
- only visual pose may interpolate;
- network identity, shape, material, cargo, skiff, attachment, phase, score,
  outcome, and hashes never blend;
- a generation or topology change holds the older pose and snaps at the next
  certificate;
- no entity, attachment, character, or event appears early.

Position interpolation is sector-relative cubic Hermite at 60 Hz. It never
converts the full far-world position to a `float`. Tangents are
monotone-limited. A non-canonical cubic result falls back to bounded linear
interpolation. Orientation uses shortest-path quaternion interpolation.

Playback clamps before the first certificate and after the terminal
certificate. It never extrapolates beyond certified data.

## Certification clocks

Application and evidence clocks are intentionally different.

- A system event has `sourcePhysicsTick == 0`.
- A player-command or world event has a nonzero source tick.
- That source tick must be no newer than the physics evidence in the first
  snapshot that certifies the event.
- `applicationTick > physicsEvidenceTick` is valid and expected under lag.

An event with evidence from the future is rejected even when its application
tick is otherwise valid. A physical mutation before the first snapshot is
also rejected because no certified before-state exists.

An event becomes visible only at the evidence-time boundary of its certifying
snapshot.

## Exact logical transitions

The loader replays a small logical shadow between adjacent certificates.
Every production mutation must have the exact certified before- and
after-state.

- Cargo revision must move from `observed` to `observed + 1`.
- Tow requires free cargo and an active same-crew target skiff.
- Cut and bank require the exact towing skiff and source attachment.
- Steal requires the exact old tow plus an active opposing target skiff.
- Bank requires the matching `CargoBanked` and `ScoreChanged` source pair.
- Attachment break and cargo loss require their exact prior tow evidence when
  the cargo was towed.
- Skiff sink changes only the named active skiff. Its optional cargo-loss
  event must precede the sink and use the same world source.
- Respawn requires a sunk skiff and exactly the next skiff generation.

After applying every event in an interval, entity count, logical identity,
crew, cargo, skiff, and attachment state must exactly equal the next
certificate. An unexplained change or a claimed change that did not occur is
rejected. Non-mutating rejection, score, and fault events may reference only
subjects proven by their certifying snapshot or its immediate predecessor.

## Roster, connection, and character provenance

Playback accepts only the production lifecycle:

1. Four unique players register into the four unique 2v2 seats.
2. Each player receives a unique initial connection ID at generation 1.
3. All four are connected before `MatchStarted`.
4. Reconnect is allowed only after disconnect and advances generation by
   exactly one.

Connection IDs are never reusable, including IDs from disconnected
generations. Player commands cannot bootstrap missing roster or connection
state. A normal command must use the player's current connected identity.
Only a canonical stale-connection rejection may name a remembered older
generation.

Every schema-5 snapshot contains exactly four character states. Each character
must:

- name one of the four registered players exactly once;
- retain one stable, globally unique character handle;
- carry the player's current connection generation;
- set its Connected flag exactly when that player is connected.

Character handles cannot move between players or change later in the replay.

## Source bundles and canonical match state

A player `(playerId, sourceSequence)` or world
`(sourceStreamId, sourceSequence)` identifies one exclusive source bundle.

The bundle is exactly one of:

- one rejection;
- one authority fault;
- one successful non-bank result;
- `CargoBanked` followed by `ScoreChanged`;
- for a skiff-sink world source, optional `CargoLost` followed by
  `SkiffSunk`.

Success, rejection, and fault outcomes cannot be mixed. Duplicate event types,
reordered sink bundles, incomplete bank bundles, or mismatched source fields
are rejected.

System events have no fabricated command/world clock, sequence, stream,
rejection, or physical subject fields. Phase changes follow only:

- Warmup to Live;
- Live to Overtime or Finished;
- Overtime to Finished.

All events at one application tick carry the same committed phase, outcome,
and scores. A score increase requires a matching `ScoreChanged` for every
crew that increased. `MatchFinished` directly follows the Finished
`PhaseChanged`.

At every certificate, phase, scores, outcome, winner, and match-state hash
must exactly equal the latest certified event state.

## Input rejection and bounds

Encoded archives first pass through `WreckwaterReplayCodec`. Playback then
independently verifies:

- record, payload, snapshot, event, and entity capacities;
- contiguous payload offsets and record sequences;
- non-regressing application ticks;
- event-before-snapshot ordering at equal application ticks;
- strictly increasing snapshot evidence ticks;
- complete event and snapshot decoding;
- exact event/source/type contracts;
- replay identity on every event and snapshot;
- payload hashes, chained record hashes, and final replay hash;
- the event-stream hash at every snapshot;
- a final Finished snapshot with exact terminal hashes.

One aggregate lifetime budget covers every tracked player, seat, connection
ID, character handle, command source, world source, network entity, logical
entity, and attachment. It cannot be multiplied by filling several
registries independently. Exhaustion reports `LifetimeCapacity`.

Identity reuse, generation regression, lifecycle disagreement, and certified
logical disagreement report a generation alias. Malformed event shapes and
source bundles report an invalid event. A malformed roster tail or mismatch
between snapshot summary state and the event stream reports an invalid
snapshot.

FNV hashes detect corruption and deterministic divergence. They do not
authenticate a replay.

## Local content compatibility

Every valid replay archive carries a nonzero schema-2 `contentHash`. The live
authority fills it with the first eight bytes of the generated authority
SHA-256 digest, interpreted as a big-endian integer.

`WreckwaterReplayPlaybackConfig::expectedContentHash` controls the optional
local-build gate:

- zero accepts any otherwise valid archive and treats its hash as provenance;
- nonzero requires exact equality and reports `ContentMismatch` on failure.

For an already decoded archive, the equality check runs before playback
prepares record, payload, snapshot, event, or lifetime storage. The encoded-byte
entry point must first run `WreckwaterReplayCodec` under its read limits, so its
temporary decoded archive allocation occurs before this playback-level check.

A product WreckCam should pass the current generated
`kWreckwaterAuthorityReplayContentHash`. Zero is appropriate only for an
explicit cross-build inspection or archival tool. This 64-bit prefix is not a
signature and does not replace the full 32-byte native-session compatibility
check.

## Event stepping

Each snapshot stores the exact event-prefix count it certifies.

A forward step returns a half-open range to consume in ascending order. A
reverse step returns the same range to undo in descending order. The range
indexes the predecoded event span; stepping does not build another event
vector.

Seeking changes the event cursor directly. It does not claim that every
crossed event was played.

## WreckCam interest

`wreckCamSelection()` is a deterministic subject selector.

It uses integer scores, a bounded look-back window, exact cargo and skiff
generations, stable tie breaks, and only the visible certified event prefix.
Tow, steal, cut, bank, score, attachment break, cargo loss, sink, respawn,
finish, and authority fault add decaying interest.

The result provides primary and secondary subjects plus a coarse shot:
establishing, follow, objective, duel, impact, or finish.

This is input for a later camera rig, not final cinematic composition.

## Deliberate limits

This core does not provide:

- GPU resimulation or cross-platform floating-point lockstep;
- vessel breach, flood, or fragment state not present in the schema;
- character animation reconstruction between certificates;
- camera collision, lenses, occlusion, or authored shot grammar;
- animation, audio, particles, slow motion, or edit cuts;
- replay networking, persistence, links, signing, or access control;
- client/runtime integration.
