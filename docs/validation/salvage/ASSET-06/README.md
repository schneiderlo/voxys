# Shared material preparation and first calibration candidate

2026-09-08. **ASSET-06 remains open.** Root prepared this work under D18 while
ASSET-04 independent acceptance remains pending. No gate or final art approval.

The authoring contract is
[`tools/salvage_assets/MATERIALS.md`](../../../../tools/salvage_assets/MATERIALS.md).
It defines palette, plastic/metal/rubber/glass targets, wetness prerequisites,
texture channels, metric detail/UV requirements, decals, mip/atlas handling,
memory limits, optional image-generation inputs and the remaining checks.

## Implemented and checked

Both Blender recipes now accept `--material-profile opaque-calibration-v1`.
The default remains `legacy`. A shared module defines dry plastic/polymer/metal
responses. Calibration emits unlit flat palette colors and per-region
metallic/roughness maps at all three detail levels. It omits the old unscaled
random grain/normals so their stretching cannot obscure the material check.
Metric surface detail is still required; this is not finished tactile art.

Pontoon socket color classification now uses the whole face and the actual
well radius for the chosen LOD in this opt-in profile. The original candidate's
fixed-radius center test remains available for historical legacy reproduction.

Isolated assets are in `data/salvage/material-calibration/r01/`:

- Pontoon: explicit draft part/visual version 3, unchanged physical parameters.
- Winch: isolated draft at the existing unpublished candidate identity; it has
  not replaced r09 or received an immutable publication/version assignment.
- Both include editable Blender sources, three GLBs, maps, strict cooked
  bundles and source/provenance records.

`data/salvage/fixture-material-calibration-r01.json` selects only these two
assets for the standalone native config in this directory. Installed pontoon,
broad/narrow/cargo fixtures and the cove are unchanged. This fixture is not yet
in the standard WASM preload list or a new browser route.

| Evidence | Result |
|---|---|
| `pontoon-author-r01.log`, `winch-author-r01.log` | Blender 5.2.1 LTS background authoring passes; 6 LOD exports |
| `exported-material-r01.json` | Actual exported GLBs reproduce 9 incorrect old well samples; all 45 corrected well samples are teal; all 3 sampled winch flanges retain metallic 1 and roughness .30 within byte quantization |
| `geometry-isolation-r01.json` | All 6 triangle-position sets match preceding geometry exactly; part metadata matches except explicit pontoon version 2→3 |
| `{pontoon,winch}-cook-r01.log` | Both pass the real strict salvage converter and C++ sidecar validator; no legacy fallback |
| `native-r01-lod{1,2,3}/report.json` | Six real 1920×1080/FOV 60 clean-view captures pass, with requested LOD and six uploads, no prototypes/physics bodies or application/GPU errors |

Root viewed all six native PNGs. Well colors stay teal; the Far flange retains
a metallic light response. The stretched random grain is absent. Geometric
faceting remains most visible when Far is forced close to the camera. The
models still need metric surface detail, better composed light/shadows and
actual cove placement. This inspection does not satisfy LOOK-01.

The GLB diagnostic samples the embedded base-level PNG using nearest texels;
it does not prove filtered atlas behavior. Native captures use the actual
renderer/mips. Browser parity, changing-light/wet previews and full-kit
replacement are still pending. No current application/shader code changed for
this calibration; the earlier native/browser build evidence is retained.

## Reproduction and next work

From the repository root, create a fresh parent/output directory and run:

```bash
blender --background --factory-startup --python tools/salvage_assets/author_pontoon.py -- --spec data/salvage/material-calibration/r01/pontoon-parameters.json --output-dir /tmp/new-material-pontoon --material-profile opaque-calibration-v1 --preview quick
blender --background --factory-startup --python tools/salvage_assets/author_functional_kit.py -- --output-dir /tmp/new-material-kit --parts winch --material-profile opaque-calibration-v1
python3 tools/salvage_assets/check_material_calibration.py --pontoon /tmp/new-material-pontoon/source --winch /tmp/new-material-kit/winch/source --old-pontoon data/salvage/pontoon/release-candidates/v2-rc01/source --report /tmp/new-material-export-check.json
```

The checker requires Pillow. It reads the actual identity-node GLBs from these
recipes and applies the declared canonical rotation. It is not a general
production importer. For old recipe reproduction, use the frozen scripts
stored with those original candidates.

Cook with `cook_gameplay_asset.py --sidecar <source/name.gameplay.json>
--sources <source> --output <new cooked directory> --converter
bazel-bin/tools/gltf_vmesh_tool --validator bazel-bin/tools/gameplay_sidecar_tool`
inside the Nix shell. Point a new registry at the resulting strict bundle
manifest hashes. Its schema and limits can follow the r01 calibration registry.

Replay the installed isolated native candidate with:

```bash
nix-shell --run 'python3 scripts/capture_salvage_asset_views.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-material-native-near --lod 1 --recipe docs/validation/salvage/ASSET-06/material-r01-views.json --config docs/validation/salvage/ASSET-06/material-r01.cfg'
```

Repeat with 2 and 3 into new directories. Next: package this isolated fixture
explicitly for a browser check, preserve the exact native recipe, and provide
the browser runner's required guide-count metadata in a separate derived
recipe if needed. Then establish metric UV/detail and actual neutral/bright/
dark/opposing-light previews; implement dedicated rubber, wetness and explicit
glass admission/rendering. Do not install a full-kit replacement based on this
two-asset calibration alone. Keep every parent task and gate open until their
requirements pass. `r01-summary.json` freezes this checkpoint's inputs/evidence.
