# Exact release-candidate runtime admission

All five installed inspection registries now explicitly select
`pontoon/release-candidates/v2-rc01/cooked`, with manifest SHA-256
`5cb23035a14272257af22ae4af47e7fcd863b8b310186346acbf7915fba8818e`.
The original r04 source/cooked files remain untouched. Previous registry bytes
are archived under `release-runtime/previous-registries/`.

This is candidate selection for technical inspection, not immutable publication
or an accepted replacement for prototype v1. No saves, inventory or live boat
physics are migrated. The part definition and three runtime LOD payloads remain
byte-identical to the earlier inspected r04 content; the manifest now records
the current cook tools.

Bazel, CMake and the CPU WASM test runner declare the selected cooked files.
The current browser package `/tmp/voxys-asset-browser-14` contains the new
package paths and no old r04 package-index entry. The same strict no-follow
registry/bundle loader performs admission; no C++/WGSL implementation changed
for this selection. An explicit independent expected manifest pin in the actual
registry test prevents silently accepting a different selection.

The eight registry cases and three pontoon-pipeline cases pass in both native
build systems. Bazel also passes the three unchanged hierarchy cases. The full
shared CPU WASM suite passes 70 cases with zero skips, using the new embedded
candidate. These include actual rotated assemblies, socket misfit rejection,
selected-LOD bounds and physical metadata. They do not certify live physics.

Seven matched native/browser views show the stand layout, front, rear, left,
right, underside and engaged stack at physical 1920×1080/FOV 60. Root reviewed
both uncropped comparison sheets: outward surfaces, socket wells, joins and
material appearance agree. Normal occlusion and browser controls remain
visible. The scene's art is still unapproved.

The actual browser lifecycle passes all detail levels, guide modes, resize,
flight, Reset, GPU-drained Leave and re-entry with the selected candidate.
A separate assembly process destroys its real GPU device while model and socket
resources are in flight; the app stops safely without further GPU errors.
This proves exceptional shutdown, not live device restoration. The hierarchy
and both rotation galleries also pass fresh browser startup with the new package.
The raw view/lifecycle run and assembly loss run complete all five inspected
experiences; this is not a new legacy/gameplay performance matrix.

Evidence under `release-runtime/`:

| Evidence | Location |
| --- | --- |
| Exact data/source/native/browser package identities | `integration/frozen-inputs.json` |
| Checked scoped acceptance and all report hashes | `integration/summary.json` |
| Native test XML | `integration/bazel-{registry,pontoon,hierarchy}.xml`, `integration/cmake-{registry,pontoon}.xml` |
| CPU WASM build, input freeze and real test result | `wasm-attempt01/manifest.json` |
| Original native/browser images and reports | `native-clean-attempt01/`, `browser-clean-attempt01/` |
| Uncropped paired views | `comparison.json`, `comparison-1.png`, `comparison-2.png` |
| Actual browser controls/retirement | `browser-journey-attempt01/summary.json` |
| Actual device destruction and terminal state | `browser-loss-attempt01/summary.json` |
| Remaining browser inspection routes | `browser-routes-attempt01/summary.json` |

Use the existing `salvage_asset_fixture.cfg`, `salvage_assembly_fixture.cfg`,
`salvage_hierarchy_fixture.cfg`, `salvage_rotations_a.cfg`, `salvage_rotations_b.cfg`
and corresponding browser experiences. The native/browser close-view recipe is
`docs/validation/salvage/ASSET-04/release-runtime/views.json`. Build with Nix;
run Chrome outside Nix with `VOXY_SMOKE_GPU=gaming`, width 1920 and height 1080.
Each capture/check needs a fresh output directory. Replaying an older r04 result
requires its archived registry and original r04 package dependencies; do not
rewrite historical evidence to current identities.

The exact-manifest admission checkpoint passes. Independent technical and
moving-image review plus immutable publication remain required before ASSET-04
can complete. G02 also needs the remaining asset kit/material/tooling tasks and
LOOK-01. No gate commit or visual approval follows from this component check.
