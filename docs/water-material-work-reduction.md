# Terrain and water work reduction

Base: `d9a6a399a9cafdb986be992413de8322ac879b8c`.

This change is independent of the unmerged periodic-gradient LUT candidate.
It adds no lookup texture, GPU allocation, sampler, storage binding, reduced
resolution, reduced texture filtering, reduced physics, or new quality tier.

## Production changes

* Retain the original texture-coordinate, noise and finite-difference mip
  gradients. The shared-gradient experiment was removed after warmed
  software comparisons regressed, including coherent terrain fixtures.
* Do not evaluate cove-local effects where their existing masks are exactly
  zero; fully rock-covered pixels do not classify absent grass/sand/soil.
  Do not evaluate run-up noise outside its bounded wet transition. These are
  zero-contribution branches, not distance-based detail removal.
* Treat scene-color refraction, procedural fallback and (legacy) infinite-floor
  override as exclusive paths, preserving the original override precedence.
* Split total-internal-reflection and transmission work. Do not sample an
  internal-reflection seabed for a transmitting pixel, or sample transmitted
  environment for a totally internally reflecting one.
* Open-water foam does not run shore-only texture/noise work after the original
  depth windows reach zero. The legacy wreck-contact window is retained.
* Zero-contribution underwater caustics exit before texture sampling.
* Time the recurring background, water/composition and underwater-particle
  passes as one lighting interval. Every successful rendering path writes
  its beginning and end once. Initialization (sky bake), presentation and
  browser scheduling are outside this interval. Stage slots stay compatible.
* Export render sample frame/age, named stage times, and the actual queue limit
  in telemetry. The overlay no longer displays the incorrect hard-coded `/4`.

Terrain and background caches retain their existing view/material invalidation
rules. In particular, moving the camera still refreshes view-dependent depth:
this patch does not reuse stale images to manufacture a high FPS number.
It is not a world-space material-atlas or temporal-reprojection implementation.

## Tests

```sh
python3 tools/test_water_material_work.py
VOXY_REFERENCE_REF=d9a6a399a9cafdb986be992413de8322ac879b8c \
  node scripts/test_water_material_work.mjs
```

The Python suite is a CPU/structural check, not shader execution. The Node
suite uses Chrome WebGPU, compiles actual production entry points, compares
material coordinates/weights, and executes material and water fragment fixtures with
mipmapped textures. It checks shoreline boundaries, negative/large coordinates,
refraction precedence, foam and TIR. Missing WebGPU fails rather than passing.
`VOXY_TEST_CHROME` selects the browser executable. Node 22 or later is required.

The synthetic fixtures include timings for diagnostic use. A software adapter
is valid for these correctness checks, **not** evidence of Intel/Windows or
whole-game FPS. Full-scene visual comparisons and same-hardware moving-scene
p50/p95/p99 captures remain the performance acceptance gate.
