# Adventure progression: bounded C implementation design

**Status: combat foundation implemented; remaining progression proposed.** This is not approved content, completed gameplay, a passing gate, or evidence of enjoyment. It develops `EXPLORE-A01`, `COMBAT-A01`, and `OUTPOST-A01` in [the implementation plan](../../GAME_IMPLEMENTATION_TODO.md). Navigation has separate evidence; it does not establish combat or progression correctness.

Build on the current G-B adventure: the actual main terrain, player-owned houses, Moss/Rivet/Lumen, the first-home quest, and the earned Trail Compass recipe. Preserve these features and all existing worlds. Solo play comes first; personal quest credit and shared world changes remain separate records for later co-op.

## Scope and proposed stable IDs

Names and rewards below are working choices. Confirm their usefulness during implementation; do not silently treat them as owner-approved content. Each installed definition starts at version 1. IDs belong to separate typed domains and must not alias constructed part/component IDs.

| Domain / ID | Proposed meaning | Completion semantics |
| --- | --- | --- |
| Quest 1 | Existing **A Place to Return** | Preserve `FirstHomeProgress` and its current-state home test exactly. |
| Quest 2 | Prepare for the trail | After explicit acceptance, equipped usable Trail Compass and equipped weapon qualify, including equipment acquired earlier. No invented historical crafting event. |
| Quest 3 | Recover the relay component | After quest 2, accept and recognize the historical claim receipt for encounter 2's core. Turn-in leaves the core available for repair. Earlier discovery/claim may count. |
| Quest 4 | Restore the relay | After quest 3, accept and activate world object 1 through the atomic repair below. |
| Quest 5 | Optional ruins side quest | Explicit acceptance, then both discovery receipts qualify, including prior exploration. Reward a permanent new useful recipe; a proposed field lantern requires an actual useful lighting/building implementation. Never gate the main route on this quest. |
| Discovery 1 | Ruin cache | Actual arrival records discovery; a separate claim transfers its installed material reward. |
| Discovery 2 | Route overlook | Actual arrival records discovery and contributes to quest 5; guidance must reveal something useful. |
| Encounter 1 | Trail guard | First readable encounter of one archetype; authored material loot. |
| Encounter 2 | Relay guardian | Tougher variant of the same archetype; unique relay core entitlement. |
| World object 1 | Relay | Permanent activation receipt controls light, dialogue, and next destination. |
| Destination 1 | Place revealed by the repaired relay | Derived from relay activation; it is not an independently granted flag. |

The schema-3 foundation fixes capacity to two encounters, IDs 1–2; extending that capacity requires a versioned change, and undefined IDs still refuse. Discoveries are bounded to two and the relay to one. Keep encounter generation 1 with no automatic respawn for this opening. New respawn rules require an explicit extension.

Append item IDs without changing 0–5: proposed ID 6 is the weapon and ID 7 is `RelayCore`, maximum stack one. The weapon shares the existing tool/weapon slot; the compass uses the existing utility slot. The core occupies ordinary backpack/chest space. Existing hammer and all 14 building pieces are already available, so awarding them again does not satisfy a new capability reward.

## Authority records and transitions

Use small typed records, not a scripting engine or an untyped quest blob:

- Keep the G-B first-home record unchanged. Add four fixed quest records for IDs 2–5: phase and permanent nonzero completion revision. Installed definitions declare prerequisites and current-state versus historical semantics. Explicit acceptance always precedes completion.
- A discovery has `discoveredRevision` and `rewardClaimRevision`; zero means absent. A full backpack cannot erase knowledge or consume an untransferred reward.
- An encounter has installed ID, spawn generation, health, `deathRevision`, and `lootClaimRevision`, plus the bounded combat checkpoint described below. Dead means zero health and a nonzero death receipt. A loot receipt requires that generation's death receipt.
- The relay has one activation revision. Quest completion, recipe knowledge, light, dialogue, and revealed destination derive from accepted receipts rather than redundant unlock booleans.

Every receipt must be within the accepted world revision. Required predecessor receipts must exist and precede their dependent transition. Unknown enums/IDs, duplicate or unsorted records, future revisions, unsupported generations, and impossible phase combinations refuse. Permanent receipts are never evicted into a payable state.

Proposed session operations are `prepareDiscover`, `prepareClaimDiscovery`, `prepareAcceptTrailQuest`, `prepareCompleteTrailQuest`, `prepareClaimEncounterLoot`, and `prepareActivateRelay`. Each uses the existing `CommandStamp` / private candidate / `PreparedChange` / commit boundary. Installed definitions supply outputs and costs; player requests never supply reward quantities, damage values, completion flags, or arbitrary health.

Reach, sight, discovery volumes, safe shelter, and coherent geometry are checked by the trusted runtime adapter on the complete candidate. Ownership, capacity, prerequisites, receipts, and conservation remain session checks. Preview, animation, menu labels, and observation JSON do not grant progress. Stale revision/sequence and foreign-owner requests leave the accepted state untouched.

## Combat, defeat, and present loopholes

The frozen G-B/r06 source had these gaps; the C combat foundation now implements the corrections below. Its ordinary gameplay acceptance remains open:

- `AdventureSession::updatePlayer(pose, health)` accepts any health from 0–100. Restrict ordinary locomotion to preserving health; damage/healing must have specific authoritative transitions.
- `AdventureRuntime::recover()` can teleport and pass health 100 at any time. Voluntary **Return home / town** needs an out-of-combat rule, otherwise Pause bypasses every hostile windup and wound.
- `prepareUseBed()` heals to 100 after shelter/reach checks but has no hostile/safe-rest rule. Add a declared safe-rest predicate to its trusted admission.
- Health zero currently does not stop walking or other actions. Add an explicit defeated state: no movement, building, attack, gathering, or loot claims until an accepted safe recovery.

Use one CPU fixed-tick combat owner and the real actor/query authority. A trusted bounded combat resolution names the accepted combat tick, geometry revision, source attack ID, target, and spawn generation. Damage is derived from installed attack definitions. One swing may consume its damage window once per target; repeated overlap frames, held input, duplicate resolutions, or a stale generation must not hit again.

Persist enough combat checkpoint state for health, actor pose, active attack/windup phase, swing IDs, consumed damage windows, attack/dodge deadlines, and defeat to survive reload coherently. Rebuild navigation paths and animation from this state. Use the same paused clock for actors and cooldowns; opening menus must not erase deadlines. Counter exhaustion refuses rather than wrapping. Do not save player damage independently of enemy death/loot state.

Defeat recovery validates the currently usable registered bed against accepted collision or falls back to town. Publish recovery pose, restored health, and cleared defeated state together, after the actor can occupy that pose. Keep inventory, equipment, homes, core ownership, and permanent quest receipts. Failure leaves the actor defeated without a partial teleport/heal. Ordinary escape to safety remains possible through movement and retreat rules.

## Unique loot and relay repair

Encounter 2's death creates exactly one unclaimed core entitlement. Claim transfers its complete authored loot into the backpack and stamps the receipt atomically; insufficient space leaves all loot claimable. Transfers to a chest conserve the core. Full-chest destruction already refuses and must keep doing so.

For generation 1, validate this whole-world conservation rule:

`unclaimed core entitlement + core count in all owned slots + core consumed by relay = guardian death entitlement`

The right side is zero or one. No core may exist before that entitlement, appear in equipment, be granted twice, or disappear after claim except through accepted relay activation. The claim receipt remains permanent after consumption. This is an ownership invariant, not cryptographic protection against arbitrary local archive rewriting.

`prepareActivateRelay` requires quest 4 active, actual relay reach/sight, the one core in the backpack, and a currently usable owned field shelter near the installed relay. A field shelter has a sheltered bed, chest, and workbench in one structure and meaningful separation from town. Its exact radius belongs in the installed definition. Accept alternative layouts; do not require the starter blueprint or replacing the registered home with the outpost.

One candidate consumes the core, records relay activation, completes quest 4, and grants/reveals its consequence. Check output space first if an inventory reward is added. Repeated activation or reload cannot repeat any reward. Later outpost removal does not revoke the earned world change. Newly installed scenery/actors must yield to old homes and saved player poses; bounded alternate placement/deferred admission must not become a save rejection or enlarged blanket town exclusion.

## Strict schema 1/2 → 3 migration

Write schema 3 and accept exactly 1, 2, and 3. Keep native `adventure-v1`, browser `voxys-adventure-v1`, world namespaces, and storage generation/confirmation rules unchanged. These profile names are not payload versions.

Freeze compatibility by **source schema plus exact installed identity**, replacing the current single `legacyIdentity` assumption with a bounded installed compatibility table. Never take compatible identities from the archive itself. The current identities are:

| Payload | Installed content identity |
| --- | --- |
| Actual G-A schema 1 | `1471b647cb7e8a02c06ae5bba53c7f9f835d432d6da598e8338825b845e448b8` |
| Current G-B schema 2 | `11383554b461917c507d21b20adad88c24989fc3e1272baec5e6d6bba840c431` |

The first identity is extracted from `frozenHomeV1()` in `tests/test_adventure_session.cpp`, whose actual archived payload SHA-256 is `7d022bec04d1f834370c48cfd4ae498b6fb574d5ff2fddc41412e47aa34a9a02`. Its source is [the retained G-A native save](../validation/adventure/G-A/native-home-r01/final-current.bin). The second is the exact current runtime recipe: SHA-256 of the lowercase schema-1 identity hex followed directly by `adventure-town-home-quest-compass-r01:quest1-v1:wood2-scrap4:equipped-utility`. These hashes must stop depending on a future edited catalog fingerprint. Retain a real completed G-B archive fixture before accepting migration as complete; a newly fabricated schema-2 packet is insufficient evidence of preserving the actual world. Independent schema-3 implementation and synthetic checks may proceed in the separate C candidate while r06 and its worlds remain intact.

Validate each old payload under its frozen contract before introducing defaults or changing its content identity. Schema 1 item IDs stop at 4; schema 2 stops at 5; old building IDs remain 1–14, with their original slot/equipment/stack rules. Preserve every existing field, including the real home, exact items, chest contents, health, first-home receipt, ID horizons, and player/recovery poses. Both old schemas permit health zero: preserve it and derive defeated state rather than silently healing, moving, or rejecting that valid archive. New discoveries, claims, quests, and relay activation default unearned; do not replay starter grants.

Keep strict bounds before allocation, whole-payload checksum, expected world, exact trailing length, canonical ordering, and output preservation on refusal. Update the browser bootstrap's explicit `1/2` allowlist to `1/2/3`; strict C++ decode remains authoritative. Return `sourceSchema` / migration metadata and keep migration dirty until normal confirmed publication. Merely loading old bytes must not rewrite them. Add the C semantic definitions to a new content identity rather than accepting arbitrary schema-3 content.

## Implementation ownership and required evidence

| Owner | Files / responsibility |
| --- | --- |
| Session/save owner | `adventure_session.*`, `adventure_save.*`, `item_catalog.*`, `quests.*`, proposed `adventure_progress.*`, and focused transaction/migration tests. Typed records, conservation, durable transitions. |
| Runtime/combat owner | `adventure_runtime.*`, new bounded combat owner, input/presentation integration. Fixed ticks, trusted hit resolution, health/recovery boundary, coherent checkpoints. |
| World/navigation owner | Installed encounter/discovery/relay definitions, layered navigation integration, encounter admission, reach/sight, field-shelter predicates, both physical route solutions. |
| Host/UI owner | Browser/native storage version admission, visible save failures, attack/dodge/health/loot controls, usable quest/relay feedback. Keep unrelated legacy routes intact. |

Before claiming C completion, verify duplicate/stale attacks and claims, wrong generations, full bags, core-in-chest conservation, defeat → reload → recovery, safe-rest refusal, removed shelter, duplicate relay interaction, and exact real schema-1/2 migration with forged-new-ID refusals. Then verify ordinary native/browser play, both obstacle solutions and alternate outpost layouts. Existing navigation tests support navigation only; tests and this document cannot certify a passing gameplay gate or player enjoyment.

The implemented combat/save contract and exact current limits are recorded in [the C foundation evidence](../validation/adventure/COMBAT-A01/foundation-r01/README.md). Discovery, remaining quest and relay operations above are still planned; serializing their reserved default records grants no such capability.
