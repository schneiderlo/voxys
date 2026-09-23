# Forest performance recovery

Status: implemented. The deterministic recipe 3 forest, its 2 km draw range, six tree variants, and the village meadow remain in place.

## Problem and result

The enhanced forest increased the buffered distant tree count from about 15,000 to about 52,000. Every frame then scanned and prepared the full list. The renderer rebuilt and uploaded material records for visible trees and replayed too many nearby trees in both shadow regions. Travel also regenerated the 2 km source whenever the player crossed a 128-unit cell.

At the same 1920 × 1080 spawn view during the implementation check, the reported frame rate rose from about 49 to about 76 FPS. CPU frame work fell from about 16 to 3.4 ms; the GPU frame interval fell from roughly 16–18 to 11.6 ms. This was one view on one host, not a forest-only GPU timing or a worst-frame guarantee. The raw local browser capture was temporary and is no longer available; these rounded values come from the interactive verification, so repeat the benchmark before using them as a formal acceptance result. The older recipe comparison is preserved in [historical-summary.json](../../validation/free-build/forest-performance-audit-r01/historical-summary.json).

The next pass adds a fourth detail level beyond 700–850 units. It retains the
six tree heights and crown outlines at 88–160 triangles each. In the checked
spawn view, colour triangles fell from about 4.18 million to 1.39 million.
The 2 km GPU silhouette test found nearly the same pixel coverage for each
variant. Coarse far-shadow casters for the village and blacksmith then reduced
the total mesh shadow triangles from about 2.7 million to 0.64 million, while
their full colour meshes and near shadows remain. The forest still has 51,836
distant trees and the same clearing. A clean, uncapped browser sample with a
second build tab active reached roughly 115–130 FPS at 1920 × 1080 and up to
about 170 FPS at 1280 × 720. The shared GPU load and variable browser pacing
make these directional measurements. A sustained 200 FPS result is not verified.

## Changes

- The forest is grouped into 64-unit render tiles. Tiles outside the draw distance or camera view are rejected before visiting their trees.
- Distant tree transforms and GPU material records are retained. Camera movement updates a compact list of visible instance indices; an unchanged selection needs no instance upload.
- Nearby detailed trees still change detail level with distance and cast sun shadows. Shadow submissions are culled separately against the near and far light regions, retaining relevant offscreen casters.
- The opaque sort key groups equal shadow eligibility so compatible draws remain batched.
- The procedural source is cached in immutable 128-unit generation tiles. Moving into the next region creates only entering tiles; collision-only refreshes reuse the same source and tree identity. Terrain, construction admission, and camera-origin changes still invalidate the appropriate caches.
- Forest telemetry now reports visited and visible trees, selection time, instance upload bytes, and color/shadow triangle totals.
- Distant forest trees switch to an 88–160-triangle mesh after 700–850 units. The
  switch varies by stable tree ID, so a grove does not change together.
- The village and blacksmith use collision-derived proxy geometry in the far
  shadow region. The colour pass and near shadow region retain the original
  meshes.

The scene retained the same recipe 3 tree counts in checked views: 51,836 distant trees at spawn; 40,821 and 26,216 at two other terrain positions. Tests compare streamed and cold generation for IDs, placement, bounds, and variants, and compare retained rendering against live draws.

## Verification and limits

The implementation check passed 47 focused native forest, scene, and renderer tests plus one free-build runtime integration test. The WASM target linked in that check. A fresh focused rerun is recorded in [the validation note](../../validation/free-build/forest-performance-r04/README.md). The new distant silhouette, shadow-only drawing, and free-build startup checks passed after the fourth forest level and proxy geometry were added; the browser target rebuilt successfully.

The 2 ms CPU and 3 ms GPU forest-only targets from the initial plan are not established: current timers cover different scopes. Elevated 2 km views, long bike runs, p95/p99 frame times, and multiple hardware profiles still need a controlled benchmark. Keep the present forest density and meadow until those measurements show a reason to change them.
