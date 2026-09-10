# ASSET-02 integrated acceptance

**Complete for the offline metadata and cooking scope.** Root integrated and
verified the independently reviewed implementation in both normal build systems
on Linux. ASSET-03 profile enforcement and ASSET-04 runtime rendering remain
separate, incomplete requirements. This work follows G00 commit `7f28fab` and
awaits the next successfully verified gate commit.

| Executed check | Result | Evidence |
|---|---|---|
| Optimized Bazel standalone parser | 9 passed | [Final cases](final-bazel-parser.log), [XML](final-bazel-parser.xml) |
| Optimized Bazel actual converter/cooker | 20 passed | [Final cases](final-bazel-cook.log) |
| Optimized Bazel combined suite, `GameplaySidecar.*` | 9 passed | [First command](combined-bazel-command.log), [cases](combined-bazel-cases.log) |
| CMake standalone parser and actual cooker | 9 parser + 20 Python cases passed | [Final CTest case log](final-cmake-cases.log) |
| CMake combined suite, `GameplaySidecar.*` | 9 passed | [Final build/CTest/direct invocation](final-cmake-command.log) |
| Two cooks with integrated Bazel tool binaries | Every bundle file byte-identical | [Cook log](integrated-cook-command.log), [retained bundle](cooked-probe-bazel/cook-manifest.json) |

The [final Bazel invocation](final-bazel-command.log) verifies all three targets
after the reviewed Linux registration correction: both standalone targets execute
again; the unchanged combined nine-case result is cached. There are no failures
or skipped cases. These focused combined runs do **not** claim the full repository
suite passed on these changes. The enabled full hook runs at the next passed gate.

[Summary](summary.json) binds exact final source, shared build, executable and
bundle identities. Normalized metadata and VMESH match the preceding standalone
cook; the manifest differs because the actual optimized tool binaries differ.
The original [standalone evidence](../report.md) and [independent implementation
review](../review.md) are retained with their own source identities.

The [independent integration review](build-review.md) confirmed that all nine
parser cases enter the mandatory combined C++ suite, without altering its hook.
The twenty-case Python suite has its own required asset-task target. Both real
tool executables, the source GLB and the sidecar are declared Bazel runfiles.
Root changed only fixture discovery in the reviewed Python test to use that
runfiles tree; the original test source is retained as
[reviewed-test-source.py](reviewed-test-source.py), SHA-256
`1ed5e7976a1cfa5e2f1e9bf41cb19e0eb10bc4c1d0c81d99a88d2431b7eb6462`.

The review caught initial registration of POSIX-only integration fixtures on
Windows. Bazel and CMake now restrict that particular test to Linux; the parser
and validator targets remain independently available. Windows authoring execution
is unverified, and Windows game release requirements remain in the plan.
CMake tools-disabled builds retain parser tests but omit the real-tool cook
test because its executables are unavailable; this is documented option behavior,
not a passing cook test.

No source assets were overwritten, added to the game scene or visually approved.
Repeated-cook equality establishes these declared inputs/tool binaries, not all
platforms or future compiler versions. Runtime hierarchy evaluation, cooked
manifest verification, visual/socket fit and broader immutable content registry
work remain required before authored parts can be used in gameplay.
