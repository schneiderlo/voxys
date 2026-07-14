# Physics baseline and reference-backend report

Date: 2026-07-13

Reviewed Voxys baseline: `2dbbb6d209e62e0e704c36004bc344493d87730d`

Pinned Box3D: `d421e45c828f6f853a145f726f0b9425d31146eb`

This is the Phase 0/1 reproducibility record for the GPU physics plan. It is
not a general hardware ranking. CPU frequency scaling was enabled, so the
numbers are directional and must be re-recorded for release decisions.

## Environment

- Linux `7.0.0-27-generic`, x86-64
- AMD Ryzen AI 9 HX 370, 12 cores / 24 threads
- GCC 15.2.0
- Bazel 8.5.0 through Bazelisk 1.28.1
- CMake 4.1.2
- Bazel fastbuild, portable SSE2 Box3D/Jolt configuration
- Fixed 60 Hz public tick; Box3D uses four internal substeps

## Reproduction

Run from the repository root inside the project Nix shell:

```bash
nix-shell --run 'bazel run //tools/physics_benchmark -- \
  --backend jolt --scene snapshot --bodies 10000 \
  --warmup 10 --frames 60 --workers 1 --format json'

nix-shell --run 'bazel run //tools/physics_benchmark -- \
  --backend box3d --scene snapshot --bodies 10000 \
  --warmup 10 --frames 60 --workers 4 --format csv'
```

The runner also supports `large_pyramid`, `rain_contact_churn`, `junkyard`,
and the deliberately bounded `high_degree_overflow` reference scenes.

## 10,000 active-body snapshot scene

| Backend | Workers | Step p50 | Step p95 | Simulation average | Snapshot average |
|---|---:|---:|---:|---:|---:|
| Jolt legacy | 1 | 69.326 ms | 74.207 ms | 68.891 ms | 2.913 ms |
| Jolt legacy | 4 | 38.622 ms | 47.230 ms | 38.323 ms | 3.694 ms |
| Box3D reference | 1 | 34.521 ms | 40.342 ms | 34.326 ms | 0.754 ms |
| Box3D reference | 4 | 12.870 ms | 15.562 ms | 12.845 ms | 0.855 ms |

The scene starts mixed primitive bodies in open space. It measures active-body
integration and full CPU pose snapshots, not dense contacts or rendering.

## Correctness differences

- Jolt and Box3D intentionally produce different semantic hashes after
  simulation. Cross-backend bit equality is not required in
  `DeterministicFloat` mode.
- Both backends pass the shared terrain, five-shape, 10k-body, sleep/wake,
  water, sampler-order, dry-body, and geometric-character contract tests.
- Box3D terrain is rotated and transposed during cooking. This maps Box3D's
  TR-BL local split to Voxys' canonical TL-BR world split.
- Box3D uses its fixed 60 Hz Soft Step with four internal substeps. Jolt keeps
  the legacy variable-frame subdivision behavior.
- Persistent-memory figures are not directly comparable yet: Box3D exposes a
  world allocator counter, while the Jolt number is an explicit estimate plus
  a 16 MiB scratch allocator.

## Browser status

The Box3D backend is built single-threaded for WebAssembly. The WASM target is
compile-checked in CI/toolchain verification. Browser timing collection needs
a WebGPU-capable browser harness; native figures must not be presented as WASM
results.

Machine-readable copies of the measurements are in
[`benchmarks/phase0_cpu_snapshot_10000.json`](benchmarks/phase0_cpu_snapshot_10000.json).

## Phase 2 WebGPU ballistic exit gate

The native Vulkan validation run updates, frustum-culls, shape-buckets, and
indirectly renders 100,000 GPU-resident bodies. Queue-retired frame time was
5.058 ms p50 and 6.584 ms p95 on the Radeon 890M. Normal frames performed no
CPU body snapshot, body-state upload, or model-matrix upload.

Reproduce it with:

```bash
nix-shell --run 'bazel test //tests:gpu_ballistic_benchmark \
  -c fastbuild --test_output=streamed --test_timeout=300'
```

The machine-readable result is
[`benchmarks/phase2_gpu_ballistic_100000.json`](benchmarks/phase2_gpu_ballistic_100000.json).

## Phase 3 WebGPU static-contact exit gate

The native Vulkan gate simulates and directly renders 100,000 mixed primitive
bodies over a 512×512 sloped heightfield and a physical shoreline. Terrain
contact generation, four-point reduction, warm starting, the body-local Soft
Step solve, water sampling, buoyancy, drag, GPU culling, and indirect rendering
are active. Queue-retired frame time was 11.873 ms p50 and 14.044 ms p95 on the
Radeon 890M, below the 16.667 ms 60 Hz gate.

Focused GPU tests cover max-height-mip rejection, all five shapes settling,
slope and canonical TL–BR diagonal traversal, shoreline interaction, and wet
versus dry behavior. Normal frames still perform no CPU body snapshot or body
transform upload.

Reproduce the gate with:

```bash
nix-shell --run 'bazel test //tests:gpu_static_benchmark \
  -c fastbuild --test_output=streamed --test_timeout=300'
```

The machine-readable result is
[`benchmarks/phase3_gpu_static_100000.json`](benchmarks/phase3_gpu_static_100000.json).

## Phase 4 deterministic GPU primitives

`DeterministicGpuPrimitives` provides portable exclusive `u32` scan, stable
compaction, stable 8-bit-digit radix sorting for 32-bit and logical 64-bit
keys, adjacent unique, sorted persistent-key merge, and ascending free-ID
assignment with explicit overflow counts. Semantic ordering comes from
count/scan/scatter or a single canonical merge owner; atomics only accumulate
radix histogram counts.

Randomized CPU/GPU differential tests compare every output byte for 8,193
elements at workgroup sizes 64, 128, and 256. The portable path deliberately
uses no subgroup operation, so every tested size exercises the subgroup-free
fallback.

## Phase 5 dynamic broad phase

`GpuBroadPhase` builds a sparse sorted 3D grid, occupied-cell ranges, forward
neighbor candidates, canonical unique pairs, and persistent contact records
without CPU body readback. Active bodies query sleeping bodies; oversized
bodies use a bounded all-body fallback. The GPU exposes sorted entries, ranges,
pairs, events, persistent contacts, high-water marks, and overflow flags for
debug tooling.

The randomized CPU brute-force oracle covers 128 mixed active/sleeping bodies,
oversized bodies, contact begin/end transitions, stable contact IDs, three
cell/workgroup tuning profiles, and repeated runs. A deliberately undersized
fixture verifies deterministic retained keys and candidate, pair, and contact
overflow diagnostics.

Reproduce it with:

```bash
nix-shell --run 'bazel test //tests:gpu_broad_phase \
  --test_output=errors --runs_per_test=2'
```

## Phase 6 dynamic narrow phase

`GpuNarrowPhase` stably buckets canonical pairs into ten dedicated shape-pair
paths. Analytic sphere/capsule/box paths, OBB face/edge SAT with incident-face
clipping, and an authored eight-sided cylinder hull produce at most four
points. Points retain feature IDs, fall back to local-anchor recycling, carry
warm-start impulses, and compute a separation-weighted central friction anchor.

Tests cover all ten pair classes at workgroup sizes 64, 128, and 256. They
reconstruct every local anchor in world space, verify separation invariants,
and verify impulse persistence after a pose change. A second randomized corpus
compares 100 bounded overlap decisions against pinned Box3D GJK geometry,
including every cylinder pairing.

Reproduce it with:

```bash
nix-shell --run 'bazel test //tests:gpu_narrow_phase \
  --test_output=errors --test_timeout=300'
```

## Phase 7 dynamic graph solver

`GpuDynamicSolver` performs deterministic full recoloring with persistent-color
preference, conflict validation, canonical color ranges, and a gather-based
overflow path. Four Soft Step substeps include gyroscopic velocity integration,
warm start, biased normal solving, position integration, no-bias relaxation,
coupled central friction, twist and rolling resistance, restitution, and
persistent impulse storage. Body state is never updated with atomic floating
point operations.

Two-body/one-contact islands are selected from deterministic endpoint degrees.
One kernel keeps both body states, the constraint cache, and the manifold local
across all four substeps, then writes final state once. Larger or connected
islands use the global color path. GPU-generated indirect dispatches skip empty
color, radix, and overflow work without a CPU count readback. A byte-for-byte
differential test proves that the small-island and global paths produce the same
physical state; workgroup profiles 64, 128, and 256 validate conflict-free
coloring and deterministic overflow gathering.

A 120-tick stress fixture covers an eight-box tower, a connected 4×4 mixed pile,
and eight slope-contact avalanche bodies. It remains finite and bounded with no
color conflict or overflow.

The native Vulkan gate solves 100,000 active spheres in 50,000 disjoint contact
islands at 60 Hz with four substeps and 32 configured graph colors. Across two
runs on the Radeon 890M, the worst observed queue-retired result was 5.576 ms
p50 and 6.034 ms p95. The 16.667 ms gate passed with no normal-frame body
readback. This phase gate measures dynamic constraint preparation, scheduling,
and solving from GPU-resident manifolds; broad- and narrow-phase correctness are
covered by their focused Phase 5 and Phase 6 tests.

Reproduce the correctness and performance gates with:

```bash
nix-shell --run 'bazel test //tests:gpu_dynamic_solver \
  --test_output=errors --test_timeout=300'
nix-shell --run 'bazel test //tests:gpu_dynamic_benchmark -c fastbuild \
  --runs_per_test=2 --test_output=streamed --test_timeout=300'
```

The machine-readable result is
[`benchmarks/phase7_gpu_dynamic_100000.json`](benchmarks/phase7_gpu_dynamic_100000.json).

## Phase 8 islands, CCD, character, queries, and events

`GpuIslandManager` builds canonical atomic-min union roots, performs fixed
pointer-jump rounds, and compacts bodies by `(root, body ID)`. Whole islands use
integer sleep ticks. Wake propagation covers awake contacts and topology
splits. Sleeping bodies remain in a persistent sorted grid, and sleep/wake
events are root ordered.

`GpuCcd` stably compacts explicit bullets by body ID. Fast spheres and capsules
use bounded terrain sweeps; bullets use 16 coarse samples plus eight fixed
bisection iterations. Bullet capacity, hits, stalls, failures, iteration high
water, and overflow are explicit GPU counters. The WebGPU backend runs this pass
before terrain integration and exposes continuous-collision capability.

The synchronous character is a bounded CPU capsule mover over the shared
height formula, centered origin, and TL–BR triangle topology. Phase 8 chooses a
terrain-only nearby-body policy, so camera motion never requests a synchronous
whole-world GPU readback. See
[`character_mover.md`](character_mover.md).

`GpuAsyncQuerySystem` submits ray, sphere-overlap, sphere-cast, and capsule-cast
batches and polls a three-slot readback ring. Hits are ordered by metric, body
ID, then feature ID. `GpuEventReadbackRing` packs contact begin/end/hit and island
sleep/wake events by type priority and stable key, then returns only completed
batches. Both paths report deterministic truncation/overflow.

The focused Phase 8 suite passed twice at workgroup sizes 64, 128, and 256 where
applicable. It covers island split/wake behavior, sleeping-grid transitions,
bullet overflow and under-resolved failure reporting, terrain tunneling,
character replay, query hit ordering, and event ordering.

```bash
nix-shell --run 'bazel test //tests:gpu_islands //tests:gpu_ccd \
  //tests:cpu_capsule_mover //tests:gpu_queries \
  //tests:gpu_event_readback //tests:gpu_physics \
  --runs_per_test=2 --test_output=errors --test_timeout=300'
```

## Phase 9 replay and Lockstep contract

Schema-v1 replay files use explicit little-endian fields and a payload checksum.
Headers bind the arithmetic mode, backend/build/shader identity, content hashes,
fixed-tick constants, and every capacity. Checkpoints retain fixed-point bodies,
contacts, roots, free IDs, manifold words, graph colors, sleep counters, water
state, and random state. Commands are sorted by tick, type priority, and the
authoritative sequence. Generation checks make stale commands inert.

The scalar CPU oracle and portable WGSL kernel share saturating fixed-point
rules, two-word multiplication, signed rounding, integer square root,
sector-relative integration, canonical sphere contacts, fixed island linking,
and a fixed-iteration solver. They hash individual bodies, contacts, islands,
and the whole world. Across 20 ticks, every state word, topology word, and hash
matches exactly on the Radeon 890M Vulkan/RADV path.

The replay corpus covers all lifecycle command classes, freezes six schema-v1
world hashes plus the encoded-file checksum, replays every hash, rejects file
corruption, and reports the first stage/object mismatch. Reversing producer
insertion order produces identical bytes. The WGSL correctness kernel is a
single-workgroup certification path; it is not presented as the scalable
Lockstep throughput path.

The exact `DeterministicFloat` scope is recorded in
[`deterministic-float-certification.json`](deterministic-float-certification.json).
Float goldens are tuple-specific. Only Lockstep requires cross-backend equality.

```bash
nix-shell --run 'bazel test //tests:determinism_ci \
  --runs_per_test=2 --test_output=errors'
```

## Phase 10 server-authoritative browser networking

The networking core now has versioned packet and command codecs, a 64-packet
acknowledgement window, four-frame input redundancy, integer tick sync,
acknowledged snapshot deltas, deterministic interest cells, island authority
epochs, and a 64-tick prediction/rollback bubble. The browser adapter prefers
WebTransport and fails over to the three documented WebRTC DataChannel roles.

The bounded two-client fixture predicts both players, drops one redundant input
send, spawns a server-authoritative meteor, reconstructs an acknowledged delta,
forces rollback, records a server correction, rejects a client correction, and
rejects the old epoch after authority advances. Network and replay logs remain
canonically sorted. See [`networking.md`](networking.md).

```bash
nix-shell --run 'bazel test //tests:networking_ci \
  --runs_per_test=2 --test_output=errors'
node --check web/network_transport.js
```

## Phase 11 distributed workers and native server GPU

The coordinator consumes horizon-bounded swept proxies, unions possible
cross-worker contacts, and schedules every connected component onto one worker
with integer load scoring. Handoffs carry full checkpoints, shadow hashes, a
future switch tick, a new epoch, and a retained rollback deadline. Hash mismatch
aborts the switch. Worker-loss recovery restores the affected component on one
surviving worker and advances all epochs.

`NativeServerGpuBackend` batches world/island dispatches into one native Vulkan
submission while using the same Lockstep WGSL and arithmetic schema as browser
WebGPU. A three-world differential test matches all body bytes and aggregate
hashes to the scalar CPU oracle.

The Radeon 890M batching gate measured 0.578 ms p50 and 1.651 ms p95 for 16
worlds in one submission, versus 3.885 ms p50 and 4.513 ms p95 for 16 isolated
submissions. See [`distributed-server.md`](distributed-server.md).

## Phase 12 composed GPU world and vertical slices

The production `WebGpuSoft` path now composes commands, active compaction,
CCD, forces/water, dynamic broad and narrow phase, the Soft Step solver,
terrain contacts, islands/sleeping, events, queries, and direct rendering.
Per-stage timestamp queries are optional. Native Vulkan timestamps are scaled
with the adapter's `timestampPeriod`; the Radeon 890M measurement uses
10.019 ns per device tick.

The 100,000-body sparse scene measured 23.071 ms p50 and 24.685 ms p95 on the
integrated Radeon 890M. Broad phase was 12.523 ms p50 and direct culling/render
was 4.178 ms p50. The sampled scene had zero dynamic pairs, contacts, and
manifolds. This is an integrated-GPU result, not a claim that the plan's
representative discrete-GPU 16.667 ms gate passed on this adapter. Normal
frames still perform no CPU body snapshot or transform upload.

The current broad phase stores one center-cell record per common body and
checks the 13 forward neighboring cells. A body whose bounding diameter is
larger than the cell width uses the oversized fallback. The brute-force oracle
proves pair coverage across workgroups 64, 128, and 256. This reduced the
production broad-phase allocation from eight potential entries per body to
one.

`PhysicsStats` receives asynchronous high-water and overflow telemetry for the
grid, candidates, pairs, contacts, manifolds, solver overflow, islands,
sleeping bodies, sleeping cells, and events. Polling does not block simulation
or read back body state.

Three deterministic validation slices now exist: Demolition League,
Deadweight, and Wreckwater. See
[`product-vertical-slices.md`](product-vertical-slices.md). Their replay tests
pass, but no product has been selected because the repository contains no
human-verified comparative playtest evidence.

The full platform gate passes the native Bazel suites, builds native and WASM
targets, produces identical native/WASM hashes for all three slices, and
compiles all 23 WGSL modules through headless Chrome WebGPU.

```bash
nix-shell --run 'cmake -S . -B build -DVOXY_BUILD_BENCHMARKS=ON && \
  cmake --build build --target gpu_ballistic_benchmark -j2 && \
  VOXY_GPU_TIMESTAMP_PERIOD_NS=10.019 ./build/bin/gpu_ballistic_benchmark'
nix-shell --run 'bazel test //tests:vertical_slices_ci \
  --runs_per_test=2 --test_output=errors'
nix-shell --run './scripts/run_platform_parity.sh'
```

The composed measurement is recorded in
[`benchmarks/phase12_gpu_world_100000.json`](benchmarks/phase12_gpu_world_100000.json).
