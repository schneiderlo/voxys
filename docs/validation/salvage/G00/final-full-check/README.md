# Mandatory complete repository check

**Passed, exit 0.** Executed from the repository root with the existing Nix
toolchain and default native fastbuild configuration:

```sh
nix-shell --run 'bazel test --jobs=4 //tests:voxy_tests //tools:terrain_diffusion_import_test'
```

The combined C++ target completed in **326.9 seconds**: **1,495 cases passed,
3 skipped, zero failures**, with **4 pre-existing disabled cases**. GoogleTest
announced 1,498 enabled cases; XML includes the four disabled entries for 1,502
total records. The separate Terrain Diffusion target passed from Bazel cache;
its retained output reports 11 Python cases with one skip. No filter was used,
and no test was disabled to obtain this result.

See [Bazel output](bazel.log), [combined output](voxy_tests-test.log),
[combined XML](voxy_tests-test.xml), [importer output](terrain_import-test.log),
[importer XML](terrain_import-test.xml), and [machine-readable summary](summary.json).
These are the actual **k8-fastbuild** results, not older optimized logs.

## Skips and coverage limits

- `WindowGLFWTest.WindowCreation`: GLFW window creation was unavailable in
  the Bazel test environment. Separate visible native BOOT-05 launches passed.
- `GpuBroadPhaseOverflowBenchmark.DenseGridCandidateClamp` and
  `RaycastPathGPUTest.LegoHorizonBenchmark`: existing opt-in benchmark environment
  variables were not set. These are not performance acceptance results.
- Existing `ApplicationGPUTest.DISABLED_InitAndShutdown`, `DISABLED_RenderPathToggle`,
  `DISABLED_ProcessSingleFrame`, and `DISABLED_UpdateCallback` remain disabled.
  This task did not alter those definitions. New real GPU lifecycle and actual
  browser/native route evidence are recorded separately; they do not pretend
  that disabled cases ran.
- The cached Python importer output reports one skipped case without printing
  its reason. The existing LDH-writer header case is the source's only
  conditional skip; no new skip was added. Its exact runtime exception text
  was not captured, so this report does not invent that detail.

The earlier 300.1-second timeout and obsolete structural shader expectations
remain preserved under [first attempt](../first-full-check/README.md). Only the
combined target's test-size declaration was raised to allow its full workload;
both corrected shader-contract cases passed in this complete run.

## Tested source and final catalog assertions

[Source hashes](source-hashes.json) identify the final app/readback helper,
catalog, expanded catalog tests, preview tests, shader checks and unchanged
hook. The combined executable SHA-256 is
`574bb8e969885d155d9b89764d391bec35a7739eee7f5b44726d9aa3422e5479`.

The expanded Winch test source (`b0cd3d3d…`) was last modified at
22:42:34.692831 UTC, before its object compiled at 22:44:04.744424 UTC and the
combined binary linked at 22:44:08.451366 UTC. The summary retains timestamps
and hashes. The later agent freeze acknowledgement was not a later source edit.
All sixteen catalog cases passed here and in the separate expanded sanitizer
run recorded by [DATA-02](../../DATA-02/winch-review.md).

The actual pre-commit hook must still run the same two targets when committing.
That checkpoint result is recorded in the parent handoff after execution.
