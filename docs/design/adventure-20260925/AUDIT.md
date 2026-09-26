# Voxys code investigation

25 September 2026. Read-only investigation by a primary agent and three parallel reviewers. Source baseline: `45eada5be4ca89d6b2b55dcf192b6352623e5083`, including the pre-existing working tree. Existing staged/unstaged shader, physics, asset and evidence changes were not modified.

Read all of root `AGENTS.md` (zero bytes), `README.md` (860 lines), and `GAME_IMPLEMENTATION_TODO.md` (733 lines). Followed current entry points, session commands, construction, resources, combat, navigation, scenery, imported assemblies, saves, UI and relevant tests/validation. Read the archived adventure's design and architecture sections. This is a subsystem investigation, not a claim that every engine line was read or every test was executed.

The owner explicitly selected adventure during this investigation. [The proposal](PLAN.md) uses that new direction rather than silently reviving the cancelled roadmap.

## Evidence limits

No fresh browser playthrough was possible: computer-use inventory exposed no browser, and opening the in-app browser returned “Browser is not available: iab.” There was no running local game server in the inspected environment. No alternative UI automation was used.

Inspected retained [live building capture](../free-build/shared-hud-correction-20260924/live/build.png) and [catalogue capture](../free-build/shared-hud-correction-20260924/live/catalog.png), plus their [September 24 report](../free-build/shared-hud-correction-20260924/README.md). These are prior captures, not a new playtest or proof of the currently deployed build. The report distinguishes live WebAssembly captures from native HUD fixtures over a recorded background.

No compilation, gameplay tests, benchmark or deployment was performed for this planning task. Historical results below retain their original scope and date.

## Current purpose and architecture

Voxys is a C++20 engine with a shared native/WebAssembly application, WebGPU rendering/physics, an authored heightfield, mesh assets and multiple game experiments. The default browser product is currently creative building. Legacy adventure, Cove salvage, RIDGEBREAK and WRECKWATER have different purposes and save/authority assumptions; their existence does not make a unified game.

| Layer | Current responsibility and source |
| --- | --- |
| Browser entry | `web/index.html:547` defaults to `build`; `:1364` selects the creative config; `:1478` installs the shared UI adapter. |
| Shared app | `src/app/application.cpp:1620` creates the adventure runtime; `:2462` updates it; subsequent frame stages schedule physics and rendering. |
| Platform lifecycle | `src/engine/platform/wasm/entry.cpp:1335` drives the shared frame loop with suspension/backlog handling. `src/engine/platform/native/entry.cpp:61` uses the same application. |
| Gameplay orchestration | `src/game/adventure/adventure_runtime.*` connects input, menus, scene admission, character, accepted geometry and session actions. |
| Authority | `src/game/adventure/adventure_session.*` owns inventory, structures, components, progress and checked transactions. `PreparedChange` protects accepted state on refusal. |
| Geometry | `construction_policy.*`, `spatial_queries.*`, `building_doors.*` and `layered_navigation.*` share accepted terrain/building queries for support, collision, use and navigation. |
| Presentation | `src/render/adventure_hud.*` supplies the current GPU HUD. `ui/src/shared.js` supplies browser accessibility, preferences and intent transport. See [current UI README](../../../ui/README.md). |
| Renderer | Heightfield/LEGO terrain raycasting, authored meshes, terrain/shadow caches, day/night lighting, live water and GPU primitive rendering; WGSL shaders and `src/render/`. |
| Physics | Backend facade with GPU simulation, CPU reference/fallback and retained Jolt; `src/physics/`. Adventure movement/combat also use CPU spatial queries and fixed ticks. GPU physics is not a universal world-state authority. |
| Storage | Canonical adventure archive in `adventure_save.*`; platform adapters and shared browser mirrored IndexedDB generations. Separate creative/adventure identities and namespaces. |
| Assets | Blender/LDraw source → GLB/cooked VMESH and strict manifests/LOD/budget checks. `tools/adventure_assets/`, `tools/gltf_vmesh_tool.cpp`, `src/game/assets/`. |

Two documentation corrections matter:

- Root README mentions the older Preact hotbar and, historically, an adventure default. Current source and `ui/README.md` describe the shared GPU HUD, six quickslots, radial catalogue, colour wheel and terrain minimap. Preact remains for historical previews.
- The active TODO's F1–F4 and many acceptance gates remain open. Later polished screenshots and performance work do not establish that building or the adventure is complete.

## Why the browser game feels empty

| Code observation | Player-facing consequence |
| --- | --- |
| `adventure_runtime.cpp:497–505` removes creative resources/encounters/progress; `:662–684` excludes the legacy actors/sites | The current route intentionally removes motivations without replacing them with a deep creative toolset. |
| `building_catalog.hpp:15–22` defines 15 kinds, only three ordinary brick sizes | The player has a much smaller vocabulary than the scenery suggests. |
| `adventure_runtime.cpp:1543–1547` removes the remembered last placement, then clears it | Undo is not a general edit history; experimentation remains awkward. |
| `adventure_runtime.cpp:1995` updates selected paint; no general repaint/edit history command in the session | Existing work is harder to revise than new placement. |
| `adventure_runtime.cpp:1006` limits creative furniture interaction to doors | Beds/chests/benches do not make the current creative world more useful. |
| `creative_village.cpp:95–150` defines fixed buildings/paths/gardens | The village is scenery, not an inhabited settlement simulation. |
| `creative_scenery.hpp:24–27` states installed props are not owned bricks and suppression lasts until load | Forest streaming is not persistent harvesting or entity simulation. |
| `imported_assembly.hpp:14–16` separates imported arbitrary parts/rotations from player grid builds | The Blacksmith is not a collection of ordinary editable player pieces. |
| `brick_thrower.hpp:15–16,72–89` bounds live throws and expires them after 1,800 ticks | Thrown bricks are transient physics activity, not progression or persistent construction. |
| `adventure_runtime.cpp:3077` refuses saves with released imported wall parts | Cannon destruction is a bounded experiment, not general durable world damage. |

These are concrete integration/depth gaps. They do not mean the whole game is a fake render: terrain, geometry, player motion, accepted placements and GPU physics are real systems.

## Reusable adventure foundations

- **Checked changes:** `adventure_session.cpp:145–162,556–566` validates caller/revision/sequence and atomically publishes prepared state. Transfers/crafting/placement/rewards preserve ownership on refusal.
- **Functional homes:** `quests.cpp:19–32` checks a registered usable bed plus chest and bench in one owned structure. `trail_sites.cpp:143–160` checks actual walking access to furniture.
- **Geometry-sensitive combat:** `adventure_combat.cpp:37–49` uses raycast line of sight; `:68–106` updates navigation against current geometry; `:108–175` implements fixed-tick attacks, dodge and enemy phases.
- **Layered routes:** `layered_navigation.hpp:10–59` represents ground and upper levels, invalidates affected tiles and proves edges with the movement controller. This is a strong basis for player-built bridges, stairs and cover.
- **Persistent ownership:** health, depleted nodes, components, inventory, quest receipts and loot claims are serialized. Browser adapters use separate namespaces and durable acknowledgements (`web/adventure_saves.js:5–8,69–85`).
- **Forgiving recovery:** `adventure_session.cpp:472–480` recovers the player while retaining possessions and home state.

The [field-home access tests](../../../tests/test_adventure_field_home_access.cpp), [combat tests](../../../tests/test_adventure_combat.cpp), [session tests](../../../tests/test_adventure_session.cpp) and [combat/session tests](../../../tests/test_adventure_combat_session.cpp) cover meaningful geometry and ownership behavior. The [runtime test](../../../tests/test_adventure_runtime.cpp) explicitly describes its full-terrain checks as synthetic library input; it is not browser play evidence.

## What the old adventure actually contains

| Content/system | Present extent |
| --- | --- |
| Enemies | Two fixed encounter definitions, both melee raiders; `adventure_encounters.cpp:11–14` |
| Resources | 18 initial radial supply piles plus three trail sources; `adventure_runtime.cpp:489–496` |
| Gathering | One interaction depletes a node; hammer doubles yield; `adventure_session.cpp:330–341` |
| Items | Seven nonempty item kinds: wood, stone, scrap, hammer, compass, staff, relay core; `item_catalog.hpp:11` |
| Progress | First-home quest plus four fixed trail quests; two discoveries; `adventure_progress.hpp`, `adventure_trail_content.cpp:7–12` |
| State shape | Fixed encounter/discovery/quest arrays, NPC bit mask; `adventure_session.hpp`, `adventure_progress.hpp` |

No general population model, persistent harvestable forest, broad recipe/equipment economy or reusable region progression was found in the inspected gameplay path. These are new work, not configuration switches.

## Integration risks and budgets

**Scale and profile coupling.** `freeBuild_` selects rules, avatar, scale, lighting features, scenery, controls and activities. `adventure_runtime.cpp:532–535` gives creative the current builder at 2.8× scale, versus the old 1.7-unit adventure character. Current creative height is 4.76 and radius 1.12. Enemy bodies, navigation and melee reach retain old measurements. Legacy doorway clear space is 1.36 × 2.24 units (`building_catalog.cpp:18–25`). New adventure must version the kit and parameterize shared measurements; changing a flag is insufficient.

**State cost.** The session copies and validates a whole candidate during combat (`adventure_session.cpp:150–158,429–457`). Separate active simulation from cold cell state before multiplying populations. Likewise, cell coordinates used for scenery are not already a persistent entity lifecycle.

**Pause coupling.** Current `AdventureRuntime::isPaused` treats any menu as paused (`adventure_runtime.hpp:54`), and application pause handling stops physics/day-night (`application.cpp:2497–2511`). Live tactical building needs an explicit split between interaction panels and the true Pause menu; this behavior is not already available.

**Content identity.** Imported assemblies, player grid parts and scenery have distinct identity/rotation/ownership models. Integrate a supported subset deliberately. Do not assume a static imported mesh can be dismantled, harvested and saved through existing placement commands.

Current source ceilings, not demonstrated product targets:

| Budget | Current bound |
| --- | --- |
| Owned construction | Four structures, 1,024 total parts, 32 functional components (`adventure_session.hpp:21`) |
| Resource nodes | 256 (`adventure_session.hpp:22`) |
| Query geometry | 16,384 solids (`spatial_queries.hpp:30`) |
| Navigation | 64 tiles, 64 cells/tile, eight layers, 4,096 search expansions (`layered_navigation.hpp:20–23`) |
| Adventure archive | Schema 6; 1 MiB (`adventure_save.hpp:5–25`) |
| Creative GPU bodies | 1,024 (`free_build.cfg:23`) |
| Creative mesh allocation | 131,072 expanded instances and 524,288 per-frame draw records (`adventure_runtime.cpp:550–556`) |

The README's old 256-parts-per-structure statement is not an enforced limit in the inspected session/save code; do not carry it into a new product promise.

## Browser performance evidence

The retained [September 24 gameplay report](../../performance/gameplay-20260924/README.md) measured headless Windows Chrome on Intel Iris Xe at 1280 × 720. Reported moving-scene throughput was roughly 27–32 rendered frames/s across its baseline/candidate observations; CPU work improved without a reliable overall FPS improvement. These are historical headless measurements, not a fresh visible-play benchmark.

The [startup report](../../performance/startup-20260924/README.md) records expensive shader compilation and inconsistent full-page completion. The [GPU-hang investigation](../../performance/gpu-hang-20260924/README.md) records intermittent automated headless driver resets, explicitly not a reproduced user-session failure. Their current status needs a new controlled browser baseline, not a claim that the cause is known.

The README's approximately 945 FPS offscreen/cached renderer results are explicitly engine microbenchmarks. They cannot establish the proposed adventure's 60 FPS target.

## Decision

Keep the engine, transactional gameplay boundaries, shared geometry, save transport and current visual identity. Build the missing relationships and persistent world state in one small playable region. Expand through proven reusable systems, rather than extending hardcoded quest indices or filling the map with more uninteractive assemblies.
