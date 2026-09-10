# Environment convolution — producer checkpoint

**Five checks pass on native Vulkan and browser WebGPU.** The final Bazel,
CMake and browser runs return identical bytes for all 14,274 sampled RGBA
values. This is a validated lighting producer, not an integrated game-view fix.
Read the [implementation contract and next integration steps](design.md).

Baseline: branch `codex/salvage-implementation`, HEAD
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911`, with authorized uncommitted work.
`summary.json` records exact frozen input/evidence hashes. No independent or
owner review, gate acceptance or commit is claimed.

## Checks and results

All three executors run the same `tests/test_environment_lighting.cpp` and
`shaders/environment_lighting.wgsl`. Native: Radeon 890M / RADV STRIX1 / Vulkan.
Browser: hardware WebGPU in Chrome; exact browser/adapter fields are in
`browser/report.json`. There are no skipped tests, browser exceptions,
uncaptured GPU errors or device-loss events. Native validation scopes pass.

| Check | Actual result |
|---|---|
| Constant HDR at every small-fixture reflection mip/diffuse texel | Maximum absolute RGB error 0 |
| Directional sky, sharp reflection | Maximum error 0.0013566; limit 0.002 |
| Directional diffuse `E/pi` | Maximum error 0.00104105; limit 0.006 |
| Roughness-one reflection | Maximum error 0.000488281; limit 0.006 |
| Both sides of cube edges/corners | Maximum difference 0.0000013113; limit 0.002 |
| BRDF vs independent hemisphere quadrature | Maximum coefficient error 0.00150659; limit 0.004 |
| CPU reference convergence, doubled grid | Maximum difference 0.0000728845; limit 0.0002 |
| White specular energy over entire test LUT | Maximum A+B = 1; limit 1.003 |
| Release before command submission | Encoded producer and consumer still return correct HDR values |
| Abandoned encoder, then bake/re-bake | Correct values from each source; output views retain identity |
| Invalid size/sample setup | Refused; previously valid views retained |
| Default allocation accounting | 1,228,944 requested texture/uniform bytes |

The reflection integration reference uses uniform incoming-hemisphere midpoint
quadrature at 256×512 and 512×1024, independently of the shader's half-vector
importance sequence. Nine LUT texels span view cosine and roughness. Native
and browser comparison scripts verify identical request bytes, finite output,
complete captures and bounded error for every read value, not screenshots.

The readbacks contain 2,398 constant-fixture values, 10,846 directional values,
1,024 BRDF values, and two sets of three re-bake values. The small constant
fixture covers all texels/mips at 16² specular / 4² diffuse / 16² LUT, 1,024
samples. The directional fixture uses 32² / 16² / 16² and 4,096 samples;
the independent BRDF fixture uses a 32² LUT and 4,096 samples. Default resource
allocation is exercised, but the full default-resolution bake is not yet an
application/performance measurement.

`native-final/` is the final focused Bazel execution. `cmake-native/` is the
configured combined CMake binary filtered to these five cases.
`bazel-combined-tests.log` verifies that the required combined Bazel target
also registers and runs these five cases. This does **not** claim its entire
suite passed. `native-browser.json` and `native-build-systems.json` retain all
cross-runtime comparisons.

## Builds and tooling regression

- Normal native Bazel application and focused test target build: pass.
- Combined native Bazel and CMake test targets build: pass.
- Normal CMake WASM application build: pass; the new WGSL is picked up by
  existing shader packaging.
- Isolated browser build with strict warnings, actual shared C++ and WGSL:
  pass. Its new `--kind environment` mode preserves the existing default.
- Fresh default mesh-diagnostic build and hardware browser run: all five
  existing tests and eighteen readbacks pass. Results are retained in
  `mesh-tooling-regression/`.

The original mesh shader and all scene consumers remain unchanged. The new
producer has not replaced terrain/water environment data or the current asset
registry. No full application screenshot comparison was repeated for this
unbound producer.

## Retained failures

`native-attempt-01/` retains the first GPU failure, its exact shader/test source
and all readbacks. The -Z cube face initially used the wrong horizontal sign.
Directional error reached 0.696, while constant/BRDF/lifetime checks passed.
The corrected shader passes the unchanged directional tolerances. `native/`
is that first corrected execution, before explicit portability casts were
added to the test source.

The first native compile rejected an implicit signed result from
`std::bit_width`; explicit checked-range conversion fixed it. The first browser
compile rejected 64→32-bit mapping-size conversion and implicit float→double
promotion in test code. Those diagnostics and their build manifest are retained
under `browser-build-attempt-01/`. Sizes now use `size_t` derived from the bounded
fixture vector, and numerical-reference conversions are explicit. Compiler
warnings were not disabled and numerical thresholds were not relaxed.

## Reproduction

Run from the repository root. Use new output directories to preserve evidence.

```bash
nix-shell
bazel build -c opt //tests:environment_lighting //tests:voxy_tests //:voxy_native
mkdir -p NEW_NATIVE_OUTPUT
VOXY_ENVIRONMENT_CAPTURE_DIR=NEW_NATIVE_OUTPUT bazel-bin/tests/environment_lighting
cmake --build build-salvage-native --target voxy_tests --parallel 4
build-salvage-native/bin/voxy_tests --gtest_filter=EnvironmentLightingTest.*
python3 scripts/build_mesh_diagnostics.py --kind environment \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output NEW_BROWSER_PACKAGE
```

Outside Nix, run the installed hardware browser with the bundled Node path:

```bash
/home/modkin/.nix-profile/bin/node scripts/run_mesh_diagnostics.mjs \
  NEW_BROWSER_PACKAGE NEW_BROWSER_OUTPUT environment
python3 scripts/compare_environment_diagnostics.py \
  NEW_NATIVE_OUTPUT NEW_BROWSER_OUTPUT NEW_COMPARISON.json
```

The preserved browser package was `/tmp/voxys-environment-browser-r02`.
`browser-build/` contains its build record/logs; hashes identify the actual
HTML/JS/WASM. Rebuild if that temporary package no longer exists. Omit
`--kind environment` and the runner's final `environment` argument to reproduce
the existing mesh diagnostics. That regression package was
`/tmp/voxys-mesh-environment-regression-r01`.

## Still required

Bind the producer to the real material path with correct owner lifetime,
submission ordering, Fresnel weighting and non-wrapping LUT sampling. Then
compare matched native/browser game views, including the map-free diagnostic.
Check default-resolution bake cost and convergence for narrow bright lights.
Normal variance, complete asset shading, actual moving/bright/dark/wet/glass
views, HDR/water ordering, shadows and the authored cove remain open. The parent
ASSET-06/LOOK-01/REND gates cannot be checked from this isolated result.
