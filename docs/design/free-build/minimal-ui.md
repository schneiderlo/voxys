# Minimal free-building interface

Owner direction, 2026-09-15: free LEGO-style building that feels good, with a
warm minifigure and the existing main landscape. Adventure is cancelled.
The first image concept was rejected as too busy. The owner explicitly chose
**a small hotbar, cycled with the mouse wheel**, with a soft colour palette
inspired by the light footprint of Tiny Glade's tools. A radial wheel, large
brand header and permanently open parts tray are not the requested direction.

## Presentation and controls

- Keep the scene dominant. Bottom centre: a short, horizontally scrolling strip
  of real piece thumbnails; the accepted selected piece has an outline and name.
- Wheel over the scene or hotbar changes piece in creative Build. Ctrl + wheel
  over the scene zooms. Walking keeps ordinary wheel zoom. R rotates, B switches
  Build/Walk. The full catalog remains available from tools / Tab.
- Colour is a small round button. Open it to show seven original swatches;
  choosing one closes it. Original keeps the authored material; other colours
  replace its base RGB while preserving material properties/textures. sRGB
  swatches convert to linear RGB before rendering. This changes NEW placements,
  not existing pieces. Repainting existing selections remains F2 work.
- A separate small tools button reveals rotate, undo last piece, removal,
  height, catalog and help. Undo currently handles only the most recent placement;
  do not imply multi-step undo/redo is implemented.
- Save and Menu stay at top right. Pause, comfort settings, controls and help
  use focused readable dialogs, with actual runtime menu intents.
- No health, resource counters, quest trackers, decorative fake tools or
  rendered text inside bitmap UI assets. The 1,024 part limit is shown in tools.
- Warm off-white surfaces, muted green selection, 44px minimum targets, visible
  focus rings. Large-text settings scale text; high contrast makes surfaces opaque.
  No UI motion is required. Portrait layouts keep the hotbar horizontally scrollable.

## Implementation contract

`ui/src/main.jsx` uses Preact and explicitly imported Lucide icons.
`ui/src/bridge.js` reads accepted C++ observations; it does not own a second
world state. Focus metadata is immediate, gameplay actions await the next
observation. Old menu tokens cannot activate repurposed rows. Canvas focus
releases UI input ownership; cleanup releases attachment and listeners.
Existing `web/adventure_preferences.js` and durable save transport are reused.
Unsaved protection persists if the UI observation fails.

`AdventureRuntime` owns selected paint, piece-wheel selection, mode and save
capabilities. New actions: 30 chooses paint in creative Build only; 31 opens
creative pause directly. Paint is already part of validated saved WorldPart
geometry. Rendering uses existing baseColorOverride; no shader rewrite or save
schema change is needed. Native retains its current HUD; this visual refresh
is the browser interface. Shared placement, paint rendering and scene wheel
controls also work in the native runtime.

The package contains local JS/CSS and existing mesh-derived SVG thumbnails;
there is no runtime CDN, UI image generation dependency or paid asset dependency.
The rejected generated image is not a shipped asset. Image generation is not
needed for these crisp, responsive interface controls.

## Handoff

Build/test instructions are in `ui/README.md`. Packaging checks hashes of both
source and generated bundle so old UI cannot silently be shipped. Continue F1
ordinary save/reopen and native interaction checks before declaring its gate.
Owner evaluation of building feel remains a separate required F2 gate.
