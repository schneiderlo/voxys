# WRECKWATER local platform prediction

`WreckwaterCharacterPlatformTimeline` supplies exact 60 Hz skiff frames to
local character prediction.

Its input is a fixed span of skiff states copied from an already accepted,
exact certified snapshot. It never accepts a sampled visual pose.

## Motion model

Each certified skiff keeps its exact:

- skiff ID and generation;
- client-only body handle;
- linear and angular velocity;
- deck half extents and deck height.

Future positions use constant linear velocity at `1 / 60` second per tick.
Future orientations use constant world-space angular velocity at the same
rate. World positions are converted through checked wide coordinates, so a
normal sector crossing is not a discontinuity.

This is only a short prediction model. Skiff collisions, waves, damage
response, and helm changes are extrapolated from the last certified velocity.
They are not authority-exact before the next certified snapshot arrives.

## Bounds and failure policy

Storage is fixed. The default future horizon is 32 ticks, with a hard maximum
of 128 ticks.

The timeline rejects a frame before changing live output when it finds:

- a stale snapshot sequence or evidence tick;
- an exhausted snapshot sequence or tick counter;
- too many skiffs;
- a changed skiff lifetime, body identity, or deck geometry;
- a correction outside the configured linear or angular acceleration bound;
- a requested tick beyond the prediction horizon;
- an invalid or overflowing world position.

Skiffs are sorted by stable identity before storage. Two input spans containing
the same skiffs therefore emit the same frame order.

## Controller integration

The normal product call is:

```cpp
controller.advancePrediction(nextTick);
```

This asks the internal certified timeline for exactly `nextTick`. The older
`advancePrediction(nextTick, exactPlatforms)` overload remains available for
tools or callers that already own exact per-tick platform frames.

These two prediction sources cannot be mixed inside one speculative run.
Switching between them fails closed, purges speculative character state, and
returns the controller to its last certified tick. A certified snapshot that
would rewind a legacy exact-frame run also hard-snaps instead of silently
replacing caller-owned exact frames. The caller may choose either source again
after that purge.

When a newer certified snapshot rewinds the character, the controller:

1. validates the new certified skiff base against the prior prediction;
2. regenerates every future platform frame from that new base;
3. replaces the platform frames in character history;
4. replays the still-buffered character inputs.

It does not replay the platform frames stored before the correction.

A platform lifetime change, impossible correction, or prediction-horizon gap
purges speculative character history and hard-snaps to the newly certified
state. The new certified platform base is retained for later prediction.

The application still needs to call the controller once per fixed simulation
tick. Renderer interpolation remains presentation-only.
