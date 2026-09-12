# ACT-01 robot asset and rigid animation runtime

Status: all six final CPU runtime cases pass, including the rounded-head
installed asset and outgoing-clip loop policy. Real native ground/camera/save
and moving-deck/swim/rescue continuations passed 8 + 10 stages respectively.
Final rebuilt restore-only passed three stages with exact ownership and
paused-state checks. At this handoff, numeric fixture GPU tests and browser integration were
separate pending checks; their subsequent passing results are in the parent
character record. Broader whole-game gates remain open. No screenshots or
render previews were produced by this asset or native journey work.

## Integration contract

- `src/game/assets/rigid_animation.hpp/.cpp` owns immutable CPU admission and
  allocation-free successful sampling. `RigidAnimationAsset.mesh` is the actual
  VMESH with clips intact; `prefab` owns validated rest geometry/accounting.
- Use `asset.clips[semanticIndex]`, ordered `idle, walk, jump, fall, swim, helm,
  tool, land`. These map to actual exported clip indices. Durations respectively
  2.0, 0.8, 0.5, 0.6, 1.2, 2.0, 1.0 and 0.4 seconds.
- `sampleRigidAnimation` accepts a source clip/time, explicit looping choice,
  optional second clip/time/weight and its own looping choice and a proper camera-relative canonical root.
  It blends local translations and normalized shortest-arc quaternions, then
  evaluates parents before children. Equivalent quaternion signs use a
  deterministic dominant-component hemisphere, including exact 180-degree ties.
- Output `draws[0..drawCount)` is ready for `MeshPath`: each matrix already
  includes the explicit export-to-canonical basis12 exactly once. Canonical
  orientation is +Y up and -Z forward. Do not apply another basis/heading flip.
- Output `anchors` and current per-pose `bounds` use the same camera-relative
  canonical root. Anchor order: head, left hand, right hand, tool, left helm
  grip, right helm grip. Anchors are real meshless children of the animated head
  or hand; they are presentation transforms, never construction sockets.
- `robot_root` is meshless, identity TRS and never animated. There is no root
  motion, skinning, morphing, collision body, inventory row or save identity.
- Root owns the fixed installed-directory loader, application state/camera,
  fixture upload and its shared CPU owner lifetime. Retain the asset until the
  normal GPU submission/retirement protocol finishes. Sampling does not submit
  work or claim GPU completion.

## Bounded resources

All LODs have 22 nodes, 15 mesh instances, 34 expanded material draws, six solid
PBR palette materials, eight clips and 73 channels. Actual LOD0 requests 387,600
GPU payload bytes and owns 387,280 VMESH decoded bytes. LOD1 requests 171,216 GPU
bytes; LOD2 requests 114,000. The unchanged hard animation limits are 32 nodes,
24 instances, 48 draws, 1 MiB GPU payload, 2 MiB decoded payload and 256 KiB key
bytes. Animation keys currently occupy 42,164 bytes. Frame pose arrays are fixed;
only admission owns variable-sized vectors. Fixture shared buffers/environment
are separate existing charges. Root selects LOD0; the other two source/cooked
LODs are provided and CPU validated, without claiming automatic runtime LOD use.

## Source and package

- Installed `data/salvage/robot-r01/{manifest.json,robot.vmesh,lod1.vmesh,lod2.vmesh}`.
- Three authored `.blend` and GLB files in its `source/` directory; original
  palette cream F0DDB2, teal197D86, orange ED7942, slate253D53, steelB5C3BE and
  rubber25363A. No external artwork or generated bitmap textures.
- `tools/salvage_assets/author_cove_robot.py`: actual rigid-node hierarchy and
  Blender NLA clips. Export preserves constant non-rest poses. Cleanup removes
  only rest-equal tracks and measured unit-scale decomposition noise <=1e-6;
  exported moving translation/quaternion samples stay unchanged.
- `cook_cove_robot.py`: exact authored input hash checks, explicit strict profile
  only, three bounded real cooks, complete new-directory publication, manifest
  plus source/cooker/provenance hashes. Existing packages are not overwritten.
- `check_cove_robot.py`: independent actual GLB key/vertex decoder. Reports unit
  normals and sampled envelopes; these samples are not continuous culling bounds.
  Runtime recalculates each sampled pose bound from all transformed mesh bounds.

## Validation and retained attempts

`checks/final-cpu-r01-rigid_animation.xml` records 6 tests, 0 failures and
0 skips, 69 ms GTest. Earlier `checks/runtime-r04.xml` records five passing
cases before the outgoing-loop case and rounded-head regression were added;
that earlier Bazel invocation elapsed 2.830 seconds. It checks basis/anchors/normals/bounds, looping and
clamping, STEP interpolation, sign-equivalent blending, 14 malformed input
mutations, failed-sample output preservation, outgoing nonlooping crossfades,
actual neutral core/capsule clearance and all eight clips in all LODs.

Earlier `runtime-r01.log` failed analysis before compilation because the asset
BUILD glob was installed before its files. `runtime-r02.log` rejected two strict
compiler warnings. `runtime-r03.log` ran all five cases and exposed one exact
180-degree quaternion tie defect (4 pass, 1 fail); the implementation was fixed,
not the expected result. Original logs remain intact.

Author attempts r01/r02/r03 preserve an array-conversion exception, the initial
polygon assertion and explicit rejection of Blender scale rounding noise.
Actual source r04 completed all three LODs but had overly wide arms. Source r05
narrowed the body; r06 tightened head horizontal clearance and walk width.
Source r06 and strict package-r03 all exit0. The accepted torso/head radial
sampling at that point did not cover the capsule top hemisphere; independent
review identified this afterward. The final bounded r07 dome/inset-face
correction and strict package-r04 all exit0. Actual indexed head vertices now
reach at most .299740269 m from the real capsule segment, and torso .277353567 m,
within radius .3. Neutral height is 1.699499965 m; sampled walk width is
.588991918 m. Walking hands/feet reach .466876639 m radially; other clips
reach .591722772 m in installed LOD0 (swim), or .599285920 m across all
authored LODs/clips. They are presentation motion, not a wider collision
capsule or ground IK claim.
Independent review accepted this core-clearance correction. Original packages
remain in scratch history.

`native-journey-r01/summary.json` records eight real native stages in 10.497 s:
visible 34-draw robot, moving walk animation, jump/fall/land, actual F2 camera
mode/zoom/load/reduced/recenter and durable schema6 checkpoint. The same world
continues in `native-motion-r01/summary.json`: ten stages in 19.230 s covering
board/helm/drive/leave, jumping from a moving deck, swim/reboard, rescue and its
durable checkpoint. Observed inherited horizontal-velocity error was .057958 m/s.
Both processes terminated only on requested SIGTERM, with no force kill or
matching runtime/GPU error lines. The exact executed driver is retained at
`checks/native-ground-motion-executed.py`; later restore-only boot simplification
is described separately in its adjacent JSON. These are two linked journeys,
not one uninterrupted process. No ground/motion route will be repeated for the
final rebuilt restore-only check. `native-restore-r01/summary.json` now records
that successful three-stage, 2.201 s check on final native r04. It compares the
physical design/inventory archive, camera settings, position/mode, world velocity
and facing, then verifies exact frozen player/character/camera eye after later
completed paused frames. Its single process also closed cleanly on requested
SIGTERM. Ground/motion binary SHA starts `8c7a1ab4`; final restore binary starts
`7e39b418`. The final restore includes the later paused semantic-clip and oriented
cargo restore-preflight fixes. No earlier control route was replayed.

## Reproduction

```sh
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 \
  --python tools/salvage_assets/author_cove_robot.py -- --output-dir /tmp/new-robot-source
nix-shell --run 'python3 tools/salvage_assets/cook_cove_robot.py --tool bazel-bin/tools/gltf_vmesh_tool --source-dir /tmp/new-robot-source --output-dir /tmp/new-robot-package'
python3 tools/salvage_assets/check_cove_robot.py /tmp/new-robot-package
nix-shell --run 'bazel test //tests:rigid_animation --test_output=errors'
```

The installed bundle is an explicit immutable asset selection. Reproduction
writes a new candidate; changing source files alone must not silently replace a
live actor or its GPU owner.
