# Forest family r02

Six original tree silhouettes: oak, beech, young oak, birch, spruce and young fir.
Dimensions are authored in studs, with one game unit per stud. Trees are rotated
but never randomly scaled, so their LEGO details keep the correct size.

The broadleaf trees use James Jessiman's LDraw 2417 foliage for their nearest
detail level. Original downloaded parts, dependency hashes and attribution are
in [source/ATTRIBUTION.md](source/ATTRIBUTION.md) and
[source/ldraw-manifest.json](source/ldraw-manifest.json).

All three detail levels share the same crown volumes. Near trees retain molded
leaf details, studs and bevels. Middle trees retain branches. Horizon trees use
264–464 triangles each. The runtime staggers the detail changes between trees
and keeps them visible up to 2,000 game units away.

Generation uses fixed integer noise and a fixed seed. Warped woodland fields,
small groves, glades and young trees at sparse edges produce coherent stands.
Water, cliffs, the high treeline and construction footprints exclude trees.
Only nearby trunks join movement collision; crowns can overlap above the player.

Placement recipe 3 reserves a fixed meadow around the entire village, including
the blacksmith and cannon. Procedural trees begin at least 60 units beyond its
outer plots. A gently irregular 90-unit transition fills in gradually, reaching
normal forest density about 150–168 units from the village edge. Authored village
trees and the small orchard remain. This clearing never follows the player.

Rebuild from the repository root:

```sh
blender --background --factory-startup --python tools/adventure_assets/author_forest.py -- --output data/adventure/forest-r02
python3 tools/adventure_assets/cook_forest.py --package data/adventure/forest-r02 --tool bazel-bin/tools/gltf_vmesh_tool --header src/game/adventure/forest_geometry.hpp
```

The cooker validates all six mesh identities, transforms, normals, indices,
bounds, material counts and triangle budgets. Update the three pinned hashes in
`installedForestMesh` after intentionally changing/rebuilding the source.
