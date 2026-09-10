# First complete authored cove composition

2026-09-08. The complete eleven-part broad skiff, eight dock deck modules,
four authored beam pilings and generator now render together in the real game.
There are 24 placements, nine bundles, 27 uploaded detail-level assets and 32
visible material draws at the starting camera. Native and hardware browser
startup pass. Root viewed one successful native and one browser image.

**This scene remains unfinished and stationary.** It has free-flight controls,
no collision bodies, no inventory or jobs, and no sailing. This is composition
preparation under D18/D19, not LOOK-01 or an accepted visual direction. The
original `salvage` route remains available while this candidate is developed.

## Open the scene

```bash
nix-shell --run 'bazel run -c opt //:voxy_native -- --config salvage_cove.cfg'
```

Browser: `?experience=salvage-cove` in a normal package built from this source.
WASD flies, mouse looks, E/Q moves vertically. The cove UI clearly labels the
boat stationary and hides the material-inspection guide/detail buttons.
Reset and Leave reuse the existing fixture controls; their complete journey
has not been rerun for this new cove route.

## What changed

`tools/salvage_assets/compose_cove.py` generates
`data/salvage/fixture-cove-r01.json` and its composition record. It preserves all
eleven broad-skiff placements under one exact lattice translation, records the
source's seventeen connections, and selects the verified dry pontoon and
winch/helm material candidates. The generator and dock reuse cooked kit meshes;
no new placeholder hull or offline rendered substitute is used. This static
registry is not a canonical physical build or accepted cargo state.

`asset_fixture_anchor = "water"` anchors this scene's Y origin to configured
water height. Existing inspection anchoring remains the default. The complete
boat sits at world `(-18,-200.16,-91)` with water at -200. Dock deck tops are
1.28 m above that datum. The actual decompressed 256² shoreline and canonical
plate-height calculation informed placement; samples are in `placement.json`.
No object was raised to conceal underwater rendering defects.

Scene admission now permits twelve bundles and thirty-six unique LOD assets.
The first launch correctly refused the old 24-asset ceiling because this scene
needs 27; its log is retained under `native/`. The count ceilings increased,
while the 32-placement, 512-draw, 16 MiB owner and 48 MiB resident limits remain.
Actual scene reservation is **11,412,008 bytes**, including one 1,228,944-byte
environment filter. No performance gate is claimed.

## Evidence

- Native Bazel and CMake WASM application builds pass; both packages include
  the new config and registry. Served-file identities are in `package.json`.
- 62 configuration cases and nine registry/admission cases pass, including
  the actual nine-bundle cove and malformed/unscoped water anchoring.
- The changed GPU capacity-boundary test passes: over-limit asset sets and
  per-owner/combined-memory overages refuse before upload; 36 admitted unique
  assets succeed. Memory limits were not relaxed.
- Seven existing browser UI lifecycle tests pass.
- `native-r02/report.json` records the successful native scene capture,
  settings, binary hash and actual runtime state. `browser-run.json` records
  the browser startup, completed GPU work and error-free state. Both request
  exactly 11,412,008 bytes, upload 27 assets and reuse one lighting bake.
- `native-r02/cove-arrival.png` and `browser-arrival.png` are the only successful
  scene views inspected here. There was no repeated detail/light matrix.

The initially found `ldh_tool` binary predates the repository's legacy checksum
reader fix and refused the shoreline checksum. Rebuilding that tool from
current source allowed normal verified decoding. The input file was unchanged;
checksum verification was not disabled.

## Findings and next implementation

The complete authored kit is now visible together, but the foreground generator
dominates the current camera and obscures the skiff. Move the arrival camera to
the boat side as part of the next composed-scene update. The dock's repeated
stud modules also need a clearer pier silhouette and useful shore connection.

Water still renders before these meshes. Submerged hull/piling surfaces do not
contribute to refraction; the hull/shore/water relationship is not accepted.
Meshes lack cast/contact shadows, and some materials/normals remain coarse.
Implement the opaque-scene-before-water composition and grounding, then add
walkable collision and controls using the same placed dock geometry. Keep
simulation, wet/glass, motion, independent review and owner approval open.

Follow D19: use focused checks for small changes. Take another scene image when
the next visible milestone or a specific unresolved visual defect warrants it.
