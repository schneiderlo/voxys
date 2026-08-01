# WRECKWATER character presentation adapter

`WreckwaterCharacterPresentation` converts character simulation and
replication output into one fixed four-slot render roster.

The roster is initialized explicitly as:

- Crew One, slot 0
- Crew One, slot 1
- Crew Two, slot 0
- Crew Two, slot 1

This mapping is configuration, not an inference from player IDs. The current
certified character wire record does not contain crew or seat membership.

## Frame contract

The caller supplies:

1. the local `WreckwaterCharacterController`;
2. one already accepted `WreckwaterClientSample`;
3. the current camera sector.

The configured local player's sampled visual pose is used only as certified
lifetime evidence. Its emitted transform always comes from
`WreckwaterCharacterController::renderPose()`. Every other player uses only
`WreckwaterSampledCharacter::visualPose`.

Output positions include both canonical `physics::WorldPosition` feet and an
f32 position relative to the camera-sector origin. A camera-sector change
therefore rebases render coordinates without being classified as a world
teleport.

Low horizontal speed retains the last facing direction for the same visible
handle and connection generation. Disconnect, reconnect, handle replacement,
or visibility loss purges that retained direction.

## Current renderer seam

`primitiveProxyBatch()` returns fixed capsule proxies accepted by
`PrimitivePath::setInstances()`. They are useful for integration and debugging
only. They provide no authored mesh, crew material, skinning, animation, or
art-complete avatar.

`thirdPersonCameraTarget()` exposes the local upper-body target in canonical
world and camera-sector-relative coordinates, plus velocity, facing, mode,
and discontinuity flags. Application code still owns the actual orbit,
spring arm, obstruction handling, and camera smoothing.

The adapter does not add terrain collision. Character collision remains the
responsibility of the shared movement/simulation layer.

Local prediction now gets exact 60 Hz platform frames from
`WreckwaterCharacterPlatformTimeline`. The timeline starts from certified
skiff states and uses bounded short-horizon extrapolation. Remote presentation
interpolation is never reused as prediction collision input.
