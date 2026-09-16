# Moss's optional scouting quest and a useful building recipe

This checkpoint extends the warm LEGO-style adventure. It does not complete
G-B, G-C, human play, controller acceptance or visual approval. The player,
Moss, Rivet, Lumen and hooded raiders keep their existing minifigure assets.
No terrain samples, village anchors, encounter positions or prior supply IDs
change in this checkpoint.

## What the player can do

After completing **A Place to Return**, speak to Moss and accept **The
Surveyor's Notes**. Visit Signal Terrace and Survey Overlook and use each
landmark to record the visit. Earlier recorded visits count. Collecting their
supplies is optional; a full bag does not erase a visit or block this quest's
turn-in. Return to Moss to finish it.

The reward permanently unlocks **Wide stone step** in the Structures section
of Building pieces. It previews three existing Pier pieces and costs **6
stone** on each placement. Rotate it, adjust height and use fine placement to
fit the ground. There is no free structure or material grant. The individual
Pier remains available for free-form construction before and after this quest.

The recipe works at arbitrary valid construction positions. One useful
application is the Signal Terrace ledge. Its three bottom centres at yaw 0
are (29.10,−129.92,−923.48), (29.10,−129.92,−923.00) and
(29.10,−129.92,−922.52), metres. This is an example, not a scripted placement.
The existing longer walking route remains available. The real-controller test
checks the paid recipe's ascent and descent on the unchanged full terrain.
It is not an ordinary mouse/controller placement recording.

The journal lists this as an optional quest without displacing the current
main-story objective. Moss gives missing-landmark guidance and explains the
learned recipe after completion. Native and browser menus share the same
intent and entitlement checks. Browser cards use the actual Pier thumbnail,
three-piece label and cost; they do not show starter-room furniture hints.

## Implementation contract

- Quest ID 5 uses the existing fourth trail-quest record; giver ID 1 is Moss.
  Accept and turn-in are separate commands. First-home completion and both
  discovery arrival receipts must precede its completion receipt. The runtime
  also checks that Moss is physically reachable. Prior visits count; unclaimed
  discovery material rewards do not prevent completion.
- `wideStoneStepRecipeUnlocked` derives entitlement from the permanent quest
  receipt. Removing the old home, collecting supplies or restoring a save
  cannot revoke it. Repeated or stale turn-ins cannot grant anything again.
- `building_blueprints.*` supplies immutable metadata and canonical layouts.
  `AdventureSession::prepareBuildRecipe` rechecks entitlement and delegates to
  normal paid construction validation. Three Piers, or the unchanged 19-piece
  starter room, publish as one new owned structure. Insufficient materials,
  collision, unsupported placement and capacity limits refuse the entire
  candidate. Undo removes that structure and refunds actual catalog costs.
  It does not remove a pre-existing home.
- The runtime selects, previews, renders and places the same recipe. Changing
  to an individual piece through menu, keyboard or controller clears recipe
  selection. Review found and fixed shoulder-button cycling that previously
  changed the anchor piece while retaining the recipe. Successful restore also
  clears previews, undo targets, queued actions and old menu contexts without
  recycling intent tokens. Native-facing quest text uses an ASCII apostrophe
  so the HUD does not print an unsupported Unicode escape.
- The writer is **schema 5**, with the same payload layout as schemas 3–4.
  Readers accept frozen schemas 1–5. Schema 4 may contain completed main
  quests, discoveries and consumed relay core, but cannot contain earned quest
  5. Migration preserves its exact prior fields and starts the side quest
  unearned. Older source-version bounds remain in force. World IDs and native
  `adventure-v1` / browser `voxys-adventure-v1` storage namespaces are unchanged.
- Schema-4 content identity is
  `0ca6be8bbc4285f4541aadc74d8cb7aeefb4b2ed078f45f86e7b0a2173ab1e93`.
  Runtime checks it before deriving schema 5 from its hexadecimal identity
  plus `sideQuestContentFingerprint()`. The resulting schema-5 identity is
  `b989f53fd82527a42f3e60582f9bb0cdcafced9cfea94b53074cccf37eca290c`.

## Retained migration evidence

Before changing the writer, the actual schema-4 Session and codec generated
`tests/fixtures/adventure/schema4-component-r01.bin`: 1,542 bytes, SHA-256
`8493c01ec604f1bdd46f3fbcfd2328cc54560ba773b0816980b9320cf4b2da85`.
The generator, old source hashes, log and decoded facts are retained beside it.
It extends the preserved schema-3 component fixture with completed main quests,
both discoveries and claims, consumed core and relay receipt. It retains its
19-piece home, equipment, health 47 and active combat. Quest 5 is default.

This is a **component fixture**, made with isolated trusted geometry callbacks;
it is not a played journey or proof of a physically usable field home. The
actual completed G-B schema-2 player journey fixture remains outstanding.
The old schema-2, schema-3 and schema-4 preview/native packages are preserved.

## Acceptance boundary

Exact checks, failures, source hashes and package identity are recorded in
`results.json`. Tests use actual ownership, costs, save codec and terrain where
stated. The headless runtime uses library input and has no game window,
displayed frame, screenshot or claim of ordinary gameplay.

Remaining work includes the full ordinary native/browser adventure and reload
journeys, two home layouts, real controller and accessibility checks, owner
feedback, polished attack/audio work, shared population scheduling and measured
frame-time budgets. Keep G-B/G-C open until their complete criteria pass, then
commit through the normal repository hook. The pending permission for an
alternate UI driver is not satisfied by passing these component tests.


## Current preview and passing checks

Open [the updated adventure](http://127.0.0.1:42757/index.html?experience=adventure).
Package `build-adventure-g-c/web-r03`, build ID
`adventure-1b8f516c77d8d6d1`, preserves all older candidates. Its matching native
executable is `build-adventure-g-c/native-r03/voxy_native`. All 46 manifest
entries match. CUA tab 7 reaches the live starting HUD; no screenshot or journey
was taken.

Native and browser builds pass. Focused evidence contains 5 side-quest,
4 blueprint/full-terrain, 10 presentation, 6 prior-trail, 72 existing adventure,
8 combat-session and 7 native-storage cases, plus 36 browser DOM and 15 browser
storage cases. The final actual headless runtime check passes in 5.240 seconds,
including locked/earned menu selection, rotation, synthetic shoulder cycling
and queued/stale actions across checkpoint restoration. Passing unchanged
component checks are reused; the changed-source runtime and builds cover the
final checkpoint cleanup and native label correction. Full gate acceptance
remains open.
