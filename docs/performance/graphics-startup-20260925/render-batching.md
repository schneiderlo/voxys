# Render pipeline startup batching

Follow-up to the [collision compilation change](README.md). Submit independent
render pipelines together instead of waiting after each request. The terrain,
background, and presentation group contains five pipelines; the water group
contains four. Both groups finish before initialization can succeed.

The shader source, specialization constants, layouts, color/depth targets,
geometry, simulation, and frame loop are unchanged. Native builds and runtime
reconfiguration still create pipelines synchronously. Failed requests are
drained before the batch returns, including on early scope exit.

## Measurements

Two runs of each version on Windows hardware Chrome, Intel Iris Xe, with fresh
profiles and the normal GPU watchdog. Run order was serial, batch, batch,
serial. No build or other benchmark ran alongside these measurements. Driver
caches and hardware clocks were not reset or fixed.

Both versions were built locally with Bazel/Emscripten from `fa8ebff` plus the
earlier collision fix. The only active implementation difference between them
is the render batching patch. The shader/asset pack is byte-identical:
SHA-256 `d89d83a5f5c0906d35c59d6364dbd94844110161d6509f577fd2e0e554b20238`.
Bazel bundles all experiences into a 144.64 MiB pack; this is a local comparison,
separate from the smaller production CMake core pack and its download budget.
Do not compare these absolute times directly with the earlier production-build
measurements in the first report.

| Run pair | Serial render groups | Batched render groups | Serial graphics span | Batched graphics span |
| --- | ---: | ---: | ---: | ---: |
| First | 24.179 s | 10.714 s | 187.913 s | 169.760 s |
| Repeat, reversed order | 38.268 s | 13.021 s | 233.038 s | 187.443 s |

The two render groups took **56–66% less time**. The whole graphics span fell
18.153 s and 45.595 s in the respective pairs. Unchanged stages also varied,
so the full-page differences are not estimates of the batching effect alone.
The useful causal check is the render trace: peak pending requests changed
from one to five in the first group, and from one to four in the water group.

All four runs completed the same 235 pipelines before play. The repeat pair
also captured all pipeline descriptors. Every recorded descriptor matched,
including fragment constants, vertex attributes, targets, blending, and depth
and stencil state. Opaque GPU resources are represented by their labels in
this comparison; the source changes leave their layout definitions unchanged.
The identical asset pack establishes identical packaged shader source.

`graphics_span_ms` spans the first asynchronous pipeline request through the
last completion. Render group times use the same span within each group, then
sum the two groups; they do not sum overlapping compilation durations.
`playable_observed_ms` additionally includes loading and readiness polling.

## Gameplay and limitations

Both versions used a 1280×720 canvas. Startup instrumentation was restored to
the original WebGPU methods before gameplay timing. Readiness requires the
loading overlay to disappear and at least 60 frames to finish. The warm-up
durations below start after that check and the canvas resize.

The batched version passed idle, camera orbit, walking, and throwing 100 bricks
in the ten-second scenario run. Physics advanced, the throw created exactly
100 bodies, resolution stayed unchanged, and no GPU errors or device loss were
reported. Idle screenshots from both builds and the batched throw screenshot
were inspected; no visible quality change was found.

The first serial run passed idle, then hit the already documented Intel
`DXGI_ERROR_DEVICE_HUNG` during camera orbit. Its failed scenario and error
are retained. This is a failure in the automated browser, not a report about
the user's session. The batching change is not claimed to fix GPU resets.

| Idle measurement | Serial | Batch |
| --- | ---: | ---: |
| First, 3 s warm-up, 10 s sample: rendered FPS | 58.62 | 53.39 |
| First: frame interval p50 / p95 | 16.7 / 16.9 ms | 16.7 / 33.4 ms |
| Follow-up, 20 s warm-up, 30 s sample: rendered FPS | 49.87 | 49.53 |
| Follow-up: frame interval p50 / p95 | 16.7 / 33.5 ms | 16.7 / 33.5 ms |
| Follow-up: frame interval p99 | 50.1 ms | 33.9 ms |

The first short sample warranted a follow-up; it is not discarded. In the
longer comparison, both versions showed the same median and p95 pacing, and
rendered FPS differed by less than 1%. This, together with unchanged pipeline
settings, shader bytes, and frame-loop code, supports retaining the startup
change. These samples do not establish equal FPS on every GPU, and the failed
serial camera test prevents a complete dynamic-scenario FPS comparison.

## Regression checks and evidence

Both full WASM builds passed. The new batch helper also passed a native C++
syntax check. The standalone Emscripten probe passed on Intel hardware Chrome
and Linux SwiftShader. It checks 24 valid render requests plus a failure queued
before waiting, copied nested descriptors with alternating target formats,
repeated waits, destructor draining, progress reporting, and GPU loss. Existing
compute and individual render creation checks remain included. Positive timers
are paused during the probe; completion still succeeds through callbacks.
The Pages workflow now builds and runs this probe with software rendering.

The startup measurement tests verify descriptor preservation, immutable
snapshots, source-substitution checks, and removal of instrumentation before
play. The loading/startup suite has 36 passing tests.

Evidence in this directory:

- `render-batching-summary.json`: condensed timings and scenario outcomes.
- `page-render-*.json.gz`, `page-render-*.log.gz`: all four runs, including failure.
- `render-*-package.json`, `render-tested-sources.json`, `render-batching.patch.gz`:
  build hashes and the tested production source change.
- `render-*-build.log.gz`, `render-probe-*.log.gz`, `render-native-syntax.log.gz`:
  build and helper checks.
- `render-serial-idle.png`, `render-batch-idle.png`, `render-batch-throw.png`:
  inspected screenshots.

## Reproduce

Build each version with `nix-shell --run 'bazel build --config=wasm --jobs=5 //:voxy_wasm'`
and stage separate sites. The serial version uses `src/render/blit_path.cpp`
from `fa8ebff`; both versions keep the collision shader fix and otherwise use
the same working tree. Copy `web/`, rename the Bazel `.wasm` and `.data` files
from `voxy_wasm_cc` to `voxy_wasm`, and replace that base name in the generated
JavaScript as well. Generate the local release manifest with
`scripts/write_release_manifest.py SITE --max-core-mib 200`; this temporary
limit accommodates Bazel's combined pack and does not change the production
budget. Replace `__VOXY_BUILD_ID__` in each staged `index.html` with a distinct
local build ID. Package hashes for the tested sites are retained above.

Run the sites one at a time, without a concurrent build:

```sh
node scripts/profile_gameplay.mjs --chrome=PATH --site=SITE --output=DIR \
  --startup=1 --seconds=10

# Longer idle follow-up, reversing the version order:
node scripts/profile_gameplay.mjs --chrome=PATH --site=SITE --output=DIR \
  --startup=1 --scenarios=idle --seconds=30 --settle-seconds=20
```

The standalone helper build command is at the top of
`scripts/fixtures/startup_pipeline_probe.cpp`. Run it with
`node scripts/test_pipeline_startup.mjs --chrome=PATH --build=DIR`;
add `--software=1` for Linux SwiftShader.

These measurements were collected locally before publishing the changes.
