# WebGPU performance profiling

The browser profiler is designed for the actual Voxy pipeline: CPU command
submission, GPU physics stages, water, terrain ray-casting, lighting, primitive
drawing, queue depth, frame pacing, allocation sampling, and network I/O.

It follows two rules from Dennis Gustafsson's BSC 2025 solver talk:

1. Record cheap timestamps in the hot path and process them later.
2. Inspect stage timelines and idle/pacing signals instead of guessing from one
   whole-frame number.

GPU timestamps are copied through an asynchronous readback ring. Profiling does
not wait for the GPU or add a lock to the measured path.

The physics timeline splits the large-world broad phase into six fields:
`broad_index_build`, `broad_index_sort_ranges`, `broad_pair_count`,
`broad_pair_scatter`, `broad_pair_sort_unique`, and `broad_lifecycle`.
Small-world direct-pair kernels intentionally leave the unused grid fields near
zero. These extra timestamp boundaries are emitted only when physics profiling
is enabled.

## Real browser journey

This is the primary experience benchmark. It launches a fresh Chrome profile,
opens the deployed site, fixes the physical canvas at 1920×1080, aims at the
real 8192² terrain, and throws exact body counts through the production
right-click volley path:

```bash
node scripts/benchmark_browser.mjs \
  --headed \
  --output /tmp/voxys-browser-baseline.json
```

The defaults run 100, 1,000, 5,000, and 10,000 bodies three times in each mode:

- `score` uses normal `requestAnimationFrame` pacing. This is the displayed
  browser experience: frame intervals, 1% low FPS, and missed refresh periods.
- `headroom` uses the production uncapped loop. This exposes improvements that
  remain hidden while `score` is pinned to the monitor refresh rate.
- `diagnose` enables GPU timestamp queries and reports every physics and render
  stage. It is opt-in because measurement itself has a cost.

The body order alternates upward and downward between repetitions. Each page
load gets the same camera warm-up, then sweeps real volleys deterministically
across the terrain every four physics ticks, followed by impact and settling
windows. This avoids an artificial single-column pile while retaining
body-body and terrain contacts. A partial final volley creates exactly the
requested total rather than rounding it to 128. Use `--ticks-per-volley` to
model a slower or more aggressive player.

The runner fails a workload if any of these are wrong:

- hardware WebGPU, fast-float GPU physics, raycast rendering, or 8192² terrain;
- physical canvas size, page visibility, build identity, or fallback-adapter
  selection;
- exact resident-body and renderer-input counts;
- observed real-terrain contacts;
- physics capacity overflows, solver errors, first-party load failures,
  JavaScript exceptions, or engine console errors.

The JSON contains compact raw samples for every submitted frame, per-phase
p50/p95/p99 timing, CPU time, refresh misses, collision-density peaks, memory,
browser/GPU identity, and all correctness checks.

A red capacity row is a measured engine limit, not a runner crash. Its timing
is still printed and stored, but it must not be accepted as a valid performance
win until the overflow is fixed.

Use a headless short run as a local correctness smoke test:

```bash
node scripts/benchmark_browser.mjs \
  --target local --build-local --quick --headless
```

Run the full local experience before deployment:

```bash
node scripts/benchmark_browser.mjs \
  --target local --build-local --headed \
  --output /tmp/voxys-browser-candidate.json
```

Add larger real workloads without changing the journey:

```bash
node scripts/benchmark_browser.mjs \
  --bodies 100,1000,5000,10000,20000 \
  --modes score,headroom --runs 3 --headed
```

Compare a candidate with a capture from the same machine, Chrome, GPU, display,
resolution, and browser mode:

```bash
node scripts/benchmark_browser.mjs \
  --target local --build-local --headed \
  --baseline /tmp/voxys-browser-baseline.json \
  --output /tmp/voxys-browser-candidate.json
```

The comparison exits with status 2 when median FPS falls or p95 frame time
rises beyond the default 7.5% threshold. Use `--expect-build SHA` when a
deployed result must come from one exact commit. Comparison also rejects
mismatched Chrome versions or flags, GPU adapters, headed/headless modes,
canvas sizes, repetition counts, and journey timing.

## Enable the probes

Open the benchmark page with both opt-in probes:

```text
http://127.0.0.1:8081/index.html?physicsProfile=1&renderProfile=1&telemetry=0
```

Start Chrome with a remote-debugging port such as `9333`. Keep the canvas size,
GPU, camera, build mode, and browser flags identical between captures.

## Full-resolution WASM renderer gate

Stage probes answer where time goes. The completion benchmark separately
answers whether the complete renderer clears a throughput target. Open the app
with the hardware WebGPU backend and `renderThroughput=1`:

```text
http://127.0.0.1:8081/index.html?physicsBackend=webgpu&benchmarkBodies=0&telemetry=0&renderThroughput=1
```

Then run the gate against Chrome's remote-debugging port:

```bash
node scripts/benchmark_wasm_render.mjs \
  --port 9333 \
  --expected-width 3440 --expected-height 1454 \
  --warmup-frames 128 --measured-frames 1500 \
  --batch-frames 64 --repeats 3 --minimum-fps 700 \
  --output /tmp/voxys-wasm-render.json
```

The runner rejects fallback adapters, a non-WebGPU physics backend, a reduced
framebuffer, a non-raycast renderer, a non-8192² terrain, or a missing
full-quality flag. The engine drains prior work, advances the normal five-view
workload with live water, and waits for `onSubmittedWorkDone` after every
batch. Only completed frames enter the result. The target is a full-size
offscreen texture so compositor pacing, occlusion, scan-out, and display
refresh do not contaminate renderer throughput.

This number is not displayed FPS. Interactive display rate remains bounded by
the browser compositor and monitor refresh even when renderer throughput is
much higher.

## Capture one workload

One deterministic right-click batch is 128 bodies. This command profiles 256
bodies at a 5504×2161 canvas:

```bash
node scripts/profile_wasm_clicks.mjs \
  --port 9333 \
  --clicks 2 \
  --settle-ms 3000 \
  --duration-ms 15000 \
  --timeout-ms 180000 \
  --expected-width 5504 \
  --expected-height 2161 \
  --profile \
  --trace \
  > /tmp/voxys-256.json
```

`--profile` adds sampled JavaScript CPU and allocation profiles. `--trace` adds
Chrome/Dawn GPU trace aggregation. Leave them off for the least intrusive
steady-state GPU measurement.

## Capture the scaling matrix

The matrix runner reloads between workloads, establishes the same fixed camera,
checks all correctness invariants, and writes one raw profile per body count.
It also stamps the browser with a persistent profile-session identity; the A/B
analyzer rejects captures from different Chrome processes because GPU warm-up,
browser lifetime, and system load can otherwise create false wins.
Its default deterministic preset creates every body 100 metres above the real
terrain with non-zero linear and angular velocity, then measures the exact
simulation interval from tick 420 through tick 720:

```bash
node scripts/profile_wasm_matrix.mjs \
  --port 9333 \
  --output-dir /tmp/voxys-matrix \
  --workload preset \
  --bodies 0,256,512,1024,2048,4096,8192,10112 \
  --duration-ticks 300 \
  --expected-width 5504 \
  --expected-height 2161
```

The slightly larger 1024/2048/4096/10112 workloads are conservative stand-ins
for the 1000/2000/4000/10000 targets, with 8192 showing the curve between 4k
and 10k. The preset accepts any body count. It uses the production terrain,
physics, water, ray-cast, lighting, and primitive-render paths; only its body
initialization and measurement window are automated.

Use the actual right-click interaction path as a separate experience capture:

```bash
node scripts/profile_wasm_matrix.mjs \
  --port 9333 \
  --output-dir /tmp/voxys-click-batches \
  --workload click-batches \
  --bodies 256,1024,2048,4096 \
  --duration-ms 15000 \
  --settle-ms 3000 \
  --expected-width 5504 \
  --expected-height 2161
```

Click-batch counts must be multiples of 128. This mode is useful for diagnosing
the deployed interaction, but wall-clock input timing makes it unsuitable as
an isomorphism oracle.

Use the production left-button hold path to reproduce a dense stream from one
camera origin and direction:

```bash
node scripts/profile_wasm_matrix.mjs \
  --port 9333 \
  --output-dir /tmp/voxys-left-stream \
  --workload left-stream \
  --bodies 200,500,1000 \
  --duration-ms 15000 \
  --settle-ms 0 \
  --expected-width 5504 \
  --expected-height 2161
```

The engine's 100 Hz firing loop can cross the requested count within one
rendered frame. The capture therefore records both the requested and exact
observed body counts. This is an experience diagnostic, not an equivalence
oracle; accept optimizations against the fixed-tick preset.

The runner records every workload even if one workload trips a capacity or
correctness invariant. The manifest marks that row as failed and the process
returns exit code 2 after the full matrix is complete.

## Find hotspots and compare a change

Analyze a single capture:

```bash
node scripts/analyze_wasm_profile.mjs \
  /tmp/voxys-matrix/bodies-04096.json \
  --html /tmp/voxys-4096.html \
  --json /tmp/voxys-4096-analysis.json
```

Compare an A/B pair:

```bash
node scripts/analyze_wasm_profile.mjs \
  /tmp/candidate/bodies-04096.json \
  --baseline /tmp/baseline/bodies-04096.json \
  --html /tmp/voxys-ab.html \
  --fail-regression-percent 3
```

The analyzer provides:

- p50/p95/p99 latency, throughput, queue depth, pacing skips, and peak memory;
- the top five GPU hotspots and each stage's measured share;
- tail-amplification ratios that expose stalls hidden by the median;
- deterministic-camera, device, canvas, body-count, backend, arithmetic,
  measurement-tick, candidate-pair, contact, solver, and input-tick
  equivalence checks;
- deterministic bootstrap confidence intervals for every A/B stage;
- a machine-readable report suitable for CI thresholds.

At 85 FPS the whole frame budget is 11.765 ms. A stage optimization only moves
the target if the combined render and physics work fits inside that budget.

## Interpretation limits

The raw sample `atMs` field is when asynchronous data reached JavaScript. It is
not a synchronized CPU/GPU clock. Use GPU durations for stage cost and Chrome's
trace for exact cross-queue scheduling. Do not infer an idle gap from `atMs`.

An optimization is accepted only when the deterministic preset's equivalence
oracle passes, the relevant confidence interval excludes zero, and the complete
scaling matrix has no correctness or tail-latency regression.
