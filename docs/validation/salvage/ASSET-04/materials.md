# ASSET-04 material evidence — 2026-09-08 UTC

The shared C++ mesh renderer passes **six native tests** through both Bazel and
CMake and **five real browser tests**, with no skipped cases. The 18 native and
browser 64×64 RGBA readbacks are **byte-identical** on this Radeon 890M host.
This proves these material cases on these two backends; it does not approve
the scene's art, prove performance, or cover other GPUs.

## What was executed

`tests/test_mesh_path.cpp` now runs the same diagnostic geometry, actual
`MeshPath`, production `shaders/mesh_path.wgsl`, texture uploads, render passes
and GPU readbacks in native WebGPU and browser emdawnwebgpu. Browser JavaScript
only requests the real device, hosts the tests and displays their returned
pixels. It does not replace the C++ renderer with a separate shader.

The browser uses the C API's reference-counted preinitialized device handle.
Map callbacks own stable completion state; a timeout cannot retain a pointer
to an expired stack frame. Both paths require actual readback completion.
The browser harness rejects GPU errors, device loss, test failure and skips.
One exact, timestamped application error is required for the deliberately
malformed CPU asset; other console errors fail.

| Check | Actual evidence |
|---|---|
| UV orientation / winding | Red/green/blue/yellow texture in orthographic and perspective views; reversed single-sided face and cleared instances render empty |
| Resource lifetime | Encoded draw still samples its uploaded texture after every external MeshPath handle is released before submission |
| Normal transform | Diagonal authored normal under nonuniform scale; perpendicular light stays dark, normal-facing light illuminates the surface |
| Tangent handedness | Directional tangent-space normal and `tangent.w` sign reverse the expected lit/dark response |
| Color space | sRGB texture code 128 matches linear base-color factor 0.2158605 after actual shading/presentation; both center pixels are RGB 141 |
| Packed material channels | Linear G=64/B=192 texture matches roughness 64/255 and metallic 192/255 factors; changing unused R has no effect |
| Roughness / metallic response | Same camera/light: smooth packed sample center RGB 146/143/139; rough sample 0/0/0 under deliberately weak test light; dielectric sample 44/43/41 |
| Legacy mesh path | Additional native bike/rider/textured-mesh case still submits its expected 77 draws |

## Reproducible evidence

- `integration/portable-mesh-native-attempt04.log`: optimized Bazel, six pass.
- `material-native-attempt01/`: exact raw native readbacks, frame metadata,
  GoogleTest XML and logs; six pass.
- `integration/material-cmake-build.log`, `material-cmake-tests.log/.xml`:
  CMake build and six tests pass.
- `material-wasm-attempt03/build.json`: source and compiler hashes, exact build
  commands and artifact hashes. Emscripten JS exceptions, Asyncify, fixed
  64 MiB heap, 1 MiB guarded stack, O2 and assertions; no Closure. This is a
  correctness test binary, not the shipping application settings or a benchmark.
- `material-browser-attempt02/report.json`, `tests.log`, `material-sheet.png`:
  Chrome 152, actual AMD RDNA 3 nonfallback adapter, five pass, 18 readbacks,
  zero uncaptured GPU errors, zero device losses and zero unexpected errors.
- `integration/material-parity.json`: all 18 source-matched pairs, hashes,
  settings, center samples and channel differences. Maximum and mean delta
  are zero for every pair. The allowed cross-backend tolerance was two codes.

Build/run recipes, from the repository root:

```bash
nix-shell --run 'bazel test -c opt //tests:mesh_path_test --test_output=all'
python3 scripts/build_mesh_diagnostics.py \
  --sdk /tmp/voxys-emsdk/upstream --node /home/modkin/.nix-profile/bin/node \
  --output /tmp/new-mesh-wasm-build
# Launch Chrome outside the Nix shell; its GPU libraries come from the host.
node scripts/run_mesh_diagnostics.mjs /tmp/new-mesh-wasm-build /tmp/new-mesh-browser-result
```

The native readback recipe is `VOXY_MESH_CAPTURE_DIR=<new-existing-directory>`
on the built `mesh_path_test` binary with GoogleTest XML enabled. The exact
successful paths are in `integration/material-parity.json` and the native log.
`scripts/compare_mesh_diagnostics.py --help` describes the source/hash-checked
comparison. Each runner preserves earlier attempts and refuses reused output.

Failure history is retained: first native command used the wrong target name;
the first portable browser compile hit an existing Clang warning in the
physics header's deleted GLM comparison; the standalone runner suppresses
only that known warning. A new native size conversion was corrected explicitly.
Browser attempt 01 passed all C++/GPU tests but its outer harness expected the
malformed-asset log without the actual logging timestamp. Attempt 02 accepts
only the exact observed timestamped message and passes the whole harness.

## Actual pontoon scene review

This section retains the pre-correction scene evidence. The current inspector
removes the obstructing cove scenery and improves ambient lighting; see
[the corrected inspector](clear-inspector.md) for implementation and validation.

`inspection-views.json` defines nine shared camera/light recipes: front, rear,
left, right, underside, engaged stack, and three lights on one fixed view.
The application still loads the r04 candidate through its installed registry.
These are actual scene captures, separate from the small material charts.
`scripts/validate_salvage_asset_views.mjs` waits for setting application and GPU
completion, records the actual camera/light/LOD/residency, and captures at
1920×1080. Close views can correctly cull other parts behind the camera.

Browser results are in `pontoon-browser-views-attempt02/`; all nine captures
completed without GPU or application errors. Its first attempt stopped at
the right view because the harness incorrectly required all four parts to
remain visible. The correction retains actual counts between one and four.

Native results are in `pontoon-native-views-attempt02/`; all nine natural
application exits and 1920×1080 captures pass with no GPU/application errors.
The runner keeps the original registry/config unchanged, creates only a unique
temporary registry beside the trusted asset bundle base, and removes it on
exit. Every effective registry/config, command, binary hash and image hash is
preserved. Its first attempt used a leading-dot filename that the existing
strict loader correctly rejected; the runner now uses an accepted leaf name.
The initial r04 source/package remains selected on both platforms.

Root visually compared native/browser material-front-light, engaged and
underside captures; the same silhouettes, seams and lighting defects are
present. These animated whole-scene screenshots are not claimed byte-identical;
only the 18 controlled material-chart readbacks have that result. Browser
left/right and opposite/low-light views were also visually inspected. Full
independent review and acceptance of every side remain open.

Root inspection confirms that socket wells have visible interior geometry,
the engaged stack has the authored .96 m center spacing, and direct lighting
changes the fixed-view material. The views also expose unresolved defects:

- Unlit sides and the underside are too dark for useful inspection.
- The old large cyan boxes and mast dominate the scene and intersect its
  presentation space; the right view is partly occluded by the mast. This is
  not an acceptable inspection stand or the intended modular craft.
- Close views show coarse peg facets and weak material separation.
- The underside crop cannot replace a complete ruler/axis/clearance overlay.

Initial source investigation finds different ambient semantics: the primitive
shader normalizes the ambient tint before applying intensity, while MeshPath
multiplies the already dim color by sampled sky illumination. The object sky
view is RGBA16Float linear radiance. This is a lead for the lighting work,
not a completed fix or proof that only one cause explains the appearance.

**ASSET-04 and LOOK-01 remain open.** Finish the rendered rulers/socket axes,
readable inspection lighting, assembled crossbeam fixture, slow LOD review,
candidate publication and independent technical review. A final cove needs
the functional asset kit, coherent lighting/composition and correct opaque/
water ordering. Offline Blender renders and passing pixel tests do not pass
those visual requirements.
