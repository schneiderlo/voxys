# Kit inspection, controls and motion

2026-09-08. **Scoped runtime checkpoint; ASSET-05 and G02 remain open.**
Root implemented and inspected these results. D18 permits preparation while
the three independent reviewers are unavailable; it does not waive review.
There is no new gate commit. The goal still includes all implementation
through G14. Preserve unrelated work and all earlier failed evidence.

## What changed

The installed asset payloads remain the nine r08 parts and unchanged v2-rc01
pontoon described in [the first checkpoint](static-kit-checkpoint.md). No
Blender source, GLB, sidecar, VMESH, registry or simulation behavior changed
during this follow-up. The current boat is a static inspection fixture.

The complete starter exposes a real inspection-owner bug: seventeen
connections produce 34 socket ends, each drawn with three axes and twelve
clearance edges. Thus 510 helpers plus eleven model draws require 521 draws.
The old owner charged both paths against a combined 512 limit, despite
already allocating a separate 512-instance buffer for each path. Clicking
Socket X-ray closed the application with an explicit capacity error.

`SalvageAssetFixture::encode` now bounds models and guides independently at
512 each. It retains the existing fixed 128 KiB reservation and 16 MiB owner
limit. It validates the entire frame before opening its queue ticket; an
overflow neither truncates late guides nor corrupts the next usable frame.
No shader, resource allocation or GPU lifetime protocol changed.

The new GPU regression submits 17 visible two-socket parts: all 510 helper
boxes plus seventeen models draw. Eighteen such parts require 540 helpers
and reject without changing the caller ticket or opening an unresolved frame;
the next valid frame still submits. The old 32-placement probe now correctly
passes with 480 helpers because its model and helper buffers both fit.

The native motion tool now accepts a bounded, explicit workload declaration
in the shared recipe. Counts must match the selected connected registry;
tracked LOD indices must be unique, valid authored placements. The original
six-part/five-connection workload remains the default. Browser and native
checks require the actual 1→2→3→2→1 sequence for each declared tracked part,
constant residency and real queue completion.

The view runner now verifies that the browser applied the requested light.
It used to accept a camera/light revision even when the light was clamped.
Three first-pass underside recipes requested a below-horizon sun; the browser
clamped elevation to 1°. Those first images are preserved but are not accepted
as matched-light pairs. The corrected recipes use supported above-horizon
directions. This was a capture-recipe defect, not a new lighting model.

## Executed validation

| Check | Result | Evidence |
|---|---|---|
| Optimized Bazel GPU/CPU inspection owner | 19 pass, zero skips | `guide-capacity-bazel-pass.log`, `guide-capacity-bazel.xml` |
| Optimized Bazel mesh renderer | 6 pass, zero skips | Same build log, `guide-mesh-bazel.xml` |
| Native CMake owner | 19 pass, zero skips; full application rebuild succeeds | `guide-capacity-cmake-pass.log`, `guide-capacity-cmake-pass.xml`, `motion-native-build.log` |
| Full browser application | Rebuild succeeds | `guide-capacity-wasm-app.log`, `browser-package-r08-guide1.json` |
| Invalid native motion requests | 9 refuse before GPU startup/output mutation | `motion-cli.json` |
| Actual browser controls, all three kit routes | 14 recorded stages each pass | `browser-controls-{broad,narrow,cargo}-guide1/summary.json` |
| Actual disposable-browser GPU loss with full socket overlay | Clean exceptional teardown; no uncaptured GPU validation error | `browser-loss-guide1/summary.json` |
| Shared 30-second broad-skiff path | Native and browser pass; eleven model draws throughout | `native-kit-motion1/manifest.json`, `browser-kit-motion1.json` |
| Original assembly motion regression | Both runtimes pass with original recipe | `native-legacy-motion1/manifest.json`, `browser-legacy-motion1.json` |
| Review video packaging | Both skiff videos encode and decode at 1920×1080, 30 seconds, 360 encoded frames | Per-video `video.json` |

The first static checkpoint's 74 CPU WASM tests remain evidence for unchanged
kit admission inputs. Their complete recorded input hashes were checked again
and still match; this does not pretend the renderer change was tested by those
CPU tests. The new renderer runs in the rebuilt hardware-browser application.

Real control journeys use Near/Middle/Far/Auto buttons, guides, two sizes,
keyboard flight, Reset, Leave to the original world and re-entry followed by
another Reset. Both skiffs draw 521 model-plus-helper draws with all 510
socket helpers, retaining 12,977,224 requested GPU bytes. Cargo draws 124 with
120 helpers, retaining 4,773,480 bytes. Leave reaches the drained state with
zero owner/helper reservation before navigation. Inventory/jobs remain empty.

The actual Chrome 152/Wayland runs use Radeon 890M hardware and a physical
1920×1080 viewport. The frozen package is `/tmp/voxys-kit-browser-r08-guide1`.
Incidental FPS text, capture throughput and requested allocation bytes are
not performance or driver-resident-memory acceptance.

## Contact and moving-view review

Six native/browser pairs were inspected directly at FOV 60:

| View | Final native/browser directory suffix | Finding |
|---|---|---|
| `deck-mating` | `contact-broad-guide1` | Stacked plate, beam and pontoon surfaces are readable; visible bevel seams are small |
| `engine-seat` | `contact-broad-guide1` | Engine mount sits on the deck; lower leg clears the back edge |
| `underside` | `contact-broad-guide2` | Beam and pontoon wells remain visible; propeller stays behind/below the deck |
| `cargo-seat` | `contact-cargo-guide1` | Generator cage seats on the cradle runners; housing and pedestal are visible |
| `cargo-below` | `contact-cargo-guide2` | Lower well is open beneath the cradle; the opposite seat does not break through the floor |
| `crate-pin` | `contact-cargo-guide2` | Cargo underside has a visible keyed pin |

Prefix each suffix with `native-` or `browser-`. The two corrected recipes are
`contact-broad-v2.json` and `contact-cargo-v2.json`; they differ only in the
three formerly negative sun elevations. The first three positive-light pairs
were checked against the stricter applied-light tolerance and retained.
Browser UI, sky timing, tiny edge shading and secondary details differ; these
are not pixel-equality certificates. A background winch flange looks different
in the cargo-seat images and needs the close winch follow-up below.

Native broad motion has 189 original JPEG captures and 1,800 trace samples.
Browser broad motion has 129 captures. Both use the same 6→100→6 metre,
30-second smooth path and keep eleven model draws and constant residency.
The two pontoons are the explicitly tracked 1→2→3→2→1 placements. Smaller
equipment starts at its actual projected-size choice, sometimes LOD 2;
this is not a claim that every part automatically visits all three levels on
this one path. Every part's actual LOD remains in the full trace. Browser
forced-detail controls independently verify all three authored levels.

Root inspected sampled motion frames around 0, 6, 15, 24 and 30 seconds in
both runs. The same boat silhouette retreats and returns; no part disappears
or changes scale independently in these samples. The videos preserve original
frames at their measured capture times, encoded at 12 FPS. Root's sampled
inspection does not replace independent continuous-video technical review.

- [Native review video](native-kit-motion1/captures/motion.webm)
- [Browser review video](browser-kit-motion1/motion.webm)

## Remaining work before kit acceptance

- [ ] Correct the visible winch drum/flange intersection in a new isolated
  candidate. In `author_functional_kit.py`, cheeks occupy x=.27… .45 m and
  −.45…−.27 m; flanges occupy x=.27… .33 and −.33…−.27, so the flanges are
  embedded in the cheeks. This produces detached-looking silver crescents
  in the near motion frames. Put the flanges inside the cheeks and shorten
  the drum/cable wraps to fit; preserve the old r08 bytes and provenance.
  Inspect all three detail levels from both sides after cooking.
- [ ] Refine deliberately coarse open-machinery collision/solid-occupancy
  proxies, especially the winch and cradle cavities, before relying on their
  surface contacts. Do not treat rendered component volume as buoyancy or
  infer a physically sealed housing from the visible cage. Recompute and
  validate any changed mass recipe explicitly.
- [ ] Complete native forced-detail inspection for all kit parts and review
  contact geometry at each LOD. Current native evidence covers automatic
  selection and the declared pontoon transitions; browser controls cover
  forced selection. These are different evidence scopes.
- [ ] Obtain independent technical review and ASSET-04 prerequisite acceptance.
  Keep ASSET-05/G02 open; prototype allowance does not excuse clear intersections.
- [ ] Continue ASSET-06 material standards and LOOK-01's real walkable cove.
  Current flat floating inspection layouts do not resolve the owner's rejected
  visual quality. Water/opaque ordering, scene lighting and grounding remain.

## Reproduction and preserved failures

Build native and browser as in the first checkpoint. Use new output paths:

```sh
nix-shell --run 'bazel test -c opt //tests:salvage_asset_fixture //tests:mesh_path_test --test_output=all'
nix-shell --run 'cmake --build build-salvage-native --target salvage_asset_fixture_tests voxy_native --parallel 4'
nix-shell --run 'python3 scripts/capture_salvage_asset_motion.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-kit-motion --recipe docs/validation/salvage/ASSET-05/kit-motion.json --config salvage_kit_broad.cfg'
nix-shell -p ffmpeg-full --run 'python3 scripts/encode_inspection_motion.py --input /tmp/new-kit-motion/captures'
```

Run hardware Chrome outside the project Nix shell. Use the same smoke runner
and package with `VOXY_SMOKE_GPU=gaming`, width/height 1920/1080, and a fresh
report/directory. `VOXY_SMOKE_ASSET_FIXTURE` runs actual controls;
`VOXY_SMOKE_ASSET_MOTION` plus `VOXY_SMOKE_ASSET_RECIPE` selects the shared
motion recipe. `VOXY_SMOKE_ASSET_VIEWS` selects camera/light captures;
`VOXY_SMOKE_ASSET_VIEW_NAMES` optionally selects comma-separated views.
Use `VOXY_SMOKE_ASSET_LOSS` only in its own disposable browser invocation.

Preserved failures include the original browser capacity rejection, the old
test's combined-budget assumption after the fix, two commands naming test
targets that do not exist, the three mismatched-light view pairs and the first
video commands run in a shell without ffmpeg. Correct target names and the
existing `ffmpeg-full` environment produced the passing results above. No
failed report was overwritten or presented as a pass. Current source/package
hashes and final artifact selection are in `inspection-summary.json`.
