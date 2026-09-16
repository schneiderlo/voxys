# Compact browser UI checkpoint

2026-09-15. The owner chose a small wheel-cycled hotbar and an on-demand soft
colour palette, after rejecting the initial large tray concept.

Final package: `build-free-build/web-r04`, `free-build-bbc98fd5c35efbd0`.
Preview: http://127.0.0.1:42764/index.html?experience=build&telemetry=0
52 package file hashes verified. Local server switched from r03 to r04 on the
same port so the saved test world remains accessible. No source world migrated.

Passed changed-behaviour evidence:
- Four jsdom cases exercise the actual generated Preact bundle and C++ bridge.
- Native build and full-installed-landscape runtime integration: wheel select,
  Ctrl-wheel retains selection, valid/invalid colour commands, pause/return,
  real placement with paint, undo and exact painted archive round trip.
- WASM build, one compiler lane. Existing compiler warnings are in the log.
- Ordinary browser on r03: colour popup -> Coral -> canvas placement -> Save ->
  confirmed Saved build -> pause -> comfort settings -> reload -> Saved build
  loaded and visible coral brick. Full wheel step changes Brick 2x4 to Floor.
- Narrow 483×998 view: hotbar, save/menu fit; terrain/minifigure dominate screen.
  Two distinct scene views inspected (empty start and persisted coloured brick),
  not a repeated screenshot matrix.

Final r04 fixes the new-world Save badge only, with an explicit component
assertion: dirty=false is not enough to claim a first durable save. Runtime
and WASM binary are identical to the ordinary r03 test. Final r04 was also
opened on the same saved world and confirmed Saved build loaded with the compact
controls. The browser tab is marked as a deliverable. Seven palette colours
apply to new pieces; repainting existing pieces is still open. No generated
raster asset ships in this UI. Existing thumbnails come from piece meshes.

Not a completed F1/feel gate: ordinary native interaction and the full browser
rotate/remove journey remain open. No gate commit yet. Multi-step undo/redo,
repainting selections, larger kits, improved snapping and owner approval of
building feel are separate remaining work. This checkpoint refreshes browser
presentation; it does not claim a native HUD redesign or finished terrain art.

See `results.json` for the tested sources and `ui/README.md` for reproduction.
