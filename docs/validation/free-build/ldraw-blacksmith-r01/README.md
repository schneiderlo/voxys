# Actual LEGO model milestone — 2026-09-17

Owner requested actual online LEGO files after rejecting procedural approximations.

## Completed evidence

- Source: [LDraw OMR 21325](https://library.ldraw.org/omr/sets/1289), Vincent
  Messenet [Cheenzo], explicit CC BY 2.0 model license.
- Offline source audit: 859 resolved definitions, 742 external files, no missing
  dependency. Original bytes, 55 authors and mixed CC BY 2.0/4.0 licenses retained.
- Blender import: 2,140 mesh objects (includes importer subobjects); source audit
  counts 2,128 expanded parts. These are different counts, not missing parts.
- Runtime cooker: rigid profile passes, 996,598 vertices, 851,880 triangles,
  44 material draws, one mesh/node. No relaxed cooker safety limit.
- Independent visual critique in `review.md`: recognisable actual part geometry
  retained after reduction; corrected excessively bright proof lighting.
- Parent inspected the original OMR image in-browser on 2026-09-17. The roof
  assembly has seams at the dormer and projections beyond the gable trim in the
  source view too. They were not introduced by triangle reduction. No parts were
  invented to conceal these source features. Source image:
  https://library.ldraw.org/media/omr_models/1595/21325-1.png?v=1758945027

## Runtime verification

- Browser build passes; game starts (`in-game.png` records the bay-facing startup frame).
  The parent also turned toward the village and observed the actual set there;
  that earlier frame was not persisted. Independent runtime visual approval is open.
- Initial 512 MiB fixed browser heap exhausted during CAD asset startup. Raising
  the explicit fixed budget to 768 MiB fixes startup; memory growth stays disabled.
  No claim of low-memory/mobile suitability is made.
- `FreeBuildRuntimeIntegration.CreativeStartupPlacementAndExactRestore` passes
  on the actual 8192 terrain with the final asset: correct plot, saved-player
  overlap suppresses the landmark without moving the player, clear reload restores it.
- Standard native build passes. Optimized native build has an existing unrelated
  null-dereference warning in `adventure_encounters.cpp:186`; it is not claimed passing.
- Initial runtime test caught installed scenery namespace ordering; corrected by
  reserving the model footprint first and appending accepted collisions after the
  existing scenery alias guard. Final focused test passes.
- Browser view confirmed the real set at the village edge; the old cottages are
  visibly smaller and remain a separate replacement task. Normal hold-right-button
  camera preference restored after inspection. No saved construction was changed.

## Remaining checks

Close walking/contact inspection and measured displayed-frame performance are open.
The asset is a static landmark with geometry-derived physical surfaces. One imported set does not replace the
whole village; translucency and further performance optimization remain open.

## Owner-reported collision correction

The nine hand-placed bounding volumes were insufficient: bases, roof surfaces
and individual stair treads were missing, while whole rooms were solid. Replaced
with 4,054 merged physical surface boxes baked from the actual imported model.
The sampling grid is 0.4 × 0.2 × 0.4 studs; tiny seams are closed, not entire rooms.
All three axes are sampled to include vertical walls and ceilings as well as
floors. The bake rejects multiple meshes or unapplied transforms.

The character's creative-mode step allowance is 1.26 studs, covering authentic
1.2-stud brick steps. Ordinary adventure-scale movement remains unchanged.
Spatial lookup cells are now 16 studs to avoid scanning the whole model on every
small movement/camera query. Packet capacity remains 8,192 solids.

Independent code review checked coordinate conversion, placement rotation, empty
room preservation, collision-count reservation and source-transform validation.
Final targeted results: 24 spatial/player/motorbike cases and the full free-build
runtime integration pass. The integration advances the full-size player on the
actual baked Blacksmith staircase at walk and run speed, climbing at least three
real risers while checking grounded mode, capsule clearance and support each tick.
It also verifies clear courtyard/room air, room floor, masonry sweep, five stair
top heights, roof ray/camera collision and saved-player precedence. Browser build
with the geometry-derived collision and creative step change passes.
Logs copied beside this report: `movement-tests.log` and `runtime-tests.log`.

Commit hook did not pass: its broad existing fixture/accounting tests failed
before this collision patch (SurfaceAndRopeChargeBothFullDrawPathsWithinUnchangedOwnerCaps,
CoveDockMarkings.AdmissionRecountsExactStorageAndRejectsUnlitOrNonIdentityMesh,
and several FixtureGPU capacity cases). The remaining broad run was stopped
after these failures; no commit was created or hook bypassed. Targeted collision
checks run separately.
