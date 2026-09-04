# GPU-baked periodic gradient lookup

This change targets the expensive material path, including background refreshes
while the camera moves. It does not depend on freezing the camera or reducing
resolution, terrain detail, texture filtering, water quality, or physics.

## Production change

The renderer's periodic gradient hash has only 16 x 16 distinct integer lattice
inputs. Previously every noise evaluation rebuilt four gradients using eight
scalar sine operations, then performed the usual quintic interpolation.

`PeriodicGradientLut` runs the original WGSL hash on the rendering device once.
Two adjacent RGBA32Float texels store a cell's four corner gradients. The 32 x 16
texture contains 8 KiB of texel data (driver allocation overhead is additional).
Each subsequent noise evaluation uses two unfiltered texture loads. Domain
warping, interpolation, anti-tiling, terrain layers, normal maps, and optical
shading remain present. CPU trigonometry, half floats, and filtered lookup are
not used. The reference hash remains available through the pipeline override
`USE_PERIODIC_GRADIENT_LUT=false` for same-shader A/B tests.

The table is renderer/device-owned, moves with `BlitPath`, and is released on
shutdown. One queue submission initializes it before rendering; there is no
per-frame upload, map, completion callback, or CPU wait. Binding 19 adds one
sampled texture and no sampler/storage buffer to the fragment layouts. The
larger layout has 15 sampled textures, within WebGPU's 16-texture baseline.

## Timing repair

The existing lighting query pair now begins before a background refresh and
ends after composition. Previously the background pass explicitly suppressed
timestamps, so its cost was missing from `Render GPU`. Each query index is
written once: a refreshed background writes the beginning, and composition
writes the end. A reused background leaves both timestamps on composition.
Stage indices and existing clients remain compatible.

This is still not a complete queue/presentation measurement. In particular,
underwater particles, one-time initialization, browser/compositor scheduling,
and completion latency are not all represented by that lighting interval.

## Validation

```sh
python3 tools/test_periodic_gradient_lut.py
node scripts/test_periodic_gradient_lut.mjs
```

The Python suite checks wrapped indexing, all four packed corners, negative
coordinates, interpolation, large fp32 lattice coordinates, resource bindings,
move/reset wiring, and timestamp coverage. It is not a GPU execution test.

The Node test requires Node 22+ and Chrome (`VOXY_TEST_CHROME` overrides its
executable). It compiles both full production modules and the embedded bake,
and creates six production render pipelines (background, cached composition,
and water, each with both specializations). It evaluates 65,539 points through
both original and LUT-specialized compute and fragment pipelines. Maximum
absolute output error must be <= 1e-6. Missing WebGPU, shader errors, validation
errors, and nonfinite values fail the test. The result is saved to
`periodic-gradient-lut-report.json`.

The optional AB/BA GPU timing test is a 12-noise-call kernel, not the composed
renderer. Software adapters are acceptable for correctness testing only.
Neither source-level operation removal nor this microbenchmark establishes an
FPS improvement on an Intel Gen-12LP or any other player's machine.

Before accepting a performance claim, compare the complete moving visible
scene on the same hardware/build settings, including p50/p95/p99 frame time,
background shading, image comparison, and queue pacing. A computation-stage
bake and fragment-stage consumer can expose driver-specific floating-point
behavior; the fragment differential test is intentionally required as well as
the compute test.

## Recorded execution: 2026-09-04

- Production CMake WASM compile/link with Emscripten 6.0.1 passed on implementation
  commit `2add98515608b1bb377ecff2397b475fea2b3e6f` in workflow run
  `33922283136`, job `101183119189`. Later commits change tests/docs/workflow
  only, not the compiled C++ or production WGSL.
- Five CPU regression tests passed locally and in CI.
- The final Chrome/SwiftShader differential run passed on test commit
  `6118372e5cb6f1e528fd596d6e243b0c770fc6bf`: workflow `33922643706`, job
  `101184229772`, artifact `9955632745`. Four modules and six production render
  pipelines compiled. Each compute/fragment comparison checked 262,156 float
  words over 65,539 input points; both had zero differing words and zero maximum
  absolute error.
- An earlier GPU run passed its comparisons but failed during Chrome profile
  cleanup. The test now waits for process closure and retries directory removal;
  the final job above exits successfully rather than hiding cleanup failures.

Performance is NOT accepted by these tests. The final software-only noise
microbenchmark was slower for the LUT in five of six pairs. It does not establish
an Intel hardware win; the raw report retains every pair. No whole-engine FPS
improvement, full-scene image equivalence, native build, or Intel/Windows GPU
validation is claimed. Keep this candidate in draft until target-hardware
performance and visual acceptance are available.
