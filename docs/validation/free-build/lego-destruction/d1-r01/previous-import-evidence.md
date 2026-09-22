# Cannon import and destruction design — 2026-09-17

Completed this turn: source-backed cannon asset and an implementation plan.
No cannon placement, firing, destruction, controls or GPU solver behaviour were
implemented or claimed. No in-game screenshot acceptance applies to this stage.

## Evidence

- `python3 data/adventure/ldraw-cannon-r01/source/validate.py`: pass, one embedded
  wrapper and 27 external dependencies, zero missing, exact pinned source bytes.
- Blender 5.2.2 LTS background import with pinned ImportLDraw
  `c306fb777a4e0da85492f09d65daf458767a0aa1`: pass, two actual source part meshes.
  All 2,368 triangles retained; two runtime materials.
- Existing `gltf_vmesh_tool --profile salvage-rigid-v1`: pass, 182,822-byte asset,
  2,333 vertices, 7,104 indices, two submeshes/materials and one node/mesh.
- Root inspected the actual generated studio preview. Independent reviewer
  `village_capture_review` inspected that same image and source geometry:
  bore, muzzle rim, rear knob, pivots, ornament and base present; scale coherent.
  Source -Z muzzle transforms to game +Z, elevated 15 degrees. No concrete
  import defect found. Authored standard-resolution faceting remains visible.
- Independent architecture investigator `village_depth_review` traced GPU
  shape admission, CCD, events, fracture, direct rendering and Free Build
  collision. Identified missing authored-body CCD, static-to-dynamic impulse
  transfer, imported-part identity/connectivity, capacities and query ownership.
  These are explicitly open in the implementation plan.
- Independent final plan critique incorporated: CPU pre-authorizes release
  candidates and GPU selects them before response; no-capacity fallback stays
  intact; reawakened rubble invalidates walking support; CMake/Bazel fixed-heap
  differences are explicit; conservation tests include anchor/world impulses.

The importer prints unsupported FABRIC finish messages while reading the full
source colour table. Neither of the two used cannon materials is FABRIC; both
are opaque BASIC plastic. This does not indicate missing cannon geometry.

Canonical asset and rebuild instructions:
`data/adventure/ldraw-cannon-r01/README.md`.
Design and remaining work:
`docs/design/free-build/lego-destruction/IMPLEMENTATION_TODO.md`.

No runtime code changed for this asset/design task. A complete feature gate and
commit have not passed. The preceding collision turn reported unrelated normal
hook failures in fixture/accounting tests; those are not waived by this import.
