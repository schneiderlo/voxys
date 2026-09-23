# Blacksmith ground-wall destruction candidate

This package exposes 20 **original LDraw source pieces** from the left ground-floor wall of the existing community Blacksmith reconstruction. It is an alternative runtime selection to `ldraw-blacksmith-parts-r01`; the old 39-piece upper-wall package remains unchanged. It uses the same pinned source MPD, original editable Blender assembly, and authentic part meshes. This is a bounded wall prototype, not whole-house destruction.

Preserve the original [source attribution and mixed CC BY 2.0/4.0 notices](../ldraw-blacksmith-r01/source/ATTRIBUTION.md). The community model is by Vincent Messenet (Cheenzo). All 2,140 original source identities and 438 shared geometry keys remain recorded in `assembly.json`. Source identity, full transforms, part numbers, colours, and source mesh geometry are retained.

## Runtime contract

- `wall-parts.vmesh`: 19,606,015 bytes, 243,279 vertices, 520,425 indices, 173,475 triangles, 54 material submeshes, 44 materials, 11 mesh nodes. Only the fixed remainder's render triangles were reduced; its collision boxes and all selected movable meshes stay at source detail.
- Node/mesh 0 is the fixed remainder. The remaining ten shared meshes render the 20 selected instances. Nodes have identity transforms; each instance uses its source transform from `wall.json`.
- Coordinates: Y up, one unit per stud, common house yaw pi. Quaternion order is w,x,y,z. Source IDs are decimal strings, not JavaScript numbers.
- Selection: 18 wall pieces plus two actual 2×8 floorplates beneath the opening. The floorplates are source IDs `3336692187990364` and `30700057810174424`; both have exact catalog stud connections. They are not invisible collision deletions.
- 18 selected pieces form one connected component with validated boundary stud connections. Two isolated 1×1 plates have unsupported external hardware support and must stay fixed. There are 30 internal stud bonds and 44 boundary bonds.
- Source geometry validation checked all 2,140 transforms and all 20 selected meshes: maximum local vertex error 0 studs.
- `remainder-collision.json`: 6,409 boxes. Refinement uses the union of individual source-member regions, with a .2-stud fine grid and one-cell halo. The existing selected-shell/stud exclusion clearance remains .0201–.0401 studs. Space between separated members retains the ordinary coarse collider bake. Adjacent boxes merge only when their occupied union is unchanged.
- The local asset bake limit is 6,500 boxes. Native full-scene admission still needs verification against existing global limits; no global physics capacity was raised.
- Generated C++ collider header: `src/game/adventure/ldraw_blacksmith_ground_remainder.hpp`. It uses the same `blacksmithRemainderSolids` symbol as the alternate upper-wall header; include only one.

A useful support-pair regression is floorplate `30700057810174424` → brick `773080669553799255`, bond `235382094044361437` (plus the other studs connecting that pair). The planned first-shot target is masonry brick `93361846531299384`.

## Cannon placement

`aim.json` contains the complete initial trajectory parameters:

- Grounded cannon position: world `(1240, .185, -1027)`.
- Fixed heading: `-pi/2`; relative yaw `-.036033274856302144`.
- Elevation: `.07267675847120061` radians.
- Speed 48, sphere radius .44, gravity -9.81, damping .05, 60 Hz and 16 force-preparation substeps.
- Target source `93361846531299384`, local contact-centre `(-5.339568557739257, 2.5999895572662353, -4.038921356201172)`.

Sixteen sphere samples per simulation tick clear all 6,409 retained collider boxes by at least .76062 studs before target contact. This geometric prediction requires the native runtime firing test; it is not a claim of an observed GPU impact.

## Known opening limit

Do not claim a fully walkable hole from this package alone. A route search used the real creative capsule radius 1.12, height 4.76, variable rounded support, 1.26-stud step height, .1-stud X/Z samples and swept intermediate checks. With all selected pieces absent, it can reach local X about -3.3 from outside X=-7.5, but it did not prove a complete route into the room. Retained corner tile/trim constrains head clearance. Debris, the two held plates, native terrain queries, and actual post-impact positions need runtime verification. Unsupported decorative parts have not been silently removed or assigned invented connectors. This is a substantially wider ground-level damage target, not a completed whole-house traversal gate.

## Rebuild

From the repository root:

```sh
/snap/blender/current/blender --background --factory-startup \
  --python tools/adventure_assets/export_ldraw_assembly.py -- \
  --output /tmp/blacksmith-ground-rebuild \
  --asset-id ldraw-blacksmith-ground-r01 \
  --selection-source-ids data/adventure/ldraw-blacksmith-ground-r01/selection-source-ids.json
/snap/blender/current/blender --background --factory-startup \
  --python tools/adventure_assets/reduce_blacksmith_remainder.py -- \
  --input /tmp/blacksmith-ground-rebuild/wall-parts.glb \
  --output /tmp/blacksmith-ground-rebuild/wall-parts-optimized.glb
bazel-bin/tools/gltf_vmesh_tool --profile salvage-rigid-v1 \
  /tmp/blacksmith-ground-rebuild/wall-parts-optimized.glb \
  /tmp/blacksmith-ground-rebuild/wall-parts.vmesh
/snap/blender/current/blender --background --factory-startup \
  --python tools/adventure_assets/bake_ldraw_collision.py -- \
  --blend /tmp/blacksmith-ground-rebuild/remainder.blend \
  --output /tmp/blacksmith-ground-rebuild/remainder-collision.json \
  --header /tmp/blacksmith-ground-rebuild/remainder-collision.hpp \
  --wall-section /tmp/blacksmith-ground-rebuild/wall.json \
  --member-regions --maximum-boxes 6500
python3 tools/adventure_assets/validate_ldraw_assembly.py \
  data/adventure/ldraw-blacksmith-ground-r01
```

The original source asset must remain available as the sibling `ldraw-blacksmith-r01` package when running source/geometry validation. The manifest pins exact installed files. Export timestamps and Blender serialization can change binary hashes during rebuilds; validate source transforms/vertices as well as updating hashes intentionally.
