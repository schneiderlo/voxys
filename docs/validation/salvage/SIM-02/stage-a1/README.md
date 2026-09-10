# SIM-02 stage-a1 — full inertia and principal body frames

Implemented 2026-09-08 after G00 commit `7f28fab`, branch
`codex/salvage-implementation`. `summary.json` records the tested uncommitted
source, build/runner, evidence and artifact hashes. **This is preparatory
numerical work, not SIM-02 acceptance or live backend integration.** SIM-01's
independent review remains pending. Existing review workers authoritatively
reported account usage limits; no replacement worker or credit reset was used.
The plan allows dependent preparation while preserving every review/gate.

## Delivered behavior

`RigidMassFrame` accepts full root-frame dry inertia and finds a proper
principal-axis body frame centered at COM. Its diagonal moments preserve the
full tensor, including off-diagonal coupling. It provides root/body point,
vector and pose mappings, COM/origin velocity conversion, angular momentum/
response and analytical impulse-at-point calculations. This permits later
backend integration to retain efficient diagonal inertia multiplies without
mistaking the authored axes/origin for the physical axes/COM.

Preparation uses fixed arrays, at most 48 cyclic Jacobi rotations, scale
normalization and explicit validity/residual checks. See the precise tolerances,
axis conventions and limitations in [design.md](../design.md). It returns no
partial value or live handle on failure and allocates nothing. Read-only
mathematical helpers do not admit commands or certify completed simulation.

## Actual validation

| Check | Result |
|---|---|
| Optimized Bazel | Seven focused targets pass; new 8-case numerical target executed, six prior targets cached/pass |
| Native CMake | All 86 shared cases pass; native core library builds |
| Strict warnings as errors plus undefined-behavior/float-cast-overflow checks | All 86 cases pass, no skipped/disabled cases |
| Actual WASM with JS exceptions and Asyncify | All 86 cases pass; fixed 64 MiB heap and 1 MiB stack; no skipped/disabled cases |
| Shipping WASM core | Numerical preparation source compiles; core library builds |
| Actual replacement-new guard, each runtime | 5,000 preparations, 1,000 rejected inputs, 10,000 transform round trips and 5,000 impulse checks; zero allocations |

The allocation-guard fixtures required at most eight Jacobi rotations. Both
runtimes reported maximum normalized reconstruction error
`2.2204460492503131e-16`. This result belongs to the executed fixtures; the hard
algorithm bound remains 48 rotations with failure if its acceptance checks fail.

Eight numerical tests cover:

- Independent exact rational asymmetric tensor/inverse/impulse values. The
  reference rotation derives from quaternion `(1,2,3,4)/sqrt(30)`, principal
  moments `(2,3,4)`, mass 5 kg and COM `(1/4,-1/2,3/4)` m. The off-center impulse
  produces angular velocity change `(-1357/3600,5863/1800,56/45)`. Applying it
  exactly at COM produces zero angular change. `golden.py` uses Fraction
  arithmetic without production imports; `golden.json` preserves the results.
- All 24 proper rotations, full tensor reconstruction, energy, angular response
  and root/body point round trips.
- Isotropic, repeated, permuted and slender principal moments, proper handedness
  and repeat-call ordering. Equivalent degenerate bases need not be bitwise
  equal across platforms; this is not a deterministic solver claim.
- Scale factors from 1e-200 to 1e200 without intermediate normalization overflow.
  These are double-precision numerical fixtures, not supported GPU world scales.
- Nonfinite/asymmetric/nonphysical/ill-conditioned input rejection and preserved
  existing output, with typed errors and reset error on success.
- Independent COM/origin pose and velocity offsets; analytical body advance
  reconstructs the authored origin's orbit about COM. This is a CPU reference
  calculation, not the shipping GPU integrator.
- 2,000 deterministic dense physical fixtures using independently constructed
  Rodrigues rotations and positive mass second moments; bounded iteration,
  full momentum and inverse-response identities.
- An actual compiled beam with offset ballast. Its nonzero off-diagonal tensor,
  root-local socket point and momentum mapping survive conversion.

The prior 78 assembly/build cases also ran on the exact shared source set.
Both core builds include the new numerical source. No production WGSL or GPU
ABI changed, and no new capability was advertised.

## Retained first failures

First Bazel, strict and WASM attempts rejected an unnecessary 72-byte matrix
copy in a test loop under warnings-as-errors. The loop now binds a const
reference. First CMake configuration caught the new source accidentally added
to the separate pinned WRECKWATER authority source list as well as normal core.
The unused authority addition was removed in both build systems; its manifest
and functionality remain unchanged. All second attempts pass. Production
numerical code and test count did not change to obtain a pass. The logs and
failed WASM manifest remain preserved. Both allocation guards passed first try.

## Reproduce

From the repository root inside `nix-shell`:

```bash
bazel test -c opt //tests:rigid_mass_frame //tests:compiled_assembly //tests:assembly_functions //tests:assembly_buoyancy //tests:assembly_collision //tests:assembly_compiler //tests:build_model --test_output=errors
cmake --build build-salvage-native --target rigid_mass_frame_tests compiled_assembly_tests assembly_functions_tests assembly_buoyancy_tests assembly_collision_tests assembly_compiler_tests build_model_tests voxy_core -j 8
build-salvage-native/bin/rigid_mass_frame_tests
build-salvage-native/bin/compiled_assembly_tests
build-salvage-native/bin/assembly_functions_tests
build-salvage-native/bin/assembly_buoyancy_tests
build-salvage-native/bin/assembly_collision_tests
build-salvage-native/bin/assembly_compiler_tests
build-salvage-native/bin/build_model_tests
bash docs/validation/salvage/SIM-02/stage-a1/build-standalone.sh
/tmp/salvage-mass-frame-tests
bash docs/validation/salvage/SIM-02/stage-a1/build-allocation-guard.sh
/tmp/salvage-mass-frame-guard
```

WASM uses recorded SDK/Node paths and fresh output directories:

```bash
python3 scripts/validate_rigid_body_inputs_wasm.py --sdk <recorded-sdk> --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-02/stage-a1/run-wasm-guard.py --shared-validation <fresh-shared-directory> --output <fresh-guard-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_core -j 8
```

Manifests retain exact commands, source/tool hashes, artifact hashes and frozen
inputs. The original SIM-01 runner remains available for its separate 78-case
scope. Aggregate test targets register the numerical tests; this checkpoint
makes no claim of a complete repository hook-suite run or live GPU test.

## Remaining work

Explicit authored shape identity/resources independent of material flags;
f32/quaternion packing and representability/error checks; actual backend
admission/upload; complete body/root COM-velocity and force-point adaptation;
explicit unsupported CPU handling; actual GPU pose/force conformance and legacy
regressions; every compound consumer through SIM-03; prerequisite/implementation
review and full gate validation. No live boat physics, scene-wide memory budget,
performance, visual quality or gate commit is certified by this checkpoint.
