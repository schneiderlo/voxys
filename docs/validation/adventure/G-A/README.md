# G-A — a place of your own

The first building-adventure milestone is implemented on the full installed
landscape. This report belongs to its gate commit; the normal pre-commit hook
must pass before that commit can be created.

## Player outcome

The main browser entry opens the adventure. The player can choose a site,
build individual modules or a paid starter room, walk through its doorway,
store items, craft a useful hammer, register a sheltered bed, and save/reload
the coherent world. The main terrain, house, robot and supplies render together.
The previous Cove and its independent saves remain available explicitly.

## Evidence

- [Native ordinary-control journey](native-home-r01/README.md): paid house,
  individual construction/removal/undo with exact refunds, doorway traversal,
  chest transfers, crafting, bed registration and actual process restart.
- [Final native rendering startup](native-render-r01/README.md): the same
  confirmed world loaded with the corrected composition path, produced running
  frames without renderer errors, and left stored bytes unchanged.
- [Browser ordinary-control journey](browser-r04/README.md): gathering,
  construction and refunds, furniture, confirmed save, nonempty chest restore,
  and resource depletion after returning. Its visual failure is preserved.
- [Corrected browser composition and entry](browser-r05/README.md): the same
  real saved world, visible house/robot, bare default entry and an ordinary
  canvas click without pointer capture or inventory mutation. This reuses
  successful gameplay checks rather than rebuilding the same house again.
- [World, movement and input](../WORLD-A01/cpu-r01/README.md): 39 focused CPU
  tests, including actual player movement through distinct 4×4 and 4×6 rooms,
  catalog stairs, roof/shelter queries, full-terrain walking and controller
  jump/place separation. Alternate layouts are CPU integration evidence;
  the manual native/browser journeys use the starter room plus free pieces.
- [Authority and capacity](../ITEM-A01/authority-r01/README.md): four supported
  homes, 1,024 pieces, 32 components, 1,068 collision solids and an exact
  31,448-byte canonical save round trip with conserved items.
- [Storage faults](../SAVE-A01/storage-r01/README.md): six native storage
  cases, including failure after one replica publishes. No confirmation is
  issued until the required pair is durable. Browser transport has separate
  namespace, validation, lock, retry and failure checks.
- [Kit and interface](../BUILD-A01/kit-r01/README.md): editable sources,
  cooked mesh digests, collision/catalog agreement, 16 focused UI cases and
  real DOM keyboard/focus checks. The UI accepts the bounded world observation
  and all backpack/chest transfer rows.

The final native and browser builds succeeded. The strict application build
also succeeded. The required gate hook runs
`bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`; it is not
bypassed or replaced with the focused checks above.

## Material fixes found during integration

Browser optimization renamed an unquoted host save callback; preserving its
public property name restored actual acknowledgements. Packaging now rejects
a bundle missing that bridge. Input handoff clears held movement; building
clicks retain a free cursor; controller Confirm cannot both jump and place.
Reachable furniture takes priority over a closer object behind a wall.
Repeated save requests coalesce while an earlier publication is busy.

The adventure registered a render callback without enabling the opaque scene,
and sampled the combined depth output instead of the terrain depth cache.
Enabling the composition path and using its correct depth source restored the
house, robot and supplies. The renderer now refuses an incompatible triangle
fallback instead of silently dropping construction after a resize failure.

## Limits

This is an early useful-home milestone. NPC towns, quests, enemies, authored
exploration encounters, region activation, sound and co-op remain open tasks.
The simple house kit, supply piles and landmark markers are not final art.
Placement previews are opaque color previews; runtime LOD selection and larger
world batching still need work.

The capacity result covers authority, geometry, UI and storage. It is not a
GPU frame-rate claim at 1,024 pieces. The browser used a fixed 512 MiB heap on
an AMD RDNA-3 hardware adapter; heap size is not a measurement of peak live
allocation. Observed FPS and automated play do not certify performance, fun,
Windows support or accessibility with real participants. Those gates stay open.

Saves are manual; unsaved changes remain visible. No player profile was used by
the verification drivers. No private saves were converted or overwritten.
