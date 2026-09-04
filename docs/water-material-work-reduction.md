# Terrain and water work reduction

Base: `d9a6a399a9cafdb986be992413de8322ac879b8c`.
Tested implementation: `8da3c7b94a1d7909fa23db2373c8615b1dc31427`.

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
material coordinates/weights, and executes material and water fragment fixtures
with mipmapped textures. It checks shoreline boundaries, negative/large
coordinates, refraction precedence, foam and total internal reflection.
Missing WebGPU fails rather than passing. `VOXY_TEST_CHROME` selects the browser
executable. Node 22 or later and the baseline Git history are required.

## Executed validation

The final validation run is `33927794935`; GPU job `101200043897` passed.
Raw report artifact: `9957445524` (`water-material-final-gpu-report`).
Source/patch artifact: `9957429613` (`water-material-final-source`).

Eight CPU regression groups pass locally and in CI. Chrome/SwiftShader
(software adapter, not Intel hardware) compiled ten production render pipelines
and executed the differential tests. Across 1,024,036 compared float words,
maximum absolute error was 1.1920928955078125e-7. Water fragment outputs matched
exactly; the clipmap foam compute comparison had a maximum difference of
6.329545776395662e-9. Eight begin/middle/end timestamp paths also passed actual
WebGPU validation, including background refresh plus underwater particles.

## Warmed synthetic timings, not game FPS

Each row is a 64x64 synthetic fragment fixture on the same SwiftShader device.
Three warmups precede six paired samples with alternating baseline/candidate
order. First-draw timings are retained separately in the raw report because
software-driver compilation can distort them. The table uses the median of
those six samples. No hardware performance threshold is enforced by this CI.

| Fixture | Baseline ms | Candidate ms | Time change |
| --- | ---: | ---: | ---: |
| Mixed terrain | 21.947599 | 21.916972 | -0.14% |
| Coherent shore | 6.736732 | 6.735634 | -0.02% |
| Coherent upland | 8.683039 | 8.535672 | -1.70% |
| Coherent rock | 10.712954 | 10.755640 | +0.40% |
| Legacy water, above | 2.627939 | 2.681315 | +2.03% |
| Current clipmap water, above | 2.788736 | 2.412479 | -13.49% |
| Legacy water, below | 3.054386 | 3.061419 | +0.23% |
| Current clipmap water, below | 3.488872 | 3.031999 | -13.10% |

The current water path was faster in all six pairs for both above/below views.
Terrain differences are small and do not establish a meaningful terrain win.
The positive changes are reported rather than hidden. These software-only
fixtures do not establish an Intel/Windows speedup, full-scene image equivalence,
or recovery from 10 to 150 FPS. No native build or complete game benchmark is
claimed here. Same-hardware moving-scene visual and p50/p95/p99 acceptance
remains required before treating this as a verified product performance fix.

The PR contains only the final implementation, tests, documentation and a
read-only regression workflow. Its temporary write-enabled preparation workflow
and temporary patch transport files were removed from the final diff.
