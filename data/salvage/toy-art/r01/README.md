# Cove structural presentation r01

Original background-Blender models for the installed pontoon, beam and deck/dock plate. These are presentation revisions, selected by `data/salvage/cove-workshop-r03.json`; canonical gameplay stays in the original bundles. See plan decision D36 and `docs/validation/salvage/LOOK-01/toy-art-r01/README.md` for runtime evidence and remaining art work.

Cream molded panels, teal cores/side moldings and orange end accents replace the earlier structural surfaces. Actual plugs, wells and the hull taper retain their original positions. Decorative seams and side bosses are not new construction sockets. The individual studded bricks remain the same paid parts.

All materials use solid linear PBR factors, with no textures, UV charts or tangent maps. Plastic roughness is 0.32. Geometry normals retain planar faces and rounded edges. These assets do not establish a texel-density or final material-quality gate.

Author: original procedural work in this repository; no external models, image generation, downloaded textures, logos or Blender MCP. Authoring used Blender 5.2.1 LTS. Each part includes editable `.blend`, exported GLBs, source metadata, provenance and the strictly cooked runtime package.

| Part | Near vertices / triangles | Middle | Far | Draws per LOD |
|---|---:|---:|---:|---:|
| pontoon | 2665 / 5188 | 830 / 1284 | 722 / 528 | 3 |
| beam | 2554 / 4334 | 1188 / 1912 | 604 / 336 | 3 |
| plate | 2782 / 4770 | 1282 / 2086 | 702 / 386 | 3 |

Run from the repository root, using a new output directory:

```bash
blender --background --factory-startup --python-exit-code 1 --python tools/salvage_assets/author_cove_toy_art.py -- --output-dir <new-directory>
```

Then cook each part using `tools/salvage_assets/cook_gameplay_asset.py`, the `salvage-rigid-v1` converter and strict sidecar validator. The exact executed commands and tool hashes are in the evidence and cook manifests. Exporter/Blender serialization is not asserted bit-identical across versions; the committed runtime manifests select the actual bytes. Do not overwrite a published visual ID with different bytes.

Only rendering uses these bundles. A replacement must match the exact canonical source manifest and all non-LOD metadata, keep LOD IDs/basis/thresholds, use fresh visual IDs, and stay inside the original union AABB plus 2 cm. The AABB limit is a coarse outer bound, not a proof of surface-to-collision distance. Changing gameplay requires a new content version and migration.
