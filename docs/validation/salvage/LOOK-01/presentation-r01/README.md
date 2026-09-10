# Cove shadows and compact controls

2026-09-10. Scoped presentation work after published main `ddaa9057`.
The complete LOOK-01 and REND-03 gates remain open. No screenshots or final
visual approval are claimed. The focused component and actual-control checks pass.

## Player-facing changes

The authored boat, dock and cargo cast sun shadows onto authored parts.
Stacked bricks, equipment and dock connections therefore occlude direct light.
The shadow pass uses the same current GPU body poses as the visible meshes.

The browser Cove panel prioritizes Workshop, nearby interaction and session
controls. Job details and winch/lift/cutting controls expand on demand.
The workshop puts the brick palette, selected part, rotate/remove/keep/undo,
and Launch first. Camera buttons, machinery settings, precise movement and
recovery controls remain in expandable sections. Keyboard bindings remain.
Native palette thumbnails do not cast shadows into the world.

A structurally valid ghost turns amber when the current pointer ray has no
supporting part. The browser explains that the player can point at a part or
Keep the current position. Invalid geometry stays red and cannot be kept.
This does not alter socket admission, saved layouts, ownership or placement
authority. Read-only workshop state distinguishes `pointerTarget` from `valid`
and reports pending camera framing for reliable application-level checks.

## Rendering contract

- Cove water-anchored fixtures opt into `MeshPathConfig::sunShadows`.
  Other rendering routes retain their existing lighting.
- One 1024 × 1024 Depth32Float map covers a 48 m square and 128 m depth
  around the camera. Its projection snaps to texels within the current
  camera-sector frame. The border fades before reaching the map edge.
- Casters use the same rigid hierarchy, COM/principal-frame reconstruction,
  GPU pose buffer and body-generation rejection as the color pass. No CPU
  transform readback or stationary-camera shadow cache is introduced.
- The depth pass precedes mesh color. Its binding contains only the light
  matrix; it never samples its own depth attachment. The later color pass
  samples the completed map with nine comparison taps and geometric-normal
  bias. Only direct sun is attenuated; skylight and emissive energy remain.
- Off-camera mesh casters remain admitted to the bounded draw list. Camera
  frustum culling is bypassed for this local shadow mode; GPU clipping handles
  the light volume. The existing 512 expanded-draw ceiling is unchanged.
  The shadow pass can replay up to 512 additional draws. Existing fixture
  `draws` telemetry counts color/guide draws, not that extra shadow replay.
- Masked materials retain alpha cutouts. Blended materials, translucent ghosts
  and explicitly excluded palette thumbnails do not cast opaque shadows.
- Each fixture generation adds a conservative 4,194,384-byte reservation
  (depth map plus matrix) to the existing fixed/environment reservation.
  The 16 MiB owner and 48 MiB combined ceilings are unchanged. Publication,
  discarded encoders, replacement and Leave use the existing submission tickets
  and GPU completion fences. Legacy paths use an inert 1×1 depth binding.
- Mesh shadows do not mutate cached terrain HDR/depth. Existing composition
  still sends linear mesh color and radial depth through water and one final
  output transform. Asset registry bytes, geometry and saves are unchanged.

## Focused checks

Both native and WASM applications build. The 32 native rendering/fixture cases
in [native-render.xml](checks/native-render.xml) pass. These cover live and
static off-camera casters, stationary-camera movement, stale identities, alpha
cutouts, palette exclusion, unchanged ambient light, existing material/color
transport, opaque depth composition, owner budgets, discard/retry, replacement,
guides and retirement. The 22 browser UI lifecycle/hold/save cases pass in
[browser-ui.log](checks/browser-ui.log).

The first new shadow test failed because its test uploader selected the
single-object buffer overload for a fixed-size span, uploading a span descriptor
instead of the pose/shape arrays. An explicit dynamic byte span fixes the test
data. A static-caster comparison isolated that error; the failure is retained
in [first-shadow-test.xml](checks/first-shadow-test.xml). Assertions were not
relaxed.

The actual native camera journey passes seven stages using an isolated copy of
an older saved 18-part brick boat. The browser camera journey passes eight stages,
including orbit/pan/zoom, gestures over the panel, portrait framing and unchanged
design/ownership. Results are in [native-camera.json](checks/native-camera.json)
and [browser-camera.json](checks/browser-camera.json). At 1280×800 the normal
352×491 panel covers about 17% of the viewport without horizontal overflow.
The camera checks use the presentation build before the subsequent amber
pointer feedback; final native and WASM application builds include that feedback.

Browser setup failures are retained in
[browser-attempts.json](checks/browser-attempts.json). Missing Xwayland display
authorization, temporary cache/shared-memory failures and a too-long Chrome
socket path were observed. The working launcher uses host Node/Chrome, the
current display authorization, and a short path to disk-backed temporary storage.
No old profiles or saves were deleted. Chrome signal exits now fail promptly.

An unused Emscripten image preload plugin also attempted to decode all assets
before startup and failed on a JPEG. It was removed: the original files still
populate the virtual filesystem, and existing C++ stb_image/VMESH loaders still
decode them. No runtime uses Emscripten `preloadedImages`. Raw asset bytes and
content identities are unchanged.

Earlier brick-driver attempts assumed a selected part, aimed through stud gaps
or beyond a growing stack, then read a camera before its frame request applied.
The driver now selects the named cradle through the UI, frames the growing
boat, waits for actual submitted application frames, and requires a current
pointer target at the expected stacking height. Its bounded visible top-surface
search still uses real pointer moves and clicks. It does not inject layouts,
disable collision validation or substitute Keep for a required pointer click.
The old green ghost could remain after the ray missed; that exposed the amber
feedback issue above. Temporary diagnostic log statements were removed.

The final browser package passes eight actual brick placements, rotation,
overlap refusal, removal/undo, physical launch, exact owned save/reload, boarding
and sailing. These are 14 recorded stages across a saved continuation:
[browser-build-save-sail.json](checks/browser-build-save-sail.json).
R16 finished construction and reload before the runner's 240-second deadline
killed Chrome during the final sailing step. R17 reopened that isolated profile
at the same origin, checked all eight saved brick transforms and continued to
sailing within a 600-second ceiling. R16 remains recorded as failed; it was not
relabeled. R17 and its browser wrapper pass with no browser errors on the AMD
RDNA 3 hardware adapter (`fallback: false`). The 18-part boat remains 1,149 kg
with the exact eight paid identities. The final fixture reserves 16,115,144 bytes
and submits 44 color draws, excluding the extra shadow replay described above.
The runner now records its own timeout explicitly to distinguish it from an
unexplained browser connection loss.

The mandatory repository targets pass: 2,054 native cases, three existing skips
and four disabled cases; the unchanged terrain-import target passes from cache.
The native aggregate took 1,062.4 seconds. The separate MeshPath pixel tests are
covered by the 32-case CMake run above; they are not part of that aggregate.
See [required-summary.json](checks/required-summary.json),
[required-native.xml](checks/required-native.xml) and
[required-terrain.xml](checks/required-terrain.xml). The normal pre-commit hook
is retained and must accept these same targets before publication.

Scratch and frozen application package:
`build-cove-presentation-4yo4yym3/`. Final binaries are `web-r03/` and
`native-r02/voxy_native`; exact hashes are in [artifacts.json](artifacts.json).
Earlier packages, browser failures and saved worlds remain in place.
The final source hashes are in [sources.json](sources.json).

## Reproduction

Build the native and WASM applications with the repository README instructions.
Run `node scripts/test_salvage_preview.mjs`. The focused native executable filter
is `MeshPathTest.*:MeshPathGPUTest.*:MeshEnvironmentGPUTest.*:MeshOpaqueGPUTest.*:FixtureGPU.*:InspectionGuides.*:SalvageAssetFixture.*`.
These GPU tests need a working Vulkan adapter and retain numeric results only.

Copy `web/` plus the freshly built `voxy_wasm.js`, `.wasm` and `.data` into a new
package directory. Run the actual-control driver with the package as its first
argument and `salvage-cove` as its second:

```sh
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_GPU=gaming-x11 \
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 \
VOXY_SMOKE_TIMEOUT_MS=600000 \
VOXY_SMOKE_KEEP_PROFILE=1 VOXY_SMOKE_REPORT=/tmp/cove-presentation.json \
VOXY_SMOKE_COVE_BRICKS=/tmp/cove-presentation-bricks \
node scripts/smoke_integrated_wasm.mjs path/to/fresh-package salvage-cove
```

Use a fresh report directory and a valid local graphical session. On this host,
the successful run also set `VOXY_TEST_CHROME=/opt/google/chrome/chrome`,
`XAUTHORITY` to the current Xwayland authorization file, and `TMPDIR` to
`/tmp/vp4yo4yym3`, a short symlink to the scratch directory's disk-backed
`browser-tmp/`. Those local paths are environment-specific; discover the current
ones rather than copying a stale display authorization path. Do not delete or
reuse an owner's save profile. The camera driver is selected separately with
`VOXY_SMOKE_WORKSHOP_CAMERA`; do not repeat already passing journeys without a
relevant change or unresolved failure.

## Remaining work

Terrain and water do not yet receive these mesh shadows, and terrain does not
cast into this mesh map. Dry-hull water exclusion, general ambient occlusion,
larger-world shadow cascades, camera-sector transition stability, terrain/boat
art, final materials, native HUD and an actual scene-quality review remain.
This component has no displayed-frame performance certification and does not
claim the plan's 2.5 ms dynamic-geometry/shadow budget.
