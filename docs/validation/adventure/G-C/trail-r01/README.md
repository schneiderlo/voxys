# Trail quests and a useful field home

This is an implementation checkpoint for EXPLORE-A01 and OUTPOST-A01. It does
not complete G-B, G-C, visual approval, ordinary play, controller acceptance or
the full game. The warm LEGO-style player, Moss, Rivet, Lumen and two hooded
minifigure raiders retain their authored meshes and animation profiles.

## Player journey implemented

1. Complete **A Place to Return** with Moss, earning the Trail Compass recipe.
2. Accept **Prepare for the trail** from Rivet. Craft and equip a Trail Compass
   and Trail staff, then return to Rivet. Equipment made earlier counts.
3. Accept **Recover the relay component** from Lumen. Defeat the beacon raider,
   collect its core and return to Lumen. A prior genuine loot claim counts,
   including a core kept in a chest.
4. Accept **Restore the relay** from Lumen. Carry the core in your backpack,
   establish a usable field home near the beacon and use the beacon itself.
   The accepted action consumes exactly one core, completes the quest, lights
   the marker, changes dialogue and reveals Watch Arch on the compass.

A field home requires an owned structure with a sheltered usable bed, chest and
workbench. Both furniture kinds must be physically usable from the same bed
area. The bounded helper uses ordinary interaction and the actual walking
controller, including a connected-doorway test. A sealed annex fails. Its
16×16 m local search and shared work allowance conservatively refuse an
unproved large or complex layout with clear guidance. Readiness is cached
across movement, combat, quest and inventory-only changes; structural changes,
restoration and static scenery publication refresh it. The bed must be within 25 m of the beacon and farther than 40 m from
town. Your town bed can remain the registered recovery point. Completion is
permanent even if the field home is later changed or removed.

The journal opens the current quest. Journal rows select information or track a
destination; they never remotely accept or turn in a quest. The compass's
**Next destination** button returns keyboard focus to walking. The bag offers
explicit destinations. Hidden, deferred or unequipped bearings remain unavailable.

## Installed world additions

The full 8192² terrain remains byte-identical. Its samples SHA-256 is
`2a0ae88395e6d6e59d8853540c875d834f0f04015ee953d76faf68abae610965`.
Existing town, beacon, NPC, encounter and resource IDs 1–18 keep their locations.

| Place | X / Z, metres | Accepted consequence |
| --- | --- | --- |
| Signal Terrace | 30 / −923 | Discover with Use; collect 4 scrap once |
| Survey Overlook | 22 / −1031 | Discover with Use; collect 12 stone once |
| Meadow beacon | −63 / −975 | Physical relay repair after the quest and field home |
| Watch Arch | −40 / −1095 | Compass destination revealed by relay repair |

Three new supply piles append IDs 19–21: 12 stone at (20,−924), 16 wood at
(21,−936), and 4 scrap at (22,−1031). They use ordinary gathering and depletion.
Discovery arrival and reward collection have separate durable receipts. A full
bag preserves discovery and unclaimed supplies; returning with space can claim
them once. Current starter inventory is generous; these amounts are not a
claim that survival balance or exploration pacing is finished.

Four scenery groups contain 23 actual building-catalog pieces: a signal frame,
survey cairn, broken watch arch and meadow waystone. The same accepted packet
supplies rendering and collision. Bounded reserved IDs cannot become owned
construction. Existing saved player poses, homes, recovery and access take
priority; conflicts defer complete groups without rewriting a save.

The real construction obstacle is the 0.96 m face at (29.5,−923). Three Pier
pieces at bottom centres (29.10,−129.92,−923.48),
(29.10,−129.92,−923.00), (29.10,−129.92,−922.52) cost 6 stone and provide
ascent and descent with the real walking controller. The 37 m contour detour
runs via (29,−941) and (30,−941), in either direction. No solution is granted or
placed automatically. The earlier proposed face at X=21.5 was already walkable
and its Pier approach failed; its failed evidence is retained. See the
[route record](../../../../design/adventure-route-r01.md) for current scope.

## Authority, migration and integration

`AdventureSession` alone accepts quest, discovery and activation changes. NPC
turn-ins recheck the actual giver's range and sight. Discovery and relay Use
recheck admitted site range/sight. Relay completion and core consumption share
one accepted revision. Chest ownership plus consumed core must equal the sole
encounter-generation claim; duplicate, stale and foreign requests cannot pay
again. Side quest 5 remains strictly unearned and absent from menus because its
future useful reward is not implemented.

The writer is **schema 4**, with the same payload layout as schema 3. Readers
accept exactly schemas 1–4. Frozen old identities and item/piece/resource bounds
are checked before migration. Only schemas 1–2 receive dormant combat defaults;
schema 3 retains its precise active attacks, enemy phases, health, home, core
location and receipts. New trail state starts unearned. Native `adventure-v1`
and browser `voxys-adventure-v1` namespaces and all world IDs remain unchanged.
Loading does not overwrite old confirmed bytes; migration stays visibly dirty
until storage confirms the new checkpoint.

The retained schema-3 fixture was generated with the actual old Session/codec
before their ABI or format changed. It is 1,542 bytes, SHA-256
`6ff640f312f07d54b4c16c35de29caa8be426ccd7a9015c4dcdde7d2679f6753`.
It contains a constructed home, completed first quest, equipment, active attack
and dodge, health 47, a live raider windup, a dead guardian and a core stored in
its chest. This is an isolated authority component fixture, **not a recorded
player journey**. A genuine completed G-B schema-2 journey fixture is still due.

## Remaining acceptance

- Complete ordinary browser/native home → equipment → fight → discovery →
  field home → relay → save/reload journeys, with two home layouts.
- Complete controller, larger-text, narrower-window and human play checks.
- Finish the planned useful side-quest reward, attack/audio polish, population
  scheduling and displayed frame-time budgets.
- Keep G-B and G-C unchecked until their complete criteria pass; commit a
  successful gate through the repository hook.

Native and WASM builds pass. Final evidence includes 6 trail-authority cases,
7 presentation cases, 5 furniture-access cases, 5 scenery/terrain cases with no
skips, 33 browser DOM cases and 14 browser storage cases. The existing 72
adventure, 8 combat-session and 7 native-save cases passed during schema-4
integration and are reused for their unchanged scope. The final real headless
runtime passes 1,201 idle ticks, 90 synthetic walking frames, all four sites and
exact schema-4 snapshot/restore in 5.355 seconds.

The candidate is [open locally](http://127.0.0.1:42756/index.html?experience=adventure),
package `build-adventure-g-c/web-r02`, ID `adventure-1118f4f0b2c23938`.
CUA tab 6 reaches the live Moss/health/play HUD. It has not been driven through
the full quest chain. The matching native executable is retained at
`build-adventure-g-c/native-r02/voxy_native`.

Exact build, test, package and source hashes are recorded in `results.json`.
The available headless runtime check does not open or control a game window,
render a displayed frame, take screenshots or prove ordinary gameplay.
