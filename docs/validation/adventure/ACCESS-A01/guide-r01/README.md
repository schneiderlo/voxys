# ACCESS-A01 — optional quick guide, r01

This records the implementation and bounded component evidence for the optional
in-game guide. It does **not** close an accessibility or ordinary gameplay gate.
The final native, runtime and WASM checks include the menu-dismissal fix.
The packaged browser guide also passed the bounded interaction below.

## Implemented behavior

- Six shared cards cover movement, building, home, quests, combat and saving.
  Each installed card is at most 180 characters. The content comes from
  `adventureGuideCard` in `src/game/adventure/adventure_presentation.cpp`.
- Movement wording follows keyboard/controller use and hold/toggle mouse look.
  Combat hints use the current Attack/Dodge bindings. The guide explains building
  costs and refusal, usable home furniture, explicit quest acceptance and manual
  save confirmation. Settings remain separate from world saves.
- “How to play” opens the topic picker from Pause. Building Help opens the building
  card. Next, Previous and All topics use published runtime menu intents.
  Closing returns to the entry mode: Pause, Explore or Build.
- The guide pauses gameplay while open. Reading it grants nothing and records no
  tutorial completion. Build selection, recipe, rotation and owned materials are
  preserved; no new save fields or world schema are introduced.
- Browser help is one focused sheet with literal runtime text, captured row
  intents and focus return to the world. Unrelated play/build controls are hidden.
- Native help wraps the complete card body and keeps the selected navigation row
  visible. Keyboard/controller navigation and guide-only mouse-wheel selection
  expose rows that do not fit together at a small viewport. Unrelated controls
  are excluded from the guide layout.
- Restore clears guide return state and queued choices. Tokens distinguish topics
  and entry contexts, preventing stale or duplicate Next/Previous activations.

## Archived checks

| Evidence | Recorded result | What it demonstrates |
| --- | --- | --- |
| `checks/core-r01.log`, `core-r01.xml` | 75/75, 0.995 s | Shared authority regressions and six presentation cases, including three guide cases: current controls, essential instructions, bounded cards and safe invalid topic. |
| `checks/browser-ui-r01.log` | 43/43, 84.711483 ms | DOM component regressions, including guide ownership, literal text, stale/double activation, guarded entry points and focus return. Uses a controlled runtime fixture. |
| `checks/native-layout-r02.xml`, `layout-runtime-r02.log` | 10/10 layout cases, 0.296 s | CPU layout/glyph and hit geometry. Actual guide cards at 100/125/150% text, 480×480, 640×480 and 1280×800; topic picker also at 480×360. Preserves complete text, selection, intent and enabled state. |
| `checks/runtime-r01.xml`, `layout-runtime-r02.log` | 1/1, 5.837 s | Actual full-terrain runtime with synthetic library input and a headless RADV Vulkan device. All six cards, dynamic device wording, paused state/archive equality, stale/double choices, entry-mode/build selection preservation and restore cleanup. |
| `checks/native-r01.log`, `wasm-r01.log` | Both builds completed | Native target compilation and the complete WASM target before the dismissal correction. These are not final package identities. |
| `checks/native-r02.log` | Build completed, 6.627 s | Native targets compiled after the dismissal correction. |
| `checks/native-layout-r03.xml`, `layout-runtime-r03.log` | 10/10 layout cases, 0.306 s | Final CPU layout regression, including the actual eight Build controls. |
| `checks/runtime-r02.xml`, `layout-runtime-r03.log` | 1/1, 5.988 s | Final full-terrain headless runtime check, including Escape + outside-sheet left click + RightShoulder on one frame. The retained selection and entire state/archive stay unchanged; the following neutral frame still adds no construction or material changes. |

The first native layout attempt is retained in `checks/native-layout-r01.xml`
and `layout-runtime-r01.log`: 9/10 passed; the only failure was a test assumption
that at least one actual card exceeded 160 characters (`sawLongCard`). These
short cards did not. That assumption was removed; complete glyph-by-glyph body
verification, paragraph preservation and navigation checks remain. The corrected
run passed all ten cases; no guide was lengthened to satisfy the test.

A later source review found that Escape/F2/controller Menu could dismiss a guide
into Build before that frame's raw click/shoulder inputs were consumed. The runtime
now suppresses world/build input for the dismissal frame. The focused synthetic
regression passes in `runtime-r02.xml`; the earlier runtime result is retained
separately and is not used as evidence of this correction.

## Final candidate and browser interaction

Native build `checks/native-r02.log` and final WASM build
`checks/wasm-r02.log` pass. The source hashes, final check counts and package
association are recorded in [results.json](results.json).

- Browser: `build-adventure-g-c/web-r07`, build `adventure-0a43174f2b40d3c1`.
- [Local preview](http://127.0.0.1:42761/index.html?experience=adventure&telemetry=0),
  server 34181, CUA tab 12. All 47 manifest entries match their file hashes.
- Native companion: `build-adventure-g-c/native-r06/voxy_native`, SHA-256
  `18923a8f2324813abf8014deceaa8fce9a4e5d63699f11ed6b6c5a4048a659af`.
  This executable is a workspace companion, not a standalone distribution.
- World schema 5 and content identity remain unchanged. Prior previews and their
  saved worlds remain intact. The pre-fix guide package web-r06 is historical.

Official CUA actions on the final package opened Build, selected Starter room,
read Building help and then the Home tip, and returned to Build. The room stayed
selected with the same 70 wood /16 stone /8 scrap cost and 640/320/80 stock;
focus returned to the game canvas. Place remained disabled because no ground was
aimed; no paid placement or browser rotation proof is claimed. A second path
opened Pause → How to play → Keep your adventure → Back to menu. All six topic
titles were exposed, the saving instructions were correct, and Pause returned
with How to play focused. The final tab is retained at Pause.
See [the recorded journey](checks/browser-journey-r01.json).

These checks do not establish ordinary walking through the game, physical
controller acceptance, human readability or user accessibility acceptance. The
headless runtime initializes real GPU resources but is not a rendered guide UI
journey; native layout checks inspect CPU geometry. Browser component tests use a
DOM fixture; the bounded interaction above uses the actual packaged game.
No screenshots, injected developer state, world-save operation, frame-rate claim
or completed human gate are claimed. Main remains the recorded G-A commit.
