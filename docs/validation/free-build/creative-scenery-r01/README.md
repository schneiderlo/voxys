# Creative clearing scenery

Owner request: add LEGO-style props so free building feels less empty, and use
an independent agent to critique the result. Scope: the main bay starting area;
no adventure, NPCs, quests, rewards, terrain modification or save migration.

## Installed result

Six original Blender-authored meshes: a branched broadleaf tree, layered pine,
three-flower clump, pair of molded rocks, bench and crate. They share the terrain's
one-stud spacing and .30/.18 stud radius/height, bevels and warm plastic palette.
The 4.76-stud figure stands below the 10.09-stud broadleaf and 7.56-stud pine.

The actual 8192² map admits all **36 props**: five broadleaf trees, five pines,
18 flower clumps, six rock groups, one bench and one crate. Placement follows
existing shelves around `creativeStart={1200,-1120}`. The first design's
right-forward positions fell on the bay cliff, so those were moved to the
right-back plateau. The center and bay sightline stay open. This is local
scenery, not a fully populated world. Bench/crate are decorative, not furniture
with new interactions or inventory.

Sources and runtime package: `data/adventure/creative-props-r01`. The source
recipe is `tools/adventure_assets/author_creative_props.py`; strict cooker is
`tools/adventure_assets/cook_creative_props.py`. Provenance and manifest record
source hashes, bounds, finite unit normals, mesh order and storage budgets.
No external images, downloaded models or texture dependencies were used.

## Behavior contract

- `CreativeScenery` admits a bounded, deterministic layout on terrain cell tops.
  Flat bench/crate footprints require equal cell heights; natural props tolerate
  at most one plate of relief and shallow underside burial. Terrain studs enter
  the hidden underside, as with a mounted brick.
- The same accepted list supplies rendering and collision. Tree trunks, pine
  courses, bench legs/seat/back, crate and rock inner volumes block movement.
  Broadleaf canopy/branches and flowers are decorative; there are no giant
  invisible canopy boxes. Collision is an authored approximation, not mesh CCD.
- Full visible prop envelopes yield to saved or new player parts and the entire
  reserved door swing. Building while aiming at scenery anchors to terrain,
  never to a prop that will disappear. A removal attempt explains how to build
  there. The independent reviewer found the initial door validation conflict;
  filtering only tagged creative scenery fixes it while other obstacles remain
  authoritative.
- A suppressed prop stays absent until a fresh load, including after deleting
  the conflicting brick. Props cannot suddenly reappear through the player/bike.
  On reload, saved builds, door swings and the saved player's body take priority.
  Scenery is derived, not serialized as owned parts; existing identity/schema
  stay unchanged. Legacy identifier collisions defer the affected props.
- Shared mesh is about 1.6 MB cooked / 1.8 MB GPU, six meshes, eight materials,
  15 submesh draws for a complete kit. 36 instances add fewer than 108 draws,
  within the existing 6,144 draw budget. No per-frame placement/terrain scan.

## Verification and independent critique

- 25 world tests pass, including four new scenery tests: deterministic layout,
  all six kinds on the actual map, clear spawn/bike departure, trunk picking and
  collision, construction/door reservations, no respawn, saved-player priority,
  invalid terrain and legacy IDs.
- 15 door tests pass, including a regression proving creative props yield to a
  new door while other static obstacles still block it.
- Both runtime integration tests pass; construction-policy checks also pass.
- Browser/WASM build succeeds. Actual game screenshot is `in-game.jpg`; it
  captures a near-tree gameplay view before the final stem-only adjustment,
  not a reset or staged fresh-spawn camera. Final package/source hashes and the
  runtime tests were verified again after the stem adjustment.
- Independent `scenery_design_review` agent inspected the implementation and
  actual screenshot. Verdict: a solid local scenery pass; no visual blocker.
  They noted consistent stud scale, plastic material, grounded visible trunk,
  cream/coral flower accents and generous space. Their flower-stem readability
  feedback led to thicker dark-green stems in the final source.

Limits: the screenshot does not show the bench/crate or full tree silhouettes;
those are covered by asset geometry checks and terrain admission, not a claimed
complete in-game visual review. Wider world population, a physical controller
playthrough, measured frame-time acceptance, and owner approval remain open.
This does not complete F1/F2 or justify a gate commit/deployment.
