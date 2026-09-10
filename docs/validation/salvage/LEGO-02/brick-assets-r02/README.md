# Individual brick quarter-turn connections — r02

The r01 meshes and global stack rotations passed, but rotating an upper brick relative to its support exposed the existing keyed-frame contract: all three relative quarter-turns were refused at `connection.frame`. The retained r01 negative probe records that missing behavior. R01 remains an offline historical asset; it was not enabled in the playable Cove palette.

R02 gives each round underside opening four canonical receptacle frames, with distinct stable socket IDs, while keeping one top plug per stud. Existing frame matching, solid-occupancy rules, plug connection capacity and weld strength are unchanged. This permits relative 0/90/180/270-degree stacking without relaxing keyed connections on existing boat modules. An attempt to use a second receptacle frame to attach twice to the same plug is still refused at `connection.occupancy`.

Changed part definitions use version 2. All nine exported mesh files are byte-identical to r01, so their immutable visual asset keys remain unchanged. The added frames are metadata, not new geometry or extra physical studs. The maximum 2×4 part now has forty sockets and twenty-nine solid proxies, within the existing per-part catalogue limits. Explicit part and visual ID maps preserve identities independently of display ordering.

## Verified

All three revised bundles pass the existing cooker and C++ sidecar/catalog validator. The unchanged brick checker is run against four five-brick, three-layer fixtures: relative yaw 0/90/180/270 degrees. For each fixture it compiles all twenty-four global cube orientations into one 120 kg root, refuses twenty-four one-tick misalignments, and refuses an overlapping-brick layout. Both the CMake-built and Bazel-built binaries pass: ninety-six orientation combinations, ninety-six misalignment refusals and four overlap refusals per binary. The separate duplicate-clutch test is also refused. No GPU state, save or inventory is injected; no screenshot is generated.

## Reproduce

Generate assets with `author_bricks.py`, cook each generated part as documented for r01, then run `build_brick_fixtures.py --asset-root <new cooked asset root>`. The generator reads that root's authored metadata and produces four positive stack fixtures. Run `check_brick_stack` with the absolute path to each `fixture-stack*.json` in `data/salvage/bricks/r02`. `fixture-duplicate-clutch-refusal.json` is a separate negative fixture that must exit nonzero with `connection.occupancy`.

The source recipes, Blender/GLB files, cooked bundles and generated fixtures are retained in `data/salvage/bricks/r02`. Raw generation/cook logs remain in `build-cove-bricks-1kpmad89/checks`.

**Remaining:** these parts still need Cove catalogue admission, larger scene/build/save capacity and direct building controls. Preserve existing saves when extending the catalogue. The actual native/browser eight-brick build/rotate/remove/undo/launch/save journey remains required. LEGO-02, PLAY-02, visual acceptance and the game gates remain unchecked.
