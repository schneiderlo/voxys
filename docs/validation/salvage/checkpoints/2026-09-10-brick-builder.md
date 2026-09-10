# Main checkpoint: playable brick building

2026-09-10. Parent `e51467858a979837c133634e62b7ef92ceb29ce5`.
This is an owner-requested working checkpoint, not a passed G00–G14 gate.

## Included

- Individual studded 1×2, 2×2 and 2×4 bricks in the actual Cove workshop: pointer picking, snap ghost, rotation, placement, removal, undo, costs, Launch and saves.
- A tested 64-brick design budget with explicit whole-operation refusal when capacity is exceeded; physical root and water limits remain separate.
- Workshop orbit, pan, zoom, selected-part framing and whole-boat framing, with focus-safe input and space left for controls.
- Additive two-cargo archive preparation. The live application retains the generator mission; two-cargo restore explicitly refuses until integration is complete.
- Both build systems, installed brick catalogue, regression checks, player instructions and compact verification records.

The builder and camera production files match their verified checkpoints exactly. The rebuilt browser JS/WASM/data also match the prior two-cargo-codec verification byte for byte. See [builder evidence](../LEGO-02/builder-r01/README.md), [camera evidence](../PLAY-02/camera-r01/README.md), and [archive preparation](../PLAY-06/two-cargo-r01/README.md).

## Excluded unfinished work

An incomplete live two-cargo refactor was preserved in local `build-main-brick-checkpoint-arq1suz8/before/` and `unfinished-two-job-runtime.patch`. Its new source files and opt-in registry/config remain local. They are not packaged or published as a playable second job. No save, old preview, scratch asset or diagnostic artifact was deleted.

## Verification

Browser UI: 22 cases pass. Cove save host: 17 cases pass. Storage envelope: four cases pass. The final required native suite passes: 2,054 passing cases, 3 skips and 4 pre-existing disabled cases. The terrain-import target passes too. Both native and browser application builds pass and match the earlier verified outputs byte for byte. See [structured results and test XML](2026-09-10-brick-results/summary.json). The normal pre-commit hook is retained; successful commit creation must pass that hook as well.

The first full run completed in 1,424.1 seconds with 2,053 passing cases, three skips, four pre-existing disabled cases, and one failing case. It exposed an outdated assertion in `AssemblyFunctions.MaximumSocketAndConnectionBudgetsRemainGloballyBounded`: the old fixed-axis overlap sweep rejected a valid vertical assembly solely by wasting its candidate-pair budget. The builder’s adaptive sweep now admits it. The test now verifies all 256 modules, 8,192 sockets and 1,024 welds survive with the correct occupied socket count, and still rejects a 1,023-connection capacity profile. The existing `BuildModel.IndependentPartConnectionProxySocketAndPairWorkCaps` retains a genuinely over-budget spatial arrangement and its exact `candidatePairs` refusal. No production limit or assertion was disabled. The corrected ten-case assembly-functions suite passes through CMake; the final full suite passes after this correction.

## Visual status and next priority

The owner again reported that the game does not look good enough. That feedback is unresolved. No visual approval, art improvement, frame-rate claim or new screenshot is claimed by this checkpoint.

The implementation plan now prioritizes a cohesive playable Cove presentation pass before expanding the second job: readable brick terrain and studs, a convincing brick-built boat/harbor, coherent light/materials, and a compact readable HUD. Preserve working building and saves. The original prototype routes remain intact.

Use `--config salvage_cove.cfg` or browser `?experience=salvage-cove`. At the dock press **B**, remove and Keep the cargo cradle to free the deck, then choose **1/2/3**, point and click to place bricks. **G** frames the part; **M** frames the boat. **Enter** launches.
