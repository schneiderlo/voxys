# Shared game HUD

**Superseded:** the [corrected shared HUD](../shared-hud-correction-20260924/README.md) is the current implementation. This page retains the earlier screenshots and validation history; its browser results do not verify the newer correction.

The implemented HUD uses the game’s GPU renderer in both native and browser builds. It follows the clear framing of concept 1, with the build selector and colour wheel drawn only while building.

- [Walking — live game](live/explore.png)
- [Building — live game](live/build.png)
- [Pieces — live game](live/catalog.png)
- [Colours — live game](live/palette.png)

These four screenshots show the **running WebAssembly game in hardware Chrome**, at 1280 × 720. No UI was hidden or added for capture. Eight real pointer actions opened the catalogue, selected a wall, applied coral paint, reopened the colour wheel, and returned to walking. All clicks preserved the part count; semantic browser buttons matched the rendered hit boxes. The successful run reported no device loss, uncaptured GPU errors, or console exceptions. [Capture report](live/capture.json).

The native renderer also has fixed visual checks: [walking](explore.png), [building](build.png), [pieces](catalog.png), [bricks](catalog-bricks.png), and [colours](palette.png). These are **real GPU renders of the implemented HUD**, using `AdventureHudPath` and the shipped shaders. Their background is the unchanged [clean gameplay capture](../hud-concepts-20260924/game-no-ui.png). A native offscreen test uploads that image, draws the HUD, then reads back the final GPU texture. These five fixtures are not live native gameplay captures or generated concept artwork. The centre pixel is checked against the source image in walking and building views.

The content fixture uses the actual piece geometry and runtime action mapping. It does not invent health, quests, chat, a minimap, or online players.

Reproduce the images from the repository root after building `//tests:adventure_hud_layout`:

```sh
VOXY_HUD_CAPTURE_BACKGROUND="$PWD/docs/design/free-build/hud-concepts-20260924/game-no-ui.png" \
VOXY_HUD_CAPTURE_DIRECTORY="$PWD/docs/design/free-build/shared-hud-20260924" \
bazel-bin/tests/adventure_hud_layout \
  --gtest_filter=AdventureHudGpu.CaptureSharedRendererOverCleanGameScreenshotWhenRequested
```

Set the same graphics driver environment used for the native test run when needed. Capture is opt-in; normal test runs do not write these artifacts. [Renderer counts](renderer-capture.tsv) and [validation details](validation.json) accompany the images.

Validation passed on native Vulkan with llvmpipe: 36 layout and GPU tests, plus the opt-in capture test. The checks cover 320 × 240 through desktop layouts, 100–150% text sizes, high contrast, non-overlapping 44px input targets, every selected piece and colour, all radial category sizes, GPU upload caching, and clearing obsolete geometry when walking. The captures are visual evidence, not a hardware gameplay performance benchmark.

Runtime and input verification is recorded in [integration-validation.json](integration-validation.json). Native and WebAssembly builds pass. The native creative gameplay test passes with 2× DPI pointer input, keyboard and controller selection, colours, undo, and exact save/restore. The browser adapter passes 29 unit/component checks plus a Chromium interaction check using a synthetic engine.

The native cannon integration test hit the existing GPU tick watchdog during imported-world admission, before reaching the new controller assertions. Its failure is retained in [the test log](checks/native-cannon.log); cannon integration is not counted as verified.

The first browser attempt hit an Intel D3D12 device hang after 17 frames. A later attempt exposed a capture-script timing error: it clicked before the loading overlay disappeared. The corrected harness waits for the loading screen to leave and verifies canvas pointer ownership before clicking. The final run passed; prior attempts remain recorded in the validation details. This is an interaction check, not an FPS or long-session stability benchmark.

The live-browser harness uses real pointer clicks on the running game's published GPU hit boxes. It checks semantic peers, minimum input target size, current menu intents, and that HUD clicks do not place a brick. It leaves the DOM visible and captures the canvas as rendered. With Node 22 or later and a packaged build:

```sh
node docs/design/free-build/shared-hud-20260924/checks/capture-live.mjs \
  --chrome=/path/to/chrome --site=/path/to/packaged-build \
  --output=/path/to/captures --width=1280 --height=720 --dpr=1
```
