# Cove camera module handoff

Owned source: `src/game/expedition/cove_camera.hpp`, `src/game/expedition/cove_camera.cpp`, `tests/test_cove_camera.cpp`. Root owns Application/build/settings/render integration.

## Frame and API contract

- All positions are double-precision Cove scene metres. Root translates to the terrain frame for terrain casts, and to camera-relative sectors only for rendering.
- Authored forward is -Z; yaw rotates about +Y: `(-sin(yaw), 0, -cos(yaw))`. Positive elevation raises the eye. Orbit input contains radians already integrated from actual events; it is not a rate.
- `settings(Settings)` selects Orbit/Chase, reduced motion, and optional load framing (off by default). `restoreOrbit(yaw,elevation,distance)` validates saved orientation and distance. Distance range is 1.5–12 m; elevation range is -0.45–1.20 radians.
- `update(Target,Input,Projection,Sweep,seconds)` borrows a synchronous nonallocating callback. Target has anchor, actor facing, exact geometry tick, optional load bounds, explicit teleport discontinuity, and chase-active state. Projection uses the actual vertical FOV, aspect and near plane.
- The root integration decision is **coherent delayed presentation**: rendered boat/cargo/robot and player/camera queries use the same completed observed pose packet. No guessed acceleration or stale GPU-body shortcut certifies a camera ray.
- Every call casts a sphere enclosing all near-plane corners plus 2 cm presentation relief. Every accepted eye lies on that newly certified segment; no interpolation through old world eyes. Obstructions pull in immediately, clear space releases exponentially. There is no artificial minimum obstructed distance.
- `pose.valid == false` on unknown/incomplete geometry or an overlapped anchor. The host must retain/skip the scene frame, or independently certify another anchor. It must not silently render the prior eye or unverified first-person fallback. A complete contact exactly at distance zero is valid and requests avatar hiding.
- Paused/menu/focus frames pass inactive input (zero dt is supported for collision recheck). This preserves orbit, ignores queued inputs, cancels unfinished recenter and rearms the Chase quiet period. Explicit teleport discards the old boom distance without sweeping across the old location.
- Chase begins only when active/moving, after 1.5 seconds without manual orbit. Its rate is bounded to 1.5 rad/s and settles exponentially. Reduced motion disables passive Chase; explicit recenter still works. No FOV boost, roll, bob or camera lookahead is introduced.
- Optional load framing solves all eight actual bounds corners inside 85% of the view. Obstruction and the 12 m cap take priority; `loadFramed` reports when fitting is impossible. Bounds are framing geometry, not a line-of-sight guarantee through other cargo.

## Certified terrain helper

`sweepCoveTerrainSphere(Surface,start,end,radius,lego,maxCells)` uses the actual quantized column heights and circular stud cylinders. Smooth fallback conservatively encloses each bilinear cell by its maximum corner height. Global -heightScale floor preserves the existing outside-surface Ground behavior; finite columns retain their boundary walls.

The sphere-expanded segment footprint is bounded before iteration (hard limit 4096 cells; caller may lower it). Each convex primitive uses closest-point separating-plane advancement, at most 64 iterations. Any work exhaustion/malformed input returns incomplete, never a miss. Contact tolerance is 1e-8 m; camera subtracts 2 mm from positive contact travel. Exact hit0 is retained. No arrays, GPU resources, collision bodies or per-call allocation are added.

## Evidence and limits

`checks/camera-r05.log` and `checks/cove_camera-final-cases.log`: focused Bazel target passed all 9 native CPU cases after the X11-safe `Result` enum and immediate restored-intent observation corrections. They cover viewport near-plane corners, fresh moving obstacle pull-in/release, hit0/unknown/overlap, Orbit/Chase/menu/reduced-motion handoff, event angles/teleport/save restore, actual load corners/limits, circular studs/cliffs, finite boundary/floor/contact and smooth/work-budget refusal. Prior r01/r02 were compile-only warnings fixed locally; r03 passed before the final recenter cancellation regression; r04 passed before the subsequent width-product overflow guard, enum compatibility change, and restore-intent assertions; r05 passes final camera source pinned by source-hashes.json.

This agent performed the two coordinated GPU cases reported below and reviewed the existing playable image without taking another capture. Root supplies integrated scene-coherence review and actual native/browser journeys. The focused work does not claim a full suite, publication or ACT parent gate.

## Native and browser camera controls

Native Facts adds `cameraAvailable`, `chaseCamera`, `frameLoad`, `reducedMotion`, and `cameraDistance`. Outside Workshop, F2/Y opens `player-camera`; actions 320–325 change mode, recenter, load framing, reduced motion, closer and farther. Existing Workshop Camera remains its own page. Availability is independent of Workshop pending; camera settings may be used while paused. Revocation and Workshop transition close and consume the frame. Native menu checks: 10/10 passed in `checks/camera-menu-r02.log`; initial r01 had one test trying to open the existing Workshop submenu while intentionally pending, corrected to set the actual enabled precondition.

Browser `#salvage-camera` is visible only for Cove player mode outside Workshop. The JSON contract is `characterCamera:{available,mode:'chase'|'orbit',distance,frameLoad,reducedMotion}`. The quoted `window['voxyCoveCameraMenu']()` re-reads state and returns true only when the existing controller owner's `openSection(details)` takes ownership immediately. Root sends F2/Y to this opener when closed and the existing `{menu:true}` bridge when active. Controller navigation stays within the Camera section. Back/Menu closes it and consumes the edge. Changes use exactly one original action 320–325; each click revalidates permissions. Distance buttons honor 1.5/12 m limits. Paused options remain enabled by authoritative availability. Missing data, explicit revoked admission, transitions, fail/cleanup and stale calls cannot activate controls. Cleanup restores the previous opener if still owned. A mouse-opened drawer permits old held-key releases to reach the engine; owned controller menus have already reset those keys. Keyboard F2/Escape inside the drawer closes it locally.

`checks/web-controller-r02.log`: 29 source-level DOM/controller cases passed (21 existing plus 8 section ownership/transition cases). `checks/web-preview-r02.log`: existing preview assertions plus one camera action/permission/distance/cleanup group passed. These are DOM-model checks, not browser layout or actual-host proof. The first controller run found only a new test accessing a deliberately restored-away global after cleanup; the assertion now uses the retained owner API.

## Robot acceptance tests authored

`tests/test_robot_asset.cpp`: three focused CPU cases passed in `checks/camera-menu-loader-r01.log`, covering actual strict provider load and sample, malformed/oversized/traversing/duplicate manifests, hash mismatch, overreturned payload and rehashed corrupt VMESH. This test run preceded the final rounded-head r04 art package; the final animation owner's separate installed-data run covered all three corrected LODs and is reported by that owner.

`tests/test_salvage_asset_fixture.cpp`: authored actual robot owner byte admission, numeric idle/walk opaque color/depth, hidden exact-baseline, submitted/discard counters and same-ticket retirement proof; extended the real nine-presentation scene +64-large-brick capacity case with robot and dock ownership, plus publication clearing both optional renderer counters. Both cases subsequently compiled and passed in the coordinated renderer lane, as recorded below. Final robot LOD0 requested GPU bytes are 387,600; the actual measured full scene owner is 10,119,560 bytes, within the unchanged 16 MiB owner cap.

The restored intent fields do not imply valid geometry. `pose.valid` remains false until a complete cast, and invalid input is returned explicitly; root must inspect `Result::InvalidInput` before considering a previous valid pose. Root's UI recenter/zoom now use explicit `restoreOrbit`/`setUserDistance`, because inactive menu input deliberately ignores free camera gestures.


### Renderer compile before coordinated GPU run

`checks/robot-fixture-build-r02.log`: `//tests:salvage_asset_fixture` compiled successfully. The first compile caught a new assertion naming an unexposed robot encoded counter; corrected to the existing total encoded counter while keeping submitted robot and both dock counters. No production API was expanded. The approved execution filter is exactly `FixtureGPU.RobotUsesExactOwnerBytesRealAnimatedPixelsAndSameRetirementTicket:FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget`. The native journey owner explicitly released the GPU lane before the execution below.


### Coordinated renderer result

`checks/robot-fixture-gpu-r01.log`, `checks/robot-fixture-gpu-r01-cases.log`, `checks/robot-fixture-gpu-r01-cases.xml`, and `checks/robot-fixture-gpu-r01-summary.json`: the exact two approved cases ran and passed, zero skipped, on AMD Radeon 890M Graphics (RADV STRIX1)/Vulkan. Total test case time 781 ms. The XDG_RUNTIME_DIR environment notice remains in the raw log; both actual headless contexts initialized successfully.

- Robot GPU request: **387,600 bytes**. Idle opaque pixels: **549**. Idle-to-walk changed pixels: **586**. Stable background pixels: **3,509**. Hidden color and depth matched the empty baseline byte-for-byte. Invalid candidate preservation, insufficient owner budget refusal, unresolved-ticket/discard/submission semantics and final owner drain passed.
- Actual full scene + dock + robot owner: **10,119,560 bytes**, below the unchanged 16 MiB cap. Normal opaque draw count **129**; with 64 additional large bricks and three palette samples, **196** draws / **92** placements. Optional dock/robot counters cleared on successor publication before any successor draw.
- After process exit, the graphics lane was released explicitly to root for its browser journey. No further renderer run is needed for these unchanged inputs. Summary pins relevant source, shader, manifest, catalogue and actual robot mesh hashes. This is focused component acceptance, not a full suite, broad gate or publication claim.
