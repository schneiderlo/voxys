# Browser menus, settings and practice acceptance

**Result: passed.** All 138 focused Node cases pass. Six real-control stages passed in r05; r06 completed actual saved-history navigation and strict world/settings reload. The final r07 Resume and exact original-blueprint check passed both stages in **2.213 seconds**, with an outer passed verdict, no browser/GPU/console errors and normal browser/controller exits. The historical failed attempts below are retained, not recast as complete runs. The joint mandatory suite also passed; see the [parent checkpoint](../README.md) for its full result and publication record.

## What the player can use

The Cove has an Overview, Job, observed-landmark Map, Inventory, Settings, Help and Winch & lift page. A visible Resume button preserves the current Job page. Settings offer 100%, 125% or 150% text, contrast, control comfort, optional captions/tutorials and the shared 71-action binding editor. They use the core's complete atomic validator. Optional device metadata is written only after accepted changes and does not alter expedition save authority.

New, Continue and the saved-world list use the current origin and strict world loader. The list contains at most 32 confirmed save/load hints. It is navigation metadata, not proof that a save exists. The title reads only bounded, valid text-size and contrast preferences. Its controller input comes from the existing C++ backend.

Test uses the kept workshop design for a free temporary boat. Normal save, rewards, harbor installation, blueprint-library writes and paid Launch are unavailable. Return restores the original boat, economy and exact local workshop draft/history, paused. Save remains unavailable while that workshop is open. Leave has an owned confirmation. Captions do not replace critical job or save status.

## Focused checks and source identity

The direct Node runs retained below passed 138 cases: menu 15, preferences 8, saves 31, controller menu 33, design library 12 and preview 39. These are DOM-model checks. They cover atomic preference refusal and persistence, controller ownership/modal cleanup, resize focus retention, the paused Job Resume strip, confirmed history, practice permissions and truthful Save availability. They are not a measured-layout claim.

Run the six scripts individually from the repository root:

```sh
node scripts/test_cove_menu.mjs
node scripts/test_cove_preferences.mjs
node scripts/test_cove_saves.mjs
node scripts/test_controller_menu.mjs
node scripts/test_design_library_ui.mjs
node scripts/test_salvage_preview.mjs
```

[Source manifest](source-manifest.json) gives the exact final hashes, counts and original check paths. Compressed raw outputs are in `node/`. [Frozen package](checks/web-r04-package.json) includes all 27 top-level files. [Frontend identity](checks/frontend-identity.json) confirms all eight browser product files match that package. Its shared native-transition drain fix changes the built JS/WASM; the frontend is identical to web-r03. Root owns the full application/build inventory.

Independent review found and corrected Save being offered in a paused returned workshop and ordinary job guidance appearing during a test. Later reviews cleared the explicit Resume strip, resize ownership, preference/title behavior and final driver/cleanup changes. See the checkpoint's [source review](../independent-source-review.md). Review is source evidence, not a substitute for runtime acceptance.

## Actual browser evidence

All runs use Chrome's real Linux OS-controller backend, actual DOM/pointer/keyboard input, read-only core observations and blueprint export. There are no screenshots, recordings, navigator replacements or game-state setters. Each captured Cove state requires a strictly later submission and its actual GPU/physics completion under the same owner. Fresh runtime handles may differ after strict load; world, build, topology, ordered roots, owned parts, economy and exact blueprint are bound to the saved source.

| Attempt | Actual outcome and retained limitation |
| --- | --- |
| r01 | Startup harness expected the former title. It stopped before the journey or controller device. The exact expected title was corrected. |
| r02 | The real controller applied 150% text, contrast and Pause→O. The next resize check failed. Its first oracle did not retain the failed numeric bounds. |
| r03 | The same prefix passed. At 640×480 the panel fit, but focused Apply remained at y611.34–681.94. The driver waited a fixed 150 ms and did not observe delivery of the resize event, so its exact timing boundary is unknown. |
| r04 | The attempt to reuse r03's settings correctly failed: actual core preferences were defaults. The earlier runner force-killed Chrome, and the owned profile contained no preference entry. This is a retention boundary, not proof that normal product save/load lost preferences. |
| r05 | Six stages passed in a 39.273-second journey before an unnecessary title F9 check failed. Landing intentionally owns input and suppresses that global shortcut. The test had already completed real Leave and reached the title. |
| r06 | One strict saved-start capture passed. Actual Leave, controller saved-history choice and exact world/settings reload succeeded, including title 150% text/contrast. The 48.736-second attempt then failed because one O outside an owned menu opens Overview; the driver incorrectly expected it to resume immediately. Chrome closed normally with exit 0 and no fallback. |
| r07 | Passed both captures in 2.213 seconds: strict saved reload, then O opening Overview, explicit Resume, released menu ownership, View and byte-exact original blueprint. Outer status passed; browser and controller exited 0 normally. Earlier picker and all six r05 stages are reused. |

The isolated GPU-disabled real-browser DOM fixture uses installed HTML/CSS and controller/preference modules. After resize delivery, the existing handler moved Apply into y395.05–465.64 at height 480 while retaining focus. Its first fixture extraction failure and corrected result are retained in `checks/resize-dom-*`. This is DOM evidence, not another game journey.

The corrected actual r05 driver observes resize delivery and the following browser animation frame before checking the original strict bounds. Apply was y395.34–465.94; panel x276–628, y12–468; text was 19.5 px. Job, Map, Inventory and Help navigation also met their recorded focused-control/obstruction/horizontal bounds. These checks cover the named targets, not every possible viewport or every translated string.

The six r05 captures prove:

1. Actual controller settings and Pause remapping.
2. Large-text small-screen menu reachability and Resume preserving Job.
3. A kept ten-part, 945 kg cradle-removal draft without replacing the owned boat.
4. The actual temporary ten-part boat and blocked normal save/reward/library/Launch controls.
5. Return of the exact draft/history and original eleven-part, 1,035 kg boat, with 48 material, zero machinery and no paid part IDs.
6. Real bound F10 publication of the unchanged owned expedition after closing the workshop without Launch.

The final r07 capture retained world `3bf8ff3d2be7a6f8f15d6131c23e1246`, build 6, topology 0 and root 7, with eleven parts, 1,035 kg, 48 material, zero machinery and no paid part IDs. The current full preference object matched the previously applied settings, including Pause→O. Its exact original blueprint assertion passed after physically opening the workshop. Fixture submission 231 was joined by completed serial 241; physics tick 229 by completion 230. Fresh runtime body handles were admitted once and then held stable within each capture.

Both r06 and r07 observed requested `Browser.close` followed by the owned process's `close` event with exit 0. Neither used termination or force-kill fallbacks. r07's OS controller also exited 0 with empty stderr. The raw r07 outer report records empty `browserErrors`, `sample.errors` and console-error lists.

Original reports and runner logs are compressed under `runtime/`; [runtime summary](runtime-summary.json) extracts the verdicts and measured layouts. Exact executed scripts are archived for r05/r06/r07, with hashes in [artifact manifest](artifact-manifest.json). Earlier historical driver revisions were not copied at execution and are not presented as exact executable snapshots.

## Final continuation reproduction

Use the actual isolated profile created by r05, the same port/origin and its saved world. The profile is local test state and is not copied into this evidence folder. A missing/corrupt save must fail; do not manufacture a replacement or inject preferences. The normal strict restore may republish its lineage. This continuation makes no manual save and does not repeat Leave or the picker. The exact final input contract is recorded in [the source audit](checks/final-browser-continuation-review.md).

```sh
DISPLAY=:0 XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.8NQFV3 \
VOXY_SMOKE_GPU=gaming-x11 VOXY_TEST_CHROME=/opt/google/chrome/chrome \
VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=720 VOXY_SMOKE_PORT=43875 \
VOXY_SMOKE_TIMEOUT_MS=420000 VOXY_SMOKE_NO_SCREENSHOT=1 \
VOXY_SMOKE_KEEP_PROFILE=1 VOXY_SMOKE_PROFILE=build-cove-ux-r01/browser-profile-r03 \
VOXY_SMOKE_RESUME_WORLD=3bf8ff3d2be7a6f8f15d6131c23e1246 \
VOXY_SMOKE_COVE_UI_CONTINUE_REPORT=build-cove-ux-r01/browser-ui-r05/summary.json \
VOXY_SMOKE_COVE_UI_RESTORED_DESIGN=build-cove-ux-r01/browser-ui-r06/summary.json \
VOXY_SMOKE_COVE_UI=build-cove-ux-r01/browser-ui-r07 \
VOXY_SMOKE_REPORT=build-cove-ux-r01/browser-startup-r07.json \
node scripts/smoke_integrated_wasm.mjs build-cove-ux-r01/web-r04 salvage-cove
```

The completed final run had a 120-second driver ceiling after startup. Its Resume uses the exact visible button and waits for both authoritative Running and released DOM/controller ownership before View. The earlier corrected landing Confirm stays held until an actual title frame or the exact target URL is observed; release never waits on a destroyed page's counter. Retained Chrome profiles now request `Browser.close`, await the owned process's `close` event, then use bounded termination/kill fallbacks if necessary. An unconfirmed/abnormal close fails the outer run. Graceful close alone is not a persistence assertion.

The browser practice stay does not prove sailing, quitting during a test, or native menu behavior. Separate native evidence covers quitting during a test and native menus; these UI journeys do not establish sailing during practice. No audio system, complete translation catalog or accessibility certification is claimed.
