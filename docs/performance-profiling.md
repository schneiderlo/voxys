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

## Enable the probes

Open the benchmark page with both opt-in probes:

```text
http://127.0.0.1:8081/index.html?physicsProfile=1&renderProfile=1&telemetry=0
```

Start Chrome with a remote-debugging port such as `9333`. Keep the canvas size,
GPU, camera, build mode, and browser flags identical between captures.

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
checks all correctness invariants, and writes one raw profile per body count:

```bash
node scripts/profile_wasm_matrix.mjs \
  --port 9333 \
  --output-dir /tmp/voxys-matrix \
  --bodies 0,256,512,1024,2048,4096,8192,10112 \
  --duration-ms 15000 \
  --settle-ms 3000 \
  --expected-width 5504 \
  --expected-height 2161
```

The slightly larger 1024/2048/4096/10112 workloads are conservative stand-ins
for the 1000/2000/4000/10000 targets, with 8192 showing the curve between 4k
and 10k. Counts must be multiples of the
deterministic 128-body batch.

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
- deterministic-camera, device, canvas, body-count, backend, and arithmetic
  equivalence checks;
- deterministic bootstrap confidence intervals for every A/B stage;
- a machine-readable report suitable for CI thresholds.

At 85 FPS the whole frame budget is 11.765 ms. A stage optimization only moves
the target if the combined render and physics work fits inside that budget.

## Interpretation limits

The raw sample `atMs` field is when asynchronous data reached JavaScript. It is
not a synchronized CPU/GPU clock. Use GPU durations for stage cost and Chrome's
trace for exact cross-queue scheduling. Do not infer an idle gap from `atMs`.

An optimization is accepted only when the equivalence oracle passes, the
relevant confidence interval excludes zero, and the complete scaling matrix has
no correctness or tail-latency regression.
