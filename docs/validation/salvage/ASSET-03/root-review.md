# Independent importer review

Reviewer: root, 2026-09-07. Read the complete importer and its new tests, the
strict-profile design, existing VMESH/shader UV contract, and retained matrix
and image-row failures. **No remaining blocking defect found for the declared
bounded rigid profile after the fixes below.** Runtime rendering/asset quality
are separate ASSET-04 and LOOK-01 acceptance.

## Reviewed boundaries

- Accessor spans prove buffer-view bounds, offsets, element size and stride
  before constructing a pointer. Every present attribute count is checked
  against POSITION before component reads or primitive-sized allocation.
- Strict GLB preflight bounds input/JSON, rejects duplicate keys, unsupported
  features and external dependencies, and validates the one-scene forest
  before TinyGLTF loads images. Loader callbacks independently deny file I/O.
- Image dimensions and cumulative decode bytes are checked before decode;
  repeated material copies have their own output cap. One additional bounded
  local image copy exists before the copy-budget rejection; the caps do not
  claim an exact process peak-memory bound.
- Matrix extraction uses column-major glTF axes, proper positive-scale rigid
  transforms, normalized quaternion sign and a reconstruction check. The tests
  cover trace and three dominant-axis branches plus an asymmetric transform.
- Strict images retain top-row-first glTF ordering and unchanged UVs. Legacy
  image rows and already-cooked scene assets remain unchanged. Unsupported
  material/sampler/texture/attribute behavior rejects explicitly.
- The two-path legacy API/CLI remains callable. The salvage wrapper explicitly
  selects the versioned rigid profile; independent wrapper review is separate.

## Review-driven correction

Root identified that finite geometry/UV inputs could overflow tangent synthesis
and that raw STB pixels could leak if the destination-vector allocation threw.
The author retained actual pre-fix CLI reproductions. Tiny UVs selected an old
finite fallback; a large finite geometry fixture actually emitted a zero
tangent. The fix rejects nonfinite synthesis intermediates and nonfinite/zero
final generated tangents, and immediately puts STB pixels under RAII ownership.
The new regression exercises both profiles; the original legacy cases remain
passing. Root reviewed the fix and the final integrated outputs.

This finding does not claim the first suggested fixture emitted NaN: the
retained observations distinguish the hypothesis from the actual zero-vector
defect. See `tangent-degenerate-before-fix/`, `tangent-overflow-before-fix/` and
`tangent-reviewed/`.

## Executed integrated evidence

Root independently built and ran all33 converter cases and all23 actual-tool
Python cases through optimized Bazel and CMake. Both passed without skipped
cases. Two complete probe cooks with the final optimized binaries are byte
identical. An unknown CLI profile exits2 without creating output. Exact source,
tool, manifest, case logs and XML are in `wrapper-integration/`. The author
also supplies a separate strict-warning ASan/UBSan run; this review does not
substitute ordinary optimized execution for sanitizer evidence.

Some diagnostics identify a field/feature rather than a complete indexed JSON
path. No full glTF support, exact offline peak memory, Windows authoring, GPU
material correctness, mip-chain production or final art approval is claimed.

Reviewed source SHA-256:

- `tools/gltf_vmesh_tool.hpp`: `5b811defbf6b84bf0a49356ae320bff5f9ba84dba41b49b3867d5610fedf0df9`
- `tools/gltf_vmesh_tool.cpp`: `c678329d2115f499871a377932a15ba5526ad7d490e45c9df5f9579378b620b3`
- `tests/test_gltf_vmesh_tool.cpp`: `ace4e29359aa8e7cd3d586b2730fe28afb4f04d3b4ec8bc5b830a87a72d6c1a8`
- `src/moto/vmesh.hpp`: `0b73c608ad72f2c101032ca2bf1c3eddd83e1d1a9212c29b36f650918c08f707`
