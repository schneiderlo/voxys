# Browser frame-gap fix — 2026-09-05

Follow-up to the [fresh-eyes review](fresh-eyes-review-2026-09-05.md).
Baseline: `3608228f9dfa143f670ffe30267863189c6f5604`.

## Cause and fix

The uncapped browser loop tracked only one GPU completion notification. If that
notification arrived late, the loop paused at its queue limit, even when later
GPU work had already finished.

Completion notifications now have independent, stable records in a fixed array.
Later notifications can retire completed work while an older notification is
late. Out-of-order notifications cannot move the completed counter backwards.
The loop resumes only when there is queue headroom and the application is alive.

Notifications normally cover pairs of frames to limit callback overhead. A full
queue always gets a notification, including an unpaired tail. The bounded queue
limit increases from eight to twelve frames to preserve throughput with the new
notification schedule. Storage is fixed; there is no per-frame heap allocation
in the C++ pacing code.

Pacing waits also no longer reset the simulation clock. This prevents frequent
wakeups from slowing gravity. Existing limits on physics catch-up, clock debt,
and long tab-suspension deltas remain in place. Rendering quality, physics solver
settings, and body counts were not reduced.

## Regression check

`--check-pacing` holds one completed GPU notification for 250 ms while allowing
subsequent notifications to arrive. The check runs before the normal benchmark
journey, then verifies that the journey still completes after releasing the
held notification.

- Old build: **zero additional frames** during the delay; queue full at eight.
- Delivered build: **154 and 156 additional frames** in two checks; outstanding
  frames bounded at twelve in both samples.
- Both subsequent workloads passed, with 100 and 5,000 bodies respectively.

This deliberately tests a delayed notification and out-of-order completion
records. It does not prove that every source of a long frame has disappeared.

## Performance

The final comparison reuses one Chrome process and one local server, alternating
saved baseline and candidate artifacts between page loads. Each scene has three
runs per build, with reversed build order in the middle repetition and reversed
body order on alternate repetitions. Timed runs had no parallel compilation or
agent-launched GPU benchmark.

Chrome 152, hardware AMD RDNA 3 WebGPU, headless 960×540, full existing quality.
Values below are medians of individual run summaries, not pooled percentiles.

| Bodies | Baseline FPS | Delivered FPS | FPS change | Baseline / delivered p99 gap |
|---:|---:|---:|---:|---:|
| 1,000 | 1,168.38 | 1,315.87 | +12.6% | 7.3 / 6.0 ms |
| 10,000 | 289.64 | 305.01 | +5.3% | 63.1 / 47.3 ms |

Simulation speed improved from 0.959× to 0.999× at 1,000 bodies and from 0.922×
to 0.941× at 10,000 bodies. Heavy-scene p95 increased from 15.6 to 17.0 ms,
although p99 decreased by 25%. The median maximum interval remained substantial:
96.8 → 93.2 ms. These measurements support improved tail pacing and preserved
throughput on this machine, not a universal performance guarantee.

The earlier eight-frame candidate reduced p99 but lost about 12.5% FPS in the
10,000-body shared-browser comparison. It was rejected. Per-frame callbacks,
three- and four-frame notification intervals, immediate physics notifications,
and a two-notification overlap variant were also explored. Separate-browser
comparisons varied substantially, including unchanged baseline runs. All compact
records are retained in [the measurement evidence](benchmarks/browser-frame-pacing-2026-09-05/measurements.json).
The selected artifact is named `headroom12`; `final` names an earlier rejected
eight-frame candidate in those historical records.

## Validation

- Optimized WASM build passed.
- Real Chrome WebGPU delayed-notification checks passed.
- Capped and uncapped browser workloads passed.
- JavaScript syntax and patch whitespace checks passed.
- Invalid `--check-pacing --modes score` input is rejected before browser launch.

Native code is unchanged; native unit tests do not exercise this browser loop.
Run directly inside the existing Nix environment:

```bash
bazel build --config=wasm //:voxy_wasm
node scripts/benchmark_browser.mjs --target local \
  --artifact-dir bazel-bin/voxy_wasm --quick --bodies 100,5000 \
  --check-pacing --headless --resolution 960x540 \
  --output /tmp/voxys-pacing-check.json
node scripts/benchmark_browser.mjs --target local \
  --artifact-dir bazel-bin/voxy_wasm --quick --bodies 100 \
  --modes score,headroom --headless --resolution 960x540 \
  --output /tmp/voxys-loop-modes.json
```

Headless measurements record application frame intervals, not physical display
scan-out. The twelve-frame limit allows more worst-case queued work than the old
eight-frame limit, but remains bounded. Heavy GPU physics and browser scheduling
can still cause long frames; the fix removes a demonstrated notification stall.
