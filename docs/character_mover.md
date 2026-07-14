# CPU capsule character mover

The WebGPU backend keeps the player character on the CPU. Camera movement does
not wait for a GPU submission or body-state readback.

## Collision contract

`CpuCapsuleMoverWorld` uses the same terrain contract as rendering and rigid
body collision:

- 16-bit height samples use the shared `worldHeight` conversion;
- sample `(0, 0)` uses the shared centered origin;
- every cell uses the canonical top-left to bottom-right diagonal;
- triangle normals come from the exact selected triangle plane.

The vertical capsule samples a fixed footprint on its lower hemisphere. Motion
uses bounded casts, fixed bisection, at most four feature-sorted contact planes,
overlap correction, velocity clipping, walkable-slope tests, and bounded
step-up/step-down snapping. No loop iterates until convergence.

## Nearby dynamic bodies

Phase 8 selects `NearbyDynamicBodyPolicy::TerrainOnly`.

This is intentional. The character collides synchronously with canonical CPU
terrain. It does not synchronously read the GPU world. Later policies are named
but inactive:

- `AsyncQueryMirror`: consume completed nearby overlap/query batches;
- `CpuAuthoritativeSet`: mirror a small gameplay-critical body set on CPU.

Character impulses for GPU bodies, if enabled later, must be next-tick ordered
commands. They must not make the current character tick wait for GPU readback.

## Verification

The tests cover both terrain triangles, landing, jumping, a walkable ramp,
step rules, steep-plane rejection, exact replay between two mover instances,
and integration through the WebGPU backend facade.

```bash
nix-shell --run 'bazel test //tests:cpu_capsule_mover //tests:gpu_physics \
  --test_output=errors --test_timeout=300'
```
