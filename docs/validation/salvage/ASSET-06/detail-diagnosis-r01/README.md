# Detail-map ablation — 2026-09-08

The strongest cream-bevel bands and steel-hub streaks remain when both detail
maps are disabled. This native comparison **does not establish a texture-chart
cause for those features**. Earlier surface reports' visual attribution was
provisional; retain them as historical evidence, but use this result to direct
further diagnosis. Smaller map/UV defects are not ruled out.

`tools/salvage_assets/diagnose_detail.py` derives three helm candidates from the
unchanged manufactured-normal r04 source. It edits only material JSON. Every
GLB binary chunk (geometry, normals, tangents, UVs and embedded pixels) remains
byte-identical; all other GLB JSON remains equal. The generator checks these
invariants and records the nine source/candidate/BIN hashes in
`data/salvage/material-calibration/diagnosis-r01/provenance.json`.

| Variant | Active detail |
|---|---|
| normal-only | Normal map; dry-family constant roughness |
| roughness-only | Roughness map; no normal map |
| factors-only | Neither map; dry-family constant roughness |

All three strict cooks pass in `cook.log`. Their separate registries leave the
pontoon/winch and placements unchanged. The provenance status says cooking is
pending because it records generation time; this report and the cook/runtime
logs record the subsequent checks without rewriting that original record.

Root inspected these three actual native images:

- [Normal only](normal-only/native/helm-grip-key.png)
- [Roughness only](roughness-only/native/helm-grip-key.png)
- [Factors only](factors-only/native/helm-grip-key.png)

Each directory retains the exact recipe, launch configuration, admitted
registry, capture report and application log. Settings are 1920×1080, FOV 60,
Near detail, eye `(6.6,.85,1.2)`, target `(6,.15,.05)`, sun `(.4,.8,-.4)`.
The recipe's inherited purpose string describes the earlier shading comparison;
the diagnostic registry and generator define this material ablation.

Reproduce from the repository root, using new output directories:

```bash
python3 tools/salvage_assets/diagnose_detail.py --output NEW_ASSET_DIRECTORY
nix-shell
python3 tools/salvage_assets/cook_gameplay_asset.py \
  --sidecar NEW_ASSET_DIRECTORY/factors-only/source/helm.gameplay.json \
  --sources NEW_ASSET_DIRECTORY/factors-only/source \
  --output NEW_COOKED_DIRECTORY \
  --converter bazel-bin/tools/gltf_vmesh_tool \
  --validator bazel-bin/tools/gameplay_sidecar_tool
python3 scripts/capture_salvage_asset_views.py \
  --binary build-salvage-native/bin/voxy_native --output NEW_CAPTURE_DIRECTORY \
  --lod 1 \
  --recipe docs/validation/salvage/ASSET-06/detail-diagnosis-r01/factors-only/views.json \
  --config docs/validation/salvage/ASSET-06/detail-diagnosis-r01/factors-only/fixture.cfg
```

Repeat cooking for the other two variants. The stored capture recipes select
the existing diagnostic registries; using newly cooked data instead requires a
new registry with that manifest's exact hash. These diagnostic variants are not
installed in the application package. The current surface-detail route still
selects r04 winch/helm and the unchanged r02 pontoon.

Source investigation found that `mesh_path.wgsl` obtains rough reflections from
ordinary equirectangular image mipmaps and diffuse light from its smallest mip.
That is a plausible contributor, **not yet a proven visual fix**. The separate
[environment-lighting preparation](../environment-r01/design.md) adds the
missing convolution/BRDF producer. Actual scene consumption and matched
native/browser comparisons must follow before accepting a rendering improvement.

This ablation is one camera/one detail level on native. It does not pass the
changing-light, motion, wet/glass, independent-review or authored-cove gates.
