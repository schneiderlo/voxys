# PLAY-02 / SAVE: named designs that survive browser reload

Date: 2026-09-09. Owner: root. The **browser blueprint-library checkpoint passes**. This implements actual save/name/duplicate/load/backup/export/import for boat designs. Full PLAY-02, SAVE-01–04 and their gates remain open. Native disk persistence and native named-design UI are not implemented by this checkpoint.

## Player workflow

Open **Workshop / B**, keep the current design, and expand **Saved designs**. Name the boat and use **Save as new**. Select an existing design to load it, update it from the current kept draft, rename it, duplicate it under a different name, restore its previous saved version, or remove it. Export/import transfers a `.voxy-design.json` file. Removing a saved design explicitly confirms removal of that design and its backup.

Loading changes the workshop draft as one undoable edit. The sailing boat stays unchanged until **Launch**. The cost appears before Launch, and required extra parts are purchased through the existing GameSession transaction. Importing a file adds a validated library entry; it does not automatically replace the draft or current world. Saving and duplicating designs never grants parts or starter entitlements.

The library persists in the same browser profile and origin across reloads and Leave. Its current verified preview is `http://127.0.0.1:38198/?experience=salvage-cove`. A new preview port is a new browser origin; use export/import to transfer designs. The visible UI states that **expedition progress is not saved yet**. A reload still creates a fresh RAM world, with its normal initial inventory; the saved blueprint must be bought/built in that world.

## Implementation

See [the exact versioned format and storage contract](../../../../salvage-blueprint-format.md). Canonical SVBP bytes contain local part ordinals, exact definition/version IDs, lattice placements, paint/settings and connections, followed by SHA-256. They contain no physical ownership, condition, world counters, loan entitlements or inventory. Decode verifies size/checksum/content/typed rules/canonical order before publishing an owned design result. Both native and WASM use this same C++ codec.

The workshop exporter captures only a kept, compiled valid draft. The importer reserves all exact currently owned matches before reusing same-definition parts, then allocates available dynamic *design slots* for extras. It never allocates durable part IDs. Removed loan slots cannot be revived from a file. The import passes the full assembly compiler, bounds the whole registry including scenery links, and explicitly rejects unsupported connection kinds/custom strengths. It preserves paint/settings through the runtime scene overlay and canonical refit. General paint rendering remains unfinished.

The browser library stores at most 32 current designs and one backup each. Updates, backup rotation and current writes share one IndexedDB transaction with **strict durability** requested and observed. UI acknowledgment waits for `oncomplete`. A one-operation UI limit, independent record validation, expected-revision checks and transaction rollback prevent stale-tab overwrites or partial backup/current updates. Names and file sizes are bounded; IDs/counters never become JavaScript floating-point numbers. File contents and names are rendered through text properties, not HTML.

The new controls have their own lifecycle handlers. General workshop action dispatch selects only buttons with `data-workshop-action`; it cannot accidentally treat a library button as construction input. Typing W/E/R/V/X in the name field does not move, launch, add or reconfigure parts. Save completion is separate from the current boat's future-tick launch confirmation.

## Verified results

- Native and shipping WASM application builds pass. Existing unrelated WASM warnings remain.
- **192 native tests pass, zero skips**: BuildModel, GameSession, journal/event, cove/navigation and fixture-registry suites. New checks cover canonical blueprint bytes, ownership/condition/provenance omission, output-preserving decode failure, every truncated prefix, corruption, unknown schemas, duplicate ordinals, forged settings and unavailable content. An actual cooked cove design round-trip preserves configuration, quotes a 24-material new pontoon, reuses the eleven accepted parts, creates one paid addition request, and leaves the old draft unchanged on corrupt input. Importing an old design after removal of its loan cradle requests a paid replacement and its catalog price instead of resurrecting the entitlement.
- **14 existing UI lifecycle cases pass**, retaining construction/settings/price/pending/focus cleanup behavior.
- **8 isolated real IndexedDB checks pass**: successful transactions; an injected quota exception after the backup put with atomic rollback of both rows; two-tab revision conflict; validation before writing; duplicate-name rejection; 32-design capacity; stored payload corruption and valid-backup recovery; and explicit removal. Every write transaction in that test has observed durability `strict`. These use an isolated temporary database and deliberately injected storage faults, not actual disk exhaustion or a physical power-cut test. They do not mutate the game's session.
- **One complete nine-stage actual browser journey passes** in Chrome 152.0.7977.82 on hardware WebGPU. It saves an unlaunched twelve-part design with the winch disabled and propeller at 75%, duplicates and renames it, updates it without the cradle, restores the previous backup, reloads the page, reads the surviving library, and loads the spare. The fresh live boat remains eleven parts/48 material until Launch. A real download is exported and imported through the file chooser. A separately corrupted file is refused without changing the library. Launch buys the extra pontoon for 24, applies **1,875 N** thrust and disabled winch, and creates the actual twelve-part/1,155 kg hull. The player boards and sails it using real controls, then Leave drains the scene.

No screenshots were captured. Gameplay checks use mouse/keyboard input and read-only state; storage fault tests are explicitly separate. No game commands, inventory, simulation ticks or cargo success are injected. Browser reload verifies **design persistence**, not owned-world recovery. `exported-design.voxy-design.json` is the actual downloaded file from the successful journey.

## Failures corrected

The first native compilation found that `StrengthLimits` has no equality operator; the cove importer now compares its four scalar limits explicitly. The first two browser trials passed save/update/backup but attempted the post-reload Workshop click during the loading overlay's 500 ms fade. The second trial's DOM hit-test identified the transparent `#loading` element above the enabled button. The final test waits until the control actually receives hit testing. Gameplay and loading timing were not bypassed or accelerated. The final trial also observes strict IndexedDB durability. Failed logs/reports are retained beside the passing evidence.

## Reproduction and next work

```sh
nix-shell --run 'bazel build -c opt //:voxy_native //tests:voxy_tests'
nix-shell --run 'bazel-bin/tests/voxy_tests --gtest_filter="BuildModel*:GameSession*:SessionJournal*:SessionEvent*:CoveMovement*:CoveNavigation*:FixtureRegistry*"'
nix-shell --run 'EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j8'
/home/modkin/.nix-profile/bin/node scripts/test_salvage_preview.mjs
VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_REPORT=/tmp/voxys-designs-browser-r03.json VOXY_SMOKE_COVE_DESIGNS=/tmp/voxys-designs-journey-r03 /home/modkin/.nix-profile/bin/node scripts/smoke_integrated_wasm.mjs /tmp/voxys-designs-web-r03 salvage-cove
python3 tools/serve_wasm.py --directory /tmp/voxys-designs-web-r03 --port 38198
```

The package contains `web/` and the shipping `voxy_wasm.js`, `.wasm`, `.data` outputs. See `source.sha256`, `package.sha256`, `results.json`, native logs, `storage-tests.json` and browser reports for exact evidence. No complete gate passed; no gate commit was made.

Continue with full expedition checkpoint/journal encoding, including inventory/jobs/cargo, ownership and processed-sequence/retirement markers. Implement native flush/replace/backup storage and browser world storage, then restore certified physical state through SAVE-04. Loading a world must restore its balance instead of applying `coveStartingMaterials` again, retire old tokens, and preserve paid identities/settings. Native named-design controls remain required, as do picking/controller/full camera and other PLAY-02 items. Keep the goal and all G00–G14 requirements intact; do not count this blueprint library as durable banking or a complete game save. Continue playable progress and avoid screenshot loops.
