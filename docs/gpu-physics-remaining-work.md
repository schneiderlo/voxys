# GPU Physics: Remaining Work

Status snapshot: 2026-07-14.

There is no immediate sector-integration blocker.

## Completed in this pass

- Packed body metadata fixes the dynamic-solver binding while preserving the
  WebGPU limit of eight storage buffers per shader stage.
- Sector-relative positions now flow through collision, sleeping, terrain,
  water, CCD, rendering, character movement, queries, Lockstep, and replay.
- Broad-phase and sleeping-grid keys cover the full signed `i32` sector range.
  Exact sector deltas reject distant cells that share a wrapped key.
- Tests cover collision across a 256 m boundary, distant-sector rejection,
  extreme coordinates, sector rebasing, and replay restoration.
- The browser harness now runs a real GPU physics tick and asynchronous
  readback instead of treating application startup as physics evidence.
- Native Jolt now defaults to its automatic multithreaded worker pool. The
  no-pthreads WASM build keeps browser CPU fallbacks single-threaded.
- Sparse WebGPU worlds now dispatch over the allocated high-water range rather
  than all 131,072 reserved slots. Empty worlds skip the body pipeline and
  expose no physics instances to render culling; released top slots shrink the
  range again at their tick boundary.
- Normal application frames no longer map a telemetry buffer after every GPU
  physics tick. Tests and explicit diagnostics can still enable telemetry.
- Pair compaction, contact lifecycle allocation, narrow-phase bucketing, solver
  color ranges, and solver overflow body ranges no longer scan their full
  capacities on one GPU invocation.
- Worlds of at most 1,024 allocated bodies keep all 32 conflict-free solver
  colors but execute each ordered color sequence inside one workgroup. This
  removes about 300 WebGPU dispatch calls per tick without dropping contacts,
  solver stages, or Soft Step substeps. Larger worlds keep the fully parallel
  per-color path.
- Worlds of at most 64 allocated bodies use an exact serial broad-phase pair
  build and lifecycle pass. This avoids paying the fixed cost of the complete
  radix/grid pipeline when the pair search itself is tiny.
- Worlds of at most 1,024 allocated bodies build and maintain islands inside
  bounded workgroup-cooperative passes. Union, root ranking, deterministic
  events, sleep state, and sleep-grid ranking remain present; the change
  removes small-world dispatch overhead rather than removing island features.
- Browser submission depth is bounded to four frames. A pacing skip discards
  catch-up debt instead of turning one slow frame into six GPU physics ticks.
  Submitted-frame statistics still include the skipped wall time, so the FPS
  display cannot hide pacing stalls.
- DOM middle/right mouse buttons are translated to the engine's GLFW-style
  button numbering, so browser right-click batch spawning now matches native.
- High-degree overflow contacts now average their Jacobi endpoint corrections.
  A regression starts 128 bodies at the exact same position and verifies that
  all poses remain finite and all 128 instances reach GPU render culling.
- Right-click now emits its 128 bodies as a camera-fitted 16 by 8 wall. Four
  forward lanes keep consecutive batches apart instead of creating a complete
  contact graph at one coordinate. Single-body left-click throwing is unchanged.
- Right-click terrain clearance now converts between the camera's local sector
  and absolute terrain coordinates. The previous mixed coordinate spaces could
  place a valid 128-body batch hundreds of metres outside the visible view.
- The direct GPU-resident primitive path now has a texture-readback regression
  that proves a physics body writes visible, nonblack pixels.
- Native benchmark automation can spawn an exact active-body workload, use a
  fixed scripted time step, enforce a minimum throughput, and fail when the
  resident or active count changes during a scenario.
- The Pages workflow pins Emscripten 6.0.1 and includes that version in the SDK
  cache key. Production WASM builds can no longer silently change compiler or
  reuse an incompatible cached emdawnwebgpu port when `latest` advances.

## Current automated evidence

Native and hardware-browser GPU evidence was collected on Linux with an
integrated Radeon 890M:

- Native CMake: all 811 enabled tests pass. Four application GPU tests remain
  intentionally disabled.
- Native Bazel: `//:voxy_native` builds. All 56 non-manual test targets pass
  when invoked individually; the targets affected by the final configuration
  change were rerun.
- WASM/browser: the WASM target builds, all 23 WGSL modules compile, and a real
  WebGPU physics self-test passes. It checks gravity integration, async debug
  readback, contact across a 256 m sector boundary, and rejection of a distant
  sector that aliases the same wrapped broad-phase key.
- Native and WASM platform hashes match: `714369668`.
- Static terrain/water benchmark, with body-body contacts disabled:
  100,000 bodies, 12.971 ms p95.
- Dynamic solver benchmark: 100,000 bodies and 50,000 contacts, 5.751 ms p95.
- Full sparse pipeline sample: 100,000 bodies, 24.869 ms p95. This was measured
  on an integrated GPU, with no terrain and zero generated body pairs.
- Dense regression scene: 400 bodies begin at one position, producing 79,800
  candidate pairs and saturating the 65,536 pair/manifold capacities. The
  native retired-frame result is 3.448 ms p50 and 3.843 ms p95. Broad phase is
  1.623 ms p50 and the compact dynamic solver is 0.422 ms p50.
- The current WASM build was exercised through the production DOM input path in
  uncapped Chromium. It measured 225.8 FPS with 10 bodies and 213.6 FPS with
  650 bodies. An intermediate run measured 223.5 FPS with 138 bodies and
  225.6 FPS with 394 bodies. The final clean build had at most two GPU frames in
  flight during the reported samples.
- The production DOM right-click path was also checked on a real Chrome surface:
  the count increased by exactly 128 and the spawned red bodies were visible in
  the rendered frame.
- Native scripted WebGPU benchmarks measured 246.2 FPS aggregate throughput
  with 10 bodies and 211.4 FPS with 650 bodies. Every scenario kept the exact
  resident count and observed every body active for the full run.
- The focused broad phase, dynamic solver, islands, full GPU physics, direct
  primitive rendering/culling, application, benchmark, configuration, and
  terrain/raycast/blit test targets pass (14 targets). The clean WASM build also
  completes successfully.
- A native 640-body non-overlap sample retired at 2.13 ms p50, about 474 FPS.
  The deliberately pathological version with all 640 bodies at one coordinate
  generated 204,480 pair candidates, saturated the 65,536 contact/manifold
  capacities, and retired at 223.6 ms p50, about 4.47 FPS. The solver now keeps
  that case finite and renderable, but its complete contact graph is not cheap.

The 16.667 ms static and dynamic phase gates pass on this adapter. The sparse
pipeline result is diagnostic only; it is not the plan's discrete-GPU
acceptance scene.

## Required engineering and validation

- Add and measure the true composed acceptance scene: terrain, physical water,
  nonzero body-body contacts, direct rendering, and no normal-frame readback.
- Run that scene on a named representative discrete desktop GPU at four Soft
  Step substeps. The target is 100,000 active bodies at 60 Hz.
- Add browser-specific replay goldens and certify the complete WebGPU physics
  facade. The current matrix certifies Linux/Vulkan components only.
- Run the platform matrix where available: Windows/D3D12, macOS/Metal, and
  Firefox WebGPU. Linux/Vulkan and Chromium WebGPU are current.
- Repair or upgrade around the Bazel 8.5 aggregate-test sandbox issue.
  `bazel test //...` currently fails while materializing external tool files;
  this is distinct from test assertions, and individual targets pass.
- Refresh the machine-readable GPU and multithreaded-Jolt benchmark snapshots
  on the final target hardware after the composed fixture is fixed.
- Re-run the deployed Chromium build on the hardware that reported 10 FPS.
  The automated hardware-browser result above proves the regression scene on
  one Radeon 890M, but it does not replace that machine's playtest.
- Add the 10/650-body browser measurements to an automated regression harness;
  the current uncapped numbers were collected through Chrome DevTools control
  on the test machine and are not yet a CI guardrail.
- Improve or cap the deliberately coincident dense case if it must become a
  supported workload. Its quadratic pair graph remains much slower than the
  spaced right-click workload even though it no longer corrupts body poses.
- Make GPU-dependent CI tests skip cleanly when no adapter is available, or
  provision a software GPU on the hosted runner. The current determinism and
  platform-parity jobs build successfully, then abort in `wgpuCreateInstance`
  for the lockstep/distributed GPU cases; their CPU-only companion tests pass.

## Human gates

- Do not remove Jolt without explicit approval.
- Choose the product direction from comparative, human-verified playtests.
- Do not claim the final 100,000-body target until the composed discrete-GPU
  measurement exists.
