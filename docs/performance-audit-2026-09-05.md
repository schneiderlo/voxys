# Performance audit — 2026-09-05

One measured inefficiency was fixed: distributed island-pair discovery copied entire physics checkpoints while reading only metadata. Borrowing the stored descriptors reduces the 256-island fixture's median query from **3.210 ms to 0.201 ms**, and allocations from **32,898 to 2**.

This is a distributed-coordinator improvement. The current WRECKWATER single-world server does not call this coordinator; no game-frame or live-server speedup is claimed. A GPU shader experiment showed no whole-frame benefit and was discarded.

## Scope and architecture

Read the full root `AGENTS.md` and `README.md`, then traced application, physics, rendering, terrain, networking, distributed authority, replay, client prediction, and their tests. Source implementation takes precedence over older planning documents. This is a source investigation with selected workload coverage, not a claim that every backend, scene, and configuration has been executed.

| Path | Responsibilities and connections |
|---|---|
| Native/WASM application | `src/app/application.cpp` drives fixed physics updates and rendering. Water uses spectral FFT processing. Cached heightfield raycasting, lighting, water, and live geometry feed presentation. |
| GPU physics | `src/physics/gpu/gpu_physics_backend.cpp` orchestrates commands, active compaction, CCD, forces/water, attachments, sparse-grid broad phase, narrow phase, colored constraint solving, static contacts, islands/sleeping, and asynchronous event/query readback. Resident body buffers feed indirect rendering directly. |
| CPU physics | Box3D/Jolt implementations serve alternative physics paths behind the facade. They are not the measured GPU authority path. |
| WRECKWATER authority | `src/server/wreckwater_server_main.cpp` connects authenticated TCP, generation fences, canonical input, atomic match mutations, exact GPU ticks, contiguous event/pose certification, damage/flooding/character authority, snapshots, and replay. Host tick submission and GPU retirement are distinct. |
| WRECKWATER client | Client runtime and application client connect certified snapshots, local fixed-step prediction, platform timelines, interpolation, camera, and presentation. This is wired into the application despite older documents describing future work. |
| Engine foundations | Generic lockstep networking, snapshot history, replay, interest management, and `WorldCoordinator` are reusable subsystems. The current small WRECKWATER match does not exercise all of them. |
| Terrain | Streaming decode, maximum-height mip construction, and shadow preparation support the active heightfield renderer. The separate SVO module is not its central hot path. |

Existing protections matter: GPU-resident state, pooled buffers, cached bind groups, bounded asynchronous readback, bounded TCP queues/read budgets, amortized receive-buffer compaction, FFT workgroup processing and twiddles, stationary-camera caching, batched Jolt locking, and streaming terrain decode already avoid several common pitfalls. Replacing these without workload evidence would be speculative.

The current match has a small bounded physics world (three primary bodies plus eight fragments, 64 pair capacity). Its logical snapshots include three entities and four characters. Large distributed-island results must not be extrapolated to that world.

## Baseline and reproducibility

Baseline commit: `db72d9c4e4a6e734cdf2da78b6eb5d80f5d5f080`. Hardware: Ryzen AI 9 HX 370, Radeon 890M / RADV STRIX1, Mesa 26.1.5. GCC 15.2, Bazel 8.5. GPU timestamp period: 10.019 ns. Default frequency scaling/boost; no CPU affinity or clock pinning.

Small durable records are in [benchmarks/performance-audit-2026-09-05](benchmarks/performance-audit-2026-09-05/). Full local logs, temporary source variants, executables, and raw profiles are in `/tmp/voxys-perf-audit`; those temporary files are not portable repository artifacts.

### Tests

The normal optimized command failed to compile because existing tests trigger warnings treated as errors:

```bash
nix-shell --run 'bazel test -c opt //tests:voxy_tests //tools:terrain_diffusion_import_test --test_output=errors'
```

`test_log.cpp` and `test_networking.cpp` trigger null-dereference warnings; `test_wreckwater_application_client.cpp` triggers missing-field-initializers. Only command-line warning demotions were used to execute the baseline; no unrelated source fixes were made:

```bash
nix-shell --run 'bazel test -c opt //tests:voxy_tests //tests:wreckwater_vessel_damage //tests:deterministic_adversity_transport //tests:wreckwater_authority_gpu //tests:wreckwater_client_probe //tools:terrain_diffusion_import_test --per_file_copt=tests/.*@-Wno-error=null-dereference,-Wno-error=missing-field-initializers --test_output=errors'
```

The main suite ran 1,302 tests: **1,295 passed, four failed, three skipped**; another four were disabled. All five additional targets passed.

Existing failures:

- `AuthoredCoveTest.ProfileHasReadableCoastalHierarchy`
- `AuthoredCoveTest.MaterialZonesAreNormalizedAndDistinct`
- `BlitShaderTest.ShorelineBlendAndWetResponseAreContinuous`
- `BlitShaderTest.CoastalFoamUsesDepthCrestAndSpatialDecay`

Skipped: window creation, dense-grid candidate-clamp benchmark, and LEGO horizon benchmark. This baseline is not green.

### Workloads

| Baseline workload | p50 ms | p95 ms | p99 ms | Throughput | Peak memory |
|---|---:|---:|---:|---:|---:|
| Coordinator: 256 islands × 128 checkpoint bodies | 3.210 | 4.200 | 7.058 | 292.7 queries/s | 13,240 KiB process RSS |
| GPU: 100,000 sparse active bodies, 300 frames | 14.181 | 15.734 | 16.423 | 69.780 frames/s | 141,088 KiB process RSS |
| Four-client proof, supplemental host-loop timing | 0.507 | 0.983 | 1.182 | 360 match ticks / 6.80 s including startup | 88,792 KiB maximum descendant RSS |

Coordinator numbers are medians of three alternating A/B run summaries, not pooled percentiles. Process launch created an RSS floor in the coordinator matrix; there is **no demonstrated peak-memory reduction**. GPU persistent/scratch buffer estimates were 24.125/185.676 MiB, separate from CPU RSS.

GPU command:

```bash
nix-shell --run 'bazel build -c opt //tests:gpu_ballistic_benchmark //tools/physics_benchmark'
nix-shell --run '/usr/bin/time -v env VOXY_GPU_TIMESTAMP_PERIOD_NS=10.019 VOXY_GPU_BALLISTIC_BODIES=100000 VOXY_GPU_BALLISTIC_WARMUP_FRAMES=20 VOXY_GPU_BALLISTIC_MEASURED_FRAMES=300 ./bazel-bin/tests/gpu_ballistic_benchmark'
```

Use `gpu-baseline-quiet.txt`: an earlier run overlapped compilation and was excluded. This fixture uses a 128×128 target, sparse mixed bodies, no dynamic contact pairs, and no terrain contacts. It measures retired GPU frames, not a displayed coastal match.

The native four-client proof passed with loss, delay, duplicates, reordering, and reconnects. Bazel's server build was blocked by a pinned Python download/DNS failure, so the existing CMake build was used:

```bash
cmake -S . -B /tmp/voxys-perf-audit/cmake -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_BENCHMARKS=ON
cmake --build /tmp/voxys-perf-audit/cmake --target wreckwater_server wreckwater_client_probe -j8
/usr/bin/time -v env BUILD_WORKSPACE_DIRECTORY=/home/modkin/workspace/schneiderlo/voxys WRECKWATER_PROOF_LOG_DIR=/tmp/voxys-perf-audit/2v2 WRECKWATER_PROOF_SERVER_TICKS=360 /tmp/voxys-perf-audit/run_2v2.sh
```

The temporary runner copies `scripts/run_wreckwater_2v2.sh`, skips its Bazel build, and points to the CMake binaries; proof checks are retained. The unmodified server printed p50 0.552 ms, p95 0.867 ms, max 2.491 ms over 367 host-loop samples; elapsed 6.92 s, peak descendant RSS 87,548 KiB.

For the missing p99, a temporary server copy extended only the final timing print to read p99/mean from its existing samples. The identical proof using `run_2v2_observed.sh` and `WRECKWATER_PROOF_LOG_DIR=/tmp/voxys-perf-audit/2v2-observed` produced the supplemental table row (366 samples, mean 0.566 ms, max 3.567 ms). Build commands are preserved locally in `observed-server-build-commands.json`. This diagnostic binary is not a delivered change. These host-loop times exclude asynchronous GPU waiting and must not be inverted into claimed match FPS. The match is configured for 60 Hz. RSS is not the sum of all processes. Scripted convergence is not the human-visible multiplayer acceptance gate.

## Profiles captured before selecting changes

Linux `perf record -e cpu-clock` was denied (`perf_event_paranoid=4`). CPU sampling therefore used gperftools at 1,000 Hz. Allocation profiling used heaptrack; I/O used `strace -f -c`. Callgrind supplied a separate instruction-count cross-check.

### Coordinator CPU

10,145 samples, 10.14 sampled CPU seconds:

| Self hotspot | Sample share |
|---|---:|
| AVX-512 memory move | 75.85% |
| `_int_malloc` | 3.21% |
| `WorldCoordinator::island` | 2.53% |
| Inlined lower-bound search | 2.34% |
| `_int_free_merge_chunk` | 1.63% |

`island()` accounts for 90.42% inclusive time; pair discovery accounts for 99.92%. The owning `optional<IslandDescriptor>` copies its checkpoint vector. The 256-island fixture performs 32,896 such copies per query, each 8,192 bytes: about **257 MiB copied to inspect metadata**. This is the concrete bottleneck, rather than an inferred cost from big-O alone.

Heaptrack recorded 1,023,190 allocations and 2.26 MB peak heap over setup, warmup, and 20 original queries. The borrowed variant recorded 3,424 allocations and 2.24 MB peak heap; setup/benchmark instrumentation differs slightly between those profiling binaries, so the precise comparison comes from the final per-query allocation counter. Callgrind attributed 56.17% of collected instructions to memcpy; instruction share is not time share.

Coordinator syscalls consumed 0.001110 s across the traced run, principally startup and final output. Pair discovery performs no file/network I/O.

### GPU pipeline and host CPU

GPU timestamp medians identify where GPU work went:

| GPU stage | Median ms | Share of summed stage medians |
|---|---:|---:|
| Culling/render | 4.365 | 35.29% |
| Broad-phase index sort/ranges | 3.105 | 25.10% |
| Dynamic solver solve | 1.683 | 13.61% |
| Islands/sleeping | 1.193 | 9.64% |
| Broad-phase pair count | 0.855 | 6.91% |

The denominator is 12.370 ms, the sum of stage medians; these are not percentages of total wall time or a pooled mean profile. Solver work remains visible even in this zero-dynamic-contact fixture.

Host CPU sampling collected 2,029 samples: unresolved symbols 29.57% self, ioctl 18.24%, memset 4.34%, wgpu bind-group usage merging 1.97%, and compute pipeline selection 1.48%. Missing driver/runtime symbols limit attribution.

GPU heaptrack recorded 875,013 allocations and 179.85 MB peak heap over the whole process, including shader/runtime startup. Prominent stacks involve Rust/wgpu vector growth. This does not establish steady-state C++ body allocation overhead. Traced syscall time was 95.93% futex and 2.50% ioctl; waiting for GPU work dominates, not disk transfer.

Exact profiling command forms (tool executable paths were pinned Nix store paths):

```bash
# CPU: libprofiler from gperftools 2.17.2; pprof 2026-03-02 build.
env LD_PRELOAD=/nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2/lib/libprofiler.so CPUPROFILE=/tmp/voxys-perf-audit/coordinator.cpu CPUPROFILE_FREQUENCY=1000 /tmp/voxys-perf-audit/coordinator-baseline 256 128 3000
/nix/store/6gdpgjrgrg592sgcbzklq2r89mbwk4yl-pprof-0-unstable-2026-03-02/bin/pprof -top -nodecount=15 /tmp/voxys-perf-audit/coordinator-baseline /tmp/voxys-perf-audit/coordinator.cpu
/nix/store/565m73i63yx2wz48248jlmjyb41qb647-heaptrack-1.5.0-unstable-2025-07-21/bin/heaptrack --record-only -o /tmp/voxys-perf-audit/coordinator-heap /tmp/voxys-perf-audit/coordinator-baseline 256 128 20
strace -f -c /tmp/voxys-perf-audit/coordinator-baseline 256 128 20
/nix/store/z3k1ih7g5njqnbhns0anx8mw6zqjs1z3-valgrind-3.26.0/bin/valgrind --tool=callgrind --collect-atstart=no '--toggle-collect=*crossWorkerPairs*' --callgrind-out-file=/tmp/voxys-perf-audit/coordinator.callgrind /tmp/voxys-perf-audit/coordinator-baseline 256 128 5
```

For GPU profiling, the same profiler wrappers used the GPU workload environment above: 1,000 measured frames for CPU sampling; 300 for allocation and syscall profiles, with 20 warmup frames. Instrumented runs are not used as latency baselines.

## Opportunity matrix and decisions

Scores are prioritization heuristics: impact 1–5, confidence 0–1, effort in relative units. Scope is part of impact. The initial ranking and later refinement are preserved in `opportunity-matrix.txt`.

| Candidate | Impact | Confidence | Effort | Impact × confidence / effort | Decision |
|---|---:|---:|---:|---:|---|
| Borrow coordinator descriptors instead of copying checkpoints | 5 | 0.99 | 1 | 4.95 | Implemented; measured subsystem win |
| Pre-filter eligible proxies once before pair enumeration | 5 | 0.99 | 2 | 2.48 | Rejected after early-exit counterexample |
| Consolidate repeated radix worker barriers | 2 | 0.80 | 1 | 1.60 | Experimental only; rejected after timing |

The pre-filter prototype preserved ordered outputs and improved sparse cases further. However, a dense 4,096-proxy query with capacity one went from 0.000060 ms to 0.091831 ms because it scanned everything before returning the first pair. The final borrowed-only approach preserves the original lazy early exit. Its 0.000040 ms measurement is too short for precise speed claims; the deterministic reduction from four allocations to one is the stronger evidence.

The temporary WGSL experiment consolidated eight per-worker barriers to one final barrier where each digit remained lane-owned. Whole-frame p50/p95/p99 changed from 14.181/15.734/16.423 ms to 14.207/15.942/16.593 ms, throughput 69.780 to 69.611 FPS. Sort median moved only 3.105 to 3.083 ms. There is no demonstrated gain; the shader was not retained and no bitwise-equivalence claim is made for it.

## Accepted change and equivalence proof

Production diff: only `src/server/world_coordinator.cpp`, inside `crossWorkerPairs`. The public owning `island()` API remains intact. No shader, solver, networking format, authority digest allowlist, or capacity changes were made.

**Proof sketch:** the local borrowed lookup uses the same sorted `islands_` collection, lower-bound comparator, and equality test as `island()`. A found pointer therefore refers to the descriptor whose value the old code copied; a null pointer corresponds to the old empty optional. The method does not mutate the collection or call out to code that can invalidate it. All fields read by every predicate have identical values. Outer/inner loop order, worker lookup, inclusive overlap, tick bounds including saturation, authority fences, pair construction, and first-K return are unchanged. Consequently every successful execution emits the same ordered pair vector for the same state and tick, while avoiding checkpoint copies.

This is functional equivalence under the existing non-concurrent access contract. Deliberate allocation failure is excluded: removing allocations necessarily changes which operations can throw `bad_alloc`. There is no floating-point reassociation or altered solver ordering.

### Oracle and invariants

`tests/test_distributed_server.cpp` retains an independent version of the original owning-lookup enumeration as an oracle. The added test covers capacities 1/7/65,536; shuffled insertion; four workers; inclusive face contact; pre-start/start/interior/end/expired ticks; saturated end ticks; online/offline transitions; and migration/shadow/authority-epoch changes. Repeated calls must return the identical ordered vector.

The benchmark independently constructs the expected ordered vector from its fixture geometry and worker assignment. Every warmup and measured query is compared, outside timing. Hashes are a readable supplement to full equality, not the sole oracle:

| Fixture | Pairs | Golden FNV-1a |
|---|---:|---|
| 32 islands, sparse | 61 | `ec9aed60da4b92b8` |
| 256 islands, sparse | 509 | `143a92d6ee86eb58` |
| 512 islands, sparse | 1,021 | `adf1727eec1275e7` |
| 4,096 islands, dense, capacity one | 1 | `7717980363c8e066` |

### Final measurements

Medians of three alternating runs; milliseconds and queries/second:

| Islands / checkpoint bodies | Version | p50 | p95 | p99 | Queries/s | Allocations/query |
|---|---|---:|---:|---:|---:|---:|
| 32 / 1 | Original | 0.013566 | 0.013675 | 0.017012 | 73,130 | 530 |
| 32 / 1 | Borrowed | 0.006091 | 0.006191 | 0.006452 | 163,645 | 2 |
| 256 / 128 | Original | 3.20966 | 4.20015 | 7.05837 | 292.7 | 32,898 |
| 256 / 128 | Borrowed | 0.201234 | 0.373554 | 0.439217 | 4,587.6 | 2 |
| 512 / 128 | Original | 13.7972 | 14.4295 | 16.7195 | 71.5 | 131,330 |
| 512 / 128 | Borrowed | 0.937383 | 1.09407 | 1.37315 | 1,023.9 | 2 |

Both revisions used the same standalone benchmark and compiler flags:

```bash
git show db72d9c4e4a6e734cdf2da78b6eb5d80f5d5f080:src/server/world_coordinator.cpp > /tmp/voxys-perf-audit/coordinator-original.cpp
g++ -std=c++20 -O3 -g -fno-omit-frame-pointer -Isrc tools/coordinator_benchmark/main.cpp /tmp/voxys-perf-audit/coordinator-original.cpp src/network/replication.cpp src/physics/deterministic/lockstep_world.cpp src/physics/deterministic/replay.cpp -o /tmp/voxys-perf-audit/coordinator-original-guard
g++ -std=c++20 -O3 -g -fno-omit-frame-pointer -Isrc tools/coordinator_benchmark/main.cpp src/server/world_coordinator.cpp src/network/replication.cpp src/physics/deterministic/lockstep_world.cpp src/physics/deterministic/replay.cpp -o /tmp/voxys-perf-audit/coordinator-borrowed-guard
```

Run each executable with `32 1 500 999999 999 sparse 65536`, `256 128 500 999999 999 sparse 65536`, `512 128 200 999999 999 sparse 65536`, and `4096 1 1000 999999 999 dense 1`. Alternate original/borrowed, borrowed/original, original/borrowed. No concurrent audit builds or profiles ran during the final matrix. Ten warmup queries precede measurement. Tail variability is visible in `results.json`; these are local measurements, not confidence intervals.

## Regression guardrails and final checks

The new manual Bazel benchmark validates full outputs, counts ordinary C++ allocations inside each query, and supports p95 limits. It returns 7 for a performance-limit failure. Allocation limits are deterministic for these fixtures; timing limits are deliberately loose and need calibration on other hardware. The benchmark is an explicit guard command, not silently enabled in every CI test run.

```bash
nix-shell --run 'bazel test -c opt //tests:distributed_server --test_output=errors && bazel build -c opt //tools/coordinator_benchmark'
nix-shell --run './bazel-bin/tools/coordinator_benchmark/coordinator_benchmark 256 128 500 2 0.75 && ./bazel-bin/tools/coordinator_benchmark/coordinator_benchmark 4096 1 1000 1 0.02 dense 1'
git diff --check
```

All seven distributed-server tests passed, including the new oracle test. Both benchmark guards passed. The baseline implementation fails the allocation guards. The unrelated full-suite baseline failures listed above were not repaired; the whole suite was not rerun after this isolated change.

## Remaining leads, requiring their own profiles

These are source observations, not additional measured performance recommendations or proposed diffs:

- `SnapshotHistory::applyDelta`: repeated removals and sorted inserts could become a sorted three-way merge. Preserve exact entity order, replacement semantics, and serialized bytes. First establish a generic lockstep workload; the current match uses a different snapshot path.
- `InterestGrid::query`: sorted entries permit an x-slab lower/upper-bound restriction before existing predicates. Preserve original traversal and capacity truncation. Profile actual interest queries first.
- Coordinator pair enumeration remains quadratic. A sweep or spatial index needs an explicit proof of canonical first-K ordering and early-exit behavior. The rejected pre-filter demonstrates why asymptotic reasoning alone is insufficient.
- GPU sort/range construction and culling are measured investigation targets, but the current sparse low-resolution fixture does not establish a safe replacement algorithm for production scenes. Capture dense contacts, terrain, queries, and displayed-match workloads before redesigning them.
- Whole-world query scans may matter under heavy query traffic; no such profile was captured here. Four-peer snapshot encoding is small at the current cadence, and per-peer acknowledgements prevent blindly sharing complete encoded packets.

No evidence justifies adding caches, striped locks, a new serialization format, or queue machinery to the measured paths. Changes to float evaluation order, broad-phase capacities/cell size, solver iterations, or substeps do not meet the requested exact-output constraint merely because their visual results look similar.
