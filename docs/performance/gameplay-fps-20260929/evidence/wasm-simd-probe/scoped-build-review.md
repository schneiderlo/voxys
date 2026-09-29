# Scoped game SIMD candidate

Prepared patch: `scoped-build.patch`. Production files are unchanged.

Exact Bazel command-line flag:

```
--per_file_copt=src/render/mesh_path[.]cpp,src/game/adventure/frontier_presentation[.]cpp@-msimd128
```

The patch places that flag only in `build:wasm` and adds equivalent per-source
CMake COMPILE_OPTIONS inside `if(EMSCRIPTEN)` in `src/CMakeLists.txt`. The selected
translation units are exactly:

- `src/render/mesh_path.cpp`: float clip matrices, bounds testing, GPU draw record
  construction and renderer preparation.
- `src/game/adventure/frontier_presentation.cpp`: double matrix chains in
  `AdventureRuntime::renderFrontier`, especially the brick lambda and animated
  avatar rendering.

No simulation/physics translation unit, native compile option, shader, input
route, export declaration, GLM define, GLM alignment mode, fast-math option or
relaxed-SIMD option is changed. Jolt and box3d already compile with SIMD128 in
the existing Bazel build. The existing final module already requires SIMD128.

ABI and numerical evidence:

- The paired actual full renderer probes preserve all 4096 output cases by byte
  and SHA256, including every 128-byte GpuDrawInstance record, float/double
  products, mirrored models, model admission edge cases and paint outputs.
- The actual production renderer retains all static assertions on GPU record
  size, alignment and every shader-visible field offset. Probe builds also
  assert packed GLM sizes: mat4=64, dmat4=128, vec3=12, vec4=16.
- Local GLM configuration selects SIMD/aligned types through explicit GLM
  architecture/intrinsic defines. None of those defines changes here, and GLM
  has no __wasm_simd128__ layout branch in this checkout. Physics public types
  contain no corresponding SIMD macro branch.
- All256 paint-byte expected-bit tests pass in both flags. 126 independent
  constant-point clip expectations and 256 model validity expectations pass.
- The exact output proof covers the renderer helpers and representative strict
  math domains. Frontier presentation is a new compiled translation unit in the
  game candidate, so run the actual browser visual/state oracle on that candidate
  before accepting the scoped flag. Earlier non-SIMD game oracle results do not
  establish the new module's output by themselves.

Before retaining a final build, compare final WebAssembly imports/exports by
name and kind, source shader package hashes, actual visual/state oracle output,
and direct frame timing against the non-SIMD candidate. Changed module bytes and
compiler diagnostics are expected; shader inputs and public application ABI
should preserve their existing contracts.

The actual assembly emits GLM matrix operators as weak COMDAT functions. Their
definitions can occur in other unflagged translation units too. The full game
link may retain a scalar copy from another object, or retain a SIMD copy shared
by all callers of that template. The per-file flags do not guarantee that the
linked GLM operator comes from the targeted source. Inspect the final linked
float/double matrix functions to verify vector arithmetic survived. Strict
bitwise math evidence remains necessary because a retained shared template may
also serve callers outside the two compilation units. Local private
`boundsVisible` has no such cross-translation-unit duplicate.

# Component timing attribution

The Node harness executes all operation loops in C++ inside the WASM module.
`probe_run` is a direct WASM-to-WASM call. It does not cross JS once per matrix,
record or bounds test. `steady_clock::now()` is called twice per 1024-operation
batch, so any clock JS/host boundary cost is amortized across the batch. Process
startup, correctness output, file writes and exact hashes lie outside each timed
interval. Fresh-process tiering and host scheduling can still affect the tail;
the large observed p99 outliers should not be interpreted as steady-frame tails.

The correctness fixture deliberately places every helper's inputs in one wide
ProbeCase. Timed loops therefore walk unused matrices/draw fields between the
small fields needed by a particular operation. Outputs also use a wide combined
record. This differs from the live renderer's compact MeshDrawInstance vector
and shared view/projection matrices. It can hide a float arithmetic gain behind
cache traffic, and limits interpreting small record/paint differences.

Optional narrow follow-up, if macro frame timing is inconclusive:

- Keep the current exact correctness fixture and checks unchanged.
- Float/bounds batch: one shared viewProj plus a compact contiguous model/bounds
  array, producing a compact output array through the actual helpers.
- Double batch: compact left/right dmat4 arrays, including the actual translate /
  yaw rotate / scale chain and f64-to-f32 output conversion used by renderFrontier.
- Record batch: contiguous actual MeshDrawInstance inputs and 128-byte output
  records, retaining the actual private constructor.
- Warm all workloads long enough for the default engine's tiering to settle
  before measurement. Keep clock overhead separate, no per-operation JS wrapper,
  strict floating point, balanced run order and exact output checks.

No narrow follow-up build, test or timing was started during the root's dense
query and upcoming foot-screen windows.
