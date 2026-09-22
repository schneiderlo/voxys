# Actual LEGO set assembly: 21325 Medieval Blacksmith

Community-authored LDraw reconstruction of LEGO set 21325 by Vincent Messenet
[Cheenzo]. This is an imported assembly of identifiable LEGO parts, not an
original procedural approximation. It is not a corporate LEGO-provided model.

- `blacksmith.vmesh`: game-ready static model, 851,880 triangles, 44 materials,
  one node/mesh. 81,984,851 bytes. The manifest pins its exact hash.
- `blacksmith-assembly.blend`: editable full assembly with individual parts.
- `preview.png`: neutral studio render of the runtime geometry, not a game capture.
- `source/`: original MPD, exact dependency closure, license texts and attribution.
- `provenance.json`: import settings, source hash, colours, dimensions and changes.

Scale is **one world unit per real stud** (20 LDU), with no proportional distortion.
Canonical Y is up; the set faces +Z. Bounds are about 40.5 × 34.8 × 38.8 studs.
The original model omits minifigures and makes the accessory/printed-part
substitutions documented in `source/ATTRIBUTION.md`.

## Rebuild

Requires Blender and the external GPL ImportLDraw tool, pinned at commit
`c306fb777a4e0da85492f09d65daf458767a0aa1` from
https://github.com/TobyLobster/ImportLDraw. No importer code is bundled here.

From the repository root, using a fresh output directory:

```sh
python3 data/adventure/ldraw-blacksmith-r01/source/validate.py
blender --background --factory-startup --python-exit-code 1 \
  --python tools/adventure_assets/import_ldraw_blacksmith.py -- \
  --source data/adventure/ldraw-blacksmith-r01/source \
  --importer-root /path/to/ImportLDraw \
  --output-dir /tmp/blacksmith-rebuild --proof
bazel-bin/tools/gltf_vmesh_tool --profile salvage-rigid-v1 \
  /tmp/blacksmith-rebuild/blacksmith.glb /tmp/blacksmith-rebuild/blacksmith.vmesh
```

Retain the source package and credits with derived/distributed geometry. Changes
must update the manifest, provenance and runtime size/hash pins together.

## Current integration limits

The first placement is a static landmark on the level village-edge plot at
(1208, -1032), rotated 180 degrees. It preserves the original set's size and
colours. Collision is baked from the actual mesh surfaces, including the base, individual
stair treads, walls, floors and roof. Empty courtyard and room space remain open.
The bake uses 4,054 merged boxes at 0.4-stud horizontal and 0.2-stud vertical
resolution; sub-stud seams too small for the character are closed before merging.
This is a bounded geometric approximation, not exact triangle collision.
Saved player construction takes precedence; conflicting saves suppress the set.

The runtime derivative reduces triangle count by 18%, preserving the complete
editable source separately. Transparent source parts retain their colour but
are opaque because the bounded static mesh profile requires opacity. Rubber and
metal classes retain distinct roughness/metal values. No physical refraction,
added stud logos, removable floors or interactive source-model doors are claimed.

Only one set is installed; it is culled beyond 220 studs. This is a first real
CAD asset, not completion of the village conversion or a measured performance
budget. The ~78 MiB uncompressed mesh is substantial. The browser fixed heap is 768 MiB
(previously 512 MiB) to cover the decoded set and temporary upload copies. A reusable part-instancing
pipeline and reviewed distance LODs are required before adding many such sets.

## Collision rebuild

After the importer creates `blacksmith-runtime.blend`:

```sh
blender --background --factory-startup --python-exit-code 1 \
  --python tools/adventure_assets/bake_ldraw_collision.py -- \
  --blend /tmp/blacksmith-rebuild/blacksmith-runtime.blend \
  --output data/adventure/ldraw-blacksmith-r01/collision.json \
  --header src/game/adventure/ldraw_blacksmith_geometry.hpp
```

The source mesh is sampled from all three axes; the result includes vertical
walls and ceilings as well as walkable tops. The full imported room is never
replaced with a solid house-sized box. Source/recipe hashes and spacing are
recorded in `collision.json`; regenerate the compiled header together with it.
