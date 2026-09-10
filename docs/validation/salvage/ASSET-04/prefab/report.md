# ASSET-04 prefab and native material draw evidence

2026-09-08 UTC, `render_architecture`. **This bounded runtime work passes its
focused checks. The authored pontoon/application fixture and full ASSET-04
acceptance remain pending.** No Application, shared build, world physics or
shader code was edited by this worker. Root owns those integration decisions.

## Delivered behavior

`src/game/assets/rigid_prefab.hpp/.cpp` now prepares an owned CPU prefab from an
admitted VMESH. It validates drawable geometry, material/image ranges, resource
ceilings and the entire parent forest. It retains full double-precision composed
node matrices, shared logical mesh instances, indexed local bounds and the
canonical render bound. It does not parse sidecars or allocate GPU resources.

Placement applies `camera-relative root * exact grid part * recorded LOD basis
* node hierarchy`, with ticks converted once at 50/m. All 24 proper rotations
reuse DATA-01 utilities. Canonical physical/socket/anchor metadata never receives
the GLB basis. Composed nonuniform-scale/shear matrices remain full matrices;
invalid, ill-conditioned or shader-unsafe transforms reject. Both prepare and
place preserve the previous output on validation failure. Allocation exceptions
follow normal C++ behavior.

Defaults are hard limits that callers may only lower: 256 nodes/meshes/mesh
instances, 512 submeshes/expanded draws, 64 materials, 65,536 vertices, 196,608
indices, 2,048 texture dimensions, and separate 32 MiB decoded/GPU payload caps.
Coordinate magnitude is capped at 100,000 m; Frobenius condition bound is 1e6,
with additional float determinant/cofactor checks matching shader normalization.
Rigid normal-map scale is bounded to [0,16]. The bundle owner must also enforce
its smaller per-LOD and cumulative pontoon limits.

Resource counts distinguish owned decoded VMESH vectors, 72-byte GPU vertices,
u32 GPU indices regardless of source stride, 64-byte GPU materials, uploaded
base texture bytes and complete RGBA8 mip chains. Repeated mesh nodes do not
duplicate the asset upload. Repeated material slots do count separate uploads.
These figures exclude allocator/driver overhead, upload staging, fixture-wide
uniform/instance/fallback resources and temporary parse/preparation memory.

`MeshPath::loadMeshData` uploads the admitted snapshot without serializing it
again. It checks header/vector consistency, u64 count products, texture spans,
512 MiB owned-byte capacity and logical mesh allocation bounds first. Existing
path/byte APIs retain the same upload validation. Pending texture/asset guards
release resources if candidate construction fails or an allocation throws.
This is not general asset eviction, streaming or whole-scene publication;
the Application will own a complete candidate MeshPath and its frame-boundary
publication/retirement lifecycle.

## Confirmed camera/winding defect and scoped correction

The first real pixel test drew outward glTF triangles using the project's actual
`GLM_FORCE_LEFT_HANDED` camera convention. Their front was black; the reverse
side was visible. The original CCW front-face setting therefore classified
single-sided glTF geometry incorrectly for that camera. The earlier legacy test
only counted submitted draws, so it did not reveal this problem. Its synthetic
textured asset was uploaded but never instanced.

The fix adds validated `MeshPathConfig.frontFace`. **Default CCW remains for
legacy callers; admitted salvage glTF fixtures explicitly select CW.** No
vertices, indices, source UVs, normal-map channels or canonical bases are flipped.
The shader's existing cofactor inverse-transpose, tangent Gram-Schmidt and
handedness behavior are reused unchanged. Global camera/input migration is
outside this work.

The final diagnostic faces canonical −Z. In the declared −Z camera view, actual
LH lookAt has +X screen-right and +Y screen-up; the test checks those axes before
asserting red/green top and blue/yellow bottom quadrants. Both orthographic and
actual GLM-default perspective projection draw the expected texture. The reverse
side is black, and clearing instances produces a black target with zero draws.

The normal test draws a diagonal-normal quad with nonuniform model scale. It
checks actual pixels under changing light, then uses a perpendicular-light
oracle that would illuminate an incorrect model-times-normal result. A separate
sampled +bitangent chart reverses its highlight when authored tangent.w flips.
These checks establish actual texture/normal sampling, not just uploads or CPU
matrix equality. No fresh browser material draw or authored pontoon screenshot
is claimed here; root's application fixture must prove that separately.

## Reproduction and final results

From the repository root:

```sh
bash docs/validation/salvage/ASSET-04/prefab/build_standalone.sh \
  > docs/validation/salvage/ASSET-04/prefab/compile-final.log 2>&1
/tmp/salvage-prefab-build/rigid_prefab_tests \
  > docs/validation/salvage/ASSET-04/prefab/tests-final.log 2>&1
nix-shell --run 'bazel test -c opt --jobs=4 --test_output=all --test_env=WGPU_BACKEND=vulkan //tests:mesh_path_test' \
  > docs/validation/salvage/ASSET-04/prefab/mesh-gpu-reviewed.log 2>&1
```

The standalone strict GCC build and **13/13 ASan+UBSan cases passed**, with the
project's GLM macros. It links prebuilt GoogleTest archives; those archives were
not rebuilt with sanitizers. Tests ran on the normal host outside the tracing
sandbox so LeakSanitizer could complete; no sanitizer suppression was added.

The focused optimized Bazel build and **4/4 MeshPath cases passed**, including
three actual GPU cases on AMD Radeon 890M Graphics / RADV STRIX1 / Vulkan.
The legacy bike/rider/track case still submits its expected 77 draws and culls
the off-screen track instance. The log reports missing XDG_RUNTIME_DIR, but
the headless Vulkan adapter initialized and every case passed. An intentionally
truncated parsed texture logs an expected upload rejection.

Exact final source, artifact and log digests are in [summary.json](summary.json).
Notable final identities:

| Artifact | SHA-256 |
|---|---|
| `rigid_prefab.cpp` | `44b5b49f41feb67b3d083255a254ba2feceac352dd08354f77586fd09ff7b736` |
| `mesh_path.cpp` | `aeadc9f2aa7ced2a89080424c84303b10a5c1e584ca365002870baf76e16336a` |
| `test_mesh_path.cpp` | `c76523533273b9737074b4bb2411572e44574bddabf5b79737799fedebce1edf` |
| Optimized GPU test executable | `99d20eeab8402ac3f6e08950a05aae1ccd71e60baca04a2d7c15ffd9ae4e0730` |

Root separately owns integrated CMake/Bazel and actual WASM CPU admission
evidence. [Independent bundle/SHA source review](bundle-review.md) found no
confirmed admission/hash blocker and records important integration limits.

## Preserved failures and remaining acceptance

`compile-first.log` retains an explicit int8-to-double conversion warning.
`tests-before-review.log` records 11/12 passes and an over-specific expected
diagnostic: composed-transform rejection correctly occurred before bounds.
`tests-reviewed.log` records the preceding 13-case source pass. Root found a
Clang-only quaternion promotion warning during real WASM compilation; four
explicit double conversions corrected it, and the final sanitizer run above
uses that exact source. Root retains the WASM failure separately.

`mesh-gpu-first.log` preserves both pixel failures that exposed the camera's
front-face mismatch. `mesh-gpu-second.log` is the first corrected four-case pass;
the final log adds stronger inverse-transpose/handedness and enum validation.
No prior failure log was relabeled as final evidence.

Root reviewed CPU bounds, material scale and parsed-upload safety; those findings
were fixed and covered by regressions. All GPU children exited, and the shared
Bazel/GPU window was released to root. No plan checkbox or Git state was changed.
ASSET-04 still needs the real generated part, socket/volume review, immutable
package, Application lifecycle and matched native/browser fixture captures.
