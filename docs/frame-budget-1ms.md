# Toward a 1 ms GPU frame

**Status: a measured wave-update improvement, not a demonstrated 1 ms game frame.**

Baseline: `077c1e97d9889de8584206b7cce6e24dd7e7972a`.
Tested production implementation: `5dd018931be0e5668d4ada977c56f33e17061df9`.

## Work removed from the ocean update

The previous ocean already used workgroup-local 256-point FFTs. This change
is not a claim of replacing sixteen global FFT passes.

1. Evolve the spectrum directly into the first row FFT's workgroup memory.
   Avoid storing and immediately rereading the complete 4 MiB evolved field.
2. Fuse pairs of radix-2 stages while keeping their arithmetic order and
   twiddle table. Per-axis stage synchronization drops from eight barriers to
   four; including the initial load barrier, each line goes from nine to five.
   Sixty-four lanes each own four outputs rather than a 256-lane workgroup
   with half its lanes idle during each old butterfly stage.
3. Retain the original final displacement/normal/compression shader. A tiled
   finalize experiment reduced requested reads but made the software test
   slower, so it was removed from production.

The complete update goes from four dispatches to three. The removed field
write/reread represents 8 MiB of source-level storage traffic per actual
spectral update, not per displayed frame. Stage shared-memory traffic is
halved, but physical memory transactions and machine instructions depend on
the GPU/compiler. The existing 256x256 resolution, two cascades, 120 Hz update
ceiling, wave settings, textures, filtering, camera, and physics are unchanged.
No new simulation GPU allocations or bindings are required.

## Measured ocean results

Both runs use Chrome/SwiftShader, a **software adapter, not the user's Intel
GPU**. They time a complete nonzero, evolving two-cascade ocean update,
including final displacement and normal output. They do not time terrain,
water surface shading, physics, browser presentation, or an entire game frame.
Four warmups precede twelve paired samples with alternating baseline/candidate
order. All individual times remain in the raw reports.

| Run | Baseline median | Fused FFT median | Reduction in GPU time |
| --- | ---: | ---: | ---: |
| Initial isolated FFT comparison | 648.231 ms | 188.431 ms | 70.93% |
| Final production rerun | 797.088 ms | 236.652 ms | 70.31% |

The final candidate was faster in all twelve pairs (about 3.37x the update
throughput on that adapter). Absolute times vary with the hosted software
runner; neither row is an Intel latency prediction or game FPS measurement.

The rejected tiling-only experiment changed the initial run from 623.847 ms
to 671.086 ms: **7.57% slower**. Combining it with fusion also lost some of the
fusion benefit. The original finalize shader is byte-for-byte retained in the
final production diff.

Initial run: `33938244211`, GPU job `101230355415`, raw artifact `9960914501`.
Final run: `33938492388`, GPU job `101231067637`, raw artifact `9960989119`.

## A frame budget, not a misleading stage sum

Two additional GPU timestamps bracket the frame's command-buffer execution,
from before water/physics through terrain, lighting, underwater particles,
primitives and optional motocross drawing. The original four stage slots are
retained. Query count changes from eight to ten.

Telemetry adds:

```text
render_gpu.frame_interval_available
render_gpu.gpu_frame_ms
render_gpu.render_width
render_gpu.render_height
render_gpu.includes_gpu_physics
```

`render_gpu.total_ms` remains the old stage sum for compatibility. It is not
used as a substitute for `gpu_frame_ms`. Sample source frame and age remain
available. Resolution is stored with each asynchronous readback rather than
borrowed from a later resized viewport.

Scope: GPU execution inside the frame command buffer, including inter-pass
work/gaps and GPU physics. This is **not end-to-end frame latency**. It excludes
CPU encoding, queue uploads enqueued before the command buffer, display
presentation, and the profiling resolve/readback after the end marker. Those
costs need separate measurement before claiming 1000 FPS or 1 ms latency.
The two boundary passes also add profiling overhead on sampled frames.

## Measure the running scene

With render profiling enabled, run this in the browser console and keep the
normal scene and viewport unchanged while moving the camera as usual:

```js
const budget = await voxyMeasureGpuBudget({targetMs: 1});
console.log(budget);
```

The helper changes no settings and performs no synchronous GPU waits. It
collects newly completed samples, deduplicates repeated asynchronous packets,
and reports median/p95/p99/max GPU duration, sample count, budget overruns,
resolution and adapter. Zero/invalid samples are not accepted as fast frames.
Changing resolution returns `mixed_resolution`; missing or too few samples
returns `insufficient_samples`. Aborting does not count as success.

The default is 30 samples, bounded by a 120-second collection window. The
engine samples every 30 rendered frames, not every displayed frame, so this
is a **sampled** percentile report. It does not establish full-workload hardware
acceptance. A slow renderer may return fewer samples before the window ends.
`renderProfile=0` disables the measurements; keep it enabled for this check.

## Correctness checks

The final WebGPU run executed 36 complete-resolution comparisons: row output,
full 2D FFT output, and all four half-float displacement/normal/compression
texture layers. Cases include zero, impulse, randomized and high-frequency
inputs, three times including the long-time boundary, and directional-sine
settings from zero through 1.5.

Across **37,748,736 compared words**, there were **zero differing words and
zero maximum absolute error** on SwiftShader. This includes 25,165,824 float32
state words and 12,582,912 half-float texture bit patterns. It is not a proof of
bit identity on every vendor or full-scene visual equivalence.

Twenty-four CPU regression groups pass: eight FFT/index/DFT/dispatch checks,
six existing lookup/lifetime checks and ten existing water/material checks.
Twenty Node tests pass: twelve budget/controller cases and eight existing
WASM timestamp-bridge cases.

Reproduction requires the baseline Git history, Node 22+, and a WebGPU Chrome
binary (`VOXY_TEST_CHROME` can select it):

```sh
python3 tools/test_water_fft_fusion.py
node --test scripts/test_frame_budget.mjs scripts/test_webgpu_timestamp_compat.mjs
node scripts/test_water_fft_fusion.mjs
```

Full-resolution moving-scene Intel/Windows timings and images remain untested.
No native CTest rerun or hardware FPS recovery is claimed for this round.
The earlier four unresolved native cove/shoreline failures are not disabled
or changed by this patch. The 1 ms target remains unproven.

## Exact application build and runtime validation

Final validation run `33938492388` passed all three jobs. The exact candidate
WASM build compiled and linked with Emscripten 6.0.1. Its browser package ran
both the ordinary terrain scene and optional RIDGEBREAK in independent Chrome
processes, with profiling enabled and no shader source injection.

Each scene reached 16 frames, returned valid frame-begin/end timestamps, and
reported no uncaptured GPU error or device loss. The shipped budget helper was
loaded and correctly classified a single timing sample as insufficient rather
than success. Browser artifact: `9961086896`; build/runtime job: `101231067647`.

These runtime checks used a 320x240 viewport and 1024-body reserve with Mesa
canvas interop and a SwiftShader WebGPU adapter. The returned first-frame
samples were 2305.868 ms (terrain) and 2286.874 ms (RIDGEBREAK), including cold
startup/first-use work. They are **not** warmed hardware FPS measurements and
are plainly not 1 ms frames. Both source frames were zero, age 16.

The browser device profile reported fallback=false despite architecture=
swiftshader. The budget helper therefore additionally recognizes named software
architectures; an extra Node regression covers this case. That reporting-only
fix follows the tested WASM implementation and does not alter rendering.

The temporary write-enabled preparation workflow and binary transport files
are removed from the delivered diff. A permanent read-only regression workflow
checks the ocean update and budget reporting on relevant main/PR changes.
