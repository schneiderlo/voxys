# First mandatory full check — retained failure

Command: `nix-shell --run 'bazel test --jobs=4 //tests:voxy_tests //tools:terrain_diffusion_import_test'`.
Exit 3; the combined C++ test target reached the default 300-second timeout.
The separate Terrain Diffusion test target passed. This was a default
**fastbuild** run, not the older optimized test log at the stale
`bazel-testlogs` symlink. These logs were copied from the actual k8-fastbuild
path printed by this invocation.

The suite announced 1,498 cases but did not finish. Two completed cases failed:
`RaycastShaderTest.EmitsStableFilteredTerrainNormal` and
`BlitShaderTest.UsesRaycastTerrainPatchNormal`. They assert earlier literal
shader-source forms. Both current shaders are unchanged from base HEAD:
terrain normal remains RGB while LEGO top distance uses the fourth channel;
the blit reads `terrainMaterial` then extracts `.xyz` and normalizes it.
Original assertion files and tested source hashes are retained here.

The new `SalvagePreviewGpu.CompletedMetadataAllowsBoundedResetAndPreservesUnownedBody`
passed, including the Application allocation-range predicate after live-range
shrink. That focused success is not a complete-suite pass. G00 stays unchecked
until the corrected structural checks and appropriately sized full suite pass.
