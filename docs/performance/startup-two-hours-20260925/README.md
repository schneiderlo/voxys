# Two-hour startup follow-up

This follows the [Claude performance methodology](https://claude.dev/blog/how-we-made-claude-ai-faster/): trace the real load, reduce measured work, compare complete loads, and check gameplay. The baseline already includes the [previous collision-preparation improvement](../collision-prewarm-20260925/README.md). Gains from the two investigations should not be added together.

The final change keeps three shader optimizations:

- Choose authored sphere/capsule body order before one large contact traversal.
- Choose a primitive box's reference face before one shared clipper.
- Expose the cylinder category already guaranteed by the cylinder-pair bucket.

Geometry, tolerances, iteration limits, solver settings, workgroup sizes and rendering quality are unchanged. **The original startup helper and compute-only public recipe are retained.** Two experiments were discarded because their complete-load or runtime benefit was inconsistent.

## Final complete-load results

<!-- FINAL_RESULTS_START -->
The final two comparisons reached play **7.0–12.3 seconds sooner** (7.4–11.8%). These are the additional gains relative to the start of this follow-up, on this machine.

| Pair | Before playable | Final playable | Before graphics | Final graphics |
| --- | ---: | ---: | ---: | ---: |
| 1 | 94.661 s | 87.661 s | 82.753 s | 75.899 s |
| 2 | 104.405 s | 92.124 s | 92.631 s | 79.823 s |

Both final visits produced exactly the same 181 program descriptors after the expected narrow-source replacement. There were no extra prepared programs, failed warmups, GPU losses or uncaptured errors. The full-scenario pair passed idle, exactly 100 thrown bricks, walking and orbit; the reverse-order follow-up passed idle. No measured gameplay slowdown appeared.

| Scenario, full pair | Before FPS | Final FPS |
| --- | ---: | ---: |
| idle | 59.76 | 59.93 |
| throw | 34.42 | 35.68 |
| walk | 13.73 | 15.85 |
| orbit | 14.48 | 15.55 |

The short FPS samples are checks against regression, not a claim of a universal frame-rate gain. [final-comparison.json](final-comparison.json) records the exact pair selection and checks.
<!-- FINAL_RESULTS_END -->

“Playable observed” requires the loading cover to disappear and more than 60 frames to complete. Polling can add up to five seconds. “Graphics span” runs from the first asynchronous pipeline request to the last completion. These are separate measurements.

## GPU execution

The final source was checked against the original with 2,048 deterministic pairs per case. All six cases produced **bit-for-bit identical manifolds**: contact counts, feature IDs, normals, anchors and unused fields. Each fixture includes contacts and misses. Authored round shapes exercise both body orders.

| Kernel, 2,048 pairs | Before median | Final median |
| --- | ---: | ---: |
| Sphere/sphere control | 0.197 ms | 0.197 ms |
| Primitive box pair | 1.835 ms | 1.442 ms |
| Cylinder pair | 7.799 ms | 3.211 ms |
| Authored sphere/box | 1.049 ms | 0.590 ms |
| Authored capsule/box | 1.114 ms | 0.524 ms |
| Restored authored-box path | 7.733 ms | 7.668 ms |

The four changed kernels run about 21–59% faster in this fixture. The restored authored-box difference is small and is not claimed as an improvement. These are isolated GPU times, not whole-game FPS gains.

`compare_narrow_runtime.mjs` uses production entry points and the production binding layout. GPU timestamps exclude compilation, uploads and readback. After warmup, four rounds alternate before/after order, with 20 dispatches per block. An earlier 1,024-pair fixture and a second 2,048-pair seed also preserved every contact bit. The GPU authored fixture is a single box; native tests cover concave and multi-cell geometry.

## Compiler work

Focused comparisons retain the actual production descriptors and change only source. Both run orders were measured with fresh Chrome profiles. The [focused summary](focused-summary.json) and raw reports preserve source hashes and timings.

| Retained transformation | First before / after | Reverse-order before / after |
| --- | ---: | ---: |
| Authored sphere and capsule | 7.536 / 3.693 s | 6.744 / 3.215 s |
| Cylinder pair | 3.828 / 3.282 s | 4.454 / 4.047 s |

Those affected compilation stages improved by 51–52% and 9–14%, respectively. The primitive-box change also removes a duplicate clipper call site and has repeated GPU execution gains. The compiler-work guards reject the original source and accept the retained source.

An initial full-batch experiment changed timings of untouched kernels too. It is archived but not used to attribute the entire batch difference to one edit.

## Discarded experiments

**Authored-face clipping merge.** Combining its two reference-face branches helped focused compilation, but runtime did not improve consistently. The first fixture's median changed from 6.423 to 6.291 ms; the larger fixture changed from 8.454 to 8.716 ms, with substantial variation between blocks. That did not meet the requirement to preserve runtime. The original authored clipping function was restored. The experimental face-group compiler savings of 11–18% are not claimed for the retained source.

**Early render preparation.** This prepared 19 additional render programs and adapted only known canvas targets to the browser's preferred format. Its unit tests, descriptor identity checks and gameplay checks passed. The first isolated load improved by 5.478 seconds, but later complete-load results were inconsistent. It also added about 74 KiB after gzip. Its publisher, startup-helper and test changes were restored to their pre-investigation versions. None of its canvas-format or cache-key changes ships.

The first combined candidate likewise had mixed complete-load pairs: 127.206→108.096 s and 102.292→108.472 s. Those results were kept, rather than selecting only the favorable pair. The final comparisons test the smaller, retained change.

## Environment and package identity

Windows Chrome 153, Intel Iris Xe, hardware WebGPU, 1280×720, normal GPU watchdog. Performance tests run serially, without a concurrent build or another benchmark. First visits use fresh browser profiles. Driver caches and hardware clocks are not reset. Results are local samples on one machine, not population percentiles or predictions for every device.

The local server adds no artificial download delay. The combined Bazel pack is about 144.64 MiB, unlike the smaller production CMake pack. Absolute times should not be compared with the remote deployment.

The WASM rebuild succeeded and its binary is byte-identical to the baseline. All 321 preload entries were checked. After rejecting the authored-face experiment, the runtime-loaded WGSL entry was repacked, with every generated offset and the package size verified. **Only the narrow shader changes, adding 441 bytes.** Meshes, textures, configurations and other shaders are unchanged. The final JavaScript startup helper matches the baseline exactly.

The staged public recipe retains the same CI-capture costs and interfaces as the baseline. Its release identity is updated only after verifying the binary, pack entries and unchanged compatibility source; the real publisher then exports the current hardware shader. This is an explicitly derived experiment fixture, not a claimed fresh CI capture. The normal release workflow still captures each release normally. The final recipe contains 158 compute programs, as before.

## Validation and limitations

The final browser and publisher check logs are archived. Generated authored/LEGO shader checks, real-Chrome loading recovery and whitespace checks pass. Extended native sphere/capsule tests verify both body orders and authored-side anchors.

Thirteen native cases pass for the original and experimental combined shaders across completed runs and isolated reruns. They cover all ten pair classes, Box3d comparisons, persistent contacts, 64/128/256-thread profiles, concavities, thin rails, large coordinates, contact reduction and warm-start anchors. Four focused contact/warm-start cases also pass on the final retained source.

Native checks use llvmpipe for correctness. Initial cold variants encountered a readback timeout and crashes with zero outputs, including the original shader at 256 threads. Isolated reruns passed; no assertion or timeout was relaxed. Initial failures and reruns are retained. Native timing is not hardware-performance evidence.

One discarded render-preparation run reached 18 frames and then reported Intel `DXGI_ERROR_DEVICE_HUNG`. The same error was recorded in the earlier baseline investigation. That failed run supplies no successful-load timing. Passing other runs does not establish a driver-reset fix.

## Evidence and reproduction

[summary.json](summary.json) contains successful and failed runs, program identities, cache statistics, gameplay samples and GPU health. Full traces, native/unit logs, package hashes and source fixtures are compressed beside it.

Artifact names reflect the experiments:

- `page-render-*`: render preparation in isolation.
- `page-final-*`: early combined candidate, including discarded authored-face merge.
- `page-retained-*`: final shader with the subsequently discarded render preparation; `before` runs are unchanged baselines.
- `page-shader-*`: final shader with the original startup helper and compute recipe.
- `runtime-retained`: GPU execution and bitwise contact check of the final shader.

The first final pair uses the immediately preceding `page-retained-before-2` baseline. The second pair reverses order. The first pair measures all four gameplay scenarios; the second measures idle after the same startup endpoint.

```sh
node scripts/performance/compare_narrow_runtime.mjs \
  --chrome=PATH --before=original.wgsl \
  --after=shaders/physics_narrow_phase.wgsl \
  --pairs=2048 --seed=305419897 --output=RESULTS

node scripts/profile_gameplay.mjs \
  --chrome=PATH --site=SITE --output=RESULTS \
  --startup=1 --seconds=10 --settle-seconds=10 \
  --scenarios=idle,throw,walk,orbit
```

Extract `narrow-before.wgsl.gz` for the original shader. `narrow-final.wgsl.gz` is the retained source; `narrow-combined.wgsl.gz` is the discarded larger combination. Use fresh output directories and run comparisons serially.
