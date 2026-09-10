# Visible composed hierarchy bounds

The hierarchy inspector now displays the Blender stand's full composed render
bounds. It selects the stand as the first placement in its registry, followed
by the same four pontoons. The actual models, positions, camera default, material
and renderer are unchanged. The earlier selection is preserved in
`hierarchy-bounds/previous-registry.json`; old records retain their original
hashes and counts.

The renderer intentionally creates only one ruler stand, for the first selected
placement. Previously this was a pontoon, so the authored hierarchy itself had
no visible bound. Reordering the inspection selection fixes that presentation
without a new rendering path or hardcoded substitute box.

The amber box comes from the selected admitted prefab's `canonicalBounds`,
computed from its transformed mesh bounds and full node hierarchy. It includes
both instances of the shared mesh under the rotated, nonuniformly scaled
parents. It is a conservative render bound, not a collision/flotation volume
or a claim of a mathematically minimal vertex AABB.

The updated triangle-oracle test additionally verifies every transformed triangle
corner lies within that bound, with the existing .1 mm transform tolerance.
The same test still matches the independent pre-export Blender triangles and
normals. All three hierarchy cases pass in Bazel and CMake, and the full shared
CPU WASM suite passes 70 cases with zero skips.

The stand footprint is `[-10,-6,-52]` to `[88,68,10]` ticks. Its two rulers
therefore span 1.24 m length and 1.48 m height. Guides comprise twelve box edges,
two ruler stems, two metre marks, five .32 m plate marks and three axis rods:
**24 boxes**. Socket mode still contains 375 boxes across the five placements.
These numbers differ from the pontoon's 26 ruler boxes; old evidence is not
silently rewritten to the new selection.

Four native/browser views at physical 1920×1080/FOV 60 show the overview, front,
back and top. The overview draws ten model submeshes plus 24 guides. Close views
legitimately cull neighboring parts. Four authored LOD uploads, zero prototype
uploads and 6,452,636 reserved GPU bytes remain constant. The real browser
journey passes mixed-LOD refusal, guide toggles, resize, flight, Reset, drained
Leave and re-entry. No new device-loss claim is made for this data-only change;
prior owner-loss evidence is retained separately.

Root reviewed all four paired views and full-size close captures. The complete
stand is visibly inside its bound, including the two floating yellow leaf
instances. This is the intentionally asymmetric hierarchy diagnostic, not game
scenery. Independent technical/moving-image review and game-art approval remain
open.

Evidence under `hierarchy-bounds/`:

- `integration/summary.json`: checked input/artifact hashes and scoped acceptance.
- `integration/frozen-inputs.json`: exact data, source, native binary and browser
  package `/tmp/voxys-asset-browser-13`.
- `integration/{bazel,cmake}-tests.xml`, `wasm-attempt01/manifest.json`: actual tests.
- `native-dimensions-attempt01/`, `browser-dimensions-attempt01/`: eight original
  PNGs and capture reports.
- `comparison.png`, `comparison.json`: uncropped side-by-side thumbnails and hashes.
- `browser-journey-attempt01/summary.json`: actual controls and lifecycle evidence.

To reproduce, use `salvage_hierarchy_fixture.cfg` or `?experience=salvage-hierarchy`
with the current registry. Press G / choose Rulers. The matching capture recipe
is `docs/validation/salvage/ASSET-04/hierarchy-bounds/views.json`. Run the existing
native capture tool with `--guides dimensions`; browser uses
`VOXY_SMOKE_ASSET_GUIDES=1` and `VOXY_SMOKE_ASSET_RECIPE=<recipe>` with windowed
`VOXY_SMOKE_GPU=gaming`, width 1920 and height 1080. Use fresh output directories,
Nix for native/tools, and Chrome outside Nix. Earlier hierarchy recipes targeting
the old first pontoon must use the preserved old registry to reproduce old
26-box expectations; use this new recipe for the current inspector.

This component checkpoint passes. ASSET-04 and G02 do not pass until independent
pipeline review and final publication are complete. LOOK-01 remains separate.
