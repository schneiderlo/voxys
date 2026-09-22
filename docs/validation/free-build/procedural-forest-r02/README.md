# Procedural forest r02

The forest now uses six authored tree shapes with shared crown geometry across
three detail levels. Placement remains deterministic. Denser groves overlap
above walkable trunk gaps; young trees, glades, terrain exclusions and warped
ecology fields break up the forest boundary. Draw distance remains 2,000 units.
The original LDraw foliage files and attribution are bundled with the asset.

An in-game walk exposed a collision-budget issue: the imported village consumed
most of the previous 8,192-box limit. Nearby trees were deferred while distant
trees remained visible, producing an empty ring around the player. The shared
budget is now 16,384, and the forest collision neighbourhood is 160 units. Its
worst-case number of jittered tree cells fits below the 2,048-prop bound. Distant
trees keep rendering beyond that physical neighbourhood.

Validation:

- Native and WebAssembly builds completed.
- 44 focused tests passed: generation, deterministic reload/approach, canopy
  overlap, trunk spacing, construction exclusions, actor clearance, crowded
  collision capacity, village traversal, walking, bike movement and GPU rendering.
- The full-terrain free-build runtime placement/save/restore test passed.
- All six variants rendered on the actual GPU at a camera distance of 2,000.
  Pixel coverage across near/middle/horizon levels was respectively:
  oak 199/199/197; beech 218/218/217; young oak 103/100/99;
  birch 188/184/180; spruce 168/167/167; young fir 91/90/90.
  This diagnostic uses a narrow field of view to resolve individual trees; no
  model scaling is involved. The normal browser view was also inspected.
- Browser review covered the bay and distant hills, the forest around the
  village, a walk underneath the canopy, and mounting/riding/dismounting the bike.
  Nearby props rose from roughly 30–45 before the budget fix to 378–450 on that
  route. No browser warnings or rendering errors were reported.

The browser observation records the actual viewport, position, visible forest
counts and frame timing. It includes the detailed village and terrain workload;
it is not a standalone forest performance benchmark. The temporary camera toggle
was restored to the original hold-right-button setting after review.
