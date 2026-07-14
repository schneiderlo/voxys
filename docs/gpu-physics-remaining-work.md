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

## Current automated evidence

Native GPU evidence was collected on Linux with an integrated Radeon 890M.
Browser evidence used headless Chromium with its SwiftShader WebGPU adapter:

- Native CMake: all 810 enabled tests pass. Four application GPU tests remain
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

## Human gates

- Do not remove Jolt without explicit approval.
- Choose the product direction from comparative, human-verified playtests.
- Do not claim the final 100,000-body target until the composed discrete-GPU
  measurement exists.
