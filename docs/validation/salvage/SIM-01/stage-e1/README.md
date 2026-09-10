# SIM-01 stage-e1 — complete owned assembly and physical cache identity

Implemented 2026-09-08 after G00 commit `7f28fab` on
`codex/salvage-implementation`. `summary.json` identifies exact uncommitted
sources, evidence, tool/runtime and test-artifact hashes. The component passes
its scoped checks. **SIM-01 remains pending independent final compiler review;
no gate pass or new gate commit is claimed.**

`CompiledAssembly` owns the complete functional plan and its matching mass,
exterior collision/BVH, overlap-aware buoyancy and source mappings. It also owns
the exact compilation profile and a versioned SHA-256 of validated physical
inputs. The [cache grammar](../cache-wire-v1.md) specifies every field, width,
ordering rule, module tag and deliberate exclusion.

The digest binds actual used part definitions, build/revision, durable parts and
connections, placement/health/settings, physical geometry/mass/socket/module
metadata and all twenty profile fields. A changed definition with the same
ContentKey cannot reuse the old identity. Source arrays canonicalize; floats
normalize signed zero. Hashing streams with bounded stack workspaces and no
serialization allocation. No pointer, C++ padding, native size_t representation
or platform-dependent derived mass sum enters the digest.

Owner/lease, paint, financial provenance, pricing, unused definitions and visual
metadata are excluded from this physical key. The complete input is still
validated before compilation; no hash-only authorization/admission API exists.
The digest cannot substitute for inventory, permissions, current visual asset
resolution or save recovery. There is no live cache activation or solver body
creation here.

## Executed checks

| Check | Actual result |
|---|---|
| Optimized Bazel | Six focused targets pass; 9 new final-assembly cases executed, previous five targets cached/pass |
| Native CMake | All 78 shared cases pass; native core library builds |
| Strict GCC, warnings as errors, undefined-behavior and float-cast-overflow checks | All 78 cases pass, none skipped/disabled |
| Actual WASM, JS exceptions and Asyncify | All 78 cases pass; fixed 64 MiB heap / 1 MiB stack; no skips/disabled cases |
| Shipping WASM core | Final compiled-assembly source compiles; core library builds |
| Actual allocation failures, each runtime | 74 failed cuts preserve serialized input, existing output and identity; eventual success |
| Actual read guards, each runtime | 7,000 frame, 9,000 socket and 1,000 identity/profile reads plus lookups allocate nothing |

The nine new cases include an independent explicit 670-byte input fixture,
37 same-key physical-definition mutations, all twenty profile fields, build/
revision/part/placement/rotation/condition/settings changes, deliberate cosmetic/
authority/unused-content exclusions with invalid provenance still rejected,
zero normalization, input/catalog/endpoint order equivalence, ten connection
state/strength/rope-length mutations, eighteen typed-module parameter changes,
and full owned-output copy/lifetime checks. Earlier 69 numerical/boundary cases
rerun on the final shared source set.

`golden-input.py` uses explicit values and Python `struct`/`hashlib`; it neither
imports production code nor reads C++ to derive fields. Its exact `.bin` and
`.json` are preserved. Native and WASM independently produce the expected SHA:

`8c9012e1400f5e21fe00fdb6ba3e9b2aed1e5155074800e770de2d0a773452e0`

The actual allocation probe retains the three-root winch/eye/propeller fixture
with seven frames, nine sockets and one 9,000 mm rope. Both runtimes produce
another exact identity:

`082d9b4a0d40408a6ea258a5a996440c2114d4a7e9269a55925afc9c27b042e7`

This second match and the independent golden fixture establish input-key
agreement for those inputs. They do not claim cross-platform solver determinism
or byte-identical floating-point mass results for every build.

## Retained failure

Bazel attempt 1 failed to compile a new copy-lifetime assertion because the test
named a collision-root field `geometry` instead of the actual `shape`. Only that
test field access changed; attempt 2 passes. No production limit/algorithm or
test count changed to obtain a pass. CMake, strict, WASM and both allocation
probes passed on their first attempts. Earlier component failures remain in
their own stage directories.

## Reproduce

From the repository root inside `nix-shell`:

```bash
bazel test -c opt //tests:compiled_assembly //tests:assembly_functions //tests:assembly_buoyancy //tests:assembly_collision //tests:assembly_compiler //tests:build_model --test_output=errors
cmake --build build-salvage-native --target compiled_assembly_tests assembly_functions_tests assembly_buoyancy_tests assembly_collision_tests assembly_compiler_tests build_model_tests voxy_core -j 8
build-salvage-native/bin/compiled_assembly_tests
build-salvage-native/bin/assembly_functions_tests
build-salvage-native/bin/assembly_buoyancy_tests
build-salvage-native/bin/assembly_collision_tests
build-salvage-native/bin/assembly_compiler_tests
build-salvage-native/bin/build_model_tests
bash docs/validation/salvage/SIM-01/stage-e1/build-standalone.sh
/tmp/salvage-compiled-tests
bash docs/validation/salvage/SIM-01/stage-e1/build-allocation-faults.sh
/tmp/salvage-compiled-faults
```

For WASM use recorded SDK/Node paths and fresh directories:

```bash
python3 scripts/validate_assembly_compiler_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-01/stage-e1/run-wasm-faults.py --shared-validation <fresh-shared-directory> --output <fresh-fault-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_core -j 8
```

Shared manifests retain commands, source/tool/artifact hashes and frozen-input
checks. `golden-input.py` reproduces the independent fixture; preserve original
artifacts and compare regenerated bytes separately when reviewing.

Next is [independent compiler review](../REVIEW_HANDOFF.md), followed by resolving
findings and affected checks before SIM-01 acceptance. The combined repository
hook suite is still required for a successful gate commit; this checkpoint is
focused CPU/compiler validation only. No GPU/backend activation, propulsion,
live flood/capture behavior, scene-wide memory admission, product performance
or visual quality is certified. The cove art and LOOK-01/G02 remain unapproved.
