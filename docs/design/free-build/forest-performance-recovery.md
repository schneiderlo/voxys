# Forest performance recovery

Status: strategy based on source inspection and saved browser telemetry. No performance fix has been applied by this audit.

Keep the 2 km forest, six tree species/ages, deterministic placement, the village meadow and nearby trunk collisions. Change how that forest is processed and drawn.

## Evidence

Historical samples at the same spawn position, camera yaw and 1920 × 1080 resolution:

| Sample median | Original forest, recipe 1 | Enhanced forest, recipe 2 | Village meadow, recipe 3 |
| --- | ---: | ---: | ---: |
| Buffered distant trees | 14,900 | 52,071 | 51,836 |
| CPU frame work | 3.7 ms | 14.6 ms | 16.3 ms |
| GPU frame interval | 11.6 ms | 18.6 ms | 19.2 ms |
| Reported FPS | 72.3 | 46.9 | 15.5 |
| Samples | 133 | 60 | 60 |

These are historical observations from different sessions, not a controlled A/B experiment. The meadow reduced tree count slightly; it did not introduce the threefold increase. The especially low recipe 3 FPS is not explained by CPU/GPU timings alone. Frame admission, scheduling and other GPU activity must be measured before assigning the entire regression to trees. The existing composite GPU timer does not isolate forest color or its shadows.

Summary and source hashes: [historical-summary.json](../../validation/free-build/forest-performance-audit-r01/historical-summary.json).

Confirmed work in the current code:

- `AdventureRuntime::render` scans the entire distant tree list every frame. Visible trees get fresh double-precision transforms and another bounds test in `MeshPath`.
- `MeshPath::render` expands each tree into material records, allocates temporary vectors, sorts, copies and uploads them every frame. Instanced drawing already exists; adding instancing alone will not fix this.
- The merge key includes shadow eligibility, but the opaque sort key does not. Mixed eligibility can split otherwise compatible batches.
- Trees within 280 units bypass camera rejection to preserve shadows. They enter the color pass too. Both shadow regions replay every eligible caster without testing each region's bounds.
- The far models still have 264–464 triangles per tree; detailed broadleaf models have about 16,000. Both tiers become expensive at the new density.
- Crossing a 128-unit boundary regenerates a 2,144-unit-radius forest. Crossing 32 units reruns admission and geometry preparation. This is a separate travel-stutter risk.

## Implementation order

1. **Establish a fair baseline.** Run one foreground game at fixed resolution, camera, lighting and physics state. Compare forest enabled/disabled, forest shadows disabled, and forced distance detail tiers. Record CPU selection/packing/upload time, separate GPU forest color/shadow time, triangles, uploaded bytes and completed presentations. Use 30 seconds after warm-up, three runs, and report frame median, p95 and p99. Also record deferred frames and queue depth. Recreate the earlier forest baseline where possible; do not compare FPS from unrelated sessions.

2. **Remove repeated CPU work first.** Store trees in persistent 64–128-unit tiles with conservative bounds. Reject tiles before visiting trees. Cache transforms and opaque material groups; keep static instance data on the GPU. Update changed tiles and compact visibility lists, rather than recreating every material record. Use tile-local coordinates with a separate world-origin offset. Start with CPU tile culling; add GPU culling only if measurements justify it. Include shadow eligibility in opaque batching while preserving transparent draw order.

3. **Give shadows their own visible lists.** Cull color against the camera and casters against each light region independently. Keep offscreen trees that can cast into visible space. Use simpler tree geometry for shadows where the projected error permits it. Verify moving sun, village roofs and ground contact before considering shadow caching.

4. **Make distant woodland much cheaper.** Select detail by projected screen size, with hysteresis. Starting tuning bands: detailed trees within about 40 units, simplified trees to 160, very cheap silhouettes to 500, then opaque grouped canopy meshes to 2,000. These are tuning proposals, not new placement boundaries. Build distant groups from the same deterministic trees; retain species mix, glades and skyline. Check elevated views, fog and transitions before replacing individual models. Avoid dense overlapping transparent cards that could exchange geometry cost for fill cost.

5. **Bound streaming work.** Generate only entering tiles and prefetch ahead of walking/biking. Spread generation over a measured per-frame budget. Recheck construction only in affected tiles. Publish render/collision changes together, preserving suppressed IDs and the existing nearby safety margin. Invalidate caches on terrain, recipe or construction changes.

## Acceptance

- Test the village facing woodland, a dense grove, an elevated 2 km view, and a bike route across tile boundaries.
- Aim to return within 10% of the reproduced pre-enhancement frame-time baseline on the same hardware/settings. Target 60 FPS where that baseline supports it; this is not yet a measured promise.
- Initial incremental forest budgets: at most 2 ms CPU and 3 ms GPU in representative views. Adjust only from measurements, and check worst-frame behavior as well as averages.
- Preserve deterministic IDs, the 60-unit village meadow, construction displacement, camera-origin transitions and nearby collisions.
- Keep the prior frame-acquisition fix: optimization must not bring back blank-frame flicker.
- Capture matching screenshots and repeat the same benchmark after each stage. Stop adding complexity once the performance and visual targets pass.

The previous functional checks established correctness, not an acceptable frame-time budget. Performance and visual acceptance must both gate the next forest revision.
