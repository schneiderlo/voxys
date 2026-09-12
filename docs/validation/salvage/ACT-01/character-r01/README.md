# Cove robot, locomotion and third-person camera

Scope: ACT-01–03 in `GAME_IMPLEMENTATION_TODO.md`. This record describes the
original single-job Salvage Cove on native Linux and browser WASM. General
jointed machinery, co-op prediction, final art and the two-job runtime remain
separate plan work. Validation is recorded below only after actual checks.

## Player controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Walk / swim; steer at helm | WASD | Left stick |
| Jump | Space | B / Back |
| Board, use or leave helm, return/reboard | E | A / Confirm |
| Orbit | Captured mouse | Right stick |
| Zoom | Wheel | Camera menu |
| Orbit / chase | V | L3, or camera menu |
| Recenter behind robot | G | R3, or camera menu |
| Keep towed load in view | M | Camera menu |
| Reduced motion | L | Camera menu |
| Camera menu | F2; browser Camera panel | Y / Alternate |
| Pause | P | Menu outside a modal |
| Workshop | B | View |
| Rescue | R | Existing recovery controls |

Camera menu input belongs entirely to the menu. Opening/closing, focus loss
and device changes neutralize movement and machinery. Explicit menu changes
work while paused. Recenter is an explicit player-requested heading change;
passive chase remains bounded and reduced motion disables passive chasing.
The construction controls and separate workshop camera retain their earlier
behavior. Camera choices join the next expedition save; simply leaving does
not save.

## Assets and runtime

`tools/salvage_assets/author_cove_robot.py` builds original Blender geometry,
materials, node hierarchy and actual exported animation keys. The installed
source, three cooked LODs, hashes and provenance are in `data/salvage/robot-r01`.
The runtime currently installs **LOD0 only**: 22 nodes, 15 mesh instances,
34 material draws, 387,600 bytes of requested GPU mesh payload. Farther LODs
are authored/cooked references, not a claimed runtime LOD feature.

The original cream/teal/coral robot uses molded surfaces, a rounded helmet,
unequal optical eyes, service panels, articulated arms and boots. Its height
is approximately 1.6995 m. The standing head/torso fit the actual 0.30 m-radius,
1.70 m-high controller capsule; the head check measures distance to the upper
capsule hemisphere, not merely horizontal width. Walking extremities reach
0.467 m radially; swimming reaches 0.592 m in installed LOD0, with a maximum
of 0.599 m across all authored LODs/clips. These animated extremities do not
enlarge the collision capsule. Full limb contact IK is not implemented.

Strict `salvage-animated-rigid-v1` cooking shares the bounded GLB/material/
geometry admission path and rejects skins, morphs, cameras, root motion,
animated scale and unsupported interpolation. Runtime admission independently
checks the graph, clips, channels, key ranges, transforms and byte budgets.
The loader reads a capped manifest and hashed VMESH through the existing
no-follow native/MEMFS byte provider; the hash must match before decode.

`rigid_animation.*` samples LINEAR/STEP translations and unit quaternion SLERP,
blends local TRS, evaluates the hierarchy, applies the authored basis, and
recomputes model matrices, anchor matrices and conservative bounds. There is
no skinning shader and no dependency on unused VMESH skin fields. The ordered
semantic clips map through asset indices: idle, walk, jump, fall, swim, helm,
tool and land. Outgoing nonlooping clips clamp during crossfades into loops.
The successful sampler uses fixed storage and makes no per-frame allocation.

`CoveCharacter` consumes accepted character time. Gait rate uses actual
relative displacement, so pressing into a wall does not keep walking in place.
A 0.14 s crossfade joins states; landed movement has a short landing clip.
Pause freezes phase and cannot queue a hidden landing transition. Tool/helm
clips are presentation states; authored hand/tool anchors are available for
future interaction IK, not claimed exact hand placement on every built helm.

The robot is a separate presentation asset under `SalvageAssetFixture`.
It consumes no construction slot, inventory ID, mass or body. Candidate
admission charges its real payload against the unchanged owner/resident GPU
limits. Materials, transformed normals, opaque depth and sun shadows use the
existing mesh pipeline. Submitted completion and retirement use the same
owner ticket as the kit and dock markings.

## Movement, camera and time

The player is a world-up capsule. Terrain support uses the exact LEGO footprint
query; camera terrain casts include stepped columns and circular studs. Boat
parts are oriented collision boxes. Every root and the oriented cargo obstacle
join one completed observed tick; incomplete/stale packets cannot certify
movement or a camera sweep. Static dock and installed harbor geometry remain
part of the same collision query.

Supported feet are stored in authored build coordinates with an explicit root.
On takeoff they become scene coordinates. The controller inherits point
velocity `v + omega × offset` once, then moves independently in world space.
It does not keep rotating or dragging an airborne character with the boat.
Supported heading follows deck yaw; input selects a world heading and airborne
heading stays independent. Landing uses relative approach to moving geometry.
Boarding/reboarding checks reach, support, clearance, speed and immersion.
An immersed helm releases the player to swimming; the existing durable Rescue
returns the craft and player while retaining protected designs and ownership.

Swimming uses the Cove's fixed water datum. It is simple recovery locomotion,
not sampled spectral-wave swimming, diving or a physical character mass that
pushes boat bodies. Arbitrary articulated-link support depends on future
accepted root/joint ownership; the current independent root path is shared.

The camera follows a point inside the capsule. Its swept sphere encloses the
actual projection's near-plane corners, plus a small margin. Current collision
is queried even for a stationary or paused camera. Obstruction shortens the
boom immediately; release and passive chase settle gradually. Manual orbit
holds off chase. Load view fits the actual oriented towed-cargo bounds within
the 12 m cap, subject to obstruction; it never promises visibility through
walls. An extremely short valid boom hides the robot to avoid filling the view.

GPU physics remains authoritative. The Cove renders boat/cargo, robot and
camera from the same completed observation already needed by the player.
This adds observation latency rather than guessing future collision geometry.
If a camera query lacks a complete safe result, a CPU-owned record replays the
last complete camera, placements, actor pose, dock, harbor and cables. The GPU
keeps progressing to obtain the next packet. The cache retains no GPU views,
body handles or submission tickets. Other experience render paths retain their
existing GPU body rendering. This is not multiplayer interpolation/reconciliation.

## Saving and reproduction

Live captures now use SVCE v6 character profile 1. It records world velocity,
facing and camera choices while preserving older profile-zero byte encodings.
Legacy attached jumps migrate once into detached world state. Full field order,
limits and restore rules are in `docs/salvage-cove-save-format.md`.

Reproduce source art into a new directory:

```sh
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_robot.py -- --output-dir /tmp/cove-robot-source
bazel build //tools:gltf_vmesh_tool
python3 tools/salvage_assets/cook_cove_robot.py --tool bazel-bin/tools/gltf_vmesh_tool --source-dir /tmp/cove-robot-source --output-dir /tmp/cove-robot-package
```

Focused checks:

```sh
bazel test //tests:cove_player //tests:cove_camera //tests:cove_character //tests:rigid_animation //tests:robot_asset //tests:cove_save //tests:native_workshop_menu //tests:gltf_vmesh_tool_test
```

Native/browser application builds, actual control journeys, real GPU fixture
checks and the mandatory full suite are separate acceptance steps. A source
or numerical asset check is never reported as a visual review.

The recorded browser run used the final frozen package and an isolated Chrome
profile. From the repository root, the actual command was:

```sh
VOXY_SMOKE_GPU=gaming-x11 VOXY_TEST_CHROME=/opt/google/chrome/chrome \
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_KEEP_PROFILE=1 VOXY_SMOKE_PORT=42759 \
VOXY_SMOKE_COVE_CHARACTER="$PWD/build-cove-actor-r01/browser-r01" \
VOXY_SMOKE_CHARACTER_SCREENSHOT="$PWD/build-cove-actor-r01/robot-playable-r01.png" \
VOXY_SMOKE_REPORT="$PWD/build-cove-actor-r01/browser-startup-r01.json" \
DISPLAY=:0 XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.8NQFV3 \
/home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs \
build-cove-actor-r01/web-r01 salvage-cove
```

For a later reproduction, use a newly packaged build, new output paths and
the current display credentials. Omit `VOXY_SMOKE_CHARACTER_SCREENSHOT` unless
a new visual change needs review. The completed run already supplies the
single playable image; there is no need to repeat it for publication.
[Final source and native binary fingerprints](checks/final-source-fingerprints.json)
and [all browser package hashes](checks/web-r01-hashes.json) identify the
tested artifacts. The retained profile is `/tmp/voxys-startup-qLuyqG`; it is
test data, separate from the user's browser world.

## Acceptance record

- **Native real controls:** eight ground/camera/save stages, ten moving-deck /
  swimming/reboarding/Rescue stages, and three final restore stages passed.
  Each phase continued the preceding durable test world. Final restore used
  the final rebuilt binary; ground and motion were not replayed for a
  paused-pose correction. Exact 11-part / 1,035 kg ownership and 48 material,
  camera choices and saved character state survived. See
  [asset and native evidence](asset-native/README.md).
- **Browser real controls:** eight stages passed in 10.811 s on the final WASM
  package: actual robot draws, walking, independent jump and landing, camera
  modal ownership, all camera choices, paused zoom and a real Save/reload.
  Stock, paid part IDs, character velocity/facing and camera settings matched.
  [Browser report](checks/browser-character-r01.json) and
  [startup/package report](checks/browser-startup-r01.json) retain the actual
  isolated profile/origin and runtime observations. No game-state setters.
- **Independent visual review:** one actual playable browser image was captured
  and reviewed once. Robot readability, apparent contact, shading and UI
  clearance have no actionable ACT defect. [Review](visual-review-r01.md).
  The image's incidental FPS overlay is not a performance benchmark.
- **Real GPU mesh ownership/pixels:** both focused fixture cases passed,
  zero skips. The robot changed 586 idle/walk pixels while 3,509 background
  pixels stayed stable; hiding it restored color/depth byte-for-byte.
  The scene + dock + robot reserved 10,119,560 bytes. A 64-brick build with
  92 scene/palette placements submitted 196 draws within unchanged limits.
  Candidate refusal, same-ticket completion/retirement and successor counter
  resets passed. [Exact renderer results](camera-fixture/checks/robot-fixture-gpu-r01-summary.json).
- **Focused logic:** seven character, six rigid-animation, nine camera,
  ten native-menu, three loader, 29 browser-controller cases and the preview
  UI assertions passed. Save/converter targets passed. The full player target
  initially passed 79/81, including all eight GPU cases; two obsolete fixture
  expectations were corrected and passed separately. Subsequent focused
  movement/restore checks passed, including oriented cargo and legacy detached
  jump restoration. [Movement record](movement/HANDOFF.md),
  [camera record](camera-fixture/HANDOFF.md).
- **Actual archive reader compatibility:** the updated read-only helper parsed
  an unchanged historical v4 construction save and both actual v6 phase saves,
  checking envelope/nested hashes and bounded fields.
  [Record](checks/actual-archive-reader-compatibility.json).
- **Builds:** native CMake r04 and WASM CMake r03 passed. Initial native build
  exposed the X11 `Status` macro collision; the camera enum is now `Result`.
  Initial WASM build exposed unsupported unsigned-character JSON traits; the
  parser now consumes a bounded char view. These failures and final build
  logs are retained in `checks/`.

- **Mandatory repository suite:** 2,182 game cases passed, zero failures,
  three skips and four disabled cases. The unchanged terrain importer target
  was reused from cache: ten passes and one skip. The run took 1,173.781 s
  including build work. All final implementation hashes still matched.
  [Exact counts, skipped reasons, XML and raw logs](checks/full-suite-r01/summary.json).
- **Checkpoint:** publication uses the containing commit through the normal
  pre-commit hook and a normal push to main. The local preview uses the tested
  `build-cove-actor-r01/web-r01` package at
  `http://127.0.0.1:42751/index.html?experience=salvage-cove`, preserving the
  existing save origin. Save an open old session before reloading it.

Full ACT parents and G08 retain their unmet prerequisite gates; the verified
Cove components do not pass the entire material, water, machine-solver or
input-lifecycle programs. The final documentation review and its corrections
are recorded [here](documentation-review-r01.md).
