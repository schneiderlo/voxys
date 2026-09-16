# Hinged door add-on

Original warm construction-toy content for `BUILD-A02`: a molded terracotta leaf,
shallow panel rails and a small brass-looking handle on both sides. The frame is
the existing cream/moss open doorway. No downloaded assets, image generation,
textures or new character/animation pipeline are involved.

This package adds **Hinged door, piece 15 / furniture Door 4**, costing **6 wood
and 2 scrap**. IDs 1–14, their JSON, cooked meshes and browser pictures remain
byte-identical. An old open doorway or Starter room is not silently replaced;
the session owns paid construction and door interaction.

## Runtime contract

- Frame: unchanged `building-kit-r01/cooked/building-kit-lod0.vmesh`, mesh 3.
  Its three solids and 1.36 m × 2.24 m opening remain exact.
- Leaf: `cooked/door-leaf-lod0.vmesh`, mesh 0 / identity node
  `15_hinged_door_leaf`. One mesh, two submeshes/materials, no skin or clips.
  Append as a separate runtime asset; never index it as legacy mesh 14.
- Canonical **basis 0**, +Y up, −Z forward; metres, .02 m lattice. Blender source
  converts `(x,y,z)` to `(x,-z,y)` before the existing strict rigid GLB cook.
  Reuse the existing building pipeline's front-face convention.
- Vertices are in **closed part-local coordinates**, not hinge-local.
  Leaf collider: X `[-32,32]`, Y `[2,110]`, Z `[-2,2]` lattice ticks.
  Hinge: `(-32,2,0)` ticks. Render the leaf as
  `partMatrix * T(hinge) * Ry(open ? -90deg : 0deg) * T(-hinge)`.
- The open leaf collider is X `[-34,-30]`, Y `[2,110]`, Z `[0,64]` ticks.
  It swings toward local +Z. The session/geometry code owns authoritative
  open state, obstruction checks and collision publication. The movable leaf
  must not become structural support or leave a duplicate closed collider.
- Body size is **1.28 × 2.16 × .08 m**. Molded rails/handle extend at most
  **.019 m** beyond the front/back collider faces; this cosmetic relief is
  explicitly excluded from physical contact. Width/height do not grow. The
  cooked open model leaves a checked 1.25 m clear corridor through the frame.
- Keep authored material factors and neutral instance tint. Leaf colours are
  `adventure_door_terracotta` (`B85C45`, roughness .62, metal 0) and
  `adventure_door_brass` (`C69A49`, roughness .42, metal .25).

| LOD | File bytes | Vertices | Triangles | Leaf draws | Requested GPU bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 49,708 | 656 | 300 | 2 | 50,960 |
| 1 | 36,652 | 480 | 236 | 2 | 37,520 |

The reused frame adds two draws, so a complete door remains within the existing
four-draw-per-piece budget. These are asset counts, not a measured world frame
rate or a claim that runtime LOD selection is installed.

## Source, identities and checks

[`catalog.json`](catalog.json) is the canonical add-on definition.
`tools/adventure_assets/generate_catalog.py` validates it separately; its old
`validated_catalog()` callers still receive only the frozen fourteen records.
The new combined content fingerprint is SHA-256 of the byte concatenation
`"voxys-adventure-building-catalog-v2\0" + legacyJSON + "\0" + doorJSON`.
`legacyBuildingCatalogFingerprint()` retains the exact earlier digest for
save migration. Native/browser gameplay must migrate old saves explicitly.

- Legacy catalog: `fc9c763b22b5ef9d81e461608dea0f1b138d8f16a4940e9378442dc18a611150`.
- Combined catalog: `9059ecdef3c9530d419fdcffbe74ca0501472f73a83f8f901147f52122a50a98`.
- Leaf LOD 0: `be59bb6888aacc94c345fa3aebf0ebf1df1cacc1c3082e55c01eff16753b1e5f`.
- Leaf LOD 1: `e5a86f71d218e9ac58ee7d4f0a78cd5713dd442c32935d765299950b9a3f8e76`.
- Reused frame source: `08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b`.

[`cooked/manifest.json`](cooked/manifest.json) records exact binary/tool hashes.
[`cooked/provenance.json`](cooked/provenance.json) records Blender 5.2.1 LTS,
the editable source recipe and sources under `cooked/source/`.
[`cooked/geometry-check.json`](cooked/geometry-check.json) verifies finite
vertices/unit normals, positive winding/volume, all six leaf contact faces,
draw/byte limits, the exact hinge transform and actual triangle exclusion from
the open corridor. This proves asset geometry, not runtime opening/closing,
collision sweeps, placement, save migration or player acceptance.

The actual frame and closed leaf triangles produce `web/adventure_piece_15.svg`
and the matching native triangle table through `generate_piece_thumbnails.py`.
The new picture adds 336 triangles after the untouched 3,350-triangle legacy
prefix; maximum per-picture count remains 812. Companion source SHA values are
in `web/adventure_piece_manifest.json` and the generated native header.

## Reproduce without touching the installed package

Use new directories. Authoring and cooking refuse existing outputs.

```sh
/snap/blender/current/blender --background --factory-startup -noaudio --threads 2 \
  --python-exit-code 1 --python tools/adventure_assets/author_door.py -- \
  --output-dir /tmp/my-door-source
python3 tools/adventure_assets/cook_door.py --source-dir /tmp/my-door-source \
  --output-dir /tmp/my-door-cooked --tool build-native-save-host/bin/gltf_vmesh_tool
python3 tools/adventure_assets/check_door.py /tmp/my-door-cooked
python3 tools/adventure_assets/generate_catalog.py --check
python3 tools/adventure_assets/generate_piece_thumbnails.py --check
```

The initial authoring run finished both source exports and provenance, then
Blender's audio shutdown stalled in this host sandbox. That owned process was
interrupted after export completion. Both strict cooks exited successfully;
all installed geometry checks passed. `-noaudio` is shown above to avoid that
unrelated shutdown path. No game/browser session or screenshot was used here.
