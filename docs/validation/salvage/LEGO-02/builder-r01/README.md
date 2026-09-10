# Individual brick builder — LEGO-02

Date: 2026-09-10. Source parent: `e51467858a979837c133634e62b7ef92ceb29ce5`.
This bounded milestone passes. It is subsequent local work; the parent above
was already pushed to main at the owner's request and the remote SHA was
reverified after these checks. No screenshots or state setters were used.

## Play it

Launch the actual Cove with `--config salvage_cove.cfg`, or use the browser
route `?experience=salvage-cove` in the package recorded in `artifacts.json`.
The local owner preview is
`http://127.0.0.1:40367/index.html?experience=salvage-cove` (server session
32779). Opening was queued in the app; prior previews remain running.

1. At the starting dock, open **Workshop / B**.
2. Select **Cargo cradle** (Next / Tab once), then **Remove / Delete** and
   **Keep / E**. This frees the front deck without blocking boarding or the helm.
3. Choose a colored brick in the palette: **1** = 1×2, **2** = 2×2,
   **3** = 2×4. The desktop thumbnails are clickable actual brick meshes.
4. Point at the cleared deck or an existing brick. **R** rotates the ghost.
   Click a green ghost to keep it. Red means the proposed connection is invalid.
5. Click an existing part to select it; click the selected part to move it.
   **Delete**, **E**, **U** remove, keep, and undo. **Backspace** discards a draft.
6. **Launch / Enter** applies the kept design and its displayed cost. Walk to
   the dock edge, board with **E**, then use the helm with **E**. **W/S** drives;
   **A/D** steers.
7. Close the workshop and pause with **P**. Save with **F10** on Linux or
   **Save expedition** in the browser. Wait for confirmation. Reopen the same
   saved world to resume; browser storage requires the same profile and origin.

The eight-brick demonstration uses sizes 2×4, 2×2, 1×2, 2×4, 2×2, 1×2, 2×2,
1×2, stacked after clearing the cradle. Rotate the third brick relative to its
support. It costs 25 of the fresh world's 48 materials and produces 18 active
boat parts at 1,149 kg. Previewing a part spends nothing; Launch uses the
existing authority transaction. Removed starter loans do not give a refund.

## Implementation

- `data/salvage/cove-bricks-r02.json` appends the existing r02 authored 1×2,
  2×2 and 2×4 bundles. The original `fixture-cove-r01.json` remains byte-exact.
  This preserves installed world identity for old saves. Admission verifies
  exact manifests, meshes and gameplay keys and refuses replacing any installed
  part ID, even at another version. Layout/navigation fields are forbidden in
  extensions; partial catalogue admission never publishes.
- Both native and WASM configuration and packaging load the same catalogue.
  Added owned parts resolve against admitted definitions, including bricks
  absent from the original starter placements.
- Picking transforms the pointer through the actual camera into the workshop
  build frame. It tests canonical collision shells. Socket mating chooses the
  nearest exact transform while preserving the selected orientation, including
  an invalid red candidate rather than silently moving to a different site.
  Click, Keep, Remove, Undo and Launch retain the existing owned construction,
  pricing, collision, mass, buoyancy and save paths.
- The workshop orbit target stays fixed while a placement ghost moves. Manual
  arrow movement leaves pointer-placement mode. **C** still cuts welds outside
  the workshop. The browser palette respects unfinished edits and pending work.
- Socket reconnection indexes world socket positions and retains deterministic
  ordering. Auto-snap retains the nearest 256 distinct candidates and preserves
  a brick's rotation. Canonical overlap uses the widest-axis sweep so a vertical
  stack does not spend its pair budget on distant parts. Box-union surface
  clipping indexes opposite face planes, preserving cell/face order and exact
  exposed geometry. Existing overlap, surface and work-limit oracles pass.

## Explicit budgets

| Resource | Bound / evidence |
|---|---|
| Admitted cooked bundles | 16 |
| Scene placement slots | 96, including installed scenery and retained slots |
| Scene welded connections | 1,024 |
| Independent rigid roots / water drivers | 32; unchanged |
| Starter entitlement recipe | 32 parts / 64 welds; unchanged |
| Workshop edit history | 32; unchanged |
| Recovery design storage | Four designs, each at most 128 KiB |
| Renderer placements | 96 scene + 3 presentation-only native thumbnails |
| Unique renderer mesh assets | 48; shared LOD uploads |
| Renderer mesh instances / expanded draws | 256 / 512; unchanged |
| Canonical parts / collision proxies / socket records | 256 / 2,048 / 8,192; unchanged |
| Candidate pairs / union clip work | 262,144 / 8,388,608; unchanged |

The real 64-large-brick recipe has 74 boat parts, 89 occupied/retained scene
slots, 1,893 collision cells and 1,893 water cells. Its recovery design is
41,024 bytes. It compiles, loads into the workshop, round-trips the canonical
design, and maps into the launch scene. A candidate beyond the scene-slot
budget is refused atomically and the previous design remains exact. An
oversized starter entitlement is separately refused.
The full design also passes normal workshop selection, removal, Keep and Undo,
restoring the byte-exact blueprint.

This proves that layout against the stated limits. Different dense or complex
designs can meet another explicit shape/work limit sooner. This is not a
64-brick frame-rate result or a claim that every arrangement is seaworthy.

## Executed checks

Scratch and complete raw logs: `build-cove-builder-3bh5dr4t/`. Frozen application
packages: `web-r03/` and `native-r01-executable/voxy_native` under that directory.
The package/hash inventory and compact result records accompany this report.

- Seven Bazel targets pass: `fixture_registry`, `config`, `build_model`,
  `compiled_assembly`, `assembly_collision`, `assembly_buoyancy`, `cove_save`.
  This includes malformed/atomic catalogue refusal, canonical overlap/surface
  checks and prior SVCE version-1 through version-4 compatibility.
- Fifteen focused native workshop, ownership and root regression cases pass.
- Full `//tests:cove_player`: 56 cases pass, including actual GPU collision,
  root and recovery consumers. The 64-brick CPU capacity case also passes with
  the cell-count and byte properties in `capacity.xml`.
- Renderer `//tests:salvage_asset_fixture`: all 21 cases pass after the two
  old-capacity test assumptions described below were corrected.
- Browser UI: 21 cases pass. Browser save-host tests: 17 pass.
- Both applications build. Native and browser actual-control journeys build
  eight bricks, rotate, reject overlap, remove/undo, launch and save. Final
  continuation outcomes are in `results.json`.

| Actual application | Construction and genuine save | Continuation from that save |
|---|---|---|
| Browser `web-r03` | `browser-r03`: all eight pointer placements, rotation, overlap refusal, removal/undo, Launch and save completed; harness failed locating Resume | `browser-resume-r03`: exact eight transforms, paid IDs and stock restored; actual boarding/helm and 90 completed sailing ticks pass; final speed 1.86092 m/s |
| Linux frozen executable | `native-r02`: all eight pointer placements, rotation, overlap refusal, removal/undo, Launch, disk save and process restart completed; harness selection sampling failed | `native-resume-r01`: exact eight transforms, paid IDs and stock restored; actual boarding/helm and 90 completed sailing ticks pass; final speed 2.00616 m/s |

Both final continuations retain 18 boat parts, mass 1,149 kg, paid IDs 35–42
and 23 materials. These are combined construction-plus-continuation records,
not relabeled successful uninterrupted initial runs. The full updated scripts
now include the corrected wait/waypoint behavior and the final sailing step.
No production application code changed between those saves and continuations.

Relevant commands, from a Nix development shell:

```bash
bazel test //tests:fixture_registry //tests:config //tests:build_model \
  //tests:compiled_assembly //tests:assembly_collision \
  //tests:assembly_buoyancy //tests:cove_save
bazel test //tests:cove_player //tests:salvage_asset_fixture
node scripts/test_salvage_preview.mjs
node scripts/test_cove_saves.mjs
```

Full browser journey, using a fresh profile and an isolated report directory:

```bash
VOXY_SMOKE_GPU=gaming-x11 VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_KEEP_PROFILE=1 VOXY_SMOKE_TIMEOUT_MS=600000 \
VOXY_SMOKE_COVE_BRICKS=/tmp/voxys-bricks-browser-new \
VOXY_SMOKE_REPORT=/tmp/voxys-bricks-browser-startup-new.json \
node scripts/smoke_integrated_wasm.mjs \
  build-cove-builder-3bh5dr4t/web-r03 salvage-cove
```

Native journey; output and storage directories must not already exist:

```bash
python3 scripts/validate_native_cove_bricks.py \
  --binary build-cove-builder-3bh5dr4t/native-r01-executable/voxy_native \
  --output /tmp/voxys-bricks-native-new \
  --storage-root /tmp/voxys-bricks-native-saves-new
```

The continuation options reopen the genuine saved world from the first
journey, compare all eight brick transforms and owned IDs, and finish the
boarding/sailing check. They never inject a design or modify saved bytes.
Native `--source-slot` copies both existing save generations unchanged to an
isolated test root. An older 13-part pre-brick native save also resumed with
its exact two paid identities and zero remaining stock.

The successful browser continuation used the existing profile
`/tmp/voxys-startup-L5Kxc5`, port `35651`, saved world
`963d52c06b195929566d224ffa3d168b`, and
`VOXY_SMOKE_COVE_BRICKS_RESUME=build-cove-builder-3bh5dr4t/browser-r03/summary.json`.
Its report directories are `browser-resume-r03/` and
`browser-resume-startup-r03.json` under the scratch directory. The successful
native continuation used `--resume-report` pointing to `native-r02/summary.json`
and `--source-slot` pointing to
`native-saves-r02/df314a0502c8ef92ad8d9a2419a7a5f7`, both under the scratch
directory. Output and copied save roots are `native-resume-r01/` and
`native-resume-saves-r01/`. Preserve these originals; use new report/root
directories if reproducing. Check profile locks and origin ownership before
reusing a browser profile.

## Retained failures and limits

Early runs exposed an action-range bug: palette actions entered the recovery
branch. The dispatch range is now bounded. A test initially projected against
the separate preview origin; pointer tests now use the real workshop display
origin. Native tests now wait for selection/removal to be observed before
issuing dependent controls. Browser resume waits for the button to be visible
and unobstructed. The established delivery driver's 25 cm waypoint tolerance
avoids overshooting a dock waypoint; boarding and helm still require their
actual in-game interactions. Failed summaries are retained, not relabeled.

Two renderer tests had old assumptions after the audited capacity increase:
99 copies of a multi-node guide fixture legitimately exceed the unchanged
256-instance limit, and 40 unique assets no longer exceed 48. Separate checks
retain the guide budget and use a one-node fixture for the full placement
budget; the unique-asset rejection now crosses the actual bound.

Final art, lighting/shadows, robot presentation, controller support, general
assembly performance, and the full game gates remain unfinished. The native
palette uses rendered meshes and a long window-title prompt; a polished native
HUD is still needed. The first-build instruction currently asks the player to
clear the crowded cargo cradle. Human usability approval is not claimed.
No G00–G14 gate or new main publication is implied by this scoped checkpoint.

Publication note: the verified component above is included in the subsequent [working main checkpoint](../../checkpoints/2026-09-10-brick-builder.md). Earlier local/publication statements describe the original verification time.
