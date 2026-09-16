> Historical plan. Superseded on 2026-09-15 by the owner’s free LEGO building direction. Do not resume adventure milestones automatically.

# Voxys — open-world building adventure: implementation TODO

Revised 2026-09-12. Working title: **Voxys**. Code baseline: `acd9d43d224734a633c3af5db5d6d519e6bc3e23`.

**This is the active, self-contained plan.** Read it, root `AGENTS.md` and `README.md` before implementation. Implementing agents will not have the conversation. The owner wants a Minecraft-inspired building game with World of Warcraft-inspired quests, NPC towns, enemies and exploration.

**Status:** G-A is implemented, verified and published on `main` at `2d6d0bc154169fc8d73d744ad9392b9c481d0bfd`. Its normal hook passed (2,289 game cases passed, three skipped, four disabled; terrain import target passed). G-B source is in progress. The owner called the UI unusable and then explicitly rejected the robot theme: **“I want a warm game, not a robot game.”** The owner then clarified: **“we need lego characters.”** UI-B01 plus the revised LEGO-style people/village direction are the immediate priorities before G-B publication. Read the [latest warm adventure brief](docs/design/warm-adventure-r02/README.md); the earlier [three UI prototypes](docs/design/ui-prototypes-r01/README.md) retain layout lessons but their robotic visual theme is superseded. A separate C encounter candidate now adds a LEGO-style trail raider, staff combat and persistent health/loot; ordinary gameplay acceptance remains open. See [G-A evidence](docs/validation/adventure/G-A/README.md).

The previous roadmap and all its completed-work records are preserved in [SALVAGE_IMPLEMENTATION_TODO_ARCHIVE.md](SALVAGE_IMPLEMENTATION_TODO_ARCHIVE.md). Its boats-first scope, second-cargo priority and G00–G14 ordering are historical. Do not resume them automatically. Reuse their implementation/evidence where relevant; do not present them as completion of this game.

## 1. Confirmed direction

The owner explicitly chose:

- **Free building on mostly fixed terrain.** Houses and useful structures are central. Digging away terrain, a voxel rewrite and a fully destructible landscape are not required.
- **Quests, NPC towns, enemies and exploration.** The Warcraft influence means an inhabited adventure world; it does not itself request classes, raids, subscriptions, copied characters or an MMO.
- **Solo first, then small co-op.** Start offline with one player; later support a host and up to three friends.
- **The main landscape as the setting.** The owner questioned why gameplay remained in a tiny crop. Populate the existing full terrain progressively.
- **Useful creations.** A decorative house mesh or a boat-only editor does not meet the requirement. Homes, storage, crafting and player-built routes must affect play.

The owner now wants a **warm, welcoming game rather than a robot game**. The owner explicitly clarified **LEGO-style characters**. The cast must be approachable toy minifigure people: yellow cylindrical heads with simple friendly faces, molded hair, blocky torsos/legs, curved toy hands and warm printed-looking clothing. Do not replace robots with organic, realistically proportioned humans. Build welcoming brick settlements with timber/stone/clay colors and a restrained warm interface. Keep original brick construction and the full landscape; preserve the old robot as legacy content or an explicitly temporary development placeholder, not the intended default cast. Exact clothing and village imagery are proposed choices, not fabricated owner approval. Use original characters, lore and assets without licensed LEGO or Warcraft branding. Boats, machines and salvage can remain useful optional activities. Completing the old awkward-crate job is not a prerequisite for building a home or meeting an NPC.

**Player promise:** explore a world, help its people, face its dangers, and build places and machines that make it your own.

**Loop:** discover a place/person → choose a quest or personal goal → gather, craft, explore and fight when needed → build something useful → gain a capability or change the world → travel farther or improve your home.

Judge progress by what the player can do and why they want to do it. Test counts, menus and static art do not establish an adventure.

## 2. Working scope

These defaults guide implementation; exact content counts and timing are design targets, not additional owner promises.

- Eventual full-game targets: native Linux and Windows. Keep a desktop WebGPU opening/demo and parity target; browser campaign scale depends on measured memory and storage limits.
- Use `data/generated/td_seed_1234_8192.ldh`, approximately 8.2 km across at one-metre sample spacing. This is a finite world, not an infinite procedural one. Loading the whole landscape does not mean all of it is authored content yet.
- Begin with one inhabited town, a player-chosen home site, a purposeful route, optional ruins and a reachable relay outpost. Add populated regions after the opening works.
- Replace the adventure robot with an original approachable LEGO-style person avatar while reusing the third-person camera/controller and maintaining existing house clearance. First-person view is optional later work, not a requirement inferred from Minecraft.
- Use an original brick-minifigure frontier cast and suitable woodland/adventure threats. Robotic enemies are no longer the assumed opening theme. The world should feel inhabited through interaction and activity, not dozens of stationary quest markers.
- Progress initially through discoveries, tools, equipment and recipes. Start with attack, dodge and health. Classes, levels, talent trees, loot rarity and instanced dungeons require a later explicit design decision.
- Homes provide rest and safe recovery; chests provide storage; benches provide crafting; bridges and stairs provide traversal; outposts support distant exploration.
- Do not make hunger/thirst, rent, fuel chores or material grinding the main reason to build. Provide starter supplies so the player can build immediately.
- Protect small visible spaces around critical town routes and quest objects. Elsewhere, construction is free within reach, terrain/support, collision and capacity rules, not confined to fixed workshop plots.
- Gather from authored resource nodes and objects. Removing a tree/rock object does not excavate the heightfield. Foundations, piers and stairs adapt to slopes; general digging and volumetric caves remain excluded.
- No general house collapse, brick-by-brick rigid-body simulation, flooding, structural stress, PvP, aircraft, MMO services or procedural quests before the opening.
- Preserve legacy LEGO, terrain, RIDGEBREAK, WRECKWATER and Cove routes and their save identities. Do not restart the engine from scratch.

## 3. First adventure: a home and a light beyond town

**Working design; target 20–30 minutes, not yet playtested.** The town's relay has gone dark. A resident helps the player establish a home, then asks them to investigate a distant landmark. Restoring it reveals another place to explore.

1. Arrive in the town on the actual main landscape. See the distant destination, meet an NPC and receive enough materials to build immediately. Use short dialogue and one clear next action.
2. Choose a suitable home site. Build a walkable room with roof and access; add a bed, chest and workbench. Offer an optional starter blueprint, but accept different layouts.
3. Use the home: register the bed, store real items and craft a useful tool. These functions and ownership survive reload before broader RPG systems are added.
4. Explore toward the relay. Find an optional ruin and useful materials. Cross a broken path with player-built steps/bridge, or discover a longer walking route. Both solutions must work through actual collision.
5. Meet a readable threat with a telegraphed attack and retreat route. Fight or maneuver past it where the quest permits. Defeat must not erase the house or unique progression items.
6. Recover the relay component, establish a useful field outpost and repair the relay through ordinary interaction. Its light, NPC dialogue and revealed destination change persistently.
7. Return to expand the home, follow the new destination or choose an optional activity. End with an achievement and a reason to continue.

Allow building or exploration first. Quest definitions must explicitly recognize permitted work completed before acceptance. Do not demand one prescribed house or force a successful design to fail.

Opening content: one town, three named friendly NPCs, four linked quests, one optional side quest, two optional discoveries, one enemy archetype with a tougher encounter variant, one relay/outpost and one visible next destination. Add a second enemy archetype after the first encounter works well.

The linked quests are: establish/use a home; prepare useful equipment; investigate/recover the relay component; restore the relay. The side quest can unlock another useful building recipe without blocking the main route. Avoid repeating four versions of “bring ten items.” Human play establishes timing and enjoyment.

## 4. Useful buildings and construction rules

| Creation | Required opening function | Later extension |
|---|---|---|
| House and bed | Walkable shelter, rest and safe recovery point | Rooms, decoration, NPC guests |
| Chest | Persistent storage and real item transfers | Sorting and shared permissions |
| Workbench | Craft building pieces and useful tools | Recipes from quests/discoveries |
| Bridge, stairs, platform | Physically reach a place or cross an obstacle | More demanding routes |
| Outpost | Recovery/storage/crafting farther from town | Earned travel links |
| Boat or machine | Transport or solve a selected world problem | Specialized exploration/salvage |

Free placement must work while exploring, outside the dock workshop. Support building modules and individual bricks. Initial kit: foundations/piers, floors, walls, wall openings, roofs, stairs, beams, compatible bricks, bed, chest and bench. Interiors stay empty and traversable.

Reuse the one-metre stud, .32 m plate, .96 m brick body and .02 m integer lattice. Do not silently rescale old parts or the 1.7 m player collision height. Author ceilings, openings and stairs around measured player/camera clearance; initial opening target is at least 1.2 m wide and 2.2 m high. Verify an actual room.

- Preview, picking and acceptance share terrain/structure geometry. Show cost, snap and a short valid/invalid reason before placement.
- Foundations anchor to permitted terrain; attached pieces need a supported path. Start with a simple deterministic support rule, not stress simulation.
- Check reach, permissions, player overlap, protected NPC/quest access, support, solids and material/capacity limits. A room-sized enclosing collider must not fill the interior.
- Initially refuse removal of essential support rather than causing uncontrolled collapse. Explain the dependency.
- Blueprints are layouts, not free inventory. Copy/undo/removal/reload cannot manufacture resources. Describe refund rules before implementation.
- Give furniture stable functional IDs. Create a chest and its container atomically. Refuse removal of a nonempty chest; never delete its contents silently.
- Bed removal clears/changes the registered recovery point. Recovery always has a safe town fallback if a bed is blocked, missing or unavailable.
- Bench use requires a reachable usable bench. Crafting checks output space before consuming ingredients.
- Shelter recognition uses a documented geometric rule for roof, support and accessible sleeping space. Accept at least two distinct room layouts; a bed mesh alone does not satisfy a house quest.
- Painting, geometry replacement or region unloading must not reset a component's contents, ownership or quest state.

## 5. Source map: reuse versus new work

| Area | Existing source/foundation | Missing adventure capability |
|---|---|---|
| Terrain | `src/terrain/heightmap.*`, `lego_surface.*`, `lego_layout_cache.*`; `lego_world.cfg` | Actual adventure placement and population; stud shading is not voxel data and layout streaming is not entity streaming |
| Coordinates | `src/physics/physics_types.hpp`, 256 m sectors | Stable world entities/structures and region spatial ownership |
| Construction | `src/game/construction/{construction_types,build_model,part_catalog,assembly_compiler,build_refit}.*` | Grounded world structures, house kit and furniture functions; a boat snapshot has no house location/container |
| Editing | `src/game/expedition/cove_workshop.*`, `design_library.*` | Free world placement without boat Launch; reuse selection, snap, paint, undo and blueprints |
| Player/camera | `src/game/expedition/{cove_player,cove_camera}.*`, `cove_character.hpp` | General world collision/interaction and combat states; current adapter assumes the small Cove |
| Scenery | `src/game/assets/cove_environment.*`, `src/game/expedition/cove_environment_collision.*` | Owned player construction; the authored workshop has no construction/inventory identity |
| Authority | `src/game/expedition/{game_session,session_transactions,session_recovery}.*` | Adventure domains; existing command variants and jobs are salvage-specific |
| Items | Paid/stored boat parts and two `ResourceAmounts` balances | Backpack stacks, equipment, containers, recipes and loot |
| Quests | Single cargo job, presentation-only onboarding | Dialogue, objective graphs, quest chains and general reward policies |
| Saves | `session_save.*`, `save_generation.*`; native `save_store.*`/`save_worker.*`; `web/expedition_store.js` | New canonical adventure profile/schema; Cove archives bind one boat and named cargo roles |
| NPC/combat | Character rendering and physics/query primitives | Population, navigation, enemy behavior, health, attacks, loot and recovery rules |
| Presentation | Toy kit, robot clips, terrain/water lighting, accessible input/preferences | Town/enemy/furniture content and sound; current captions are not an audio backend |
| Networking | WRECKWATER experiments and ownership principles | Adventure co-op; no generic RPG replication or cross-device float lockstep is established |

The Cove baseline passed native/browser journeys, 2,243 game tests and 138 UI tests. Those results do not prove a new house, NPC, quest or enemy. Full original evidence is retained in the archived plan.

## 6. Architecture contracts

### A. Runtime and world profile

The implemented modules live under `src/game/adventure/`: `AdventureRuntime` composes the feature; `AdventureSession` owns accepted game state. Keep rendering, physics and platform storage behind existing interfaces. Runtime integration alone does not pass the gameplay gates below.

Develop via proposed `adventure.cfg` and `?experience=adventure`, on the full landscape. At G-A, expose it as the clear new-game choice at the existing preview origin. Preserve explicit legacy Cove continuation and never interpret a Cove world ID as an adventure save. A development route must not become another permanent cropped test world.

`Application` delegates adventure behavior. Avoid adding all quest/NPC/inventory rules to its already large source file. Reuse construction types and storage directly where they fit; extract narrowly shared transaction utilities only as needed. Do not copy/rename the whole salvage session or build a new generic ECS, scripting language or server framework first.

`world_definition.*` binds the initial profile, terrain digest/scales/recipe, spawn/recovery points, landmark and protected spaces. Extend it with towns, NPCs and quest sites as those tasks are implemented. Saves reference trusted installed definitions, not arbitrary content supplied by save bytes.

### B. Full terrain and spatial ownership

- Use `data/generated/td_seed_1234_8192.ldh` at its existing one-metre scale and shared LEGO surface. Author one starting area while keeping the surrounding landscape visible and traversable.
- Current `data/lego_shore.ldh` is a 256² crop at sample origin `[2816,7424]`. It is not the new world.
- The full-map loader currently applies a separate WRECKWATER authored patch, while the crop has a conditional berth-depth edit. Define the adventure terrain recipe explicitly and include it in content identity; do not inherit terrain changes merely from a filename.
- The unmodified crop-to-main offset is X −1,152 m, Z +3,456 m. It is not proof of safe placement or save compatibility. Choose the town site from actual surface, water depth and routes.
- Keep terrain, studs, walking, camera and collision consistent. Support built bridges/ramps over fixed terrain; no general underground excavation or voxel rewrite.
- Keep the full terrain resident initially. One 8192² R16 field is 128 MiB; its GPU mip chain is about 170.7 MiB, separate from the CPU copy and other assets. Never load one full copy per region.
- The 4 MiB LEGO layout cache streams cosmetic ownership in 32-cell chunks; it does not stream game state or terrain samples.
- Use existing 256 m sectors for initial region indexing where practical. A cross-boundary structure has one owner plus overlap references. Pin the player's collision neighborhood, occupied structures, interactions and pending edits.
- Unload derived runtime resources, not saved construction, quests or loot entitlement. Far NPCs need not run full physics. Introduce terrain paging only for a measured need after the first playable house/quest.

### C. Canonical records and commands

Preserve checked durable IDs, canonical ordering and native/WASM lossless counters. Use metres/seconds and the .02 m isotropic lattice with proper rotations. Canonical content is +Y up, +X right, −Z forward; explicitly bridge the engine's left-handed rendering conventions.

| Proposed record | Required canonical data |
|---|---|
| `AdventureWorld` | World/profile/content identity, epoch/tick, ID horizon, world flags, region revisions |
| `PlayerRecord` | Identity, pose/recovery point, health, backpack, equipment, discoveries, quest progress |
| `WorldStructure` | Stable ID/owner, region/lattice origin, accepted revision, parts/connections/paint, components |
| `StructureComponent` | Stable ID/type/anchor, container/bed/bench/device state and permissions |
| `ItemDefinition` / `ItemStack` | Versioned content ID, stack bound/tags/function; quantity and owned location |
| `ContainerRecord` | Stable ID, owner/permissions, bounded slots, revision and contents |
| `RecipeDefinition` | Ingredients/output, station and unlock predicates |
| `QuestDefinition` / `QuestState` | Versioned objectives/prerequisites/reward; accepted instance, facts and claimed receipt |
| `WorldEntity` | Stable spawn/entity ID, definition, region, persistent health/death/resource generation |
| `CompiledStructure` | Derived geometry, collision/query acceleration and render mappings; never inventory authority |

Start with wood, stone, scrap, building pieces, tools, a few consumables and unique quest components. Catalog IDs and quantities are explicit. A resource node has a persistent spawn/depletion generation; unloading cannot refill it accidentally.

The session accepts intents such as Gather, TransferItems, Craft, Equip, Place/Edit/RemoveStructure, UseComponent, AcceptQuest, TurnInQuest, Interact, Attack and Respawn. Validate caller, revision, reach, permissions, capacity and gameplay predicates.

- Reserve output, geometry and persistence capacity before consuming inputs.
- Transfer atomically between both container revisions. Craft consumes ingredients and creates outputs together; full output space leaves inputs untouched.
- Create/remove functional state in the same transaction as its accepted structure change.
- Keep the old revision usable while replacement prepares. Publish logical geometry, collision, queries and render mapping together at a defined boundary.
- Tag async work with world epoch/object revision; stale work cannot publish. Durable IDs are not GPU slots.
- Keep bounded idempotent receipts and durable generation/high-water state. Receipt eviction never makes a quest, death or resource payable again.
- Undo is a compensating transaction; it cannot rewind unrelated transfers, quest rewards or another player's later work.
- Presentation, tutorial progress, animation and debug observations never grant items or objective success directly.

### D. Static building runtime

Houses are static structures; vehicles and actually moving machines remain dynamic assemblies. Never create a dynamic rigid body per house brick or require propulsion/buoyancy to admit a building.

Batch visible parts by shared mesh/material and region, with structure/cluster bounds and detail levels. Compile nearby static collision into bounded compounds or spatial batches. Reuse `AuthoredShapePool` and its submission-completion retirement discipline where appropriate. Draw, instance, collision-child and rigid-body budgets remain separate.

Replace the small flat Cove obstacle packet with a spatial query interface for terrain, admitted structures and relevant actors. Walking, camera obstruction, placement, interaction picking and navigation use compatible accepted geometry. Do not show a wall before its collision exists or remove a floor before coordinated replacement is ready.

Keep rooms and doorways empty. Moving doors/devices need explicit state and matching collider behavior when added. An open doorway is sufficient for the first usable house; closed doors are required before claiming an enemy-proof interior. General mechanical joints and house collapse are not opening prerequisites.

Do not allocate a unique material/texture per brick. Preserve old shape/render data until all referencing GPU submissions complete. Apply the existing lighting/water ordering and resource ownership contracts rather than making a separate house renderer.

### E. Player interaction and inventory

Reuse walking, jumping, swimming, orbit camera, controller and accessibility settings; generalize scene queries beyond dock/boat transitions. Add context interaction, compact hotbar, backpack, world-building mode and short quest tracker.

Building flow: choose piece → aim/snap/rotate → place → remove/undo. It must not require entering the boat workshop or pressing Launch to make a floor usable. A blueprint gets a full-cost preview and whole-placement validation.

Begin with a bounded backpack and chest, one tool/weapon slot and one utility slot. Implement stack splitting, full-container refusal, gathering, crafting, equipment and loot as ownership operations. Preview props are not usable items.

The current preference schema is strict with 71 actions. Add an explicit compatible version migration for new actions, preserving existing bindings/settings and fixed escape routes. Do not break saved preferences by appending mandatory fields. Extend the shared router and both host UIs together.

### F. Quests, towns and progression

Use a small data-defined quest graph, not a new scripting language. Initial objective types: TalkTo, DiscoverLocation, Gather/DeliverItems, BuildUsableComponent/Shelter, Craft/UseItem, DefeatEncounter and ActivateWorldObject. Each declares current-state, event-since-start or historical semantics, including pre-acceptance work and event deduplication.

Turn-in consumes required items, claims the quest instance, grants output and changes world flags in one durable transition. Check output capacity before consuming anything. Dialogue/tracker/markers derive from authority, not the reverse.

Separate personal progress from shared world facts now. A repaired relay is one persistent world change; later guests need an explicit eligibility/credit policy without receiving repeated world-object payouts. Persist quest-instance and reward receipts.

NPC definitions include dialogue branches, interaction range, activity anchor/patrol and fallback. Start with three residents and short meaningful dialogue. Animation does not imply full daily-life simulation. Keep critical NPC access protected and construction restrictions visible.

Landmarks, names and optional map guidance should make quests legible without long text or color alone. Permit creative traversal and valid alternate builds. A recipe reward must enable something useful in the next journey.

### G. Navigation, combat and recovery

Start with bounded navigation over the opening area, combining terrain walkability and accepted structure occupancy. Represent multiple walkable heights at the same X/Z using layered spans/tiles or an equivalent bounded graph: ground beneath a bridge, the bridge deck, stairs and upper floors are distinct surfaces. One blocked/unblocked terrain cell cannot represent them. Rebuild only changed tiles; tag/revalidate paths against relevant revisions. Bound path requests/expansions. Blocked actors wait, repath or return instead of clipping through walls.

Choose one authority/solver owner per actor. Reuse kinematic character movement/spatial queries where appropriate; never simulate independent CPU and GPU versions of the same actor. Gameplay uses fixed ticks and animation interpolates accepted state.

Enemy states: Idle/Patrol, Notice, Chase, Windup, Attack, Recover, Return, Dead. Define attack shape/range, visible windup, one damage window, cooldown, sight and retreat/leash behavior. Player attack and dodge use the same authority. Queries need coherent accepted actor/obstacle state, not stale debug readback or animation-only damage.

Enemies collide and navigate around player structures. Prevent spawns inside homes or at the recovery point. Towns remain usable. General siege AI and enemy destruction of houses are later choices, not opening requirements.

Stable encounter/spawn generations control death and loot. One death creates one loot entitlement; pickup transfers it once. Initial player defeat retains equipment, buildings and unique quest progress, returning to a valid bed or town. No corpse-run or hunger requirement. Rest at a safe usable bed restores health.

### H. Saves and region activation

Create a new versioned adventure schema/profile. Reuse proven transport, integrity, exclusive ownership and storage recovery where compatible; do not insert untyped RPG blobs into the Cove boat archive.

Persist player state/items, structures/components/containers, quests/receipts, world flags, resource/encounter generations, discoveries, content versions and ID horizons. Capture a coherent accepted state. Never save a spent backpack independently from the house made with its materials.

For the first bounded region, a whole-adventure snapshot is acceptable. SAVE-A01 must define manual/event checkpoints and the dirty interval for repeated edits. Only confirmed publication may say “saved.” An accepted click is not necessarily durable yet; quota/disk errors must leave recoverable visible unsaved progress.

Before independently stored regions, use immutable region payloads plus one world commit manifest binding every changed region/global/player revision. Publish the manifest last. Interrupted saves recover a complete old or new generation; retain prior reachable payloads until safe reclamation. No mixed independently saved inventory/building state.

Pin occupied/edited regions and prioritize nearby collision before travel. Unloading derived resources cannot delete houses or replenish gathered resources. An unavailable bed resolves its region and safe clearance or uses town fallback.

Cove save identity includes fixture content, terrain dimensions/samples and scene origin. Retain its old route/profile. Do not silently rebase saves. A future validated design import transfers layout only, consuming adventure-owned materials normally.

### I. Small co-op later

Use host-authoritative state and the same validated commands as offline. The host save owns world structures, containers and flags. Guests have stable participant identity and explicit construction/storage permissions. COOP-A01 fixes shared-material, personal-quest and world-credit rules before exposing multiplayer transfers.

Reliable messages carry commands/receipts and structure/quest revisions; bounded snapshots carry motion. Prediction/interpolation cannot grant items or damage. No cross-device float GPU lockstep. Unknown revisions need bounded wait/request handling.

Solo pause stops the world; co-op menus do not. Edits and transfers use revision checks/short leases without freezing everyone. Test competing chest transfers, stale builds, late join, disconnect, host exit and restart. Prove two players before four. No MMO service, dedicated deployment or seamless host migration is required.

## 7. Budgets and quality targets

These are initial gate targets to implement and measure, not proven capacities. Show the first functioning home before expanding to the full target; describe its current supported limits honestly. Refuse overflow without deleting accepted work. Record reasons when revising a budget.

| Opening target | Initial requirement |
|---|---|
| Populated route | One town and roughly 400–800 m of purposeful travel; full landscape remains loaded |
| Construction | Four independent structures, 1,024 total accepted parts; at least 256 parts in one house |
| Functional components | 32 total beds/chests/benches/devices |
| Inventory | 24 backpack slots; 32 per chest; stack bounds in item content |
| Population | Three friendly residents; up to eight active hostiles |
| Content | Four main quests, one side quest, two optional discoveries |
| Performance | Target 60 FPS at 1080p during actual walking, construction and combat on named hardware |

Current Cove limits (48 environment boxes, 59 player obstacles, 96 placements, eight-build/256-total-part defaults and 256 fixture nodes) are not settlement capacity. Audit separately and replace narrow adapters; do not increase every constant or compile one giant compound.

Measure CPU gameplay, physics, GPU frame time, p95/p99 presented cadence, input response, edit/region hitches and CPU/WASM/GPU memory including staging/retirement overlap. Use a moving populated scene, not cached/offscreen frame rates.

Provisional release thresholds: p95 presented cadence within 16.67 ms under a declared tolerance, p99 below 25 ms, no routine edit/region hitch above 50 ms. Record CPU/GPU/RAM, drivers, power mode, viewport and browser. Do not silently lower workload to pass.

Historical browser startup used about 404 MiB of a fixed 512 MiB WASM heap. This is not the new game's measurement. Account for full terrain and populated-world peak memory before adding allocations. Native headroom does not prove browser headroom.

Human target: the owner can start unaided, understand a quest, build/use a home, solve an exploration obstacle and choose to continue. Later observe 5–8 real newcomers, including accessibility needs; record confusion, time, reasons to build and voluntary continuation. This guides design, not commercial-retention claims. Never fabricate participant results.

## 8. Ordered implementation TODO

Claim work in the ledger before editing code. Every task needs its behavior and relevant checks. Necessary support work belongs inside a player-visible deliverable, not a long chain of invisible framework milestones.

### A — live and build on the main landscape

- [x] **WORLD-A01 — Enter the actual adventure world.** Add installed full-terrain profile/recipe, safe town/home-site spawn and focused AdventureRuntime. Reuse robot, camera, pause and controls; place a landmark and walkable route. **Pass:** native/browser exploration on the full landscape, consistent terrain/character collision, no legacy save reinterpretation. Show it before the rest of G-A is complete.
- [x] **ITEM-A01 — Own construction supplies.** Develop alongside BUILD-A01. Add the small item catalog, bounded backpack, one-time starter supplies, gather/consume/refund operations. **Pass:** build immediately, gather a real node, spend stock and refuse full/unaffordable operations without duplication. Starter supplies cannot be claimed repeatedly.
- [x] **BUILD-A01 — Freely place a grounded structure.** Needs WORLD-A01 and ITEM-A01 integration. Add world structure IDs, compatible kit, preview/picking, support/overlap/protected-space rules, place/remove/undo and coordinated static collision/render publication. **Pass:** choose a location and build two different traversable room layouts with openings, stairs and roof. Refusals preserve materials; floors/camera/picking agree. No dock or boat Launch dependency.
- [x] **HOME-A01 — Make the home useful.** Needs BUILD-A01/ITEM-A01. Add bed, chest and bench, shelter/clearance rules, transfers, one useful recipe and safe recovery. **Pass:** register/rest, store/retrieve, craft, handle nonempty chest removal and blocked bed recovery clearly; different valid houses work.
- [x] **SAVE-A01 — Keep the home and possessions.** Develop with A tasks. Add adventure schema/profile, coherent checkpoints, dirty/error feedback and strict restore. **Pass:** build/use/save/leave/restart with exact structures, items, containers and bed. Storage failure is recoverable; old Cove saves still open through their own route.
- [x] **G-A — A place of your own.** Needs all A tasks. Deliver at the normal preview entry: choose a site, build/use a home, explore away and return after restart. Verify the declared construction budget. Commit through the normal hook. **This is the next game milestone, ahead of the old crate job.**

### B — an inhabited world with a real quest

- [ ] **CHAR-B01 — Warm LEGO-style people.** The owner explicitly requires LEGO-style characters and warmth. Author/render a toy minifigure protagonist and three toy minifigure residents: yellow cylindrical heads, friendly printed-looking faces, molded hair, blocky clothing/legs and curved toy hands. Organic human proportions, individual fingers and sculpted realistic faces do not meet this task. Keep editable Blender sources, distinct role silhouettes and real rigid-part animation. Reuse controller/camera and stable NPC/quest IDs. **Pass:** ordinary native/browser movement, building, doorway/stair traversal and conversation with the brick-minifigure cast; saved houses, possessions and progression remain exact. Robot assets stay available to legacy routes.
- [ ] **WARM-B01 — A welcoming place to build and explore.** Follow the [warm direction](docs/design/warm-adventure-r01/README.md). Develop one inhabited starting area and player-built home with warmer modular materials, appropriate architecture and restrained natural dressing. **Pass:** one convincing playable village/home view with real collision, preserved existing builds and useful interactions; not an orange filter or a concept-image substitute for a game. Do not turn this into another environment screenshot loop.
- [ ] **UI-B01 — Make the adventure interface usable.** Owner-requested priority during G-B. Implement the [Explore / Build / Talk direction and self-contained checklist](docs/design/ui-prototypes-r01/README.md): remove duplicate browser/native overlays, provide an illustrated building picker with a compact aiming tray, focused dialogue/crafting/storage/journal views, readable input prompts and stable menu intents. **Pass:** actual native/browser play with keyboard/controller/pointer, large text and narrow viewport; clear focus return, visible placement, meaningful Journal and no double-click reinterpretation. Prototypes are complete design artifacts, not completion of this implementation task or invented owner approval.
- [ ] **NPC-A01 — Meet the town's people.** Needs WORLD-A01/shared spatial queries. Add three residents, short dialogue, names/interaction and bounded idle/patrol behavior. **Pass:** ordinary keyboard/controller dialogue works and closes safely; accepted construction preserves mandatory access.
- [ ] **QUEST-A01 — Finish a meaningful quest.** Needs HOME-A01, SAVE-A01 and NPC-A01. Add objective definitions, journal/tracker, accepted facts, prerequisites and durable rewards. **Pass:** establishing/using a valid home completes the first quest, recognizes permitted prior work, and survives restart. Repeated turn-in cannot pay twice; full inventory cannot lose a reward.
- [ ] **PROGRESS-A01 — Unlock something useful.** Needs QUEST-A01. Award a tool/building recipe that helps the next destination. **Pass:** it is unavailable before the accepted unlock, persists afterward and changes what the player can do.
- [ ] **G-B — People and purpose.** Needs G-A and B tasks, including UI-B01. Deliver a usable real town/home/NPC quest with persistent consequences. Ask the owner to play and record feedback without inventing approval. Commit the verified implementation.

### C — exploration and danger

- [ ] **EXPLORE-A01 — Author the relay route.** Needs G-A/PROGRESS-A01. Add landmarks, ruins, useful discoveries, resources and destination on the main map. **Pass:** built bridge/stairs and longer detour both work; discovery rewards are useful; exploration continues beyond town.
- [ ] **AI-A01 — Navigate a changing built world.** Needs BUILD-A01/NPC-A01. Add bounded layered paths, occupancy updates, sight and repathing. **Pass:** actors can walk across and beneath a built bridge and climb built stairs; new/removed walls and bridges change valid routes without clipping, unbounded jobs or trapped mandatory NPCs.
- [ ] **COMBAT-A01 — Finish one readable encounter.** Needs AI-A01/ITEM-A01. Add real health, attack/dodge, enemy states, hit feedback, death/loot and retreat. **Pass:** land/avoid attacks, fight or retreat around structures, recover at bed/town and claim loot once; unique progress survives restart.
- [ ] **OUTPOST-A01 — Change the world through the quest.** Needs EXPLORE-A01/COMBAT-A01/QUEST-A01. Recover the component, build/use an outpost and activate the relay. **Pass:** light, dialogue and revealed destination change from real accepted actions; alternative construction works; duplicate interaction/reload cannot duplicate rewards.
- [ ] **G-C — First complete adventure implementation.** Needs G-B and C tasks. Complete section 3 through ordinary controls on native/browser, including reload/recovery, two house layouts and both obstacle solutions. Reuse unrelated evidence. Commit the engineering checkpoint.
- [ ] **FUN-A01 — The opening is enjoyable.** Needs G-C. The owner and then actual newcomers play it. Record clarity, timing, building choices, combat readability and desire to continue; implement fixes. Keep unchecked if people are unavailable, while continuing independent engineering. Tests cannot certify fun.

### D — enough content to keep exploring

- [ ] **WORLD-A02 — Activate/persist populated regions.** Needs the implemented G-C domains. Add region ownership, spatial activation, immutable payloads/commit manifest and occupied-region pins. **Pass:** leave/revisit edited buildings, nodes and quests with bounded residency; interruptions never mix inventory and older structures.
- [ ] **CONTENT-A01 — A second distinct adventure area.** Needs G-C. Add another settlement/outpost, enemy behavior and quest chain with a different building/traversal problem. **Pass:** the new capability matters and both areas stay connected on the main landscape.
- [ ] **BUILD-A02 — Expand useful creation.** Needs G-C. Add structural variants, doors, decoration, blueprint placement and recipes; improve controls at budget. **Pass:** distinct homes, workshop and remote outpost have useful roles without breaking navigation or frame time.
- [ ] **ADVENTURE-A02 — Optional boats and machines.** Needs a playable building/quest world. Adapt existing mechanics to selected adventure sites and inventory ownership. Decide if the parked crate is useful content before resuming it. **Pass:** the activity helps exploration/progression and preserves old routes; it is not mandatory scaffolding for unrelated quests.
- [ ] **CONTENT-A02 — Lock the full solo content manifest.** Needs FUN-A01 and CONTENT-A01. Specify populated regions, main/side quest chains, enemies, useful unlocks and ending in-repo. Keep the full terrain. Base pacing on real play; no invented campaign-hour claim. Classes/dungeons/larger RPG systems require an explicit decision.
- [ ] **G-D — A coherent solo game.** Needs WORLD-A02, CONTENT-A01/02, BUILD-A02 and whichever optional activities the release manifest selects. Complete multiple quests, build useful places, explore connected regions and reach the ending with working saves/recovery and measured performance. Commit the passing checkpoint.

### E — play with friends

- [ ] **COOP-A01 — Fix shared-world rules.** Needs G-C and the implemented region model. Define identity, construction/storage permissions, personal/world quest credit, drops, recovery, pause and host exit. **Pass:** executable two-player scenarios protect concurrent builds/transfers/rewards.
- [ ] **COOP-A02 — Two-player adventure.** Needs COOP-A01. Reuse offline authority with network intents, reliable revisions and bounded motion snapshots; implement join/late join/reconnect. **Pass:** native host with supported native/browser guest explores, fights and builds; disconnect/stale requests preserve host state.
- [ ] **COOP-A03 — Four real players.** Needs COOP-A02. Expand measured interest/AI/physics/render/persistence workloads. **Pass:** four actual clients meet budgets without hidden reduced workload or simulated clients presented as real play.
- [ ] **G-E — Dependable small co-op.** Needs E tasks and G-D content. Complete opening and later quest together, including disconnect and host restart, without lost houses or duplicate rewards. Commit. No MMO infrastructure implied.

### F — finish and release

- [ ] **POLISH-A01 — Finish world, character art and sound.** Make targeted assets alongside gameplay; complete the selected release kit here. Include interiors, residents/enemies, readable attacks, landmarks, original audio and volume controls. Audio follows accepted events. Concepts do not pass runtime art acceptance.
- [ ] **ACCESS-A01 — Finish accessible play.** Maintain parity as features arrive; complete text scaling, short tutorials, remapping migration, motion options and non-color cues across dialogue, inventory, combat and building. Real captions follow actual audio. Conduct human dyslexia/accessibility sessions and fix observed issues.
- [ ] **PLATFORM-A01 — Native Windows/Linux release support.** Finish missing Windows durable storage, lifecycle, content packaging, settings and recovery. Test actual machines. Linux evidence does not certify Windows.
- [ ] **QA-A01 — Complete product verification.** Run meaningful regressions, save migration/fault checks, ordinary-control playthroughs, region/construction budgets, selected co-op cases and visible performance on declared hardware. Include full storage, interrupted saves and inaccessible recovery points.
- [ ] **RELEASE-A01 — Prepare distribution.** Complete selected content, onboarding, credits/provenance, default entry, packages and recovery documentation. Browser demo limits are explicit; legacy experimental routes must not confuse new players.
- [ ] **G-F — Release candidate.** Needs G-D, G-E and F tasks, or explicit owner-approved solo release scope deferring co-op. Content, human play, platforms and performance pass. Commit the candidate. External publishing/spending still needs actual authorization; a successful startup is not release completion.

## 9. Asset workflow

Blender is installed. Reuse parameterized/headless Blender sources and GLB/VMESH cooking conventions in `tools/salvage_assets/` and `data/salvage/`. Blender MCP is optional, not a prerequisite or a new installation task. An agent can author Blender scripts; generated code alone is not an accepted model.

The owner permits image generation for concepts, textures and illustrations, including their reference to “image gpt 2.5.” Use the actually available image tool; record a model identity only when exposed. Do not claim a specific requested version without tool support.

Use generated images for useful town/house/enemy design exploration or actual textures. Keep existing UI icons/ghosts in their code/vector systems. A concept does not replace interactive construction, collision, authored meshes or proper materials.

Asset deliverables: editable source, stable versioned ID, real scale/axes, materials, suitable LODs, simplified collision, interaction/function anchors and provenance. Reuse mesh/material resources across instances. Distinguish static construction, actors and cosmetic props.

Judge new art through one representative playable house/interior and one town encounter. Capture materially changed views only; no repeated screenshot matrices for small color/camera changes.

## 10. Working and validation rules

- Communicate briefly and clearly; the owner is highly dyslexic. Game text should be short too.
- Prioritize the next player-visible gate. Supporting authority/storage/collision work enables that deliverable; it is not a substitute for it.
- Delegate bounded non-overlapping work after agreeing shared data contracts. One owner integrates application/schema/build changes.
- Preserve unrelated files and saves, including parked `cove_cargo.*`, `fixture-cove-two-jobs-r01.json`, `salvage_cove_two_jobs.cfg` and scratch patches. Their existence does not make them the new priority.
- Register shared code in both Bazel/CMake and actual WASM packaging. Declarations or isolated tests alone do not establish integrated features.
- Keep tasks unchecked until behavior and relevant verification pass; use the ledger for progress. Old engine checks do not complete new adventure tasks.
- Use focused authority/geometry/UI/storage checks and one final ordinary-control journey per runtime for the changed milestone. Repeat only after relevant changes or concrete failures; reuse passing unaffected stages.
- No state injection, fake rewards/NPCs, inspection pages or substituted physics as player-journey evidence. Read-only diagnostics may establish ownership without screenshots.
- Record concise evidence in proposed `docs/validation/adventure/<TASK-ID>/`: source revision, behavior, actual checks, failures, limits and links to reused evidence. Avoid copying the same historical logs into every task.
- **Commit successful gates.** Keep the hook enabled: `bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`. Use `nix-shell` and a consistent toolchain environment so valid cache can be reused. Let the required hook satisfy the full check when practical; do not deliberately run two unchanged full suites. A cache miss is not permission to bypass it.
- A docs-only plan edit is not a gameplay gate and needs no game rebuild/GPU run. Stage only owned work. Publication to `main` remains authorized from prior work; never force-push or include unrelated changes.
- Preview origin remains `http://127.0.0.1:42751`. Publish the verified adventure package `build-adventure-g-a/web-r03` (`adventure-c7e884ef1ae075e8`) after the G-A commit. The bare entry and `?experience=adventure` open the building adventure; `?experience=salvage-cove` and confirmed Cove continuation remain explicit. Preserve the old package and never automatically reload an unsaved user session.
- The old four-hour session ended on 2026-09-12. It is historical; no new deadline was given for this revision. Do not invent a timer or treat elapsed time as approval.

## 11. Decisions and execution ledger

| Decision | Authority/status | Consequence |
|---|---|---|
| P01 — Game identity | Owner confirmed 2026-09-12 | Useful free construction, quests, NPC towns, enemies and exploration replace salvage-first progression. |
| P02 — Terrain | Owner chose mostly fixed terrain | Use main landscape; no digging/voxel rewrite; world building is required. |
| P03 — Multiplayer | Owner chose solo then small co-op | Prove solo opening before networking; later host plus three friends, not MMO. |
| P04 — Opening | Working design | Home → NPC quest → exploration/combat → outpost/relay → next destination; timing/names need play feedback. |
| P05 — Preserve foundations | Existing save/preservation requirements | Keep old Cove and its history within their scope; no silent save migration or engine restart. |
| P06 — Immediate milestone | Derived from confirmed direction | G-A: freely build/use/save a home on actual main terrain. |
| P07 — Warmth and character identity | Owner correction after UI prototypes | Warm game, not a robot game. The owner then explicitly required LEGO-style characters. Use warm toy minifigure people and a welcoming brick village; ordinary organic humans are not the target. Exact outfits and concept details remain proposals. |

| Work | Owner/status | Evidence/next action |
|---|---|---|
| Product and architecture pivot | Root; plan revision complete | Three explicit owner choices captured; independent world/gameplay/design source reviews informed boundaries. No adventure code claimed. |
| G-B runtime integration | Root; in progress | Own runtime, native HUD, brick-character rendering, build registration and gate publication. NPCs, dialogue, quest recognition and compass must work through ordinary controls before completion. |
| BUILD-A02 functional doors | Root with adventure_session/world/kit; implementation in progress | Add an original buildable hinged door with accepted open/closed collision, ordinary Use interaction, paid placement/refund, safe occupied-space refusal and persistent state. Preserve existing fourteen pieces and all schema1–5 worlds. This independently advances useful homes while G-B/C human and ordinary journey acceptance remain open; it does not pass G-D or bypass those dependencies. |
| ACCESS-A01 quick guide | Root with adventure_session/kit/world; implemented and component/browser verified | Six short shared topics use current controls and preserve the build setup. Core75, DOM43, native layout10 and real-terrain runtime1 pass; final native/WASM builds pass. Bounded ordinary browser guide paths pass without screenshots. Human onboarding/accessibility and full gameplay gates remain open. [Evidence](docs/validation/adventure/ACCESS-A01/guide-r01/README.md). |
| G-B quest/item/save authority | adventure_session; implemented, full gameplay verification open | Session commands, compass ownership and schema 2/strict schema 1 migration are integrated. Complete ordinary home/quest/reload journeys before gate acceptance; preserve actual G-A worlds. |
| G-B town geometry/readiness | adventure_world / adventure_session; focused checks pass | Resident admission, home readiness, village routes and new-build/reload clearance are integrated. Existing saved poses/houses retain priority. Full ordinary gameplay remains open. |
| G-B browser presentation/content | adventure_kit; implemented, partial browser checks pass | Explore/Build/Talk layout, pictured catalog, first-row Starter room and quest/compass controls are integrated. Twenty-five DOM behavior cases pass; full gameplay/accessibility checks remain open. |
| UI-B01 prototypes and usability correction | Root; implementation and partial integration pass | New layout replaces the duplicated sidebar. Ordinary browser conversation, build focus, foundation placement/refund, blueprint preview and save/reload pass. Corrected movement basis has CPU evidence; full held-key journey/controller/accessibility acceptance remains open. |
| CHAR-B01 / WARM-B01 direction | Root; LEGO-style cast/village integrated, final acceptance open | Player and three distinct toy minifigure residents render in the actual village. Editable/cooked packages and bounded changed-scene integration pass. Earlier organic-human proportions are superseded. Owner feedback and complete character/gameplay criteria remain open. |
| G-A integration / WORLD-A01 / BUILD-A01 | Root; complete | Full main terrain, default adventure entry, paid/free piece construction, usable home and visible rendering. Native/browser journeys and corrected composition passed; [gate report](docs/validation/adventure/G-A/README.md). |
| ITEM-A01 / SAVE-A01 authority | adventure_session; complete | Atomic ownership, coherent adventure saves, 1,024-part capacity, six native storage fault cases and exact browser nonempty-chest reload. [Authority](docs/validation/adventure/ITEM-A01/authority-r01/README.md), [storage](docs/validation/adventure/SAVE-A01/storage-r01/README.md). |
| WORLD-A01 / HOME-A01 geometry | adventure_world; complete | Native house/save journey, 39 focused CPU cases, real player traversal of two room layouts and stairs, native final-render startup. [Evidence](docs/validation/adventure/WORLD-A01/cpu-r01/README.md). |
| BUILD-A01 kit and browser | adventure_kit; complete | Fourteen pieces with editable/cooked sources; 16 UI cases; complete browser house journey, final visible scene and default-entry/free-cursor checks. [Evidence](docs/validation/adventure/G-A/browser-r05/README.md). |

### G-A implementation handoff

These contracts describe the completed G-A milestone. Later tasks remain unchecked. G-A capacity evidence covers authority, geometry, UI and saves, not GPU frame rate at the full limit. Two distinct room layouts were traversed by the actual player controller in CPU integration; the native/browser manual journeys exercised the starter house and individual pieces.

- **Entry and terrain:** `adventure.cfg`, browser `?experience=adventure`; `Application` delegates to `adventure_runtime.*`. Profile `voxys.adventure.main.v1`, revision `meadow-home-r01`, recipe `full-main-unmodified-lego-r01`. The full 8192² terrain skips the legacy authored Cove/WRECKWATER edits. Runtime verifies its sample SHA before accepting a world. Town is at X/Z (−63, −895); the initial landmark is (−63, −975), with a traversable meadow between them. Loading the landscape does not create the future town population.
- **Authority:** `adventure_session.*`, `item_catalog.*`. `CommandStamp` checks caller, expected world revision and monotonic request sequence. A move-only prepared change includes the entire accepted candidate; geometry must prepare before commit. Durable IDs start after reserved town/landmark IDs 1 and 2. Parts use absolute .02 m grid positions and quarter-turn yaw. Structure origin establishes one region owner; GPU instances never own inventory.
- **Supplies:** a fresh identity receives 640 wood, 320 stone and 80 scrap once. Eighteen installed supply piles near town have persistent depletion. The field hammer costs four wood and two scrap, is crafted at a reachable bench, and doubles gathering yield. Starter room placement costs 70 wood, 16 stone and eight scrap for all 19 pieces together. Removal refunds actual costs; a nonempty chest cannot be removed. Whole-room undo is one compensating transaction.
- **Geometry and movement:** `spatial_queries.*`, `construction_policy.*`, `adventure_player.*`. Shared terrain/stud queries and per-piece solids drive placement, support, capsule movement, interaction rays and camera sweep. Collision publication reserves at most 8192 solids and uses 256 m sector overlap references. A world revision is mapped to a nonzero query revision. The adventure player has a .3 m capsule radius, 1.7 m height and .36 m step. Openings are hollow solids, not a solid box around a house.
- **Support and shelter:** pieces connect through actual face contact (tolerance .021 m); grounded foundations/piers root that graph. Removing essential support is refused. Foundations may embed into the slope but must clear its highest stud. A usable bed needs a roof above, enclosure in three of four cardinal directions, and a clear adjacent recovery capsule. Recovery revalidates the bed and falls back to town when necessary.
- **Assets:** `building_catalog.*` is checked against `data/adventure/building-kit-r01/catalog.json`; `tools/adventure_assets/` holds generation/checking tools. The cooked kit contains editable Blender/GLB sources, two mesh LODs and a digest manifest. Runtime currently draws LOD0 through the shared mesh/shadow path. Green/red placement previews are opaque color previews. LOD switching, full-population batching and transparent previews are not implemented performance claims.
- **Saves:** `adventure_save.*` implements canonical little-endian `VXADHOME`, schema 1, maximum 1 MiB, whole-payload SHA and installed content/namespace validation. The snapshot coherently includes player, backpack, equipment, structures, functional components, chest contents, recovery and depleted nodes. Native uses `native_adventure_saves.*` over the existing durable save worker in its separate `adventure-v1` directory. Browser uses `web/adventure_saves.js` over database `voxys-adventure-v1`; optional local metadata key `voxys-adventure-current-v1` remembers a confirmed world. Cove bytes and identities are never migrated implicitly.
- **Checkpoint behavior:** Save is manual (F5 or the visible button). Movement and accepted edits make the world dirty. Only confirmed storage completion clears the captured revision; newer edits remain unsaved. Browser confirmed publication updates the URL with the world identity and removes `new=1`. Browser unload warns when dirty. Native override variables `VOXY_ADVENTURE_ROOT`, `VOXY_ADVENTURE_WORLD` and `VOXY_ADVENTURE_NEW` support isolated developer worlds; never use them to overwrite a player's profile.
- **Controls:** WASD/left stick move; Space/Confirm jumps; right-drag/right stick looks; E/Tool uses nearby furniture or gathers; B/View toggles building; Tab selects a piece; R rotates; Page Up/Down change height; Shift enables .02 m fine horizontal snap; click/E places; Delete removes; Ctrl+Z undoes; F2/Menu pauses; F5 saves. The browser also has named buttons and a piece selector. DOM input ownership clears physical and logical movement when entering menus. Pointer input owns aiming after actual mouse activity; merely connecting an idle controller cannot steal the cursor.
- **Observations and verification:** `get_adventure_state_json` is read-only; native `VOXY_ADVENTURE_OBSERVE` optionally emits the same bounded observation. `scripts/validate_native_adventure.py` uses ordinary owned-window key/pointer events and saved checkpoints. `scripts/package_adventure_preview.py` creates a new browser package without replacing a running preview. Engineering fixtures/capacity checks must be identified separately from ordinary-control journeys.
- **Outstanding scope:** NPCs, quest state/rewards, enemies/combat, authored relay adventure, region activation, sound and co-op remain later unchecked tasks. Do not describe the initial supply piles or landmark blocks as a populated town or a finished adventure.
| Legacy Cove | Published `acd9d43d` | [Menus, controls and safe Test evidence](docs/validation/salvage/UX-02/menus-r01/README.md); original work in archived plan. |

Record task results and source revisions here. Record the commit when a gate passes. Keep human approval separate from automated engineering evidence; if people are unavailable, leave that item open and continue useful independent work.

### Frozen G-B / r06 implementation contract

- Current source uses separate original LEGO-style resident models: Moss (builder), Rivet (outfitter), Lumen (beacon keeper), installed under `data/adventure/residents-r01/{moss,rivet,lumen}`. Their internal rigid rig remains compatible with the existing animation controller. IDs 1–3 are installed NPC IDs, separate from constructed part IDs. Initial anchors X/Z: Moss (−65.5, −893.5), Rivet (−60.5, −893.5), Lumen (−65, −898). Ground them on the real terrain inside the existing five-metre protected town area. Use bounded idle/turning behavior; general pathfinding belongs to C. Admission may choose a bounded temporary actor position or defer an actor when it conflicts with the restored player. Never reject a valid old save, move its player/house or enlarge building exclusion to fit new content.
- Moss offers **A Place to Return**. Its objective is explicitly current state: a registered, currently usable sheltered bed, plus a chest and workbench in the same owned structure. An empty chest and a valid alternate house qualify. Recognize existing G-A work; do not invent past crafting/transfer facts, require a hammer or prescribe the starter blueprint.
- Persist `FirstHomeProgress` with phases NotAccepted/Active/Completed and a permanent nonzero completion-revision receipt. Ready is derived presentation only. Dialogue resolves Completed first, then NotAccepted, then Active readiness. Explicit acceptance precedes turn-in; revalidate current shelter, giver range and sight at turn-in. Completion and its reward receipt publish atomically. House removal never revokes an earned reward; duplicate/stale/foreign requests cannot repeat it.
- Reward: the **Trail Compass recipe**, derived from the completion receipt; no duplicate unlock bits and no inventory slot cost for knowledge. At a reachable bench, spend two wood and four scrap to craft ItemKind 5 (stack limit one). Existing hammer/pieces remain available. An owned compass must move atomically into the new utility equipment slot to activate real bearing/distance to the relay or registered usable home. Merely holding it in the backpack does not activate it. Full output space preserves ingredients; any compass in any owned location requires the permanent receipt in this solo gate. Later trading needs an explicit rule.
- Write schema 2; accept exactly schemas 1 and 2. Freeze the old installed content identity and item/piece bounds before validating old archives, then preserve every old field and default new quest/greeting/utility fields. Schema 1 item byte 5 remains invalid. No starter grants or inferred quest completion during migration. Persist only the three known greeting-mask bits. Keep `adventure-v1` native directory, `voxys-adventure-v1` browser database and all world IDs. These names identify the profile, not the payload version. Return source-schema/migration metadata; retain visible migration-dirty state until normal storage confirmation, without overwriting old bytes merely by loading.
- Verify focused quest transitions, prior/alternate homes, blocked shelter, full inventory, utility conservation, canonical schema 2 and exact real schema 1 preservation/refusals. Then one ordinary native/browser NPC → quest → craft/equip compass → save/restart journey, including keyboard/controller menu routing. Take one inhabited-town view after its actual visual change. G-B requires owner feedback solicitation; human acceptance must remain honest and separate from engineering checks.

### Warm LEGO-style cast and interface integration checkpoint (2026-09-13 UTC)

The owner clarified **“we need lego characters.”** CHAR-B01 means warm toy minifigure people, including the player and all three residents. The organic-human proportions in the first warm concept are superseded by the [revised minifigure village concept and handoff](docs/design/warm-adventure-r02/README.md). The current authored character uses a yellow cylindrical smiling head, molded hair, a compact trapezoid torso, short block legs and open curved toy hands. Its installed technical path/identity remains `data/adventure/human-r01` / `voxys-adventure-human-r01`; those names do not mean organic-human art. The existing `robot_*` node names are internal rig compatibility identifiers, not visible robot geometry.

- [x] Correct the warm village concept so its player and villagers visibly use LEGO-style minifigure anatomy; preserve the welcoming scene and compact UI, and retain the exact prompt and provenance in `docs/design/warm-adventure-r02`. This completes a design correction only. It does not complete CHAR-B01, WARM-B01, UI-B01 or a gate, and owner feedback remains open.

- [x] Record the explicit character correction in this plan and both active design briefs.
- [x] Add a separate strict adventure-character loader and runfile/WASM packaging while retaining the legacy robot loader's identity boundary. The focused installed-package test passes for all eight animation clips and rejects cross-loading character/robot identities.
- [x] Implement browser Explore/Build/focused-menu layout and create all fourteen browser part thumbnails from actual installed cooked mesh triangles, using the same named warm palette as the runtime. Reproduce with `python3 tools/adventure_assets/generate_piece_thumbnails.py`; source/output hashes are in `web/adventure_piece_manifest.json`. This does not complete integrated UI acceptance.
- [x] Verify 63 adventure CPU cases, five native layout cases, fifteen Cove HUD CPU regressions and seven native storage cases. See `docs/validation/adventure/UI-B01/native-layout-r01/README.md` and `docs/validation/adventure/TOWN-B01/full-terrain-r01/README.md` for exact source/evidence scopes. Strict runtime and browser executable builds pass after the character loader/input changes.
- [x] Author distinct resident appearances with editable sources and strict role-specific loading. Moss has a cap/apron, Rivet a rust vest/travel pack, and Lumen a brimmed keeper hat/coat. All nine authored LODs pass geometry and strict cooking checks. Runtime selects separate models with white tint. See [resident package, dimensions, measured budgets and proof](data/adventure/residents-r01/README.md). The player package remains byte-identical.
- [x] Implement the inhabited starting-area geometry: cottage, market shelter, gardens, trees and paths. All eight groups admit on the real full terrain: 44 catalog pieces, 22 props and 94 collision boxes. Actual player-controller paths through both entrances and NPC approaches pass. Conflicting old saved poses, buildings and doorway approaches defer whole scenery groups; an interior saved pose retains its floor. See [precise CPU scope](docs/validation/adventure/WARM-B01/village-r01/README.md) and [editable prop contract](data/adventure/village-props-r01/README.md). This is not final visual or full gameplay acceptance.
- [x] Replace native approximate icons with projected triangles from the same actual meshes as the browser. Eight CPU cases and one GPU case pass, including 454 sampled colors, capacity bounds and resource cleanup. Correct both native/browser graphics-header initialization; the browser executable and the subsequent native GPU case pass. See [native thumbnail evidence](docs/validation/adventure/UI-B01/native-mesh-thumbnails-r01/README.md).
- [ ] Complete ordinary native/browser gameplay with the new cast/interface: movement, building, doorway/stair traversal, full home quest and compass craft/equip/save/restart, plus controller and accessible menu checks. Keep CHAR-B01, WARM-B01, UI-B01 and G-B open until their full criteria pass. No new gate commit or owner visual acceptance is claimed by this checkpoint.

- [x] Bounded new browser integration: the actual LEGO-style figure and pictured part picker render together; ordinary conversation, quest acceptance, selection/focus return, confirmed save and real reload preserve the active quest. Twenty-three DOM behavior cases pass. This is partial evidence, not the full G-B gameplay/character gate. See [exact scope and remaining checks](docs/validation/adventure/CHAR-B01/browser-r01/README.md). Separate candidate: `http://127.0.0.1:42753/index.html?experience=adventure`; package `build-adventure-g-b/web-r02`, ID `adventure-1a49bcffb8919e9a`, server session 11522. The published G-A package at port42751 is unchanged.

- [x] Integrate the distinct cast and actual village in a new package, with native/browser builds and role-loader checks passing. Inspect one changed-scene view; use ordinary Moss dialogue, accept the quest, enter/leave Build by keyboard/UI, save and really reload the active objective. All fifteen packaged adventure payloads match installed bytes. [Exact scope](docs/validation/adventure/CHAR-B01/cast-village-r01/README.md). Latest candidate: `http://127.0.0.1:42754/index.html?experience=adventure`; package `build-adventure-g-b/web-r03`, ID `adventure-51440f5c75b42947`, server session 89077. Retained test world `f350305bf389f645d61236b25d4d2613`. Old previews/saves remain intact. Continue the unchecked ordinary gameplay criteria above; do not repeat this screenshot or claim the full quest/native journey passed.

### Building controls and village consistency checkpoint (2026-09-13 UTC)

This supersedes the **development preview** address/package handoff above; prior
evidence stays attached to the package actually exercised. Published G-A/main
remains `2d6d0bc154169fc8d73d744ad9392b9c481d0bfd`. G-B is still uncommitted and open.

- [x] Expose **Starter room** beside Choose pieces in the B tray and as the first row in every catalog category. Display the real combined cost (70 wood / 16 stone / 8 scrap); selecting it only previews the room. Twenty-five DOM behavior cases pass. A real browser choice preserved stock and showed the bed-use hint.
- [x] Correct adventure camera-relative horizontal movement for the engine's left-handed rendered view. Two CPU cases cover 768 keyboard/controller directions through the actual router, orbit camera and view projection. [Movement evidence](docs/validation/adventure/UI-B01/movement-projection-r01/README.md). This is not physical held-key/controller acceptance.
- [x] Map logical pointer coordinates through the actual per-axis framebuffer scale for both world picking and native HUD hit testing. Preserve queued clicks when canvas input ownership is already unchanged. Four focused CPU cases and native/WASM builds pass. In the actual browser at 1280×720 CSS / 1920×1080 rendering, a center click maps to (960,540); a real click placed a foundation for four stone and Delete refunded four. [Exact evidence and limits](docs/validation/adventure/UI-B01/pointer-scaling-r01/README.md).
- [x] Make new village-adjacent construction use the same approach-clearance predicate as fresh scenery admission. A .20 m near-market proposal is refused; a .90 m proposal keeps all eight groups after fresh admission. An already-saved .20 m building retains priority and defers the market. Nine focused CPU cases plus native/WASM builds pass. [Evidence](docs/validation/adventure/WARM-B01/village-clearance-r01/README.md).
- [x] Package the updated source as `build-adventure-g-b/web-r06`, build ID `adventure-3d90959ee3ae652a`, and serve it at `http://127.0.0.1:42754/index.html?experience=adventure` (server session 22217). Save the retained CUA test world before replacing r05; a real reload shows **Saved adventure loaded**, the active quest, all three available residents, zero placed parts and restored 640/320/80 stock. No new screenshot was needed. The earlier r03/r05 packages remain historical evidence; their servers 89077/93250 are stopped.
- [x] Prepare independently source-reviewed ordinary-input browser/native home-quest drivers with fresh profiles/worlds, bounded movement, actual menu/placement/crafting inputs and save/reload assertions. [Browser recipe](docs/validation/adventure/G-B/browser-driver.md), [native recipe](docs/validation/adventure/G-B/native-driver.md). Syntax/source review is not gameplay evidence.
- [ ] Run those prepared full journeys after the pending explicit owner permission arrives. The current CUA instructions require specific authorization to use another UI driver; CUA cannot hold walking keys and its native surface is unavailable. Do not execute either driver merely by setting its acknowledgement flag. No alternative-driver permission has been received at this checkpoint.
- [ ] Complete remaining controller/accessibility, character and owner-feedback criteria before marking UI-B01, CHAR-B01, WARM-B01 or G-B done. The separate style-feedback question is pending; no visual approval is inferred.

Next agent: keep the r06 package and freshly built `bazel-bin/voxy_native` fixed
during any authorized journey. Use new evidence directories, one GPU application
check at a time, ordinary inputs and the prepared route west of the village.
Retain failed evidence and fix its actual cause before another run. Do not repeat
the already recorded static views. Commit through the normal hook only when a
stated gate's complete criteria pass; preserve all unrelated Cove work and saves.

### C navigation foundation checkpoint (2026-09-13 UTC)

Work continued on independent C engineering while the alternate UI-driver
permission remained pending. **The full G-B playthrough still has not run.**
The r06 playable package and preview above were not replaced for this checkpoint;
the following navigation code is not yet driving the installed residents/enemies.

- [x] Add `AdventureSpatialQueries::walkableFeet`: bounded, distinct supported foot heights with real terrain/stud and accepted solid clearance. It represents ground and bridge decks separately. More than 64 examined distinct supports or the caller's output capacity refuses the complete column; output remains unchanged. Nine focused cases pass.
- [x] Implement `layered_navigation.*`: .5 m horizontal samples, eight standing layers, 4 m tiles and at most 64 tiles in one local graph (maximum 32×32 m). Graphs borrow the existing immutable full terrain and accepted solids. Exact old/new solid differences invalidate affected tiles; unrelated tiles retain their generation and usable paths.
- [x] Bound column construction, endpoint connection and A* work across calls. Defaults are 16 columns and four endpoint checks/A* expansions combined; hard call caps are 64/32. A request itself performs no movement probes. Search caps at 4,096 expansions and a path at 512 waypoints; incomplete/overflowing geometry does not silently become a passable route.
- [x] Prove directed edges through the actual `AdventurePlayer` walking solver, then move the real actor with `NavigationFollower` one fixed step at a time. Terrain identity, query owner/revision, path graph/configuration/tile generations, and actor geometry/water policy must agree before movement. Source review found and fixed terrain-rebind and actor-binding mismatches, plus synchronous endpoint work outside the reported budget.
- [x] Verify nine navigation cases: actual bridge deck and ground-below traversal, eighteen catalog stair treads up/down, a changed wall stopping the old path before movement and producing a detour, removed/restored bridge decks, unrelated edit preservation, work/capacity bounds, foreign paths, terrain rebinding and actor policy mismatch. Eighteen existing spatial/player/construction cases also pass. Native focused library/tests and the WASM application target build pass. [Exact evidence and remaining integration](docs/validation/adventure/AI-A01/navigation-r01/README.md).
- [x] Record a terrain-grounded next exploration proposal: [meadow / eastern terrace / beacon loop](docs/design/adventure-route-r01.md). Preserve town (−63,−895) and beacon (−63,−975). The direct meadow is not a ravine. A real .64 m eastern terrace near (21.5,−923) offers a proposed three-Pier, six-stone step or 27 m contour detour. The approximately 400 m waypoint loop still needs exact production traversal/placement validation; terrain-grid sampling is not a gameplay pass.
- [x] Record the [bounded C progression/save contract](docs/design/adventure-progression-r01.md): discoveries, remaining quests, encounter generations, one-time loot, unique relay-core conservation, atomic outpost/relay repair and schema 1/2→3 preservation. Names and exact rewards are working choices. Current unrestricted health/recovery must become explicit combat transitions before enemies are integrated.
- [ ] Integrate the navigation graph/follower into real NPC/enemy activity with bounded request scheduling, correct live actor occupancy and region ownership. Prove displayed population/frame-time budgets and ordinary native/browser navigation around changed player buildings. AI-A01 remains open.
- [ ] Validate both proposed terrain obstacle solutions with the actual full-terrain controller and placement authority; author the useful discoveries/ruins and persistent progression. EXPLORE-A01, COMBAT-A01, OUTPOST-A01 and G-C remain open.

Next implementation must preserve the tested r06/G-B save contract while its
ordinary playthrough is pending. Before accepting schema-3 migration as complete,
retain a real completed G-B schema-2 save fixture. Independent schema-3 source
and synthetic migration checks may proceed in a separate candidate; keep r06
and its existing worlds intact. Both installed compatibility identities are frozen
in the progression note. Keep the local navigation graph shared/scheduled rather than
allocating a full map or an unbounded job per actor. No enemy, combat, complete
route, performance or owner-approval claim is made by this foundation checkpoint.


### C trail encounter implementation checkpoint (2026-09-13 UTC)

This source continues the warm LEGO-style direction. It does not complete G-B,
AI-A01, COMBAT-A01 or G-C. Existing r06/browser worlds and the pre-combat native
candidate are retained. The completed G-B quest journey and explicit alternate
UI-driver permission are still pending. No approval or ordinary fight is inferred
from component tests.

- [x] Author a separate original hooded LEGO-style raider with yellow toy face, block torso/legs, C-hands and wooden staff. Three cooked LODs, editable Blender/GLB sources, measured geometry, strict role loader and one corrected asset proof are retained in [the asset package](data/adventure/raider-r01/README.md). All 52 player/resident source and cooked files remain byte-identical. Runtime uses white tint and the existing rigid clips; finished attack animation remains open.
- [x] Install two grounded encounters at X/Z (−58,−945) and (−55,−967), with health 60/100, damage 12/18 and 12 m leashes. Both anchors pass actual full-terrain controller approach/exit checks. New actors use bounded fallback and yield to existing homes/player/recovery; saved positioned actors retain their exact pose or defer. Nine admission cases pass. The second actor deliberately remains avoidable from the direct trail; its future quest will point to the core.
- [x] Implement the fixed 60 Hz combat resolver: equipped staff, 12-tick player windup, 36-tick attack cooldown, 12-tick collision-tested dodge and 54-tick dodge cooldown; raiders notice/chase, freeze facing for 42-tick windup, strike once, recover 54 ticks and return within leash. Static walls block sight and hits. Eight resolver cases include real navigation/retreat and 1,201 accepted idle ticks plus an actual solver approach/impact through session authority.
- [x] Implement schema 3 authority for health, attack/dodge deadlines, positioned encounter poses/phases, immutable death and once-only loot receipts. Staff item 6 costs 4 wood / 4 scrap at a reachable bench; encounter 1 grants 8 scrap; encounter 2 grants the one RelayCore item 7. Full bags preserve entitlement; chest transfers conserve the core. Health zero disables ordinary commands until safe recovery. Eight authority cases, 72 existing adventure cases and 13 browser storage cases pass. Schema 3 reserves two discoveries/four later quests/relay fields but admits only unearned defaults; no exploration or relay completion is implemented by those reserved records.
- [x] Integrate real player/enemy movement and atomic actor collision publication with accepted fixed ticks. Two bounded 32 m navigation graphs use 16 columns / four endpoint or A* expansions each per tick; no full-map nav allocation. Static building packets invalidate changed tiles. Living return-home is refused near raiders; defeated recovery keeps homes/items and chooses safe bed/town. Add actual right-hand anchored wooden Beam staff and compact health/threat/Attack/Dodge/Return controls. Thirty DOM cases pass, including immediate game focus and cooldown/defeat gating.
- [x] Native executable and WASM target builds pass. Focused installed character/grip, native storage, player movement, navigation and combat targets pass. The native build exposed an X11 `Status` macro collision; navigation enums now use `SearchStatus`/`FollowStatus`. This is a naming fix, not a changed path algorithm.
- [x] Pass the real headless runtime startup/snapshot check: actual full terrain and GPU resources, 1,201 idle ticks, 90 synthetic walking frames, exact schema-3 snapshot/restore and resumed tick. Package `build-adventure-g-c/web-r01`, ID `adventure-162877127ade6230`, served at `http://127.0.0.1:42755/index.html?experience=adventure` (session 75898). CUA tab 5 reaches the live health/Moss/Attack/Dodge HUD; no screenshot was needed. This is component and startup evidence, not a played fight.
- [ ] Perform the authorized ordinary native/browser fight, build-around-enemy, pause/reload/defeat/recovery/loot journey. Complete the separate G-B journey and actual schema-2 migration fixture.
- [ ] Finish polished attack poses/audio, combat remapping migration, controller/accessibility acceptance, shared region/nav population scheduling and displayed frame-time budgets. Add the remaining trail quests/discoveries, physical obstacle alternatives and relay/outpost consequences before G-C.

Exact source scope, retained failures and results: [combat foundation evidence](docs/validation/adventure/COMBAT-A01/foundation-r01/README.md).

### Trail quests, discoveries and relay checkpoint (2026-09-13 UTC)

This extends the combat candidate. It does **not** complete G-B or G-C. The warm
LEGO-style cast remains the required direction; all player/resident/raider asset
bytes are unchanged by this work. Exact current scope and evidence live in
[the trail checkpoint](docs/validation/adventure/G-C/trail-r01/README.md).

- [x] Implement explicit quest 2–4 authority: Rivet checks currently equipped compass/staff after the first home quest; Lumen recognizes a genuine prior guardian-core claim; a separately accepted restoration quest completes only at the physical relay. Core consumption and permanent relay/quest receipts publish together. Six focused authority cases pass, including forged/stale/duplicate requests and removal after completion.
- [x] Extend the save writer to schema 4 with the existing schema-3 payload layout; accept frozen schemas 1–4. Preserve schema-3 active combat, health, homes and core-in-chest ownership exactly. Retain the actual old-code 1,542-byte component fixture at `tests/fixtures/adventure/schema3-component-r01.bin`; this is not a played journey. Fourteen browser storage cases pass, including failed schema-4 publication preserving the old confirmed world and later successful migration in the same namespace.
- [x] Author two useful material discoveries, three appended supply piles and four bounded scenery groups/23 actual catalog pieces. Signal Terrace is (30,−923); Survey Overlook is (22,−1031). Discovery rewards are 4 scrap and 12 stone, once each, with separate arrival/claim receipts and full-bag preservation. Resource IDs 1–18, old terrain/town/relay/NPC/encounter anchors remain exact; new supplies are IDs 19–21. Saved homes, access and poses take priority over whole authored groups.
- [x] Prove the real terrain construction choice. The old proposed X=21.5 face was already walkable and its step failed. At X=29.5/Z=−923, the 0.96 m face blocks ordinary walking. Three paid Piers cost 6 stone and support actual ascent/descent; the 37 m contour detour works in both directions. Signal Terrace is on the reached high side. Five focused CPU cases pass with the actual full terrain, village, residents and markers; see [exact route evidence](docs/validation/adventure/EXPLORE-A01/trail-sites-r01/README.md). This does not prove the complete roughly 407 m adventure loop or ordinary UI placement.
- [x] Add shared trail dialogue, current-objective tracker, journal and five semantic compass destinations. Accept/turn-in remains physically bound to the right NPC; journal rows only select information or track destinations. Watch Arch remains hidden until real relay activation. Review fixes make the journal open the current quest, show discovery reward quantities and return walking focus after a compass change. Seven presentation and 33 browser DOM cases pass. These checks do not certify layout, controller or human usability.
- [x] Verify field-home furniture access and final native/WASM runtime/package integration. A qualifying owned field home needs a usable sheltered bed, chest and workbench within 25 m of the relay and beyond 40 m from town. The town bed can remain registered. Five physical-access cases pass: paid starter/annex, actual doorway walking, sealed furniture, foreign ownership, stale geometry, shared limits and unchanged state. Both furniture kinds use the same bed area. Cache the result across inventory/quest/combat changes; recompute for changed structures, restore and static scenery publication. The final headless runtime passes 1,201 idle ticks, 90 synthetic walking frames, all four installed sites and exact schema-4 snapshot/restore. Native and WASM builds pass.
- [ ] Complete ordinary native/browser quest/fight/build/discovery/relay/save/reload journeys, two home layouts, controller/accessibility checks and the actual completed G-B schema-2 fixture. The pending alternative-UI-driver permission has not been answered; do not infer it from elapsed time or a test acknowledgement flag.
- [x] Implement the planned useful side-quest reward: Moss's optional survey quest now unlocks the paid Wide stone step recipe. The [side-quest checkpoint](docs/validation/adventure/G-C/side-quest-r01/README.md) records explicit acceptance/arrival/turn-in authority, exact six-stone payment, durable unlock and full-terrain recipe traversal. This completes the implementation item, not ordinary opening acceptance. Keep broader AI/population scheduling, attack/audio polish, displayed performance, human feedback and G-B/G-C acceptance open.

Current trail candidate: **http://127.0.0.1:42756/index.html?experience=adventure**,
package `build-adventure-g-c/web-r02`, ID `adventure-1118f4f0b2c23938`,
server session 55630, CUA tab 6. Its live starting HUD reaches Moss, health and
play controls; no screenshot or ordinary full journey was taken. All 46 package
file hashes match the recorded manifest. The matching native executable is
preserved at `build-adventure-g-c/native-r02/voxy_native`. Schema-4 content
identity is `0ca6be8bbc4285f4541aadc74d8cb7aeefb4b2ed078f45f86e7b0a2173ab1e93`.

Keep the earlier schema-3 candidate (`web-r01`, port 42755, `native-r01`) and
G-B schema-2 candidate (`build-adventure-g-b/web-r06`, port 42754,
`build-adventure-g-b/native-r06/voxy_native`) for their respective migration and
ordinary-play evidence. Published G-A/main remains `2d6d0bc154169fc8d73d744ad9392b9c481d0bfd`.
G-B/C source is still uncommitted; no new complete gate or hook result is claimed.
The historical route record distinguishes its first rejected proposal from the
final passing implementation. See the trail checkpoint's `results.json` for
exact source hashes, checks, package identity and remaining criteria.


### Optional scouting quest and paid blueprint checkpoint (2026-09-13 UTC)

The required warm LEGO-style cast and full landscape are unchanged. This
checkpoint supplies the missing optional quest in the opening. It does not
complete G-B/G-C or certify ordinary controls, visual quality or human play.
See [the self-contained implementation and evidence](docs/validation/adventure/G-C/side-quest-r01/README.md).

- [x] Add quest 5, **The Surveyor's Notes**, from Moss after the completed first-home quest. Both actual discovery arrival receipts count, including prior visits; supply claims are optional. Explicit acceptance and physical turn-in are required. Completion grants the permanent recipe entitlement with no free stock or placed structure. Five authority cases pass, including full inventory, duplicate/stale requests and persistence after home removal.
- [x] Add canonical **Wide stone step** blueprint metadata/layout and paid Session entry point. It places three existing Piers for 6 stone in one new owned structure at an arbitrary valid anchor/quarter turn. Existing manual Piers and the 19-piece starter room remain available. Four tests pass with no skips when supplied the actual full terrain, covering rotated layouts, paid construction/refund, atomic refusals and ascent/descent of the real Signal Terrace obstacle.
- [x] Extend the writer to schema 5 without changing payload layout; preserve frozen schemas 1–4. Capture the actual old-code schema-4 component fixture before changing the writer: 1,542 bytes, SHA `8493c01ec604f1bdd46f3fbcfd2328cc54560ba773b0816980b9320cf4b2da85`. It contains the old home, gear, health 47, active combat, completed main chain/discoveries and consumed relay core; quest5 is unearned. This is a component fixture, not a completed player journey. Fifteen browser storage cases pass.
- [x] Integrate optional journal/Moss dialogue and semantic recipe menus. The main tracker keeps its priorities. Browser preview/menu labels show three Piers and six stone without room/bed hints; 36 DOM and 10 presentation cases pass. Menu, direct piece choice and shoulder cycling leave blueprint mode when selecting individual pieces.
- [x] Pass final native/WASM builds and real headless runtime integration: actual terrain/GPU assets, 1,201 idle ticks, 90 synthetic walking frames, exact schema-5 snapshot/restore, locked/earned recipe menu, optional journal, rotation, synthetic shoulder cycling and stale/queued choice clearing on restore. Restore now clears the previous blueprint, undo target and pending menu actions. The 5.240-second component check creates no window or screenshot and does not certify physical controller input.
- [ ] Complete the ordinary G-B/G-C play, reload, two-house-layout, controller/accessibility and owner feedback criteria. The pending alternate-driver authorization remains unanswered. Keep the successful-gate commit requirement; this partial checkpoint does not justify a gate commit or a main publication.

Current candidate: **http://127.0.0.1:42757/index.html?experience=adventure**,
`build-adventure-g-c/web-r03`, build ID `adventure-1b8f516c77d8d6d1`;
server session 29221, CUA tab 7. One startup observation reaches the live
Moss/health/Use/Build/Bag/Journal HUD without a screenshot or played journey.
All 46 package-manifest entries were hash-verified. Matching native executable:
`build-adventure-g-c/native-r03/voxy_native`, SHA
`9a2a8cabba8f210e76da9db3a4f502e8b6cff9e9ecc6a6bdfb1e6e8d108defc2`.
Schema5 content identity is
`b989f53fd82527a42f3e60582f9bb0cdcafced9cfea94b53074cccf37eca290c`.
The prior schema-4 candidate at 42756/web-r02/native-r02 remains intact, as do
schema-3 at 42755 and G-B/schema-2 at 42754. Main remains the G-A commit recorded
above. No successful G-B/C gate or new commit is claimed.

### ACCESS-A01 controls and comfort checkpoint

- [x] Add the separate bounded Adventure preference profile, Linux persistence and browser transport. Text size, contrast, motion, look speed/inversion, hold/toggle and deadzones survive a confirmed device write without changing paused world state or archive bytes. Existing Cove preferences and Adventure schema 5/content identity remain intact.
- [x] Route Attack/Dodge through configurable semantic bindings with reserved-control/conflict refusals, release-to-rearm after focus/menu/rebind/reconnect, short key/mouse taps and current visible labels. Apply mouse inversion to both held and completed drags.
- [x] Integrate shared native/browser preference and binding menus, immutable choices, explicit reset/retry and honest storage status. Nine core, six Linux store and twelve Cove-input cases pass; the expanded real-terrain headless runtime case passes. Native/WASM builds pass. Eleven browser preference and forty UI cases pass, including the production-shaped snapshot regression for the readiness defect found during real browser use.
- [x] Verify ordinary browser preference changes and fresh-instance restore: set text125%, rebind Dodge Q→F, observe confirmed storage, then open a fresh same-origin game and observe Dodge F and text125%. No state injection, world-save operation or screenshot. The first tab's reload command did not establish a new instance and is not counted as reload evidence.
- [ ] Complete remaining ordinary native/browser gameplay, physical-controller and human accessibility/visual acceptance before closing ACCESS-A01, UI-B01, CHAR-B01, WARM-B01, G-B or G-C. The partial checks above do not complete those gates.

[Exact source, checks and limits](docs/validation/adventure/ACCESS-A01/controls-r01/README.md).
Retained controls candidate: `http://127.0.0.1:42759/index.html?experience=adventure&telemetry=0`,
`build-adventure-g-c/web-r05`, ID `adventure-8dda99b275e01b17`, server72611,
browser tab10. Native companion: `build-adventure-g-c/native-r04/voxy_native`,
SHA-256 `a99c6db70194e67871950e78f45242cfe9e5bb1681f2552fb3232d02dde92b6a`.
The first controls package r04/port42758 remains a retained failed browser
integration candidate; earlier world packages and all main/Cove saves remain
intact. Main remains the recorded G-A commit; no new gate commit is claimed.

### ACCESS-A01 optional short guide checkpoint

- [x] Add six shared short tips covering movement, building, home, quests, combat and saving. Use the current device, look mode and combat bindings. Reading help pauses play, grants nothing and changes no tutorial or save schema state.
- [x] Add Menu → How to play and Building help, with Next/Previous/All topics and safe return to Pause, Explore or the retained build setup. Reject stale/duplicate choices and clear queued guide actions on restore. Consume the modal-dismissal input frame so closing help cannot also change the selected building piece.
- [x] Keep complete native guide text and navigation readable at the tested 100/125/150% sizes, and preserve all eight Build controls. Pass 75 core, 43 browser DOM, 10 native CPU layout cases and the expanded full-terrain headless runtime case. Final native/WASM builds pass; runtime checks include exact paused state/archive invariance and the Escape/shoulder/click dismissal regression.
- [x] Check the final browser package through ordinary controls: Starter room → Building help → Home tip → Return to building retains selection, cost, supplies and canvas focus. Pause → How to play → Saving → Back to menu returns to Pause. No placement, world-save operation, injected state or screenshot was used.
- [ ] Complete ordinary native gameplay, physical-controller and human onboarding/accessibility acceptance before closing ACCESS-A01 or broader G-B/G-C gates. These guide checks do not substitute for those criteria.

[Exact evidence and limits](docs/validation/adventure/ACCESS-A01/guide-r01/README.md).
Current candidate: `http://127.0.0.1:42761/index.html?experience=adventure&telemetry=0`,
`build-adventure-g-c/web-r07`, ID `adventure-0a43174f2b40d3c1`, server 34181,
browser tab 12. Native companion: `build-adventure-g-c/native-r06/voxy_native`,
SHA-256 `18923a8f2324813abf8014deceaa8fce9a4e5d63699f11ed6b6c5a4048a659af`.
All 47 package-manifest files match. Schema 5/content identity remain unchanged.
Pre-fix web-r06 and all prior candidates remain historical; existing player saves
are preserved. No full gate or new commit is claimed.
