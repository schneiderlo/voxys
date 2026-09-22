# Turning over elevation — 2026-09-16

Owner report: the motorcycle jerks, stops, or loses steering while turning
across changing elevation. This follows the ground-contact changes in
[motorbike-ground-r02](../motorbike-ground-r02/README.md).

## Reproduction and cause

The regression drives eight routes on a studded 24% slope: four initial
headings, each turning left and right after an initial acceleration phase.
Before the fix, four routes lost steering for between 1 and 19 simulation ticks.
One downhill turning route abruptly lost its entire 22 studs/second speed.

- The controller set turn rate to zero whenever the front tire became airborne,
  even briefly over a terrain step. Repeated contact loss switched rotation on/off.
- A swept collision guard could cross a stud crown above both endpoint supports.
  The existing two-segment step routes did not clear that intermediate rise,
  even though the destination was clear. Rejection then zeroed forward speed.

## Changes

`BuilderMotorbike::State` now stores yaw rate. Front-tire contact approaches the
requested turn rate smoothly; brief airborne intervals retain and gently damp
existing angular momentum. New airborne steering input does not directly set
turn rate. Pause, a blocked move, or a grounded stop clears yaw rate, preventing
stationary rotation after braking.

When ordinary sweeps fail but the destination is clear, the controller checks
an up/across/down guard route 0.32 studs above the higher endpoint, within the
existing step allowance. All three segments are swept against terrain and
buildings, including overhead clearance. This is only the conservative collision
guard; the rendered bike still uses the original solved tire contact heights.

## Validation

- All eight turning routes now complete with zero sudden stops or steering
  cut-outs. The tests also bound turn-rate changes and validate both axle
  positions against terrain. They explicitly confirm front-tire unloading
  actually occurs, so the continuity check exercises the reported condition.
- A separate braking test confirms the bike cannot keep rotating in place.
- All 21 `//tests:adventure_world` tests pass, including earlier thin-wall,
  low-roof, water, slope, drop, dismount and frame-rate regressions.
- `//tests:adventure_runtime` passes using the installed 8192 terrain and assets.
- The WASM browser build passes; the local preview uses
  `?experience=build&preview=motorbike-turn-r03`.

The assisted upright-bike limits documented in the previous validation still
apply. These tests establish behavior on the reproduced routes; owner riding
acceptance across the full landscape and publication remain open.
