# First trail encounter candidate

This is an implementation checkpoint, **not a completed gameplay gate**. G-B's full home/quest journey, CHAR-B01, UI-B01, AI-A01, COMBAT-A01 and G-C remain open. The existing warm LEGO-style player, three villagers, village and homes are preserved. This change adds the first playable combat implementation and a separate preview.

Candidate: [local adventure](http://127.0.0.1:42755/index.html?experience=adventure), package `build-adventure-g-c/web-r01`, build ID `adventure-162877127ade6230`, server session 75898. The earlier G-B/r06 preview remains at 42754. The published G-A/main at 42751 is unchanged. A new origin has separate browser storage; it does not silently transfer an earlier world's progress. The pre-combat native executable is retained at `build-adventure-g-b/native-r06/voxy_native` for the pending G-B driver.

## What the player can try

1. Build a home and use its workbench. **Trail staff** costs four wood and four scrap; crafting equips it. The bag can switch between staff and field hammer.
2. Head north from the village toward the beacon. Original hooded LEGO-style raiders occupy X/Z (−58,−945) and (−55,−967). The second is avoidable from the direct trail; the future core quest will provide explicit guidance.
3. Click or use **Attack** to swing. **Q / Dodge** gives a short movement burst through the real collision solver. On a controller, right shoulder attacks and Back dodges outside building/menus. These initial new bindings are not yet integrated into the preference-remapping migration.
4. A raider raises its staff before striking. Its facing freezes; step aside or dodge. Three small ground bricks and a text cue indicate the windup. Walls block sight and hits. Retreat beyond the leash to disengage.
5. Approach defeated-raider loot and press **E / Use**. The first grants eight scrap; the second grants one RelayCore. Full inventory leaves the loot unclaimed. The core currently has no repair action: relay/outpost progression is still planned.
6. Health zero disables ordinary commands. **Return home** retains possessions and recovers at a safe usable bed or town. Living return-home is refused near raiders. Save manually to preserve the current checkpoint.

The normal home quest, furniture, building and compass features remain. Existing gates still need their ordinary-control journeys.

## Authority and collision

`adventure_combat.*` resolves one accepted tick at 60 Hz. Player staff damage is 25, windup 12 ticks, attack readiness 36 ticks. Dodge lasts 12 ticks at twice normal walking speed, with 54-tick readiness and persisted direction/invulnerability deadlines. The real capsule solver handles steps, support and blocked movement; animation cannot move an actor or grant damage.

Both encounters use generation 1, notice 7 m, leash 12 m, reach 1.6 m, windup 42 ticks and recovery 54 ticks. Health/damage are 60/12 and 100/18. States are Dormant, Idle/Patrol, Notice, Chase, Windup, Attack, Recover, Return and Dead. Each impact has one serial/window. Dead checkpoints cannot revive or move; session authority creates permanent death/loot receipts. Returning actors may heal only from prior Return to Return/Idle at their installed maximum.

The runtime keeps accepted static geometry in `walkQueries_`: terrain, buildings, village and friendly NPCs. Two bounded 32×32 m graphs borrow that geometry. Each receives at most 16 sampled columns and four endpoint checks/A* expansions per tick. Changed solid tiles invalidate affected paths. This fixed two-encounter allocation is an opening bound, not the future eight-hostile/region pooling solution.

`queries_` adds the currently admitted alive raider bodies for player movement, camera and picking. Enemy proposals use the same static `AdventurePlayer` solver plus conservative actor separation. A combat candidate and its next collider packet prepare before session publication. Failed preparation restores the player's prior controller and resets derived paths. Building changes cannot silently remove an active actor to make a conflicting placement succeed.

New actors use nine bounded fallback offsets and actual walking-footprint checks; existing saved player/home/recovery has priority. Positioned checkpoints retain their exact pose or defer. Fresh actors cannot appear inside old homes. An unavailable actor neither moves nor attacks. Deferred and dead state is not an excuse to reissue loot.

Rest validates a safe source; recovery validates the destination before moving/healing. Live voluntary recovery refuses nearby enemies but remains available for an otherwise stranded player away from threats. Defeated recovery permits an unsafe source. No movement method can set health arbitrarily.

## Assets and input

The [raider package](../../../../../data/adventure/raider-r01/README.md) contains original Blender/GLB sources, three cooked LODs, geometry checks, strict identity and a corrected actual asset proof. Its existing eight rigid clips supply an initial tool gesture. Final windup/impact animation, sound, weapon visual sweeps and LOD switching are still open. Runtime uses asset index 6 and white tint; existing 52 player/resident files were rehashed unchanged.

The player's staff uses the existing wooden Beam through `trailStaffModel()`, attached to the actual right-hand anchor. Its 0.8 m length and 4.48 cm square cross-section fit the C-hand opening through all eight clips, verified against real vertices. The staff is cosmetic; the declared attack arc supplies gameplay damage.

Native and browser provide health, threat text, Attack, Dodge and recovery. DOM Attack/Dodge/Use return input to the game immediately; a later accepted dialogue focuses its menu. Native information-panel clicks do not also swing or place a piece. This does not certify final layout/accessibility or physical controller behavior.

## Save compatibility

Schema 3 retains the schema 1 prefix and 13-byte schema 2 extension, adding 298 bytes of typed combat and reserved trail records. Source-schema-specific frozen identities admit exactly 1/2/3; old item bounds and all old fields are validated before migration. The actual 1,231-byte schema 1 home remains byte-identical in the fixture. Schema2 migration has synthetic coverage; the real completed G-B fixture is still required for acceptance.

Player pose/health, attack/dodge deadlines, enemy pose/phase/health/serials, death and claim receipts publish with inventory/homes. The one core must remain conserved across backpack/chests. Discovery, later quest and relay records currently accept only their unearned defaults; no unfinished progress can be smuggled in by a save. Native `adventure-v1`, browser `voxys-adventure-v1`, world identities and manual confirmation semantics stay stable. Loading alone does not rewrite the old archive.

## Actual evidence

| Check | Result | Scope |
| --- | --- | --- |
| Combat/session authority |8 cases pass|Atomic staff, damage, defeat, recovery, unique loot and schema admission. |
| Existing adventure authority/geometry |72 cases pass|Existing home/quest/storage/component rules. |
| Encounter admission |9 cases pass|Includes exact 8192² terrain and four-direction controller approaches at both installed anchors. |
| Combat resolver plus session boundary |8 cases pass|Windup, sidestep, one impact, walls, dodge collision, death, navigation/retreat,1,201 idle ticks and a real solver approach/impact accepted through session commands. |
| Layered navigation |9 cases pass|Bridge deck/ground, stairs, edited routes and bounded work; enum rename rebuilt. |
| Camera-relative movement |2 cases pass|Existing actual router/projection cases. |
| Character/prop loader |9 cases pass|Strict friendly/enemy identity separation and actual staff-to-hand geometry across all clips. |
| Native durable storage |7 cases pass|Separate storage/restart/confirmation and legacy preservation. |
| Browser storage |13 cases pass|Strict bootstrap and schema 3 confirmation failure/retry. |
| DOM controls |30 cases pass|Weapon/cooldown/defeat gating, focus, duplicate dispatch and existing menu/build behavior. |
| Real headless runtime |1 case passes, 5.065 s|Actual full terrain, GPU asset/HUD initialization,1,201 idle updates,90 synthetic walking frames, exact canonical snapshot/decode/restore and resumed tick. AMD Radeon 890M, RADV/Vulkan. No window/surface, screenshot or rendered frame. |
| Native + WASM applications |Builds pass|Final executables include native HUD click guard. |
| New browser startup |CUA tab 5 reaches active adventure HUD|Health 100/100, nearby Moss, correctly disabled unequipped Attack, Dodge and existing actions. No movement/fight/save acceptance is claimed. |

The headless runtime test ran before the final native information-panel click guard; later builds include that isolated guard. Focused result/source hashes are in [results.json](results.json) and [integration-results.json](integration-results.json); compressed exact logs/XML are under `checks/`. The live CUA startup is retained in `browser-startup.json`. No screenshots were needed.

The headless test is explicitly **component integration with synthetic library input**. It is not the pending ordinary native/browser player journey. The current CUA rules require explicit owner permission to use the prepared alternate UI drivers; that permission has not arrived. Neither acknowledgement flags nor elapsed time authorize them.

Retained corrections: source review caught an initial duplicate query revision and an idle phase counter exceeding the authority bound; both were fixed before successful runtime testing. The native build found the X11 `Status` macro collision and one formatting warning. New asset tests needed explicit numeric conversions and distinct trace lines. Those compiler failures are not test passes. The asset's first proof exposed hood/face/strap defects; one corrected proof is retained. No repeated unchanged-view matrix was performed.

## Remaining gate work

Ordinary native/browser combat, changed-building navigation, pause/windup reload, defeat/recovery, once-only pickup and the completed G-B migration fixture remain open. Finish the longer route, two physical obstacle solutions, discoveries, remaining quests and outpost/relay consequences. Also finish attack animation/audio, remapping/controller/accessibility, region/population scheduling, moving-scene frame-time budgets and actual owner/newcomer play. No gate commit or visual approval is claimed.
