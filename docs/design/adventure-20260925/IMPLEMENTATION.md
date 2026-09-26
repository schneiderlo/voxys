# Dawnreach playable first region

Owner approved implementation on 25 September 2026. This document accompanies
[the design](PLAN.md) and [the architecture audit](AUDIT.md).

[Play the local browser build](http://127.0.0.1:8768/index.html?telemetry=0).
The browser now starts Frontier by default. Explicit `?experience=build` opens
the creative workshop. This local preview does not update the public website.

## Implemented scope

Dawnreach runs as a distinct installed `frontier` profile using the existing
full terrain, approved figure, warm WebGPU materials and streamed forest.
The second pass begins in an open canvas camp with the beacon keeper and a
fire. Its route crosses the Broken Stair's severed aqueduct, Dawnreach's pale
observatory, Stormwatch's blue-grey fortress, the Old Quarry's rails and winch,
and the Last Ember's amber, tiered sanctuary. These sites use different shapes,
materials and heights. Warm meadow, cool ridge and autumn woodland palettes,
irregular path verges and varied resource assemblies distinguish the areas.
There are 60 durable resource sources and six independently persistent enemies.
Harvested assemblies disappear from both rendering and collision. Defeated
raiders leave supplies with a once-only receipt. None respawn on reload.

The player begins with a staff and enough materials to experiment. Construction
is paid. Nine figure-sized modules supply floors, stairs, walls, open doorways,
roofs and useful furniture. Their rendered mesh assemblies and collision boxes
share a catalogue. Legacy geometry and content identity remain unchanged.

A workbench within reach of a beacon is required to restore it. The first light
costs 12 wood, 12 stone and 8 scrap, unlocking the quarry hammer and its first
craft's materials. Dawnreach and Stormwatch each require their two assigned
defenders to be defeated. Outer lights also require 4 cut stone each; Last Ember
additionally requires an accepted harvest of a quarry source. Merely holding
cut stone does not satisfy that discovery gate. All three lights produce
persistent visual change and a final completion message. The cache requires
physically reaching its ledge.

Near the tracked high cache or a cleared beacon, **Build base** first selects a
foundation. Once an owned base is nearby, **Build stairs** selects figure-sized
stairs at the cache and **Build bench** selects the field workbench at a beacon.
Existing matching hotbar slots are reused without replacing the foundation slot.
These open the normal paid preview; collision and resource admission still govern placement.
The keeper supplies the opening guidance and restores health when approached
without nearby active wardens, keeping supplies intact.

Enemies use the scaled walking controller against current accepted geometry,
with pursuit, local obstacle steering and separation. Scouts give a short warning,
dart forward and retreat; brutes give a longer warning, commit to a locked charge
direction and leave a longer recovery opening. Hits can interrupt a scout but
cannot cancel a committed brute. Warning cues, strike lanes and exposed-state
feedback make those phases visible. Nearby attackers coordinate windups.
Attacks check reach, facing and line of sight; one strike can spend contact only
once, including a successful dodge. Dodge and damage cooldowns use fixed
simulation time. Building and its palette do not freeze the encounter clock.
A defeated player recovers at a valid sheltered bed or starting camp without
losing creations or equipment. Shelter remains necessary for bed recovery;
beacon assembly requires an accessible bench without a prescribed room shape.

Move, repaint and a bounded edit history operate on stable part identities.
Undo removal pays its current material cost; history never rewinds gathering,
loot, inventory, enemy state or progression. Occupied storage cannot be deleted.
The current edit history is session-local and resets on loading a checkpoint.

The shared HUD adds real health/material counts, destination bearings, distance,
progress and milestones. Accepted damage, depletion and restoration changes
produce bounded visual bursts; reduced-motion preferences suppress these bursts.
Browser sound adds varied gathering/building/impact textures, grounded footsteps,
distinct scout/brute warnings and a rising beacon payoff. A sparse pentatonic
score evolves between phrases, with wind, birds, campfire and a combat tension
layer. The existing accepted-state poll advances this audio; it adds no timer.
Voices have finite lifetimes and a cap, and transitions fade. Sound is gesture
gated, respects the persisted mute preference and retires voices while hidden.
No external sound service is used. Native sound is not implemented by this
browser audio module.

## Technical map

| Boundary | Main implementation |
| --- | --- |
| Installed region, persistent IDs and matching scenery collision | `src/game/adventure/frontier_world.*` |
| Enemy/site authority, receipts and schema7 frontier saves | `frontier_state.*`, `adventure_session.*`, `adventure_save.*` |
| Movement, combat, interactions and checkpoints | `frontier_runtime.cpp`, `frontier_combat.hpp`, `frontier_interactions.hpp` |
| HUD, figures, telegraphs and beacon effects | `frontier_presentation.cpp`, `src/render/adventure_hud*` |
| Safe build edits and session-local history | `frontier_editing.cpp`, session commands |
| Figure-sized mesh/collision catalogue | `building_catalog.*`, `data/adventure/frontier-kit-r01/catalog.json` |
| Browser routing, separate storage and audio | `web/index.html`, `web/adventure_saves.js`, `ui/src/frontier_audio.js` |

Frontier writes schema7; older modes continue writing schema6. The trusted
installed profile controls rules, never an incoming save flag. Static world
geometry is published for actual building/depletion changes. Movement and
combat receipts do not rebuild the forest each tick. Enemy movement checkpoints
are saved in bounded batches.

## World compatibility

The second-pass geometry is fingerprinted as `dawnreach-world-r02`. Earlier
Frontier saves are rejected by installed-content validation; no silent migration
or replacement is attempted. The browser's Frontier load-failure screen offers
**Retry** at the exact original URL and **Start a new expedition** at the same
origin/path/profile with `world` removed and `new=1`. The action creates a
separate expedition through the existing new-world flow. Old save bytes remain
on-device. Creative and legacy profiles keep their separate storage and rules.

## Second-pass verification

- Native and WebAssembly builds pass. The focused session, combat, interaction,
  world and HUD suites pass (69 executed cases). The optional GPU HUD capture
  is not included in that count.
- The exact installed terrain check verifies spawn and approach clearance,
  grounded solid assemblies and a scaled walking controller ascending a built
  three-module stair route to the actual cache interaction target. This route
  test compiles placed geometry; it does not cover interactive placement admission.
- Browser checks verified the revised opening, worn trail, automatic cache
  discovery, foundation-first contextual guidance and hotbar preservation.
  A foundation and stairs were placed through normal controls, charging exactly
  8 stone and then 8 wood + 4 stone. Occupied placement was correctly refused.
  Reload retained both pieces, the discovery and the resulting 40 wood / 20 stone /
  12 scrap inventory. The final packaged browser run reported no console errors.
- Browser combat verified scout pursuit, damage, retreat after a staff hit and
  recovery at camp with supplies intact. Manual attack timing is limited by the
  browser-control interface; no full playthrough, opening duration, balance
  conclusion or measured performance is claimed here.
- An earlier-world bookmark failed the installed-identity check and displayed
  Retry and Start a new expedition. The latter opened a separate playable world.
  A browser shader compilation error, obstructed opening camera and hotbar
  replacement problem were found during this pass and corrected before delivery.
- JavaScript adapter, procedural audio, startup/loading and browser-save unit
  suites pass. They cover mute/visibility lifecycle, fresh-event delivery,
  bounded audio voices and the separate-new-expedition recovery URL.
- `//tests:frontier_combat` adds focused checks for distinct phase schedules,
  locked charge direction, interruption rules, single-contact dodge handling,
  actual movement reach and collision with built cover.

## First-pass verification record (historical)

The following checks were recorded before the second-pass world and combat
changes. They describe the first playable candidate, not fresh validation of r02.

- Native application and WebAssembly release builds pass.
- Focused adventure, door, HUD layout, configuration, frontier authority,
  building, interaction and installed-world suites pass. Native save/profile
  checks pass separately. The optional installed-terrain test was explicitly
  enabled and passed against the actual `.r16` terrain shipped with this build.
- Figure-sized doorway clearance, actual stair traversal, shelter and service
  point clearance are covered by the native tests.
- The complete economy sequence now passes using installed resource yields:
  gather, pay for three camps, restore the first light, craft/equip the quarry
  hammer, mine cut stone, and restore both outer lights. Save/reload occurs
  between milestones; repeated claims are rejected. This test accepts placement
  geometry through a fixture, so it proves costs and durable progression rather
  than a complete walk through the rendered region.
- Browser adapter, storage/loading, routing and synthesized audio tests pass.
- Browser play verified the rendered opening region, movement and ridge
  traversal, enemy pursuit/damage and loot, foundation costs, undo/redo refunds,
  a player-built workbench, field-hammer crafting/equipping, and a reload that
  retained health, materials, construction and defeated-enemy state.
- Browser gathering with the field hammer increased scrap from 14 to 38 and
  removed that source's interaction prompt; the doubled yield was saved.
- Browser defeat/recovery returned the player to camp at full health with
  supplies intact. Journal destination tracking updated the bearing and distance.
- Screenshots exposed menu-label overlap, clipped journal text, unsupported font
  characters and an oversized hammer head; these were corrected in the candidate.
- The manual browser run did not complete all three beacons. Full campaign
  playtesting, timing and balance remain separate from authority test coverage.

## Scope limits

This is one playable region, not the proposed multi-hour campaign. The wider
roadmap still includes additional encounter types, a ranged enemy, additional
regions, broader material upgrades and extended balancing/playtest work. The
second pass improves opening variety; pacing through its first ten minutes needs
rendered playtesting. AI uses bounded local steering, so complex player-made
mazes can stop pursuit. There is no multiplayer, hunger or durability system.
Native/browser renderer parity and performance numbers require measured evidence;
passing authority tests alone does not prove those qualities.

Browser checks used a separate QA world. A fresh world starts with the original
starter supplies and no completed objectives; test progression is not baked in.

## Local build record

The first-pass browser executable was `frontier-d97ec68f8cf8ba22`; it is retained
as historical evidence. The second-pass package is `frontier-f25669a648036a13`,
with a reproducible copy at `/tmp/voxys-frontier-repro-world-r02-colorfix`.
The active preview directory is `/tmp/voxys-frontier-preview`, served
on loopback port 8768 by
`tools/serve_wasm.py`. The native target is `//:voxy_native`; the browser target
is `//:voxy_wasm` with Bazel's `--config=wasm`. Rebuilding the UI uses
`npm --prefix ui run build`.

Package new browser outputs with the checked-in script (choose a new output
directory each time):

```sh
python3 scripts/package_adventure_preview.py --profile frontier \
  --binary-directory /path/to/bazel-bin/voxy_wasm --output /tmp/dawnreach-preview
python3 tools/serve_wasm.py --directory /tmp/dawnreach-preview \
  --host 127.0.0.1 --port 8768
```

The packager accepts both `voxy_wasm.*` and `voxy_wasm_cc.*` generated filenames,
checks the UI manifest and required host exports, and refuses to overwrite an
existing preview. Its package fingerprint covers the whole web package, so it
differs from an executable-only fingerprint.

Focused test targets are `//tests:config`, `//tests:adventure`,
`//tests:adventure_doors`, `//tests:adventure_hud_layout`,
`//tests:frontier_session`, `//tests:frontier_building`,
`//tests:frontier_world`, `//tests:frontier_interactions`,
`//tests:frontier_combat` and `//tests:native_adventure_saves`.

## Main integration checks (2026-09-26)

The candidate was applied to main revision `9bf642b1`. UI tests, browser-host
storage/loading/routing tests, catalog consistency and smoke-script syntax pass.
The terrain importer test passes. The complete native suite ran 2,688 cases:
2,665 passed, 21 optional/environment-dependent cases skipped, and two failed.

One failure was an outdated Cove fixture expectation for the GPU batching
already implemented on main. Its seven exact draw-count expectations are now
corrected; the focused rerun passes. Expanded geometry counts, memory budgets,
ownership checks and submission/discard assertions remain unchanged.

`CannonPhysicsSceneGpu.ThrownBrickFlightBounceAndStackMatchJolt` still fails in
isolation: GPU stack heights/stability and airborne orientation differ from the
Jolt comparison. Its test, physics implementation and physics shaders are
unchanged from the main revision above. No physics fix or relaxed tolerance is
included here. The complete native suite is therefore **not green**.

Native checks used the Nix toolchain and optimized compilation. Narrow per-file
warning-as-error downgrades accommodated existing GCC 15 standard-library
diagnostics; warnings stayed visible and test assertions remained enabled.
