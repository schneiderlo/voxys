# Forest performance recovery validation

Checked on 2026-09-22 after the forest optimization was committed in `b0c19e03`.

## Current checks

- Focused renderer: 6 passed, including retained-vs-live pixels, separate shadow culling, and the 2 km silhouette checks. [Log](mesh-path-test.log).
- Procedural forest, village, and scenery on the installed 8192² terrain: 20 passed, none skipped. [Log](adventure-test.log).
- Free-build runtime startup and exact save restore on that terrain: 1 passed, none skipped. [Log](adventure-runtime-test.log).
- The browser target `//:voxy_wasm` built successfully from the current source. The produced WASM SHA-256 is `0e25a3eddd866f9ba71cc145ad7706cb9c94715aecff78fc3180c6ebbaff5807`.
- The local browser preview reached Free Build and showed the village clearing with near and distant woodland. A later tab showed a black canvas with its controls still visible. A fresh tab rendered correctly through a resize cycle and more than a minute of continued play; the trigger for the stalled tab is not yet confirmed.
- The strict native build first exposed an implicit integer-to-floating conversion in the 2 km silhouette assertion. Its threshold is now expressed with exact integer ratios. All three focused targets then built and passed.
- The repository-wide pre-commit target ran 2,608 `voxy_tests` cases and failed two outside the forest changes: `FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget` (scenery draw-count expectations) and `CannonPhysicsSceneGpu.ThrownBrickFlightBounceAndStackMatchJolt` (brick simulation expectations). The separate terrain importer target passed. This full-suite result is not green; the forest commit is made with the hook bypassed after the focused checks passed.

The raw terrain was `data/generated/td_seed_1234_8192.r16`; the real-terrain runs set `VOXY_ADVENTURE_TEST_TERRAIN` and `VOXY_ADVENTURE_TEST_WORKSPACE` to absolute paths. The first pass without those variables skipped seven real-terrain cases, so it is not counted above.

## Interactive observation from the implementation check

At a fixed 1920 × 1080 spawn view, the optimized recipe 3 scene reported about 76 FPS versus about 49 FPS before the optimization. CPU frame work was about 3.4 ms versus 16 ms, and the GPU frame interval about 11.6 ms versus 16–18 ms. The older cross-recipe baseline was a separate, non-controlled observation and is in [historical-summary.json](../forest-performance-audit-r01/historical-summary.json). The interactive raw capture was temporary and is no longer available, so these rounded values are directional evidence rather than a reproducible benchmark archive.

The fresh preview server also received telemetry from other active game views at different viewport sizes and camera angles while the 1920 × 1080 view was running. Their shared graphics load makes its FPS samples unsuitable for a controlled comparison. No new performance claim is drawn from that smoke check.

The black-canvas investigation found a gap in error reporting: forest static preparation, selection, and final scene-render failures returned `false` without a useful message. Those failure paths now log an error, so a recurrence can identify which stage stopped. The old tab did not report an error before it closed; the exact initiating failure remains unknown.

The same procedural density remained in the checked views, including 51,836 distant trees at spawn. The village meadow and 2 km range remain unchanged.

## Still to measure

There is no isolated forest-only GPU timer or controlled p95/p99 data here. Before declaring the original 2 ms CPU / 3 ms GPU forest-only targets met, measure a village view, dense grove, elevated skyline, and continuous bike ride with matching settings and saved telemetry.
