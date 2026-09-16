# Free-building browser UI

Preact components with Lucide icons; C++ remains authoritative for game state.
Creative `?experience=build` uses this bundle. Explicit legacy routes keep their
existing UI. The accepted design is in `docs/design/free-build/minimal-ui.md`.

From the repo root:

```sh
npm --prefix ui ci
npm --prefix ui run build
npm --prefix ui test
```

Build writes `web/build_ui.js`, `web/build_ui.css`, license notices and a source/
output hash manifest. Commit source, package-lock and these generated outputs
together. `scripts/package_adventure_preview.py` refuses stale bundles. All
runtime dependencies are bundled locally; installation requires npm access.
Use the locked versions, not unversioned CDN scripts.

Tests run the actual compiled bundle in jsdom with a clearly synthetic C++
bridge fixture. They check stale intent refusal, one pending gameplay action,
nonblocking focus metadata, canvas ownership, cleanup, unload protection,
actual piece/colour commands, on-demand palette and modal focus. These tests
are not proof of rendering or ordinary in-game placement/save.

The bridge polls the engine every 100ms. Its local state is presentation only.
IDs 2/3/5/6/8/etc. use the existing adventure host bridge. Action 30 sets new-piece
paint; 31 opens creative pause. Shape IDs and wheel ordering match C++.
Selected colours are packed sRGB 0xRRGGBB; zero is the authored-material sentinel.
Do not turn arbitrary save metadata into creative permissions.

Reference documentation: [Preact](https://preactjs.com/guide/v10/getting-started/)
and [Lucide for Preact](https://lucide.dev/guide/preact).
