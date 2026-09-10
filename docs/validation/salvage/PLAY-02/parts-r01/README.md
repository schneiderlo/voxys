# PLAY-02: buy parts and sail the expanded boat

Date: 2026-09-09. Owner: root. This is a **passed paid-part builder checkpoint**, not full PLAY-02 or a complete game gate. Both application builds, 156 native checks, thirteen UI lifecycle cases and a fifteen-stage real browser journey pass. No screenshots were taken.

## What the player can do

The starting dock's **Workshop / B** now has a parts drawer. Choose one of eight admitted part types with its Previous/Next type buttons (**C** cycles forward with the canvas focused); **Add part / V** creates an editable ghost. The drawer displays the part's catalog price. New ghosts search real sockets and prefer positions that preserve boarding and helm standing room. Move, rotate, snap, cancel or remove the ghost, then **Keep** the design. None of this spends material.

**Launch / Enter** purchases the added parts and rebuilds the actual boat. The stock and launch price are visible before purchase. Unaffordable designs can be planned, but cannot be launched. Existing Undo launch / Redo launch, walking, helm, throttle, steering, Reset and Leave still work.

A fresh cove host world now starts with **48 general material and zero special machinery**. This is root-selected initial tuning (D22), not a claimed owner-selected amount. It gives the player an immediate building choice while salvage and progression are completed. The grant happens only during fresh-world bootstrap, never at Reset or Launch. Future SAVE loading must restore the recorded balance instead of applying this grant again. Leave currently ends the entire local RAM world; no progress transfers into its newly created successor.

Pontoons cost 24 material and weigh 120 kg. Two purchases change the starter from eleven parts / 1,035 kg to thirteen parts / 1,275 kg and use all 48 material. Undo returns the exact purchase cost; redo restores the same paid identity and charge. Dismantling a paid pontoon returns its catalog salvage value of 16, rather than its purchase price of 24. Original starter-loan parts still return zero. The catalog also includes beam, plate, engine, propeller, helm, winch and cradle; machinery requirements and the starter consumer's module-count constraints remain enforced.

## Technical implementation

`CoveWorkshop` now owns a bounded catalog selection, installed-slot boundary, original boat collision slots and the active slots at editor creation. Add chooses a free dynamic slot, builds a ghost from the exact admitted definition, and reconnects through the existing full compiler. Cancel/Remove/Undo restore the previous blueprint without issuing IDs or touching inventory. The original scene contains 25 slots and the present renderer permits 32: up to **seven active paid additions** fit this cove. A local removal must be launched before its accepted slot can be reused by the drawer. General 256-part craft remain VIS/SIM work; no unbounded capacity is claimed.

`prepareCoveRefit` keeps source IDs for retained parts and emits zero-source paid additions. The caller supplies the original loan slot set so a removed loan cannot be silently revived through Add. `quoteCoveDesign` computes checked gross charges/refunds without compiling on every HUD refresh. `CoveDesignCost::affordable` handles exact per-resource net costs and overflow. Prices cross JavaScript as strings and use `BigInt`; the UI test includes values above safe JavaScript integer precision. GameSession still calculates and reserves the authoritative costs independently.

`prepareCoveExpandedLaunchDesign` maps the canonical result into the current scene. Original loan bindings remain reserved. Retained paid slots are reserved **before** assigning any new/restored paid IDs; this prevents an older restored ID from taking a younger retained part's slot. Unknown paid IDs use an available dynamic slot and an exact admitted definition key. Their IDs come from GameSession, not the renderer or catalog. Inactive paid slots are reusable after publication, so repeated edits do not consume one permanent slot per historical ID. Undo keeps canonical part/weld records and provenance in the existing bounded session history.

The launch candidate preallocates its complete scene, boat, player collision, editor, LOD selection vector and dynamic boat slot set. The accepted LOD vector is swapped with the other prepared mappings only after physical confirmation. Ghosts can render new slots without indexing the old accepted LOD vector; diagnostics use the accepted scene's expanded placements. Removed dynamic slots never become fixed scenery. All added models reuse already admitted mesh/material uploads; new bodies/shapes still follow the previous launch checkpoint's future-tick confirmation and retirement protocol.

Auto-placement for a *new* part now also checks the cove's four authored standing anchors. An early connected second-pontoon suggestion blocked the helm and was correctly refused at Launch; the new preference finds a usable mount instead. Explicit movement of an existing part remains free to preview structurally connected layouts, with final Launch validating navigation and consumer requirements.

## Verified evidence

- Optimized native app and shipping WASM app builds pass. Existing unrelated WASM warnings remain.
- **156 native tests pass**, with no skipped cases: 133 session/journal/event cases and 23 cove/navigation cases. New checks cover new ghost cancellation/removal, actual pontoon addition and prices, paid identities, restored slots, removed collision, salvage refund, and a second usable pontoon placement.
- **13 UI lifecycle cases pass**, including pending controls, unpaid ghosts, disabled unaffordable Launch and lossless large prices.
- **15 actual browser stages pass** on hardware WebGPU. The journey creates/cancels an unpaid ghost, keeps an uncharged design, purchases two real pontoons, undoes/redoes a purchase, plans an unaffordable beam, boards and sails/turns the expanded hull, observes its physical Reset at the berth with zero remaining stock, dismantles a paid pontoon for 16, undoes that dismantle with the same paid IDs, and drains Leave. There are six confirmed physical replacements. No simulation setters, direct command injection or images are used.

See `results.json`, `browser-journey.json`, `browser-startup.json`, build/test logs and source/package hashes. The first real browser trial correctly refused a helm-blocking mount; its failed result is retained separately. The accepted result is browser r02, native build r03/tests r02 and WASM build r03.

## Reproduction

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="GameSession*:SessionJournal*:SessionEvent*:CoveMovement*:CoveNavigation*"'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
node scripts/test_salvage_preview.mjs
# Package web/* with build-lego-wasm/bin/voxy_wasm.{js,wasm,data}.
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/parts-browser.json \
  VOXY_SMOKE_COVE_PARTS=/tmp/parts-journey \
  node scripts/smoke_integrated_wasm.mjs /tmp/voxys-parts-web-r02 salvage-cove
python3 tools/serve_wasm.py --directory /tmp/voxys-parts-web-r02 --port 38196
```

The verified package is `/tmp/voxys-parts-web-r02`; the owner preview is `http://127.0.0.1:38196/?experience=salvage-cove`. Previous previews at 38194/38195 remain historical and do not gain starting stock retroactively.

## Next implementation work

Full PLAY-02 is still open. Add meaningful configuration controls and carry typed module settings through the design, canonical request, restored editor and physical consumers; `FixturePartPlacement` currently carries geometry only, and the cove's water/tow setup still consumes nominal definition limits. Do not add sliders whose values the simulation ignores. Finish picking/controller/full camera and durable named/duplicated designs through SAVE.

The initial grant is onboarding stock, not completion of recovery jobs, banking, durable rewards or progression. Preserve the generator's actual-world recovery; no mission script may teleport it into a success state. The one-helm/one-propeller/at-most-one-winch profile, articulated tools, powered machinery, latching, damage/flooding and larger renderer budgets remain their existing tasks.

Add targeted live Leave/failure/capacity timing coverage when working on those boundaries. Normal browser Leave and the existing generic session/shape suites do not prove every adversarial timing case. No native human/controller acceptance, final visual-quality, performance, multiplayer, campaign-duration or full gate claim is made. Do not rerun passed captures/journeys without a relevant change or unresolved failure. Commit only after a complete gate passes its required repository suites.
