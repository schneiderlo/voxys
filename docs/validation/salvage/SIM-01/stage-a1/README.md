# SIM-01 stage-a1 — welded roots and analytical dry mass

Implemented and checked 2026-09-08 after G00 commit `7f28fab`. Current branch is
`codex/salvage-implementation`; `summary.json` binds exact uncommitted source and
validation artifacts. **SIM-01 remains open. No gate has passed or been committed.**

This component provides `AssemblyMassPlan`, an owned, read-only intermediate
result. It validates the input build against the supplied catalog, groups enabled
welds, preserves stable part/root mappings and computes each root's dry mass,
root-local COM and full tensor about COM. It does not expose a complete physical
assembly or activate a solver. See [compiler design and remaining stages](../design.md).

Roots use the least member part ID and its lattice translation as anchor; root
axes stay parallel to build axes. The anchor part's rotation remains in its own
part frame. All outputs are canonical-ID ordered. Common grid translation is
removed before conversion to double precision. Compensated sums and a two-pass
parallel-axis calculation retain off-diagonal terms without subtracting large
world-space second moments. The six independent tensor entries are mirrored
exactly. Aggregate physical validity is checked after normalization, avoiding
the catalog's per-part absolute magnitude ceiling.

Rope/latch links do not merge dry roots. Damage, health, paint, module enablement
and loan provenance do not silently remove mass; accepted topology changes are
required. A root key is derived member identity scoped by build/revision, not a
new durable object or backend handle. Output owns all values and retains no
catalog/input pointers. A changed catalog with the same keys is revalidated and
its current mass values are used; cache identity is deliberately not implemented.

Per-call caps are 256 parts, 64 roots and transformed footprint coordinates
within ±256 m of each root anchor on every axis. Invalid profiles, builds,
empty builds, excessive roots/parts, excessive extent and invalid aggregate mass
return explicit issues. These bounds do not reserve scene-wide resources or
declare disconnected builds launchable.

## Executed checks

| Check | Result |
|---|---|
| Optimized Bazel | New 11-case compiler target passes; existing 23-case build target cached/pass |
| Native CMake | All 11 compiler + 23 build cases pass; core library builds with registration |
| Strict GCC, warnings as errors, undefined-behavior and float-cast-overflow checks | All 34 cases pass |
| Actual WASM with JS exceptions and Asyncify | Same 34 cases pass, no skips/disabled; fixed 64 MiB heap / 1 MiB stack |
| Shipping WASM core library | New compiler source builds in the current application configuration |
| Actual allocation-failure probe, native and WASM | All 21 allocation cuts preserve input and an existing plan; eventual success. Over-part-cap preflight makes zero allocation attempts |
| Independent rational reference | `make-golden.py` reproduces `asymmetric-golden.json` byte for byte; full asymmetric C++ result matches |

The 11 compiler cases exercise two unequal masses with nonzero local COMs and
full off-diagonal inertia, all 24 whole-build rotations with independently dense
long-double comparison, actual ballast relocation, equivalent insertion and
endpoint order, disconnected/disabled welds, rope/latch separation, condition/
provenance neutrality, ±2,000,000,000-tick common translations, current-catalog
revalidation, typed errors, 64/65-root and 256/257-part limits, and output lifetime
after both catalog and input destruction.

The Python rational fixture and C++ test use separately expressed arithmetic;
they were authored by the same implementer. This is an independent numerical
method, **not** a separate reviewer. The allocation probe replaces actual global
new/new[]/aligned-new rather than supplying a fake allocator to the API.

## Preserved failure

`cmake-attempt01.log` failed configuration because the new test-discovery call
was placed before the existing GoogleTest module import. Moved discovery beside
the other registered discovery calls; `cmake-attempt02.log` passes. No test or
warning was disabled. Bazel, strict and WASM first attempts passed. The tests
have not been run as the entire GPU/CPU repository hook suite at this stage.

## Reproduce

From the repository root inside `nix-shell`:

```bash
bazel test -c opt //tests:assembly_compiler //tests:build_model --test_output=errors
cmake --build build-salvage-native --target assembly_compiler_tests build_model_tests voxy_core -j 8
build-salvage-native/bin/assembly_compiler_tests
build-salvage-native/bin/build_model_tests
bash docs/validation/salvage/SIM-01/stage-a1/build-standalone.sh
/tmp/salvage-assembly-tests
bash docs/validation/salvage/SIM-01/stage-a1/build-allocation-faults.sh
/tmp/salvage-assembly-faults
python3 docs/validation/salvage/SIM-01/stage-a1/make-golden.py
```

For WASM, take SDK/Node paths from `wasm-attempt01/manifest.json`, choose fresh
output directories and run:

```bash
python3 scripts/validate_assembly_compiler_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-01/stage-a1/run-wasm-faults.py --shared-validation <fresh-shared-directory> --output <fresh-fault-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_core -j 8
```

Runners record exact commands, source/tool hashes, binaries and exception-policy
checks. Preserve older outputs. Combined Bazel/CMake suites register the new
cases; this checkpoint does not claim a full repository suite execution.

## Remaining work

Implement bounded exterior collision/child acceleration and stable contact→part
mapping; transform and union buoyancy without double-counting; compile module
and non-weld joint frames; create complete immutable assembly output and physical
content/topology cache identity. Then validate the full compiler and complete
review before accepting SIM-01. SIM-02 backend mass/shape support, GameSession
prepared activation and real floating/propelled machines remain separate tasks.

The cove is unchanged placeholder scenery. No gameplay, art approval, GPU
simulation or performance improvement is claimed by these CPU analytical tests.
