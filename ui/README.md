# Shared building HUD

The default creative game uses `AdventureHudPath` in both native and WebAssembly.
Its layout, artwork, colours, hit boxes and shaders are the same on both
platforms. The visual target is **Clear adventure**, with the **Social playground**
radial piece selector shown during building. Review images and verification are in
[shared-hud-correction-20260924](../docs/design/free-build/shared-hud-correction-20260924/README.md).

The shared 1024 × 512 RGBA atlas contains CPU-baked pictures of all 15 installed
pieces, a portrait of the real builder, DM Sans at weight 550, and three generated
Ride/Cannon/Save action icons. The piece renders preserve actual mesh geometry and
materials; generated controls are decorative action art. Font, original images,
prompts and reproduction details are retained in
[the art package](../data/art/adventure-hud-r01/README.md). The menu symbol remains
a code-drawn glyph. No font rasterization or model rendering happens for these
icons during gameplay.

Keys 1–6 select the displayed tray slots. The initial presets are a red 2 × 2 brick,
white 2 × 4 brick, original wood floor, green foundation, white doorway and original
hinged door. Each slot carries a real piece kind and paint value. Catalogue/colour
choices update the active slot; placed parts retain their paint in saves. The tray
is a session preference and resets when a world loads.

The layout uses logical pixels, then scales geometry and hit boxes to the actual
framebuffer. High-DPI displays retain readable text and 44-pixel input targets;
game rendering keeps its existing resolution policy.

`web/index.html` installs `VoxyBuildUI.installShared`. The small browser adapter in
`src/shared.js` provides preferences, unsaved-world protection, status announcements
and semantic keyboard/screen-reader buttons. Those transparent buttons mirror the
GPU's exact published hit boxes. They do not receive pointer input: the game owns
mouse hit testing on both platforms. Browser focus outlines are the only visual DOM
addition. There is no second browser layout to keep in sync.

C++ remains authoritative. Menu actions use the original opaque intent and captured
menu token; the bridge re-reads accepted state before dispatch. Keyboard focus is
retained through asynchronous menu changes. Canvas Tab enters the catalogue before
Emscripten's document listener consumes it; Escape returns to the world. Native
keyboard and controller routing stays in the game.

```sh
npm --prefix ui ci
npm --prefix ui run build
npm --prefix ui test
npm --prefix ui run test:shared-browser -- \
  --playwright=/path/to/playwright/index.mjs \
  --browser=/path/to/chrome
```

The build writes `web/build_ui.js`, `web/build_ui.css`, third-party notices and the
source/output hash manifest. Keep generated output and source together.
`scripts/package_adventure_preview.py` rejects stale bundles. Release URLs include
the build ID so an old browser bundle cannot silently accompany a newer game.

The earlier Preact/Lucide DOM interface remains available as `VoxyBuildUI.install`
for historical component previews (`test:browser`); it is not the default game's
visible HUD. Its tests are retained alongside the shared adapter's ownership,
focus, stale-intent and cleanup tests. The explicitly routed legacy adventure
experience still uses `VoxyAdventureUI`.

Creative artwork, text and analytic rounded shapes use bounded quad buffers.
The legacy adventure thumbnail path retains its bounded triangle buffer.
Unchanged HUD content reuses uploads. Navigation uses a separate quad buffer so
camera movement does not invalidate the piece tray. Its north-up minimap is
sampled from the real heightfield, water level and accepted building footprints,
with the camera bearing shown by the marker and compass. Terrain rasterization is
cached across player ticks; geometry changes update the accepted-footprint overlay.
Small viewports and pause/help/settings menus skip map work. These caching properties are
covered by functional tests; they are not an FPS benchmark.

The builder portrait, placed-piece count, compass and terrain minimap are real
creative HUD features. The generated MMO concepts remain design references;
health bars, quests, chat, party members and online play are not claimed as
implemented creative features. The current preview page distinguishes native
overlay fixtures from live browser captures and records which checks passed.
