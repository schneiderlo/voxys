# Continuous brick construction

Status: implemented; focused CPU/UI checks, independent routing review and
combined native/browser gameplay checks pass. Publication checks are pending. This is a bounded
PLAY-02 usability improvement, not completion of PLAY-02 or a game gate.

## Player behavior

Choose a 1×2, 2×2 or 2×4 brick once, then point at a supporting part and click
repeatedly. Each valid click keeps that brick and starts the next preview with
the same orientation. R rotates. Select/Esc stops the tool and discards only
the unused preview; already placed bricks remain. Another Esc can close the
workshop. Explicit Keep/E retains the existing one-shot editing behavior.

Switching brick size replaces only the unused preview. Undo removes the last
kept edit. Launch, blueprint export and saved designs contain the kept build,
excluding the preview. Stock is consumed before purchasing a matching part;
ordinary paid identities, prices and save formats are unchanged. A full design
stops the tool after its final accepted brick instead of losing the placement.

## Implementation contract

`CoveWorkshop` owns the kept design and one transient preview. The application
routes native/browser inputs through that state. Action 96 means Select/stop;
95 remains cutter and 97/98 remain camera controls. Read-only observations are
`brickTool`, `canChooseBrick`, `placedParts`, and `placedBricks`; the historical
`parts` field includes the current preview. `changed` can therefore be true
while the tool is armed even though its kept build is exportable.

Pointer placement must resolve the current target before accepting a click.
An incompatible ray hit cannot reuse the previous valid ghost position. The
independent routing review found this case; the fix and regression are included
in the final focused run. Native workshop framing reserves the new HUD and
palette, and pointer input behind the HUD cannot place a part.

## Evidence so far

- Eight focused CPU cases pass with no skips, including stored stock, preview
  switching, rotation, Undo, clean refusals, target resolution and final-slot
  handling: `checks/voxys-brick-tool-cpu-r03*`.
- Twenty-three browser UI cases pass: `checks/voxys-brick-tool-ui-r03.log`.
- Native combined journey passes all 16 stages: eight placements, Select/Esc,
  removal/Undo, unused-preview Launch, save, actual process restart, exact layout
  and sailing. The HUD retains 20 px text without truncation at 960×540; real
  completed GPU serials advance and current scene shadows remain enabled.
  See `native-journey.json` and the retained process logs.
- Independent read-only review found no further kept-part, stock, rotation,
  Undo, Select, Launch or export defects after the pointer correction.
- Initial CPU attempt used an incorrect expected price; it was corrected to
  the actual catalog price without changing production prices. Its log remains.

## Required combined journey

Use actual controls and read-only observations, without screenshots or state
injection. Clear the Cargo cradle; place three 2×4, two 2×2 and three 1×2 bricks
using only three tool choices. Rotate and stack, refuse overlap, stop the tool,
remove/undo, arm an unused preview and Launch. Eight paid parts cost 27 material,
leaving 21 from the initial 48. Save/restart must preserve exact transforms,
identities, mass and inventory; board and sail afterward.

Native runner: `scripts/validate_native_cove_bricks.py --continuous
--expected-hud --expected-presentation-parts 3`, plus its required binary,
output and storage-root arguments. Browser runner uses
`VOXY_SMOKE_CONTINUOUS_BRICKS=1`, `VOXY_SMOKE_COVE_BRICKS=<new-output-directory>`
and `VOXY_SMOKE_NO_SCREENSHOT=1` with `scripts/smoke_integrated_wasm.mjs`.
Use fresh isolated save roots/profiles and preserve any supplied existing ones.

The first combined native attempt reached three placements, then failed its
expected stacking-height assertion after a palette change. The driver now waits
for the requested catalog and fresh application frames before moving on. The
second attempt outgrew the initial camera; the third focused the spare preview
instead of the tower. The final driver uses the existing Whole boat control,
and a bounded set of actual top-stud targets. No application behavior, costs or
acceptance assertions were weakened. All three failures remain in `checks/`.
The first browser journey exposed a real UI defect: status text shrank by
18.84375 px between Rotate pointer-down and pointer-up, moving the button away
from the click. The status now reserves three readable lines with scrolling for
longer explanations. The final browser journey passes all 15 stages, including
exact blueprint/ownership reload and sailing. Pointer-down/up/click all hit the
same Rotate rectangle at y=465.25, height=36.84375; orientation changes 0→21.
`browser-journey.json` contains this passive trace. No click retries or state
setters substitute for the control. `browser-startup.json` reports no browser
errors, and every stage checks uncaptured GPU errors.

Both final boats retain 18 parts, 1,173 kg, paid IDs 35–42 and 21 material.
`source-hashes.json` and `package-hashes.json` identify the final checkpoint.
The required full repository suite and publication are still pending.

The native journey ran frozen `build-cove-playable-r01/native-r01/voxy_native`.
The subsequent `native-r02` build changes only vertex-attribute initialization
to explicit member assignments for compatibility with Dawn's different struct
layout; native member values and runtime behavior are unchanged. The full
repository suite covers that final source. The browser package is
`build-cove-playable-r01/web-r02`, including main commit 299c8ef2
(LEGO World defaults), the fixed status region and the latest WASM data.

Controller parity, native named-design controls, wider workshop tools, final
art/human review and displayed performance remain open in the parent plan.

## Combined checkpoint verification — 2026-09-12

The final combined source passes the required repository suite: 2,067 native
cases pass, three skip and four remain disabled; terrain import has ten passes
and one skip. Both actual-control construction/save/reload journeys pass.
[Checkpoint, source/package identities and full raw check results](../../checkpoints/2026-09-12-playable-cove.md)
record the current result. Earlier pending statements and original identities
above describe their historical stage. This checkpoint does not approve final
art or complete a game gate. The commit containing the checkpoint report is
the publication unit; normal repository hooks remain enabled.
