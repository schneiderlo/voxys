# PLAY-02 / UX-01 workshop core handoff

Implemented live in five owned paths only:

- `src/game/expedition/cove_workshop.hpp`
- `src/game/expedition/cove_workshop.cpp`
- `src/game/expedition/cove_boat.hpp`
- `src/game/expedition/cove_boat.cpp`
- `tests/test_cove_workshop_editing.cpp`

Root owns Application, renderer highlights, platform/browser controls, build entries, plan, full checks and publication. No source was staged or committed by this agent. Parked two-job files were not touched.

## Integration contract

- `selected()` is the primary slot; `selectedParts()` is a nonempty sorted unique bounded span; `isSelected(slot)` is the highlight predicate. `selectPart(slot, SelectionMode::{Replace,Toggle,Add})`, `selectAll()` and `selectOnlyPrimary()` require a clean ordinary draft. Selection never silently discards an ordinary edit. Explicit tool selection cancels only the continuous tool's unused ghost.
- Existing movement commands edit the whole selection. `moveSelection(GridPosition)` uses checked integer ticks. `rotateSelection(Axis::{X,Y,Z}, quarterTurns=1)` uses permitted proper cube rotations about the primary translation. Group `aimAt` resolves the primary socket and translates every selected member; picks that exclude selection exclude all selected slots. Snap applies one proper frame transform to the whole group.
- `duplicateSelection(deltaTicks)` copies to distinct new dynamic slots. Root's proposed default is +X 32 ticks. `mirrorSelection(Axis::{X,Z}, planeTicks=0)` copies across the displayed canonical plane, only for the three admitted symmetric bricks. The proper rotation is world reflection × original rotation × local-X reflection. It never negatively scales or reflects asymmetric machinery. Copies still need an ordinary compiler-valid connected fit before Keep.
- `replaceSelection(catalogIndex)` removes differing selected definitions from membership and uses distinct free dynamic slots for replacements at the old poses. Same-definition members remain unchanged. Existing owned slot definitions are immutable. New settings use authored defaults; brick replacements preserve paint. Slot count, integer bounds and permitted rotations are checked before any candidate is installed.
- Remove, paint and configure operate on the entire selection. A setting unsupported by any selected member refuses atomically. Toggle/limit/reverse use the primary value as the common requested value. `paintIndex()` is empty for mixed colors. Paint is still free design intent; purchase/refund/source-ID rules remain entirely in existing refit authority.
- `Action::Redo` is appended after Remove, preserving old enum ordinals. `undoCount()` and `redoCount()` describe one combined bounded history of at most 32 accepted edits. New Keep/load clears the future. History restores exact registry, selection and primary, including the trusted separated owned state from an earlier cut. Dirty ordinary previews must be kept or cancelled before history actions.
- `placementIssue()` exposes numeric `Problem` and optional actual placement. `message()` is authoritative player text. `CoveBoatAssembly::Diagnostic` narrowly forwards existing structured compiler failures; overlap, connector clearance, disconnection, unsupported rotation/settings and physical limits are distinguished without parsing strings.
- Continuous placement is unchanged: pointer/controller Place must call `placeBrickTool(nextStored)` when `brickToolActive()`. `command(Action::Keep)` is deliberately one-shot and stops the tool. Export/Launch use the kept design; an unused ghost is never charged.

Root reserved action mapping: 300 all, 301 only primary, 302 duplicate +X, 303 mirror X=0, 304 mirror Z=0, 305 replace with current catalog, 306 draft redo, 307 rotate X, 308 rotate Z, 309 toggle next selection. Core APIs do not perform GameSession transactions, mint IDs, alter saves or mutate the live sailing body.

## Verification

- `checks/focused-r04.log` and `checks/editing-r04.{log,xml}`: all 12 new CPU cases passed (10.7 seconds). These use admitted production brick geometry/sockets and the actual refit authority. Cases cover atomic group transforms, aim, exact weld restoration after moving back, checked overflow, visible invalid overlap refusal, group paint/remove, history/selection/branching and 32-entry bound, all 24 proper mirror orientations plus an actual compatible mirrored stud fit, distinct-slot replacement, stored/paid/loan rules, mixed configuration, 64-brick editing, and separated-state history.
- Final source delta after r04 only reports `Problem::NotConnected` when creating/restoring trusted separated history, with matching regression assertion. `checks/legacy-and-history-r05.log`, `checks/history-r05.xml` and `checks/legacy-r05.{log,xml}`: affected history case plus all 13 selected existing brick/workshop CPU regressions passed (0.5 and 5.0 seconds).
- Initial r02 compile failed only because the new test used `SocketId.value` instead of `value()`. Initial r03 runtime passed 11/12; the separated-history test compared input import bytes against the loader's remapped encoding. It now captures the accepted post-load bytes and requires exact Undo/Redo recovery of those bytes. Neither failure required weakening a product check.
- Six design-library model and eight native host cases also passed in the shared r02 invocation. See `native-library-review.md` for the independent scoped review and limits.
- No GPU/browser/native app journey, screenshot, broad suite, gate claim or commit was performed by this agent. Root's integrated checks remain necessary.

Focused commands use the installed Nix environment and Bazel targets `//tests:cove_workshop_editing`, `//tests:design_library`, `//tests:native_design_library`. The r05 legacy filter selects only `CoveMovement.Brick*`, `CoveMovement.Workshop*`, `SavedDesignReloadReusesOwnedPartsAndPricesAdditionsWithoutGrantingLoans`, and `CutProtectsIntactDesignAndReopensBrokenWorkshopWithoutRepairingOwnership`, plus the new separated-history case. Exact command and output are preserved in the named logs.
