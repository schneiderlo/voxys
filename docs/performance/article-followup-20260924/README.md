# Measured performance follow-up, 24 September 2026

This pass applies the [Claude performance article's](https://claude.dev/blog/how-we-made-claude-ai-faster/)
method: identify a real hot path, reproduce its work, reduce that work, compare
timing separately, and retain correctness and work-count checks.

The public page was inspected at
`https://schneiderlo.github.io/voxys/index.html`. It reported source
`84593e9cea602b3a2415ecba541c448ac6938616`, so the earlier collision-pipeline
batching and physics-readback changes were already live. They are not new gains
from this pass. Its default data pack was 98,717,487 bytes and its WASM module
9,581,136 bytes before HTTP compression.

The checkout also contained an unfinished shared GPU HUD. This work preserves
that interface. Comparisons isolate the performance changes against saved
pre-change files from the same checkout; they do not compare the old public
DOM interface against a different new GPU interface.

## Main-branch release selection

The release is prepared from `186e2a68e6a7afaeb1c3c7dfb2e91e0f125e9717`. It
contains the mesh preparation changes, early startup downloads, and UI bridge
performance changes. The existing main-branch Preact components, styling, and
build script are preserved; the browser bundle is rebuilt from those sources.
The unfinished shared HUD and its focus fix remain in the original checkout.

The renderer source/header/test hashes match the versions tested below. The
release startup dependency guard and all 31 loading/startup/preference tests
passed again in the isolated checkout; see
[startup-requests-main.json](startup-requests-main.json). Baseline HTML is
reproducible with `git show 186e2a68:web/index.html`. The original workspace
measurements below remain historical evidence, with their source hashes and
UI scope retained.

The rebuilt release UI passed all 13 tests. Three alternating-order browser
runs using the exact main-branch Preact components confirmed that App renders
and DOM writes fell from 1,000 to zero for 1,000 polls in every run; visible
selection still updated. Final review found concurrent native test executables
in this additional run's process snapshots, so its timing is excluded from
release speed claims. See [the complete record](ui-polling-main-contended.json)
and [release baseline](ui-bridge-origin-main.json). The earlier clean timing
comparison below used the original working-copy UI snapshot.

## Changes and evidence

### Start downloads before styles finish

The release manifest and `data_packs.js` now execute before the first stylesheet.
Previously, blocking styles delayed the script that starts the data and WASM
downloads. A preload uses the exact existing runtime-script URL, including its
release version, and the original script tag still executes it.

`scripts/performance/measure_startup_requests.mjs` runs the actual document
ordering and data loader in Chromium. Its tiny data, WASM, and runtime fixtures
exclude engine and GPU initialization. The candidate's CSS responses are held
until all three critical requests start. A ten-second failure deadline catches
dependency cycles; it is not a speed threshold. The baseline uses a separately
labeled 750 ms CSS delay. Browser interception avoids an artificial
six-connection HTTP/1 limit from a delayed localhost server. The test also
validates compiled WASM and received data, and rejects duplicate runtime
fetches or execution. Substituting the original HTML fails the dependency check.

The baseline data and WASM requests waited for CSS; the candidate requests
started before it completed. The runtime was fetched and executed once in
both. The two CSS fixtures have different release conditions, so their absolute
times must not be compared. See [startup-requests.json](startup-requests.json).

### Publish controls only when presentation changes

The engine advances its observation counter every tick. Comparing full JSON
bytes therefore republishes an unchanged interface every 100 ms. The bridge
now compares the fields displayed by the main-branch Preact interface. Every poll still
refreshes authoritative state, checks pending actions and unsaved changes, and
processes preference revisions. Actions still re-read state before admission.

After an action, a bounded animation-frame callback checks for its accepted
result without waiting for the next 100 ms interval. The chain stops when the
observation advances, on failure, on cleanup, or after eight callbacks. Idle
play schedules no extra callbacks; the interval remains the fallback for a
stalled engine or a hidden tab. The initial working-copy shared HUD also gained
a keyboard-focus guard after suppressed idle observations; that adapter and
its focus change are outside the main-branch release.

Changing ticks, camera coordinates, or diagnostic counters alone must cause
zero UI publications and DOM mutations. Visible changes, action completion,
menu tokens, preference persistence, and keyboard focus remain covered by
tests. This reduces browser presentation work; WASM JSON serialization and
parsing still occur.

### Reduce repeated mesh preparation

Mesh upload builds an index of each logical mesh's original submesh indices.
Frames visit that mesh's submeshes directly instead of scanning the entire
asset for each instance. Original order is retained even when logical meshes
are interleaved. Five temporary frame vectors retain their storage between
frames. Shadow rendering reuses unchanged vertex, index, and material bindings.

This target came from an existing real gameplay V8 profile, where
`MeshPath::render` had 290 self samples out of 2,178 non-idle samples. The
[audit](mesh-evidence.json) retains the profile hash and related call counts;
the [compressed original](mesh-historical-idle.cpuprofile.gz) is preserved.

The image regression compares interleaved mesh/material input with separately
uploaded reference assets, then changes the instance population and switches to
retained scenery. Work checks cover submesh visits, steady scratch capacity,
draw and shadow triangle counts, and unchanged retained uploads. Existing
transparency and offscreen-shadow checks remain relevant.

Counting the loops against the benchmark's actual asset metadata gives:

| Encoding fixture | Submesh checks before | Submesh visits after |
| --- | ---: | ---: |
| 600 live trees (stress) | 19,500 | 2,200 |
| 6,000 live trees (stress) | 195,000 | 22,000 |
| 3,000 retained trees + 330 live forest/village/character instances | 12,272 | 1,210 |

The old count includes the logical-mesh existence search and the subsequent
full submesh scan. It is derived from the original loops and asset metadata;
the new renderer also exposes its actual visit count. The baseline binary has
no such counter, so its placeholder value is not interpreted as zero work.

Retained scratch storage trades repeated allocation for a bounded high-water
allocation. The 600-instance case retained 754,400 bytes. After the 6,000-live
stress case and the subsequent mixed scene, it retained 7,548,840 bytes (about
7.2 MiB), released on renderer teardown. That latter value is not the fresh
mixed scene's memory requirement.

No graphics resolution, draw distance, material quality, physics rules, or
simulation cadence is reduced.

## Timed comparisons

The final comparisons ran sequentially after the builds finished. Each used
three alternating-order runs per variant. Earlier contended samples are kept
for audit but excluded from the results below.

The [Chromium UI benchmark](ui-polling-clean.json) uses the same current UI
components on both sides, changing only the bridge. Its synthetic snapshots
advance observations, camera state, and counters while controls stay unchanged.
Each run measures 1,000 polls after 100 warmup polls. DOM writes are counted in
a separate untimed pass; visible selection changes must still work.

| UI mode | Median CPU time / 1,000 polls before | After | Redundant DOM writes before → after |
| --- | ---: | ---: | ---: |
| Preact in the initial working copy | 738.3 ms | 190.1 ms | 1,000 → 0 |
| Shared HUD accessibility controls in this checkout | 142.7 ms | 152.8 ms | 3,000 → 0 |

Preact polling CPU time fell 74.3%, with App renders also falling from 1,000 to
zero. Shared-HUD polling CPU time rose 7.1% (about 0.010 ms per poll), so no CPU
speedup is claimed for that mode. Both modes eliminate redundant DOM writes.
This test excludes WASM serialization, displayed-frame layout, and GPU work.

The [native renderer comparison](mesh-clean-summary.json) measures CPU
preparation and command encoding using Vulkan llvmpipe. Values below are the
median of three per-run medians, with 20 warmup and 120 measured frames per case.

| Encoding fixture | Before | After | Median CPU time change |
| --- | ---: | ---: | ---: |
| 600 live trees | 0.981 ms | 0.989 ms | +0.8%, effectively flat |
| 6,000 live trees | 12.714 ms | 9.797 ms | −22.9% |
| 3,000 retained trees + 330 live mixed instances | 0.893 ms | 0.782 ms | −12.4% |

Tail results are mixed: median-of-run p95 improved from 26.44 to 20.50 ms for
the large stress case, but rose from 2.26 to 4.67 ms for the small case and
2.12 to 2.48 ms for the mixed case. These samples do not establish a universal
latency improvement or a browser FPS gain. Draw and triangle counts matched.
The [process monitor](mesh-clean-environment.json) detected no overlapping known
builds or probes; ordinary Windows host activity is outside that check.

## Initial workspace correctness checks

- UI build and all 29 Node UI tests passed, including moving observations,
  pending actions, preference revisions, focus, and the bounded frame scheduler.
- Chromium passed all 14 DOM-interface layout/input cases and the shared HUD's
  pointer ownership, keyboard/menu focus, resize, and cleanup checks.
- All 20 loading/data/startup tests and 11 preference-transport tests passed.
- The startup dependency guard passed on the candidate and rejected the
  original CSS-blocked ordering.
- All nine focused native mesh tests passed on Vulkan llvmpipe: interleaved
  materials/scratch reuse, batching, transparency, retained selection/growth,
  offscreen/far shadows, shadow proxies, live physics/stale handles, and encoded
  resource lifetime. Software rendering is used here for correctness only.
- The full optimized WebAssembly target built successfully. Application and
  adventure-runtime objects were recompiled after the mesh class layout change.
  The [build record](wasm-build.json) includes source and staged binary hashes;
  the [build log](wasm-build.log.gz) is preserved.

## Full hardware check and remaining costs

The staged build completed all four ten-second scenarios in Windows Chrome
153 on the Intel Iris Xe, with a 1280×720 canvas: idle, orbit, walk, and throw.
Every scenario advanced physics and had no reported device loss or uncaptured
GPU errors. The throw check required exactly 100 new bodies. Idle and throw
screenshots were inspected. See the [hardware summary](hardware-summary.json),
[full compressed report](hardware-report.json.gz), and
[browser/driver record](hardware-environment.json).

This was one candidate run with the existing shared HUD changes included. A
separate native build resumed after the clean comparisons and overlapped this
hardware run. Its startup and frame times are therefore observational, with
no matched baseline and no whole-game speedup claim. The local Bazel pack is
144.63 MiB; production uses different CMake packs, so loading times are not
compared with the public deployment either.

Sparse GPU timestamps still point to terrain and the aggregate lighting work:

| Scenario | Unique timing frames | Median terrain raycast | Median lighting/compositing |
| --- | ---: | ---: | ---: |
| Idle | 16 | 0 ms, cached | 13.66 ms |
| Orbit | 9 | 14.88 ms | 19.60 ms |
| Walk | 8 | 13.63 ms | 16.81 ms |
| Throw | 12 | 0 ms, cached | 8.72 ms |

The lighting interval spans multiple passes and object color rendering; it
does not identify one expensive shader. Samples are deduplicated by timing
frame and can include cached boundary records. The
[GPU summary](hardware-candidate-gpu-summary.json) preserves those limits and
the CPU profile hash. Current WASM function names are stripped, so an older
symbol map was not reused to name new CPU hotspots.

Cold engine initialization reached its main loop at about 279 seconds in this
run. Shader and renderer initialization remain major costs beyond the download
dependency improvement. This is not a controlled public-page load measurement.

## Scope

Browser UI measurements isolate presentation from the engine. Request-order
checks isolate download dependencies from shader compilation. Native encoding
measurements isolate CPU preparation from GPU execution. None is, on its own,
an end-to-end page-load or gameplay-FPS result.

Previous hardware runs recorded intermittent Intel D3D12 device hangs. The
successful current run does not establish their cause or prove a fix; see the
[GPU investigation](../gpu-hang-20260924/README.md). Cold shader compilation and
the large GPU terrain/lighting passes remain separate performance costs.

The initial investigation did not publish the working copy. The main-branch
release selection above preserves the unfinished HUD work in its original
checkout and includes only the performance changes.

## Reproduce

Use the same Chromium binary and machine for both sides. Run timing comparisons
without builds or other benchmarks competing for CPU/GPU resources. These
commands reproduce the release fixtures against the original main-branch
files. The historical shared-HUD measurements also require the unfinished UI
sources identified by their recorded hashes; those UI sources are outside
this release. For a new comparison, save its baseline files before editing.

```sh
git show 186e2a68e6a7afaeb1c3c7dfb2e91e0f125e9717:web/index.html > /tmp/voxys-baseline-index.html
gzip -dc docs/performance/article-followup-20260924/ui-bridge-origin-main.mjs.gz > /tmp/voxys-baseline-bridge.mjs

node scripts/performance/measure_startup_requests.mjs \
  --playwright=/path/to/playwright/index.mjs --browser=/path/to/chrome \
  --baseline=/tmp/voxys-baseline-index.html --output=/tmp/startup-requests.json

node ui/benchmark-polling.mjs \
  --playwright=/path/to/playwright/index.mjs --browser=/path/to/chrome \
  --baseline=/tmp/voxys-baseline-bridge.mjs --output=/tmp/ui-polling.json

npm --prefix ui test
node --test scripts/test_data_packs.mjs scripts/test_loading_roller.mjs \
  scripts/test_webgpu_startup.mjs scripts/test_startup_budget.mjs

nix-shell --run 'bazel build -c opt //tests:mesh_path_test'
nix-shell --run './bazel-bin/tests/mesh_path_test --gtest_also_run_disabled_tests --gtest_filter=MeshPathGPUTest.DISABLED_ForestCpuEncodingBenchmark'

node scripts/profile_gameplay.mjs --chrome=/path/to/hardware/chrome \
  --site=/path/to/packaged-site --output=/tmp/gameplay \
  --seconds=10 --scenarios=idle,orbit,walk,throw \
  --profiling=1 --diagnose-first=1
```

The opt-in native benchmark measures actual renderer preparation, buffer
uploads, and command encoding. It discards draw encoders and drains transfers
outside the measured interval, so it measures no GPU rasterization or displayed
frames. Each case warms 20 frames and measures 120. The two large live-tree
cases are stress cases; the mixed case includes retained forest.

For the recorded native comparison, a separate build already occupied Bazel.
Both variants therefore linked the same freshly compiled test object and
cached dependency libraries, replacing only the directly linked MeshPath
object. No cached Bazel outputs were modified. Source/binary hashes, backend, and
linkage notes are in [build metadata](mesh-build-metadata.json). The archived
[compile script](mesh-direct-build.sh), linker response files, and instrumented
baseline source record this machine-specific direct build, including its
zero-return capacity accessor. They require the referenced cached dependencies
and repository Nix compiler wrapper. The Bazel commands above reproduce the
current implementation; an A/B comparison also requires the archived baseline
and matching dependencies.
