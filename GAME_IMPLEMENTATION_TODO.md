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

### Authentic cannon and GPU destruction — 2026-09-18

Owner asks how firing an actual LEGO cannon at the imported house can produce
convincing physics, using the existing GPU engine, and explicitly requests the
cannon import. Keep the Free Build direction and all existing player features.
[Self-contained architecture, budgets and ordered D0–D5 checklist](docs/design/free-build/lego-destruction/IMPLEMENTATION_TODO.md).

- [x] Investigate reusable GPU bodies, contacts, events, graph splits and rendering;
  identify missing building CCD, part connectivity and impact-release handling.
- [x] Import real LDraw cannon `2527c01` with original base/barrel, source/license
  closure, editable Blender assembly, GLB, VMESH and preview. No generated substitute
  geometry; all 2,368 triangles retained. `data/adventure/ldraw-cannon-r01/README.md`.
- [x] Incorporate independent asset and architecture critique; document evidence.
- [x] D1 implementation: place/aim/fire the articulated cannon through the existing
  GPU world; compile house collision and sweep shots against fixed authored bodies.
- [ ] D1 full gate: dynamic authentic brick-stack proof, moving-target CCD,
  complete browser firing acceptance and normal repository checks/commit.
  See the linked checklist for exact limits; no full gate is complete.
- [ ] D2: authentic connected wall, explicit cut and GPU gravity collapse.
  The earlier 39-part upper-facade checks are historical. Runtime now uses the
  20-part ground-wall package: 18 eligible pieces and two unknown-support plates
  held fixed, preserving all 2,140 original house source identities. Source
  topology, certified settlement and rebuild are implemented; a player-sized
  opening and final browser acceptance remain open.
- [x] Correct GPU event packing so sparse manifold slots cannot hide real cannon
  hits. The regression fails before the fix and passes with dense solved contacts.
- [x] Add Visit cannon, one outstanding shot, safe Leave/settlement locks, and
  source-aware impact selection with atomic projectile retirement.
- [ ] D3: coupled impact-triggered release without duplicate impulse; visible and
  physical wall openings agree for player, bike and camera. The interim candidate
  uses explicitly approximate, dissipative, capped linear/angular impulse transfer.
  The actual-world regression now proves a real local knock-out: eight pieces
  release, the struck source moves 2.64 studs, and collision/rebuild checks pass.
  This does not establish a walk-through opening or broader collapse. Damage is
  session-only; Rebuild restores saveability. No whole-house or roof collapse.
  [Current evidence and remaining checks](docs/validation/free-build/lego-destruction/d3-r01/README.md).
- [ ] D4: full-house support failure, recognizable debris, measured performance.
- [ ] D5: damage persistence, explicit rebuild/reset and verified release.

### Shift running — 2026-09-17

- [x] Hold either Shift while moving on foot for 1.75× pace; release to walk.
  Available with the brick palette open too. Fine placement remains available;
  the motorbike retains its throttle controls. Running jumps retain horizontal
  pace and use the same collision/ground support solver.
- [x] Verify Shift routing, release, running jumps, collision and browser build.
  [Evidence](docs/validation/free-build/shift-run-r01/README.md).


### Actual LEGO assemblies — 2026-09-17 — current direction

The owner explicitly rejected procedural buildings with a LEGO-like surface.
Use existing LDraw models of actual LEGO sets and identifiable parts. Keep the
current figure, free building, motorbike and warm world lighting. The older
village art checklist below is superseded as an asset-authoring strategy.

- [x] Find a reusable actual set: 21325 Medieval Blacksmith, Vincent Messenet
  [Cheenzo], from https://library.ldraw.org/omr/sets/1289.
- [x] Pin original MPD and all 742 external dependencies, licenses, authors and
  known source substitutions. Offline validation resolves all 859 definitions.
  Source and credits: `data/adventure/ldraw-blacksmith-r01/source/README.md`.
- [x] Import the actual assembly into Blender at one game unit per stud; retain
  the editable source assembly and create a separate runtime derivative.
- [x] Integrate one full Blacksmith landmark at correct scale with ground contact,
  bounded rendering, collision and saved-build precedence.
- [x] Independently critique the imported geometry and verify browser placement.
  Evidence: `docs/validation/free-build/ldraw-blacksmith-r01/README.md`.
  Close gameplay inspection, performance profiling and owner acceptance remain open.
- [x] Collision correction requested by owner: replace nine broad collision boxes
  with surfaces baked from the actual model; cover base, stairs, walls and roofs.
  Preserve clear courtyard/interior air. Verify player walking on the actual
  imported steps (1.2 stud rise), not only generic synthetic ground.
- [ ] Replace the remaining imitation village buildings/props with verified
  actual-part assemblies. One imported set does not complete the whole village.
- [ ] Profile the CAD geometry on target browsers and introduce reviewed LODs
  before increasing the number of full sets. Do not claim a frame-rate target
  without measurements. Obtain owner visual acceptance before marking complete.

### Village visual correction — 2026-09-17 — NOT accepted

Owner rejects the gap between the first village and the supplied reference.
The prior functional checks remain valid; visual completion was not established.
The following are core missing art/composition work, not minor polish.
[Self-contained reference gap review and acceptance criteria](docs/design/free-build/village/reference-gap-review.md).

- [ ] Rebuild one cottage with convincing roof, beam, window and doorway depth;
  establish that quality in-game before creating more variants.
- [ ] Create at least four distinct building silhouettes, then compose varied
  clusters, a readable square and visible tower/windmill landmarks.
- [ ] Add substantial crop plots, gardens, hedges, market awnings and purposeful
  yard props; improve tree canopy shapes and planted groups.
- [ ] Improve bounded paths and diagnose weak building shadow/contact depth
  while preserving the warm low-angle light and readable shade.
- [ ] Verify the revised result in comparable overview/approach/close views,
  movement/building regressions and measured displayed-frame performance.
- [ ] Obtain independent reference-match review and owner visual acceptance.

### Small creative village — 2026-09-16

Owner requests a warm LEGO-style village using the supplied reference, while
retaining the current minifigure. This is free-build scenery, not adventure.
[Reference and scope](docs/design/free-build/village/README.md).

- [x] Author original editable cottage, tower, windmill, garden, path and well meshes.
- [x] Place eight cottages, landmarks and four gardens on the inland shelf with
  terrain foundations, passable doors, entry steps and connected lanes.
- [x] Give owned construction/doors priority; remove a whole scenery building
  together and reserve its complete roof/sail envelope from trees.
- [x] Independently critique geometry and the actual game view; correct doorway
  trim and filled tower bands; add first-pass facade details and roof studs.
  These changes do not establish reference-quality architecture (see correction above).
- [x] Verify actual full-size character routes through every cottage, a motorbike
  route past the well, and construction conflicts against the real terrain.
- [ ] Owner visual acceptance and village-plus-large-build frame-time measurements.

[Implementation, asset rebuild details and evidence](docs/validation/free-build/village-r01/README.md).

### World scenery correction — 2026-09-16

Owner rejected the previous local-only coverage and glossy/buggy prop appearance.

- [x] Independently identify and fix bent exported cap normals, disconnected pine
  tiers, coplanar crate faces and a stud overhanging its small rock top.
- [x] Match the terrain more closely with matte materials, preserving warm light.
- [x] Extend placement to stable terrain-aware world cells; stream around the
  player, with lighter distant models and bounded rendering/collision work.
- [x] Preserve construction/door/player/bike clearance and suppressed props during
  travel; publish matching render/collision revisions and invalidate cached aims.
- [x] Verify revisits and four actual terrain regions; native world/door/runtime
  tests, strict geometry/shading checks and browser build pass. Independent
  reviewer confirms all 4,780 exported horizontal triangles have flat normals.
- [ ] Owner acceptance, large-build frame-time measurements and distance polish.

[World scenery evidence](docs/validation/free-build/world-scenery-r02/README.md).
This supersedes the starting-area-only placement scope below.


### Creative scenery follow-up — 2026-09-16

Owner request: add LEGO-style props around the free-building area and have an
independent agent critique the result. Preserve open building/riding space.

- [x] Author six original editable prop meshes: broadleaf, pine, flowers, rocks,
  bench and crate; package strict cooked meshes and source provenance.
- [x] Admit 36 grounded props on the actual bay shelves, preserving the open
  center and main landscape. Decorative bench/crate do not introduce quests.
- [x] Share admitted draws/collisions; make scenery yield to player construction,
  door swings and saved player space, without changing save identity/schema.
- [x] Verify deterministic admission, all six kinds on real terrain, bike departure,
  collision/picking, no respawn after edits and door precedence. Native world,
  door, construction-policy/runtime tests and browser build pass.
- [x] Obtain an independent code and actual-game-image critique. Fix its door
  validation finding and strengthen flower stems based on visual feedback.
- [ ] Owner acceptance and measured performance with large player builds.

Evidence and exact review limits:
[creative scenery](docs/validation/free-build/creative-scenery-r01/README.md).
This is starting-area scenery; wider world population and F1/F2 gates remain open.


### Shadow readability follow-up — 2026-09-16

Owner feedback: shaded bricks were too dark to distinguish colours and detail.
The owner explicitly rejected the first daylight correction: preserve the warm
morning/evening atmosphere. Do not raise the sun to solve shadow readability.

- [x] Identify the weak indirect light in the original Low Golden Sun preset:
  ambient colour `[0.18, 0.23, 0.42]` and strength `0.34`.
- [x] Restore the exact original sun azimuth `-38`, elevation `8`, sun colour
  `[1.0, 0.43, 0.16]`, strength `1.7`, fog colour `[0.48, 0.25, 0.20]` and
  exposure `1.15`. Free building overrides only the preset's ambient colour
  and strength with `free_build.cfg`: `[0.62, 0.64, 0.72]` and `0.65`.
  This lifts shaded detail while retaining long shadows and warm direct light.
  Other LEGO modes, geometry, shadow visibility and gameplay are unchanged.
- [x] Build and visually verify the corrected warm-sun candidate in the browser.
  The starting view retains the original orange horizon and sunlit hills;
  shaded terrain seams and the character's clothing are now visible. Only
  indirect illumination differs from the public release's lighting preset.
- [x] Correct the flat terrain shading identified in owner review. The regular
  LEGO terrain renderer used a constant ambient term, so caps and sidewalls
  received identical fill after the sun was blocked. `ray_blit.wgsl` now uses
  world-space sky/ground illumination and a broad sky lobe, plus subtle analytic
  occlusion at stud/plate junctions. Contact detail fades with pixel footprint.
  Both cached and direct terrain paths call the same helper. Smooth terrain and
  the separate Cove PBR path retain their existing lighting. No new texture
  reads, render passes, collision changes or additional direct light are added.
  Browser visual check confirms rounded studs and contact depth in the shaded
  foreground while the low-sun atmosphere is preserved.
- [x] Pass focused shader and GPU composition tests, including cap/sidewall HDR
  contrast with zero direct sunlight and unchanged fill under a moving sun caster.
  `//tests:shader_blit` passes; all 22 `//tests:blit_path` cases pass across the
  suite run and the corrected focused fixture rerun. The fixture samples a wall
  below its rounded bevel and requires its indirect HDR output to remain between
  25% and 75% of the cap output in every colour channel.
- [ ] Complete the normal commit/release checks and publish this lighting follow-up.

Local candidate: `http://127.0.0.1:42765/?experience=build`, served from
`/tmp/voxys-shade-preview` using `build-lego-wasm/bin`. Build command:
`nix-shell --run 'cmake --build build-lego-wasm --target voxy_wasm -j4'`.
This is a local visual correction; owner acceptance and the wider visual gate
remain open. It is not included in the verified public performance release below.

### Character proportions follow-up — 2026-09-16

Owner feedback: the figure feels tiny compared with the terrain studs. One world
unit is one stud spacing. The old human asset/controller was 1.7 units tall.

- [x] Apply a creative-only scale of `2.8`: figure/body height `4.76` studs,
  initial capsule radius `0.84` (superseded by `1.12` for the broader builder
  model below). Keep legacy adventure and Cove proportions unchanged.
  Scale the sampled character and anchors around its feet; visible meshes and
  their shadow instances use the same transforms.
- [x] Match collision, placement exclusion and door sweep checks to the larger
  body. Set the camera target to `3.36` units above the feet and initial orbit
  distance to `10.5`. Scale stepping and immersion with the body, horizontal
  movement/jump velocity by `sqrt(2.8)`, and animation cadence to the longer stride.
- [x] Preserve saved bricks and content identity. When an old saved player pose
  no longer fits, search bounded nearby supported positions for the larger body,
  relocate only the player and mark the world dirty. Refuse visibly if no safe
  location exists. A valid newly saved pose retains exact archive round trips.
- [x] Pass `//tests:adventure_world`, `//tests:adventure_construction_policy` and
  `//tests:adventure_runtime`, with the actual installed 8192 terrain enabled.
  Added checks cover body size, wall/ceiling clearance, jump/landing/speed,
  camera target, old-size pose relocation, unchanged saved bricks and exact
  subsequent save/load bytes. Browser/WASM target builds successfully.
- [x] Visually verify the resized figure relative to studs in the local game.
  The browser candidate shows the full larger figure with comfortable headroom,
  readable studs beside its feet, and retained warm horizon/shaded stud contours.
- [ ] Resize/version the old architectural kit for these proportions. Existing
  prefab door openings are only `2.56` units high and cannot fit the `4.76`-unit
  figure. Do not silently change saved piece dimensions or content fingerprints;
  introduce compatible new pieces or an explicit migration before closing the
  usable-interiors/building-feel gate. Freely built openings must also provide
  the new body's height and diameter clearance.
- [ ] Complete commit/release checks and publish the proportion correction.

Local proportion candidate: `http://127.0.0.1:42765/?experience=build&preview=figure-scale`.
The warm-sun shadow correction above is included in this candidate.

### Character ground contact follow-up — 2026-09-16

Owner feedback: the resized character does not feel connected to the ground.
In terrain shade, the direct-sun shadow cannot supply this cue, and the prior
indirect-light model had no character occlusion.

- [x] Add two bounded soft contact-occlusion sources, derived from the animated
  boot mesh bounds after scaling. Feed them in the same camera-sector frame as
  mesh draws through `SunShadowUniforms` (128 bytes, including two sole vectors).
  Sources default to disabled for other scenes and hidden/swimming avatars.
- [x] Apply sole occlusion only to indirect light on terrain and authored mesh
  receivers. Preserve sunlight, collision, depth and the warm preset. Fade with
  foot/receiver separation and reject receivers above the sole; combine feet by
  maximum occlusion to avoid doubled darkness. This is a local approximation,
  not general scene AO or foot IK. No extra textures or render passes are added.
  The live scene-terrain pass applies it over the untouched static background
  cache, so footsteps do not become baked into the terrain.
- [x] Verify GPU contact/no-contact, cached/fresh equivalence, moving/lifted feet,
  roof-height rejection, mesh integration and owner-generation memory accounting.
  `//tests:shader_blit`, `//tests:blit_path`, `//tests:mesh_path_test` and the
  real-terrain `//tests:adventure_runtime` pass. The focused
  `SceneSunShadowsGPU.*` and
  `FixtureGPU.FilteredEnvironmentAndSunShadowsAreChargedRetriedAndRetiredWithTheirGeneration`
  checks pass. Generated shader synchronization and `git diff --check` pass.
- [x] Verify the ground contact in the actual browser candidate.
  Successful WASM build and actual-game visual check show localized soft shading
  beneath the boots while retaining shaded stud contours and the warm horizon.
  Preview: `http://127.0.0.1:42765/?experience=build&preview=foot-contact`.
- [ ] Complete the normal commit/release checks and publish with the pending
  shade/proportion corrections. The wider building-feel gate remains open.

### Scenic starting location follow-up — 2026-09-16

Owner request: the opening should feel beautiful and vast. Use the actual main
landscape, with warm sunlight, a layered distant view and usable building ground.

- [x] Survey the installed raw heightfield for coastal shelves, local relief and
  low-sun exposure. Select the meadow edge at world X/Z `(1200, -1120)`, roughly
  200 studs above water. Derive Y from the real terrain and scaled body radius.
  Start facing yaw `0.75` radians, across the bay. Keep the existing lens, orbit
  distance, landscape, lighting and construction rules.
- [x] Apply this only to the initial player pose of newly created creative
  worlds. Keep content fingerprints and the existing recovery contract stable;
  loading a saved world must retain its saved player and structures exactly.
  Legacy adventure/Cove starts remain unchanged. The older recovery point remains
  part of the saved-world contract; this is an opening location, not a migration
  of players' homes or a new travel mechanic.
- [x] Verify actual-terrain startup, placement and save/load at the new location,
  including old saved player positions and a 32×32-stud low-relief building area
  behind the overlook (centre `(1180,-1070)`, at most `0.65` units relief).
  `//tests:adventure_world` and the real-terrain runtime suite pass; the final
  `FreeBuildRuntimeIntegration.*` rerun passes with the new location, old-save
  preservation and meadow-relief assertions. WASM build and diff checks pass.
- [x] Verify the final browser opening composition: lit LEGO foreground, visible
  bay, nearby headlands on both sides and a distant mountain ridge. The first
  shelf-centre candidate hid the water; the selected edge location corrects that.
  Local preview uses `?experience=build&preview=bay-overlook`.
- [ ] Publish after the normal commit/release gate.

### Recognizable minifigure follow-up — 2026-09-16

Owner feedback: the character still did not look like a real LEGO-style figure.
Its narrow torso, head and legs produced a tall doll silhouette, even after the
world-scale correction. Correct the actual model, not only the camera or size.

- [x] Author a separate original creative builder in Blender: broad trapezoid
  torso, stout block legs, thick open C-shaped grips, yellow cylindrical head
  with exposed top stud, orange-red jacket and blue trousers. Remove the old
  backpack/hair cap from this variant and use smoother plastic materials.
- [x] Install `data/adventure/builder-r01` with its own strict manifest identity,
  three cooked detail levels, editable Blender/GLB sources and provenance. Keep
  existing adventure, resident and robot packages intact. The authoring recipe's
  `--builder` variant shares the rigid hierarchy and all eight clips.
- [x] Preserve the 4.76-stud standing height. Widen the creative capsule to
  `0.4 × 2.8 = 1.12` studs radius; use it consistently for movement, placement,
  door clearance, startup and safe restored-position recovery. Keep the old
  recovery/content identity stable. Match the soft boot-contact radius to `0.7`.
- [x] Correct the wider-foot walk lift and remove idle torso tilt for the flat
  head stud. Check actual exported vertices across all three detail levels:
  grounded walk/land/fall, open grips, normal lengths and a bounded core envelope.
- [x] Pass `//tests:robot_asset`, `//tests:adventure_world`,
  `//tests:adventure_construction_policy` and both actual-terrain runtime cases.
  The focused creative rerun chooses a clear rotated-brick placement for the
  wider figure; rotating a nearby long brick into its body correctly refuses.
  Exact saved-build restoration and wider-body safe relocation pass.
- [x] Build the browser package and visually verify the actual character in the
  game, retaining the scenic opening and contact shading. Local route:
  `?experience=build&preview=classic-builder`.
- [ ] Owner visual acceptance and the normal commit/release gate remain open.

[Model render, reproduction and validation](docs/validation/free-build/builder-r01/README.md).
The existing undersized architectural-kit follow-up remains required.

### Owner-supplied character reference — 2026-09-16

The owner supplied a concrete visual reference after the exposed-stud prototype.
That reference supersedes the bald head and generic jacket styling above.
[Reference, actual model and reproduction](docs/validation/free-build/builder-reference-r02/README.md).

- [x] Retain the supplied image in the validation record for future agents.
  Match its recognizable features in the real 3D asset: swept brown molded hair,
  rounded yellow head, eyebrows/eye glints/smile, red jacket with dark printed
  seams and pockets over a grey tee, and solid blue rounded-hip legs and feet.
- [x] Replace faceted character shading with manufactured plastic normals and
  correct inverse-transpose handling when baking the body proportions. Keep the
  existing animation hierarchy, overall height, collision, saves and warm scene.
- [x] Correct the first model render's hair/head intersections and coplanar leg
  overlap. Use a complete hair shell under swept locks and one continuous molded
  leg extrusion. Check the final actual Blender model, not an image mockup.
- [x] Cook all three detail levels and validate their actual vertices, normals,
  hand openings and sampled animation envelopes within the existing budgets.
- [x] Pass model/runtime integration and verify the updated browser character.
  `//tests:robot_asset` and both installed-terrain `//tests:adventure_runtime`
  cases pass. WASM build, source/package hashes and diff checks pass. Actual
  browser review confirms the new hair, red/blue outfit and smooth toy surfaces.
  Preview: `?experience=build&preview=reference-builder`.
- [ ] Owner acceptance and normal commit/release checks before publication.

### Arm silhouette correction — 2026-09-16

The owner rejected the reference-builder arms in the actual rear game view.
Straight sleeves overlapped the flared jacket and hid the wrists.

- [x] Replace the creative player's sleeves with rounded shoulder caps, outward
  upper arms and forward-bent cuffs; retain one rigid molded piece per arm.
- [x] Move wrists, hands and grip/tool/helm anchors together. Preserve collision,
  overall scale, grounded feet, warm lighting and the existing shoulder clips.
- [x] Regenerate all detail levels and check real exported animation envelopes.
  Walking width is now 1.248 authoring units; allow 1.27 for cosmetic arms while
  retaining the .4-unit core capsule. Add a regression check for exposed wrists.
- [x] Verify character/runtime tests and corrected browser model. Both
  `//tests:robot_asset` and installed-terrain `//tests:adventure_runtime` pass;
  WASM build and source hashes pass. Actual rear game view shows both sleeves
  and hands outside the jacket. Preview: `?experience=build&preview=builder-arms-r03`.
- [ ] Owner acceptance and normal commit/release checks before publication.

[Corrected mesh and validation](docs/validation/free-build/builder-arms-r03/README.md).

### Creative motorbike — owner request 2026-09-16

- [x] Author an original red toy motorcycle from the supplied reference: red
  tank/body/mudguard, black saddle/grips/tires, silver engine/forks/exhaust and
  round headlight. Store Blender/GLB source, cooked mesh and provenance.
- [x] M summons and mounts on clear dry ground; M dismounts at low speed into
  supported clear space. Provide the same action in the small browser UI.
- [x] Implement fixed-step acceleration, braking, reverse, bike-relative steering,
  assisted balance/lean, terrain following, gravity on drops and swept building
  collision. Refuse steep/wet/blocked mounts and unsafe dismounts.
- [x] Seat the existing figure astride the saddle, angle legs clear of the tank,
  aim arms toward the steering grips, animate wheels/front assembly, frame the
  vehicle and stop motion on menu/focus loss. Block building while mounted.
- [x] Pass focused movement/collision/runtime tests and UI tests; build WASM.
  Verify actual browser M mounting. Check final combined model after the newer
  figurine target below. Full acceptance and publication remain separate.
- [ ] Owner riding/visual acceptance and normal commit/release checks.

[Behavior, controls, source and evidence](docs/validation/free-build/motorbike-r01/README.md).
The vehicle is summoned transport, not a saved world entity: reloading starts
on foot and M summons it again. Parked-bike persistence is outside this request.

### Motorcycle ground handling — owner feedback 2026-09-16

- [x] Replace highest-probe snapping and delayed pitch with actual-radius tire
  contact, projected wheelbase, supported frame pitch and gravity on drops.
- [x] Keep grip over stud ripples, filter contact impulses, clear descending
  plate edges, and let the underside slide over a ledge without freezing.
- [x] Apply slope gravity to riding/coasting; make brakes hold on slopes and
  require wheel contact for traction, steering and dismounting.
- [x] Follow visible frame height with the camera; suppress airborne tire shadows.
- [x] Pass seven motorcycle regressions (19 world tests total), full-terrain
  runtime tests, and the browser build. Check actual axle clearance, ledge
  release/landing, thin walls, slopes and frame-rate independence.
- [ ] Owner riding-feel acceptance and normal commit/release checks.

[Implementation, validation and limits](docs/validation/free-build/motorbike-ground-r02/README.md).

### Motorcycle turning over elevation — owner feedback 2026-09-16

- [x] Reproduce jerking, steering loss and sudden stops while turning downhill.
  Eight fixed routes cover four approach headings and both turn directions.
- [x] Preserve yaw momentum during brief front-tire unloading; smoothly regain
  steering on contact. Clear angular motion on pause, collision and a grounded stop.
- [x] Sweep the conservative collision guard up/across/down over intermediate
  stud crowns, without lifting the visible tire/frame pose. Retain wall and
  overhead sweeps on every segment.
- [x] Pass all 21 world tests, including turning continuity, tire clearance and
  no stationary rotation after braking; pass full-terrain runtime tests and
  rebuild the browser preview.
- [ ] Owner riding-feel acceptance and normal commit/release checks.

[Reproduction and validation](docs/validation/free-build/motorbike-turn-r03/README.md).

### Definitive figurine target — owner update 2026-09-16

Rear-view follow-up:

- [x] Preserve the [owner's rear reference](docs/design/free-build/figurine-rear-target/README.md).
- [x] Model two recessed sockets in each blue leg, rounded socket rims and a
  rear heel edge; keep internal animation geometry out of the cavities.
- [x] Add lower, overlapping rear hair layers and preserve the plain red back,
  molded arm recipe, standing scale and animation hierarchy.
- [x] Check actual front/rear renders, all three exported detail levels, source
  hashes, normals, grounded animations, model/runtime tests and browser build.
- [ ] Owner acceptance and full rear-reference likeness; hair waves and finish
  remain approximate. [Actual model and validation](docs/validation/free-build/figurine-rear-r05/README.md).
- [x] Obtain the owner-requested independent rear comparison. Verdict: not a
  reference match. [Full critique](docs/validation/free-build/figurine-rear-r05/independent-review.md).
- [x] Address the initial review: thinner swept hair tiers, tapered socket walls,
  dark recessed floors, mirrored upper socket ledges, a rounded pelvis connector,
  closer sleeves/shorter exposed wrists and visible leg/heel taper.
- [x] Have independent agents check actual socket normals and review the revised
  front/rear model. Final verdict: accept this iteration as an improved stylized
  approximation, not an exact reference match. Pass asset and full-terrain
  runtime tests. [Reviewed model and remaining polish](docs/validation/free-build/figurine-rear-reviewed-r06/README.md).
- [ ] Finish lower-socket depth cues, finer curved/flared hair tips, owner
  acceptance and normal release checks. Do not claim exact reference fidelity.

[Latest reference and complete brief](docs/design/free-build/figurine-target/README.md).
This supersedes earlier character references. Preserve the approved scale,
ground contact, low warm sun and motorbike fit.

- [x] Store the owner's full figurine reference and explicit art requirements.
- [x] Revise the actual mesh: broad wavy brown locks, rounded head edges, thick
  circular C hands, cleaner jacket prints and continuous blue leg/toe surfaces.
- [x] Verify all detail levels, grounded animation, character/runtime tests,
  motorbike compatibility and final browser package. Final robot/runtime tests
  pass; motorbike world tests and all five UI tests pass. Actual browser M mount
  and dismount pass with the revised figure.
  [Actual mesh and validation](docs/validation/free-build/figurine-target-r01/README.md).
- [ ] Owner visual acceptance and normal commit/release checks.

### Independent figurine comparison and correction — 2026-09-16

The owner requested a separate agent comparison because the figurine still did
not match the close-up reference. [Review, corrected model and reproduction](docs/validation/free-build/figurine-review-r03/README.md).

- [x] Obtain an independent comparison of the actual model against the owner
  image. Record specific shape differences rather than claiming visual success
  from mesh tests or repeated game screenshots.
- [x] Correct face height, torso/limb widths, hand size, shoulder placement,
  facial prints, torso taper, neckline/pocket prints and the curved blue hip
  connection. Retain the approved height, collider, contacts and warm lighting.
- [x] Replace the hair closure crossing the forehead; author a closed shell,
  flattened locks and sideburns. Update bike hip/shoulder pivots from actual
  asset transforms so the revised figure retains its riding behavior.
- [x] Cook/check all three detail levels and eight clips. Pass character, actual
  terrain runtime and motorbike tests; build the browser package. Actual browser
  startup, M mounting and dismounting pass with the revised asset.
- [ ] Match the remaining hair silhouette: broader irregular overlapping waves
  and layered side volume, without a tidy central part or leaf-shaped locks.
- [x] Rebuild molded elbow transitions after the owner rejected the angular
  arms: sweep a continuous sleeve around a rounded bend with perpendicular
  cross-sections, a seated shoulder cap and clean cuff edges. Retain wrist/grip
  positions, rigid animation and existing model budgets. Export checks,
  character/runtime tests and WASM build pass.
  [Actual mesh and validation](docs/validation/free-build/molded-arms-r04/README.md).
- [ ] Refine rounded three-dimensional grip openings and plastic finish.
  Compare under appropriate camera/lighting conditions.
- [ ] Full reference fidelity, owner visual acceptance and normal release gate.
  Independent review explicitly says these are **not achieved** by this pass.

### Verified public release


The bounded performance pass is complete. Code release `268bfe0e` is
verified on main and Pages. Three derived-result caches preserve all replay and
save bytes while improving the largest measured editing CPU workload by 10.50×.
See [measurements, proofs and release evidence](docs/performance/free-building-20260916/README.md).
The wider creative feel and design gates below remain open.

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
- [x] Implement only measured changes, one performance lever per diff, each with
  an output-equivalence proof sketch and exact replay comparison.
- [x] Add reproducible performance regression guards and retain before/after data.
- [x] Pass the normal commit hook, push main and verify the final live release.

The release-candidate baseline and profiles were collected during its remote
build, before any performance implementation. Initial release `7ea4d762` is
verified live. Investigation window: **2026-09-16 14:56:33–16:26:33 UTC**. See the [baseline, profiles and ranked options](docs/performance/free-building-20260916/README.md).

First performance commit: `5381b18a` (unchanged preview reuse), full normal hook
passed. A separately measured structure-serialization change preserves all 24
replay outputs and raises the 768-piece edit workload to 8.21× baseline throughput.
Its full normal hook passed as `1e4e75d3`. A final measured ray-result cache
preserves all golden output/save bytes and raises this workload to **10.50×**
the original throughput (10.41× a fresh original-binary control). Final browser
compilation passed. Final code commit `268bfe0e` passed the normal full
hook and Pages run `35121091665`; exact public source and live creative controls are
verified (2026-09-16 16:29:10 UTC). Implementation froze at 15:56:30 UTC; all requested
performance-pass items are complete.

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
