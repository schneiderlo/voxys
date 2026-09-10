# r09 kit correction and forced-detail inspection

2026-09-08. **Scoped implementation verified; ASSET-05 and G02 remain open.**
The installed broad, narrow and cargo registries now select r09. The pontoon
remains the unchanged v2-rc01 candidate. D18 permits this preparation while
independent reviewers are unavailable; it does not accept either parent task.
No gate passed and no gate commit was made. The active objective remains G14.

## Corrections

- The winch flanges now clear their side plates by **10 mm** on each side.
  The drum is .42 m wide; flange centers are x=±.235 m with .05 m thickness.
  Seven cable wraps fit between the flange inner faces. Every LOD measures
  actual post-modifier vertices before export and refuses a gap below 9 mm.
- The helm's .20 m socket well now cuts its support column as well as its
  foot. A second checker imports the actual exported GLBs and casts vertical
  insertion rays across all three peg profiles. Each old LOD has 14 blocked
  samples out of 41; each corrected LOD has **zero**. This is a sampled exported
  geometry check, not an exhaustive collision proof.
- The helm, winch and cradle use disjoint solid box proxies, preserving useful
  open spaces. Collision and build occupancy agree. Solid-material buoyancy
  stays inside each proxy and does not turn the frame into a sealed chamber.
  Their dry masses are unchanged; COM and the full inertia tensor are
  recomputed from uniform density over these proxies.

`r09-change-isolation.json` verifies that only the helm and winch GLB bytes
changed. The other seven models are identical to r08. Only the mass-property,
collision, occupancy and buoyancy fields of the three corrected definitions
changed; all other part metadata is unchanged. All three registry placements,
connections and initial cameras are unchanged. Both starters still weigh
1,035 kg and have eleven parts with seventeen exact connections. Moving the
pontoons from the narrow to broad layout still adds 480 kg·m² to yaw and roll
inertia with identical mass and world COM between those two layouts.

The propeller uses a conservative rotating envelope; the generator uses a
housing/cage envelope. Fine socket recesses are omitted from gameplay contact
proxies. Uniform proxy density is an approximation to machinery internals.
These limits remain explicit and do not establish live contact or sailing.

## Completed checks

| Check | Result | Evidence |
|---|---|---|
| Blender authoring and strict cooks | Nine parts, 27 valid LODs | `author-r09.log`, `cook-r09.log`, r09 provenance and cook manifests |
| Exported helm well | 123 corrected samples clear; 42 old samples reproduce the obstruction | `helm-well-r09.json`, `helm-well-r09.log` |
| Optimized Bazel kit | Five cases pass | `native-kit-r09.log`, `native-kit-r09.xml` |
| CMake kit | Five cases pass | `native-kit-r09-cmake.log`, `native-kit-r09-cmake.xml` |
| Native applications | CMake and optimized Bazel build | `native-app-r09.log`, `native-app-r09-bazel.log` |
| Shared CPU WASM | 75 actual cases pass, zero skips | `cpu-wasm-r09-pass/manifest.json`, `tests.log` |
| Full WASM application | Build and frozen package verified | `wasm-app-r09.log`, `browser-package-r09.json` |
| Invalid native capture requests | Thirteen refuse before GPU startup or output mutation | `motion-cli-r09.json` |
| Real forced-detail motion | Six matching native/browser runs; all authored placements retain the requested LOD | `native-r09-{broad,cargo}-lod{1,2,3}/`, `browser-r09-{broad,cargo}-lod{1,2,3}/` |
| Original automatic-detail motion | Both runtimes retain 1→2→3→2→1 for all four tracked pontoons | `native-r09-assembly-auto/`, `browser-r09-assembly-auto/` |
| Browser controls | All three kit routes pass fourteen stages | `browser-r09-{broad,narrow,cargo}-controls/summary.json` |
| Actual GPU loss | Clean exceptional teardown with socket guides active | `browser-r09-broad-loss/summary.json` |

The initial `cpu-wasm-r09/` run passed all 75 test cases, but its surrounding
checker still expected the old count of 74 and therefore rejected the run.
Both that failure and the fresh successful run are preserved. The checker now
expects the five kit cases in addition to the existing seventy asset cases.

The forced-detail paths are ten seconds, 6→8→6 m, at 1920×1080 and FOV 60.
Native produces forty original JPEG frames per run; browser produces thirty.
These are actual submitted native surfaces and hardware Chrome/Wayland pages,
using the same registry, path and lighting. Capture overhead is not game FPS.
The normal application detail action selects each level; the recorder does
not replace assets, alter shader behavior or fabricate LOD telemetry.

The shared recipe accepts optional `forced_lod` integers 0…3. Omission retains
the original automatic traversal requirement. A schema-1 gallery requires an
explicit workload and must have no assembly telemetry; schema-2 assemblies
retain their exact parts/connections checks. Tracked placements must be unique,
bounded authored indices. A fixed level requires a one-element observed LOD
sequence; Auto still requires 1→2→3→2→1. Every run checks generation, resident
uploads, guides, canonical inventory, actual draws and GPU completion.

Both runtimes report **12,989,320 requested GPU bytes** for the skiff and
**4,776,288 bytes** for cargo, within the existing 16 MiB owner ceiling. This
is requested allocation, not measured driver residency. Clean draws remain
11/4; socket guides add 510/120. Real browser controls include Near/Middle/Far,
Auto, both guide modes, resize, keyboard flight, Reset, Leave with actual GPU
retirement, re-entry and a further Reset. The original default world appears
after Leave. Native capture controls and actual GPU submission are verified;
this record does not claim an automated native keyboard/mouse journey.

## Root visual review and remaining work

Root inspected the winch/helm Blender previews and all twelve matching first
frames of the six forced-detail runs. The winch no longer has flanges buried
in the cheeks. Mounting outlines, open cradle space, cargo seating and main
silhouettes agree between desktop and browser. Far detail intentionally removes
small wraps, vents, seams and bevels while retaining the large forms. Forcing
Far close to the camera demonstrates its simplified geometry; it is not the
normal near-camera visual target. The browser UI and sky animation differ.

The first-frame comparisons do not substitute for continuous moving-image
review. The prior six r08 close-contact pairs remain historical evidence and
must not be relabeled r09. Finish targeted close-contact/underside inspection
of the installed correction at all LODs, including the helm seat and opposite
side of the winch, then obtain independent technical review after ASSET-04
acceptance. Keep ASSET-05 open until those remaining checks pass.

These are dry static asset-inspection scenes. The walkable cove is still the
owner-rejected prototype. ASSET-06 materials, LOOK-01 scene composition, proper
opaque/water ordering and independent visual review remain necessary. No live
winch, propulsion, latching, mission, final art or gate acceptance is claimed.

## Reproduce from the repository root

Use fresh output directories. All six `motion-r09-*.json` files are shared by
the runtimes; use `salvage_kit_cargo.cfg` and its route for cargo recipes.

```sh
nix-shell --run 'bazel test -c opt //tests:functional_kit --test_output=all'
nix-shell --run 'cmake --build build-salvage-native --target functional_kit_tests voxy_native --parallel 4'
build-salvage-native/bin/functional_kit_tests

python3 scripts/validate_asset_admission_wasm.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/new-r09-cpu-check --exception-mode js --asyncify --functional-kit

/snap/bin/blender --background --factory-startup --python-exit-code 1 \
  --python tools/salvage_assets/check_helm_well.py -- \
  --old-dir data/salvage/functional-kit/r08/helm/source \
  --candidate-dir data/salvage/functional-kit/r09/helm/source \
  --report /tmp/new-helm-well.json

nix-shell --run 'python3 scripts/capture_salvage_asset_motion.py \
  --binary build-salvage-native/bin/voxy_native \
  --recipe docs/validation/salvage/ASSET-05/motion-r09-broad-lod1.json \
  --config salvage_kit_broad.cfg --output /tmp/new-native-kit-detail'

VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_REPORT=/tmp/new-browser-kit-summary.json \
VOXY_SMOKE_ASSET_MOTION=/tmp/new-browser-kit-detail \
VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-05/motion-r09-broad-lod1.json \
/home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs \
  /tmp/voxys-kit-browser-r09 salvage-kit-broad
```

The browser package combines `web/` with the completed
`build-lego-wasm/bin/voxy_wasm.{js,wasm,data}` outputs. Rebuild using the current
maintained BUILD/CMake preloads when the frozen temporary package is absent;
do not mix old `.data` with a newer loader. `r09-summary.json` binds the checked
candidate, reports and sources; `r09-sources/` preserves the changed code and
harnesses. Earlier summaries and candidate directories remain historical.
