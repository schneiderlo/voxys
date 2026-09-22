# Village reference gap review — 2026-09-17

## Status and scope

**The current village does not meet the owner's visual target.** It is a
functional first pass with tested entrances and movement. Passing those checks
and finding no collision blockers did not establish an acceptable visual match.
The owner explicitly rejected the visual gap on 2026-09-17. Do not describe the
remaining work as minor polish or mark visual acceptance complete.

Compare `owner-reference.png` in this directory with
`../../../validation/free-build/village-r01/in-game-r04.jpg`. The owner repeated
the same village target in `/tmp/codex-clipboard-0fe02e1b-47c7-470d-99b1-49894db58733.png`.
Use checked-in images for future work; the temporary attachment may disappear.
The later Shift-running change did not change this scenery.

Keep the current minifigure, motorcycle, warm low-angle light, fixed terrain,
free construction and Shift running. Ignore the obsolete figure in the target.
Do not add quests, enemies, NPC systems or progression to satisfy an art request.

## Source-confirmed causes

- `tools/adventure_assets/author_creative_village.py` calls the same `cottage`
  function twice, changing only roof colour. There are two mesh entries but
  only one cottage shape. Front, side and rear details remain shallow boxes.
- `src/game/adventure/creative_village.cpp` repeats those shapes at four shared
  X coordinates in two parallel rows, with only 0/180-degree orientations.
  The tower and mill sit behind the nearer roofline from the starting approach.
- Only four 10x6 garden beds are authored. There are no market awnings, yard
  groups or broad crop plots establishing a lively village centre.
- The natural scenery kit repeats a tall exposed-trunk broadleaf silhouette
  with sparse flat canopy plates. Increasing its count repeats the same defect.
- The screenshot has weak building surface depth compared with the target.
  This is an observed result; shader, shadow-distance and material causes need
  investigation. It is not evidence that roughness alone is wrong or that the
  warm sun must be replaced by noon light.

## Required work, in impact order

### 1. Establish architectural quality before duplicating more buildings

- [ ] Rebuild one representative cottage to the reference's visual standard:
  roof overhang and thickness, legible molded roof courses, substantial beams,
  recessed window surrounds, shutters, doorway depth, masonry base and chimney.
- [ ] Review it at normal gameplay distance and close range in the actual game.
  Roof, wall and window depth must read without enlarged diagnostic framing.
- [ ] Derive at least four visibly different silhouettes: a low cottage,
  taller gabled house, porch/dormer house and a workshop/market building. Different roof colour
  alone does not count. Vary height, footprint, porch/dormer and roof arrangement.
- [ ] Keep doors compatible with the 4.76-stud figure; regenerate collision
  metadata and runtime hashes with the authored models. Preserve movement tests.

### 2. Compose a village rather than two rows of repeated houses

- [ ] Arrange small irregular clusters around a recognisable shared square,
  with secondary lanes, courtyards and open space for player construction.
- [ ] Put the tower and windmill in readable skyline gaps from the approach.
  The tower body as well as its roof must read; add deeper openings and an
  articulated entrance or lower annex. The mill roof and four sails must be visible.
- [ ] Establish a wide village overview comparable to the target, alongside an
  ordinary player-height approach. Account for camera distance/FOV before
  judging scale. Do not shrink houses below usable size or change the current
  character to imitate the old reference figure.
- [ ] Preserve the bay overlook and walk/ride access; any new viewpoint is an
  inspection position, not permission to move existing saved players.

### 3. Add the missing settled landscape

- [ ] Create substantial fenced crop plots with dense rows and clear borders,
  plus smaller cottage gardens, hedges, flowers and low stone walls.
- [ ] Add a small market/awning group and purposeful yard details such as
  barrels, stacked supplies and benches. Populate coherent locations rather
  than spreading isolated random objects across the grass.
- [ ] Redesign broadleaf canopy mass and add shrub/conifer variation. Use
  planted clusters, hedgerows and sparse clearings rather than repeated poles.
- [ ] Give paths a warm, clearly bounded main route and narrower branches.
  Retain terrace-aware grounding and a continuous accessible bike route.

### 4. Restore readable volume and prove the actual result

- [ ] Diagnose roof/eave/window/contact shadows in the game at village distance.
  Preserve readable shaded colours and the warm low sun. Avoid a global gloss
  increase or time-of-day change as a substitute for depth.
- [ ] Separate stone, plaster, timber and roof materials through restrained
  colour/roughness and real shape. LEGO seams/studs must read at useful distances.
- [ ] Recheck real character entrances, running, bike traversal, construction
  precedence and matching rendered/collision bounds after layout changes.
- [ ] Measure displayed village-plus-player-build frame times. Share geometry,
  use instancing/LOD where justified and record actual draw/triangle budgets.
- [ ] Request a fresh independent comparison against the supplied target.
  Report remaining differences plainly. Owner visual acceptance stays open.

## Review method

Use three purposeful views: a comparable village overview, the normal approach,
and one close cottage. Reuse them after substantial changes. Existing screenshots
already prove the current mismatch; do not repeatedly capture an unchanged scene.
Functional regression checks and visual reference acceptance are separate gates.

## Fresh independent agent verdict

The reviewer inspected both images directly and called r04 a **functional
blockout, well short of the visual target**. Ranked gaps: (1) architecture,
(2) village composition, (3) signs of habitation, (4) vegetation, (5) paths,
(6) landmark character, (7) materials/depth and (8) comparable presentation.

The low, close actual view hides layout detail compared with the reference's
ridge overview. That explains some apparent scale/composition differences, but
cannot explain away repeated plain buildings and missing planted/domestic detail.

Priorities are the richer modular building set, recomposition around a commons
and substantial gardens, then fuller vegetation and purposeful house frontages.
Acceptance must establish a cohesive inhabited-looking LEGO village in overview
and ordinary play. Merely counting eight houses or passing collision tests is
insufficient. The current figurine is explicitly outside this critique.
