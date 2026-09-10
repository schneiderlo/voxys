# Rendered assembly rotations

The installed rotation galleries show all 24 proper orientations of the same
two-pontoon welded assembly. This advances ASSET-04's awkward-rotation check.
At this checkpoint, actual camera-sector transitions, the hierarchy-bound
presentation, independent technical/moving-image review and final publication
were still open. The subsequent [camera-sector record](sectors.md) completes
the scoped live transition check and fixes inspection camera normalization. This is not an ASSET-04/G02 pass or game-art approval.

## Reproducible scene data

`tools/salvage_assets/build_rotation_fixtures.py` generates two closed-schema-2
registries and their camera recipes. It uses DATA-01's frozen axis enumeration:
local X and Y are orthogonal choices from +X,+Y,+Z,−X,−Y,−Z; local Z is X×Y.
It does not modify geometry, gameplay metadata or the renderer. Both galleries
select the original v2-r04 pontoon bundle and its independent manifest digest.

| Gallery | Native configuration | Browser experience | Rotations |
| --- | --- | --- | --- |
| A | `salvage_rotations_a.cfg` | `salvage-rotations-a` | 0–11 |
| B | `salvage_rotations_b.cfg` | `salvage-rotations-b` | 12–23 |

Each gallery has 24 part placements, 12 actual welds and no prototype assets.
The same `loadAssetFixture` and private `BuildModel::create` path used by the
crossbeam fixture admits it. No player build, durable ID, inventory, physics
body or save record is created. Each pair connects socket 1 on the first
pontoon to socket 2 on the second, at the engaged .96 m center separation.

Twelve pair origins form a 4×3 grid, at 6 m spacing. In ticks the first pair is
`[-450,150,-300]`; subsequent columns add 300 to X and rows add 300 to Z.
The second pontoon's offset is the chosen proper rotation of `[0,48,0]`.
Both part rotations are identical. Negative translations are therefore part
of the actual rendered selections, not an unrendered synthetic example.

Two groups preserve existing limits of 32 placements and 512 expanded draws.
A single 48-part gallery would exceed the declared placement cap. Each group's
24 connected socket endpoints need 360 guide boxes, remaining within the
shared draw budget. The group split does not omit any orientation.

`rotations/manifest.json` maps rotation IDs to placement ordinals and view names
`pair-a` through `pair-x`. The camera recipes target each pair's physical center
and include an overview. Each close camera is offset `[3.2,2.4,-4.8]` metres
from its target. These are installed, data-driven scenes, without screenshot-
specific replacements for models, material shading or socket transforms.

## Admission and runtime checks

The added `FixtureRegistry.AdmitsEveryInstalledRotationPairAndRejectsOffsetOrWrongKey`
test loads both actual registry files and actual cooked runfiles through the
no-follow loader. It confirms every rotation appears exactly once, all 24
part pairs have the correct composed offset, and all 24 endpoint pairs are
the expected socket IDs. For every orientation, moving the second part by
one X tick rejects, and rotating only its key rejects. All 48 failed replacement
admissions preserve the earlier valid assembly.

Eight registry tests pass in both Bazel and CMake. The full shared CPU WASM
suite passes 70 tests without skips, using the existing JavaScript exceptions,
Asyncify, fixed 64 MiB heap and 1 MiB stack. The same sources are compiled;
the installed galleries and cooked pontoon bundle are embedded test inputs.
This is CPU runtime portability evidence, not browser GPU rendering evidence.

Browser captures separately use the real windowed Wayland/Vulkan application,
package `/tmp/voxys-asset-browser-11`, at physical 1920×1080 and FOV 60.
Native uses the same settings at logical 1536×864 on the scaled desktop.
All model and guide paths are unchanged from the prior checkpoint.

Each overview submits 24 model draws from three unique uploaded LODs and zero
prototype uploads. Close views legitimately cull other pairs; observed clean
draw counts range from 6 to 24. The owner reserves 6,262,620 GPU bytes throughout,
the same as the four-pontoon raw inspection because all instances share the
existing LOD assets and fixed owner buffers. This excludes CPU instance data,
driver allocations and capture storage. No performance claim is made.

The real browser journey exercises all manual levels and Auto, rulers, sockets,
resize, flight, Reset, Leave and re-entry. In the overview it records 24 clean
draws, 50 ruler draws, and 384 socket-overlay draws. Leave reaches the drained
owner state with zero residency before navigating to the original LEGO route.
Re-entry produces a fresh ready inspection owner. A separate gallery-B run
then destroys the actual GPU device with model and socket resources in flight.
The application reaches its terminal failed state, releases the inspection owner
and stays stopped without further graphics errors. This proves safe shutdown,
not live device restoration.

The browser panel now describes an assembly without claiming that prototype
crossbeams exist when its prototype count is zero. Existing crossbeam scenes
retain their explicit missing-well limitation. View/lifecycle/loss runners
recognize the two new galleries and their expected counts.

## Evidence

Paths below are relative to `rotations/` beside this file. Earlier candidates
and validation records remain intact. `integration/frozen-inputs.json` binds
the exact source, native binary, current data and browser package.

| Evidence | Files |
| --- | --- |
| Identical output from two fresh scene-generator runs | `reproducibility.json`, `manifest.json` |
| Eight Bazel/CMake admission tests, zero skips | `integration/bazel-admission-tests.xml`, `integration/cmake-admission-tests.xml` |
| 70 actual WASM CPU cases, source/tool hashes and commands | `wasm-attempt01/manifest.json` |
| Every pair, plus two overviews, in native | `native-a-clean-attempt01/`, `native-b-clean-attempt01/` |
| Matching browser clean views | `browser-a-clean-attempt01/`, `browser-b-clean-attempt01/` |
| Overviews and six selected close socket-overlay views | `native-a-sockets-attempt01/`, `native-b-sockets-attempt01/`, matching `browser-*-sockets-attempt01/` |
| Full actual browser lifecycle with 24 parts/12 welds | `browser-a-journey-attempt01/summary.json` |
| Actual device loss with model/socket resources in flight | `browser-b-loss-attempt01/summary.json` |
| 34 matched pairs, 68 original images and seven uncropped comparison sheets | `comparison.json`, `comparison-1.png` through `comparison-7.png` |
| Scoped acceptance with verified frozen inputs and evidence hashes | `integration/summary.json` |

Root inspected all seven comparison sheets and full-size native overview and
upside-down pair captures for solid outward surfaces,
consistent part orientation and joined peg/well geometry. Neighboring pairs
can appear at screen edges in close views; normal occlusion is preserved.
The review must not be described as independent technical or owner approval.

## Reproduction and boundary follow-up

Run from the repository root. Generator and capture outputs must be fresh.
The generator output is a staging directory: copy its two registries into
`data/salvage/` and its recipes/manifest into a new validation directory after
review. Registry bundle paths are relative to the installed registry directory.

```sh
python3 tools/salvage_assets/build_rotation_fixtures.py --output-dir /tmp/new-rotation-data
bazel test -c opt //tests:fixture_registry
python3 scripts/capture_salvage_asset_views.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-native-rotations-a --recipe docs/validation/salvage/ASSET-04/rotations/views-a.json --config salvage_rotations_a.cfg
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_VIEWS=/tmp/new-browser-rotations-a VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-04/rotations/views-a.json VOXY_SMOKE_REPORT=/tmp/new-rotations-startup.json VOXY_SMOKE_SCREENSHOT=/tmp/new-rotations-startup.png node scripts/smoke_integrated_wasm.mjs /tmp/voxys-asset-browser-11 salvage-rotations-a
```

Substitute B's paths/experience for the other group. Native/tool commands use
`nix-shell`; Chrome uses the normal desktop environment outside Nix. Native
socket captures use `--guides sockets --views overview pair-a pair-f pair-i`
for A and `overview pair-n pair-s pair-x` for B. Browser uses
`VOXY_SMOKE_ASSET_GUIDES=2` and comma-separated `VOXY_SMOKE_ASSET_VIEW_NAMES`.

**The galleries alone are not sector-transition evidence.** The subsequent
[camera-sector record](sectors.md) addresses the defect described below; this
paragraph preserves the finding that motivated the change. The
CPU all-rotation/adjacent-sector tests already verify mesh/socket transforms,
but these gallery cameras do not deliberately cross a sector boundary.
`voxy_set_camera_pose` at the gallery checkpoint set sector zero with an absolute float position;
the free-fly camera itself does not normalize sectors. Native initial placement
uses `setCameraWorldPose` and `worldPositionFromAbsolute`, whose 256 m sectors
use local coordinates in `[−128,128)`. Merely moving browser absolute X through
128 with the current helper does not demonstrate a camera-sector change.

The remaining proof must record actual sector/local camera state on both sides
of a boundary with the same live assembly owner, completed GPU submissions and
matched model/socket images. Use an explicit canonical camera placement path
or a narrowly scoped inspection control; preserve existing camera/scene
contracts and verify legacy behavior. The observed inspection origin on this
host is `[-19,-184.68,-37]` metres, but calculate it from the scene state as
the current capture tools do rather than silently assuming it on another host.
The hierarchy's own full composed render bound also still needs a dedicated
visible inspection; current dimension guides intentionally select the first
placement, which is a pontoon in the hierarchy scene.
