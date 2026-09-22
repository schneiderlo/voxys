# Forest horizon meshes

Third distance level of the existing creative scenery kit. Original editable
Blender/GLB source and the existing palette are retained. No purchased assets,
textures, stretched trees, camera-facing cards, or separate placement pattern.

| Tree | Close triangles | Middle triangles | Horizon triangles |
| --- | ---: | ---: | ---: |
| Broadleaf | 10,776 | 2,224 | 412 |
| Pine | 1,656 | 636 | 188 |

Horizon meshes retain crown clusters, trunk size and ground origin. Small studs,
bevels, trunk-course seams and hidden branches are removed. The broadleaf keeps
three crown rings. Canonical units and quarter-turn rotations match the other
levels. The package contains the same six mesh slots for cooker compatibility;
only the two tree slots are selected at horizon distance.

Rebuild into fresh directories with `author_creative_props.py --lod 2`, then
`cook_creative_props.py`. The runtime checks the cooked byte count and SHA-256;
refresh these together with the package. The manifests record source/tool hashes.

The Blender export completed and the strict cooker passed. Blender was stopped
after it stalled in audio shutdown; a clean Blender process exit is not claimed.
Both cooked tree meshes subsequently passed real GPU rendering at 2,000 metres.
