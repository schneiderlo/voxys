# ASSET-02 — independent build integration review

Reviewer: `lego_gameplay`, 2026-09-07.

**Final source integration approved; the platform-registration finding is
resolved.** The twenty-case integration fixture uses POSIX FIFOs and shell
executables. Root added a Linux target constraint in Bazel and a Linux host
condition in CMake. The parser and CLI targets remain independently available;
Windows authoring-test execution stays explicitly pending.
Root owns the actual focused Bazel/CMake checks and their outcome. The retained
CMake log already proves its nine standalone parser cases and twenty-case cook
suite pass. Root also reports passing focused Bazel and combined parser runs;
their exact logs and the final metadata recheck remain root's evidence handoff.
This reviewer did not execute those builds/tests. No build,
Bazel invocation, GPU use or shared-source edit was performed by this reviewer.

## Mandatory and standalone coverage

All nine `GameplaySidecar` cases in
`tools/salvage_assets/test_gameplay_sidecar.cpp` enter the mandatory combined
Bazel suite explicitly through `tests/BUILD:681`. The combined target also
depends on `//tools:gameplay_sidecar_lib` and declares
`//tools:gameplay_sidecar_fixture` as run data. No filter or skip was introduced.
The unchanged pre-commit hook still executes `//tests:voxy_tests`, so these
nine cases are within its normal case list.

`//tools:gameplay_sidecar_test` separately builds the same nine cases with the
parser library, shared catalog/types and GoogleTest, and declares its fixture.
`//tools:cook_gameplay_asset_test` declares the actual converter, actual validator,
sidecar fixture and unchanged probe GLB. Its two Python sources are declared,
and explicit tool paths come from Bazel `rootpath` expansion. The probe's new
filegroup is independent of `//data:data`; this does not silently ship the
authoring fixture in the game scene package.

The twenty-case Python integration target remains a separate task check; it is
not one of the unchanged hook's two targets. Root must retain its focused result
rather than implying those twenty cases ran within the combined C++ suite.

## Runfiles correction

Compared the current test with
[the retained reviewed source](reviewed-test-source.py), whose SHA-256 is
`1ed5e7976a1cfa5e2f1e9bf41cb19e0eb10bc4c1d0c81d99a88d2431b7eb6462`.
The only code delta selects `TEST_SRCDIR/TEST_WORKSPACE` when running under
Bazel and loads the cooker module beneath that root. It leaves all twenty
cases, fixtures, rejection assertions and actual tool invocation intact.
Outside Bazel it retains repository-root discovery from the script path.

This prevents source symlink resolution from making undeclared source-tree
fixtures appear available in a Bazel test. Under Bazel's test contract, the
working directory is the workspace runfiles root, so the declared relative
`rootpath` executable values resolve to the supplied tools. The focused Bazel
run remains the practical verification of this registration. Missing actual
tools still fail setup; there is no fallback mock or silent skip.

## CMake options and dependencies

Root CMake adds the tools directory only with `VOXY_BUILD_TOOLS`; it adds tests
only with `VOXY_BUILD_TESTS` on non-Emscripten builds and supplies Python before
entering `tests/CMakeLists.txt`. The sidecar CLI is non-Emscripten and requires
the existing tinygltf target. Its sources are the parser/main, construction
modules and VMESH reader; it does not link the renderer or GPU runtime.

Parser tests are available when tests are enabled, including tools-disabled
builds. Their standalone target and the combined `voxy_tests` contain the parser
source and all nine test cases, use existing JSON includes and preserve project
warning settings. CTest discovers the standalone cases with the repository as
working directory, where the fixture's fixed relative path is valid. Combined
CTest discovery already uses that same directory.

The `salvage_asset_cook` CTest entry is registered only on Linux when both actual
tool targets exist. It uses target-file generator expressions and explicit tool
environment variables, rather than depending on old `/tmp` binaries. Therefore
`VOXY_BUILD_TOOLS=OFF` deliberately omits this real-tool integration test while
retaining parser tests; it does not claim a skipped cook as a pass. Its 60-second
CTest limit and the small Bazel target bound this short CPU fixture workload.
The initial registration relied only on tool-target existence and would have
scheduled POSIX-only fixtures on Windows. The reviewed Linux conditions now
make this limitation explicit in both build systems. This limits the offline
integration test; it does not remove Windows from the game's release contract.

Bazel's validator uses the existing `//src/moto:moto` library to obtain
`vmesh_io.cpp`. That closure contains additional CPU motorcycle modules and GLM,
but no renderer/physics/GPU dependency. The parser uses only construction
modules and the existing tinygltf JSON dependency. The broad CMake dependency
of the existing GLB converter on `voxy_core` predates this change; the new
validator does not reproduce that dependency. No new web scene/runtime linkage
or asset-shipping claim is made.

## Reviewed integration identities

| File | SHA-256 |
|---|---|
| `tools/BUILD` | `38bf0d7ce1412f8bdb01e85549a0181192112583f0d61963c7f912e0898ce0b3` |
| `data/BUILD` | `ddb1b7726416e0a0846d89eac1bf5884a73cefa2a7f9661de5763b4a6ee8ce60` |
| `tools/CMakeLists.txt` | `95e9afd9a1692b1ad5075b989919adfb97617afa7eb29b259d36e306f7d840bb` |
| `tests/BUILD` | `fdc7855d7251db6658edd88ca2b0ff4024b7c19f73af4c1bd808eea9907ca1c3` |
| `tests/CMakeLists.txt` | `d239d658cf44b964607bb7adea8bb121a2236678b863664ffd0d3aac0abd8629` |
| Current Python integration test | `aa433ce65468762f21f5fc75caca61b69f7ec0c033f1751a2977bf2f25a2fb94` |

The build files also contain root's separately accepted DATA-03 registration;
this review did not change or duplicate that implementation. The retained
[CMake command log](cmake-command.log) reports ten passing CTest entries; the
[case log](cmake-cases.log) shows that these mean nine parser cases plus the
twenty-case Python suite using the newly built real tools. Keep the final Bazel,
combined-suite and platform-metadata recheck outcomes with root's integration
report before marking ASSET-02 complete. The independent
offline implementation review remains in [review.md](../review.md), with its
original and final URI follow-up identities preserved separately.
