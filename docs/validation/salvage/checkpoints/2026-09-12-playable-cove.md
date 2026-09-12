# Playable Cove: continuous construction and presentation

Status: combined gameplay journeys and mandatory repository checks pass.
Publication uses the commit containing this report; the normal hook remains enabled.
This is a working checkpoint, not a completed G00–G14 gate or approved final art.
The owner authorized publication to main and a four-hour work session ending
2026-09-12 07:27:33 UTC (03:27:33 EDT Toronto). No screenshots were taken.

## Included work

- Continuous 1×2, 2×2 and 2×4 brick tools: choose once, click repeatedly, retain
  rotation, switch sizes, Select/Esc, Undo, and exclude unused previews from
  Launch, blueprint export and owned costs.
- Native text HUD for the selected tool, material stock, Launch cost, placement,
  nearby actions and pause/save state. Readable licensed font; camera and palette
  have reserved space. Fixed atlas/vertex storage is 164 KiB, excluding driver
  and pipeline overhead.
- Original molded pontoon, beam and plate art with nine cooked LODs and editable
  Blender sources. Presentation identities preserve canonical parts and saves.
- Current part shadows on terrain and ocean, explicit sector-frame conversion,
  finite terrain-shadow boundaries and shared generated shader logic.
- Browser controls retain their position while placement status changes. The
  full journey found an actual Rotate click missing its button after a message
  shrank the panel; passive event/rectangle observations preserve that failure.
- Supplied browser test profiles are retained by the runner.

## Main synchronization

The work began after published `dcae5c3cb91642af38aae90915a705d37d365f95`.
Before publication, remote main advanced to
`299c8ef27d71640677b17d98356a544034b03b30` (LEGO World renderer defaults and a
previous-preset option). The working branch fast-forwarded cleanly to that
revision. Its `lego_world.cfg` and `web/loader.js` changes are preserved. Final
packaging includes them; Cove uses its separate route/settings.

## Evidence

- [Structural art and older-save compatibility](../LOOK-01/toy-art-r01/README.md).
- [Continuous builder and actual construction/save/sailing](../PLAY-02/continuous-r01/README.md).
- [Native HUD, font provenance and lifetime](../UX-01/native-hud-r01/README.md).
- [Terrain/ocean shadows and retained review corrections](../REND-03/scene-shadows-r01/README.md).

Native integration passes 16 actual-control stages at 960×540, with eight paid
bricks, exactly three type choices, 27 material charged and 21 remaining. The
unused preview is discarded at Launch. A real process restart retains all eight
transforms, identities, mass and inventory; the player boards and sails. HUD
text stays 20 px without truncation while actual GPU completion advances.

Focused checks pass: eight builder CPU cases, 23 browser UI cases, four HUD
layout/GPU cases, and seven final shadow-frame/border regressions. Earlier
structural art/registry and renderer checks remain linked with their exact
scope. Independent source review found no remaining builder/HUD issue after
the pointer-target correction. Initial failures and interrupted checks are
retained; they are not counted as passes.

Browser integration passes 15 stages at 1280×800, with the same exact part IDs,
mass and inventory, blueprint export, unused-preview exclusion and save/reload.
The corrected Rotate button stays fixed across pointer-down/up/click and changes
orientation 0→21. No browser or uncaptured GPU errors. Exact final source and
package identities are in `../PLAY-02/continuous-r01/{source,package}-hashes.json`.
The required suite passes: **2,067 native cases pass, three skip, four remain
disabled**; terrain import has ten passes and one skip (cached). The native suite
ran once on the final source in 1,071.3 seconds. Exact skipped/disabled names,
XML and raw logs are in [repository checks](2026-09-12-playable-cove-checks/summary.json).
The normal commit hook reuses these valid cached results. Publication is the
commit containing this report, pushed normally onto main; its remote SHA is
verified after the push.

## Scope retained

Full LOOK-01, PLAY-02, UX-01, REND-03 and all game gates remain open. Controller
parity, native named-design UI, robot/animation, final machinery/harbor art,
dry hulls/flooding, complete second-job integration, co-op, campaign and declared
displayed performance still have their original requirements in the plan.
Unfinished two-cargo files/patches and older saves/previews remain untouched.

Future agents must honor the owner's time limit in `GAME_IMPLEMENTATION_TODO.md`.
At the deadline stop implementation and test workers, preserve unfinished files
and report actual progress. Do not mark the complete-game objective achieved.
