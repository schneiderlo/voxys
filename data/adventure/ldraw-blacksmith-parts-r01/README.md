# Blacksmith source parts — D2 bounded wall export

This derivative uses **actual meshes from the existing LDraw 21325 Blacksmith**.
It does not generate substitute LEGO shapes. It exposes 39 original upper-front
wall bricks/plates for the destruction prototype; the rest of the house stays
one fixed render batch. This asset alone does not implement destruction.

The source is Vincent Messenet (Cheenzo)'s community reconstruction. Retain the
[complete attribution and mixed CC BY 2.0/4.0 notices](../ldraw-blacksmith-r01/source/ATTRIBUTION.md)
and pinned original sources when distributing this derivative. The original
MPD, source dependency files, editable assembly and static asset are unchanged.

## Files and ownership

- `wall-parts.vmesh`: 81,606,908 bytes; 848,493 triangles; 12 mesh nodes;
  56 material submeshes, 45 materials. Mesh/node 0 is the fixed remainder.
  The other 11 shared meshes render the 39 selected source instances.
- `wall-parts.glb`: the auditable cooker input, with identity mesh nodes.
- `wall.json`: small runtime selection, validated bonds, source hash and support
  coverage. This is the runtime topology contract.
- `assembly.json`: all 2,140 original mesh-instance identities, all 438 shared
  source geometry keys, source hierarchy, part filenames, colour codes, source
  hashes and unquantized transforms. Unsupported connector types are explicit.
- `remainder.blend`: joined, grounded collision-bake input with all 39 selected
  parts removed. Use it to replace the old indivisible house collision bake.
- `manifest.json`: pinned input/output file hashes and counts.
- `geometry-validation.json`: independent comparison against the original blend.
- `remainder-collision.json`, if present, is produced separately by the existing
  multi-axis mesh collision baker, under the runtime integration task.

The whole 438-mesh library is **not loaded as thousands of draw instances**.
Only the selected repeated geometry is shared in the runtime mesh. Unselected
instances retain their original `geometryKey`, resolvable in the unchanged
`sourceBlend`, and identify their merged remainder render membership. This keeps
D2 under the existing rigid-cooker node capacity without raising global caps.

## Version 1 contract

`sourceId` and bond `id` are **decimal strings**, parsed as unsigned integers.
They exceed JavaScript's exact numeric range. Instance IDs use the first 60 bits
of SHA-256 over the pinned MPD hash plus the original imported object hierarchy.
The validator rejects ID collisions. IDs are stable for this pinned
source assembly, not promised to survive replacing it with a different MPD.

All runtime geometry uses **Y up and one unit per stud**. Local brick body top
is Y=0; bottom is Y=-1.2 for bricks or -.4 for plates. `translation` and
`rotation` (quaternion **w,x,y,z**) place each selected part in the same grounded
house frame as the original asset. The runtime then applies the common house
plot transform (currently 180-degree yaw). `matrixRows` is the authoritative
full source affine transform; unsupported remainder pieces may retain slight
non-unit scale or arbitrary source rotations. They are never snapped to the
24 player-build grid orientations. Selected parts must be rigid within 2e-5.

Selected types: 3005 (1×1 brick), 3004 (1×2), 3010 (1×4), 3009 (1×6), 98283
(1×2 masonry brick), 3024 (1×1 plate), 3023 (1×2 plate), 3623 (1×3 plate).
`studsX` follows the long local X axis; `studsZ` is the short axis. Slots use
`z * studsX + x`. Top-stud interface Y=0 and lower interface Y=-height refer
to the mating planes, not the raised stud tip. Connections require positions
within .002 stud and opposed normals; overlapping bounding boxes do not bond.

The 46 internal bonds and five external boundary bonds identify actual standard
stud interfaces. Four selected parts have validated connections to intact
non-selected pieces. Components of 15 and 14 selected parts contain these
boundary anchors. The other ten parts (components 7,1,1,1) bear on special
hardware whose connectors are not modeled: they have
`supportStatus: unsupported-external-support` and
`manualReleaseEligible: false`. Keep them fixed. **These are boundary anchors,
not proof of a fully connected foundation-to-roof house graph.**

The complete 39-part selection is an upper-front facade region around local
Z=2.461079, X=-.4..14.6, top Y=11.2..17.6. Window hardware, SNOT parts, arches,
clips, roof attachments and decorations remain in the intact remainder.
Releasing a stud connection cannot erase a physical bearing support. A gravity
check must account for remaining windows/arches/beam contact and must not
fabricate falling motion just to make the graph appear correct.

`bodyBounds` are **approximate solid rectangular collision envelopes**. They
omit studs, underside cavities and masonry relief. Visible geometry is exact
source CAD, including those details. This is not triangle collision or exact
plastic volume/inertia. Mass and support policy belong to the runtime adapter.
Unknown part types are explicitly unsupported; there are no guessed connectors.

Hard bounds: 4,096 source instances, 1,024 shared source geometries, 64 selected
parts/nodes, 512 internal bonds, 512 external bonds. Capacity, unknown schema,
unsupported selected types, invalid transforms, malformed IDs and connector
mismatches fail validation. Do not truncate data to pass admission.

## Reproduce and validate

Run from the repository root, using Blender 5.2.2 LTS and the existing cooker:

```sh
/snap/blender/current/blender --background --factory-startup \
  --python tools/adventure_assets/export_ldraw_assembly.py -- \
  --output /tmp/blacksmith-parts-rebuild
bazel-bin/tools/gltf_vmesh_tool --profile salvage-rigid-v1 \
  /tmp/blacksmith-parts-rebuild/wall-parts.glb \
  /tmp/blacksmith-parts-rebuild/wall-parts.vmesh
python3 tools/adventure_assets/test_ldraw_assembly_export.py
python3 tools/adventure_assets/validate_ldraw_assembly.py \
  data/adventure/ldraw-blacksmith-parts-r01
/snap/blender/current/blender --background --factory-startup \
  --python tools/adventure_assets/validate_ldraw_assembly_blender.py -- \
  --asset data/adventure/ldraw-blacksmith-parts-r01
```

The export retains every selected source vertex, matches material policy of the
original importer, and applies its .82 decimation ratio only to the fixed
remainder. Rebuild results should be reviewed before refreshing pinned manifests;
Blender metadata/timestamps can change artifact bytes even with identical meshes.

Validation on 2026-09-18: all 2,140 source transforms checked against the original
blend; all 39 selected meshes checked in both directions with **zero vertex
position error**; cooker accepted without global limit changes; 13 focused
contract/refusal tests passed. Runtime physics, player contact, browser rendering
and support collapse require separate integration evidence.
