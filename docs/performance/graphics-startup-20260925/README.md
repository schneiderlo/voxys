# Graphics startup, 25 September 2026

This change follows the [Claude performance article](https://claude.dev/blog/how-we-made-claude-ai-faster/):
reproduce the reported delay, measure a specific source of work, reduce it,
check the actual game, and retain a regression guard.

This report covers the first, collision-shader change. The subsequent
[render batching report](render-batching.md) measures another improvement.
Both builds in that follow-up already include this collision fix; their
absolute times come from separate builds and measurement sessions.

The [early compilation and reuse follow-up](overlap/README.md) adds two further
physics compiler reductions, starts GPU setup during asset downloads, and
measures first and repeat visits with matching pipeline descriptions.

The inspected public release was `fa8ebffcecaef5c98aedf9363a8c262c88bb9168`.
Earlier collision-pipeline batching is already in that release. This change
does not claim that earlier improvement again.

## Change

`collide_authored_polyhedra` used two call sites for the same large contact
routine, with opposite body orders. Inlining can duplicate the entire BVH
traversal and contact generation. Select the body indices before one call,
then perform the existing anchor/normal conversion when the order was reversed.
The same routine receives the same arguments and executes once per invocation.

This changes no geometry, contact limits, arithmetic inside the traversal,
simulation cadence, renderer settings, or deferred compilation policy.

## Focused measurement

Windows Chrome 153, Intel Iris Xe. Identical production shader, 14 collision
variants, explicit production layout, workgroup size 128. Each run used a fresh
browser profile, normal GPU watchdog, and no concurrent build or benchmark.
The driver cache was not cleared. Timings include asynchronous batch completion.

| Source | Collision compilation |
| --- | ---: |
| Existing release | 95.019 s |
| One ordered contact call | 55.224 s |

One sample per version: 41.9% less time for this stage. Both completed without
reported device loss or validation errors. The same-device repeat is an in-memory
cache check, **not** a page reload. Raw reports: `baseline.json.gz` and
`ordered-call.json.gz`. The exact baseline source is in `baseline.wgsl.gz`.

Additional clipping-loop and call-merging experiments measured 50.367 s and
50.207 s. They were not retained: the much smaller body-order change captures
most of the observed gain with less execution-path change. These exploratory
samples do not establish the extra benefit against timing variation.

## Full browser check

The same production release was served locally for both versions. Its data and
WASM bytes were checked against the release manifest; hashes are retained in
`packaged-release.json`. The baseline shader was injected unchanged for the
reference run; the candidate run substituted only the edited WGSL. No rebuild,
software rendering, reduced visual setting, or disabled GPU watchdog was used
in these browser measurements. Each started with a fresh Chrome profile and
ran without a concurrent build or other benchmark.

| Local release | Graphics preparation | Playable observed | Pipelines completed |
| --- | ---: | ---: | ---: |
| Original collision shader | 192.389 s | 203.833 s | 235 |
| One ordered contact call | 145.388 s | 156.689 s | 235 |

The graphics span fell 47.001 seconds (24.4%) in this pair. This is one matched
run per version, not a population estimate. The pipeline count stayed the same;
compilation was not deferred into gameplay. Driver caches were not cleared.

Both runs used a 1280×720 gameplay canvas, Intel Iris Xe, and ten-second
scenarios. Startup instrumentation was removed before scenario timing.

| Scenario | Baseline frame interval p50 / p95 | Candidate p50 / p95 |
| --- | ---: | ---: |
| Idle | 16.7 / 16.8 ms | 16.7 / 16.8 ms |
| Camera orbit | 33.4 / 50.2 ms | 33.3 / 50.0 ms |
| Walk | GPU reset during test | 16.8 / 50.0 ms |
| Throw 100 bricks | Not reached | 16.7 / 117.0 ms |

The candidate advanced physics and passed all four checks without reported
device loss or uncaptured GPU errors. The throw check required exactly 100
new bodies. Idle and throw screenshots were visually inspected and are retained
as `candidate-idle.png` and `candidate-throw.png`.

The baseline hit `DXGI_ERROR_DEVICE_HUNG` during walking. An earlier run directly
from the public URL completed graphics preparation in 213.979 s and passed idle,
then hit the same error during orbit. Its 326.683 s observed playable time
includes remote downloads and must not be compared with the local candidate's
page time. Its idle screenshot is `baseline-idle.png`.

These failures are in the automated test browser; they are not a reported failure
of the user's ordinary browser session. They match the already documented
[GPU reset investigation](../gpu-hang-20260924/README.md). The successful
candidate is not proof of a reset fix. Idle/orbit showed no measured slowdown,
but the failed baseline prevents a complete walking/throwing FPS comparison.
No universal gameplay speedup is claimed.

`summary.json` contains the condensed comparison. Full reports, startup traces,
and console/browser logs are retained as `page-*.gz`, including failed baseline
attempts. The first run predates the harness's failed-scenario retention fix;
its process failure is recorded in `page-baseline-outcome.json`.

## Regression checks

`scripts/test_collision_compile_work.mjs` guards the single large traversal
call site. It passes on the candidate and rejects the archived original
(`guard-rejects-baseline.txt.gz`). This is a source-level count, not a claim about
emitted GPU instructions. Hardware compilation timings establish its relevance.
The existing Pages regression step runs this guard and the measurement-helper
tests. Dispatches that explicitly build older source commits remain supported.

The browser measurement helper refuses to substitute a shader unless the
deployed source exactly matches the archived baseline. It restores the original
WebGPU APIs before gameplay timing. Its tests cover descriptor preservation,
restoration, source mismatch, missing substitution, and unfinished compilation.

The native `gpu_authored_shapes` target built successfully. Four collision
checks passed against both the archived shader and the candidate on Vulkan
llvmpipe: primitive/authored body order and exterior anchors, contact warm-start
identity, rotated compound contacts across sectors, and missing/retired shape
handles. This establishes correctness on a software Vulkan device, not hardware
FPS. Each version's first run exceeded the fixture's existing GPU readback
deadline in the new body-order test; the other three passed. Repeating the
body-order test passed on both versions without changing the test or deadline.
The initial failures and repeats are all retained as `native-*.xml.gz` and logs.

All 36 tests in the expanded loading/startup suite passed, along with five
gameplay-work checks. Both generated-geometry consistency checks passed.
No renderer or physics C++ implementation changed in this first pass.

These measurements were collected locally before publishing the changes.

## Reproduce

```sh
node scripts/performance/compare_pipeline_startup.mjs --chrome=PATH --mode=batch --output=DIR
# Use --shader=PATH to measure an archived source with the identical harness.
node --test scripts/test_collision_compile_work.mjs scripts/performance/test_startup_graphics_probe.mjs

node scripts/profile_gameplay.mjs --chrome=PATH \
  --url=https://schneiderlo.github.io/voxys/index.html --output=DIR --startup=1 \
  --narrow-shader=PATH --expected-narrow-shader=BASELINE_PATH --seconds=10
```

For the reference run, use the baseline source for both shader arguments.
Use separate fresh profiles and output directories, and run measurements
sequentially without competing builds. `graphics_span_ms` measures the span
from the first asynchronous pipeline request to the last pipeline completion.
`playable_observed_ms` includes downloads and the readiness poll (up to five
seconds), and requires the loading overlay to be gone and 60 frames completed.
The latter is not a precise first-frame timestamp.
