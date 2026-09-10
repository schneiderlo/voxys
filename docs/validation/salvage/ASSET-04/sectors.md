# Live inspection camera sectors

The inspection camera now rebases its local coordinates during ordinary flight
and scripted movement. Real native/browser captures cross X, Y and Z sector
boundaries, return to the starting sector, and retain the same loaded assembly.
This completes the scoped camera-transition check following the
[rotation galleries](rotations.md). ASSET-04, G02 and LOOK-01 remain open.

## Change and scope

Previously, free flight changed the camera's local float position indefinitely.
The browser camera-placement API also wrote an absolute float position into
sector zero. Terrain uniforms normalized a copy, but the inspection camera
itself could leave the canonical local range. CPU asset rebasing tests alone
did not demonstrate a changing camera sector in the application.

`Application::update` now uses the existing `canonicalWorldPosition` helper
after free flight and the optional scripted update callback, before rendering.
It changes the camera only when its canonical sector/local position differs.
This runs only while the local inspection asset owner exists. Ordinary scenes
retain their controller contracts; no global `Camera::move` behavior changed.

For an inspection configuration, `voxy_set_camera_pose` also converts the
requested absolute position immediately with `worldPositionFromAbsolute`.
Out-of-range absolute coordinates are refused before modifying the camera.
Its existing finite checks, yaw/pitch behavior and other scene behavior remain.
No mesh, material, socket, rendering pass or asset ownership code changed.

Sectors are 256 m on each axis, with local coordinates in `[−128,128)`.
The normal renderer subtracts the camera sector from the double-precision
asset origin, then applies the exact build transform and authored node hierarchy.
The correction preserves the absolute eye and camera orientation while changing
the camera-relative coordinate frame.

## Actual evidence

All paths here are relative to `sectors/` beside this document.
`integration/summary.json` binds the accepted reports and verifies the source,
native executable and complete browser package in `integration/frozen-inputs.json`.
The current browser package was `/tmp/voxys-asset-browser-12`.

| Check | Result | Evidence |
| --- | --- | --- |
| CMake camera, world-position and application regression | 54 passed, zero skipped | `integration/native-tests.xml` |
| Bazel same filtered regression | 54 passed, zero skipped | `integration/bazel-camera-tests.xml` |
| Actual native movement | 30.007 s, 1,801 state samples, 190 original JPEGs | `native-motion-attempt01/captures/` |
| Actual browser movement | 30.006 s, 1,742 state samples, 131 original JPEGs | `browser-motion-attempt01/` |
| Real browser W/S input across X=128 and back | Passed in clean and socket modes; six PNGs | `browser-flight-attempt01/summary.json` |
| Native matching socket poses | Three PNGs, normal exits, no validation errors | `native-sockets-attempt02/report.json` |
| Ordinary cove lifecycle and return to LEGO | Passed | `browser-cove-regression-attempt01/summary.json` |
| Root visual inspection | Eight paired motion samples and three socket pairs | `comparison.json`, `comparison-1.png` through `comparison-3.png` |

The motion recipe extends the existing 6→100→6 m path to **6→450→6 m**,
over 30 seconds. The six-part/five-weld assembly and its private owner are the
original installed fixture. Both actual traces have this sector sequence:

```text
(0,-1,0) → (0,-1,-1) → (0,0,-1) → (0,0,-2) → (1,0,-2)
         → (0,0,-2) → (0,0,-1) → (0,-1,-1) → (0,-1,0)
```

Every recorded local coordinate is canonical. Reconstructed absolute positions
match the requested eye within 1 mm, including crossings. Every recorded motion
frame retains six model draws, the same generation, three authored uploads,
one prototype upload and 6,264,556 bytes of reserved GPU memory. Each pontoon
traverses LOD 1→2→3→2→1. Final GPU completion passes the last recorded submission.
No physics bodies, inventory, player builds or durable IDs are created.

The separate browser flight starts just beyond X=128, uses actual W key input
to move below the boundary, and S to return. It repeats with socket guides,
checks continued GPU completion and unchanged ownership, refuses an
unrepresentable camera request, and verifies Reset returns to the original view.
Native socket captures replay those three actual recorded camera poses. They
are separate fixed-pose launches; native continuous movement was captured in
clean mode. No live native keyboard/socket recording is claimed.

Root reviewed the three uncropped comparison sheets and full-size socket/native
boundary images. The assembly stays in place and returns to its close view;
socket overlays agree between the two renderers. Distant assembly images are
small and foggy, and the finite terrain edges are visible. These captures prove
coordinate continuity, not fine socket geometry, world streaming, art quality,
performance or an independent moving-image review. Existing close geometry
and all-rotation evidence remain necessary. Capture overhead is included.

## Reproduction

Build with the repository Nix toolchain. Use a fresh package/output directory
for each run; do not overwrite preserved attempts. Native uses the configured
1536×864 logical window to obtain physical 1920×1080 on this scaled desktop.
Browser captures use physical 1920×1080 and FOV 60 outside Nix.

```sh
nix-shell --run 'cmake --build build-salvage-native --target voxy_native -j 6'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm -j 6 && make package-wasm WASM_BUILD_DIR=build-lego-wasm WASM_PACKAGE_DIR=/tmp/new-sector-web'
nix-shell --run 'python3 scripts/capture_salvage_asset_motion.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-native-sectors --recipe docs/validation/salvage/ASSET-04/sectors/motion.json'
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_MOTION=/tmp/new-browser-sectors VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-04/sectors/motion.json VOXY_SMOKE_REPORT=/tmp/new-sector-startup.json VOXY_SMOKE_SCREENSHOT=/tmp/new-sector-startup.png node scripts/smoke_integrated_wasm.mjs /tmp/new-sector-web salvage-assembly
```

For the separate keyboard run, replace `VOXY_SMOKE_ASSET_MOTION` with
`VOXY_SMOKE_ASSET_SECTORS=/tmp/new-sector-flight` and omit the recipe variable.
The runner operates only its fresh disposable browser, uses real input and
queries application state without modifying session or GPU completion fields.
For native socket views use `capture_salvage_asset_views.py`,
`--recipe docs/validation/salvage/ASSET-04/sectors/socket-views-r02.json`,
`--config salvage_assembly_fixture.cfg --guides sockets`, and a fresh output.

The first socket recipe used digits in view names, which the existing capture
runner correctly rejected before launching native. `socket-views.json` and
`integration/native-sockets-attempt01-note.txt` preserve this harness failure.
The corrected recipe changes labels only. No application workaround was used.

Remaining ASSET-04 work: the authored hierarchy's visible composed bounds,
final reproducible candidate publication and independent technical/moving-image
review. The cove still requires its separate LOOK-01 visual proof. No gate
passed and no gate commit was created for this component checkpoint.
