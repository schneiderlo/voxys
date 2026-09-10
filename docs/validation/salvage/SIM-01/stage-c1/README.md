# SIM-01 stage-c1 — buoyancy coverage with complete provenance

Implemented 2026-09-08 after G00 commit `7f28fab`, on
`codex/salvage-implementation`. `summary.json` records exact uncommitted source
and evidence hashes. **SIM-01 remains unfinished and no gate commit is claimed.**
Independent final compiler review remains outstanding.

This component prepares overlap-aware displacement geometry. It does not sample
water, implement flooding, compute submersion, apply forces or activate a solver.
Those mechanics retain their later SIM/MECH gates.

## What changed

`BoxCoverage` partitions orthogonal input regions into disjoint cells. Every
cell keeps all source labels covering its entire interior. Existing cells split
at each new intersection; their residual pieces preserve their contributor list,
while the common piece adds the new source. Previously uncovered volume becomes
new cells. The final bounds-sorted cell array references canonical sorted labels
in one owned flat pool. Empty coverage is a valid no-displacement result.

Unlike collision's least-source ownership, this preserves a solid core inside a
sealed region. If the seal later becomes inactive, the core remains available
to displace water. Overlapping active regions are counted once. Hypothetical
source-activation fixtures verify this numerical property; they are not live
flooding gameplay or a chosen partial-flooding approximation.

`AssemblyBuoyancyPlan` owns the collision/mass plan from the same build/catalog
validation and adds coverage per welded root. Each source retains durable part,
definition key, definition-scoped buoyancy region ID, root, solid/sealed kind,
exact root-from-region frame, local half extents and root-space bounds. No
borrowed catalog/input pointers survive. Disabled welds, ropes and authored
latches do not silently merge independent bodies' displacement.

Exact clipping primitives moved into the internal
`orthogonal_geometry.hpp` so collision and coverage share one implementation of
bounded box/face subtraction. All existing collision/mass/build cases rerun on
that change. Historical stage-b1 source hashes remain historical evidence.

## Bounds and failure behavior

Whole-build caps: 2,048 source regions, 4,096 coverage cells, 32,768 contributor
references, 8,388,608 clipping tests and 8,388,608 reference writes. Copying labels
through scratch/final packing counts against the reference-write budget. Each
box scratch list caps at 4,096 pieces. All roots consume the same remaining
generated/work budgets; an empty later root can succeed with no output slots.

Cells and reference arrays use two bounded working pools. Vector growth slack,
both pools, source copies and box scratch lists must be included in preparation
memory accounting. These generated/request bounds do not claim a measured RSS,
GPU or scene-wide live-plus-retiring memory budget. Root coordinates remain
within ±256 m per axis. Excessive input, work or fragmentation returns a typed
failure; no incomplete candidate or canonical mutation is published.

Consumers receive const spans into owned output; keep the owner alive and do
not move/replace it while using borrowed spans. Reading source identities and
contributor lists performs no allocation and changes no authority.

## Executed evidence

| Check | Actual result |
|---|---|
| Optimized Bazel | 13 buoyancy/coverage and 12 collision cases pass; 11 mass and 23 build cases cached/pass |
| Native CMake | All 59 shared cases pass; native core library builds with new/shared sources |
| Strict GCC, warnings as errors, undefined-behavior and float-cast-overflow checks | All 59 cases pass |
| Actual WASM with JS exceptions and Asyncify | All 59 cases pass; no skips/disabled cases; fixed 64 MiB heap / 1 MiB stack |
| Shipping WASM core library | Buoyancy, coverage and refactored collision primitives compile |
| Actual allocation-failure probe, each runtime | 70 failure cuts preserve canonical input and an existing complete buoyancy plan; eventual success |
| Actual read guard, each runtime | 8,000 source/contributor reads across 1,000 numerical solid-volume checks allocate nothing |

Seven coverage tests compare complete label sets to an independent integer-cell
oracle. They cover multiple intersections, nested/identical source bounds,
24 deterministic six-box mixed fixtures, reversed input order, every proper
rotation, enclosed cavities, empty coverage, ownership/copy lifetime and every
input/output/scratch/work budget. All 64 hypothetical active-source subsets of
each six-box mixed fixture match the oracle without double-counting volume.

Six integrated tests use actual canonical build/catalog validation: overlapping
sealed/solid regions preserve the core; both whole-build and authored region
rotations compose once; absent buoyancy does not invent collision-box flotation;
256 parts retain exact source/cell/reference counts; generated/work budgets span
multiple roots; and loan/condition metadata does not erase displacement while
current catalog changes are actually used.

The overlapping synthetic two-part fixture retains 480,000 ticks³ of all-active
displacement and 8,000 ticks³ when only its solid source is considered. Dry mass
stays 180 kg. It uses deliberately hollow analytical occupancy; this is not an
approved game asset or live captured cargo. A separate frame fixture produces
the exact root bounds `(5,1,-7)` to `(15,7,13)` after composing two rotations.

The failure probe uses three parts across two roots. Its first root has seven
cells and eight contributor references; total all-active volume is 960,000
ticks³, with two clipping tests and 19 counted reference writes. Actual global
new/new[]/aligned-new failures cover validation, collision, working coverage
pools and final owned publication. The source build serializes identically and
an existing plan's data remains intact after every failed attempt.

All tests, probes and the independent unit-cell method were authored by the
implementer. A separate reviewer has not accepted the complete compiler.
No failures occurred in these stage-c1 first attempts. Earlier stage failures
remain in their own folders and have not been overwritten or hidden.

## Reproduce

From the repository root inside `nix-shell`:

```bash
bazel test -c opt //tests:assembly_buoyancy //tests:assembly_collision //tests:assembly_compiler //tests:build_model --test_output=errors
cmake --build build-salvage-native --target assembly_buoyancy_tests assembly_collision_tests assembly_compiler_tests build_model_tests voxy_core -j 8
build-salvage-native/bin/assembly_buoyancy_tests
build-salvage-native/bin/assembly_collision_tests
build-salvage-native/bin/assembly_compiler_tests
build-salvage-native/bin/build_model_tests
bash docs/validation/salvage/SIM-01/stage-c1/build-standalone.sh
/tmp/salvage-buoyancy-tests
bash docs/validation/salvage/SIM-01/stage-c1/build-allocation-faults.sh
/tmp/salvage-buoyancy-faults
```

For WASM use SDK/Node paths from `wasm-attempt01/manifest.json`, fresh output
directories and the recorded exception policy:

```bash
python3 scripts/validate_assembly_compiler_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-01/stage-c1/run-wasm-faults.py --shared-validation <fresh-shared-directory> --output <fresh-fault-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_core -j 8
```

Manifests preserve exact commands, binaries, source/tool hashes and runtime
policy checks. Combined test targets register the new cases, but this checkpoint
does not claim a complete repository hook-suite run or any live GPU test.

Next: module and joint frames, final immutable assembly output with complete
physical-content/topology cache identity, then full compiler validation/review.
Later mechanics must decide flood/submersion state at a certified tick using the
retained contributors, without silently adding cargo/region mass twice. The
cove's placeholder visuals and all product quality gates remain open.
