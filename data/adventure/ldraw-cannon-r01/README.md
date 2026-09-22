# Authentic LDraw cannon

Articulated source asset, now placed in Free Build at X/Z `(1204, -1074)`.
Walk near it and press C, aim with A/D and W/S, fire with Space/click, leave with C.
The anchored cannon fires physical sphere projectiles against static GPU house
collision; barrel recoil is cosmetic. House destruction remains open
in [the destruction plan](../../../docs/design/free-build/lego-destruction/IMPLEMENTATION_TODO.md).

This is the community-authored LDraw `2527c01.dat` shortcut by Andy Westrate,
using Paul Easter's `2527.dat` base and `518.dat` barrel, with contributing
primitive authors. It models actual LEGO cannon parts. It is not a LEGO-company
asset or a claim to reconstruct an entire set. The wrapper chooses a red base;
the source shortcut supplies the dark-grey barrel and 15-degree elevation.
All source authors and CC BY 4.0 notices are retained in `source/`.

- `cannon-assembly.blend`: editable original base and barrel meshes.
- `cannon-runtime.blend`: material-adjusted separate base/barrel runtime geometry.
- `cannon.glb`: portable Y-up runtime export.
- `cannon.vmesh`: cooked `salvage-rigid-v1` asset, 182,911 bytes.
- `preview.png`: neutral Blender render, **not an in-game screenshot**.
- `articulation.json`: validated mesh indices, hinge and muzzle contract.
- `validate_runtime.py`: file, mesh, ground-contact and hinge/muzzle validator.
- `manifest.json`: exact file hashes and cooked counts.
- `provenance.json`: source/importer/recipe hashes, colours, scale and bounds.
- `source/`: pinned wrapper, 27 original dependencies, seven authors,
  licensing, archive provenance and standalone source validator.

The runtime keeps all 2,368 triangles, with 2,333 cooked vertices, two material
submeshes and two meshes/nodes. There are no generated substitute parts. The
standard-resolution source has visible faceting around curved details.

Scale is **one world unit per stud** (20 LDU). Bounds are approximately
3.40 wide × 3.41 high × 7.91 long studs. The root is horizontally centred and
grounded at Y=0. The muzzle faces +Z. Both mesh nodes have identity transforms and vertices in
shared grounded root coordinates. Mesh/node **0 is the base**, mesh/node **1 is
the barrel**. The original pose is 15 degrees upward.

- Hinge: `(0, 1.799994707, 0.401407957)` studs.
- Muzzle lip centre: `(0, 2.705861092, 3.782148838)`.
- Muzzle direction: `(0, 0.258818924, 0.965925813)`.
- Muzzle is 3.5 studs from the hinge; source bore radius is 0.5 studs.
- Barrel transform: `T(hinge) * Rx(-(elevation - 15 degrees)) * T(-hinge)`.
- Apply common world position and Y-axis yaw after this transform. Base receives
  only common world position/yaw. Rotate the muzzle with the same barrel transform.
- Spawn a projectile beyond the lip by its radius plus clearance; scene collision
  and self-hit exclusion remain gameplay responsibilities.

The hinge comes from the actual `518.dat` origin/pins, and its muzzle plane is
at source Z=-70 LDU. The exporter verifies that source ring against 64 vertices.
The runtime validator checks geometry at 5, 15 and 45 degree aiming poses.
All 2,368 triangles remain: vertex displacement from the earlier merged export
is at most 0.00000102 stud (float roundoff). The existing preview remains valid
for the unchanged default 15-degree pose; it is not an aiming or firing demo.

Plastic uses source linear LDraw colours, roughness 0.48 and zero metallic.
The source meshes are preserved; the runtime uses a simple opaque material
compatible with the existing game renderer. Studio lighting is only for import
inspection, not a proposed change to the world's warm lighting.

## Rebuild

Requires Blender (validated with 5.2.2 LTS), the game's `gltf_vmesh_tool`, and
[TobyLobster/ImportLDraw](https://github.com/TobyLobster/ImportLDraw) pinned to
`c306fb777a4e0da85492f09d65daf458767a0aa1`. Importer code is external and GPL;
the LDraw-derived geometry keeps its source CC BY 4.0 attribution.
Use a new, nonexistent output directory:

```sh
python3 data/adventure/ldraw-cannon-r01/source/validate.py
blender --background --factory-startup --python-exit-code 1 \
  --python tools/adventure_assets/import_ldraw_model.py -- \
  --source data/adventure/ldraw-cannon-r01/source \
  --importer-root /path/to/ImportLDraw \
  --output-dir /tmp/cannon-rebuild --articulate-cannon --proof
bazel-bin/tools/gltf_vmesh_tool --profile salvage-rigid-v1 \
  /tmp/cannon-rebuild/cannon.glb /tmp/cannon-rebuild/cannon.vmesh
```

After installing rebuilt output and updating manifest hashes, run
`python3 data/adventure/ldraw-cannon-r01/validate_runtime.py`.

Distribute attribution and licenses alongside converted meshes. Update manifest
and provenance when rebuilding; binary blend/GLB hashes may depend on exporter
version and metadata. Validate the resulting geometry and counts, not only that
the command completed.

Source discovery: [LDraw cannon/base listings](https://library.ldraw.org/parts/list?tableSearch=2527.dat).
Exact pinned archive and per-file hashes live in `source/manifest.json`.
