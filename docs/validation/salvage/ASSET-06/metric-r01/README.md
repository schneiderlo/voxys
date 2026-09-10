# Metric surface detail: first engine checkpoint

2026-09-08. Root implementation under D18. **ASSET-06 and visual gates remain
open.** This validates physical texture scale and dry material transport. It
does not approve finished art, wetness, glass, sailing or the walkable cove.

## Implementation

`tactile-metric-v1` is an opt-in profile in both Blender recipes. It separates
palette families into material slots, uses linear palette factors and repeats
small normal/MR tiles at a .5 m physical period. Near 128² gives 256 texels/m;
Middle 64² gives 128. Far retains dry palette/roughness/metallic identity with
no detail images. The helm wheel has a dedicated dark rubber material.

`tools/salvage_assets/metric_materials.py` contains the shared recipe and
export-factor checks. The material contract in `tools/salvage_assets/MATERIALS.md`
describes channels, height modes, UV orientation, limits and outstanding work.
No renderer/shader format extension or generated external imagery was needed.
Source-image sharing does not imply runtime texture deduplication.

The isolated candidates are under `data/salvage/material-calibration/r02/`:

| Part | Selected directory | Materials per LOD | Identity |
|---|---|---:|---|
| Pontoon | `pontoon-r02/` | 3 | Draft version 3 |
| Winch | `kit/winch/` | 4 | Existing unpublished version 2 |
| Helm | `kit/helm/` | 6 | Existing unpublished version 2 |

These contain editable Blender files, three GLBs, source PNGs, strict cooked
bundles and provenance. They do not replace installed r09/v2-rc01. Candidate
directories are review revisions, not publication or save migration.

The registry `data/salvage/fixture-material-metric-r01.json` selects those exact
bundle manifest hashes. Native: `--config salvage_metric_fixture.cfg`.
Browser: `?experience=salvage-material-detail`. The standard package includes
both this route and the unchanged r01 `salvage-materials` calibration route.

## Evidence

- `export-check.json`: nine actual GLBs retain exact preceding triangle-position
  multisets. Measured principal UV stretch is at most 1.001864. Near-equivalent
  density stays within 255–257 texels/m across all measured triangles.
- The same diagnostic checks actual material factors, mean roughness and map
  channels; 4,277 sampled exported normal texels agree with finite differences
  of the declared height field within 8-bit rounding error (1/255 in decoded
  normal components). Exported tangent
  frames preserve authored +green orientation. All 45 well rays hit teal;
  all three winch flange rays hit steel. This is a focused identity-node GLB
  diagnostic, not a general importer or a filtered pixel proof.
- `physical-isolation.json`: all three parts retain exactly the same physical
  metadata and tool anchors as their preceding selected candidates.
- `kit-cook.log`, `pontoon-cook.log`: all three pass the real strict converter
  and C++ metadata validator. The local cook's `published` status means an
  atomic output directory, not external publication.
- `native-lod{1,2,3}/report.json`, `browser-lod{1,2,3}/report.json`: 18 matching
  pairs at 1920×1080/FOV 60, three objects under two opposing azimuths at every
  detail level. All actual captures report the requested LOD, nine retained
  uploads, no physics/prototypes and 5,564,672 requested GPU bytes. Visible
  model draws are 7 or 13 depending on culling; the owner limit remains 16 MiB.
- Root viewed all 36 PNGs. Wells retain their color; the metal highlights
  change with the light and the rubber grip remains dark/diffuse. Detail has
  consistent scale, but faint directional repetition/chart boundaries and
  faceted shading remain visible. Metal cylinders, wheel and bevel shading
  still need refinement. The bright inspection fill and missing contact
  shadows do not meet the final scene target. No owner approval is implied.
- `browser-controls-r02/summary.json`: all 14 actual detail/guide/resize/flight/
  Reset/drained Leave/re-entry stages pass. Nine sockets produce 135 guide
  boxes; rulers produce 26. Leave returns the GPU reservation to zero.
- `bazel-build.log`, `wasm-build.log`: native optimized Bazel application and
  CMake WASM application builds pass. There were no C++ or WGSL changes in
  this checkpoint. Device loss and continuous-motion tests were not repeated.
- `original-route-summary.json`, `calibration-route-summary.json`: the ordinary
  prototype and preceding r01 opaque calibration still launch from this exact
  expanded package. Both startup checks pass without browser errors.

The machine is the existing Linux Ryzen AI 9 HX 370 / Radeon 890M reference,
native Vulkan and hardware Chrome 152 WebGPU on Wayland. Browser screenshots
include the inspection UI. No pixel-identical image or game-performance claim.

## Preserved failures and fixes

The first pontoon in `r02/pontoon/` failed the UV stretch check at 2.422581
because its Boolean shell contains nonplanar n-gons. The fix classifies material
domains before triangulation, then charts the actual triangles. Exact exported
triangle positions remain unchanged. `pontoon-author.log` and the failed
partial output are preserved; only `pontoon-r02/` is cooked/selected.

Blender initially returned process status zero despite the Python exception.
Use `--python-exit-code 1` for further batch authoring and check completed
provenance/export records. The corrected invocation uses that flag.

The first browser control run expected 150 socket boxes and timed out: the
helm has one receptacle, so all nine sockets require 135. This was a runner
expectation error. `browser-controls/`, its log and `failure-sources/` preserve
that attempt. The corrected runner completes all stages in `browser-controls-r02/`.
`views.json` is the immutable recipe used for the clean captures; its unused
socket-mode count was also 150. `views-r02.json` changes only that count to 135
and is the recipe to use for subsequent captures. Cameras/lights are identical.

Blender warns that shared image nodes use the first sampler. Every node here
uses the same repeat/linear settings, and the actual exported sampler is
checked. The export diagnostic's Pillow `getdata()` deprecation warning does
not affect its result; update that API when migrating the tooling environment.

## Reproduction

Use new output directories. Reproduce the kit with the frozen scripts in
`data/salvage/material-calibration/r02/recipes/`; the corrected pontoon uses
`recipes-pontoon-r02/`. The current working recipes also expose this profile.

```bash
blender --background --factory-startup --python-exit-code 1 --python tools/salvage_assets/author_functional_kit.py -- --output-dir /tmp/new-metric-kit --parts winch helm --material-profile tactile-metric-v1
blender --background --factory-startup --python-exit-code 1 --python tools/salvage_assets/author_pontoon.py -- --spec data/salvage/material-calibration/r02/pontoon-parameters.json --output-dir /tmp/new-metric-pontoon --material-profile tactile-metric-v1 --preview quick
python3 tools/salvage_assets/check_metric_materials.py --candidate data/salvage/material-calibration/r02 --report /tmp/new-metric-export-check.json
```

The checker currently expects this candidate tree's `pontoon-r02`, `kit/winch`
and `kit/helm` layout, and compares the named preceding repo candidates.
Cook each source with `cook_gameplay_asset.py --sidecar <source/name.gameplay.json>
--sources <source> --output <new cooked> --converter bazel-bin/tools/gltf_vmesh_tool
--validator bazel-bin/tools/gameplay_sidecar_tool` inside the Nix shell.

```bash
nix-shell --run 'python3 scripts/capture_salvage_asset_views.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-metric-native-near --lod 1 --recipe docs/validation/salvage/ASSET-06/metric-r01/views-r02.json --config salvage_metric_fixture.cfg'
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_ASSET_VIEWS=/tmp/new-metric-browser-near VOXY_SMOKE_ASSET_LOD=1 \
VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-06/metric-r01/views-r02.json \
VOXY_SMOKE_REPORT=/tmp/new-metric-browser-near.json \
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-metric-browser-r01 salvage-material-detail
```

Repeat at 2/3 into fresh directories. `package.json` hashes the actual served
package; `sources/` and `summary.json` freeze the implementation and evidence.
For controls omit the view variables and set `VOXY_SMOKE_ASSET_FIXTURE` and a
new report path. The browser runner launches outside Nix on this host.

## Next work

Improve surface shading and chart transitions before replacing the complete
kit. Verify normal variance/mip behavior and physical hand-contact density;
the initial rubber grip still uses the 256 texels/m structural target.
Add bright/dark exposure cases and continuous motion, then wet response and
explicit glass admission/rendering. Complete water/opaque HDR ordering and
contact shadows before the authored cove's LOOK-01 review. Preserve the
owner's rejection of the earlier visual quality. Parent tasks, independent
reviews and all unmet gate requirements remain open; no gate commit yet.
