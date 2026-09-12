# Dock markings candidate r01 — integration contract

Status: source-only preparation. No compilation, GPU runs, screenshots, visual acceptance or gate completion is claimed. Root owns live integration, Application, build entries and validation. The renderer base is the final mechanism overlay, not the earlier live paint checkpoint.

## Application contract

- Include `render/cove_dock_markings.hpp`. Prepare once with `makeCoveDockMarkings(const game::assets::LoadedAssetFixture&, CoveDockMarkings&, std::string&)` from the **original admitted installed fixture**, after catalogue admission is acceptable because it preserves registry placements. Never pass the mutable workshop/live/owned scene. The routine excludes navigation boat/cargo membership and prototypes and uses canonical plate metadata, not visual content identities. Unsupported layouts return false with an error, leaving the output unchanged.
- Store the successful CPU result beside the fixture content while the GPU candidate is admitted. Call `fixture.beginCandidate(content->renderBundles(), error, content->prototypes, &dockMarkings)`. The trailing argument defaults to null, preserving all existing routes. The owner copies and independently validates the bounded mesh before upload; callers may release their copy after this call. Root should make refusal visible rather than rendering an unsafe approximate layout.
- For normal Cove world rendering, set `frame.dockMarkingsRoot` to the same static-world `glm::dmat4` root used for installed dock placements: subtract the exact camera sector in double precision from `asset.origin` before the float conversion. Geometry already includes installed plate translations. Do not apply a boat COM/body frame or a second part placement. Leave the optional root empty in workshop/palette/asset inspection. Pause keeps the same geometry; no clock or simulation state is introduced.
- The field is `std::optional<glm::dmat4> SalvageFixtureFrame::dockMarkingsRoot`. Read `stats().active.dockMarkingGpuBytes` for owned storage and `stats().lastEncodedDockMarkingDraws` / `lastSubmittedDockMarkingDraws` for actual ticket state. Submitted count changes only in `submitted()`; encoding/discarding a hidden frame leaves the previous submitted count intact, and the next submitted hidden frame reports zero. Requesting markings without an admitted marking mesh, with a non-rigid/non-finite root, or without sufficient shared instance/draw capacity rejects before opening a frame ticket. Existing owner submission/discard/Leave rules cover this draw and storage.

## Rendering and storage contract

- One CPU-generated rigid mesh, one root node, two opaque lit solid PBR materials (teal lane, orange pad/arrows), at most three material draws and at most 16,384 requested GPU bytes. No textures, authored part IDs, catalogue changes, inventory, collision, save changes, new buffers, shader ABI changes or limit increases.
- Draw through the active owner's existing model `MeshPath`, with `castsSunShadow=false`; it still receives the shared scene shadow and writes normal opaque color/depth before water. Selection tint and paint override remain neutral. It never uses the unlit/X-ray guide path.
- Upload after the existing helper mesh. Thus all LOD/prototype/helper indices retain their meaning. Charge the exact newly prepared geometry/index/material bytes to both `assetGpuBytes` and `reservedGpuBytes` **before** the per-owner/resident admission check. The fixed 128 KiB reservation is unchanged; the extra asset stays inside existing 16/48 MiB limits. Owner stats expose the additional `dockMarkingGpuBytes`; it is not counted as a canonical unique LOD upload.
- Route comes from `navigation.spawn` to `navigation.dockBoarding`: move 0.5 m towards boarding, sideways early to the boarding-side lane, then towards the boarding point, clearing static collision at the player's 0.30 m radius. Orange pad surrounds the boarding socket instead of filling it. Small orange directional arrows sit between socket rows.
- Geometry lies 1.5 mm above the exact top of the canonical upright dock plates. Each of the eight one-metre molded panels is inset by 40 mm, clearing both plate edges and the real recessed cross channels. Independent cooked-geometry review found all-LOD flat half-widths 0.488 m in X / 0.486 m in Z; this candidate uses the stricter 0.460 / 0.460 m. All intersecting canonical socket-clearance projections retain 20 mm margin. Rectangles are clipped against these exact exclusions and static obstacles. Unsupported orientation, disconnected plates, invalid navigation or a blocked route refuses the candidate rather than guessing.

## Build integration owned by root

- Add `src/render/cove_dock_markings.cpp` / `.hpp` to `src/render/BUILD` target `salvage_asset_fixture`, add direct dependency `//src/game:fixture_registry`, and add the source to CMake source lists where explicit. No shader or data build entries are needed.
- Focused regressions are appended to the existing `tests/test_salvage_asset_fixture.cpp`; retain the final mechanism tests when applying this overlay. CPU cases load existing declared Cove fixture runfiles. GPU owner/draw checks are authored only and must run once after integration.

## Validation limits

Source checks and exact generated geometry metrics will be appended when this candidate is ready. A single focused native/browser normal-Cove check should assess whether the lane/pad are visible and unobstructed; this document is not visual approval. No broad new test matrix is requested.

## Candidate files and checks authored

Only these five source files belong to this renderer overlay:

- `src/render/cove_dock_markings.hpp` (new CPU/API record)
- `src/render/cove_dock_markings.cpp` (new clipping, route validation, and bounded mesh preparation)
- `src/render/salvage_asset_fixture.hpp` (trailing optional admission/frame API and stats)
- `src/render/salvage_asset_fixture.cpp` (same-owner upload, exact charge, draw and ticket counters)
- `tests/test_salvage_asset_fixture.cpp` (four focused regressions; retains all mechanism cases and root's 64-brick admission extension)

New focused cases, authored but **not compiled or run**:

1. `CoveDockMarkings.InstalledStaticPanelsKeepSocketsChannelsAndGeneratorClear`: exact static source indices 11–18, one mesh/two draws, actual geometry/material byte accounting, outward triangle winding, all triangle bounds inside independently measured flat patches and outside every canonical socket clearance and static generator footprint.
2. `CoveDockMarkings.MovingBoatAndCargoCannotMoveStaticMarkingsAndUnsafeRouteRefusesAtomically`: boat/cargo transforms cannot affect markings; moving the generator into the lane, changing/removing a route plate, unsupported orientation, or non-finite navigation refuses without replacing valid output.
3. `CoveDockMarkings.AdmissionRecountsExactStorageAndRejectsUnlitOrNonIdentityMesh`: does not trust supplied GPU counts; refuses unlit/root mutations; verifies the actual double camera-sector bridge at sector `(1000000,-2000000,3)`; confirms existing instance ABI and fixed reserve.
4. `FixtureGPU.DockMarkingsUseExactOwnerChargeSharedDrawTicketAndRetirement`: one-byte-over-budget CPU admission rejects before creating a candidate; actual owner charge increments once; missing geometry/root refuses before ticket; two draws share owner submission; hidden encoding/discard cannot advance submitted counts; hidden submission reports zero; Leave drains marking storage.

Source-only preparation includes byte equality of all three base copies with final live mechanism sources and a whitespace diff check. There are no new build, test, GPU, browser or image results. The runtime mesh preparation itself enforces the 16,384-byte requested-storage ceiling; root should record the actual successful `dockMarkingGpuBytes` value after integration instead of treating a handwritten estimate as measured.

Known gate limits: this candidate is deliberately limited to the installed upright 4x2 m dock with a short orthogonal side-boarding route. Other layouts refuse instead of guessing. New scenes need their own admitted surface/navigation contract. Colors, 0.14 m lane width and 0.86 m boarding outline still need the single combined normal-Cove visual check. No level-of-detail variants, decal system, character, new harbor gameplay or save migration are introduced.

## Final source review and arithmetic

Sibling `native_cove_hud` completed a bounded read-only review of the generator, owner integration, four cases, and root's App/build overlay: no actionable finding. No source changes were requested.

`source-arithmetic.json` records a source-level polygon arithmetic model for the installed layout: 98 vertices, 48 triangles / 144 indices, two material draws, and predicted requested storage **7,760 bytes** (7,056 vertex + 576 index + 128 material). XZ bounds are `[4.07,-53.43]` to `[6.07,-49.43]`, at Y `1.2815`. The smallest triangle's double area is 0.0042 m². This is an independent source arithmetic calculation, **not** a compiled CPU/GPU result; root should confirm the actual counter once after integration. Expected owner reservation against the submitted-tick mechanism candidate is 10,745,384 + 7,760 = **10,753,144 bytes**, within the unchanged 16 MiB ceiling.

`source-hashes.json` identifies the five source files and exact three base hashes. Only those five overlay sources belong to this renderer candidate; root's separately prepared App/build changes must be integrated alongside them. Source is ready for root's one combined mechanism/dock native and browser check. No visual or gameplay gate is claimed here.
