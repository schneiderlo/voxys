# Opaque calibration: browser packaging and inspection

2026-09-08. Scoped follow-up to `../r01-summary.json`. ASSET-06 and all visual
gates remain open. No replacement of the installed kit or cove.

The ordinary WASM package now includes `salvage_material_fixture.cfg`, the r01
registry and its two strict cooked bundles. Native configuration uses that
same registry. Browser route: `?experience=salvage-materials`.

## Checked result

- Native Bazel optimized application and CMake WASM application builds pass:
  `bazel-build.log`, `wasm-build.log`.
- Six hardware browser captures use the native cameras/light, 1920×1080 and
  FOV 60: `lod{1,2,3}/{pontoon-wells,winch-metal}.png`. Root viewed all six.
  Socket wells remain teal and the Far metal flange retains its response.
  Geometric faceting at forced Far remains visible. These views do not approve
  scene composition, shadows, wetness or final surface detail.
- Each actual capture reports the requested LOD for both objects, 6 uploads,
  2 model draws, no prototype uploads or physics bodies, and 6,548,816 requested
  GPU bytes. Native results are preserved in `../native-r01-lod{1,2,3}/`.
- All 14 browser stages pass: initial, four detail modes, three guide modes,
  resize to 1280/1920, flight, Reset, drained Leave/re-entry and another Reset.
  Guides report 26 rulers / 120 socket boxes. Leave releases the reservation.
  See `controls/summary.json`. This run does not repeat device-loss testing.

`summary.json` records scoped conclusions and artifact hashes; `sources/`
freezes the packaging/runner sources. `package.json` hashes the actual served
22-file package `/tmp/voxys-material-browser-r01`. Historical native evidence
and r01 asset hashes were not refreshed. Browser captures include its UI;
the images are visually compared, not asserted pixel-identical to desktop.

## Reproduction

Build the WASM application with the configured Emscripten toolchain. Copy the
web files and completed `voxy_wasm.js`, `.wasm` and `.data` to a fresh package
directory. Run the hardware browser outside the Nix shell on this host:

```bash
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 \
VOXY_SMOKE_ASSET_VIEWS=/tmp/new-material-views-near VOXY_SMOKE_ASSET_LOD=1 \
VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-06/browser-r01/views.json \
VOXY_SMOKE_REPORT=/tmp/new-material-views-near.json \
node scripts/smoke_integrated_wasm.mjs /tmp/voxys-material-browser-r01 salvage-materials
```

Repeat with 2/3 and fresh output names. For controls, omit the view variables
and set `VOXY_SMOKE_ASSET_FIXTURE=/tmp/new-material-controls` and a new report.
This host used Chrome 152 hardware WebGPU, AMD RDNA 3, without a fallback
adapter. Capture timing and the sparse GPU profiler samples are not a game
performance gate.

Next: metric surface detail and rubber, opposing-light and motion inspection,
then wet/glass support and independent review under the parent plan.
