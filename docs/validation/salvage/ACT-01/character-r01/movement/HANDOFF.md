# Cove movement and collision handoff

Implemented in `src/game/expedition/cove_player.hpp`, `.cpp`, and `tests/test_cove_player.cpp`. Application, save codec, restore forwarding, build lists and publication belong to the root agent.

## Player contract

- Upright capsule: radius 0.3 m, height 1.7 m, 60 Hz fixed steps, at most 0.25 s catch-up. The App supplies elapsed completed coherent observation ticks. Zero elapsed time retains queued jump/interaction edges.
- Walking and helm feet are authored build-local when supported by a boat section. Airborne feet and velocity are independent Cove-scene coordinates. Departure adds origin velocity plus angular velocity crossed with the world offset exactly once. Every admitted boat section and fixed obstacle participates in collision.
- Walking speed 3.6 m/s; swimming target speed 2 m/s; jump adds 6 m/s world-up; gravity 18 m/s². Air steering retains inherited speed, neutral input retains momentum. Velocity components are bounded to ±150 m/s.
- Deck support uses actual oriented collision proxies, a 45-degree walkable limit, 0.36 m stepping and an upright capsule. Landing uses relative platform approach velocity. Submerged support releases even from the helm. Nearby slow boats can be boarded from water through the existing interaction action; fast or submerged targets cannot. Water remains a local datum of zero, with swimming feet at -0.8 m.
- `worldVelocity()`, `facingYaw()` and `supportNormal()` feed animation. Heading is world yaw around canonical -Z forward, normalized to [-pi, pi], and follows a supporting root turn.

## Geometry API

- `setBoatRootMotion(key, sceneFromBoat, originVelocity, angularVelocity, observedTick)` stages all sections and commits only a complete same-tick packet. The origin velocity must refer to `sceneFromBoat[3]`, the transformed authored build origin. Root code converts from the physical root origin.
- Supported player and obstacle poses interpolate the previous complete packet to the new packet while executing its elapsed character steps. Queries and returned transforms expose the accepted final packet. The App renders that same completed packet, so the camera needs no guessed motion extrapolation.
- `setStaticObstacles` keeps the existing 11 fixed harbor AABBs. `setSceneObstacles` adds up to four rigid oriented scene proxies in stable order, atomically joined to `collisionTick()`. Publish an explicit empty packet when none exist. A nonzero root tick without a matching scene packet fails closed.
- `GroundSupport(x,z,radius)` supplies exact vertical capsule-foot support, optionally as the last `initialize` argument. The old scalar Ground fallback is for legacy flat fixtures. ACT uses the analytic LEGO support callback.
- `TerrainSweep(start,end,radius)` certifies continuous sphere movement over the downward-solid heightfield. `sweepSphere` includes all box proxies and rejects missing terrain, malformed results, stale geometry or exhausted bounded casts. Distance is sphere-center travel; zero-distance overlap is retained even when terrain also reports a tangent at zero.

## State and limits

State version 1 stores full world velocity and heading. Airborne states cannot retain a boat root. Airborne/swimming verticalSpeed equals worldVelocity.y; walking/helm verticalSpeed is zero. Version 0 attached jumps migrate using the restored support transform and point velocity. Invalid restore preserves the previous player and pending input. Helm validation allows a bounded 0.5 m local anchor tolerance for version 1 to accommodate upright contact on tilted support; legacy anchors remain exact.

This is a local CPU kinematic character against admitted GPU-body observations. It does not apply player impulses to boats and does not provide network character authority. Camera terrain traversal and App packet publication are separate reviewed integration work.

## Checks so far

- `checks/movement-r01.log`: environment-only Nix sandbox socket refusal; no compilation/tests.
- `checks/movement-r02.log` and XML: strict compile passed; 21 of 22 selected cases passed. The old detached-pontoon test attempted standing below an overlapping intact deck. The fixture now moves the cut pontoon clear first, preserving its ownership check and respecting actual standing clearance.
- `checks/movement-r03.log`, `movement-r03-cases.log`, XML: all 23 selected cases passed, no skips, 8.536 s case time. Includes independent takeoff/migration, atomic root packets, rolling deck and sunken helm, scenery/thin wall, swim boarding, exact terrain support, and camera overlap/near-parallel/rotated cargo tests.
- `checks/final-cpu-r01.log`, three case logs/XML and summary: strict build passed. Complete cove_player ran 81 cases: 79 passed, two compatibility fixtures failed. All six character and all six animation cases passed. Despite the filename and my initial classification, the complete player target inherits VOXY_NATIVE transitively and ran eight existing headless GPU cases on AMD Radeon 890M Graphics (RADV STRIX1), Vulkan; all eight passed. The standalone target was not CPU-only. XDG_RUNTIME_DIR environment messages remain in the original log. No application window or screenshot was used.
- `checks/compatibility-r02.log`: the two named compatibility cases passed after explicit test-state corrections. Harbor collision now starts with a real 3.6 m/s incoming airborne velocity; it no longer mistakes finite air steering from rest for failure to reach a post. Legacy fragment restore now checks detached feet, zero support key and the exact inherited origin + angular + old vertical velocity instead of expecting the obsolete attached airborne state. No movement behavior was weakened.
- `checks/restore-character-r03-*`: three CPU movement/restore cases and all seven character cases pass, zero skips. These include independent finite air acceleration, restored legacy fragment momentum, and nonactivating rejection of a capsule inside the oriented cargo before or after harbor installation. Only those named cases ran; no GPU cases were repeated.
- `checks/final-source-r03.json` records the final source hashes. The root owns actual native/browser acceptance, the complete mandatory suite and publication. Those results are not claimed here.


## Character and crossfade follow-up

`CoveCharacter::restoreMode(mode, verticalSpeed)` selects a semantic paused-load pose with time zero and blend one. It clears landing history and refuses invalid input without changing the old pose. Zero elapsed updates now return before any state mutation, preventing a false landing queued during pause. Seven focused cases cover frozen time, movement-based gait, jump/fall/land, swimming/helm/tool priority, reset, invalid observations and paused restore.

`RigidAnimationBlend` now has an optional outgoing loop override. Omission retains the old shared loop policy. CoveCharacter supplies the outgoing clip's own policy so a clamped fall/land does not wrap when crossfading into looping idle/walk. The new sampler case proves both override directions and legacy default behavior; all six sampler cases passed with the corrected installed robot assets.

## V6 read-only validation helpers

The three native and two browser helpers are syntax-checked, with no archive writes or runtime runs by this agent. `validate_native_cove_saves.archive_payload` retains the frozen 419-byte v1 prefix, 22-byte harbor extension, recovery records, and 88-byte roots. V5/v6 add the bounded extra-cargo count/169-byte entries; the live one-cargo drivers require zero extras. V6 then adds exactly 47 bytes: u32 profile, three f64 world velocity components, f64 facing yaw, f64 camera distance, and three strict u8 booleans. Root IDs, SVCE/SVSG hashes, recovery blueprint hashes, nested checkpoint hashes and bounded exact logical slices remain checked.

`archive(slot)` exposes `character` with profile, worldVelocity, facingYaw, cameraDistance, chaseCamera, reducedMotion and loadView. Builder/mechanism readers share the versioned outer offset while retaining their exact accepted part/connection byte comparisons. Browser capture additionally compares each v6 character/camera field with the live read-only observation. These parser changes make no two-job acceptance claim. Existing v4 parsing remains supported; new live save journeys expect v6.
