# Manufactured surface-normal checkpoint

This checkpoint improves curved shading on the isolated winch and helm while
preserving flat panels and cylinder caps. It is preparatory ASSET-06 work under
D18. ASSET-06, LOOK-01, prerequisite review and game gates remain open.

The existing `salvage_metric_fixture.cfg` / `?experience=salvage-material-detail`
inspection now selects `data/salvage/fixture-material-shading-r02.json`.
It combines the unchanged r02 pontoon with r04 winch/helm candidates. The full
r09 kit and the owner-rejected walkable cove have not received these candidates.
No C++ or shader change is part of this checkpoint.

## Implementation

`tools/salvage_assets/surface_shading.py` adds opt-in `manufactured-v2`:

- Rounded boxes derive corner normals from the original box's inset core.
  Panels stay planar; bevel normals meet those planes without a hard stripe.
- Cylinders use radial barrel normals and axial cap normals. Bevel normals
  account for Blender's offset of polygon side planes. Unbeveled Far caps keep
  a hard boundary at the barrel, even when corner positions coincide.
- Rings use analytic torus normals from their original center/radius/axis.
- Components with Boolean socket cuts use area/angle weighted normals with
  sharp boundaries above 50 degrees. Their interior faces are not treated as
  the outer rounded-box/cylinder surface.

The kit recipe retains shape metadata, applies normals before joining, and
preserves custom normals through triangulation. The default is still `flat`.
The model positions, winding, UVs, material assignment, material pixels/factors,
physical part definition and tool anchors are unchanged from the r02 metric
assets. The existing dry-material profile remains `tactile-metric-v1`.

The first version is retained in [shading-r01](../shading-r01/README.md).
Its weighted panels rounded visibly and failed the exported flat-panel check.
The analytic box/cylinder correction is the version selected here.

## Verified results

`export-check-r02.json` is the final GLB diagnostic. `export-check.json` is the
earlier passing diagnostic before winding preservation was added to its checks.
All six final GLBs pass exact triangle/corner/material/physical comparisons.
Normals and tangents are finite and unit length within 0.0002, their dot product
is below 0.0002, and no tested face normal or projected normal-map green axis
reverses direction. The old flat-normal check's 0.995 angle threshold is not
silently reused for smooth corners: the new diagnostic measures the projected
direction and separately checks the analytic shape normals.

| Exported surface | Baseline worst error | Candidate worst error |
|---|---:|---:|
| Winch flange barrel, Near | 7.5012° | 0.0092° |
| Winch flange barrel, Middle | 11.2505° | 0.0129° |
| Winch flange barrel, Far | 18.0017° | 0.0017° |
| Tested flat panels, all levels | 0° | 0.0115° |
| Helm rubber torus, all levels | See per-level diagnostic | 0.0081° |

The 0.03-degree acceptance bound covers four-decimal exported normal
quantization. The panel and cap tests select entire coplanar triangles, not
arbitrary vertices at a shared sharp edge. This prevents the normal of a
correct adjacent face being mistaken for a panel defect.

Both strict cooks pass (`kit-cook.log`). The native Bazel build succeeds using
cached code actions (`native-build.log`); the WASM CMake application relinks with
the new data (`wasm-build.log`). Both build descriptions include the registry
and ten cooked files. Historical r02 inputs remain available for replay.

Root inspected all 24 actual game images: four opposing-light views at three
forced detail levels in both native and browser applications. Every capture
uses 1920×1080, FOV 60, nine model uploads, 13 model draws and zero physics
bodies/prototype uploads. The unchanged pontoon remains in the fixture; its
isolated underside views were not recaptured in this normals-only checkpoint.

The fixture requests **5,485,472 GPU bytes**, below the unchanged 16 MiB owner
limit. This is 79,200 bytes below r02 because 1,100 duplicated vertices merge
when their normals agree. It is requested asset residency, not measured total
device memory or a performance result.

All 14 browser control/resize/Reset/drained Leave/re-entry stages pass in
`browser-controls/summary.json`. Rulers use 26 boxes and sockets use 135;
Leave drains the owned asset residency to zero. No browser errors are reported.

Hardware: existing Linux Ryzen AI 9 HX 370 / Radeon 890M reference, native
Vulkan and hardware Chrome 152 WebGPU on Wayland. Blender 5.2.1 LTS, build
`9e2066aef7ef`. Static captures do not establish frame-time, motion or owner
visual approval.

## Reproduction and source integrity

Use new output directories. The exact ten-file recipe closure for the generated
candidate is in `data/salvage/material-calibration/r04/recipes/`.
`recipe-integrity.json` verifies all 16 recipe hashes declared by the two
provenance records; `frozen-recipe-import.log` proves the archived entry point
loads its dependencies in Blender.

```bash
mkdir -p /tmp/new-salvage-shading
blender --background --factory-startup --python-exit-code 1 --python data/salvage/material-calibration/r04/recipes/author_functional_kit.py -- --output-dir /tmp/new-salvage-shading/kit --parts winch helm --material-profile tactile-metric-v1 --surface-shading manufactured-v2
python3 tools/salvage_assets/check_surface_shading.py --candidate /tmp/new-salvage-shading --report /tmp/new-salvage-shading-check.json
```

The first local invocation lacked the required parent directory and refused
before authoring (`kit-author.log`). The corrected invocation succeeded
(`kit-author-r02.log`). Do not drop `--python-exit-code 1` from batch authoring.

Cook each part with the frozen `cook_gameplay_asset.py`: `--sidecar
<source/name.gameplay.json> --sources <source> --output <new cooked>
--converter bazel-bin/tools/gltf_vmesh_tool --validator
bazel-bin/tools/gameplay_sidecar_tool` inside the Nix shell. The cook's
"published" result means an atomic local output directory, not external release.

The current working recipe also marks the separate cradle latch's Boolean
interior as machined; that part is not included or validated in this checkpoint.
The frozen candidate recipe remains exactly as used for the winch/helm exports.
Other parts and the pontoon need their own geometry/shading checks before this
profile is adopted by the complete kit.

The earlier r02 recipe archives accidentally included `author_hierarchy_probe.py`
instead of their required `authoring_probe.py`. To replay r02, copy its recipe
directory into a new scratch directory and add `authoring_probe.py` from the
r04 frozen recipes. `recipe-integrity.json` proves that helper exactly matches
the hash declared in r02 provenance. Do not rewrite old archives or manifests.

```bash
nix-shell --run 'python3 scripts/capture_salvage_asset_views.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-shading-native-near --lod 1 --recipe docs/validation/salvage/ASSET-06/shading-r02/views.json --config docs/validation/salvage/ASSET-06/shading-r02/fixture.cfg'
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_ASSET_VIEWS=/tmp/new-shading-browser-near VOXY_SMOKE_ASSET_LOD=1 \
VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-06/shading-r02/views.json \
VOXY_SMOKE_REPORT=/tmp/new-shading-browser-near.json \
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-shading-browser-r02 salvage-material-detail
```

Repeat at levels 2/3 into new directories. `package.json` hashes the actual
22-file served package. To rebuild it, build `voxy_wasm` through configured
CMake, copy the files from `web/` and `build-lego-wasm/bin/voxy_wasm.{js,wasm,data}`
into a fresh directory. For controls omit the view variables and set
`VOXY_SMOKE_ASSET_FIXTURE` to a new directory. `sources/` freezes the checkers,
capture/control runners, packaging and current handoff; `summary.json` records
the selected asset and evidence hashes.

## Remaining work

The normal correction does not remove visible texture chart transitions or
directional grain. Inspect/bound normal variance and mip behavior, improve
hand-contact density, and verify bright/dark exposure plus continuous motion.
Socket-cut surfaces still use the weighted fallback and need further visual
refinement. Far silhouettes remain deliberately coarse when forced close to
the camera; automatic transition distances are not accepted by this test.

Wet response and explicit glass admission/rendering remain unimplemented.
Water/opaque HDR ordering, contact shadows and an authored walkable cove still
block LOOK-01. The owner's rejection of the prior scene stands. Complete the
independent/prerequisite reviews and all parent requirements before any gate
commit; this checkpoint does not pass a gate.
