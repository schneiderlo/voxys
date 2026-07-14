# Product vertical slices

Date: 2026-07-13

These are deterministic engine-validation slices. They prove the required
systems can compose and replay. They do **not** prove that a product is fun.

## Demolition League

Implemented loop:

1. Submit ordered impact commands.
2. Damage structural edges.
3. Split the building into deterministic connected components.
4. Promote significant fragments to simulated bodies.
5. Integrate falling fragments and score new detachments.
6. Replay the command stream to the same final hash.

Telemetry covers accepted/rejected impacts, fractures, detached bodies, and
the state hash.

## Deadweight

Implemented two-client co-op transport loop:

1. Validate client identity, tick window, sequence, and bounded input force.
2. Canonically merge redundant client commands.
3. Move a shared cargo body through hazards.
4. Keep a bounded rollback history.
5. Detect delivery, loss, and timeout.
6. Replay both clients' inputs to the same final hash.

Telemetry covers duplicates, rejected commands, missing-client ticks, hazard
ticks, and history eviction.

## Wreckwater

Implemented assembly and buoyancy slice:

1. Damage an authored hull connectivity graph.
2. Split significant fragments from tiny debris.
3. Open deterministic compartment breaches.
4. Advance flooding at a fixed integer rate.
5. Evaluate fixed-point buoyancy and drag.
6. Replay every fracture, flooding, and motion state to the same hash.

Telemetry covers fracture events, significant/tiny fragments, flooded-node
ticks, and the state hash.

## Playtest decision gate

`PlaytestLedger` only accepts completed, human-verified observations. A product
choice requires comparative evidence for all three candidates. It considers:

- fun rating;
- blockers and crashes;
- support time;
- participant count and session duration.

Synthetic observations are rejected. Insufficient or tied evidence produces
no product choice. No human playtest evidence is stored in this repository, so
the commercial product decision remains open.

## Reproduce

```bash
nix-shell --run 'bazel test //tests:vertical_slices_ci \
  --test_output=errors --runs_per_test=2'
nix-shell --run './scripts/run_platform_parity.sh'
```

The same sources and tests are present in Bazel and CMake. The platform gate
builds both native and WASM targets, compares their slice hashes, and validates
all browser WGSL modules.
