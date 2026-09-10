# Individual brick assets — offline checkpoint

Three original studded parts now exist in `data/salvage/bricks/r01`: 1×2, 2×2 and 2×4. Each has a reproducible Blender recipe, a saved `.blend`, three exported/cooked LODs, content identity, socket IDs, mass/inertia, collision/occupancy and costs. The supplied five-brick fixture tests a three-layer stack using all three sizes.

**This is not the playable brick editor.** The Cove registry/palette still uses the existing boat modules. Admission of these parts into the actual workshop, larger construction capacity, mouse picking, placement previews and the native/browser building journey remain unchecked in LEGO-02.

## Geometry and gameplay contract

- One-metre stud pitch; .02 m placement lattice; .96 m engaged spacing; .18 m round studs. The .04 m width/depth gap between neighboring nominal bricks is a molded edge gap, not extra stack spacing.
- The underside is open, with an actual .12 m roof and .08 m walls. Incoming studs fit the cavity. This is not a full box with studs painted on.
- Round studs use bounded, conservative three-box collision cross sections, overlapping only their own roof by .02 m. Buoyancy uses disjoint solid-material proxies, including conservative stud volumes. The underside cavity receives no sealed-compartment displacement.
- Mass uses the shell boxes and analytic cylinders with the parallel-axis theorem. Gameplay masses are 12/24/48 kg; costs are 2/3/5 material and no machinery. These are initial gameplay tuning values, not claims of measured final handling.
- Simple original red, blue and yellow PBR plastic materials, with .32 roughness and no metal. No texture generation, external model, logo, Blender MCP or image capture is required.

## Executed evidence

Blender 5.2.1 LTS exported all nine meshes. Their components are manifold, contain no degenerate triangles and retain the measured bottom at −.48 m and stud top at +.66 m. LOD triangle counts are 692/308/100, 1260/556/172 and 2396/1052/316 respectively. All three bundles pass the existing `cook_gameplay_asset.py` and C++ sidecar/catalog validator.

`check_brick_stack.cpp` admits the actual cooked bundles through `loadAssetFixture`, then uses `CompiledAssembly` and `BuildModel`. All 24 cube orientations compile to one root with 120 kg mass. All 24 one-tick weld misalignments and a separate overlapping-brick layout are refused. The normal compiler and clearance rules are unchanged.

The initial standalone build reused the current CMake library and compile/link configuration. Its command arrays and output are retained here. Named Bazel/CMake checker targets are included for subsequent runs. The normal CMake target also builds and passes the same stack checks. Its first invocation reported an unknown target because the old build directory had not been reconfigured; reconfiguration resolved it. Both logs are retained. The normal Bazel target also builds and passes the same 24-orientation/misalignment and overlap checks. The raw build logs are retained locally.

## Reproduction

Run Blender in background/factory-startup mode with `tools/salvage_assets/author_bricks.py -- --output-dir <new directory>`. Cook each generated part with `cook_gameplay_asset.py`, pointing to its `source/<name>.gameplay.json`, source directory and a new cooked destination, using the existing converter and sidecar validator.

Build `//tools:check_brick_stack` with Bazel or `check_brick_stack` with CMake. Run the resulting checker with the absolute path to `data/salvage/bricks/r01/fixture-stack.json`. This is a CPU content check and opens no window.

Scratch and raw author/cook logs: `build-cove-bricks-1kpmad89`. No image or screenshot was generated. No whole LEGO-02 task, PLAY-02 requirement or game gate is marked complete.

## Next integration work found in source

- The installed Cove has ten bundles; these three bricks require thirteen. Admission and rendering currently cap bundles at twelve and unique LOD uploads at thirty-six (`fixture_registry.cpp`, `salvage_asset_fixture.hpp`). Raise those related ceilings together, retaining the owner/resident byte checks.
- Scene placements are capped at thirty-two across `CoveWorkshop`, `CoveBoatAssembly`, `CovePlayer`, `CoveRigidRoots`' part-binding adapter, `cove_build.cpp`, recovery-design normalization and the renderer. This is independent of the thirty-two physical-root ceiling. Preserve root/water limits when giving construction at least sixty-four editable bricks plus scenery.
- Generic canonical builds already allow 256 parts, 1,024 connections and 2,048 solid proxies; one 2×4 brick uses twenty-nine proxies. Check full compiler candidate/union/shape limits with the actual larger design instead of assuming the placement-array change proves capacity.
- `sceneForCoveBuild` currently finds a new paid part's asset by searching original **placements**. New palette definitions must restore from admitted catalogue bundles even when the world has no original instance of that brick. Helm standing anchors still need their authored placement path.
- `Application`'s save content identity hashes the installed registry digest plus terrain/rule inputs. Adding catalogue content must include an explicit compatible-content/legacy-save policy, with existing-world resume evidence. Do not strand previous saves by silently changing the registry hash or accept arbitrary content hashes to make them load.
- Workshop display and camera use the authored build transformed by the scene origin and a four-metre lift. Pointer rays must use the same transform. Existing arrows/T/Keep/Launch remain useful while adding direct placement; do not move the camera target on every ghost mouse movement.

These are inspected integration requirements, not completed features. Continue here before extra visual diagnostic work or another salvage job.
