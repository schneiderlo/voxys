# Voxys: a building-driven adventure

Design proposal, 25 September 2026; implementation subsequently approved by the owner. See [the implementation record](IMPLEMENTATION.md) for actual scope and validation. The proposal is based on a read-only code investigation of `45eada5be4ca89d6b2b55dcf192b6352623e5083` plus the existing working tree. The original proposal preceded gameplay changes.

**New owner direction:** “Adventure: explore, gather, fight and unlock progress through building.” This explicitly supersedes the earlier cancellation of adventure. The old adventure roadmap is evidence and reusable work, not an instruction to repeat its implementation sequence.

**Recommendation:** make a warm, single-player frontier adventure in which the player builds the routes, cover and outposts that let them explore farther. Prove one satisfying region before expanding across the landscape.

Companion: [code investigation and evidence](AUDIT.md). The root `AGENTS.md` is empty; all 860 lines of `README.md` and all 733 lines of the existing active plan were read. The current HUD was checked against newer source and September 24 evidence because those root documents are partly stale.

## 1. What must change

Voxys already has a landscape, a recognizable figure, construction, a bike, a cannon, forests, imported buildings and substantial engine work. The player has few reasons to connect these things. The older adventure adds reasons, but they are a short sequence of fixed supplies, two enemies and quest flags.

Adding ten villages or generating a larger forest would reproduce that problem at a larger scale. The missing feature is a chain of consequences:

**I explored → found something I wanted → gathered what I needed → built my own solution → used it → changed what I could do next.**

Hand-authored places are useful. Predetermined solutions are the problem. A carefully placed ravine can accept a bridge, stairs or a longer route. Randomly scattered scenery can still be empty.

Keep the current toy character, warm materials, main landscape, walking/swimming and shared HUD. Preserve creative mode as an unrestricted workshop with separate saves. Adventure gets its own versioned profile and progression rules.

## 2. The game promise

**Explore a broken frontier. Build your way through it. Make places worth returning to.**

The player begins near a small inhabited workshop settlement. A damaged ridge beacon is visible from the opening area. Restoring it introduces the game's central relationships: finding resources, reaching difficult ground, responding to danger and establishing a useful camp.

Buildings must provide practical advantages:

| What the player builds | What it changes in play |
| --- | --- |
| Bridge, stairs, platform | Opens a real walking route, a shortcut or access to a resource ledge |
| Wall, corner, gate | Changes sight lines, melee access and enemy pursuit |
| Sheltered bed | Establishes a reachable recovery point farther into the region |
| Chest | Stores owned supplies between expeditions |
| Workbench | Converts found materials into equipment and useful construction capabilities |
| Outpost near a restored site | Makes future trips shorter and supports the next expedition |

A house is successful because it works, not because it matches a prescribed blueprint. A bridge is successful because the player crosses it, not because six specific parts occupy six marked sockets.

```mermaid
flowchart LR
    A[See a place worth reaching] --> B[Explore and gather]
    B --> C[Build a route, cover or camp]
    C --> D[Use it to reach or survive]
    D --> E[Recover a new capability]
    E --> F[Improve home or explore farther]
    F --> A
    D -->|A solution fails| C
```

Failure should encourage revision. Keep the house, equipment and earned progression after defeat. Return to a valid camp or the settlement. Avoid hunger, thirst, durability chores and punitive corpse runs in the first release.

## 3. First playable region: the ridge beacon

Build a dense route within the existing full landscape. Select its exact position by surveying real terrain, walking times and buildable shelves at the approved character scale. Do not assume the old adventure coordinates or scenic creative spawn already provide a good level.

The topology is a loop with alternatives, not a straight quest corridor:

- Settlement and a choice of home sites.
- Nearby wood, stone and salvage sources, visible and safe enough for learning.
- A ledge/cache reached by a player-built crossing or steps; a slower natural route remains available.
- A small hostile site approached from the path, a flanking route or player-built cover.
- A ridge shelf suitable for a personal outpost.
- The beacon, which becomes a persistent landmark and reveals an adjacent expedition.
- A return shortcut the player can create and continue using.

Suggested first-session pacing is a playtest target, not an estimate of current content:

| Time | Player experience | Proof of a real system |
| --- | --- | --- |
| 0–3 minutes | See the beacon and immediate obstacle; move and make a useful placement | Clear purpose; responsive construction without a long tutorial |
| 3–8 | Gather a few sources and choose a route to the ledge | Harvesting changes the world; multiple constructions can solve access |
| 8–15 | Fight a readable threat, retreat or change the approach | Walls and paths affect actual sight, collision and navigation |
| 15–23 | Build and use a ridge camp | Shelter, storage, crafting and recovery work together |
| 23–30 | Restore the beacon; gain a capability; save and revisit | Visible world change and a reason to continue |

Recognize work completed before talking to an NPC. Allow early exploration. Optional instruction can suggest a solution without placing it automatically. Restoration can use either a recovered component or a more expensive crafted substitute, so avoiding an encounter remains a legitimate choice.

Make one useful construction essential: final beacon assembly happens at a player-built field workbench with reachable working space and shelter on the ridge. Accept different camp shapes and placements within that usable area. A bed and chest add recovery and storage benefits, rather than becoming arbitrary furniture-count requirements. Players can bypass individual fights or crossings, but cannot bypass the game's central building activity entirely.

**First reward: the quarry hammer.** Completing the expedition unlocks this tool and provides its first craft's ingredients. It harvests previously unworkable dense-stone deposits into cut stone for advanced station/structure recipes. Put a small deposit beside the beacon and an optional broken stair route nearby: the player immediately tries the tool, crafts a wide stone stair module and improves the return route. The deposit's state, output and recipe unlock persist. This is a proposed new capability, not the existing hammer's yield multiplier. Ordinary steps and alternative return paths already work; the reward expands what the player can gather and assemble. Freeze this choice before final M1 level composition.

**Minimum scope:** one settlement with one useful resident, one region loop, three raw material families plus the reward's refined cut stone, one well-tuned melee enemy, camp functions, one traversal challenge, the quarry-hammer upgrade and a visible next destination. Bring the other two existing NPC roles into M3 if they serve useful interactions. Add a ranged enemy only after the melee loop and geometry interaction work. Its distinct role is to make cover and positioning useful; a recolour is insufficient.

Do not promise a full campaign on this milestone. Its purpose is to prove that the game's repeated activity is enjoyable.

### From the slice to a complete small game

Proposed first-release scope: three connected, deliberately populated regions and a clear final restoration project, with a roughly 3–5 hour first journey as a pacing target to validate. The ridge teaches access and shelter; a woodland quarry tests harvesting and fighting around construction; a flooded causeway tests raised routes and expedition staging. Exact sites depend on the terrain survey. Avoid adding boats as a hidden requirement for this release.

Each region adds a capability that is useful immediately and improves an earlier place. The home grows into a convenient expedition base through storage, stations and saved reusable designs. Optional caches, alternate routes and encounter approaches provide reasons to return. After the final project, retain the world for free construction and optional expeditions.

Replayability comes first from different builds, approaches and home locations. Later, validated seeded resource/site variants and explicitly timed resource renewal can vary expeditions; they must preserve the player's construction and cleared milestone sites. A finite campaign with an ending is the initial product, not a promise of endless generated quests. The full-release gate includes playing the connected campaign through its ending and returning to earlier creations, beyond the M2 opening proof.

## 4. Systems that create lasting play

### Gathering and crafting

Replace anonymous supply piles with selected harvestable trees, rocks and ruin objects. Decorative distant forest remains cheap scenery. Interactive sources have stable identity, readable tool requirements, bounded yields and persistent state. Show whether an object can be harvested; do not imply every tree is interactive if it is not.

Use three starting materials: wood for light structures, stone for supports/shelter, salvage for equipment and mechanisms. Give enough starter supplies for a first useful construction; rebalance the old grant of 640 wood, 320 stone and 80 scrap, which largely bypasses opening gathering. Target short purposeful gathering trips, not repeated clicking to fill a timer.

Recipes declare ingredients, station and capability requirements as installed data. Outputs and input consumption commit atomically. No random rare drop should block the opening. Distinct resources should invite different routes or choices rather than simply increasing required quantities.

Initially, harvested sources remain depleted until an explicit in-world regeneration rule exists. Add bounded regeneration later, using saved simulation time; traveling out of range or reloading must never refill them. Reserve enough safely accessible material of each required type for completion after plausible failed experiments. For the slice, dismantling intact player construction returns its exact paid inputs, subject to inventory capacity; defeat retains inventory. Reusable basic equipment avoids consuming the last required building supplies. Validate this economy against alternate paths and wasted placements, not only the intended walkthrough.

### Building and editing

Before demanding complex constructions, deliver stable snapping, connection/height cues, continuous placement, pick-existing, repaint, selection/move and multi-step undo/redo. Copy/group blueprints follow after the basic edit transaction is reliable. Undo compensates an edit; it must not rewind the backpack, a chest's contents or combat rewards. Inverse commands revalidate present ownership, geometry, component state and inventory. Restoring a removed piece consumes its recorded refunded inputs; refuse with a reason if those supplies were spent or restoration is blocked. Retain history on refusal. Do not silently discard chest contents to make an inverse succeed.

Use a small coherent, correctly scaled kit: foundations, piers, steps/ramps, floor/bridge decks, walls/openings, roof pieces, gates/doors, a few useful brick/plate shapes, bed, chest and bench. Freeze legacy piece dimensions. Introduce new versioned IDs for pieces that fit the current 4.76-stud character and its camera. Do not sell the existing 2.24-unit door opening as usable by that figure.

During the first slice, structures remain static and support-validated. Full structural collapse is not required for bridges, cover and shelter to matter. Keep build changes responsive under combat: no pausing the world from the palette, no placement inside an actor, no instant mass-blueprint placement beside enemies, and no refunds duplicating materials. Use a small placement cadence/cost policy, tuned in play, instead of forbidding all tactical building.

Non-pausing tactical controls are new work: today every runtime menu pauses simulation. Separate quickbar/catalogue/colour interaction from the dedicated Pause menu. The former must keep combat live; the latter must consistently stop gameplay clocks and resume with neutral input. Verify this in M2, including keyboard focus and browser focus loss.

### Combat and inhabitants

Reuse fixed-tick combat, windup, dodge, line-of-sight checks and layered navigation. Rescale bodies, reach, step heights, sight origins and attack poses together. Add clear hit reactions and audio before multiplying enemy types.

Enemies replan after relevant construction changes. If a target becomes unreachable, they reposition, guard or disengage; they do not walk through the wall, teleport to the player or attack through solid cover. Pausing/saving cannot reset health or duplicate loot. Building can provide an advantage without guaranteeing a reward from a permanently trapped enemy.

NPCs need a small set of legible activities and useful services, not a full settlement economy at first. Begin with one resident who frames the expedition and provides workshop guidance. In M3, give the workshop, supply yard and beacon keeper visible roles and simple local movement. Their routes use the same collision rules. Avoid crowds until a few inhabitants behave credibly.

### Progression and world change

Reward capabilities: a better harvesting tool, a new structural family, a ranged option, a more useful camp station, then transport or machinery. Preserve basic expressive building and colours from the start.

Persistent site states should change play as well as dialogue: cleared access, working beacon, opened travel route, available station or safer staging point. A restored beacon lights the ridge and reveals a real next site; it does not merely increment a quest counter.

Later regions combine proven systems: a wet lowland where raised paths matter; a ruin where cover and elevation matter; a coast where transport becomes useful. Keep the fixed terrain and avoid a voxel excavation rewrite. Introduce seeded resource/encounter variation only after one carefully composed region is fun and reliably solvable.

## 5. Technical implementation

Reuse the C++20/WebGPU engine. No engine rewrite, generic ECS project or new scripting language is needed for the slice.

| Work area | Concrete change |
| --- | --- |
| Runtime/profile | Separate economy, character measurements, presentation, pause policy and available activities from `freeBuild_`. Add a new installed adventure profile; do not flip old saves into it. |
| Content | Replace additions to fixed encounter/quest arrays with bounded installed definitions: pieces, recipes, resources, enemy archetypes and sites. Keep existing legacy adapters. |
| World state | Introduce stable entity/site/cell IDs. Separate deterministic base content from persistent changes: depletion, health/death, loot claims, site state and player structures. |
| Commands | Extend `AdventureSession`'s prepare/validate/commit contract. Use explicit revisions, ownership, capacity checks and once-only receipts for grants. |
| Spatial queries | Publish one accepted geometry revision to movement, camera, previews, interaction, AI and building-dependent objectives. Cache derived capability checks by relevant revisions. |
| Active simulation | Activate only a bounded neighborhood. Persist before releasing dirty state; pin active/overlapping structures and entities referenced by interactions or pending operations. Far structures/cells retain records without full AI/physics. |
| Saves | Version the new content/schema and namespace; retain confirmed durable writes, browser locks and recovery generations. Save stable records rather than GPU slots or render instance indices. |
| UI | Extend current shared C++ GPU HUD with health, contextual interaction, bag/crafting and one tracked objective; retain browser accessibility peers and menu intent validation. |

Proposed module boundaries within `src/game/adventure/`: `adventure_profile`, `world_entities`, `active_regions`, `resource_system`, `recipe_catalog`, `site_progress`, `build_history`. Names are suggestions. Each should own a concrete responsibility; avoid growing `Application` and `AdventureRuntime` with every new rule.

An entity identity derives from world identity plus an installed site/local ID, or seed + generator version + cell + local ID. A structure crossing cells has one durable owner and overlap references. Changing an authoring recipe must not silently reshuffle IDs in existing saves.

For the slice, implement bounded records and lifecycle with a few sites; do not build an infinite-world service. Before wider population, split hot combat state from cold world history: current combat prepares and validates a copy of the complete session every tick. Appending thousands of entities to that state is not a scaling plan.

The browser save transport can be reused, but the current 1 MiB archive limit cannot simply disappear. Begin with a measured bounded snapshot. If region growth requires chunking, commit a world manifest and changed cell generations atomically so a crash cannot mix old construction with new rewards. Keep native/browser serialization equivalent.

## 6. Ordered milestones and completion gates

These are outcome gates, not dates. Schedule estimates should follow the first measured integration slice.

| Milestone | Deliverable | Gate before expanding |
| --- | --- | --- |
| M0 — establish the baseline | Fresh browser run, exact build identity, first-region terrain survey, moving-frame/startup measurements, profile/scale contract | Reproducible launch and ordinary place/use/save/reopen on named hardware; document remaining failures |
| M1 — first useful construction | Modern figure/HUD in new adventure, scaled kit, reliable editing, resources/crafting, one functional traversal problem; minimal stable entity/site records | Three materially different solutions work through actual traversal; resources and edits survive reload; no item loss/duplication; an uninstructed player understands and uses the build |
| M2 — complete expedition | Tuned melee encounter, useful cover/navigation, working camp, beacon state and quarry-hammer reward/use, autosave checkpoints | Fresh player can gather → build → face danger → recover/restore → try the new capability → reload → continue without debug help; repeat the uninstructed playtest |
| M3 — make it worth replaying | Distinct ranged enemy, alternate approaches, useful NPC activity, optional sites, wider recipes, clear feedback | Small blind playtest shows comprehensible goals and voluntary revising/exploring; fix observed friction before more regions |
| M4 — expand the world | Active-cell lifecycle, data-authored regions, validated seeded variation, content tools and measured larger saves/builds | Two regions share rules; revisiting preserves edits/harvests/encounters; travel and memory stay within measured budgets |
| M5 — broaden the toy systems | Carefully scoped editable authentic assemblies, persistent local destruction, earned transport/machines, later co-op decision | Each addition strengthens the adventure loop and passes its own save/performance/ordinary-play checks |

M0 performance investigation and M1 content/authority work can run in parallel. The kit/character scale contract must precede final level composition, navigation tuning and animation. Save identity must precede durable new entities. M2 must not depend on whole-house destruction or multiplayer.

The first implementation package should be small: new-profile skeleton, one correctly scaled bridge/step family, one real harvestable source, an access challenge and save/reopen. It should prove **“I gathered this, built this, and can now reach that.”** This tests the game thesis before months of content production.

## 7. Player and technical acceptance

Required ordinary browser journeys for M1/M2, except the marked expansion check:

1. Start fresh, understand a nearby goal, gather and build without developer coaching.
2. Solve the first crossing in three different valid ways; no exact-blueprint detector.
3. Place cover and alter an enemy's sight/path without clipping or a permanently broken state.
4. Construct two distinct usable camp layouts; use bed, storage and crafting. Recover after defeat.
5. Earn the upgrade, save, close/reopen and continue. Revisit a harvested source and a defeated encounter; neither pays twice.
6. **M4 expansion gate:** cross activated/unloaded cell boundaries and return. Builds, contents, depletion, opened doors and site progress remain correct. M1/M2 use the bounded region snapshot and require leave/reopen persistence without depending on general streaming.
7. Simulate storage refusal and recover visibly. Never announce “saved” before durable confirmation.

Use existing session, migration, real-terrain, navigation and combat tests for changed invariants. Add property/sequence cases for edit/refund/loot interleaving and unload/reload. UI fixtures and synthetic runtime input are supporting evidence, not a replacement for ordinary play.

Begin informal uninstructed checks in M1 and repeat in M2. Proposed M3 feel gate: five fresh players; at least four can explain the next goal and make a useful construction without coaching. Observe whether they voluntarily improve a creation or pursue another site. Record confusion, retries and route choices, then let the owner play. This is a small formative test, not statistical proof of retention.

Publish exact hardware, browser, resolution, build hash, scene and frame-time percentiles. Suggested targets after M0: a 60 FPS mode on selected reference hardware, moving-frame p95 at most 20 ms and p99 at most 33 ms, with no recurrent gameplay stalls. Calibrate targets honestly against the actual browser baseline; current evidence does not establish them. Measure cold/warm startup separately, including download and shader compilation, and set an explicit startup budget after observing both. Keep GPU watchdogs enabled.

Use realistic slice workloads: representative 256/512/1,024-piece builds, 4/8/16 nearby actors and continuous gathering, editing, combat and travel. These are test points, not promises of supported counts. Inspect memory, save size and pending-work growth through a 30-minute session. Do not treat allocation ceilings or offscreen renderer FPS as gameplay performance.

## 8. Art, sound and tool use

Art should make gameplay readable: a harvestable source looks different from background foliage; a gap communicates its problem; enemies telegraph attacks; a restored site visibly changes. Make the opening view show an opportunity, danger or destination rather than an empty scenic overlook.

Use the established LDraw/Blender/cooking workflow for correct-scale parts and assemblies. Retain editable sources, source identities, provenance, collision and reviewed LODs. Imported showcase sets are not automatically editable player kits; support a reviewed subset before promising universal disassembly. Avoid multiplying large full-detail CAD buildings to fill the world.

Image generation is useful for a regional mood target, enemy silhouette studies or interface illustrations. Label such output as concept art. It does not prove a playable location, good collision or a completed game. Do not spend this first milestone on another set of polished imaginary screenshots.

Add feedback tied to real events: placement/removal, gathering progress/yield, footsteps, enemy windup/impact, crafting and beacon activation. Establish an audio event boundary and browser audio-unlock/preferences behavior; captions alone are not an audio implementation. Use short original or properly sourced sounds, readable visual equivalents and reduced-motion support.

The success criterion is a player wanting to improve a camp and take another expedition. More screenshots, more tests or more trees are not substitutes for that observation.
