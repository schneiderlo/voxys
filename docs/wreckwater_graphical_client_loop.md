# WRECKWATER graphical client loop

Status: tested orchestration layer; not yet wired into `Application`

Date: 2026-07-30

`WreckwaterGraphicalClientLoop` is the fixed-step product seam between:

- `WreckwaterClientRuntime`;
- `WreckwaterCharacterController`;
- `WreckwaterCharacterPresentation`.

The loop owns the controller and presentation state. It receives a reference
to a separately owned network runtime. Production can inject a native or
browser transport runtime. Tests inject the same runtime with a fake
transport.

It does not own a window, renderer, camera rig, input backend, or network
thread.

## One render frame

`frame()` performs this bounded order:

1. Pump the injected network runtime exactly once.
2. If the pump accepted snapshots, request only `latestSample()`.
3. Pass that exact whole physics-evidence tick to the local controller once.
4. Fence any connection, character-availability, handle, or generation
   change before scheduling input.
5. Add integer wall time to the 60 Hz phase accumulator.
6. Run at most `maximumCatchUpTicks` exact simulation ticks.
7. For each tick, send one quantized movement sample for
   `simulationTick + inputLeadTicks`.
8. Record that input in prediction only after the send succeeds.
9. Advance prediction with `advancePrediction(simulationTick)`. This uses the
   controller's certified platform timeline, never a rendered/interpolated
   skiff pose.
10. Advance local render correction, sample remote characters at the caller's
    render tick, and update presentation in the caller's camera sector.

The controller therefore sees only the newest accepted exact snapshot.
Presentation can still interpolate or freeze remote visual poses at the
requested render tick.

## Fixed clock and hitch limit

Wall time is supplied as integer nanoseconds.

The accumulator uses a rational phase:

```text
phase += elapsedNanoseconds * 60
one simulation tick = 1,000,000,000 phase units
```

There is no floating-point tick decision. Equal elapsed time produces the
same exact simulation ticks at 30, 60, or 144 Hz render cadence.

`maximumCatchUpTicks` is configurable from 1 through 16. If accumulated wall
time exceeds that cap, the loop keeps the sub-tick remainder and drops excess
whole wall-clock ticks. It never skips a character tick number: dropped wall
time makes local simulation lag instead of asking the controller to jump over
ticks.

A lineage change clears the accumulator and does not simulate from that
frame's elapsed time. Scheduling resumes on the next render frame.

## Input contract

Movement starts as two finite floats. The loop:

1. clamps the vector to the unit circle;
2. rounds each component to signed Q15 in `[-32767, 32767]`;
3. creates at most one new logical sample for each exact simulation tick.

The input lead is explicit and configurable from 1 through 64 ticks. It must
be smaller than the controller input-history capacity and must also fit the
authority's configured future-tick window.

**Current product limit:** 64 is only the client API's hard bound. The
checked-in native authority executable sets `maximumRequestedTickLead` to 16,
and its certified character bridge retains a 16-tick exact-input window.
Therefore the checked-in product must use `inputLeadTicks <= 16`. A value from
17 through 64 is valid only for a deployment whose authority configuration
and proof were raised with it. This limit is not negotiated by the current
wire protocol.

For simulation tick `S`, the logical input targets:

```text
S + inputLeadTicks
```

If transport send fails, that exact target tick, Q15 axes, identity, and
one-shot flags remain pending. The controller does not record it and
prediction does not advance. A later render frame retries the same logical
sample. New wall time remains bounded by the catch-up cap.

A hard send or controller failure blocks scheduling until a newer exact
certified sample repairs the lineage.

If a pump rejects an inbound frame, that rejection cannot spend a jump or
board edge. Ordinary movement prediction may continue from the last certified
state, but newly created samples omit one-shots and leave those edges latched
for the next clean pump. A pending retry that already contains a one-shot
waits unchanged. A rejected frame alone does not change connection or
character lineage.

## Jump and board

The frame input supplies button levels.

Rising edges latch independently for jump and board. A latch is assigned to
one pending logical sample. It is cleared only when that sample has been
created; the pending sample itself survives transport retries.

A new rising edge during a retry remains latched for the next logical sample.
Multiple additional edges before that next sample coalesce and are counted.

This gives each successful edge one inner movement sequence. Packet-level
temporal redundancy may transmit its flags again, but authority dedupe prevents
double-fire.

Disconnect, connection replacement, character disappearance, handle change,
or connection-generation change purges:

- pending sends;
- queued simulation time;
- unsent one-shot latches;
- controller prediction/history;
- visible presentation state.

Current button levels become the new edge baseline. A held button cannot fire
an old-generation action after reconnect; it must be released and pressed
again.

## Fixed storage and telemetry

The orchestration scheduler contains only scalar state and one pending sample.
The controller, certified platform timeline, and presentation use fixed
arrays. The scheduler performs no per-frame allocation.

This is not a zero-allocation claim for the complete client stack. Existing
packet codecs, transport frames, and snapshot-buffer construction use bounded
dynamic storage.

All cumulative loop counters saturate. Telemetry reports:

- pump and rejected-frame counts;
- exact-snapshot consumption;
- connection/generation/availability fences;
- queued, executed, and dropped wall-clock ticks;
- quantized samples, send attempts, retries, and successes;
- controller recording and prediction failures;
- latched, coalesced, sent, and purged one-shots;
- render-sample and presentation results;
- per-frame and per-sample high-water marks.

`WreckwaterGraphicalClientFrameResult` also returns the exact runtime,
send, controller, replication-sample, and presentation statuses for the most
recent frame.

## Deliberate limits

- No `Application` or renderer wiring is included.
- The loop outputs presentation avatars, primitive proxies, and the
  presentation camera target; it does not implement the final camera rig.
- Input lead is configuration, not clock synchronization.
- Dropped wall time is not replayed later.
- Only one pending logical movement sample is retained. A failed send stalls
  simulation until retry or lifecycle purge.
- Button edges are boolean latches, not an unbounded event queue. Repeated
  presses can coalesce while the next logical sample is blocked.
- Platform prediction is bounded by the controller's configured certified
  constant-velocity/angular-velocity horizon.
- Remote rendering still obeys snapshot-buffer interpolation, extrapolation,
  and stale-freeze limits.

## Evidence

`test_wreckwater_graphical_client_loop.cpp` proves:

- identical logical samples and final prediction at 30, 60, and 144 Hz;
- one pump per render frame;
- newest-only exact controller ingestion when one pump accepts two snapshots;
- remote presentation at a fractional render tick;
- four-tick catch-up under a one-second hitch, with 56 wall ticks dropped;
- rejected inbound frames preserve lineage, allow movement-only prediction,
  and defer new or retrying one-shots until a clean pump;
- exact retry after send failure and record-only-after-success;
- jump/board one-shot retention without held-button double-fire;
- pending-input and one-shot purge across disconnect/reconnect generation;
- camera-relative presentation through a world-sector crossing.
