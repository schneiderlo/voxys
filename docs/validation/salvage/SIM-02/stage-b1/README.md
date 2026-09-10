# SIM-02 stage-b1 — authored shape preparation and lifetime ledger

Implemented by root on 2026-09-08 in `codex/salvage-implementation`, after G00
`7f28fab`. This is a completed preparatory component, **not SIM-02 acceptance,
live backend support, a visual improvement or a passed gate**. SIM-01 and later
independent reviews remain pending. The existing workers are unavailable; no
replacement worker or usage reset was requested. Plan decision D18 permits
dependent preparation while preserving all prerequisite/review/gate acceptance.

## What changed

`AuthoredShape` owns one root's exact exterior collision cells/patches/BVH and
its full-mass principal frame in explicitly sized storage rows. It validates
f32 mass/reciprocals, COM, quaternion and tensor reconstruction before producing
a value. Material flags do not select this shape kind. Every exterior patch
retains the compiler's source label; fully enclosed cells have no contact faces.

The exact union implementation and integer point/box types were extracted to
`src/geometry/`, with construction compatibility aliases. This gives physics
the same validated geometry without importing game authority or maintaining a
second clipping algorithm. The compiler's independent canonical hash golden
still passes; no physical-input grammar, clipping rules or compiler version
changed. Current runner/build paths use `geometry/box_union.cpp`. Earlier
evidence remains historical and must not be overwritten to disguise path changes.

`AuthoredShapePool` owns prepared resources under `(index, generation, pool)`
handles. All fields must be nonzero, and the caller assigns a unique pool
incarnation. Retiring resources remain charged until both registered GPU use
and retained readers finish. Stale handles cannot access a replacement; slot
generations and submission serials never wrap. Admission refuses partial batches
and preserves failed insertion inputs. Pool creation/preparation allocate; the
subsequent lifetime operations do not.

See [the exact format, numerical tolerances, memory caps and lifetime protocol](../design.md).
The pool tests supply explicit **model completion serials**. No new GPU buffer,
descriptor upload, actual fence observation, physics command, contact consumer
or scene activation is implemented here. Future bindings must also retain the
matching compiled assembly/revision/provenance mapping until old events retire.
Opaque source labels alone do not retain a durable part identity after that
external mapping is destroyed.

## Validation

| Check | Actual result |
|---|---|
| Optimized Bazel | Ten focused targets pass, containing 130 cases; final authored target rerun and nine unchanged targets cached |
| Native CMake | All 130 cases pass |
| Strict warnings as errors plus undefined-behavior/float-cast-overflow | All 130 cases pass, no skipped/disabled cases |
| CPU WASM, shipping JS exceptions and Asyncify | All 130 cases pass; fixed 64 MiB heap and 1 MiB stack; no skips/disabled cases; source freeze verified |
| Actual replacement-new probes, native and WASM | Three shape-preparation failure cuts and one pool-creation cut preserve prior state |
| Allocation-free lifetime probes, each runtime | 1,000 insert/retain/submit/retire/complete/release/reuse cycles, 1,000 preserved capacity refusals and 14,000 provenance reads; zero allocation attempts |
| Complete applications | Native and shipping WASM applications build and link via CMake |
| WRECKWATER compatibility manifest | Real Bazel and CMake generated headers match byte-for-byte; eight generator tests and five manifest tests pass |

The 130 shared cases are 15 shape/pool, eight numerical mass, 78 existing
assembly/build and 29 catalog/grid cases. New coverage includes partial mating
faces, a fully enclosed cell, exact packed geometry/BVH/provenance, the prior
independent rational off-center impulse, all proper rotations and 400 continuous
rotation fixtures, conservative bounds at ±256 m, unsupported numeric ranges,
quantization-amplified conditioning, actual compiled offset ballast, and every
currently implemented starter definition in all permitted rotations.

The current canonical draft contains **12 definitions**, so that last check
prepares 288 single-part builds. The plan's 14-definition launch catalog still
requires its later cutter/crane work; this check neither supplies those parts
nor lowers that product requirement. Module fixtures use each definition's
validated default settings.

Pool checks cover each world budget while resources retire, reader versus GPU
completion order, foreign pool identity, move ownership, stale slot reuse,
permanent generation exhaustion, invalid/oversized atomic submissions and
submission-serial exhaustion. These are CPU state-machine checks, not live
render/simulation race or device-loss evidence.

The two-box allocation fixture owns 1,184 bytes natively / 1,152 bytes in WASM;
its one-slot table occupies 368 / 336 bytes respectively. These measured CPU
layout figures are not total allocator overhead or GPU memory. Storage records
have identical 48-byte mass/cell/face and 32-byte node layouts on both runtimes.

The extracted handle header is a new quoted include of `physics_types.hpp`.
It was therefore added to both build systems' authority content input lists and
the checked allowlist. The separate authority translation-unit closure was not
expanded with the unused authored-shape implementation. The actual generated
header equality and generator checks prevent silently dropping this dependency
from content compatibility.

## Retained failures and corrections

- The initial extraction build, before stage logs were created, failed because
  the query still called construction's `isValid(GridPosition)`. The tool result
  reported `src/geometry/box_union.cpp:127: isValid was not declared`. The
  replacement common helper preserves the exact prior INT32_MIN exclusion;
  all 29 grid/catalog and existing geometry cases pass. This paragraph records
  the observed tool result; it is not a reconstructed complete build log.
- `bazel-attempt01.log` rejects implicit double promotions under GCC. Explicit
  conversions fix the warnings; attempt02 passes the initial 129 cases.
- `wasm-attempt01/` catches Clang's additional declaration-promotion warnings.
  They were also made explicit; tolerances and precision were not weakened.
- Bazel attempt03 and WASM attempt02 reject the newly added catalog fixture at
  the engine because it omitted definition-specific module settings. The test
  now uses `defaultModuleSettings`. Its unexecuted final count also mistakenly
  assumed the planned 14 definitions; repository inspection confirmed the
  current 12, as the existing catalog test already specifies. The test now
  explicitly covers those 288 valid builds. No production validation, catalog
  content, launch scope or numerical limit changed to make it pass.
- Final passing evidence: `bazel-attempt04.log`, `cmake-attempt02.log`,
  `ubsan-attempt02.{log,xml}`, `wasm-attempt03/manifest.json`,
  `wasm-guard-attempt01/summary.json`, `native-app-attempt01.log`,
  `wasm-app-attempt01.log`, and `authority-manifest-attempt01.log`.

The broad WASM application build retains pre-existing compiler warnings,
including the GLM WorldPosition defaulted comparison. The strict shared source
set passes with warnings treated as errors. A successful link is not a new
browser playthrough or legacy GPU physics acceptance.

## Reproduction and next integration work

Use the repository Nix shell and preserve fresh attempt paths. The source,
toolchain, commands and produced binaries for CPU WASM are recorded by
`wasm-attempt03/manifest.json`; the executed runner is copied beside it.

```sh
bazel test -c opt //tests:authored_shape //tests:rigid_mass_frame \
  //tests:compiled_assembly //tests:assembly_functions //tests:assembly_buoyancy \
  //tests:assembly_collision //tests:assembly_compiler //tests:build_model \
  //tests:construction_types //tests:part_catalog --test_output=errors

cmake --build build-salvage-native --target authored_shape_tests voxy_native -j 8
ctest --test-dir build-salvage-native --output-on-failure \
  -R '^(authored_shape|rigid_mass_frame|compiled_assembly|assembly_functions|assembly_buoyancy|assembly_collision|assembly_compiler|build_model|construction_types|part_catalog)\.'

bash docs/validation/salvage/SIM-02/stage-b1/build-standalone.sh
/tmp/salvage-authored-shape-tests
bash docs/validation/salvage/SIM-02/stage-b1/build-allocation-guard.sh
/tmp/salvage-shape-guard

python3 scripts/validate_rigid_body_inputs_wasm.py --sdk <recorded-sdk> \
  --node <recorded-node> --output <fresh-shared-directory> --exception-mode js --asyncify
python3 docs/validation/salvage/SIM-02/stage-b1/run-wasm-guard.py \
  --shared-validation <fresh-shared-directory> --output <fresh-guard-directory>
EM_CONFIG=/tmp/voxys-emsdk/.emscripten cmake --build build-lego-wasm --target voxy_wasm -j 8

bazel build -c opt //:wreckwater_build_content_header
bazel test -c opt //tools:generate_wreckwater_build_content_test //tests:wreckwater_content_manifest
```

Next: bind these prepared rows to real GPU allocations and generational
descriptors; connect real submission/completion and delayed readers; add typed
backend admission with explicit unsupported CPU handling; map COM/principal
poses, root render poses, velocity fields and force/attachment points. Then
carry shape identity, exterior patches, BVH and provenance through every
broad/narrow/terrain/CCD/query/event/render consumer and validate actual GPU
behavior on native and WASM. Preserve the existing primitive/studded paths.
SIM-06 must make whole-assembly reservations and publication atomic across
shapes, bodies, render/provenance mappings and authority revisions.

Independent review and SIM-01 prerequisite acceptance remain required. No
SIM-02, SIM-03, G03, LOOK-01 or other gate is checked or committed by this stage.
