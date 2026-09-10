# ASSET-03 — bounded rigid glTF import design

2026-09-07, `render_architecture`. **Read-only design/prework.** Implementation
must wait for integrated ASSET-02 acceptance and an explicit file claim. This
document does not complete ASSET-03 or demonstrate improved engine visuals.
Root AGENTS/README and the complete implementation plan were read again.

The immediate output is a dependable path for the pontoon, skiff components,
dock and generator needed by LOOK-01. Preserve the existing model routes and
the ASSET-02 canonical sidecar contract. Do not expand this task into animation,
general material support, a new mesh format or the later assembly renderer.

## 1. Entry points and current defects

`tools/gltf_vmesh_tool.hpp` exposes `convertGltfBytesToVmesh(bytes,isGlb,out,error)`.
The implementation at `tools/gltf_vmesh_tool.cpp:1096` uses TinyGLTF, then builds
materials, meshes, skins, nodes and animations into a temporary `VmeshData`.
Only success assigns `out`. The CLI at `:1143` accepts two positional paths.
`tests/test_gltf_vmesh_tool.cpp` has 19 cases, including intentional legacy
missing-normal defaults, skins, animations, unlit materials and parent storage.
The ASSET-02 Python cooker invokes that actual CLI and validates its output.

| Current location | Required correction |
|---|---|
| `resolveAccessor`, `:75–120` | Replace unchecked additions of accessor/view offsets and spans with checked/subtraction-based bounds. Validate component alignment and stride before pointer construction. |
| `convertPrimitive`, `:543–780` | Validate **every present attribute count** against POSITION before component reads. Known attributes currently inherit POSITION's loop length without this check. Extra attributes are silently ignored. |
| `decomposeMatrix`, `:255–324` | Correct column/row mapping in quaternion extraction; reject matrices that cannot be represented by the accepted TRS contract. |
| `buildNodes`, `:902–992` | Define one-scene semantics and validate all hierarchy edges. Current code traverses every scene and stores all nodes, including unvisited ones. Exact TRS lengths, zero quaternions/scales and matrix-plus-TRS need rejection. |
| `decodeRgbaBytes`, `:331–362` | Bound dimensions and decoded bytes before STB allocation; correct row orientation for new strict-profile assets. |
| `convertTexture`/`buildMaterials`, `:432–530` | Reject unsupported appearance, validate finite factor ranges, and cap aggregate copied image bytes before insertion. Images are copied once per material slot, so source-image count alone does not bound output memory. |
| Loader setup, `:1103` | Reject strict-profile features/dependencies before image decoding; install filesystem callbacks that deny external reads. A check after TinyGLTF loading is too late for this boundary. |
| CLI read loop, `:1154–1174` | Stop reading when the selected input budget is exceeded. The current 1 GiB converter limit is checked only after the CLI has read the entire file. |

The matrix failure was already reproduced in LOOK-01 prework: a +90° Z matrix
produced the inverse quaternion. A new bounded offline check during this design
also confirmed the image-row problem. A 2×2 PNG with top row red/green and bottom
row blue/yellow cooked to `[blue,yellow,red,green]` in VMESH storage. The converter
exited 0, writing 145,573 bytes. Its SHA-256 was
`79785b8fdde4c1488b10b5c2b312be9e855aa613fb988e36d3fe89c0db3627cb`.
The first check injected an embedded PNG into a temporary copy of the trusted
probe, ran the existing converter, read `VmeshHeader.imagesOffset`, and removed
the temporary files. The exact reproducible check was then rerun and retained
in [texture-rows-before-fix/observation.json](texture-rows-before-fix/observation.json),
with its [input GLB](texture-rows-before-fix/input.glb),
[quadrant PNG](texture-rows-before-fix/quadrants.png), output VMESH and command
logs. The record pins all input/output, converter, relevant source and base
revision identities. No build or GPU was used. STB documents top-left-first
output in the [vendored primary source](../../../../third_party/stb/stb_image.h)
at line 158; no flip setter is used by this converter.
The existing texture test is 2×1 and cannot reveal vertical inversion.

Reproduce with a new output directory; the script refuses to overwrite one:

```sh
python3 docs/validation/salvage/ASSET-03/reproduce_texture_rows.py \
  --converter build-salvage-native/bin/gltf_vmesh_tool \
  --output /tmp/asset03-texture-row-observation
```

Normative reference: glTF stores node matrices column-major, composes local
transforms as T×R×S, uses XYZW unit quaternions and requires equal attribute
counts within a primitive. Its texture origin is upper-left. These conventions
must be preserved before applying the separate gameplay basis.
[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
`src/moto/vmesh.hpp:51` currently describes UV V as up; that comment does not
match the glTF convention. `mesh_path.wgsl:86` forwards UVs unchanged.

## 2. Explicit profile selection

Propose a versioned `salvage-rigid-v1` profile. Add a typed options/overload to
the in-memory converter while preserving the existing API as a legacy wrapper.
The existing two-positional-path CLI stays valid. The new invocation is:

```text
gltf_vmesh_tool --profile salvage-rigid-v1 input.glb output.vmesh
```

Unknown profile names fail. The salvage cooker must select this explicitly,
record the profile and exact invocation in its manifest, and never retry with a
legacy profile after rejection. Sidecar schema fields need no change merely
to select an importer profile. Coordinate wrapper/build edits with root; the
ASSET-02 worker confirmed this boundary.

Memory-safety repairs (checked ranges, recognized-attribute count matching,
safe narrowing, bounded file input) apply to all profiles. Strict rejection of
otherwise valid unsupported features applies to the new profile. Preserve the
legacy skin/animation/missing-normal tests; add explicit strict rejection tests.
Keep already-cooked legacy assets unchanged. Correct strict-profile image rows
without globally flipping shader UVs; any legacy re-cook/orientation migration
requires a separate comparison of affected textures and routes. Record the
profile so new outputs are distinguishable from the historical cook contract.

## 3. Proposed first profile

These are project restrictions for this milestone, not a claim to support all
valid glTF. Document every rejection with an indexed property path, such as
`meshes[2].primitives[0].attributes.NORMAL: count 2, expected 3`.

| Area | Accept initially | Reject explicitly |
|---|---|---|
| Container | Self-contained GLB 2; bounded embedded PNG/JPEG and the four ASSET-02 supported strict-base64 forms | External dependencies; unknown container/required version; unsupported image payload format even if MIME claims PNG |
| Scene | Exactly one scene; default scene absent or zero; a bounded forest of rigid nodes, every node reachable once; mesh reuse by multiple nodes | Multiple/empty scenes, duplicate roots, multiple parents, cycles, unreferenced nodes, bad node/mesh indices, cameras/lights |
| Transform | Omitted identity or exact finite TRS; proper normalized rotation; positive nonzero local scales; affine decomposable matrix | Matrix plus any TRS field, wrong vector lengths, zero/invalid quaternion, reflection/negative local scale, singular scale, shear/perspective; values that overflow the output float representation |
| Geometry | Nonempty triangles, indexed or non-indexed; POSITION and usable NORMAL as FLOAT VEC3; optional FLOAT VEC4 TANGENT and FLOAT VEC2 UV0; interleaved accessors when valid | Sparse accessors, non-triangles, colors, extra UVs, joints/weights, morph targets/weights, unknown custom attributes, wrong types/normalized flags/counts |
| Normals/tangents | Finite usable normals; authored unit tangents with sign ±1 for normal-mapped primitives; small documented normalization tolerance | Zero/degenerate basis, nonfinite components, material normal map without UV0 and authored tangent frame; no silent up-normal fallback |
| Material | Core metallic/roughness, base/normal/MR/emissive maps; finite factors; OPAQUE and double-sided flag; optional explicitly supported unlit | Separate AO texture, alpha MASK/BLEND for this early opaque kit, texture transforms, transmission/clearcoat/specular extensions, variants, unsupported appearance |
| Sampler | Unspecified default or exact current repeat U/V + linear/trilinear behavior | Clamp/mirror/nearest and other explicit settings that the current renderer would replace |
| Extensions | A closed allowlist, initially only `KHR_materials_unlit` in its implemented material location | All other required or used/present extension behavior, including unsupported texture-info extensions; do not check only `extensionsRequired` |
| Metadata | Bounded names/extras may be retained or explicitly ignored as non-render metadata; sidecar owns gameplay IDs | Treating node indices/names/extras as durable socket or part identity |
| Animation | None in the rigid profile | Skins, animation channels/clips and skin attributes, including extra influence sets |

The actual authoring probe fits this proposed basic profile: one selected scene,
4 nodes/meshes/materials, 20 accessors, POSITION/NORMAL/TANGENT/UV0, no extensions,
skins, animations or images, opaque double-sided factor materials. Add a textured
golden fixture; this factor-only probe cannot certify texture handling.

Do not require visible render meshes to be watertight. Collision and flotation
closure is a separate authored-volume requirement. Do not bake the canonical
basis into vertices or node transforms. ASSET-04 evaluates the retained hierarchy
then applies the recorded per-LOD basis once above it; gameplay frames are
already canonical.

## 4. Validation stages and budgets

1. **Before TinyGLTF:** bound input and GLB JSON; parse with duplicate-key,
   depth/value and record limits; inspect feature/type/extension declarations,
   scene policy and URI forms. Use deny-read filesystem callbacks regardless
   of preflight so a parser fallback cannot access the working directory.
2. **Image callback:** inspect PNG/JPEG signature and dimensions before decoding;
   use checked width×height×4, per-image and aggregate decoded budgets. Budget
   the callback's accumulated decoded images while TinyGLTF builds the model.
   Validate MIME versus actual payload. Do not rely solely on subprocess limits.
3. **Resolved model:** validate every accessor span and semantic type, every
   attribute relation, graph edge, transform and material/texture reference
   before allocating primitive-sized intermediate arrays. Validate required UVs
   and tangents against the material used by that primitive.
4. **Conversion:** checked cumulative reservations for vertices, indices,
   submeshes, names and copied texture slots; validate finite generated values
   before output. Keep the output object untouched on failure.
5. **Serialization/cooker:** retain `writeVmesh`/`readVmesh` and the ASSET-02
   drawable-data checks; bind the new converter/profile in a fresh immutable
   bundle. Run the real selected CLI, not only the in-memory helper.

Initial suggested **offline** strict-profile caps: 64 MiB source, 1 MiB JSON,
depth 32/100,000 JSON values; 256 nodes/meshes, 512 primitives, 64 materials,
64 images/textures/samplers, 4,096 accessor/view records; 1,048,576 total vertices,
3,145,728 indices; 2,048 maximum image dimension; 128 MiB cumulative decoded
images and 128 MiB copied output image bytes; 256 MiB serialized output. Check
the complete output sum too. These are proposed rejection ceilings to tune
before implementation, not measured working-set or engine draw-capacity claims.
ASSET-06 will set much tighter production-part/material budgets. MeshPath's
256-instance/512-draw defaults do not automatically admit a kit near these caps.

Accessor arithmetic should prove the view against its buffer first, then prove
accessor offset and element bytes against the view, then bound `(count-1)` by
remaining span divided by stride. Only then construct the pointer. Enforce
component/vertex alignment and prevent narrowing of JSON offsets/counts. Test
both short and long attribute mismatches; surplus vertices are also invalid.

For transforms, perform scale lengths, orthogonality and determinant tests in
double precision. Use column vectors consistently: e.g. matrix entry row 0,
column 1 is `ry.x`, not `rx.y`. Extract/normalize a proper quaternion, canonicalize
its sign, and recompose to compare with the input within a documented relative
float-output tolerance (start at 1e-5). Exact TRS values should use the same
normalization/sign policy. Test the trace-positive and all three dominant-axis
quaternion branches, especially half-turns. Reject an unsupported matrix rather
than approximating it. Parent/child composition may create an affine transform
with nonuniform scale; the later runtime must retain the full composed matrix.

## 5. Bounded implementation and acceptance sequence

1. Add profile API/CLI selection and documented errors, preserving the old
   invocation. Add direct legacy/strict selection tests before altering policy.
2. Repair accessor spans/count relations and matrix extraction with independent
   fixtures. Cover every recognized attribute in the legacy safety path too.
3. Add strict preflight, deny-read callbacks, graph/feature/material policy and
   decode/copy budgets. Correct the new profile's row orientation and test it.
4. Wire the actual salvage cooker profile and manifest, preserving immutable old
   bundles. Cook the real probe twice and compare bytes; exercise bad feature
   rejection through the full Python publication path.
5. Run focused native optimized and ASan/UBSan tests under both build systems;
   retain failures, actual tool hashes and exact filters. Root owns shared build
   integration. No WASM converter execution is required: native cooking produces
   VMESH for both runtimes. ASSET-04 must separately prove native/browser loading.

Minimum meaningful regressions:

- NORMAL/TANGENT/UV0/JOINTS0/WEIGHTS0 count ±1, valid short backing buffers at the
  allocation end, bad stride/alignment, huge/wrapping offsets, huge declared
  count with tiny input and out-of-range/restart-sentinel indices. Failed
  conversion leaves a nonempty sentinel output unchanged.
- Equivalent matrix/TRS for +90° Z and a non-axis-aligned rotation, parent
  rotation with child translation, nonuniform positive scale, half-turns,
  quaternion sign equivalence, mixed matrix/TRS, shear, perspective, singular,
  negative determinant, NaN/infinity and float-overflow values.
- One scene with two nodes sharing a mesh; duplicate root, shared child, cycle
  including an otherwise unreferenced component, invalid default scene and
  multiple scenes. Preserve node/local-mesh data, not merely node counts.
- Each rejected appearance/function family, including an unsupported optional
  extension, extension nested in texture info, extra UV/color/influence set and
  normal map without its required basis. Legacy supported fixtures still pass.
- A 2×2 colored-quadrant PNG with byte-exact top-row-first output and unchanged
  UVs, a direction-marked normal map, linear/sRGB slot flags, finite factors,
  image dimension bomb rejected before decode and repeated material references
  exhausting the copied-image budget. GPU material inspection follows ASSET-04.
- Actual GLB CLI/profile selection, unknown-profile error, failed cook leaves no
  bundle, source/metadata IDs unchanged, profile/converter/output manifest
  identity, deterministic repeated probe cook and independent review.

Once these pass, ASSET-04 can build the rigid prefab bridge and one pontoon.
LOOK-01 still needs authored kit composition, collision-only visual suppression,
water-before/after-opaque corrections, grounded lighting and actual matched
native/browser captures. Correct importing is a prerequisite for that visual
progress; it is not the visual milestone itself.
