# DATA-03 integrated acceptance

Root completed focused integration after G00 commit `7f28fab`. DATA-03 is
complete; G01 remains pending DATA-04–06. These changes will be committed at a
subsequent successfully verified gate.

The standard-library-only `build_model` library is registered in Bazel and the
CMake game sources. A lean CPU test target exists in each build system; the
same test source is also registered in both combined suites.

| Executed check | Result | Evidence |
|---|---|---|
| Optimized native and WASM Bazel library builds | Both exit 0 | [Build log](native-wasm-library-build.log) |
| Optimized `//tests:build_model` | 23 passed, zero failed/skipped | [Command](bazel-command.log), [cases](bazel-test.log), [XML](bazel-test.xml) |
| CMake `build_model_tests`, CTest prefix `build_model.` | 23 passed, zero failed | [Build and CTest log](cmake-command.log) |
| Independent strict GCC and runtime sanitizer run | 23 passed; no diagnostics | [Golden fixture report](../golden-fixture.md) |
| Independent fixture recipe verification | 509 bytes match retained and embedded fixture | [Recipe](../generate-wire-fixture.py), [recorded output](../golden-recipe-check.log) |

[Summary](summary.json) records the exact commands, source/artifact hashes and
resolved Bazel test path. The Bazel XML identifies the actual optimized run at
2026-09-07T23:03:10.801; this is not a stale combined-suite symlink. The final
test source is `36f69f12d5ee451013cc7d9a45795d9f87ad10568d016c68b32003ed00da29bf`.

Root reviewed the shared build changes and earlier model correctness findings;
the [independent correctness review](../review.md) found no remaining blocker.
The recommended independent full wire fixture has now been added and exercised
by all three CPU test builds. It covers asymmetric rotations, exact integer
placement, lossless 64-bit state, provenance and distinct winch/rope fields.
Other cases cover analytic overlap, exact engaged spacing, atomic invalid edits,
revision handling, canonical ordering and design-only blueprint duplication.

WASM acceptance here is compilation only. No browser gameplay, live inventory,
session authority, physics, persistence journal or visual acceptance is claimed.
The next gate still requires its full repository checks and enabled commit hook.
