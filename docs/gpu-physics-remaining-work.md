# GPU Physics: Remaining Work

Status snapshot: 2026-07-14.

This branch contains a broad implementation of the GPU-physics and distributed-world plan. It is an integration snapshot, not a claim that every exit gate is complete.

## Immediate blocker

The sector-relative world-coordinate conversion is partly integrated.

The native C++ test binary builds. The current GPU smoke test stops while creating `solver_prepare_constraints`: `physics_dynamic_solver.wgsl` reads group 0, binding 3, but that metadata binding is missing from the matching C++ pipeline layout.

Fix this binding without exceeding the WebGPU target of eight storage buffers per shader stage. Then compile and run every physics pipeline again.

## Engineering still required

- Finish sector-relative terrain and water sampling.
- Finish sector-relative CCD and bullet sweeps.
- Rebase rendering and GPU culling around the camera sector.
- Carry sectors through the CPU character mover.
- Verify queries that cross sector boundaries or span more than one sector.
- Store and restore sectors in replay checkpoints and divergence diagnostics.
- Add end-to-end tests for collision across a 256 m boundary.
- Add tests proving distant sectors with identical local coordinates do not pair.
- Add large-coordinate tests for sleeping, queries, CCD, rendering, and replay.

## Validation still required

- Run the complete native CMake test suite.
- Run all Bazel native tests.
- Build the Bazel WASM target.
- Re-run browser shader parity and real WASM startup.
- Re-run deterministic replay and cross-backend hash checks.
- Re-run the 100,000-body benchmark after sector integration is stable.

The last complete native suite, WASM startup check, and browser shader check passed before the latest sector refactor. They must not be treated as current evidence.

## Human gates

- Do not remove Jolt without explicit approval.
- Choose the product direction from playtest evidence.
