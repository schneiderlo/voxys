# Corrected shared game HUD

The design follows the approved [Clear adventure reference](../hud-concepts-20260924/01-clear-adventure.png), with the [Social playground radial selector](../hud-concepts-20260924/04-social-playground.png) during building. The correction adds dimensional toy artwork, a numbered piece tray, lighter framing, DM Sans, a builder portrait and a real terrain minimap.

**Current game screenshots:** [building](live/build.png), [piece selector](live/catalog.png), [colour wheel](live/palette.png) and [walking](live/explore.png).

These show the running WebAssembly game in hardware Windows Chrome at 1600 × 900, using packaged build `free-build-3ef92387ac1372ca`. The capture leaves every DOM element intact and uses real pointer clicks on the runtime's GPU hit boxes. Ten actions verified the red and white presets, catalogue selection, blue paint and return to walking. All actions preserved the world part count. Rendered controls matched their accessibility peers, numbered shortcuts and selected-slot state. The run reached frame 354 with no device loss, uncaptured GPU errors or console exceptions. The real map remained cached across these stationary interactions. See the [capture report](live/capture.json), [live-check log](checks/live-browser.log) and [reproduction harness](checks/capture-live.mjs).

**Native visual proofs — GPU-rendered HUD over a recorded game background:**

- [Building](build.png)
- [Radial piece selector](catalog.png)
- [Brick category](catalog-bricks.png)
- [Colour wheel](palette.png)
- [Walking](explore.png)

These five 1600 × 900 PNGs come from the final v3 native offscreen test. It uploads the unchanged [clean gameplay screenshot](../hud-concepts-20260924/game-no-ui.png), draws `AdventureHudPath` with the shipped shaders, and reads back the GPU texture. They are **not live native gameplay captures** or generated HUD concepts. Their menu content is a deterministic fixture; the minimap samples the installed heightfield at a fixed position and displays zero placed pieces. [Capture counts](renderer-capture.tsv) and [image hashes](native-validation.json) record the evidence.

The piece art and portrait are CPU renders of the actual installed meshes, including their authored normals, materials and node transforms. The atlas also contains the licensed DM Sans font at weight 550. Only the Ride, Cannon and Save action icons use generated artwork; they do not replace construction models. Their original RGBA outputs and exact prompts are retained, and no specific image-model version is claimed. See [art provenance](../../../../data/art/adventure-hud-r01/README.md), [atlas review](../../../../data/art/adventure-hud-r01/review.png), [generated controls](../../../../data/art/adventure-hud-r01/generated-controls/README.md) and [font provenance](../../../../data/fonts/adventure-hud/README.md).

The six starting slots select real piece-and-paint combinations:

| Slot | Piece | Paint |
| --- | --- | --- |
| 1 | Brick 2 × 2 | Red |
| 2 | Brick 2 × 4 | White |
| 3 | Floor | Original wood |
| 4 | Foundation | Green |
| 5 | Doorway | White |
| 6 | Hinged door | Original materials |

Number keys select the displayed slots. Choosing another piece or colour updates the active slot. Placed pieces save their actual paint; the tray itself is a session preference and starts fresh when a world loads. The catalogue and colour wheel appear while building. Rotation, undo, height adjustment, removal, help, walking, motorbike, cannon and save retain their game commands.

The portrait represents the actual builder. The piece count comes from placed parts. The north-up minimap uses the world's heightfield, water level and accepted building footprints; its marker and compass follow the camera bearing. Navigation stays visible in the piece catalogue and colour wheel, but small viewports and pause/help/settings menus hide it. Health bars, quests, chat, party members and multiplayer from the concepts are not represented as implemented creative features.

Native and browser builds share the C++ layout, GPU atlas, shaders and hit boxes. The browser adapter supplies accessibility peers, focus, announcements, preferences and unsaved-world protection; it does not draw a second HUD. Layout uses logical pixels before framebuffer scaling. See [UI architecture](../../../../ui/README.md).

Native verification passed: **41 tests in CPU-only runs, 5 tests in GPU-filtered runs, and 1 creative runtime test**. One of the five GPU-filtered tests is a CPU guidance check whose name matches the filter; four actually create GPU contexts, including the capture test. The final v3 run rebuilt the native application and checked the HUD; unchanged Cove, minimap and creative-runtime behavior retain their v2 evidence. All 47 checks passed without skips. [Native validation](native-validation.json) links the retained logs and XML reports.

The checks cover bounded layouts, large text, high contrast, density scaling, exact input identities, quick-slot paint, minimap caching, GPU upload reuse, stale geometry, pointer ownership, placement and exact save/restore. Controller paths use synthetic input, not a hardware-controller session. The cannon integration test was **not rerun**; its earlier GPU-watchdog failure remains in the [previous validation record](../shared-hud-20260924/checks/native-cannon.log).

The browser UI's [30 tests](checks/ui-tests.log), [Chromium adapter check](checks/browser-adapter.log), [WebAssembly build](checks/wasm-build.log) and live game interaction check pass. The adapter check uses a synthetic engine; the live captures above use the real engine. Earlier browser screenshots belong to the [superseded HUD revision](../shared-hud-20260924/README.md).

These are functional, input and rendering checks. They do not establish an FPS improvement or long-session stability result.

To reproduce the native images after building `//tests:adventure_hud_layout`, run from the repository root with a working native graphics driver and the decoded installed 8192 × 8192 terrain:

```sh
VOXY_HUD_CAPTURE_BACKGROUND="$PWD/docs/design/free-build/hud-concepts-20260924/game-no-ui.png" \
VOXY_HUD_CAPTURE_DIRECTORY="$PWD/docs/design/free-build/shared-hud-correction-20260924" \
VOXY_HUD_CAPTURE_TERRAIN=/path/to/decoded-terrain.r16 \
bazel-bin/tests/adventure_hud_layout \
  --gtest_filter=AdventureHudGpu.CaptureSharedRendererOverCleanGameScreenshotWhenRequested
```

Capture is opt-in. Ordinary tests do not write these PNGs. The atlas can be checked independently with `python3 tools/adventure_assets/bake_hud_art.py --check`.

To reproduce live screenshots with Node 22 or later and a packaged build:

```sh
node docs/design/free-build/shared-hud-correction-20260924/checks/capture-live.mjs \
  --chrome=/path/to/chrome --site=/path/to/packaged-build \
  --output=/path/to/new-capture-directory --width=1600 --height=900 --dpr=1
```
