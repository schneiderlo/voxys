# Corrected molded propeller r04

Original Blender 5.2.1 LTS presentation for canonical `functional-kit/r09/propeller`.
The current `cove-workshop-r04.json` catalog pins this directory and manifest
`390120c23e8e062886c92ff358d5e50dc9ba63df7b73ca114f86a8388da40812`.
Visual keys use `voxys-toy-art-v1`, version 1, counters 711–713. Canonical gameplay,
shaft endpoint, guard envelope, LOD thresholds and basis remain unchanged.

This replaces the rejected r03 presentation (701–703), whose inherited blades
intersected the guard. Narrower, shorter blades have a conservative 13.994 mm
radial gap inside even the coarsest guard polygon. The proof covers all blade
rotations and LODs before bevel; it does not establish whole-assembly motion or
animation. The propeller is currently static.

From repository root, with a new output directory:

```sh
blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/salvage_assets/author_cove_propeller_art.py -- --output-dir <new-directory>
```

Use the strict cook command in
`docs/validation/salvage/LOOK-01/machinery-r01/README.md`; do not reuse the old
power recipe's propeller output. Source `.blend`, three GLBs, source/cooked
sidecars, VMESH payloads and hashed provenance are included. Materials are solid
PBR, with no textures, images, UVs or tangents. Blender completed export/save
before a recorded PulseAudio shutdown interruption. Strict cook, canonical
registry and final GPU admission pass. Exact bytes, the rejected evidence and
application results are recorded in the linked checkpoint. Never overwrite a
published visual ID; final appearance approval and animation remain open.
