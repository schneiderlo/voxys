# WRECKWATER authoritative replay

Status: authority recording, archive export, and isolated WreckCam playback
foundation; not yet integrated into the client runtime

## Contract

WRECKWATER records certified state playback.

It does not claim that the production floating-point GPU simulation is
cross-platform lockstep.

Each recording contains:

- exact canonical certified-snapshot bytes;
- exact authoritative match events;
- session, match, world, epoch, tick-rate, and content identity;
- the authoritative match-configuration hash;
- strict independent event and snapshot sequences;
- a per-record payload hash;
- a chained replay hash;
- a whole-archive corruption hash;
- final match-state and event-stream hashes certified by the last snapshot.

Replay schema 2 stores the match-configuration hash in the archive header.
The recorder, importer, and playback loader use the match core's canonical
event-stream hash functions. Every certified snapshot must match the exact
event prefix recorded before it; a caller-supplied or forged snapshot hash is
not trusted.

The container is deliberately separate from the snapshot payload schema.
Vessel topology, breaches, flooding, fragments, characters, and constraints
can extend the certified state contract without redesigning the archive.

FNV-1a hashes are deterministic corruption and convergence fingerprints.
They are not authentication codes.

## Runtime bounds

`WreckwaterReplayRecorder::initialize()` reserves both record metadata and the
payload arena.

Accepted append operations do not allocate or move either arena. A full arena
returns an explicit capacity error without partially appending a record. The
authority treats any recorder error as a permanent fail-stop.

The production authority reserves:

- 262,144 record headers;
- 64 MiB of replay payload;
- exact event records as soon as the match publishes them;
- the same canonical snapshot bytes used for network transmission.

The recorder accepts finalization only when its last record is a `Finished`
snapshot with exact `physicsEvidenceTick == applicationTick`, at least one
prior match event, and no event tail after that snapshot.

At a Live/Overtime boundary, the authority first closes all physics-derived
damage through logical tick `N`. It then commits logical tick `N+1`, including
the already-certified sink/break events, and submits one final
certification-only physics tick. Contacts from that post-whistle tick are
validated for stream integrity but cannot create an impossible event for the
finished match. Its exact pose certifies the final snapshot.

A terminal server can export the archive with `--replay-output PATH`. An
interrupted or `--max-ticks` partial match is deliberately not presented as a
finalized replay.

Archive export and archive import may allocate. They are outside the fixed
authority tick. Import checks exact archive size, caller limits, record
schemas, payload hashes, rolling hashes, ordering, identities, canonical
snapshot bytes, per-snapshot event-stream hashes, and terminal conditions
before reserving archive storage.

## Playback meaning

Snapshots provide authoritative transforms and logical state. The isolated
playback core interpolates only visually compatible entity topology and
exposes events by the exact prefix certified at each snapshot. Match events
provide cuts, steals, banks, sinks, respawns, scoring, reconnects, and
deterministic camera-interest cues between snapshots.

The isolated WreckCam core selects deterministic subjects and a coarse shot
class. It does not yet compose a production camera.

It is not yet:

- replay transfer to clients;
- replay links or persistence;
- client/runtime playback integration;
- final cinematic camera composition;
- final vessel-topology, flooding, and fragment checkpoint coverage;
- character animation reconstruction beyond the four certified schema-5
  character states;
- cryptographic signing.

Those remain product gates.

The headless executable sets replay schema 2's 64-bit `contentHash` from the
first eight bytes of the generated authority SHA-256 digest, interpreted as a
big-endian integer. It is no longer a manually maintained value. Native TCP
admission still compares the full 32-byte digest; the replay field is a
truncated compatibility/provenance value, not authentication.

Product WreckCam playback should set
`WreckwaterReplayPlaybackConfig::expectedContentHash` to the current generated
64-bit value. A zero expectation deliberately permits a structurally valid
foreign-build archive for archival inspection; it does not prove local content
compatibility.
