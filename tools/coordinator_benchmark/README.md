# Coordinator pair-discovery benchmark

This exercises the production `WorldCoordinator::crossWorkerPairs`. It is a
Linux CPU subsystem benchmark, not a game-frame benchmark.

Build and run inside the project Nix shell:

```bash
bazel run -c opt //tools/coordinator_benchmark -- 256 128 500 2 0.75
bazel run -c opt //tools/coordinator_benchmark -- 4096 1 1000 1 0.02 dense 1
```

Positional arguments:

```text
islands bodies_per_checkpoint measured_queries maximum_allocations maximum_p95_ms layout pair_capacity
256     128                   200              unlimited           unlimited      sparse 65536
```

All arguments are optional; supply a prefix of the list. `dense` makes all proxy boxes overlap;
the default sparse arrangement touches the next two boxes. Four workers are
assigned cyclically. Ten queries warm the path before measurement.

Each query must match an independently constructed, ordered golden vector.
The benchmark also prints its FNV-1a hash, p50/p95/p99, queries/second, process
peak RSS in KiB, and maximum C++ allocation count inside one query. Hashing,
equivalence comparison, and printing are outside the timed query. Ordinary
`new`/`new[]` allocations are counted; this is not a general malloc profiler.
Current query storage does not use over-aligned allocations.

Exit code 7 means an allocation or p95 threshold failed. The allocation guards
are deterministic for these fixtures: two allocations for sparse result growth,
one for the capacity-one result. The old checkpoint-copying implementation
fails both guards. Timing thresholds are deliberately loose reference-machine
limits; recalibrate them on another machine. Sub-microsecond dense timings
are clock-resolution diagnostics, not precise throughput claims.

For an isolated build without the renderer dependencies:

```bash
g++ -std=c++20 -O3 -g -fno-omit-frame-pointer -Isrc \
  tools/coordinator_benchmark/main.cpp src/server/world_coordinator.cpp \
  src/network/replication.cpp src/physics/deterministic/lockstep_world.cpp \
  src/physics/deterministic/replay.cpp -o /tmp/coordinator-benchmark
/tmp/coordinator-benchmark 256 128 500 2 0.75
```

The recorded audit used this standalone compiler command for both revisions.
See [the audit](../../docs/performance-audit-2026-09-05.md) for baseline source,
profiles, limitations, and the equivalence proof.
