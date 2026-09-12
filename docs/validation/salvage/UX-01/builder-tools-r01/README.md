# Cove builder and blueprint tools — 2026-09-12

The owner's selected work is PLAY-02 and the workshop/blueprint tools of UX-01. The starting published revision is `2353dae2bf09d5570ea57c46dc3de055b8b97fd6`. D46 in GAME_IMPLEMENTATION_TODO.md records scope, action ownership and the fact that the former four-hour session has ended. No new deadline was requested.

Native and browser component acceptance and the required repository suite are complete. PLAY-02 is accepted. The UX-01 tools component is accepted while its MECH-01/G04 prerequisites remain open. Normal-hook publication uses the commit containing this report; no broad game gate is claimed.

[Player controls](CONTROLS.md) explains every entry point, editing operation, camera gesture, named-design operation and current limit without this conversation.

## Delivered behavior

- Native and browser keyboard/mouse/controller operate the same bounded workshop commands. C++ alone polls the actual platform controller. The browser receives semantic menu edges through a stable public JavaScript boundary.
- Select groups; move, rotate, snap, remove, paint and configure supported selections atomically. Copy, mirror supported symmetric bricks, or replace definitions in distinct dynamic slots. Invalid placement has a specific readable reason. A refused edit changes neither the design nor stock.
- Keep, Undo and Redo retain exact selection and design history, bounded to 32 entries. New kept work clears redo. Continuous placement still uses the existing real socket/compiler path and excludes its spare ghost from export and Launch.
- The native in-game menu accepts a mouse, keyboard or standard controller. It includes the on-screen naming keyboard, part/tools pages, configuration and camera controls, and the full named-design library. Browser controls expose the same game commands and owned naming/deletion dialogs.
- Native Linux designs use a separate asynchronous durable library. Browser designs retain the existing strict IndexedDB transactions. Both exchange the same validated `.voxy-design.json` envelope: version, UTF-8 name and checksummed SVBP bytes. Save, update, copy, rename, backup restore, delete, export and import preserve the separation between a design and owned physical parts.
- Opening/closing menus, text input, focus loss and controller reconnect neutralize held actions. Release to neutral rearms the controller. Confirm never auto-repeats; menu navigation does. Fast keyboard chords retain the modifiers held when the key was pressed, even if released before rendering.

## Implementation map

| Responsibility | Sources |
|---|---|
| Atomic draft, selection, mirror/replacement and diagnosis | `src/game/expedition/cove_workshop.{hpp,cpp}`, `cove_boat.{hpp,cpp}` |
| Strict library names, collection and exchange | `src/game/expedition/design_library.{hpp,cpp}` |
| Linux worker/store/file exchange and native menu | `src/engine/platform/native/design_library.{hpp,cpp}`, `workshop_menu.{hpp,cpp}` |
| Normalized controller and keyboard/focus/text lifetime | `src/engine/platform/gamepad.hpp`, `input.hpp`, native/WASM input and native window |
| Shared action dispatch, quote/refit and live input ownership | `src/app/application.{hpp,cpp}`, native entry/save host |
| Native bounded text/menu rendering and exact pointer layout | `src/render/cove_hud.{hpp,cpp}` |
| Browser controls and owned dialogs | `web/controller_menu.js`, `design_library.js`, `salvage_preview.js`, CSS and HTML |

Actions 300–309 are select-all, primary-only, copy +32 X ticks, mirror X=0, mirror Z=0, replace with catalog, redo draft, rotate X, rotate Z and toggle next selection. These extend the existing action ABI without renumbering it. The legacy observation field `controller` remains a string; new device state is under `gamepad`.

No editor or library operation can mint ownership IDs or award materials. Launch independently recalculates and admits the authoritative refit. Native save paths honor the explicit expedition root. A failed/uncertain library write is never acknowledged as saved. Native Windows durable storage remains an existing missing backend.

## Verified checks

| Check | Result |
|---|---|
| New group editor | 12 cases pass; affected final separated-history case and 13 existing workshop regressions also pass |
| Shared / native design library | 6 + 8 cases pass |
| Native menu and pointer scaling | 7 cases pass, including 2× pointer layout |
| Native layout | Menu bounds, selected rows and long import-path/refusal checks pass within the unchanged 768-quad budget |
| Controller / keyboard event modifiers | 5 cases pass |
| Browser UI | 21 controller cases, 11 library UI cases and 36 salvage UI assertion groups pass |
| Actual browser tools | [9 stages pass in 41.574 seconds](browser/README.md); 12 parts, 993 kg, IDs 35/36 and 42 material retained after real reload |
| Save recovery / history boundary | 138 native and 199 configured WASM cases pass; [independent acceptance](../../DATA-05/independent-review-r01/README.md) |
| Native / WASM application builds | Both final r05 builds pass (WASM output is byte-identical to r04); final frozen browser package is web-r03 |
| Actual native tools | [12 completed construction/naming stages plus 9 final continuation stages](native/README.md); exact blueprint, library and owned boat after restart; final 12 parts, 969 kg, IDs 35/36 and 44 material |
| Final browser package startup | Passed, no browser errors; the recovery-only browser delta is checked by 199 configured WASM cases |
| Required full repository suite | **2,137 game passes, 3 skips, 4 disabled; terrain import 10 passes / 1 skip.** Both targets executed successfully in 1,145.590 seconds. [Exact result](checks/required-r01.json), [runner log](checks/required-r01.log), compressed full game log/XML and terrain log are retained. |

## Evidence and reproduction

Focused source and integration reviews are [core handoff](HANDOFF.md), [library review](native-library-review.md), [Application/menu review](application-menu-review.md) [native handoff](native-tools-handoff.md) and [event-time modifier review](event-time-modifier-review.md). Earlier lane notes deliberately distinguish their then-pending app acceptance. The final results below supersede those pending notes only where actual evidence exists.

Focused commands run under the installed Nix environment:

```sh
bazel test //tests:cove_workshop_editing //tests:design_library //tests:native_design_library //tests:native_workshop_menu //tests:gamepad_controls //tests:cove_hud_layout
node scripts/test_controller_menu.mjs
node scripts/test_design_library_ui.mjs
node scripts/test_salvage_preview.mjs
```

The combined regression suite is required by the normal commit hook:

```sh
bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test
```

Actual-control drivers are `scripts/validate_cove_builder_tools.mjs` (dispatched by `scripts/smoke_integrated_wasm.mjs`) and `scripts/validate_native_builder_tools.py`. They use a temporary Linux uinput standard controller and real window/browser input. `scripts/cove_virtual_gamepad.py` destroys that temporary device at exit. Read-only observations and accepted exports verify state; no game setters, navigator overrides, direct success commands or modified world saves are used. This is automated OS-controller evidence, not a human fun test or a physical-controller usability review. Run the hosts sequentially because they share focus and controller devices. All startup screenshots are disabled with `VOXY_SMOKE_NO_SCREENSHOT=1`.

Native command (new output and storage directories required):

```sh
python3 scripts/validate_native_builder_tools.py --binary /absolute/voxy_native --output /absolute/new-journey --storage-root /absolute/new-storage
```

The browser driver is selected with `VOXY_SMOKE_COVE_BUILDER_TOOLS=/absolute/new-journey` and `VOXY_SMOKE_GPU=gaming-x11`. Use the exact runner command retained in the final checks and frozen package hashes. The browser performs one world/library reload; native performs one process restart. No old screenshot/sailing matrix is repeated.

## Resolved findings

1. Native high-DPI pointer coordinates originally disagreed with rendered menu rows. The menu now uses exact framebuffer dimensions plus explicit logical-pointer scaling; the 2× pointer case passes.
2. Native menu content previously refreshed at ordinary HUD cadence. Active and closing menu frames now refresh immediately, so displayed rows and click actions agree. Small viewports reserve sufficient room for the selected row and footer.
3. Browser group replacement and asynchronous library focus restoration were corrected during review. A pending disabled action cannot redirect Confirm to another action.
4. The first actual browser controller check opened the workshop but could not open its menu. Closure had renamed the public bridge functions and event keys. All external lookups and keys are now explicitly quoted; generated release code retains the agreed names. The failed run is preserved. No GPU/browser error was reported in that run.

5. The second browser attempt passed menu entry, controller pan/zoom, placement and overlap feedback, then exposed swapped X/Y codes in the temporary OS-controller driver. The helper now matches the actual Linux/GLFW Xbox mapping. The game and frozen web-r02 package were unchanged for the next attempt; the failed evidence remains retained.

6. Independent DATA-05 review found and fixed a preexisting valid-save refusal after a reward fills either currency while a future history refund remains. A bounded private history balance now allows positive hypothetical carry while retaining underflow, exact catalog value, identity and accepted-balance checks. Live accounts and actual replay stay checked 64-bit values. [Independent review, root correction review and 138 native / 199 configured WASM checks](../../DATA-05/independent-review-r01/README.md) complete DATA-05. Native-r04 / web-r03 incorporate this recovery delta after the passing browser builder journey. The final native-r05 also fixes import-path wrapping; its matching WASM build is byte-identical to frozen web-r03.

7. The native import journey exposed a clipped folder path. The subtitle now wraps to two lines; the actual long-folder/refusal layout and existing selected-row bounds pass focused CPU checks. The continuation uses the persisted named design and exported bytes, skipping completed construction/name loops.

## Remaining parent requirements

The native attempt details and final acceptance are recorded in the [native report](native/README.md). The earlier 90 ms Keep press was not acknowledged; its cause is unproven. The subsequent deliberate 200 ms single-press protocol retains the same required outcomes and never retries an action.

This change retains the existing 96 scene slots and tested 64-brick workshop. MECH-01 still requires diverse 256-part assemblies. G04 retains its separate progress/storage acceptance work. Thus the UX-01 controls component can be verified without falsely checking MECH-01, G04 or the UX-01 parent. COM/flotation/stress overlays are optional and are not implemented. General rebinding, accessibility/human sessions, final art, both cargo jobs and other game gates remain elsewhere in the plan. No screenshot or final-art acceptance is claimed.

## Publication and continuation

The normal repository commit hook remains enabled. Publication uses the containing commit on main, with its remote SHA verified by the parent task. The verified local package is `build-workshop-tools-r01/web-r03`; native is `build-native-save-host/bin/voxy_native` with the native-r05 hash. Reuse the completed control/library evidence when advancing MECH-01 and G04, and repeat only checks affected by later changes or a concrete failure. No further screenshot or builder-loop repetition is needed for this component.
