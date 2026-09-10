# ASSET-03 — converter implementation and sanitizer evidence

2026-09-07. Author: `render_architecture`. **Offline importer work; no engine
appearance or GPU acceptance is claimed.** Converter source and tests are frozen.
Root owns integrated acceptance, wrapper review and the plan checkbox.

## Result

The explicit `salvage-rigid-v1` importer is implemented. The final standalone
suite passed **33/33 C++ cases with ASan/UBSan** and **23/23 actual-CLI cooker
cases**. Root separately reported passing final CMake and Bazel 33+23 cases;
their commands, artifact identities and repeated cook are in
[wrapper-integration/](wrapper-integration/). This report records the standalone
work and does not replace that integrated evidence.

```text
gltf_vmesh_tool --profile salvage-rigid-v1 input.glb output.vmesh
```

Unknown profiles reject. The original two-positional CLI and existing C++ API
remain the legacy route. Safety fixes apply to both profiles; the rigid profile
adds a closed, versioned feature policy. Existing shipped VMESH files were not
regenerated. The final tool's legacy conversion of the unchanged authoring probe
still has SHA-256
`4444fd14cfc967b46f3dcbe1079c04f1d0329ed890754ce98f367e71c1c36689`,
matching the preceding accepted probe output.

## Implemented boundaries

- `tools/gltf_vmesh_tool.cpp:124`: strict GLB/JSON preflight validates types,
  bounded counts, embedded dependencies, extensions, materials and the complete
  scene graph before TinyGLTF/image decode. Strict filesystem callbacks deny
  reads and writes. Unsupported optional appearance rejects as well as required
  extensions; there is no legacy retry.
- `:365` and `:898`: accessor spans use subtraction/division proofs before
  pointers. Every present primitive attribute must match POSITION's count
  before component reads and primitive-sized allocations. Strict alignment,
  semantic types, UV/material relations and authored tangent frames are checked.
- `:545` and `:1307`: matrix extraction uses the correct column-major entries.
  Finite, representable positive-scale TRS is retained; unsupported shear,
  reflection, perspective and malformed transforms reject in the rigid profile.
  Independent matrix/TRS fixtures cover every quaternion extraction branch.
  Local vertices and the node hierarchy remain in the exported glTF frame.
  This importer does not apply the separate per-LOD canonical gameplay basis.
- `:635`: image dimensions and cumulative decoded bytes are checked before STB
  allocation. STB pixels acquire RAII ownership immediately. Rigid-profile image
  storage follows glTF's top-left origin; legacy row order remains unchanged.
- `:1095`: finite source values can still overflow tangent synthesis. Invalid
  generated directions, lengths and final components reject before publication;
  generated zero-length tangents cannot be emitted. Failure leaves the caller's
  existing output unchanged.
- `:1532` and `:1604`: explicit profile API/CLI, bounded input reading, final
  complete-output budget and transactional in-memory conversion.

The first rigid profile accepts one reachable rigid scene, triangle meshes with
FLOAT POSITION/NORMAL and optional UV0/TANGENT, opaque metallic/roughness or
unlit materials, embedded PNG/JPEG, and current repeat/linear/trilinear sampler
behavior. Normal maps require UV0 and authored tangents. It rejects animation,
skins, morphs, extra attributes/UV sets, separate AO, alpha modes, unsupported
samplers/extensions and appearance the current renderer would silently drop.
The frozen [design](design.md) is historical prework; current implementation and
this report supersede its proposed wording and old source line numbers.

## Offline limits

Strict input: 64 MiB GLB, 1 MiB JSON, depth 32, 100,000 JSON values; 256 nodes
and meshes, 512 total primitives; 64 materials/images/textures/samplers; 4,096
accessors, buffer views and buffers. Aggregate declared buffer bytes are bounded
to 64 MiB. Total geometry is at most 1,048,576 vertices and 3,145,728 indices.
Images are at most 2,048 pixels per dimension, with separate 128 MiB cumulative
decoded-image and copied-output-image ceilings; complete output is at most
256 MiB.

These are rejection ceilings for offline cooking, **not measured process
working set or engine asset budgets**. One bounded local image copy can exist
before the copied-output reservation check; input JSON/model/temporary storage
also contribute to peak memory. ASSET-06 owns tighter production budgets.

## Final commands and identities

Run from the repository root:

```sh
bash docs/validation/salvage/ASSET-03/build_standalone.sh \
  > docs/validation/salvage/ASSET-03/compile-final.log 2>&1
/tmp/salvage-import-build/gltf_vmesh_tool_tests \
  > docs/validation/salvage/ASSET-03/tests-final.log 2>&1
SALVAGE_CONVERTER=/tmp/salvage-import-build/gltf_vmesh_tool \
SALVAGE_VALIDATOR=/tmp/salvage-sidecar-build/gameplay_sidecar_tool \
python3 tools/salvage_assets/test_cook_gameplay_asset.py \
  > docs/validation/salvage/ASSET-03/cook-integration-final.log 2>&1
```

All three exited 0. The test binary ran on the normal host so LeakSanitizer could
complete outside the tracing sandbox; no sanitizer was disabled. The build
script records the GCC 15.2.0 path and strict warnings with `-Werror`. Owned
converter, VMESH I/O and test translation units were instrumented. Existing
TinyGLTF, core and GoogleTest static libraries were linked without rebuilding
them with sanitizers. The CLI uses UBSan; the C++ test binary adds ASan. This
allows the actual CLI to run within the cooker's 2 GiB address-space limit.

| Final file | SHA-256 |
|---|---|
| `tools/gltf_vmesh_tool.cpp` | `c678329d2115f499871a377932a15ba5526ad7d490e45c9df5f9579378b620b3` |
| `tools/gltf_vmesh_tool.hpp` | `5b811defbf6b84bf0a49356ae320bff5f9ba84dba41b49b3867d5610fedf0df9` |
| `tests/test_gltf_vmesh_tool.cpp` | `ace4e29359aa8e7cd3d586b2730fe28afb4f04d3b4ec8bc5b830a87a72d6c1a8` |
| `/tmp/salvage-import-build/gltf_vmesh_tool` | `774472f2a21d42893db23879697f051f70d79a57935618b531b3d7d5a20355b7` |
| `/tmp/salvage-import-build/gltf_vmesh_tool_tests` | `3c950664e60f7ca3289d5a3cd452b35bb8f2ab5112f6beac43d35354a708ce5e` |

## Retained review proofs and prior failures

[texture-rows-reviewed/observation.json](texture-rows-reviewed/observation.json)
pins the final tool/source and exact 2×2 PNG input/output: top red/green, bottom
blue/yellow. [tangent-reviewed/observation.json](tangent-reviewed/observation.json)
replays both retained finite-input failures through both final profiles: all
four reject with exit 1 and no output file. The scripts
`reproduce_texture_rows.py` and `verify_review_fixes.py` record exact invocations
and refuse to overwrite existing output evidence. The final legacy observation
is `legacy-probe-reviewed.vmesh` and its log.

Historical failures are preserved: initial strict-warning compile errors;
`tests-first.log` (an over-specific expected diagnostic and a tracing-sandbox
LeakSanitizer failure); `tests-before-fixture-fix.log` (an image-budget fixture
hit the earlier JSON cap, corrected to use one embedded image with repeated
references); and review tangent fixtures. The ordinary-UV tangent fixture
demonstrated an actual emitted zero vector. The tiny-UV fixture demonstrated
overflow being hidden by a fallback, not emitted NaN. The earlier 32-case pass
and 23-case pass remain in `*-before-review-fix.log`. The directory named
`texture-rows-strict-final` and `legacy-probe-final.vmesh` predate the tangent/RAII
review fix; **the `reviewed` observations are the final-tool evidence**.

Root independently reviewed the complete converter and regression changes, then
reviewed the final tangent/RAII correction, and reported no remaining blocker.
No GPU, native/browser material draw, runtime node evaluation or scene-quality
claim follows from these offline checks. Those remain ASSET-04 and LOOK-01 work.
