# Procedural forest to 2 km

Free Build now draws the existing broadleaf and pine assets up to 2,000 world
units from the camera horizontally. Terrain units are metres in the engine;
the toy presentation also calls them studs. Mountains still occlude trees.

## Forest structure

- Smooth 384-unit woodland regions, 72-unit groves and 128-unit clearings.
- Pine-dominant and broadleaf-dominant patches, with mixed edges.
- Jittered 12-unit cells leave at least four units between tree centres.
  Crowns may overlap; the grid is a placement accelerator, not an orchard layout.
- Roots follow the actual stepped terrain. Water, cliffs and high elevations
  reject trees; the upper treeline thins gradually.
- Flowers concentrate at woodland edges. Rocks use a separate sparse layer.
- The starting clearing, village, orchard, doors and player builds remain reserved.

## Reproducibility and streaming

Recipe 1 uses seed `0x937a25`. Integer coordinate hashing and fixed-point smooth
noise determine density, species, position and rotation. Loading order does not
advance a random-number stream. The same tree keeps its identity at every level
of detail. Terrain and installed recipe must remain the same for reproduction.

The raw forest is cached for each 128-unit region, with a 2,144-unit radius to
cover the full view plus region/orbit movement. The nearby list updates on the
existing 32-unit boundary. Immutable cached candidates are shared between those
updates; construction exclusions are applied to both near and distant trees.

Only the 224-unit neighbourhood needs tree collision. Distant trees use no
physics bodies or collision slots. Actual landmark, village and owned geometry
retain priority within the existing 8,192-solid budget. Near props are capped at
2,048. As before, displacement stays suppressed for the live session; saved
construction suppresses it again on reload. Removing trees permanently is not
new gameplay or a new save-file feature.

## Rendering

The original detailed models switch to the existing middle level around 70–90
units, then to the new horizon meshes around 260–340. Each tree's stable identity
selects its switch point so a whole grove does not change at once. These are
hard switches, not blended LOD transitions. Tree scale is unchanged.

Horizon broadleaf: 412 triangles. Horizon pine: 188 triangles. Matching materials
batch into instanced draws. Distant objects are rejected against the camera
frustum before instance expansion; nearby offscreen shadow casters are retained.
Sun-shadow work stays within 280 units. Existing lighting and atmospheric fog
apply to all levels. The material-record buffer starts at 4 MiB and grows on demand.

## Real-terrain observations

These are buffered counts, not simultaneous visible or GPU-submitted counts.

| Player X/Z | Near props | Distant trees | Trees at 1.8–2 km | Collision solids* |
| --- | ---: | ---: | ---: | ---: |
| 1200 / -1120 | 178 | 14,900 | 2,716 | 1,667 |
| 800 / -800 | 455 | 11,441 | 1,976 | 3,269 |
| -63 / -895 | 237 | 7,254 | 1,063 | 2,575 |

*Scenery-only probe including the village. Runtime also reserves the imported
Blacksmith and cannon before admitting tree collision.

Browser inspection: distant wooded hills are visible from the starting bay;
the inland view retains the village, near trees and distant forest. No browser
warning/error logs were reported during inspection. Final checks are recorded
in the accompanying test logs. Frame rate depends on viewpoint, resolution,
other scene content and hardware; this change does not establish a 60 FPS budget.

## Completed checks

- Desktop Release application and browser WebAssembly builds succeeded.
- 62 selected forest, movement, village, door and mesh-renderer checks passed.
- The Free Build runtime startup / construction / exact save-restore check passed.
- Both actual horizon models rendered on the GPU at exactly 2,000 metres:
  broadleaf 138 covered pixels; pine 66, using a narrow-FOV 64×64 test target.
- Existing batching and offscreen-shadow tests passed. The added culling test
  preserves offscreen casters while rejecting distant non-casters.
- Browser runtime reported 14,900 distant trees, a 2,000-unit draw range,
  and 29 encoded horizon material batches in the starting view.

See `regression-tests.txt`, `runtime-tests.txt`, and `browser-observation.json`.
The browser observation is a whole-game sample on an AMD RDNA 3 integrated GPU,
not a forest-only benchmark or a controlled before/after comparison.
