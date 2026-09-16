# Voxys — free LEGO-style building

Revised 2026-09-16. This is the active, self-contained implementation plan.

## Owner direction

The owner explicitly said: **“I want us to forget about the adventure”**, then
**“free lego building, but it should feels good.”** This supersedes the adventure
objective and its milestone ordering. The main activity is making things with
bricks. Quests, combat, gathering, crafting requirements and NPC progression are
out of the active scope. Do not finish them as prerequisites for this game.

Keep the warm toy aesthetic and LEGO-style minifigure character. Use original
assets without licensed branding. Keep the existing mostly fixed main landscape;
terrain excavation is not requested. A flat building surface can be a useful
option, but must not silently replace the main landscape. Solo building comes
first. Multiplayer and machine simulation are deferred until building feels good.

The old plans and evidence remain in
[the adventure archive](ADVENTURE_IMPLEMENTATION_TODO_ARCHIVE.md) and
[the salvage archive](SALVAGE_IMPLEMENTATION_TODO_ARCHIVE.md). Preserve existing
worlds, original save identities and explicit legacy routes. Retaining old code
is not an instruction to continue its roadmap.

## What “feels good” means

- Start building immediately. Every basic piece and color is available without
  gathering, currency, unlocks or crafting. Unlimited stock is a working default.
- The ghost stays where the player expects. It snaps to a readable stud/plate
  grid, chooses the intended surface, and stays steady across triangle edges.
  Rotate keeps the intended attachment. Preview and accepted geometry agree.
- Place one piece with one action; continue with the same piece and orientation.
  No menu reopening after each placement. Held input and double-click behavior
  are deliberate and cannot accidentally delete or duplicate a selection.
- Correct mistakes easily: remove, undo and redo; pick an existing piece/color;
  move/copy selections later. Refusals explain the actual blocked placement.
- The camera gives space to aim. Support comfortable orbit/zoom and inspecting
  from inside a creation. Input over the palette never places into the world.
- Pieces look like molded toys: readable studs, small edge highlights, warm
  colors, coherent contact shadows and a clear translucent placement ghost.
- Successful placement has an immediate, restrained visual response and a short
  original click sound. Invalid placement sounds different and stays readable
  without color. Volume/motion preferences apply; no invented audio claims.
- The interface is a small pictured hotbar, cycled with the mouse wheel.
  The owner explicitly chose this over a circular selector. Colour and extra
  tools open only on demand; keep the main view clear. Large text and keyboard/controller access remain supported. Remove
  quest trackers, health, material counters and adventure-specific tutorials
  from the creative entry.
- Walking through a finished creation is satisfying too. Keep the minifigure,
  believable scale, usable doors and physically traversable interiors.

These are implementation criteria, not proof that the owner finds it enjoyable.
Owner feedback after a short hands-on build is required for the feel gate.

## Technical foundations and boundaries

Reuse the C++ WebGPU renderer, fixed landscape, integer .02 m lattice, existing
piece meshes, shared geometry queries, player/camera and durable save transport.
Relevant source: `src/game/adventure/{adventure_runtime,adventure_session,
construction_policy,building_catalog,building_doors,adventure_save}.*`,
`src/render/adventure_hud.*`, `ui/src/{main.jsx,bridge.js,style.css}`, and
`web/adventure_saves.js`. The new creative browser UI uses Preact/Lucide;
`web/adventure_ui.*` remains for legacy routes.
Names may remain internal during extraction; do not rewrite the engine first.

Create an explicit free-build profile and route (`free_build.cfg`,
`?experience=build`). Installed profile/content identity owns creative rules;
client UI flags and save-supplied metadata must not grant them to old worlds.
Use separate native/browser save namespaces and remembered-world metadata.
Never migrate an adventure into creative by merely changing its resource rules.
A future explicit layout import must preserve the source world.

Creative placement/removal does not consume/refund inventory. Keep identity,
capacity, collision, reach, support and occupied-space validation authoritative.
Permit basic bricks to start directly on supported terrain: requiring a house
foundation before every brick is an adventure constraint. Runtime, preview,
picking, collision and saved transforms must share the same accepted geometry.

Initially retain honest supported budgets (1,024 parts, four owned structures,
32 functional objects) while investigating better connected-build ownership.
Show limits before they cause surprise. Unlimited pieces means no material
cost, not unbounded memory. Do not claim these budgets meet a finished creative
game; expand from measured edit/camera/frame performance.

Undo/redo must be command history, with stable IDs and validated geometry.
Functional furniture contents and door states cannot disappear during edits.
Save snapshots include complete accepted construction; only confirmed durable
writes may say saved. Add meaningful native/WASM and browser checks for changed
behavior. Tests do not substitute for the owner’s building experience.

## Ordered TODO

### F1 — a clean creative entry

- [x] Record the owner’s new direction and archive the superseded adventure plan.
- [x] Add explicit creative profile, safe save separation and unlimited placement/removal.
- [x] Start in Build with a useful basic brick selected; permit supported terrain starts.
- [x] Remove town/quests/enemies/resource chores and their HUD from this entry.
- [x] Present a small pictured hotbar, on-demand colour/tools, placement feedback, pause/save and comfort controls in the browser. Native retains its existing HUD.
- [ ] Verify ordinary place/rotate/remove/save/reopen in native/browser; old saves stay intact.
- [ ] **Gate F1:** publish a usable free-building preview and commit its verified implementation through the normal hook.

### F2 — enjoyable moment-to-moment building

- [ ] Stabilize snapping and surface selection; add clear connection/height cues.
- [ ] Provide responsive continuous placement with explicit repeat rules and safe input ownership.
- [ ] Add true multi-step undo/redo and pick-existing-piece; preserve functional state.
- [x] Add an original colour palette for new pieces; render accepted colours and preserve them in saved builds.
- [ ] Add authoritative repainting of existing selected pieces.
- [ ] Tune orbit/zoom/inspect controls against actual small and tall creations.
- [ ] Finish molded materials, contact lighting and a legible translucent ghost.
- [ ] Add original placement/remove/refusal sounds and immediate low-motion visual feedback.
- [ ] **Gate F2:** the owner builds freely for a short session; fix observed frustration and record real feedback. Commit the passing work. Do not infer enjoyment from tests or screenshots.

### F3 — creations worth keeping

- [ ] Expand the coherent brick/plate/slope/tile/window/door kit from actual building needs.
- [ ] Finish functional doors already in progress; verify shared collision, safe opening and reload.
- [ ] Add selection, move/copy, group blueprints and a saved creations browser.
- [ ] Offer useful terrain-adapting supports and an optional clear building surface.
- [ ] Improve autosave/recovery with visible confirmation and explicit manual saves.
- [ ] Measure larger builds and expand limits without losing accepted creations.
- [ ] **Gate F3:** build, revise, save and revisit several distinct creations through ordinary controls; commit.

### F4 — polish and sharing

- [ ] Finish accessibility, native Linux/Windows packaging and measured browser limits.
- [ ] Add export/import/sharing only with explicit ownership and safe file validation.
- [ ] Decide with the owner whether small co-op or machines are worth adding after the building loop is satisfying.
- [ ] Complete distribution, provenance and release checks for the agreed scope.

## Current state and next action

The owner requested publication to main. Commit `5b52d209` publishes the accumulated
creative implementation; `f6a70ea4` adds the building thumbnail shader omitted from
that commit. The clean build then exposed a Chrome software-GPU shader compilation crash.
Repair `7ea4d762` preserves collision calculations while separating authored
and ordinary collision compilation and using caller-owned terrain-contact output.
Its normal commit hook passed: 2,457 tests passed, eight opt-in tests skipped,
and the terrain importer passed. Actual browser startup, completed GPU work,
scene image and brick-thumbnail checks pass locally. Pages publication passed;
the exact source and creative controls are verified on the public game. See [deployment evidence](docs/validation/free-build/deployment-20260916/README.md).
This release checkpoint does not constitute a completed creative feel gate.
The previous local creative candidate remains `build-free-build/web-r04`, build
`free-build-bbc98fd5c35efbd0`, served at
`http://127.0.0.1:42764/index.html?experience=build&telemetry=0`.

The new browser interface uses a compact pictured hotbar, mouse-wheel selection,
a small floating colour palette, and on-demand tools. The confirmed owner
specification is in `docs/design/free-build/minimal-ui.md`; future agents must
not return to the rejected large tray / brand-heavy concept. Source lives in
`ui/`; regenerate the bundled web files with `npm --prefix ui run build`.
Packaging checks source/output hashes. Native still uses the previous HUD.

Current changed-behaviour checks pass: four actual-bundle UI component cases,
full-landscape native runtime placement/paint/wheel/pause/exact archive restore,
and native/WASM compilation. The browser package verifies all 52 files. Through
ordinary browser controls on r03, the agent opened the colour palette, chose
Coral, placed a brick, saved, opened pause/comfort, reloaded and saw the persisted
coral brick. A full wheel step selected Floor. Narrow 483×998 layout is inspected.
Final r04 changes only the initial Save badge: an untouched unsaved world must
not claim Saved. Its added component assertion passes. No screenshot matrix.
Evidence: `docs/validation/free-build/F1/minimal-ui-r01`.

Earlier foundational checks remain in `entry-r01`: 163 native component cases,
46 legacy browser UI and 17 browser storage cases. Do not repeat unchanged
suites without a reason. Ordinary native interaction and the complete browser
rotate/remove journey remain open; F1 has not passed, so there is no gate commit.
F2 painting existing selections, multi-step undo/redo, placement feel and owner
hands-on approval remain unfinished.
Door source/assets: `data/adventure/door-r01`, piece15, cost6 wood/2 scrap only in
legacy adventure; creative mode ignores costs. Shared door C++ tests now pass.
The old runtime door check passes after placing its test player within handle
reach for both door poses. Doors use discrete quarter-turn poses, not animated
swings. Detailed evidence belongs under `docs/validation/free-build/F1/entry-r01`.

## Requested performance pass — 2026-09-16

After the corrected release is verified live, the owner requested a 90-minute
performance investigation of that release, followed by a push to main and a
verified deployment. Record the UTC start/deadline; stop adding optimizations at
the deadline and finish required checks/publication. Preserve game behaviour and
all save formats. This is a bounded performance task, not a return to adventure.

- [x] Verify the initial public release and record the timed investigation start.
- [x] Establish a reproducible baseline: full suite, representative creative
  workloads, p50/p95/p99 latency, throughput and peak memory with exact commands.
- [x] Capture CPU, allocation and I/O profiles before proposing optimizations;
  report the top 3–5 measured time hotspots.
- [x] Define explicit golden observations, accepted archives and invariants.
- [x] Rank opportunities by `(Impact × Confidence) / Effort` before implementation.
- [ ] Implement only measured changes, one performance lever per diff, each with
  an output-equivalence proof sketch and exact replay comparison.
- [x] Add reproducible performance regression guards and retain before/after data.
- [ ] Pass the normal commit hook, push main and verify the final live release.

The release-candidate baseline and profiles were collected during its remote
build, before any performance implementation. Initial release `7ea4d762` is
verified live. Investigation window: **2026-09-16 14:56:33–16:26:33 UTC**. See the [baseline, profiles and ranked options](docs/performance/free-building-20260916/README.md).

First performance commit: `5381b18a` (unchanged preview reuse), full normal hook
passed. A separately measured structure-serialization change preserves all 24
replay outputs and raises the 768-piece edit workload to 8.21× baseline throughput.
Its full commit/deployment gates remain pending.

Measurement tooling is in `tools/benchmarks` and `scripts/performance`.
Its native CPU component timings must not be presented as browser FPS. See the
benchmark README for workload parameters, isolation and comparison thresholds.

## Working rules

Read root AGENTS.md and README.md. Use short, clear communication for a dyslexic
owner. Prioritize what the player can build and how it feels. No screenshot
matrices or repeated unchanged tests. One purposeful changed-scene check beats
repeated static views. Preserve unrelated changes and all old saves. Mark only
proven items done. Commit successful gates with the normal required hook
(`bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`). Do not
force-push, fabricate owner approval, or revive cancelled adventure milestones.
