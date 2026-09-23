# Distant building shadows

The village and blacksmith keep their full colour models. Their nearby shadow
maps also keep the detailed meshes. The larger, lower-resolution shadow map
uses these coarse casters made from the same collision solids as the buildings.
That avoids sending millions of hidden studs into the distant shadow pass.

Meshes 0–14 match the installed creative village mesh indices. Mesh 15 is the
static blacksmith assembly. The source assets and their attribution remain in
`creative-village-r01` and `ldraw-blacksmith-r01`; the provenance file pins the
exact source and collision hashes. Dynamic blacksmith wall pieces still use
their original meshes for both shadow ranges.

Rebuild from the repository root:

```sh
blender --background --factory-startup --python tools/adventure_assets/author_shadow_proxies.py -- --output data/adventure/shadow-proxies-r01/source --village data/adventure/creative-village-r01 --blacksmith data/adventure/ldraw-blacksmith-r01
python3 tools/adventure_assets/cook_shadow_proxies.py --package data/adventure/shadow-proxies-r01 --tool bazel-bin/tools/gltf_vmesh_tool
```

The cooked mesh has 50,676 triangles across 16 shapes. It is used for shadows
only and never replaces the colour geometry or building collision.
