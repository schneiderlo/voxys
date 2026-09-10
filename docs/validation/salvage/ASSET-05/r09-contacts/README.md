# r09 close-contact inspection

2026-09-08. Root completed this **scoped geometry inspection**. ASSET-05,
ASSET-04 independent acceptance and every later gate remain open under D18.
These are static dry inspection assemblies, not sailing or approved cove art.

## Result

Root viewed all **48 actual application PNGs: 24 desktop/browser pairs**.
The same eight cameras were repeated at Near, Middle and Far detail:

| Scene | Views | What was inspected |
|---|---|---|
| Broad starter | deck-mating, engine-seat, underside, helm-seat, winch-rear | Deck/beam/pontoon seats, outboard clearance, bottom wells, corrected helm well and opposite winch flange face |
| Cargo | cargo-seat, cargo-below, crate-pin | Generator/cradle seating, cradle underside well and crate latch pin |

All pairs use the installed r09 kit and unchanged pontoon v2-rc01, physical
1920×1080, FOV 60, clean view, matching camera/light recipes and the real
renderer. Geometry, attachment seating and visible openings agree between
desktop and browser at each selected LOD. No newly visible solid penetration
was found in these views. The small cream peg cap above the thin helm/winch
foot occupies the open well; it does not pass through solid material.
The earlier exported-geometry ray and flange measurements remain in
`../r09-correction.md`; images do not replace those measurements.

The broad reverse-winch camera legitimately culls one model (10 draws);
other broad views have 11. Cargo has 4. Every authored placement retains the
requested LOD. Uploads, zero prototype/physics counts, 17 broad connections
and requested GPU allocation remain unchanged: 12,989,320 bytes broad,
4,776,288 bytes cargo. The 16 MiB owner ceiling was not raised.

Browser UI and animated sky/water timing differ. The images are visually
compared, not claimed pixel-identical. Forced Far is intentionally inspected
much closer than its normal screen footprint. Capture FPS is not performance
acceptance. These static cameras cannot prove every hidden surface or dynamic
attachment behavior.

## Material findings carried into ASSET-06

- Pontoon bottom-well walls change teal → cream → teal across Near/Middle/Far.
  This occurs in the unchanged pontoon candidate. Its face-center palette
  classification depends on tessellation and needs a stable region assignment.
- Far kit assets omit metallic/roughness maps and use a dielectric fallback.
  Metal becomes flat gray. Material identity must survive geometric LOD.
- Some broad surfaces show stretched or banded grain. Atlas packing has no
  consistent physical texel density. Existing material textures do have full
  runtime mip chains; missing mip generation is not the diagnosis.
- Object contact shadows, composed water and finished lighting remain LOOK-01
  and REND work. None of these images establishes final visual quality.

## Implementation and focused regression checks

Inspection configurations now accept `[game] asset_fixture_lod` values
`auto`, `near`, `middle`, `far`. Unsupported names, malformed strings and a
forced detail without an asset registry refuse validation. Every real bundle
must supply a requested forced LOD. Reset restores Auto and clean guides.
Native successful PNG capture logs its actual inspection state; both view
runners bind the selected detail, uploads and placement state to their PNGs.

| Check | Actual result | Evidence |
|---|---|---|
| Config and CLI unit cases | 60 Bazel; CMake 38 config + 22 CLI; zero failures | Three XML files and corresponding logs |
| Application builds | Optimized Bazel native, CMake native, full WASM pass | `bazel-app.log`, `cmake-build-config.log`, `wasm-app.log` |
| Close views | 24 native + 24 hardware Chrome/Wayland PNGs; clean app/GPU results | `native-{broad,cargo}-lod{1,2,3}/report.json`, matching browser reports and outer summaries |
| Actual unsupported native bundle detail | Mixed-LOD hierarchy Far exits 1 with exact unavailable-detail error; no PNG | `hierarchy-unsupported-far.{cfg,log,json}` |
| Original native Auto | Pontoon front capture passes; root viewed PNG | `native-pontoon-auto/` |
| Current browser controls | Broad and mixed-LOD hierarchy real button/flight/resize/Reset/Leave/re-entry journeys pass | `browser-{broad,hierarchy}-controls/summary.json` |

The unavailable-bundle check occurs after GPU initialization, before fixture
asset uploads; it is not a pre-GPU rejection. Previous r09 loss and full motion
checks are retained as historical evidence, not relabeled as new runs.

## Reproduction

From the repository root, with the built current native application:

```bash
nix-shell --run 'python3 scripts/capture_salvage_asset_views.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-broad-near-contacts --lod 1 --recipe docs/validation/salvage/ASSET-05/r09-contacts/broad-views.json --config salvage_kit_broad.cfg'
```

Use `cargo-views.json` and `salvage_kit_cargo.cfg` for cargo. Repeat with `--lod
2` and `--lod 3`, always using new output directories. Native launches one
fresh application per view and removes its unique registry afterward.

The browser package directory and all 22 file hashes are in
`browser-package.json`. Serve a fresh completed package through:

```bash
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_REPORT=/tmp/new-browser-broad-near-summary.json \
VOXY_SMOKE_ASSET_VIEWS=/tmp/new-browser-broad-near-contacts \
VOXY_SMOKE_ASSET_LOD=1 \
VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-05/r09-contacts/broad-views.json \
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-kit-browser-r09-contacts salvage-kit-broad
```

Repeat cargo and other detail levels. Run GPU captures sequentially.
`summary.json` binds this checkpoint's evidence and frozen changed sources.
The preceding `../r09-summary.json` and `../r09-sources/` remain historical;
later source changes must not refresh their hashes.

Remaining ASSET-05 acceptance: independent technical review after ASSET-04
prerequisite acceptance and resolution of any findings. Material corrections
are tracked separately in ASSET-06 and must precede LOOK-01 acceptance.
