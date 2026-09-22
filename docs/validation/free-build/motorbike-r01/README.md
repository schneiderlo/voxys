# Mountable creative motorbike — 2026-09-16

Owner brief: press M to mount a red construction-toy motorcycle matching
[the reference](owner-reference.png), and make it ride properly.

## Controls and behavior

- M or the Motorbike button summons/mounts at the current safe standing position.
- W accelerates. S brakes, then reverses at low speed. A/D steer relative to the bike.
- Space brakes. M gets off only at low speed onto clear, supported ground.
- The bike stays balanced, leans into turns, spins both wheels, steers the front
  assembly, and follows front/rear terrain support. Gravity handles drops.
- All movement uses fixed 60 Hz accepted state and three swept collision volumes
  against the same terrain and player buildings. Steep slopes, shores, unknown
  support and blocked mounts fail closed. Menus/focus loss stop buffered motion.
- Building is disabled while riding. Dismounting returns walking/building controls.
- This is an assisted solo transport feature, not the separate motocross physics
  simulation. The bike is transient: saves preserve the player/build, not a
  parked vehicle record. Restoring starts on foot and M summons it again.

## Implementation and reproduction

`src/game/adventure/builder_motorbike.*` owns movement and safe dismount queries.
`adventure_runtime.*` routes M/action 32, loads the hash-bound installed mesh,
updates the player/session, renders the seated rider and frames the bike.
`ui/src/main.jsx` supplies the mount button and compact riding controls.

`tools/adventure_assets/author_builder_motorbike.py` produces the original mesh
with Blender: four rigid meshes (body, front assembly, rear wheel, front wheel),
6 materials, no textures. Export with a fresh output directory:

```
blender --background --factory-startup -noaudio --python-exit-code 1 \
  --python tools/adventure_assets/author_builder_motorbike.py -- --output /tmp/new-bike
bazel-bin/tools/gltf_vmesh_tool --profile salvage-rigid-v1 \
  /tmp/new-bike/motorbike.glb /tmp/new-bike/motorbike.vmesh
```

Installed source, provenance, manifest and cooked data live in
`data/adventure/motorbike-r01`. Updating the installed asset requires updating
its exact byte count/digest in `installedMotorbikeMesh`, then rebuilding WASM.
The loader checks the four-mesh layout and retains separate renderer slot 8.
The package contains 7,154 vertices and 28,356 indices; cooked bytes: 573,429.

## Validation

- `//tests:adventure_world`: fixed-step consistency, acceleration, turning,
  braking, dismount limits, thin-wall sweeps, low roofs, water and pause input.
- `//tests:adventure_runtime`, using the installed 8192 terrain: actual M key
  edges, held-key behavior, acceleration, braking, dismounting and build restore.
- `npm test` in `ui`: 5 passing UI checks including mount routing and riding HUD.
- WASM build and actual browser M mount confirmed. Final browser mount/dismount review passed after
  corrected normals/terrain contact and the revised figurine. No claim of hardware-controller testing.
- `rider.png` is a Blender rendering of the actual installed model and seated
  pose. The owner subsequently supplied a new figurine reference in
  `docs/design/free-build/figurine-target`; this image predates that character pass.

Local only; publication and owner visual acceptance remain open.
