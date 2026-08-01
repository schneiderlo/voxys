# WRECKWATER third-person camera rig

Status: tested client presentation component; not wired into `Application`

Date: 2026-07-30

`WreckwaterThirdPersonCameraRig` consumes the local
`WreckwaterThirdPersonCameraTarget` and normalized orbit, zoom, and shoulder
controls.

It emits:

- a canonical sector/local camera position;
- a canonical sector/local look target;
- a look target expressed in the camera sector for f32 view-matrix work;
- finite forward, right, and up vectors;
- a bounded vertical FOV;
- one fixed-storage asynchronous obstruction-probe seam.

The rig owns no window, input backend, renderer, physics world, collision
geometry, or dynamic container.

## Frame contract

`update()` accepts one positive finite render delta.

The delta is capped by `maximumDeltaSeconds`. Orbit and zoom are normalized
rates in `[-1, 1]`. The caller converts raw mouse, stick, or wheel input into
those rates.

The camera uses:

- an analytic critically damped target tracker;
- analytic critically damped orbit, pitch, zoom, shoulder, and FOV state;
- velocity look-ahead with a hard distance limit;
- an immediate inward obstruction clamp;
- an analytic exponential release after a certified miss.

The analytic steps remain stable during low render rates. Tests compare the
same moving target and held controls at 30, 60, and 144 Hz.

Pitch, boom distance, target speed, velocity look-ahead, FOV, control rates,
probe radius, probe endpoint-drift margin, and frame delta all have validated
limits. The probe radius plus drift margin cannot exceed four metres.
The maximum yaw step is strictly less than half a turn, which keeps wrapped
angle tracking unambiguous. Spring velocity is cleared when a clamped pitch,
zoom, shoulder, or FOV state pushes farther out of bounds, so a saturated
control cannot rebound on a later frame.
Non-finite or out-of-range input is rejected without changing the prior pose.

## World sectors and discontinuities

Camera and look-target positions always use canonical
`physics::WorldPosition` coordinates:

```text
local axis range = [-128, 128)
sector size      = 256 metres
```

Tracking displacement is computed from an exact signed 64-bit sector delta
plus local coordinates. Camera offsets are canonicalized with checked sector
arithmetic. An endpoint beyond the signed 32-bit sector range is rejected as
`PositionOverflow`; it is never silently clamped.

The rig snaps when:

- the player, character handle, or connection generation changes;
- the target reports `teleported`;
- the target reports `discontinuity`;
- an unflagged target jump exceeds `teleportDistance`.

A snap clears spring velocity, invalidates the old obstruction certificate
and in-flight probe, and clamps the camera to
`minimumObstructedDistance`. The next probe starts at the new canonical target
and ends near the new camera. It never sweeps from the previous location or
the world origin.

## Asynchronous obstruction probe

The camera permits exactly one probe in flight.

The caller performs this handshake:

1. Call `takeObstructionProbeRequest()`.
2. Submit `request.physicsQuery` to the rigid-body query backend and query
   every other rendered obstruction domain, including the static heightfield.
3. Retain the complete `request.identity` beside that submission.
4. Poll `PhysicsWorld::pollQueryResults()`.
5. Select the closest valid hit across all domains.
6. Set `completeScene=true` only when every required domain completed.
7. Return the closest distance and retained identity through
   `resolveObstructionProbe()`.

If submission fails, call `cancelObstructionProbe()` with the same identity.

The identity contains:

- a monotonically increasing 64-bit sequence;
- player ID;
- character handle;
- connection generation.

`PhysicsQueryRequest::requestId` contains only the low 32 bits of the sequence
because that is the physics API field width. It is not sufficient by itself.
The adapter must retain and return the full identity. This prevents a delayed
result, a reconnect result, or a 32-bit request-ID wrap from becoming current.

The 64-bit sequence never wraps. `reset()` clears camera tracking but retains
the sequence. Exhaustion fails closed with `ProbeSequenceExhausted`.

### Spatial certificate

The request sphere radius is:

```text
obstructionProbeRadius + maximumProbeEndpointDrift
```

An accepted hit or miss retains the submitted world-space start and end.
The current target and unobstructed-camera endpoints must each remain within
`maximumProbeEndpointDrift` of those submitted endpoints.

For static geometry, the inflated submitted sweep contains the current
camera-radius sweep while that bound holds. This gives the delayed result a
bounded spatial meaning instead of applying an old miss to an unrelated boom.

If either endpoint leaves the bound:

- the old certificate is invalidated;
- the rendered camera clamps immediately to
  `minimumObstructedDistance`;
- an in-flight request is marked expired;
- its exact result is consumed as `ProbeGeometryExpired`;
- the next request gets a new sequence and current endpoints.

Returning inside the old corridor does not revive an expired request.

An accepted hit is a hard bound. The current pose is rebuilt immediately and
never eases through the certified obstruction. A miss permits only the slower
configured release.

A backend overflow is consumed before optional hit fields are inspected.
Even an overflow carrying NaN or contradictory hit fields immediately clamps
the current pose to the configured minimum and returns
`ProbeResultOverflow`.

A malformed non-overflow exact result is rejected, fail-closes the current
pose, and remains outstanding so the caller can cancel it explicitly. Stale
sequence and identity-mismatched results do not alter a newer request.

`completeScene` is mandatory for both hits and misses. The current
`PhysicsWorld` GPU sphere cast checks rigid bodies; it does not bind or sample
the terrain heightfield. A rigid-body miss therefore cannot be returned as a
camera miss by itself. Likewise, a rigid-body hit is not complete if terrain
could contain a closer hit. The adapter must merge the closest rigid-body,
heightfield, and any other rendered-domain result before certifying the scene.

## Deliberate limits

- Collision geometry is external. The rig only describes a sphere cast and
  rejects results that are not marked complete-scene.
- Query submission, polling, closest-hit selection, body exclusion, and
  collision-layer policy are external and asynchronous.
- The spatial certificate covers endpoint motion against the geometry queried
  by the backend. It does not certify a dynamic occluder that moves after the
  backend samples it. The adapter must use a current query tick or add its own
  dynamic-motion margin.
- The rig does not exclude the followed character by body handle because the
  current physics query filter has no per-body exclusion field.
- The result applies a scalar boom limit. It does not reconstruct or slide
  the camera along a wall.
- A request asks for one hit. The current GPU backend reports overflow when
  more hits exist; the camera deliberately chooses the minimum boom instead
  of trusting a partial result. Dense geometry can therefore over-constrain
  the camera until a complete query policy is supplied.
- There is no internal wall-clock timeout. A submitted request remains
  outstanding until it resolves, expires by endpoint motion, or the caller
  cancels it.
- Sustained endpoint motion faster than the configured drift corridor keeps
  the camera at the minimum distance. A production adapter should issue a
  current probe every render frame and measure this telemetry.
- `initialize()` is one-shot for one object. Create a new rig to change
  configuration.
- Fixed-storage camera methods perform no per-frame allocation and are
  `noexcept`. Existing `PhysicsWorld` query batches use dynamic storage
  outside this component.

## Evidence

`test_wreckwater_third_person_camera.cpp` covers:

- render-rate equivalence at 30, 60, and 144 Hz;
- hitch delta capping;
- occluder entry and slower release;
- immediate fail-close on malformed overflow;
- rejection of rigid-body-only/partial-scene misses;
- tolerated endpoint drift covered by an inflated cast;
- material stale miss and hit expiry before re-probe;
- stale and mismatched async results;
- exact world-sector crossing;
- teleport snap and new-origin probe generation;
- configuration, input, target, finite, and overflow rejection;
- shoulder rising-edge behavior and camera limits;
- 64-bit probe-sequence exhaustion.
