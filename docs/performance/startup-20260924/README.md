# Chrome shader startup, 24 September 2026

The startup crash fix (`9545aec`) changed pipeline creation to asynchronous
creation, but waited for each pipeline before submitting the next. Independent
collision pipelines therefore compiled serially. Its timer-based wait could
also be delayed by background-tab timer throttling.

This change queues the independent narrow-phase pipelines together, then waits
for all callbacks before initialization continues. Promise completion replaces
timer polling. Failed requests also complete their wait; the batch drains every
callback before releasing callback state. Native and runtime pipeline creation
retain their synchronous behavior. Shader source and physics calculations are
unchanged.

## Controlled collision compilation

Windows Chrome 153.0.8010.53, Intel Iris Xe (`intel`, `gen-12lp`). Each mode
started with a fresh browser profile. The test used the same production shader,
14 collision variants, workgroup size 128, and explicit production pipeline
layout. No build or other GPU benchmark ran concurrently. Chrome's GPU watchdog
remained enabled. Background timer throttling was disabled for both measurements.

| Submission | Fresh profile | Repeat on the same device |
| --- | ---: | ---: |
| Submit and wait individually | 248.250 s | 0.0058 s |
| Submit together, then wait | 100.502 s | 0.0012 s |

Batch submission reduced this stage by about 59.5% in this comparison. Neither
run reported device loss or validation errors. These are one sample per mode,
not a statistical benchmark. The driver's system cache was not cleared. The
repeat retained pipeline handles and reused the device: it is not a page-reload
measurement. These measurements do not establish total page startup time.

Raw results: `serial-pipelines.json.gz`, `batch-pipelines.json.gz`.
Shader SHA-256:
`4e5df07810989ee2ff681ffe7a7ff9d4e3d086f26089e5a137bc99f55e195278`.

Reproduce locally with Node supporting built-in WebSocket:

```sh
node scripts/performance/compare_pipeline_startup.mjs --chrome=PATH --mode=serial --output=DIR
node scripts/performance/compare_pipeline_startup.mjs --chrome=PATH --mode=batch --output=DIR
```

Use separate output directories. Run sequentially with the same Chrome binary
and GPU, without a build running alongside the browser.

## Full application comparison: failed gameplay checks

Both builds used Bazel WASM outputs, identical staged web assets, a fresh Chrome
profile, the same Intel adapter, and a 960 × 540 viewport. The unchanged build
is the gameplay candidate from `fb494c7`, before this startup patch. Its WASM
SHA-256 is `d2c3a93d5f64635830a186d107666fea7633f1ddaa17139060ab9831b0bdec83`.
The startup candidate is
`c95e213ab332820602ead31fd93bba99373237f82b964d8a8f0f0507c5acea55`.

| Build | Initialization observed | Gameplay check |
| --- | ---: | --- |
| Unchanged serial waits | 381.235 s | Device hung at frame 21 |
| Promise waits and collision batch | 277.062 s | Device hung at frame 19 |

Both runs reported `DXGI_ERROR_DEVICE_HUNG` after shader compilation and world
initialization. Neither reached the 30-frame success criterion or the reload
round. These are **not successful page-load measurements**. The candidate
initialized 104 seconds earlier, but this does not establish reliable gameplay
or explain the driver hang. Earlier gameplay tests also recorded Intel hangs;
see the neighboring `gameplay-20260924` report.

The failed runs are retained in `baseline-page-failed.json.gz` and
`candidate-page-failed.json.gz`. No GPU watchdog or error check was disabled.
A candidate repeat at 1280 × 720 also hit the same driver error at frame 21;
`candidate-page-repeat-failed.json.gz` retains it. Changing the viewport did not
resolve the failure. This patch has not been deployed.

Reopening that candidate profile on the same local origin completed one run in
256.037 seconds, with 73 frames and no reported GPU errors. Its screenshot was
visually checked: the world and building controls rendered correctly. This was
not a fast cached launch; pipeline compilation occurred again. The following
reload stopped responding after the last sample at 232.213 seconds, during
graphics preparation. The browser was explicitly closed after sampling remained
stalled, rather than counting the reload as a pass. The retained report is
`candidate-retained-profile.json.gz`; its `Chrome closed` error reflects that
manual termination. The underlying reason for the unresponsive reload is not
established. These mixed results do not satisfy a reliable full-page startup
check, despite the focused compilation improvement.

## Local correctness checks

The standalone Emscripten probe passed on Windows Intel Chrome and Linux Chrome
with SwiftShader. It covers batch growth, temporary descriptors and constants,
invalid compute/render entry points, repeated waits, destructor draining, device
loss with pending requests, and progress reporting. JavaScript timers are paused
after startup begins; callbacks still finish without using a polling timer.

The 13 loading/startup JavaScript tests passed. Native narrow-phase syntax
compilation and the full Bazel WASM build also passed. These checks run locally; this change adds no GitHub
Actions workflow or CI step.

See `scripts/fixtures/startup_pipeline_probe.cpp` for the compile command, then:

```sh
node scripts/test_pipeline_startup.mjs --chrome=PATH --build=DIR
# For a software-only Linux environment, also pass --software=1.
```

The page harness measures a fresh profile and then a reload. It waits for the
loading screen to disappear, at least 30 rendered frames, and GPU timing data,
and records browser errors, device loss, and screenshots:

```sh
node scripts/performance/measure_startup.mjs --chrome=PATH --site=DIR --output=DIR
```

Optional `--width` and `--height` select the viewport. To reopen a retained
profile on its original origin, pass `--profile=DIR --port=PORT`; this is labeled
`retained-profile`, not `fresh-profile`, in the report.
