Follow-up: the [ruler and socket guide checkpoint](guides.md) is now implemented and verified. This report preserves the earlier layout/lighting correction.

# Clear asset inspection scene — 2026-09-08

The authored pontoon inspection route now has **no obstructing cove scenery**
and a brighter ambient-light setting. The original large cyan hull and mast
are absent from this route, and the pontoon's right side and underside wells
are readable in actual native/browser rendering. This is inspection progress;
the game cove, functional craft, ruler/socket overlays and visual gates remain
unfinished.

## Implementation

`SalvagePreview::Scenery::Inspection` shares the frame-boundary Reset/Leave
control flow while owning zero scene-body lifetimes. Its Reset completes the
empty scenery transition without requesting a zero-byte metadata readback.
Leave still supersedes a queued Reset. Initialization validates the scenery
enum and active state, and reinitialization can select the ordinary cove.

The Application selects this layout only when a cooked asset registry is
configured. It captures physics lifetime metadata only when scenery bodies
exist. The separate `SalvageAssetFixture` still owns the model uploads and
waits for actual GPU queue completion on Leave. The browser remains active
while that owner drains, even after the empty scenery's Leave is complete.

The ordinary cove defaults to its original 32-body layout and uses completed
GPU metadata to retire those bodies. Inspection has zero bodies throughout
startup, Reset, resize, Leave and re-entry. The scene props do not represent
inventory or gameplay entitlement in either layout.

`salvage_asset_fixture.cfg` changes the linear ambient multiplier from
`[0.17,0.20,0.23]` to `[0.70,0.75,0.82]`. MeshPath samples linear sky radiance
and preserves this multiplier's magnitude. The previous primitive path had
normalized the tint, so transferring the same small values made the PBR
surfaces much darker. This change uses the existing material shader and
authored texture maps; it does not bake lighting into the texture.

## Verification

| Evidence | Result |
|---|---|
| `integration/clear-inspector-native-tests-attempt02.log` | Ten focused cases pass through optimized Bazel: nine CPU lifecycle cases plus the existing real GPU-retirement case |
| `integration/clear-inspector-cmake-build.log`, `clear-inspector-cmake-tests.log/.xml` | Native application/tests build; the same ten cases pass through CMake |
| `integration/clear-inspector-wasm-build-final.log`, `clear-inspector-wasm-package.log` | Actual shipping browser configuration rebuilt and packaged from the final source |
| `clear-inspector-native-attempt01/` | Native inspector at 1920×1080 and ordinary cove at 1600×900 both launch, capture, and exit naturally without application/GPU errors |
| `clear-inspector-browser-attempt01/journey/summary.json` | Actual browser fixed/automatic LOD, resize, flight, Reset, GPU-drained Leave to LEGO World, re-entry and second Reset pass |
| `clear-inspector-browser-loss-attempt01/loss/summary.json` | Fresh browser's real `GPUDevice.destroy()` reaches terminal application teardown, releases the asset owner, and produces no uncaptured GPU errors |
| `clear-inspector-browser-views-attempt01/` | Nine camera/light recipes captured at 1920×1080; zero cove bodies throughout; right side and underside visually inspected |
| `clear-inspector-native-views-attempt01/` | Same nine recipe views at 1920×1080, all natural exits and zero errors; source registry preserved and temporary selections removed |
| `clear-inspector-cove-regression/journey/summary.json` | Ordinary browser cove still owns 32 bodies; its complete Reset/Leave/control-priority/re-entry journey and original routes pass |

`integration/clear-inspector-summary.json` pins the source, native/browser
artifacts, test reports and all 18 corrected scene images. The prior material
chart renderer source remains unchanged by this layout/config correction.

The new CPU lifecycle case preserves an unrelated live body through eight
inspection resets and Leave, confirms no scenery spawn/destroy calls or GPU
readback requests, and then reinitializes the real cove. Existing cove tests
still cover delayed/stale metadata, generation reuse, bounded destruction,
rollback and true GPU retirement. No physics-retirement proof is inferred
from the inspection's empty body set.

The browser Reset checks now require a fresh completed model-frame serial
after the request. Leave records `Drained`, no active generation and zero
reserved model bytes before navigation. Repeated parts continue to share
the same three LOD uploads and 6,197,084-byte reservation while active.
These are correctness runs, not performance acceptance.

Failure history: the first native compile used `Scenery::None`, which collided
with X11's `None` macro. The public enumerator was renamed `Inspection` without
altering platform headers. The final native and browser builds use that name.

## Next required work

Build the actual bounded ruler/axis/socket-clearance stand around these
unobstructed models, assemble the crossbeam fixture, and inspect a slow LOD
transition. The model is static and has no live compound/buoyancy simulation.
The starter asset kit and deliberate cove composition still belong to
ASSET-05/06 and LOOK-01. ASSET-04 also needs candidate publication and the
required independent technical review. No task or gate is newly accepted by
this lighting/layout correction alone.
