# Original village props

Editable, original brick scenery for the adventure's Meadow cottage and Market
shelter: clay stepped roof, wooden planter with flowers, block-canopy tree and
stone path tile. The existing building catalog supplies foundations, walls,
doorway, stairs and flat roof supports. The scenery does not spend materials or
become player-owned furniture.

`source/` contains both Blender and GLB sources. `manifest.json` records exact
source recipe, cooker and payload identities; `provenance.json` records the
authoring checks. Runtime loads the near model and checks its exact bytes and
SHA-256 before parsing. The far model is prepared content; runtime LOD selection
is not implemented for this package.

## Runtime contract

Profile `salvage-rigid-v1`, asset `voxys-adventure-village-props-r01`, canonical
basis 0 (+Y up). Four identity nodes and meshes in this exact order:

| Index | Prop | Canonical size, metres | Collision |
| --- | --- | --- | --- |
| 0 | Roof | 4.6 × 1.28 × 4.6 | Eight stepped boxes, matching the two roof slopes |
| 1 | Planter | .9 × .7 × .9 | Box through .5 m; upper flowers are decoration |
| 2 | Tree | 2.6 × 4.2 × 2.6 | .44 m square trunk to 2.4 m; block canopy above |
| 3 | Path tile | .64 × .08 × .64 | Tile box |

Place at the authored feet using translation and quarter-turn yaw only. Never
stretch a prop. Tiny surface seams, inlays and bevels are decorative differences
from the box proxies, at most .011 m in the cooker envelope check. The tree's
canopy is a solid block; its collision does not pretend scattered leaves fill an
empty volume. Colors are material factors, with no external image textures.

Near: 457,370 file bytes, 6,104 vertices, 2,664 triangles, 13 draws and 471,968
requested GPU geometry bytes. Far: 266,138 file bytes, 3,528 vertices, 1,704
triangles, 13 draws and 274,976 GPU geometry bytes. These exclude instances and
the rest of the scene; they are not a frame-rate measurement.

`VillageLayout` owns real terrain placement and admission. An existing saved
player or building takes priority: a conflicting scenery group is omitted
together with its collision. Admission is retained through that live session.
Installed scenery is protected from removal and new intersecting construction.

## Reproduce and verify

From the repository root, use fresh output directories:

```sh
/snap/blender/current/blender --background --factory-startup -noaudio --threads 2 --python-exit-code 1 --python tools/adventure_assets/author_village_props.py -- --output-dir <new-source-directory>
python3 tools/adventure_assets/cook_village_props.py --source-dir <new-source-directory> --output-dir <new-package-directory> --tool bazel-bin/tools/gltf_vmesh_tool
```

Both exports passed closed-component, nondegenerate-face and positive-volume
checks. The strict cooker then checked source hashes, canonical basis, actual
VMESH mesh/node order, normal/index validity and measured geometry envelopes.
The authoring process completed all exports and provenance but was interrupted
after hanging during audio shutdown (`pa_write`); its exit status is not claimed
as a successful authoring-process test. Independent cooking of the finished
source files passed. Actual game rendering and player traversal are separate
integration checks, not proven by asset export alone.
