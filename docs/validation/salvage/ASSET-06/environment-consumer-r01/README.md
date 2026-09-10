# Filtered environment lighting in the actual asset scene

2026-09-08. The surface-detail scene now uses the validated environment filter.
Root reviewed 24 actual native/browser images: four views at each of three
detail levels in each runtime. Cream bevels and steel hubs have smoother
lighting; the previous strong environment bands are reduced. Geometry,
textures, camera recipes and exposure remain the same. Far silhouettes remain
coarse, the pontoon still needs shading refinement, and contact shadows and the
authored cove remain unfinished. This is a scoped integration checkpoint,
not ASSET-06, LOOK-01, independent review or owner approval.

Baseline: `codex/salvage-implementation`, HEAD
`7f28fabfabf3d63726f6cfa1001c3ce1ed557911`, plus authorized working changes.
`summary.json` records exact source/evidence hashes. No gate passed or commit
was created. Historical producer and material reports remain unchanged.

## Implementation

`MeshPathConfig.filteredEnvironment` defaults to false. The explicit
`[game] asset_fixture_lighting = "filtered"` setting in
`salvage_metric_fixture.cfg` enables it for the existing surface-detail route.
Other routes retain legacy lighting. Invalid or unscoped settings are refused.

Each admitted fixture generation owns one filter, shared by all its materials;
guide rendering does not allocate another. The default 128² specular cube,
32² diffuse cube, 128² BRDF LUT and bake parameters request **1,228,944 bytes**.
The actual three-part scene requests **6,714,416 bytes**, within the unchanged
16 MiB owner and 48 MiB resident ceilings. This is requested resource storage,
not driver allocation size or product performance.

The first frame encodes the bake after sky preparation and before mesh draws.
Only submission acknowledges the cache; discarded encoding retries. Source
view replacement invalidates it; depth-only replacement, camera movement,
guides and Reset retain it. The existing generation's submission/retirement
fences cover the filter. `environmentReady` means submitted and usable in queue
order, not that GPU execution has completed. Future in-place source changes
must explicitly invalidate the cache. Global sharing across renderers remains
rendering work.

The shader samples directional diffuse E/pi, the roughness-selected specular
cube and a clamped BRDF LUT. Integrated specular energy is F0*A+B; the remaining
nonmetal energy weights diffuse. Direct light, fog and presentation remain
unchanged. This does not solve HDR/water composition or cast shadows.

## Executed checks

- Native focused tests: eight mesh, twenty fixture/lifetime and sixty-one
  configuration cases pass. All 89 also pass in the CMake combined executable.
- Two new consumer GPU tests cover white metal/plastic energy, discarded
  encoding, cached reuse and changed environment input. Five 64×64 readbacks
  match native/browser byte-for-byte; `consumer-parity.json` records hashes.
- Native and WASM application builds pass. Existing WASM physics comparison
  warnings are retained in the build log; no warning-free claim is made.
- `native-lod1..3` and `browser-lod1..3`: all 24 capture checks pass; root
  inspected every PNG. The browser UI overlay is expected.
- `browser-controls`: all fourteen control/resize/flight/Reset/re-entry stages
  pass. Each retains one bake and the same reservation. Drained Leave reports
  zero environment and owner bytes. The original world loads afterward.
- `browser-loss`: an actual destroyed GPU device in a disposable test browser
  reaches exceptional teardown without uncaptured validation errors.
- `browser-legacy-mesh`: the existing five-test/eighteen-readback regression
  passes with the new shared shader/binding layout.

`native-current-legacy` and `native-filtered-factors` retain two additional
successful comparison captures. They have not received a separate visual
review and are not used to expand the visual claims above. Following owner
feedback, further repeated full capture matrices stop unless a relevant change
or unresolved defect justifies them.

The first consumer attempt incorrectly used the 2D texture upload helper for
six cube faces. Its failed log and exact source remain in
`native-consumer-attempt-01`. A dormant zero-initialized fallback cube fixes the
legacy binding layout without changing the helper or loosening checks.
The first control launch failed at localhost binding in the sandbox; the
subsequent authorized local browser run passes. Both logs are retained.

## Reproduction

Run from the repository root, using fresh output directories. The snapshot
under `sources/` identifies the implementation tested here.

```bash
nix-shell
bazel build -c opt //:voxy_native //tests:mesh_path_test //tests:salvage_asset_fixture //tests:config
mkdir NEW_NATIVE_READBACKS
VOXY_MESH_CAPTURE_DIR=NEW_NATIVE_READBACKS bazel-bin/tests/mesh_path_test --gtest_filter=MeshEnvironmentGPUTest.*
python3 scripts/build_mesh_diagnostics.py --kind mesh-environment \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output NEW_DIAGNOSTIC_PACKAGE
```

Use the hardware browser outside the Nix environment:

```bash
/home/modkin/.nix-profile/bin/node scripts/run_mesh_diagnostics.mjs \
  NEW_DIAGNOSTIC_PACKAGE NEW_BROWSER_READBACKS mesh-environment
```

`filtered.cfg`, `legacy.cfg` and `views.json` specify the matched scene.
The actual application package was `/tmp/voxys-filtered-scene-browser-r01`;
`package.json` identifies its 22 served files. It contains `web/` plus the
normal CMake WASM JS/WASM/data outputs. Rebuild that package if it is gone.
The capture scripts are `scripts/capture_salvage_asset_views.py` and
`scripts/smoke_integrated_wasm.mjs`; existing runner logs and per-view reports
record their actual arguments. Use the latter with
`VOXY_SMOKE_ASSET_FIXTURE=NEW_OUTPUT` for controls or
`VOXY_SMOKE_ASSET_LOSS=NEW_OUTPUT` in a separate fresh run for GPU loss.
The experience is `salvage-material-detail`; hardware browser settings were
`VOXY_SMOKE_GPU=gaming`, width 1920, height 1080.

Still open: default bake timing and narrow-light convergence; other parts and
socket normals; texture transitions/variance/hand density; wet/glass materials;
full lighting/motion acceptance; water ordering, shadows and the authored cove.
