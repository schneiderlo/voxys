# Brick paint and free repaint

Checkpoint prepared 2026-09-12 during the 03:27:33–07:27:33 UTC work window.
Native and WASM builds, focused CPU/GPU checks, and both native and browser
journeys pass. The final mandatory suite also passes as recorded in the
[combined checkpoint](../../checkpoints/2026-09-12-painted-cove.md), which owns publication.
The preceding published checkpoint is `40e84269`. No screenshots were taken.

## Player controls

Choose an individual 1×2, 2×2 or 2×4 brick, then choose a paint color. The active
brush carries an explicit color through repeated placement and size changes.
Select/Escape discards its unused preview. Only kept bricks cost materials,
receive ownership, appear in exports and survive a save.

- Browser: use the eight labeled swatches. A brush choice returns keyboard
  focus to the canvas. Click to place; Select stops the brush.
- Native: B opens Workshop; 1/2/3 choose size; Y cycles paint; R rotates;
  Escape stops; U undoes; E keeps an edit; Enter launches.
- To repaint an existing brick, select it, choose a color, then Keep and Launch.
  Repainting is free and preserves part ID, condition, provenance, settings,
  placement, connections, mass and inventory. Machinery is not paintable.

| Index / action | Name | RGBA8 |
|---|---|---|
| 0 / 200 | Original | 255, 255, 255, 255 |
| 1 / 201 | Cream | 239, 235, 217, 255 |
| 2 / 202 | Teal | 35, 145, 137, 255 |
| 3 / 203 | Blue | 50, 108, 190, 255 |
| 4 / 204 | Yellow | 239, 191, 54, 255 |
| 5 / 205 | Red | 195, 55, 47, 255 |
| 6 / 206 | Orange | 234, 119, 39, 255 |
| 7 / 207 | Charcoal | 56, 65, 72, 255 |

Original explicitly restores authored appearance. An absent brush preference
preserves exact stored paint, including custom colors outside this palette.
Custom paint reports `paintIndex: null`, `paintName: "Custom"`; no swatch is
falsely selected. Reopening a saved world does not invent a brush preference.

The shared palette is `src/game/expedition/brick_paint.hpp`. Workshop JSON adds
`canPaint`, `paintIndex`, `paintName`, `paint` and `brushPaint`. Missing/false
capability or a pending application disables swatches. Refused actions do not
optimistically change selection. Placement status reserves three readable lines
and paint guidance two, with overflow scrolling; changing text cannot move a
control between pointer press and release.

## Rendering and executed checks

Per-instance paint replaces authored RGB before selection tint. It decodes sRGB
once and preserves textures, opacity and other material factors. Original uses
a disabled override. Instances are 112 bytes; the two instance paths add 16,384
bytes. **Fixed requested storage is 119,336 bytes**, including both copies of
the guide mesh, inside the unchanged 128 KiB reservation. The draft's 117,400
figure omitted one guide mesh and is superseded.

| Evidence | Actual executed result | Scope |
|---|---|---|
| `checks/paint-ui-test.log` | 25 cases pass | 23 existing, two new paint UI cases |
| `checks/focused-r01-test.xml` | 8 cases pass, zero skips | Four gameplay, one accounting, one registry, two GPU fixture cases; initial propeller candidate |
| `checks/materials-r01-test.xml` | 4 cases pass, zero skips | One paint CPU case, three real GPU material/pose/shadow cases |
| `../../LOOK-01/machinery-r01/checks/final-machinery-r01-test.xml` | 2 cases pass, zero skips | Only affected registry/admission cases rerun after the propeller correction |
| `checks/native-build-r01.log`, `checks/wasm-build-r01.log` | Both builds pass | Final integrated native and browser packages |
| `native/summary.json` | 10 stages pass in 10.89 seconds | Actual controls, save and process restart |
| `browser/summary.json`, `browser/startup.json.gz` | 7 stages and outer startup pass | Actual browser controls, save/reload and exact blueprint |

The broad filter in `focused-r01` named mesh-path tests, but its selected target
contained only eight matching cases. Those four mesh-path cases actually ran in
the separate `materials-r01` target; they are not credited to the first run.
Raw logs/XML are retained. GPU tests used AMD Radeon 890M / RADV STRIX1 / Vulkan.
Material checks cover lit/unlit × opaque/mask/blend, with zero pixel mismatches
against independently painted material and a visible difference from unpainted
output. Live-root paint has zero differing bytes from the static reference.
No images were saved. Final machinery admission records 10,740,488 owner bytes
and 78 color draws; see the linked machinery evidence.

## Actual native journey

The fresh world removes the starter cradle, places one Teal 2×4 on the real
cleared deck, preserves Teal through the next ghost and a switched 1×2, then
discards the spare ghost and launches. It selects the owned brick, repaints Blue,
keeps for zero debit/refund, launches, saves and restarts the process.

All ten recorded states enforce exactly seven presentation replacements.
The observed deck placement is `[100,72,-2775]`, rotation 0; height 72 is checked
before clicking, so an unrelated valid snap cannot pass. Both launches retain
**11 parts, 993 kg, owned brick ID 35, 43 material and zero machinery**. The final
brick is exactly `[50,108,190,255]` Blue with unchanged geometry. Launch counters
must advance; equal old/new part counts cannot satisfy acknowledgment alone.

`native/summary.json` retains complete observations and save generations/tick.
`native/blue-brick.svce` is the actual 19,969-byte save, SHA-256
`9a92910183584e071f002a0d46e122593597ebef2056a30e4e34d7e666382ba9`.
Process logs are retained. `checks/native-package-r01.json` and
`checks/wasm-package-r01.json` identify the exact compiled packages; the native
binary SHA-256 starts `0000b4eb2b69207d` and the WASM SHA-256 `03897bb4e0993f87`.

## Actual browser journey

Chrome 152.0.7977.82 passes all seven stages using real DOM/pointer controls,
the same Teal-to-Blue repaint sequence and exact seven-presentation expectation.
After reload, the saved brick retains ID 35, placement `[100,72,-2775]`, rotation
0 and exact Blue RGBA, with 11 parts, 993 kg and 43 material. The read-only
blueprint export matches byte-for-byte. No unused ghost is charged or stored.
No repeated eight-brick or sailing journey was needed for this paint change.

The outer startup report also passes, with `browserErrors: []`,
`sample.errors: []` and no console error entries. Its exact 659,826-byte JSON is
stored losslessly as `browser/startup.json.gz`; `gzip -dc` reads it. The raw
seven-stage result is `browser/summary.json`. Neither report establishes final
visual approval or an aggregate performance result.

## Reproduction

Run from repository root with fresh output/storage directories. Do not repeat
unchanged journeys to create more views. Test target/filter pairs are preserved
verbatim in `checks/*-test.log`; use `bazel test` inside `nix-shell`, with
`--local_test_jobs=1 --test_output=all`. The material target is
`//tests:mesh_path_test`; gameplay/fixture cases use `//tests:voxy_tests`.
Treat a skipped GPU case as missing evidence. The browser UI command is
`node scripts/test_salvage_preview.mjs`.

The browser toolchain is Emscripten 6.0.1-git
`25e4e8d6550d392ba9e0c2936bce7cf41ee47cc0`, with pinned Dawn port
v20260423.175430 / `31e25af254ab572c77054edec4946d2244e184dd`. Build
`voxy_native` in `build-native-save-host` and `voxy_wasm` in `build-lego-wasm`;
package the current `web/` plus all three `voxy_wasm.{js,wasm,data}` outputs.

```sh
nix-shell --run 'python3 scripts/validate_native_cove_paint.py --expected-presentation-parts 7 --binary build-native-save-host/bin/voxy_native --output build-paint-recheck/native --storage-root build-paint-recheck/storage'
nix-shell --run 'VOXY_SMOKE_GPU=gaming-x11 VOXY_TEST_CHROME=/opt/google/chrome/chrome VOXY_SMOKE_WIDTH=1280 VOXY_SMOKE_HEIGHT=800 VOXY_SMOKE_TIMEOUT_MS=600000 VOXY_SMOKE_NO_SCREENSHOT=1 VOXY_SMOKE_PRESENTATION_PARTS=7 VOXY_SMOKE_COVE_PAINT=build-paint-recheck/browser VOXY_SMOKE_REPORT=build-paint-recheck/browser-startup.json node scripts/smoke_integrated_wasm.mjs build-paint-recheck/web salvage-cove'
```

Use the host's active `DISPLAY`/`XAUTHORITY`; run native and browser GPU work
serially. Both drivers require the exact configured presentation count (1–16),
not whichever count happens to load. The browser additionally checks exact
read-only blueprint export after reload. These checks do not establish a final
art, performance, accessibility, controller or whole-game gate.
