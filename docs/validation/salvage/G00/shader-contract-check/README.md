# Shader contract correction — focused verification

The [first full check](../first-full-check/README.md) retained two failures in
old structural shader tests and a 300.1-second combined-suite timeout. Neither
shader changed. The corrected tests preserve filtered smooth normals, geometric
brick normals, RGB normal output with an independent fourth channel, and the
material load and normal consumption in both blit paths.

Executed from the repository root:

```sh
nix-shell --run 'bazel test --jobs=4 --test_filter=RaycastShaderTest.EmitsStableFilteredTerrainNormal:BlitShaderTest.UsesRaycastTerrainPatchNormal //tests:shader_raycast //tests:shader_blit'
```

Exit **0**; both selected cases passed. [Actual output](bazel.log) and
[source hashes](source-hashes.json) are retained. This targeted check does not
replace the complete mandatory suite.

Only the combined `voxy_tests` target now declares `size = "large"`. Its first
default fastbuild run was still executing cases at the medium timeout. The
large default allows 900 seconds rather than 300; no case, assertion, shader or
hook was disabled. See the [Bazel test runner specification](https://bazel.build/reference/test-encyclopedia)
and [independent review](../review.md). Full-suite completion remains required.
