# In-game performance, 2026-09-24

This pass follows the [measurement and regression-check method in the Claude performance article](https://claude.dev/blog/how-we-made-claude-ai-faster/): reproduce gameplay, find repeated work, remove it without changing game rules, and check both work counts and real frame times.

Scope: creative building after initialization. Loading is outside every measurement window. No resolution, draw-distance, mesh-quality, physics-step or event-capacity reductions.

## Baseline and hardware

The public page reported build `b21afe91f86d9d8b612b05bb02388d52497a72f9` when inspected. The local baseline is `9545aec4`, which adds the startup pipeline-compilation fix needed to run reliably on this Intel adapter. It retains the deployed gameplay implementation. The baseline WASM SHA-256 is `c33728666eba65edf152f0c4e8cb93e4c157b8abf215d992c02da9ff7719cb2b`.

Browser measurements use headless Windows Chrome and the hardware Intel Iris Xe adapter, with a fixed 1280 × 720 canvas. Headless frame times do not establish display latency or a sustained visible-screen FPS guarantee. Instrumented diagnosis and clean timing runs are separate. Early exploratory runs overlapped compilation and are not a before/after speedup claim.

The optimized WASM SHA-256 is `d2c3a93d5f64635830a186d107666fea7633f1ddaa17139060ab9831b0bdec83`. Both sites use identical staged web assets. This is a local comparison; these gameplay changes have not been deployed.

## Measured result

The optimized repeat completed all four 12-second scenarios, with no GPU errors, the requested resolution, advancing physics and exactly 100 additional bodies from the throw. Its work checks passed. The final baseline repeat confirmed the old work counts, then suffered an Intel D3D12 device hang during camera movement. It is an incomplete control, not evidence that this patch fixes the driver failure.

| Quiet-scene work | Baseline repeat | Optimized repeat |
| --- | ---: | ---: |
| CPU bytes extracted per reported physics tick | 2,557,363 | 200 |
| Redundant seed / terrain passes | 237 / 237 | 0 / 277 |
| Physics timestamp resolves in five seconds | 94 | 3 |

This is over **99.99% less CPU readback extraction** in the quiet scene. Reported physics ticks come from sampled telemetry, so the per-tick denominator is approximate. The native cadence test verifies the exact sampling rule. Full-capacity GPU buffer copies remain.

The completed first baseline and uncontended candidate repeat gave these timings. Each row is one run per build, not a statistically established speedup:

| Scenario | CPU median before → after (ms) | RAF p95 before → after (ms) | Rendered frames/s before → after |
| --- | ---: | ---: | ---: |
| Idle | 10.4 → 9.1 | 33.5 → 33.5 | 51.1 → 49.4 |
| Camera movement | 12.3 → 9.4 | 50.5 → 50.2 | 31.9 → 30.8 |
| Walking | 11.1 → 9.1 | 50.3 → 50.2 | 27.1 → 28.7 |
| 100-brick throw | 8.1 → 6.4 | 116.9 → 100.3 | 31.6 → 30.9 |

CPU medians decreased 13–24% in this comparison, but **there is no reliable overall FPS improvement**. Frame throughput was mixed, walking still had long outliers, and the repeated baseline failed. The successful idle portion of that final baseline had a 12.0 ms CPU median, also showing run-to-run variation. Camera paths use wall-time input and can diverge slightly with frame scheduling; snapshots retain the resulting camera positions.

Lighting/compositing and terrain raycasting remain the large GPU costs. The optimized V8 profile also identifies mesh submission as a remaining CPU cost. This pass removes verified repeated work; it does not establish the maximum achievable performance of the renderer.

[summary.json](summary.json) contains the compact results. The four `*.json.gz` files preserve raw intervals, telemetry and work counts; decompress with `gzip -dc FILE`. [baseline-repeat-error.txt](baseline-repeat-error.txt) preserves the driver error. The original two archives contain an invalid timestamp counter as explained below.

## Changes

- The current-sun terrain pass writes every color/depth pixel itself. It now initializes those targets directly, removing the preceding full-screen seed copy. Frames without a shadow-producing scene retain the seed path. GPU timestamp boundaries follow the actual first pass.
- Physics event readback still reserves, copies and asynchronously maps its full GPU capacity. CPU extraction reads the header, then only the live records. Empty event packets no longer allocate and copy the unused 2.55 MB capacity into WebAssembly. Overflow, tick identity, event validation, ordering and submission identity remain intact. This reduces CPU extraction; it does **not** claim to remove the full-capacity GPU buffer copy.

- Routine kinematic targets now respect the configured diagnostic interval. Discrete body/attachment mutations still trigger immediate samples. Interactive telemetry uses the existing 30-tick cadence even when GPU profiling is enabled; benchmark body workloads retain their one-tick cadence.

## Reproduction

Build the WASM target with the repository toolchain and stage the normal web assets plus its JS, WASM and data outputs. Keep the original staged baseline in a separate directory.

```sh
node scripts/profile_gameplay.mjs \
  --chrome=/path/to/chrome --site=/path/to/staged-build \
  --output=/tmp/gameplay-timing

node scripts/profile_gameplay.mjs \
  --chrome=/path/to/chrome --site=/path/to/staged-build \
  --output=/tmp/gameplay-work --counts=1 --check-work=1
```

The probe exercises standing still, right-drag camera movement, walking, and one real 100-brick throw. It checks startup, hardware rendering, resolution, simulation progress and GPU errors. Reports retain individual RAF intervals, CPU samples, telemetry snapshots, screenshots and optional V8 CPU profiles (`--cpu=1`). `--profiling=0` disables the game's GPU diagnostics for a separate observer-overhead experiment.

Work ceilings in `scripts/performance/gameplay_work.mjs` cover redundant seed passes, quiet-scene mapped CPU bytes, and physics timestamp sampling. These are deterministic work guards, not wall-clock CI gates. Real event bursts during throws retain their full capacity.

Use `--diagnose-first=1 --check-work=1 --seconds=12` to collect five seconds of instrumented idle work and a V8 CPU profile, restore the original WebGPU methods, then measure the four gameplay scenarios without that instrumentation. The baseline intentionally fails the new work ceilings, so omit `--check-work=1` for baseline runs. Keep the same browser profile, port and canvas size for the pair. Stop compilation and other test workloads before timing.

CI checks the work-guard logic with fixtures; the hardware browser probe must be run separately to enforce those ceilings on a real build.

## Correctness validation

- Production WASM build passed (`--config=wasm`, with a symbol-map sidecar for profiling).
- All 25 JavaScript tests passed: frame budget, timestamp compatibility and gameplay-work guards.
- All three `gpu_event_readback` tests passed, including empty, partial, full and malformed counted packets and ring reuse.
- Both selected `gpu_physics` tests passed: combined-copy preflight and diagnostic cadence during kinematic movement versus discrete mutations.
- All four selected `blit_path` tests passed: baseline bindings, day/night cycling, and moving-caster terrain/water checks with both visual modes.

Native GPU tests used Vulkan llvmpipe as a correctness oracle, never as performance evidence. Cold software shader JIT required a 120-second readback timeout on CPU adapters; hardware retains the existing 10-second timeout and pixel assertions are unchanged. The native build required a per-file suppression for an existing GCC 15 null-dereference warning in `fixture_registry.cpp`.

An experimental per-pass timestamp probe ended with an Intel D3D12 device hang. It was discarded, removed from the tooling, and contributed no performance result. The retained probe uses the game's existing timing support. A later ordinary baseline also hung, so the experimental instrumentation is not established as the underlying cause.

## Rejected and limited measurements

`baseline-first.json.gz` and `candidate-contended.json.gz` preserve the initial pair. A separate `voxys-live-startup-check` browser smoke test overlapped the candidate run. Its slower frame times are not accepted as an isolated estimate of the patch's effect. The work counts remain useful: the baseline extracted 2,557,363 bytes per reported physics tick and performed 226 seed passes for 226 terrain passes; the candidate extracted 194 bytes/tick and performed zero seed passes for 207 terrain passes.

Those two archived reports also contain an invalid zero `physicsTimingSamplesPerTick`: Emscripten omitted the query-set label. Treat that field as unavailable. The corrected probe identifies physics resolves by their destination-buffer label and rejects missing counters instead of reporting zero. A regression test covers this browser behavior.
