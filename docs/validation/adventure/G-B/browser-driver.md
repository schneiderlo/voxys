# Prepared ordinary-input home quest check

**Prepared; not executed.** This document and
`tools/validate_adventure_quest_browser.mjs` provide a bounded G-B verification
recipe. They are not evidence that G-B passes.

## Permission and setup

The current CUA interface cannot hold movement keys. Obtain the owner's explicit
permission to run this prepared Playwright UI driver outside CUA before execution.
`--authorized-ui-driver` acknowledges that permission; the flag does not grant it.
Do not launch another GPU test at the same time.

Use an already built, immutable browser package containing the current resident
assets, village, corrected movement handedness, first-in-category Starter room,
current UI and localhost-only `adventureObserve=1` readout. The package must have
its generated `package.json` hash manifest. Serve that exact folder on a dedicated
localhost port. Do not change the package or reroute the server during the run.

Supply the installed Playwright module entry and Chrome executable explicitly.
Use a new output directory whose parent exists. The driver creates a fresh
browser profile inside that output directory and refuses to overwrite it. It
never opens the user's profile, old G-A profile, or an existing world.

```bash
node tools/validate_adventure_quest_browser.mjs \
  --authorized-ui-driver \
  --playwright /absolute/path/to/playwright/index.mjs \
  --browser /absolute/path/to/chrome \
  --package /absolute/path/to/immutable/web-package \
  --url 'http://127.0.0.1:42752/index.html?experience=adventure' \
  --output /absolute/path/to/new/quest-browser-r01 \
  --seconds 900
```

Chrome runs headed at 1280 × 800 on X11, using hardware WebGPU. Keep the window
visible. This recipe does not disable the browser sandbox. Browser installation
or display errors stop the run; they must not be worked around by silently
changing its environment. The timeout accepts 180–1,200 seconds. The driver
adds `new=1` and `adventureObserve=1` to the initial local address. It rejects a
supplied existing `world` argument. The game's normal Save operation replaces
`new=1` with the generated world identity before the reload.

## Journey and input contract

1. Verify local package hashes and the server manifest. Start an empty world.
2. Check one short D movement against screen-right derived from the real
   view-projection matrix. Walking uses this right vector and the horizontal
   eye-to-target vector, rather than the old yaw/sign helper.
3. Approach Moss's actual admitted position. Accept **A Place to Return** through
   its named dialogue button.
4. Walk west along the meadow, clear of the new cottage at (−71, −901). Verify
   Starter room is visible in the B tray and first in all three catalog categories.
   Select it through an actual DOM button. Aim near (−85, −146.24, −895), raising
   the preview only when the game specifically requests it. Require a valid
   preview, 19 placed parts and exact payment of 70 wood, 16 stone and 8 scrap.
5. Walk through the doorway. Store, take and finally leave ten wood in the chest.
   Open the real workbench, verify the compass is locked, then craft/equip the
   field hammer for 4 wood and 2 scrap. Use the sheltered bed to register home.
6. Return to Moss and complete the quest. A real double-click and subsequent
   conversation must not produce a second recipe receipt or free items.
7. Craft the compass at the home workbench for 2 wood and 4 scrap. Confirm that
   a compass in the backpack is inactive, then equip it through the named menu.
8. Verify its actual horizontal bearing/distance to the beacon at (−63, −975),
   walk and verify changed distance, then check home/beacon target switching.
9. Save through Pause, reload the actual same-world URL, and compare all observable
   ownership and permanent quest progress exactly. Revisit Moss after reload;
   completion must remain unavailable and the receipt/items must remain unchanged.

Only Playwright keyboard down/up and real pointer actions operate the game.
Menu choices click the visible DOM button carrying the captured published
intent. No exported action, injected DOM event, state setter, save-byte edit,
teleport, camera setter or artificial inventory is used. Read-only `evaluate`
reads the inert `#adventure-observation` JSON and ordinary DOM focus/visibility.
Shift+Tab transfers focus to the canvas without accidentally placing a part.
Ground aiming projects actual world coordinates with the reported camera matrix;
an obscured point allows at most six ordinary right-drag adjustments.

The route is deliberately bounded and specific to the installed meadow content.
Each walk permits at most 240 input pulses and stops after nine stalled pulses.
Placement allows at most four requested height steps. Missing residents, blocked
routes, unexpected labels, invalid previews or failed saves fail with the last
observation; the driver does not invent alternate routes or restart automatically.

## Evidence and limits

The output retains `summary.json`, the exact `executed-driver.mjs`, the supplied
`package-sha256.json`, and the isolated browser profile. Stage observations,
ordinary input traces, active failed walks, last DOM state and console/GPU errors
are preserved. The owned browser closes on completion or failure. Keep failed
outputs; a changed package requires a separately named run after diagnosis.

There are **no screenshots, video, browser tracing or repeated views**. This is a
functional verification, not a visual review, performance benchmark, co-op test
or complete gate claim. Archive bytes and hidden slot metadata are not exposed
by the UI observation. Exact reload comparison covers every observed placed
part, chest material content, material total, equipped hammer/compass, registered
bed, met-resident mask and permanent quest receipt. The fresh journey creates no
other item kinds. It does not claim full save-byte equality or adversarial direct
command replay coverage. Repeated completion is checked through ordinary UI,
including the double-click guard and absence of another completion choice.

The prepared source adapts the proven ordinary-input/furniture route in
`docs/validation/adventure/G-A/browser-r05/executed-driver.mjs`; none of that old
driver is executed by this recipe. Its old sidebar selectors and incorrect
screen-right movement helper have been replaced. A syntax check alone does not
certify that the prepared journey is runnable or passes the changed scene.
