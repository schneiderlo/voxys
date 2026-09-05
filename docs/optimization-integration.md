# Optimization branch integration

## Scope

User-directed integration of all existing branches into main. Production
history is preserved through merge commits; no branch history is rewritten.

Pinned starting points:

- Main (already includes PR #2): `3f06f364ce65a01da2abedc6a880c908903eee3a`.
- Water/material PR #4: `84fba7856cc19257f985e3f1ef40f8b4ce545751`.
- Motocross prototype: `19134562d032945072ddd463b021f2d93d886737`.
- Merged implementation before the additional shading round:
  `b0cf3a4179fafb2a235b8dbd26b06846ebb9ef45`.

The temporary validation branch was already an ancestor of main. Its obsolete
write-enabled workflow and binary patch transport files are removed, together
with this integration's temporary preparation workflows/scripts. Permanent
regressions have read-only permissions and now run on relevant main pushes.

## Conflict resolutions

The blit merge retains both the GPU-generated gradient lookup bindings and the
complete background/composition/underwater-particle timing interval. Each
endpoint is written once; empty middle timestamp descriptors are omitted.

Build/configuration merges preserve the existing authored terrain, 8K
heightmap and 2048-pixel macro texture startup-memory fix. RIDGEBREAK is opt-in:
`?experience=ridgebreak` in the browser, or `--config ridgebreak.cfg` natively.
Course generation and alpha-channel material classification are gated by that
mode; ordinary JPEG alpha does not turn the terrain demo into an all-soil track.
The native Wreckwater command-line options are retained.

The existing broad-phase, physics, material layers, normal maps, anisotropic
filtering, screen resolution and FFT cadence are not reduced. This is not a
material-atlas redesign or a temporal reprojection implementation.

## Additional shading round

- Entire refraction-depth/UV/fallback work is bypassed for underwater rays
  whose final output uses total internal reflection. Above-water scattering
  work stays in the above-water branch.
- Procedural seabed caustic samples are skipped beyond their existing zero
  contribution endpoint (360 units).
- Backlit terrain specular returns zero before evaluating the remaining BRDF.

These are equivalent-result work reductions, not an accepted hardware speedup.
The direct comparison of this round against the integrated baseline returned
zero differing words in 1,126,436 compared float words on Chrome/SwiftShader,
including critical reflection angles, fade boundaries and optional course
materials. Combined changes against the original pre-LUT scene compared
1,093,668 words, with maximum absolute error 1.1920928955078125e-7. Both runs
validated eight timestamp paths. Fifteen CPU regression groups pass.

Evidence: successful run `33933604201`, artifact `9959417418`
(`optimization-round-reports`). The production WASM integrated target also
compiled and linked in run `33933267595`, job `101216133627`.

## Performance limits

SwiftShader is a software adapter. Warmed synthetic timings are mixed, not
proof of a faster Intel/Windows game. Against the original scene shader,
current water fixtures improved about 7% above / 3% below, while mixed terrain
was about 7% slower and several other fixtures also regressed. In the isolated
new round, many differences were small; the critical-angle fixture was about
9% slower. All raw paired samples are retained. None of these figures is a
whole-frame FPS claim or proof of recovery from the reported 150-to-10 drop.

## Build integration fixes

Native integration exposed a standalone probe test missing protocol objects;
its CMake target now links those implementations. The vendored X11-only GLFW
fallback now signals that Wayland native-access symbols are unavailable,
without disabling Wayland in system GLFW packages. Pages restores tracked
third-party build definitions after cache extraction, requires JS/WASM/data
artifacts, and verifies both scene configurations' required preload assets.

## Reproduce

```sh
python3 tools/test_periodic_gradient_lut.py
python3 tools/test_water_material_work.py
node scripts/test_periodic_gradient_lut.mjs
node scripts/test_water_material_work.mjs
VOXY_REFERENCE_REF=b0cf3a4179fafb2a235b8dbd26b06846ebb9ef45 \
  node scripts/test_water_material_work.mjs
```

The application smoke script accepts a directory containing `web/*` plus
`voxy_wasm.js`, `.wasm` and `.data`: `node scripts/smoke_integrated_wasm.mjs DIR`.
It uses a small viewport and body reserve to verify startup, not performance.
Full-resolution moving-scene visual and frame-time validation on the target
Intel adapter remains outstanding.
