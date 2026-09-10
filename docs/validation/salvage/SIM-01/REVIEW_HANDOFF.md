# SIM-01 complete analytical compiler — independent review request

Prepared by implementing root on 2026-09-08. **This file is a request, not a
performed independent review.** SIM-01 remains unchecked until review findings
are resolved and its acceptance is verified. No G03 or other gate commit is
claimed. Read root AGENTS.md, README.md and GAME_IMPLEMENTATION_TODO.md first.

## Current worktree and evidence

Branch `codex/salvage-implementation`, base G00 `7f28fab`.
The implementation remains uncommitted alongside other authorized asset and
session work. Preserve unrelated files. Current compiler acceptance evidence is
`stage-e1/README.md` and `stage-e1/summary.json`; source/tool/runtime/binary hashes
there identify what was actually tested. Earlier stage folders remain historical.
Do not compare their old shared build/runner hashes to current files as if they
were new failures, or silently replace historical evidence.

**Current-source follow-up, SIM-02 stage-b1:** the exact union implementation
and its integer boxes/points now live in `src/geometry/{box_union.*,
orthogonal_geometry.hpp,grid_types.hpp}`. The old construction headers retain
compatibility aliases; `construction/orthogonal_union.cpp` was moved. Physics
can therefore consume the same validated geometry without depending on game
authority or duplicating clipping logic. The query still rejects INT32_MIN.
Canonical values, the clipping algorithm and compiler input identity are
unchanged. All 78 compiler/build cases have been rerun as part of the current
130-case set in `../SIM-02/stage-b1/`; that source-hashed record is the latest
validation for these shared files. The stage-e1 evidence remains historical.
This follow-up does not constitute an independent review of either task.

The implemented CPU chain is:

1. `AssemblyMassPlan`: current-catalog BuildModel validation, enabled-weld
   components, least-ID root anchor, exact frames, compensated dry COM/full
   inertia and parallel-axis aggregation.
2. `AssemblyCollisionPlan` and `BoxUnion`: bounded exact disjoint box union,
   internal-face clipping, exterior contact provenance, flat stackless BVH.
3. `AssemblyBuoyancyPlan` and `BoxCoverage`: bounded disjoint cells retaining
   **all** solid/sealed source contributors, globally shared work/output caps.
4. `AssemblyFunctionPlan`: typed module parameters/settings/health/strength,
   exact functional/socket frames and canonical connection bindings.
5. `CompiledAssembly`: complete owned CPU output with actual physical
   input/topology/profile SHA-256 identity. No live backend/authority operation.

Sources are in `src/game/construction/`: `assembly_compiler.*`,
`orthogonal_union.hpp`, `orthogonal_geometry.hpp`, `assembly_collision.*`,
`orthogonal_coverage.*`, `assembly_buoyancy.*`, `assembly_functions.*`,
`compiled_assembly.*`. Dependencies include `construction_types.*`,
`part_catalog.*`, `build_model.*` and `src/core/sha256.hpp`.
Tests are `test_assembly_compiler.cpp`, `test_assembly_collision.cpp`,
`test_assembly_buoyancy.cpp`, `test_assembly_functions.cpp`,
`test_compiled_assembly.cpp` plus the 23-case canonical build suite. Shared
Bazel/CMake registration and `scripts/validate_assembly_compiler_wasm.py` are part
of the review; the runner requires shipping JS exceptions with Asyncify.

## Review priorities

- Independently assess the rational full-inertia fixture and dense rotation
  oracles, not merely test coverage counts. Root axes/COM frame must not be
  confused. Large common translations must subtract before floating conversion.
- Only enabled welds group roots. Disabled modules, condition, loans, ropes and
  distant authored latches must not erase/add mass or imply capture. Any
  same-root rope/latch activation remains a later mechanics decision.
- Validate exact collision subtraction/face clipping for containment, partial
  faces, cavities and every rotation. Raw union cells are **not** safe unmasked
  compound contacts; SIM-02 must honor exterior patches and reduce edge contacts.
- Validate overlap-aware buoyancy provenance, including active subset behavior:
  losing a sealed contributor must not lose an overlapping solid core. This
  preparation does not choose a partial-flooding/submersion approximation.
- Check global versus per-root source/cell/face/reference/work bounds, scratch
  vector growth, failures before publication and 256-part input assumptions.
  Generated element caps are not measured RSS or whole-scene admission. Dense
  1,024-weld geometry can exceed canonical mating work even within record caps.
- Verify all module kinds and exact composed frames. Thrust is local −Z of its
  force frame; socket +Y is outward and +X is key. Winch default payout must not
  overwrite a connected rope's rest length. Disabled links reserve slots.
- Audit ownership and borrowed-span lifetime, binary lookup ordering and invalid
  lookup behavior. Output arrays are const; no pointer into input/catalog remains.
- Independently audit `cache-wire-v1.md`, the explicit 670-byte Python fixture
  and production hashing. All used physical content and profile fields must be
  bound, with fixed widths/tags and +0/−0 normalization. Same ContentKey with
  changed metadata must invalidate. Economics, permissions and render state
  exclusions must never be treated as authorization or a complete asset cache.
- Inspect actual replacement-new probes, exception policy, every retained first
  failure and source freeze checks. Verify that publishing failed candidates
  never mutates input/existing output and reads allocate nothing. Tests/oracles
  so far were authored by the implementer, not an independent reviewer.

## Required handback

Report actionable findings with file/line, concrete failure scenario, priority
and a reproducing check. Distinguish observed failures from unexecuted risks.
Run only tests relevant to findings; preserve new logs in a fresh directory.
Do not mark tasks/gates, commit, weaken limits, hide a failure or claim human/
GPU/product acceptance. Root must resolve findings, rerun affected checks and
then decide task acceptance. Every successful gate still requires the repository
hook suites and its own commit; SIM-01 alone is not a gate.

## Current limits of the claim

This is a complete analytical CPU preparation implementation under review.
No live machine launches, forces, flood state, backend body/COM adaptation,
render motion from compiled roots, scene-wide allocation reservation, saved
solver state or inventory mutation is supplied by this compiler. Those remain
SIM-02 onward and the later mechanics/session/build/save gates. Placeholder cove
art is still unapproved; visual gate LOOK-01/G02 remains open.
