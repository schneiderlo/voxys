# Frontier building kit r01

Canonical source for additive piece IDs 16–24. Regenerate the compiled catalogue
with `python3 tools/adventure_assets/generate_catalog.py`; use `--check` to verify
it. IDs 1–15 and their two historical fingerprints remain frozen. Frontier
content includes `frontierBuildingCatalogFingerprint()` in its separate identity.

The kit fits the current 4.76-stud figure with a 1.12-stud collision radius.
Foundations, decks and roofs cover 6 × 6 studs. Walls are 5.76 studs tall; the
open doorway has 4 × 5.44 studs of clear space. A stair module covers 6 × 6 studs
and rises 2.88 studs. Furniture uses 2.5 times the original mesh scale, with its
collision boxes rounded to the nearest .02-stud lattice tick.

Modules compose the existing original `building-kit-r01` meshes. Floor, roof,
foundation and stair surfaces repeat unscaled meshes so their studs retain the
terrain's size. The doorway composes narrow wall columns and beam headers.
`buildingVisuals()` is the shared render transform list and `buildingIconKind()`
selects the corresponding existing thumbnail. These modules require the
Frontier profile; legacy and unrestricted workshop saves cannot acquire them.

`tests/test_frontier_building.cpp` checks preserved legacy identities, module
bounds, doorway collision at every rotation, stair visual/collision agreement
and walking the staircase with the actual figure controller.
