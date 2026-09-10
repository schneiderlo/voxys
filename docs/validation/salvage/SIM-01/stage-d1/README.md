# SIM-01 stage-d1 — module and connection frames

Implemented 2026-09-08 after G00 commit `7f28fab` on
`codex/salvage-implementation`. Exact uncommitted source/tool/evidence hashes are
in `summary.json`. This component is validated; **SIM-01 and all later gates
remain open**. No new gate commit or independent final compiler review is claimed.

`AssemblyFunctionPlan` owns the same invocation's buoyancy/collision/mass plans
plus all module parameters/settings/health/strength, root-local functional
frames, every socket and canonical connection bindings. See
[the complete data/ownership contract](../function-design.md).

Thrust uses its authored frame's −Z; socket outward/key axes remain +Y/+X.
Ropes keep their attached rest length independently of a winch's default payout.
Disabled links retain their identity, condition and reserved socket slots.
Welds already belong to one mass root. Rope/latch links can reference distinct
or identical roots; neither silently creates a live joint or captures cargo.
All returned arrays are owned and read-only. No solver handle, force, inventory
mutation or live simulation tick is produced here.

## Executed checks

| Check | Actual result |
|---|---|
| Optimized Bazel | Five focused targets pass; new 10-case target executed, prior four targets cached/pass |
| Native CMake | 10 functional + 13 buoyancy + 12 collision + 11 mass + 23 build cases pass; native core builds |
| Strict GCC, warnings as errors, undefined-behavior and float-cast-overflow checks | All 69 cases pass; none skipped/disabled |
| Actual WASM with JS exceptions and Asyncify | All 69 cases pass; fixed 64 MiB heap / 1 MiB stack; no skips/disabled cases |
| Shipping WASM core | New functional compiler compiles and core library builds |
| Actual allocation failures, each runtime | 74 failed allocation cuts preserve serialized input and existing plan; eventual success |
| Actual read guards, each runtime | 7,000 frame and 9,000 socket reads, identity lookup and connection access allocate nothing |

Ten new cases exercise all eleven module types across twelve starter parts,
including copied power/torque/drag/thrust/capture/repair limits. A dense integer
matrix oracle independently evaluates root-local positions and orientations for
every proper rotation, asymmetric authored force/operator frames and very large
common build translations. Thrust directions are checked separately.

Other cases verify input/connection order and endpoint reversal, disabled welds,
damaged links, rope length preservation with separate/shared roots, distant
latches remaining separate, exact per-call budget boundaries and nested error
reporting. A 256-propeller build reaches all 768 function frames; another
256-part fixture reaches 8,192 sockets and 1,024 connections with 2,048 occupied
endpoint slots. Source/copy lifetime, current same-key catalog changes, disabled
zero-health engines, loan provenance and missing lookup behavior pass.

The isolated allocation probe uses three rotated independent roots: winch,
tow eye and propeller, with one reversed-endpoint authored rope. It returns
three modules, seven frames, nine sockets and one canonical connection with a
9,000 mm rest length. Actual global replacement new/new[]/aligned-new failures
cover all nested preparation allocations and all final functional arrays.
Guarded read loops check complete frame/socket identity and length preservation.
This is CPU data evidence, not live rope or propulsion gameplay.

## Failed first attempts retained

The first Bazel/CMake/strict run failed one stress fixture: 1,024 actual welds
in a tall build exceeded the existing canonical `candidatePairs` work budget.
The expected success was wrong. The test retains that layout as an explicit
Capacity/`candidatePairs` rejection and adds a permitted 255-weld/769-rope
layout to exercise record/socket maxima. Production limits and code were not
relaxed, tests were not removed, and the first logs/XML are retained. Attempt 2
passes in all native configurations. WASM and both allocation probes passed on
their first attempts. No production compiler source changed between attempts.

## Reproduce

From the repository root inside `nix-shell`:

```bash
bazel test -c opt //tests:assembly_functions //tests:assembly_buoyancy //tests:assembly_collision //tests:assembly_compiler //tests:build_model --test_output=errors
cmake --build build-salvage-native --target assembly_functions_tests assembly_buoyancy_tests assembly_collision_tests assembly_compiler_tests build_model_tests voxy_core -j 8
build-salvage-native/bin/assembly_functions_tests
build-salvage-native/bin/assembly_buoyancy_tests
build-salvage-native/bin/assembly_collision_tests
build-salvage-native/bin/assembly_compiler_tests
build-salvage-native/bin/build_model_tests
bash docs/validation/salvage/SIM-01/stage-d1/build-standalone.sh
/tmp/salvage-function-tests
bash docs/validation/salvage/SIM-01/stage-d1/build-allocation-faults.sh
/tmp/salvage-function-faults
```

For WASM use recorded SDK/Node paths and fresh output directories:

```bash
python3 scripts/validate_assembly_compiler_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-01/stage-d1/run-wasm-faults.py --shared-validation <fresh-shared-directory> --output <fresh-fault-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_core -j 8
```

The manifests retain exact commands, binary/source/tool hashes, frozen-input
checks and exception policy. Aggregate targets register the new tests; this
checkpoint does not claim a full repository hook suite or a live GPU run.
The final immutable assembly/cache identity and complete compiler review remain.
The unapproved cove visuals and all product quality gates also remain open.
