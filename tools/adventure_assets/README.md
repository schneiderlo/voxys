# Original adventure building kit

This is the first useful-home kit for `G-A` in the active game plan. It is
original construction-toy content, with cream, teal, warm wood, coral and slate
materials. No external model, texture, image generator or screenshot was used.

The canonical editable dimensions live in
[`catalog.json`](../../data/adventure/building-kit-r01/catalog.json).
`generate_catalog.py` produces the immutable C++ catalog from those exact bytes.
The runtime content fingerprint is their SHA-256. Costs use the shared adventure
item catalog. Piece IDs 1–14 are stable; compiled mesh index is `ID - 1`.

## Geometry and runtime contract

- Coordinates are metres, +Y up, +X right, −Z forward. Origins are bottom-centred.
  The Blender conversion is `(x, -z, y)`, so exported GLB/VMESH is already
  canonical, **basis 0**. Do not apply the old Cove basis-12 correction.
- One lattice tick is .02 m; stud pitch 1 m, plate .32 m, brick body .96 m.
- The kit has foundation, floor, wall, open doorway, flat roof, stairs, beam,
  three brick sizes, bed, chest, workbench and pier. The doorway is 1.36 m wide
  and 2.24 m high. Stairs rise toward −Z through six .16 m steps.
- Use the union of the **30** catalog solid boxes for collision. The aggregate
  footprint is a placement bound, never a room-filling collider. Bed and bench
  legs, doorway opening and stair heights are real geometry.
- Floors and stair treads have no raised decorative studs. Their trim differs
  from authoritative contact faces by at most .02 m. Bricks have cosmetic .18 m
  studs; the .96 m body remains the stack/contact pitch. Studs are not extra
  structural solids or a reason to block normal stacking. Brick undersides
  occlude the inserted studs when stacked.
- Bed/chest/workbench meshes do not own gameplay. The accepted structure
  component creates inventory, bed and crafting functionality atomically.
- Both LODs use one mesh per piece, 29 total submeshes and five shared opaque
  PBR materials. Each piece needs one to four submesh draws. Near has 14,908
  vertices and 20,100 indices; far has 6,984 vertices and 9,924 indices.
  Both resident payloads request 1,696,960 GPU bytes before instance buffers.
  Shared mesh/material batching remains the renderer's responsibility.

The installed package is
[`data/adventure/building-kit-r01/cooked`](../../data/adventure/building-kit-r01/cooked).
It contains two `.vmesh` files, editable `.blend`/`.glb` sources, exact hashes,
provenance and the numerical geometry report. The strict existing
`salvage-rigid-v1` **import format** is reused; these stationary houses are not
salvage parts or dynamic boat assemblies.

## Reproduce

Use new output directories; the tools refuse to overwrite existing assets.

```sh
python3 tools/adventure_assets/generate_catalog.py --check
blender --background --factory-startup --python-exit-code 1 \
  --python tools/adventure_assets/author_building_kit.py -- \
  --output-dir /tmp/my-new-adventure-kit
python3 tools/adventure_assets/cook_building_kit.py \
  --source-dir /tmp/my-new-adventure-kit \
  --output-dir /tmp/my-new-adventure-kit-cooked \
  --tool build-native-save-host/bin/gltf_vmesh_tool
python3 tools/adventure_assets/check_building_kit.py \
  data/adventure/building-kit-r01/cooked
```

The installed source used Blender 5.2.1 LTS. The checker verifies the generated
C++ against the canonical JSON; stable mesh/node IDs and identity transforms;
finite vertices, unit normals, draw/byte limits; visual containment within the
true solid/decor envelope; all six actual faces of every collision box; and
full triangle clipping against the empty doorway. It also verifies the six
riser heights and the smaller second LOD. It does not certify a playable room,
accepted placement, human art approval or world performance.

Initial authoring attempts found exporter mesh names inherited `Body` instead
of piece IDs, then excess vertices spent on millimetre trim bevels. The recipe
now names mesh data explicitly and leaves shallow trim planar, preserving body
bevels without raising the 22,000-vertex cap. The first checker assumed 32-bit
indices; it was corrected to decode both legal VMESH index strides. Only the
final passing package was installed.
