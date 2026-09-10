# Native and browser inspection guides

Follow-up: [assembly work](assembly.md) revealed that paired socket guides were
fully hidden inside the opaque assembled parts. The current implementation uses
a labeled X-ray pass with separate buffers in the same fenced owner. Its fixed
reservation is now 128 KiB; the original numbers and depth-tested captures below
preserve this earlier checkpoint, not the current allocation or overlay mode.

This is an ASSET-04 implementation checkpoint. ASSET-04, LOOK-01 and G02 remain
open. The guides are technical inspection aids, not accepted game art. Continue
from `GAME_IMPLEMENTATION_TODO.md` and `ASSET-04/design.md`; the conversation is
not required to understand this checkpoint.

## Behavior and geometry

The installed `data/salvage/fixture-pontoon-v2.json` still selects the unchanged
v2-r04 cooked pontoon and its four placements. No Blender source, texture,
VMESH, physics body or inventory item was changed or created for the guides.

- Clean view is the default. `G` cycles modes on native and browser; browser
  buttons select them directly. Public inspection actions 20/21/22 select
  off/dimensions/sockets. Invalid or busy/Leaving requests reject.
- Dimensions mode draws the first placement's actual selected-LOD render bound,
  including the pegs. It draws a four-metre length ruler with one-metre marks,
  a .96 m body-height ruler with .32 m plate marks, and an asymmetric axis stand.
  These are 26 shared cube instances for this pontoon. Ruler bounds come from
  the admitted part footprint; render bounds come from its prepared hierarchy.
- The stand's red arm is .6 m toward canonical +X, green .8 m toward +Y,
  and blue 1 m toward forward -Z. Socket triads have a different explicit
  convention: red is socket-local +X key direction, green outward +Y, blue +Z.
- Socket mode covers all six sockets on each of the four placements. Three
  axis bars and twelve clearance edges per socket give 360 guide boxes. The
  source is the admitted canonical socket frame and clearance, never glTF
  coordinates or visual inference. Clearance is not additional solid volume.
- Bars are .012 m thick, opaque and unlit, with ordinary depth testing and
  backface culling. Hidden internal socket guides are occluded by the model;
  this is not an X-ray or cutaway. The underside close-up exposes the wells,
  axes and clearance outlines. Switch to clean view to inspect visible joins.
- A proper part rotation and camera-relative root are composed in double
  precision before the final float conversion. No exporter basis is applied
  to socket, ruler or clearance metadata.
- `[game] asset_fixture_guides` accepts `off`, `dimensions`, or `sockets`.
  Non-off values require the explicit inspection registry. Malformed or
  unknown values reject; native/browser entry points carry the setting through.
  Reset restores automatic detail and clean view without uploading new meshes.

`src/render/inspection_guides.*` owns pure CPU helper geometry.
`SalvageAssetFixture` owns the single shared unit cube and all submitted guide
instances with the same real queue fences, error scopes, view rebinds and
exceptional teardown as the authored assets. Nothing uses legacy primitive
submission or global physics-instance buffers.

## Resource and failure contract

The cube has 24 outward-facing vertices, 36 u32 indices, one unlit material and
no textures. Requested GPU storage is exactly 1936 bytes: 1728 vertex, 144 index,
64 material. Its validated prefab accounting must match before GPU publication.
Together with 512 96-byte instances, 144 uniform bytes and two 1x1 fallbacks,
the owner requests 51240 fixed bytes, within its existing conservative 64 KiB
reservation. The cube is uploaded once per owner generation, even if guides
start hidden. It is reported separately as `guideMeshGpuBytes`; `uploads`
continues to count authored LOD assets only.

The 256 authored-node, 512 expanded-draw, 32 placement, 16 MiB owner and 48 MiB
combined-resident ceilings remain unchanged. Authored and helper instances share
the 512-draw buffer. Incomplete overlays reject transactionally before any GPU
write or frame ticket, rather than silently hiding late sockets. A large valid
catalog fixture can therefore exceed the overlay ceiling; the current installed
four-pontoon fixture fits. The application surfaces an encode failure as a
terminal inspection error. This is a diagnostic route, not gameplay recovery.

The four pontoons still share three LOD uploads and a 6197084-byte conservative
GPU reservation. Guide toggles do not change the reservation or generation.
These figures are requested storage, not staging, allocator overhead, driver
working set, browser process memory or a performance acceptance measurement.

## Verification and preserved attempts

- Optimized Bazel: all 15 inspection/owner cases and all 58 configuration cases
  pass. Four new CPU cases cover outward face winding/storage, exact ruler
  spacing, all 24 proper rotations on both sides of a camera-sector rebase at
  a 1e9 m world origin, and transactional rejection. The ten owner cases use
  real native WebGPU; one new case exercises guide submission/toggle, unchanged
  resources and an over-capacity overlay without opening a frame ticket.
- Native CMake: the same 15 inspection/owner cases plus the changed configuration
  case pass: 16 total, zero skips. The native application also builds.
- Browser UI lifecycle suite: six cases pass, including pending Reset/failure
  suppression and removal of guide event handlers.
- The actual shipping browser package, O3/Closure/JS exceptions/Asyncify, passes
  `guides-browser-journey-attempt01/summary.json`. Real clicked controls select
  all three guide modes and LODs. GPU completion is observed after each change.
  Resize, flying, Reset, guide-active GPU-drained Leave, original-world return,
  re-entry and another Reset pass with no uncaptured errors or device loss.
- A separate fresh browser passes real `GPUDevice.destroy()` with all 360
  socket guide boxes active: submitted serial 193, completed 191 at injection.
  The owner is released, initialization becomes false, the actual loss reason
  is `destroyed`, and no uncaptured GPU errors appear. See
  `guides-browser-loss-attempt01/summary.json`. No synthetic loss flag or native
  device-destroy stub is used as evidence for this result.
- Native and browser captures use the same 1920x1080 physical viewport, FOV 60,
  camera and lighting recipes. Native logical windows remain 1536x864 on this
  host; browser emulation is 1920x1080 at DPR 1. No FPS claim follows from the
  on-screen counter or these instrumented runs.

Native captures live in `guides-native-rulers-attempt01/` (front and material
view), `guides-native-sockets-attempt01/` (underside and engaged), and
`guides-native-stand-attempt01/`. Browser counterparts use the same names with
`browser` replacing `native`. Their reports preserve exact requests, image
hashes and original configuration/registry identities. The first four captures
used the original nine-view recipe. A tenth `guide-stand` view was then added:
the first front view put part of the axis stand behind the browser panel, so
the wider angle shows all three arms and both rulers without hiding controls.

Root visually inspected the native ruler/front/material/underside/engaged/stand
captures and matching browser front/underside/stand captures. Geometry and
placement agree in the inspected regions. These are actual application PNGs,
not Blender renders or edited screenshots. This root inspection is not the
independent technical review required to complete ASSET-04.

Logs are in `integration/guides-*`. Failed attempts remain visible:
an incorrect nonexistent Bazel `linux` configuration, the first test exposing
an accidental increase of the authored-instance ceiling (fixed to preserve
256), and cached build-tool setup corrections. The initial browser build
referenced a removed Nix store Make executable. Native CMake uses Ninja, so its
first attempted Make override was corrected to the actual Ninja executable.
No repository tests or compiler diagnostics were bypassed to obtain a pass.

Exact final source, binary, package, test and image identities are recorded in
`integration/guides-summary.json`. The prior `clear-inspector-summary.json`
remains a historical record of the earlier layout/lighting implementation.

## Reproduction

From the repository root in the normal graphics-capable development shell:

```sh
bazel test -c opt //tests:salvage_asset_fixture //tests:config //:voxy_native --test_output=errors
node scripts/test_salvage_preview.mjs
python3 scripts/capture_salvage_asset_views.py --binary /absolute/path/to/voxy_native --output /new/output/directory --guides dimensions --views guide-stand
```

Build the shipping WASM application using the configured Emscripten/CMake
workflow, then `make package-wasm WASM_BUILD_DIR=build-lego-wasm
WASM_PACKAGE_DIR=/tmp/voxys-asset-browser-05`. The existing directory is the
verified local package, not a permanent distribution artifact. Future runs
must use fresh output directories and record their own package hashes.

Run Chrome outside the Nix GPU-library environment. The browser runner starts
its own disposable hardware browser and local server:

```sh
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_FIXTURE=/new/journey/directory node scripts/smoke_integrated_wasm.mjs /tmp/voxys-asset-browser-05 salvage-asset
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_VIEWS=/new/views/directory VOXY_SMOKE_ASSET_GUIDES=2 VOXY_SMOKE_ASSET_VIEW_NAMES=underside,engaged node scripts/smoke_integrated_wasm.mjs /tmp/voxys-asset-browser-05 salvage-asset
```

`VOXY_SMOKE_REPORT` and `VOXY_SMOKE_SCREENSHOT` select startup evidence paths.
The loss runner uses `VOXY_SMOKE_ASSET_LOSS=/new/loss/directory` in a separate
fresh process; it enables all 360 socket guide boxes before real device loss.

## Next work and acceptance boundary

1. Render the existing v1 prototype crossbeams at `{0,48,-50}` and `{0,48,50}`
   ticks with the v2 pair at X ±75 ticks. Validate the complete same fixture
   through `BuildModel`. Its box visual must be labeled: it has no bottom well.
   Use bounded, accounted mesh ownership; do not spawn fake physics/inventory.
2. Finish the asymmetric authored hierarchy/winding stand and rotated assembly
   scene evidence. The guide CPU rotation proof is not a rendered 24-assembly
   matrix. Preserve the prior actual-mesh/BuildModel CPU tests.
3. Capture slow projected-size LOD transitions in native and browser. Fixed
   LOD buttons are already tested but do not prove temporal silhouette quality.
4. Publish the reproducible candidate to the final content path only after the
   required independent technical review, then complete ASSET-04. Keep v1 and
   historical source/cook provenance intact.
5. Continue ASSET-05/06/07 and LOOK-01: the actual cove appearance still needs
   its own accepted composition, materials and water integration.

No gate passed at this checkpoint. No gate commit was made.
