# Adventure pointer, placement and blueprint checkpoint

Recorded 2026-09-13 UTC. This is a bounded engineering and ordinary-browser-input
check. It does not complete the home quest, character, accessibility or G-B gates.

## Corrected behavior

GLFW cursor positions and browser canvas positions are logical pixels. Rendering
and the native HUD use framebuffer pixels. `adventurePointer()` maps each axis
using the actual logical and framebuffer extents, then derives NDC from that
same point. It rejects invalid sizes and non-finite/overflowing coordinates;
outside points remain outside rather than being clamped onto a valid target.
The application supplies actual window dimensions on native and canvas CSS
dimensions in WASM. No guessed device-pixel ratio is used.

HUD hit testing and world picking now share this conversion. Raw logical pointer
activity still controls aim ownership. Repeated action 15 notifications that the
canvas already owns input no longer erase the queued placement click; genuine
UI/world ownership transitions still clear inputs.

The Build tray now exposes **Starter room**, and that blueprint is the first row
in every building category. Its displayed cost comes from the actual 19-piece
layout. Selection only previews the room; acceptance remains a paid transaction.

## Passing evidence

- Four focused CPU cases use actual ray/solid and HUD hit functions at 1×, 1.5×,
  2× and rounded nonuniform framebuffer scales. They cover 100/125/150% HUD text,
  disabled rows, resize, outside points and invalid/overflow inputs. The old
  coordinate formula misses a clicked part in the regression case.
- Native and WASM executable builds pass. The retained first native build failed
  strict double-promotion checks in the HUD comparison; an explicit float HUD
  point fixed that error. Both the failure and successful build logs are retained.
- Ordinary CUA input in package `build-adventure-g-b/web-r05`, build ID
  `adventure-c40a300bc5ddd428`, verified the actual browser behavior below. Its
  canvas was 1280 × 720 CSS pixels and 1920 × 1080 framebuffer pixels.
- A center click at logical (640, 360) produced used framebuffer aim (960, 540).
  The prior package incorrectly used (640, 360) as framebuffer coordinates.
- A real game-view click placed a foundation at (−60, −145.92, −904), creating
  structure 3 / part 4. Parts changed 0 → 1 and stone 320 → 316. Delete removed
  that part; parts returned to zero and stone to 320.
- Selecting the first **Starter room** row showed 70 wood / 16 stone / 8 scrap.
  Parts stayed zero and stock stayed 640 wood / 320 stone / 80 scrap. The tray
  explained that using the placed bed establishes home.

`browser-observations.json` is a compact transcription of those observed checks,
not a full browser trace. No screenshots were taken for this checkpoint. The
local opt-in `adventureObserve=1` node mirrors the already-published validated UI
snapshot; it exposes no state-changing interface. Twenty-five DOM behavior cases
passed for the updated interface, including blueprint discovery and this observer.

The same test world was subsequently saved through the visible Pause menu with
**Saved adventure** confirmation before replacing the development server package.
The later village-clearance build is documented separately; this pointer evidence
remains attached to the exact r05 package that was exercised.

## Limits and next step

This proves browser click placement/refund and blueprint discovery, not a full
walking/furniture/quest journey. The two short CUA D-key taps did not establish
held-key movement acceptance. The independent
[movement projection cases](../movement-projection-r01/README.md) verify the
corrected camera-relative basis in CPU integration. Prepared ordinary-input
[browser](../../G-B/browser-driver.md) and [native](../../G-B/native-driver.md)
journeys await explicit permission under the current computer-tool restriction.
Physical controller, accessible menus, visual feedback and full quest/reload
acceptance remain open.
