# Painted Cove: free brick repaint and molded machinery

Status: final paint/static-machinery candidate, following published
`40e842690baf5c9637414f584189cd032aec702d`. Focused checks, final native/WASM
packages and both real-control paint/save journeys pass as recorded below.
**The final mandatory repository suite passes.** Publication uses the commit
containing this report, with the normal hook enabled. This report does not
complete a whole-game or final-art gate.

## Included work

- Eight labeled brick paints, including Original, on individual 1×2, 2×2 and
  2×4 bricks. Repeated placement and size changes retain the selected brush.
  Select/Escape discards an unused preview without charging or saving it.
- Free repaint of owned bricks preserves identity, condition, provenance,
  placement, connections, settings, mass and inventory. Keep/Undo and exact
  native/browser save/reload behavior are covered. Native uses Y; browser uses
  labeled swatches. Machinery remains outside the paint tool.
- Linear per-instance RGB replacement before selection tint, retaining authored
  textures, opacity and other material factors. Fixed requested storage is
  119,336 bytes inside the unchanged 128 KiB reservation.
- Original molded helm/winch r02, engine r03 and corrected propeller r04, with
  editable Blender sources, three cooked LODs per part and provenance. The new
  catalog selects seven presentation replacements including the three existing
  structural parts. Canonical gameplay and owned save identities stay unchanged.
- The corrected propeller has at least 13.994 mm conservative blade/guard radial
  clearance. The rejected r03 propeller is retained only as historical evidence;
  it is not the installed selector. These machines are **static** in this
  checkpoint; later mechanism-animation scratch work is excluded.

## Executed evidence and exact inputs

- [Paint controls, focused checks, native/browser journeys and reproduction](../UX-01/brick-paint-r01/README.md).
- [Final machinery, provenance, rejected candidate and clearance correction](../LOOK-01/machinery-r01/README.md).
- [Frozen source/data SHA-256 inventory](../UX-01/brick-paint-r01/source-hashes.json).

The existing paint UI run passes 25 cases. The initial focused target passes
eight cases; the separate material target passes four; after the final propeller
correction, only the two affected registry/GPU-admission cases rerun and pass.
All fourteen focused test executions have zero skips. The linked reports distinguish
the first candidate from the final corrected assets; earlier results are not
credited as new final-art checks.

The final real GPU admits seven replacements at **10,740,488 owner bytes and
78 color draws**, within unchanged 16/48 MiB owner/resident limits. Native and
WASM application builds pass; exact package identities are in the paint evidence.

Native passes ten actual-control stages: place Teal, preserve the brush through
another preview and size change, discard that preview, launch, repaint the owned
brick Blue for zero debit/refund, launch, save and restart. Browser passes seven
stages plus its outer startup check and exact blueprint export after reload.
Both retain brick ID 35, placement `[100,72,-2775]`, rotation 0, exact Blue
`[50,108,190,255]`, **11 parts, 993 kg and 43 material**. Browser error arrays are
empty. These runs avoid repeating the unchanged eight-brick sailing journey.

The source inventory covers the final applied paint paths, build/package inputs,
config/catalog, four installed art directories and their readmes, three author
recipes, tests, README and plan. Evidence documents are excluded from that hash
inventory to avoid self-reference. They remain part of the explicit staging
list prepared in scratch. No build directory, unrelated two-cargo source or
two-job fixture/config belongs to this checkpoint.

## Mandatory result and publication

The required suite passes: **2,074 native cases pass, three skip and four remain
disabled**. Terrain import passes ten cases with one skip (cached). The native
suite ran once on the final source in 1,293.5 seconds. Exact skipped/disabled
names, XML and raw logs are in [repository checks](2026-09-12-painted-cove-checks/summary.json).
The normal commit hook remains enabled and may reuse these valid cached results.
Remote main was fetched before publication and matches predecessor `40e84269`;
no newer remote source required integration. Normal push and remote-SHA verification
are the final publication steps; the execution ledger records the resulting commit.

## Scope retained

LOOK-01, UX-01, material/art approval, mechanism animation, robot/third-person
camera, complete two-job integration and the full implementation goal remain
open according to the plan. Blender exports/saves completed before recorded
PulseAudio shutdown interruptions; strict cooks and runtime checks are reported
separately. No images or screenshots were generated for these checks or this
documentation. The authorized work window ends **2026-09-12 07:27:33 UTC**.
