# First-visit collision preparation

This follows the [Claude performance article](https://claude.dev/blog/how-we-made-claude-ai-faster/):
trace the slow journey, reproduce it, change one dependency, measure the whole
journey, and retain a correctness/work guard. It builds on the existing
[startup work](../graphics-startup-20260925/overlap/README.md).

Across the two main comparisons and the longer-idle follow-up, first visits
reached play **7.5–17.8 seconds sooner** (6.3–14.0%). These are matched local
measurements on one machine.

## Change

The published early-compilation recipe omitted all 23 collision programs.
They waited for normal engine initialization, even though their source and
pipeline interfaces were available during downloads. The recipe now includes
them: 158 file-backed compute programs instead of 135.

The release workflow captures its recipe on SwiftShader. That uses a separate
collision implementation with the same pipeline interface. The publisher now
recognizes that **exact current compatibility source** and exports the current
hardware source with the captured layouts, entry points and specialization
constants. A hardware capture exports directly. Unknown or stale source is
still excluded; the capture is not mutated.

The existing browser guard disables speculative compilation on software GPUs.
Hardware requests still require an exact descriptor/source match to reuse a
program. The four-request warmup limit, failure fallback, release identity,
cache behavior and draining before play are unchanged.

This changes only the build-time recipe publisher. WASM, game JavaScript,
shaders, meshes, textures, physics, rendering settings and frame-loop code are
byte-identical in the comparison. Both versions create the same 181 distinct
GPU programs before play. Nothing is deferred into gameplay.

## Measurement method

Windows Chrome 153, Intel Iris Xe, headless hardware WebGPU, 1280×720.
Each run uses a fresh browser profile, the normal GPU watchdog and a fresh
world. No build or other benchmark runs concurrently. Driver caches and
hardware clocks are not reset; these are local paired samples, not a prediction
for every device or a population percentile.

The two staged sites use the same existing optimized WASM/data build. The
baseline publisher is from `14b27c4`; the candidate changes only the publisher.
Both receive the **same software capture**, as the release workflow does.
The local server adds no artificial download delay. Bazel's combined pack is
144.64 MiB, unlike the smaller production CMake pack. Absolute load times must
not be compared with a remote deployment or the earlier reports.

The harness records the interval from the first asynchronous pipeline request
to the last completion. “Playable observed” additionally requires the loading
cover to disappear and 60 frames to complete; polling adds up to five seconds.
These are distinct metrics, not exact first-frame timestamps.

After startup, the harness removes its instrumentation, waits ten seconds,
then runs ten seconds each of idle, throwing exactly 100 bricks, walking and
camera orbit. The moving scenarios run while thrown bricks remain active, so
their frame times include that load. Both versions use the same order.

## Results

Run order was candidate, baseline, baseline, candidate. Every run used a fresh
profile and downloaded its pack. [summary.json](summary.json) retains timings,
program identity checks, hardware/browser details, package hashes and source
hashes; the complete per-run reports and traces are compressed beside it.

| Pair | Baseline playable | Candidate playable | Baseline graphics span | Candidate graphics span |
| --- | ---: | ---: | ---: | ---: |
| First | 116.663 s | 104.536 s | 102.605 s | 91.679 s |
| Second, reversed order | 127.467 s | 109.661 s | 112.203 s | 96.525 s |
| Longer-idle follow-up | 120.579 s | 113.036 s | 107.041 s | 99.142 s |

The follow-up used the same startup measurement and fresh profiles, followed by
longer idle sampling instead of the four short scenarios. Its smaller startup
gain is included in the overall range above.

The first collision request moved from **32.767 / 35.206 seconds** to
**3.949 / 3.835 seconds**. This is the dependency change being measured.
All four runs completed exactly the same set of 181 programs, comparing full
shader source, layouts, entry points, constants and render state. Only diagnostic
labels and measured compilation costs are excluded from that comparison.
All speculative work finished before gameplay, with no failed warmup requests.
The 181 count is distinct pipeline descriptors/API requests, not a measurement
of internal driver compiler invocations.

The published recipe grew from 726.0 to 867.1 KiB before HTTP compression.
No asset or WASM bytes changed. Return visits that already have a saved full
recipe already include collision programs; no extra return-visit win is claimed.

All four runs passed idle, exact 100-brick throwing, walking and orbit checks.
Physics advanced at the unchanged resolution, without device loss or uncaptured
GPU errors. Baseline/candidate idle and candidate throw screenshots were inspected.

| Scenario | Pair 1 baseline / candidate FPS | Pair 2 baseline / candidate FPS |
| --- | ---: | ---: |
| Idle | 59.00 / 59.92 | 59.34 / 56.88 |
| Throw | 33.17 / 34.25 | 33.04 / 32.11 |
| Walk, with thrown bodies | 12.72 / 13.43 | 12.67 / 12.18 |
| Orbit, with thrown bodies | 13.12 / 13.88 | 12.08 / 11.48 |

The short gameplay samples vary in both directions. The second candidate's
lower frame rate warranted a longer settled-idle comparison; it is not hidden
or presented as an FPS gain. These throwing/moving workloads remain slow on
this GPU regardless of this startup change. Unchanged code and program settings
are not a claim that all gameplay scenarios have been optimized.

The follow-up ran the candidate, then the baseline, with **30 seconds settling
and 30 seconds measured**. Both passed and retained the same full program set.

| Settled idle | Baseline | Candidate |
| --- | ---: | ---: |
| Rendered FPS | 59.7 | 59.5 |
| Frame interval median / p95 | 16.7 / 16.8 ms | 16.7 / 16.8 ms |
| CPU frame time median / p95 | 5.9 / 8.1 ms | 5.8 / 7.6 ms |

The approximately 0.4% FPS difference and identical median/p95 frame intervals
support retaining this startup-only change. The sample does not prove equality
on every device or under every workload. Its raw results are in `long_idle` in
the summary and the `*-idle-long-*` archives. No FPS improvement is claimed.

A preliminary `baseline-1` run reached play in 109.017 s and reproduced the
previously documented Intel `DXGI_ERROR_DEVICE_HUNG` during orbit. It used a
hardware-captured public recipe and the older idle/orbit/walk/throw order. Its
report is retained, but it is excluded from the paired comparisons because the
final experiment uses the same software-captured input as CI and an identical
scenario order on both sides. Passing later runs does not establish a GPU-reset
fix.

## Checks

The expanded publisher tests compare the complete file-backed compute program
set from archived hardware and software captures, including source, layouts,
entry points and constants. All 158 programs must match, including all 23
collision variants. The fixtures carry their original source, so unrelated
future shader edits do not silently turn the old capture into a new fixture.

Synthetic checks cover exact compatibility-source admission, rejection of
unknown source, preserving both authored passes, capture immutability, and
release-manifest identity. Existing browser tests cover software fallback,
exact descriptor reuse, bounded concurrency, compilation failures and draining
before play. No native or WASM rebuild is required for this build-script change.
All four publisher tests and 35 browser startup unit checks pass. Running the
final publisher tests against the archived original rejects it (the two missing
collision export checks fail and compatibility-source export is refused).
The passing and rejecting test logs are retained here. `git diff --check` passes.

## Reproduce

Stage two copies of the same release. Keep the WASM, data, runtime JavaScript
and web files identical. Use each publisher with the same successful default
software capture and the matching shader directory:

```sh
python3 scripts/publish_graphics_recipe.py SITE CAPTURE --shaders shaders
python3 scripts/test_publish_graphics_recipe.py
node --test scripts/test_gpu_startup.mjs scripts/test_data_packs.mjs \
  scripts/test_webgpu_startup.mjs scripts/test_startup_budget.mjs \
  scripts/test_loading_roller.mjs

node scripts/profile_gameplay.mjs --chrome=PATH --site=SITE --output=OUTPUT \
  --startup=1 --seconds=10 --settle-seconds=10 \
  --scenarios=idle,throw,walk,orbit

# Longer follow-up, candidate then baseline, in fresh browser profiles:
node scripts/profile_gameplay.mjs --chrome=PATH --site=SITE --output=OUTPUT \
  --startup=1 --seconds=30 --settle-seconds=30 --scenarios=idle
```

Run comparisons sequentially and reverse their order for the second pair.
The archived baseline publisher is `publisher-before.py.gz`. Package hashes,
published recipes, complete startup traces, gameplay reports and logs identify
the tested inputs. Changes are local; this work does not publish a release.
