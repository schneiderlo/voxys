# Authored hierarchy and winding checkpoint

This is the ASSET-04 diagnostic stand, alongside the existing v2-r04 pontoons.
It checks the real authoring/import/render path. It is not a playable part,
approved game art, an ASSET-04 pass, or a G02 gate pass. The next scene work is
rendered rotated assemblies; independent technical/moving-image review and
final pontoon publication remain open.

## Asset and independent geometry oracle

`tools/salvage_assets/author_hierarchy_probe.py` runs only in a fresh background
Blender scene. Candidate `data/salvage/hierarchy_probe/candidates/v1-r02/`
contains editable `.blend`, GLB, exact-bound gameplay sidecar, 512² preview,
pre-export golden geometry, provenance, and the strict cooked bundle.
The original authoring probe, runtime probe v1 and pontoon candidates remain
unchanged. No external artwork, generated texture, or Blender MCP is required.

The canonical stand has a white .30×.08×.30 m base and three unequal rods:
red +X .60 m, green +Y .80 m, blue −Z 1.00 m. Rod widths are .04 m. The two
yellow objects share one actual Blender mesh and retain different transforms.
Five unique meshes produce six mesh instances, eight nodes, 940 unique
triangles and 1,128 instanced triangles. Every material is opaque/single-sided.

The yellow objects have two transformed Empty parents. Their local transforms
are deliberately noncommuting; composed matrices contain shear even though
each source node has valid positive-scale TRS. These are Blender coordinates,
metres, Euler XYZ angles in degrees:

| Node | Parent | Translation | Rotation | Scale |
| --- | --- | --- | --- | --- |
| nested_root | scene | −1.5, 0, .3 | 20, 0, 35 | 1.25, .75, 1.10 |
| nested_child | nested_root | .1, −.35, .1 | 0, 25, −15 | .8, 1.2, .9 |
| nested_leaf_a | nested_child | 0, 0, .5 | 10, 20, 0 | .25, .15, .55 |
| nested_leaf_b | nested_child | −.65, .3, .35 | −20, 0, 65 | .3, .1, .7 |

The generator records Blender's evaluated world matrices, every triangle and
flat face normal **before export**. Canonical conversion is `(-x,z,y)`.
Blender's glTF conversion is `(x,z,-y)`; metadata supplies only the remaining
proper Y rotation, index 12. Runtime placement keeps the full parent matrix;
it does not decompose the resulting shear into an approximate TRS.

`tests/test_hierarchy_pipeline.cpp` admits the actual cooked bytes through
`admitCookedPartBundle`. Manifest and golden digests are pinned independently
in the test. It verifies retained parents/shared geometry, nontrivial shear,
all 1,128 triangles with one-to-one outward cyclic matching, and transported
normals. Every referenced vertex and normal also matches the independent
Blender matrices through all 24 part rotations, negative grid translation,
and adjacent-sector rebasing at sector coordinates of magnitude one billion.
Position tolerance is .0001 m. A naive normal transform demonstrably differs
by more than .1 in this fixture, so a translation-only or uniform-scale path
cannot pass the normal check.

The first normal test used .0001 vector tolerance and exposed a maximum
difference of .000126299. The installed Blender 5.2.1 exporter explicitly
rounds local normals to four decimal places and renormalizes them
(`primitive_extract.py`, `__get_normals`; `io/com/constants.py`,
`ROUNDING_DIGIT = 4`). Candidate r02 records a .0002 normal tolerance, below
.012 degrees. The geometry, GLB, sidecar and cooked bytes are identical to
r01; only oracle precision metadata/provenance changed. Failed tests and r01
are preserved. `hierarchy/author-hierarchy-r01.py` matches the original
manifest's generator hash; r02's source snapshot is also retained.

## Installed scene and rendering

Launch native with `--config salvage_hierarchy_fixture.cfg`, or open the
packaged main page at `?experience=salvage-hierarchy`.
`data/salvage/fixture-hierarchy-v1.json` selects two independently pinned
bundles: the existing pontoon v2-r04 and this stand. Four pontoons retain their
raw inspection placements. The stand sits at ticks `[-250,24,-100]`, rotation
zero. Five placements expand to ten model draws; only four unique cooked LODs
are uploaded. The two yellow objects share uploaded geometry.

The stand's gameplay sidecar has one deliberately conservative diagnostic box
with coherent box mass/inertia and one neutral socket. It is never registered
in the player catalog, inventory, physics or save data. The session retains
zero builds, cargo, jobs and bodies. The stand is not a flotation/collision
quality claim.

Normal `MeshPath` renders the fixture. Its rasterizer uses no fixed cull mode;
the existing shader rejects back-facing fragments for single-sided materials.
The salvage owner selects clockwise projected front faces for the left-handed
camera. Thus the all-side inspection exercises actual back-face rejection,
not a double-sided material hiding incorrect winding. Existing real GPU quad
tests also check visible fronts and a black back-face readback. No renderer or
shader change was needed for this stand.

Mixed assets exposed an interface defect: the stand has only LOD 1, while the
pontoons have 1/2/3. The backend already refused unsupported global forced
levels, but the buttons appeared enabled. Shared state now publishes
`availableLods`, the intersection of levels 1..3 across cooked bundles plus
Auto. The interface disables unavailable levels and fails closed if that
availability field is missing. Auto still selects each bundle independently.
The generic non-assembly panel is now titled “Model inspection.”

The bounded owner reserves 6,452,636 GPU bytes for this scene, unchanged through
detail controls, camera movement, resize and Reset. That includes its fixed
reservation and all selected uploaded LODs, not driver allocations or screenshot
readback storage. Rulers add 26 guide boxes; sockets add 375. Draw counts are
10 clean, 36 with rulers and 385 with sockets, without more model uploads.

## Evidence and review

All paths below are relative to `hierarchy/` beside this document. Exact
source, native executable and package hashes are in
`integration/frozen-inputs.json`; `integration/summary.json` verifies those
hashes, the test records and the twenty original comparison images.

| Check | Evidence |
| --- | --- |
| Two clean author/cook runs, same GLB/golden/sidecar/cooked bytes and decoded pixels | `reproducibility-r02.json` |
| Three hierarchy cases, seven registry cases, six GPU MeshPath cases; no skips | `integration/bazel-final-*.xml` |
| Same three hierarchy cases through CMake; native app build | `integration/cmake-final-tests.xml`, `cmake-final-attempt01.log` |
| 69 shared CPU WASM cases, zero skips; source/tool freeze | `integration/wasm-confirmation.json` |
| Seven interface lifecycle/availability cases | `integration/ui-tests.log` |
| Current shipping browser package | `/tmp/voxys-asset-browser-10`; `integration/wasm-shipping-attempt02.log` |
| Native overview/front/back/left/right/top | `native-clean-attempt02/`; exclude its rejected `under.png` |
| Corrected native underside | `native-under-attempt01/` |
| Same seven browser views | `browser-clean-attempt03/` |
| Two matched close views | `native-stand-attempt01/`, `browser-stand-attempt01/` |
| Matched metre/plate rulers beside both assets | `native-rulers-attempt01/`, `browser-rulers-attempt01/` |
| Actual unavailable-level refusal, detail controls, guides, resize, flight, Reset, drained Leave and re-entry | `browser-journey-attempt01/summary.json` |
| Actual device destruction after 375 guide boxes reach GPU completion | `browser-loss-attempt01/summary.json` |
| Original pontoon route still accepts all three manual levels and full lifecycle | `browser-raw-regression-attempt01/summary.json` |

Views use physical 1920×1080, FOV 60 and the same daylight. Native logical size
is 1536×864 on the scaled desktop; browser uses DPR 1. The browser includes HTML
controls. These are still-image comparisons, not a frame-rate claim. Normal
occlusion and some screen-edge clipping remain in side views; close views make
the asymmetric stand readable. Root reviewed `comparison-1.png` and `comparison-2.png` (ten pairs), plus
full-size overview/underside/close stand images and browser top view. Geometry,
axes, outward surfaces and parent transforms agree across the paired views. It does not substitute for independent
technical/moving-image review or LOOK-01 visual approval.

Preserved unsuccessful attempts:

- Bazel attempt 01 caught a test initializer typo; attempt 02 exposed the
  documented export-normal precision difference. The final tests pass.
- WASM attempt 01 caught a test-only JSON byte-iterator incompatibility with
  libc++; parsing an explicit character string fixes it. Attempt 02 compiled,
  linked and passed all 69 cases, then its runner rejected the result because
  its expected total was still 66. That historical failure remains. The
  confirmation verifies every original source/tool hash, reruns the same frozen
  binary, and checks 69 passes with zero skips. The runner now expects 69.
- The original underside camera was inside terrain. Its green capture is not
  accepted. `views-r02.json` moves the camera above terrain; only that view
  needed native recapture.
- Browser capture attempt 01 used the wrong default viewport and rejected it.
  Attempt 02 used headless native Vulkan at 1920×1080 and produced a blank
  canvas despite completed work. The blank-image guard rejected it. Windowed
  Wayland/Vulkan, matching the earlier accepted platform setup, captures the
  real scene. The headless failure's root cause is not established here.

## Reproduce

Use the repository toolchain and Blender 5.2.1 LTS build `9e2066aef7ef` for
same-build reproducibility. Every output directory must be fresh. Blender's
`.blend` and PNG container hashes are recorded but need not match between
clean runs; decoded preview pixels must match.

```sh
/snap/bin/blender --background --factory-startup --python-exit-code 1 --python tools/salvage_assets/author_hierarchy_probe.py -- --output-dir /tmp/new-hierarchy-source
python3 tools/salvage_assets/cook_gameplay_asset.py --sidecar /tmp/new-hierarchy-source/hierarchy.gameplay.json --sources /tmp/new-hierarchy-source --output /tmp/new-hierarchy-cooked --converter build-salvage-native/bin/gltf_vmesh_tool --validator build-salvage-native/bin/gameplay_sidecar_tool
python3 tools/salvage_assets/check_hierarchy_probe.py --source data/salvage/hierarchy_probe/candidates/v1-r02/source --cooked data/salvage/hierarchy_probe/candidates/v1-r02/cooked --repeat-source /tmp/new-hierarchy-source --repeat-cooked /tmp/new-hierarchy-cooked --report /tmp/new-hierarchy-reproduction.json
bazel test -c opt //tests:hierarchy_pipeline //tests:fixture_registry //tests:mesh_path_test
python3 scripts/capture_salvage_asset_views.py --binary build-salvage-native/bin/voxy_native --output /tmp/new-hierarchy-native --recipe docs/validation/salvage/ASSET-04/hierarchy/views-r02.json --config salvage_hierarchy_fixture.cfg
VOXY_SMOKE_GPU=gaming VOXY_SMOKE_WIDTH=1920 VOXY_SMOKE_HEIGHT=1080 VOXY_SMOKE_ASSET_VIEWS=/tmp/new-hierarchy-browser VOXY_SMOKE_ASSET_RECIPE=docs/validation/salvage/ASSET-04/hierarchy/views-r02.json VOXY_SMOKE_REPORT=/tmp/new-hierarchy-startup.json VOXY_SMOKE_SCREENSHOT=/tmp/new-hierarchy-startup.png node scripts/smoke_integrated_wasm.mjs /tmp/voxys-asset-browser-10 salvage-hierarchy
```

Run native/tool commands in `nix-shell`; run Chrome outside Nix with the normal
desktop GPU environment. For close views substitute `stand-views.json`.
For rulers use `--guides dimensions --views overview` on native and
`VOXY_SMOKE_ASSET_GUIDES=1 VOXY_SMOKE_ASSET_VIEW_NAMES=overview` in the browser.
Use a fresh disposable browser for the actual loss test, never a user's tab.
