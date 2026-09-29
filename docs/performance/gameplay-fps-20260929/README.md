# Gameplay FPS investigation — 29 September 2026

This investigation follows the measurement loop in
[How we made claude.ai faster](https://claude.dev/blog/how-we-made-claude-ai-faster/):
measure a real journey, profile it, remove unnecessary work, prove that the
output remains correct, then verify the change with separate timing runs.
Deterministic work checks protect the changes after the timing experiment.

Work started at **18:27:30 UTC** and continued for more than **3 hours 40 minutes**.
The requested minimum was three hours. The closing review records its UTC time
in [final-review.json](evidence/final-review.json).

**No reliable FPS increase was obtained.** The retained changes remove CPU work
and repeated uploads. Their correctness is verified; their effect on full-game
FPS is still unverified. Production collision queries, shaders and compiler
flags remain original.

The investigation resumed after the request to continue. The next Frontier
shader screen is prepared in [run-frontier-shader-screen.ps1](run-frontier-shader-screen.ps1).
Current progress and the required hardware test are recorded in
[the continuation notes](CONTINUATION.md). No new FPS capture has run.

The first CPU-only hardware experiment did **not** establish an FPS improvement.
Across six runs per build, idle throughput was 29.22 → 28.99 FPS and orbit was
26.884 → 26.893 FPS. Orbit's median p95 interval rose from 104.65 to 114.55 ms,
exceeding the 7.5% regression guard. That measured candidate included the collision
prototype that has since been removed. Walking was rejected because the routes
diverged. This capture does not measure the final CPU build.

The first four trials ran faster than the later trials for both builds. The cause
is unproven. Raw captures and the reviewed midpoint-median summary are preserved
in [the evidence directory](evidence/). Later trials record the direct Windows
power-line status before setup and reject a change in status between builds.

## Measurement

- The real Frontier camp runs at 1280 × 720 on Windows Chrome and Intel Iris Xe.
- Each build uses the same terrain, assets and physics settings.
- Timing runs alternate baseline, candidate, candidate, baseline.
- CPU profiles, GPU timestamps and work counters run separately from clean timing.
- Surface acquisition counts must match the engine's rendered frame counter.
- Invalid game health, changed graphics settings or different routes reject a run.

The FPS metric is **uncapped headless renderer throughput**. It measures rendering
headroom, rather than the display's refresh rate. WSL's software Vulkan adapter
is used only for correctness checks and CPU component experiments.

The original walking script turned the camera while changing movement keys.
Its route depended on frame scheduling. That comparison was rejected; endpoint
tolerances were kept unchanged. Raw failed captures are retained.

The earlier v3/v4 runners requested a clock hour and pause state without
explicitly enabling the day cycle or recording the resulting lighting values.
Their actual lighting mode is unverified. The revised runner explicitly enables
the cycle and records the engine's hour, pause state and sun intensity at both
scene endpoints. Ordinary midnight uses moonlight with intensity 0.28; an
intensity-zero shader gate is not a general night optimization. Only a narrow
horizon interval or an explicit zero-light fixture can exercise that gate.

## Retained CPU changes

| Change | Unnecessary work removed | Correctness check |
| --- | --- | --- |
| Publish the final HUD packet once | Two alternating HUD layouts and uploads per frame | Full runtime HUD pixel and input checks |
| Test enemy sight after attack admission checks | Terrain rays for enemies unable to attack | Normal and nearby combat replays |
| Cache the 256 exact byte-paint conversions | Three powers per painted instance | Every byte channel, native and actual WASM |
| Construct GPU instances in their vector | The first 128-byte temporary-record copy | Exact records, draw work and image captures |

A further mesh prototype constructs records directly in final sorted order.
It removes the second 128-byte record copy. All 384 record/work files and 358
image files match exactly across 181 frames. It is **not promoted**: native
encoding medians changed by +8.6% for 600 live instances, −22.3% for 6,000,
and −10.8% for the retained forest/village/character mix. Normal-sized fixtures
changed sign between balanced blocks. These are component timings, not FPS.

A small readonly player-clock export lets the benchmark poll an integer instead
of serializing the entire game state. Both timing builds include that export.
It does not change the simulation.

## CPU component evidence

These are native CPU timings, **not FPS gains**. They use the installed 8192²
terrain and 2,252 actual collision boxes. Six samples per variant use balanced
order; each sample contains 200 batches of 64 queries after warmup.

| Query | Original median, µs | Candidate median, µs |
| --- | ---: | ---: |
| Support height | 0.576 | 0.188 |
| Capsule clearance | 0.569 | 0.189 |
| Capsule sweep | 2.439 | 2.083 |
| Walkable column | 2.290 | 0.608 |
| Terrain/box ray | 55.296 | 54.831 |

Paint conversion drops from 0.0377 to 0.00508 µs per call in the separate native
component experiment. All workload checksums match exactly. The unchanged ray
cost is retained in the table because it limits the effect on the full game.

The collision changes are **rejected; production queries are restored exactly**.
The table records an earlier prototype, not the final implementation. Additional
empty, 256-solid, 4,096-solid and 16,384-solid scenes exposed a repeatable
full-capacity sweep slowdown in the compact-bitset prototype. A direct-visitor
replacement matches both full runtime replays and passes all seven boundary tests,
but its capacity sweep is also slower in each of three balanced blocks:
25.7%, 9.8% and 9.7%. These camp microsecond savings did not establish an FPS gain.
Both prototypes and all raw results remain preserved.

An isolated WASM SIMD component experiment also removes arithmetic work. Its
temporary build vectorizes private bounds tests and some Frontier presentation
operations while shared GLM matrix functions remain scalar. This experiment is
not promoted; the final build retains the original compiler flags and excludes
the scoped SIMD experiment.

## Correctness and rejected candidates

The two full runtime replays each contain 1,200 observations: 241 advancing
simulation updates and 959 paused observations. Game state, final save bytes and
all 21 HUD image hashes match the original build. Original HUD uploads are
1,442; the candidate uses 125 for the normal replay and 78 for the nearby replay.
The stable idle and paused HUD checks require zero repeated uploads.

An initial foot-shadow bounds shortcut passed the software adapters but produced
139 one-ULP differences in a dense Intel boundary fixture. It was rejected.
The revised conservative bounds preserve the original calculation near the
edge and match all **3,013,956** Intel samples exactly.

A terrain night-shadow shortcut initially changed underwater caustics. It was
repaired to preserve the underwater path and remains outside production.

The conservative foot-shadow bounds candidate was also rejected by its actual
Intel ABBA screen: idle 9.33 → 8.52 FPS, orbit 7.73 → 7.83, walk 6.91 → 6.21.
Idle p99 and walking p95/p99 intervals regressed. All routes and power status
matched. Both builds were slower than the earlier experiment; the cause is
unproven. The lighting caveat above applies to this v4 capture.

Combined daytime mesh/water shadow gates and the deferred Cove background load
match all 24 paired Intel shipping-fragment frames exactly. Their planned game
timing was **not executed**. The current restricted environment prevents even
a readonly Windows Node launch with `UtilBindVsockAnyPort: socket failed 1`.
The runner now stores its browser profile inside its output directory and passes
all 25 Linux tests; that cannot restore blocked Windows process interop.

An actual Cove lighting-cache prototype targets duplicate legacy/Cove shading.
Its 33 paired software-GPU cases match exactly, including underwater fallback.
The continuation compiled its isolated native and WASM source files. Linking
and GPU lifecycle execution remain unfinished; they were held to leave a clean
window for the user-run Windows benchmark. Intel exactness, engine lifecycle
tests and actual game timing remain required. Neither
this cache nor the combined daytime shaders is applied to production.

## Final verification

The final CPU build restores the exact original query object and retains the
other CPU changes. Both 1,200-observation full runtime replays match the original
JSON, final save bytes and all 21 HUD image hashes. The stable HUD work check,
all seven collision boundary tests and all 30 active mesh tests pass. One
pre-existing mesh benchmark remains disabled.

The final WASM module validates locally and retains the original 247 imports and
260 exports in the same order. The data file is byte-exact. Generated loader
differences are limited to its output-path metadata and Node launch line.
The packaged build is `frontier-8e255cd28b6b43e5`; its WASM SHA256 is
`a04daf5d95497f97737838cc64cec9e2122280ea1bbd6f0f3e0162b3722f574b`.
This build has no actual Intel FPS capture under the current permissions.

All pre-existing user changes match the initial patch byte for byte. At the
initial closing review, no commit, push or publication had been made.
Frozen proofs, raw timing captures, rejected
patches and the blocked follow-up are in [evidence](evidence/).

## Reproduce

See [the gameplay runner instructions](../../../scripts/performance/README_gameplay_fps.md).
The runner saves raw surface intervals, build and shader hashes, browser/driver
identity, game health and the exact input ticks and camera/player poses.

Focused native checks are `//tests:adventure_query_candidates`,
`//tests:adventure_walkable`, `//tests:frontier_combat` and `//tests:mesh_path_test`.
The complete runtime check is `//tests:frontier_runtime_performance`; set
`VOXY_ADVENTURE_TEST_TERRAIN` to the full decoded terrain and
`VOXY_ADVENTURE_TEST_WORKSPACE` to the repository root.
