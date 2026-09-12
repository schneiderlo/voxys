# Current object shadows on terrain and water

Status: independent code review found a scene-coordinate bug and a finite-terrain
boundary bug missed by the initial GPU and application checks. Both are corrected
and new numerical regressions pass. Combined native 16-stage/browser 15-stage
construction, save/reload and sailing checks now pass with the continuous builder
and native HUD. The initial broad suite was stopped during compilation to avoid
duplicating it before these changes; the final mandatory suite is running. REND-03, LOOK-01 and their parent
gates remain open.

Base: `dcae5c3cb91642af38aae90915a705d37d365f95`, plus the compatible structural
art checkpoint in `../../LOOK-01/toy-art-r01/`. Scratch and frozen applications:
`build-cove-scene-shadows-gvdzhpdc/`. Exact source and binary identities are in
the final `source-hashes.json` and `package-hashes.json`; original pre-review
identities remain in the corresponding `*-initial.json` files. Final combined
packages are in `build-cove-playable-r01/`. No screenshots, image files or
state injection were used for these checks.

## Behavior and resource contract

The boat, built bricks, cargo and dock now cast sunlight shadows onto the brick
terrain and live ocean. The original mesh receivers and new terrain/water
receivers share one generated bias/filter/border-fade implementation. Water's
sun glint, sun scattering and foam respond to occlusion. Environment reflection,
ambient illumination and refracted background retain their independent lighting.
The shadowed seabed can still be seen through the surface.

The frame order is immutable terrain background/depth → current model sun map →
separate opaque terrain HDR/depth → model color → ocean → final output conversion.
Terrain re-shading uses the existing terrain material and stored geometric normal;
pixels outside object shadows copy the original cached result exactly. Moving a
caster does not invalidate or overwrite static terrain depth/background. Output
exposure stays in final composition. Zero sun intensity does not darken ambient
terrain/water when a caster moves into position.

`SceneShadowConsumer` borrows the renderer's current bind group after its map is
encoded and before model color. It is part of the existing fixture generation and
submission ticket. BlitPath retains it only on the current call's stack. No
second owner, cross-frame reference, new map, target or buffer is introduced.
Empty/admitting/leaving scenes seed the original background without a stale map.
Discard invalidates the unsubmitted cache state; resize replaces target bindings.
Move construction/assignment and shutdown transfer/release all added handles.

The application explicitly supplies `shadowFrameWorldOrigin` from its camera
sector origin. Mesh casters/receivers stay in that local coordinate frame;
`sceneSunVisibility` subtracts the origin from absolute terrain/ocean positions.
The shared sun uniform is 96 bytes, up from 80, with its actual size charged by
the existing owner. The toy-art fixture reservation is now **14,522,896 bytes**.
Do not derive this origin from a guessed camera position: the actual Cove already
uses a negative-Y sector. Ocean outside the finite terrain gets full *terrain*
sun visibility; local object shadows can still cover it.

Terrain and water have separate input layouts to remain inside WebGPU's baseline
16 sampled textures per stage. The terrain receiver uses 14 including the map;
water uses 10. Existing scene target storage stays at 12 bytes/pixel and the
existing 1024² map remains inside the fixture's 16 MiB reservation limit. Extra
pipeline/layout/group handles and a live terrain lighting pass are introduced.
There is no claim of measured display performance or final shadow quality.

`assetFixture.sceneSunShadows` is a read-only observation of the latest encoded
scene path, not a fabricated GPU-completion receipt. Application evidence also
retains real submitted/completed serials; browser startup requires retired GPU
timestamps. The numerical GPU test supplies the pixel/depth proof.

## Executed checks

- Initial focused Bazel run: **50 passed, no skips**. It covers all BlitPath cases,
  fixture owner/lifetime and registry cases, shared shader-source synchronization,
  and live GPU-pose casting with stale-generation rejection. Compiler warnings
  are errors. `checks/bazel-focused-r02-tests.xml` has individual results.
- Fixed-camera native GPU receiver check uses one final BGRA8 pixel and two
  depth values, with no image creation. At exposure 0.5, the dry red channel is
  **196 → 37 → 196** as the caster moves into and out of the light ray. Water is
  **191 → 181 → 191**. A separate caster position shadows the refracted bed and
  produces **84** while final depth remains the ocean surface (**4.23246 m**),
  ahead of the immutable bed depth (**7.05414 m**). Discard/retry restores the
  clear sample. At zero sun intensity, moving the caster changes neither
  terrain nor water ambient samples. `checks/receivers-r07.log` records values.
- The previous GPU-only caster test also passes: movement, off-screen casters,
  stale body generations, caster masks, thumbnail exclusion and ambient light.
- Initial native and WASM application builds pass. The exact copies are
  `native-r01/voxy_native` and `web-r01/voxy_wasm.{js,wasm,data}` under scratch.
- Native application: **five stages pass** using actual owned-window keyboard
  and resize events: dock, workshop, resized targets, dock return and pause.
  All retain LEGO terrain, three presentation parts, eleven physical parts and
  48 material. The process log contains no engine errors. See `native-journey.json`.
- Browser application: **seven stages pass** at 1280×800, then 1152×720: dock,
  workshop, resize, dock return, pause, drained Leave to LEGO World, and re-entry
  with a current shadow owner. All Cove stages retain the eleven-part craft,
  three presentation parts and 48 material. No uncaptured GPU error or device
  loss. See `browser-startup.json` and `browser-journey.json`.
- Complete mandatory repository suite: initial run stopped during compilation
  after review found missing cases; final run will cover the corrected source.

Those initial application runs preceded the coordinate/border review fixes;
they are lifecycle evidence, not proof of those corrections. The combined
continuous-builder journey supplies later application coverage.

The review corrections pass **seven targeted cases**, with no skips or warnings,
in `checks/frame-fix/focused-r01.xml`. Terrain and water preserve their pixel
results under origins `(0,-256,0)` and `(256,0,-256)`; nonfinite origins reject
before encoding. All four ocean borders remain **75 → 75** when the terrain's
border column is made fully shadowed, while an inside-map positive control
darkens. Copied-shader mutation checks independently restore each bug: missing
origin causes 159-level terrain and 10-level water errors; unbounded border
sampling incorrectly darkens each outside-ocean sample by 66 levels. Both
mutations fail as expected, without modifying the shared source or taking images.

## Retained failures and corrections

All attempt logs remain in `checks/`. Early compilation caught the missing shared
header and test lighting include. Test setup then exposed a one-layer upload
helper restriction and missing output CopySrc usage; both were fixed in the test.
Strict Bazel compilation caught indentation and float-promotion warnings, fixed
without disabling checks.

The first water oracle put its nominal clear caster at x=5. That position actually
shadowed the refracted seabed, correctly making the control darker. The clear
control is now x=15; x=5 is retained as a separate positive seabed-shadow case.
At exposure 1, the real surface difference was five 8-bit levels, exactly on the
test's strict threshold. Setting the diagnostic exposure to 0.5 avoids highlight
compression and yields ten levels with the original assertion. Production exposure
and water/refraction lighting were not changed to force a passing test. An explicit
zero-sun case also guards the new foam response against darkening ambient light.

## Reproduce

From the repository root, in `nix-shell`:

```bash
python3 scripts/sync_scene_shadows.py --check
python3 scripts/sync_authored_shapes.py --check
python3 scripts/sync_lego_surface.py --check
bazel test //tests:voxy_tests --test_filter='SceneShadowSources.*:BlitPathTest.*:MeshPathGPUTest.LiveSunShadowUpdatesOffscreenCastersAndRejectsStalePoses:FixtureGPU.*:FixtureRegistry.*'
cmake --build build-native-save-host --target voxy_native -j3
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j3
```

Use an active X11 display/authentication and new output directories:

```bash
python3 scripts/validate_native_scene_shadows.py --binary <built-native-app> --output <new-native-report-directory>
VOXY_SMOKE_GPU=gaming-x11 VOXY_TEST_CHROME=<chrome> VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_TIMEOUT_MS=600000 VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_PRESENTATION_PARTS=3 VOXY_SMOKE_REPORT=<new-report.json> VOXY_SMOKE_COVE_SCENE_SHADOWS=<new-journey-directory> node scripts/smoke_integrated_wasm.mjs <packaged-web-directory> salvage-cove
```

Browser runner needs Node 22+ and a short, writable, disk-backed TMPDIR suitable
for Chrome sockets. This run uses an isolated fresh profile; supplied existing
profiles are retained automatically by the runner. Build the web package by
copying `web/` and the three built WASM artifacts into a new directory.

Before committing, run the repository's mandatory command:
`bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`.

## Boundaries and next work

The catamaran intentionally has open water between its pontoons. This change does
not blank the entire boat's AABB. Dry interior exclusion requires actual authored
interior volumes and remains open with flooding integration. Underwater shafts,
particles, transparent materials, wet surfaces, far-field shadow coverage and
displayed frame-time certification also remain open. The bounded sun map covers
48 m laterally and fades at its border; this is not a cascaded regional solution.
Owner/independent visual approval and final machinery/harbor art are not supplied
by numerical GPU tests. The separate native HUD component and combined journeys
are recorded in `../../UX-01/native-hud-r01/` and
`../../PLAY-02/continuous-r01/`. The whole game goal remains active.

## Combined checkpoint verification — 2026-09-12

The final combined source passes the required repository suite: 2,067 native
cases pass, three skip and four remain disabled; terrain import has ten passes
and one skip. Both actual-control construction/save/reload journeys pass.
[Checkpoint, source/package identities and full raw check results](../../checkpoints/2026-09-12-playable-cove.md)
record the current result. Earlier pending statements and original identities
above describe their historical stage. This checkpoint does not approve final
art or complete a game gate. The commit containing the checkpoint report is
the publication unit; normal repository hooks remain enabled.
