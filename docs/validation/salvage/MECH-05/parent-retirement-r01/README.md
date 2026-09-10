# Atomic parent retirement preparation

The native parent-retirement primitive is verified under D32. No full task or gate acceptance.

`PhysicsMutationBatch::bodyDestroys` prepares an entire set of already encoded
body lifetimes. A bad/stale/duplicate parent, pending spawn, full command queue,
conflicting command or new attachment to a retiring body rejects the whole
batch. Preparation leaves live ownership and pending retirements unchanged.
Discard consumes no body slots or generations. Commit uses preallocated storage
and publishes all destruction commands at the same next tick, together with any
explicit attachment transfers. Existing generation-safe GPU retirement remains
responsible for slot reuse. Pending children are still reserved by the caller;
this API does not allocate them or claim a complete fracture transaction.

Three added actual GPU tests cover rejection/discard, combined two-parent
retirement and rope transfer, stale generation/reuse, and command capacity.
The actual authored skiff-to-two-fragments fixture now uses this batch for its
parent retirement. All eight selected native transaction cases pass, including
the three new retirement cases, and the actual authored split case passes.
The first combined run failed only because `voxy_tests` omitted the cove fixture
runfiles. Its eight transaction successes and missing-file failure are retained.
Adding `//data:salvage_material_metric` fixes the combined target; only the failed
actual split case was rerun. No physics assertion was removed.

Bazel builds the native application and both test targets. CMake builds the
native application/cove tests and the configured WASM application. The final
implementation uses a preallocated touched-index table so duplicate/membership
checks are linear in the request size, not quadratic. Eight bytes per reserved
retirement handle and one membership byte per body supplement the preallocated
pending-retirement records. No runtime allocation is required by prepare/commit.
This is native execution plus WASM compilation; the current browser game does
not yet consume this API and no browser fragment-execution pass is claimed.

Reproduce the focused native checks (sequentially, from the repo root):

```sh
nix-shell --run 'bazel test -c opt --jobs=8 //tests:voxy_tests --test_filter="GpuPhysicsTest.Prepared*:GpuPhysicsTest.InvalidPreparedTransfer*:GpuPhysicsTest.FullCapacityTransfer*:GpuPhysicsTest.AccumulatorAnd*:CoveMovement.ActualCutReplacesOneMovingSkiffWithTwoCompleteFloatingGpuBodies"'
```

`checks/transactions-r01.xml` contains the eight passing transaction cases and
the retained missing-runfile failure. `checks/authored-r02.xml` contains the
corrected actual split pass. `manifest.json` records source/build/evidence hashes.

The API currently retains the existing explicit-fixed-tick preparation policy.
The playable cove uses an accumulator plus drained pauses. Before replacing its
single-parent Launch path, add a checked joined-pause admission path or unify
its authority scheduler; do not simply remove the accumulator refusal. Full
root ownership, all child/shape/water/pose/event reservations, joined execution,
logical publication, per-root player/render/rope bindings, SVCE v4 live capture
and restore, and distant-fragment rescue remain required. Cutting stays disabled.

Changed source: `tests/BUILD`, `src/physics/physics_types.hpp`,
`src/physics/gpu/gpu_physics_backend.cpp`, `tests/test_gpu_physics.cpp`, and
`tests/test_cove_player.cpp`. Run GPU checks sequentially after the wave-clock
browser journey. Scratch logs are in `build-cove-water-XHSMQGSh` with the
`parent-retirement-` prefix. No screenshots.
