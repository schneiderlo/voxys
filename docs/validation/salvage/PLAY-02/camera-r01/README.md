# Workshop camera and control layout

2026-09-10. Local work after the LEGO-02 builder; source parent `e5146785`.
This record covers a keyboard/mouse camera component, not full PLAY-02,
controller parity, art approval, performance certification or a G00–G14 gate.
The keyboard/mouse component passes the executed checks in [results.json](results.json).
Exact source, package and retained report hashes are in [artifacts.json](artifacts.json).
This work is local and uncommitted, after the previously published main checkpoint.

## Player controls

- **G / Focus part** frames the selected part, including a placement ghost.
- **M / Whole boat** frames all current build parts. Opening the workshop also
  frames the boat instead of leaving the camera centered on the starter winch.
- **Right-drag** orbits horizontally and vertically.
- **Shift + right-drag** or **middle-drag** pans in the view plane.
- **Mouse wheel** zooms. Existing view buttons, A/D and W/S remain available.

Framing keeps the model clear of the native brick thumbnails or browser side
panel. Below 700 CSS pixels the browser panel becomes a scrollable bottom panel
and the model fits above it. Camera controls use a two-column grid; protected
design controls have their own expandable section. Scroll over that panel
scrolls the UI. Returning to the canvas hands keyboard focus back to the game.

These controls do not modify construction, spend material, launch a boat or
move its physical bodies. Camera gestures suppress left-click placement while
the gesture is active, including a quick gesture within one displayed frame.
The existing save format, catalogue and owned part identities are unchanged.

## Implementation and bounds

`WorkshopCamera` in `src/game/expedition/workshop_camera.*` owns presentation
state only. Distances are bounded to 2.5–512 m; elevation avoids the vertical
singularity and yaw wraps. `CoveWorkshop::viewBounds` combines every admitted
LOD's actual composed canonical drawable bounds, transformed by the current
part placement. Prototype fallbacks use their declared footprint. Scenery and
palette thumbnails are excluded. The chosen bounds include actual studs.

Framing solves all four perspective-frustum inequalities for all eight bounds
corners. It accounts for corner depth and the asymmetric screen rectangle left
available by the UI. Offsetting the view center preserves the actual build
pivot while keeping that pivot inside the available area. Resize reframes the
last selected scope; an overlarge or invalid request preserves the prior view.
Pan sensitivity is proportional to distance and vertical projection scale.
Canonical build coordinates remain independent of render-sector rebasing.

Mouse input now records motion only while each button is physically held.
It retains a complete press/move/release that falls between displayed frames,
excludes movement before/after the drag, and clears held motion on focus reset
or queue-pressure fallback. Native and WASM use the same bounded bookkeeping.

WASM still handles captured wheel events at document level, but unlocked wheel
events outside the canvas belong to the UI. Mouse release is observed on the
document so ending a drag over a panel cannot leave a camera button held.
Shift can pass through a focused workshop control as a gesture modifier;
ordinary editing/action keys remain in the focused UI. Canvas pointer-down
restores canvas focus without taking pointer lock in the workshop.

## Verification

Scratch: `build-workshop-camera-3s8ze75b/`. Both application builds and the
focused checks use the existing Nix development shell.

```bash
bazel test //tests:input //tests:workshop_camera --test_output=errors
cmake --build build-native-save-host --target voxy_native workshop_camera_tests -j4
build-native-save-host/bin/workshop_camera_tests
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j4
node scripts/test_salvage_preview.mjs
```

The three camera cases project tall/asymmetric boxes through independent GLM
view/projection matrices over landscape/portrait aspect ratios and varied
azimuth/elevation; all corners must remain inside the unobstructed rectangle.
They also check pointer-relative pan, bounded zoom, invalid-input refusal and
recovery through framing. The 33 input cases include same-frame drag retention
and focus reset. Browser UI checks total 22 cases, including camera availability
while diagnosing a blocked ghost and focus-safe modifier routing.

Actual-control scripts:

```bash
VOXY_SMOKE_GPU=gaming-x11 VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_WORKSHOP_CAMERA=/tmp/voxys-camera-browser-new \
VOXY_SMOKE_REPORT=/tmp/voxys-camera-startup-new.json \
node scripts/smoke_integrated_wasm.mjs \
  build-workshop-camera-3s8ze75b/web-r03 salvage-cove

python3 scripts/validate_native_workshop_camera.py \
  --binary build-workshop-camera-3s8ze75b/native-r02/voxy_native \
  --output /tmp/voxys-camera-native-new \
  --source-slot build-cove-builder-3bh5dr4t/native-saves-r02/df314a0502c8ef92ad8d9a2419a7a5f7
```

The browser validation reuses the real eight-brick saved world
`963d52c06b195929566d224ffa3d168b` in profile `/tmp/voxys-startup-L5Kxc5` on
origin port 35651. Set `VOXY_SMOKE_PROFILE`, `VOXY_SMOKE_KEEP_PROFILE=1`,
`VOXY_SMOKE_PORT` and `VOXY_SMOKE_RESUME_WORLD` to those values to reproduce
that exact starting state, after checking profile/origin ownership. Native
copies both original save generations unchanged into its isolated output root.
The source saves and earlier previews remain available.

Final browser r04 passes eight stages; native r01 passes seven. Both retain
the same 18-part, 1,149 kg boat, paid IDs 35–42 and 23 material. The browser
compares exact blueprint bytes across camera changes and cancelling a ghost;
it also verifies a 600×960 viewport with no horizontal panel overflow. Its seven
recorded UI clicks are trusted mouse events. The final startup/drain report
passes too. Earlier failures were not counted as passing journeys.

The owner preview is [the updated Cove](http://127.0.0.1:39535/index.html?experience=salvage-cove),
served from the frozen `web-r03` package. This new origin starts a fresh world;
existing browser saves remain on their original origins. Open B / Workshop at
the dock to use the brick palette and new camera controls.

The scripts exercise focus, whole-build framing, orbit, pan, wheel, ghost
placement isolation and return to the dock, checking retained parts and stock.
The browser also releases a drag over UI and resizes to a narrow screen.
Only ordinary controls and read-only observations are used; no screenshots,
state setters, cargo injection or substituted physics.

## Retained failures and remaining scope

Browser r01 found that skipping the first pressed frame dropped a short orbit
gesture entirely. Per-button held-motion accumulation fixes the actual input
loss. Browser r02 then found Shift was swallowed by a previously focused
workshop button; preserving modifier routing and restoring canvas focus fixes
that handoff. Browser r03 passed through wheel routing, then exposed a validation
driver click aimed at a moving button after panel scrolling. The final driver
waits for a stable button location before sending its real click; production
code did not change for that driver correction. Browser r04 then passed all
eight stages. Initial input compilation needed an explicit GLM common-functions
header. The first CMake invocation also preceded registration of the new test
target; reconfiguration adds it. Failed logs remain in the scratch directory.

Camera collision with scenery, controller gestures, final native HUD, art,
lighting, animation and broader frame-time work remain separate requirements.
This camera has no cinematic smoothing, saved camera preset, new gameplay
authority or human-usability certification.

Publication note: the verified component above is included in the subsequent [working main checkpoint](../../checkpoints/2026-09-10-brick-builder.md). Earlier local/publication statements describe the original verification time.
