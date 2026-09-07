# LEGO landscape validation — 7 September 2026

Source baseline: `c5070265aae8e41a3ee59813f333967296a75a45`.
The changes and this evidence are committed together; reports were captured
from the release WASM build in the working tree. See Git history for the release.

## Hardware and method

- AMD Radeon 890M / Strix, PCI `1002:150e`, integrated GPU.
- Mesa `26.0.8-1ubuntu0.3`, Linux/Wayland, Chrome `152.0.7977.82`.
- User's Vulkan Gaming configuration: `--ozone-platform=wayland
  --enable-features=Vulkan --enable-unsafe-webgpu`.
- Fresh temporary browser profiles; the personal Gaming profile is untouched.
- 1920×1080 and 960×540, device pixel ratio 1, normal scene presentation pacing.
- Full original 8192×8192 terrain, default URL, ordinary application startup.
- Emscripten 6.0.1, release build, fixed 512 MiB WASM heap.

`final-1080/` and `final-540/` contain screenshots, sampled frame times and
application state. Browser animation callbacks measure presentation cadence;
application telemetry measures CPU work; GPU timestamps measure completed GPU
execution including physics. They are separate clocks. Telemetry collection
adds a small browser-side cost. Tests ran without a simultaneous build or GPU
fixture. Startup times use a local asset server, not a cold Internet download.

## Startup failures investigated

The original Pages configuration selected SwiftShader WebGPU without compatible
canvas backing. Chrome reported `Could not find SharedImageBackingFactory` and
`Unable to create shared image`, then destroyed the device. Pending queue work
reported `A valid external Instance reference no longer exists`.

`presentation-before.json` reproduces that chain with sixteen green canvas
clears: no application, terrain shaders or WASM heap. The original application
fails identically (`startup-before-swiftshader.json`) but starts on AMD Gaming
(`startup-before-gaming.json`). This isolates the original failure from terrain
memory usage. A consistent Vulkan configuration still produced black headless
captures on this Chrome version; the strengthened image gate rejects those.

The window-backed configuration uses Xvfb, SwiftShader ANGLE and SwiftShader
Vulkan. `startup-window-swiftshader.json` records the initial full-app pass.
Pages now uses this configuration and **the startup test is blocking**. It checks
submitted frames plus completed GPU timestamp readback, error-free console, hidden loading overlay, correct terrain
and heap dimensions, and a nonblank landscape screenshot.

A second failure appeared during compound-brick development:
`compound-before-swiftshader.json` records a GPU-process SIGSEGV on the first
submission. Replacing only the narrow-phase shader with the baseline made the
full app pass (`compound-baseline-shader-diagnostic.json`; diagnostic only).
The generic compound dispatcher was reachable from all ten collision classes.
The final shader specializes sphere, capsule and polyhedron compound routines
and calls them only from the four classes that can contain a brick box.
`final-swiftshader.json` / `.png` records an unmodified full-app pass, 79.4 seconds
including software startup and capture. This evidence implicates reachable
shader complexity; Chrome did not provide a stack proving the precise internal
compiler defect. No feature, collision child or startup assertion was disabled.
Software startup timing is **not hardware FPS evidence**.

## Changes checked

- Grouped rendering now initializes from the incoming scene configuration.
- Browser canvas dimensions are preserved. Normal play respects scene VSync;
  explicit throughput benchmarks still use the immediate loop.
- Adjacent long joints use a running bond. World tags, four-chunk upload bound,
  fixed 4 MiB cache and geometry independent of cache residency are preserved.
- Distant material normals blend toward broad terrain relief to reduce sparkle.
- Native shader validation now rejects errors that older tests only logged.
  This caught and fixed runtime indexing of the constant palette on older Naga.
- Original primitive face winding and cylinder caps now face outward; GPU pixel
  readback tests cover both cap directions and the resident brick stud draw.
- Playground uses the existing **WebGpuSoft** world. Box and stud child geometry
  participates in narrow-phase contacts, terrain contacts and spatial queries.
  No Jolt playground or CPU transform mirror drives motion or rendering.

## Visual and gameplay evidence

World captures cover walking, flying, cliffs at close range, shoreline, distant
views, grouped/single K comparison and long-distance teleports back into the
cache. Physical keyboard events verify walking and K. Geometry/contact tests
cover stud edges, gaps, terraces, chunk boundaries and capsule support.

Playground captures cover a three-level tower, resting target, ball impact,
success and replay. Enter placement and P exit/re-entry are exercised through
physical keyboard events; buttons use browser mouse input. The stress run fills
48 brick slots, waits for all to sleep, launches eight balls, checks wake-up and
pool bounds, then resets. Success requires a ball contact and a target drop of
one brick body height or 1.4 units of displacement.

Limits: 48 bricks, eight playground balls, one target, one fixed pad, 48 pooled
dust particles. A brick contains one box and up to eight studs; broad-phase uses
one parent, narrow-phase tests at most 81 child pairs and retains four contacts.
Rendering reads resident GPU poses with a maximum 192-byte changed-ID upload.
Only bounded 10 Hz state readback supports the HUD and snapping.

The existing 32 landscape-ball allowance remains separate. Terrain is still a
heightmap; its visible bricks cannot be individually removed. Construction uses
a free camera. Character walking onto dynamic construction is not implemented.

## Hardware frame times

Milliseconds, median / 95th percentile. Lower is better. These short captures
are evidence for this device and scene sample, not a guaranteed frame-rate floor.

| Resolution and workload | Presentation | Application CPU | GPU execution |
| --- | ---: | ---: | ---: |
| 1920×1080 idle | 17.80 / 19.60 | 0.30 / 0.60 | 1.19 / 2.21 |
| 1920×1080 walk | 17.70 / 19.30 | 0.30 / 0.70 | 6.69 / 7.54 |
| 1920×1080 cliff-close | 18.00 / 19.60 | 0.60 / 1.10 | 1.76 / 2.03 |
| 1920×1080 distant | 18.00 / 19.80 | 0.70 / 1.00 | 2.55 / 2.85 |
| 1920×1080 48 bricks + 8 balls | 18.70 / 23.30 | 2.90 / 3.60 | 4.36 / 4.60 |
| 960×540 idle | 17.90 / 19.90 | 0.30 / 0.80 | 1.03 / 1.47 |
| 960×540 walk | 17.80 / 19.10 | 0.40 / 0.70 | 2.94 / 3.37 |
| 960×540 cliff-close | 18.00 / 19.30 | 0.60 / 0.90 | 1.04 / 1.27 |
| 960×540 distant | 17.80 / 19.10 | 0.60 / 0.90 | 1.62 / 1.79 |
| 960×540 48 bricks + 8 balls | 19.00 / 23.30 | 2.90 / 3.70 | 3.36 / 4.42 |

The walking sample lasts ten seconds; idle lasts twelve seconds; each teleport
view lasts six seconds; the full-pool impact sample lasts ten seconds. Hardware
reports retain the individual samples and GPU timestamp age. A static camera
uses the existing terrain cache, so idle GPU time is much lower than walking.

## Memory and limits of acceptance

Live WASM allocation just after startup is about 404.2 MiB, within the unchanged
512 MiB heap. Reports include JS heap and cumulative GPU buffer allocation bytes
separately. These are not a combined residency measurement or a transient
malloc high-water mark. Both source heightmaps retain byte-for-byte parity.

Some distant geometry sparkle and tiny bright cliff-edge pixels remain visible;
this is not a claim of complete antialiasing. Stud-to-convex contacts use the
engine's polygonal cylinder approximation; sphere contacts and terrain studs
use analytic cylinders. Mobile, other hardware vendors and prolonged thermal
soak have not been validated. The sound graph is exercised after user input;
actual speaker output has not been independently recorded.

The explicit LEGO World, shoreline, smooth terrain and RIDGEBREAK routes also
passed full hardware startup using their normal URLs and configured capacities
(`route-*.json` / `.png`). No reduced-capacity or benchmark URL was used.

## Reproduce

```sh
# Build release WASM, then copy web/* and bin/voxy_wasm.* into a serving directory.
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
  VOXY_SMOKE_JOURNEY=/tmp/lego-world \
  VOXY_SMOKE_PLAYGROUND=/tmp/lego-playground \
  node scripts/smoke_integrated_wasm.mjs /tmp/voxys-web default
VOXY_SMOKE_GPU=swiftshader-window xvfb-run -a \
  node scripts/smoke_integrated_wasm.mjs /tmp/voxys-web default
```

Chrome, Node 22+, Python/Pillow and Xvfb are required. `final-native.txt`,
`final-playground-native.txt`, `final-gpu-shaders.txt` and `final-wasm-assets.txt`
retain native, real GPU shader and packaged-asset verification output. The
native selected suite passed 110 tests, with one opt-in benchmark skipped;
15 focused renderer/physics tests passed after compound specialization.

## First CI release attempt

The blocking smoke correctly stopped deployment: the engine had completed GPU
work without device loss, but its decorative loading roller still covered the
canvas after three minutes. The roller capped every animation delta at 50 ms;
on CI's very slow software GPU, 27 frames were insufficient to finish its spring.
The roller now interpolates by elapsed time and completes after two seconds even
if animation frames stop. Engine initialization must still finish before landing
begins. Three production-class regressions cover waiting, delayed frames and no
frames. The startup gate's timeout, resolution and assertions are unchanged.
`loader-fixed-swiftshader.json` and a further AMD gameplay run passed locally.

The same CI attempt reported two native LLVMpipe segmentation faults during
simultaneous cold-cache tests. Other playground cases, including ball impacts
and replay, passed. Exact Mesa 25.2.8 / LLVM 20.1.2 Vulkan tests passed locally
both singly and concurrently; a restricted AVX run also passed. Early local
attempts selected OpenGL and were rejected as reproduction evidence. The cause
is not yet established. Native CI retains every assertion and now captures
cold-driver debugger backtraces on failure; these diagnostic retries do not
change the failed gate's result.


## Native cold-start timeout isolated

The second CI attempt reproduced both faults. Matching Ubuntu Mesa debug
symbols locate the driver crash in `lvp_execute_cmd_buffer`, at the read of
`pipeline->type` during pipeline binding. This is command execution, not an
LLVM shader-compiler stack. `ci-second-native-backtrace.txt` retains the trace.

The pinned wgpu-native 22 backend has a concrete timeout defect:
[`CLEANUP_WAIT_MS` is 60000](https://github.com/gfx-rs/wgpu/blob/v22.1.0/wgpu-core/src/device/mod.rs#L40),
but [`Device::maintain`](https://github.com/gfx-rs/wgpu/blob/v22.1.0/wgpu-core/src/device/resource.rs#L436)
ignores the `false` result from the fence wait and unconditionally retires
submissions at the requested index. Cold software shader compilation can take
longer than that wait. Recycled command storage is then still in use by Vulkan.

Constraining the unchanged test to two CPU cores, AVX, two LLVMpipe workers
and 20% process duty reproduced a SIGSEGV locally. The new tests now use
nonblocking device polling to inspect the actual completion fence, with a
bounded 110-second wait and the existing 120-second CTest deadline unchanged.
With identical 20% duty for the first 85 seconds, the old binary crashed
(SIGSEGV) after 88.2 seconds and the fixed test passed in 107.6 seconds.
`unsafe-poll-slow-native.txt` retains the negative control. The focused
22-test AMD suite also passed. See `safe-poll-slow-native.txt` and
`safe-poll-amd.txt`. This changes test synchronization, not the physics, and
does not disable any test or shader path. Legacy blocking polls elsewhere in
native tests and the native benchmark/screenshot paths still require care on
very slow devices; the browser application does not use this native API.

The opt-in `scripts/reproduce_lego_native_timeout.py` records the exact adapter,
process result and timing without changing system settings. Set
`VK_DRIVER_FILES` to Mesa 25.2.8's LLVMpipe ICD JSON and provide its matching
LLVM 20.1.2 library path, then pass the native test executable and output log.
It limits only its own process tree to two cores / two software workers and
restores full duty after 85 seconds. It terminates after 120 seconds. Ordinary
hardware tests and the CI suite do not apply this artificial slowdown.
