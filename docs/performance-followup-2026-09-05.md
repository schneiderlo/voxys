# Core performance follow-up — 5 September 2026

Three independent changes preserve the computed results:

- **GPU radix prefixes:** 100,000-body physics throughput improved **15.7%** in five alternating A/B runs. Median frame latency fell **13.7%**.
- **Interest queries:** sparse, 12-peer snapshot fan-out fell from **0.321 ms to 0.011 ms** at the median.
- **Distance precomputation:** dense fan-out fell from **5.23 ms to 1.97 ms**, with identical encoded snapshots.

These are engine workload results. **They do not establish the visible WRECKWATER match performance gate.** The current small match uses a different snapshot publisher, and its process proof failed before the match started.

The investigation began at `6701a85` on a clean working tree. The earlier [performance audit](performance-audit-2026-09-05.md) already removed coordinator checkpoint copies. This report does not count that earlier work as a new optimization.

## Scope and architecture

Read the complete root `AGENTS.md` and `README.md`, including the distinction between engine benchmarks and the WRECKWATER completion contract. Traced the main application, GPU/CPU physics, rendering, terrain, water, authority, transport, replication, replay, and test paths. This is a source investigation with selected execution coverage; it is not a claim that every configuration or every line of the roughly 104,000-line C++/WGSL system was executed.

| Part | Purpose and connections |
|---|---|
| Application | Native and WASM entry points feed `Application::processFrame`. Fixed physics updates and rendering have distinct timing. Benchmark mode can use a fixed step and retire GPU submissions. |
| GPU physics | Commands → active compaction → CCD → forces/water/attachments → broad phase → narrow phase → colored Soft Step solver → islands/sleeping → events, telemetry, and queries. Shared deterministic primitives sort and compact the intermediate records. |
| Rendering | Resident body buffers feed culling and indirect primitive drawing. Terrain raycasting, shadows, lighting, animated ocean, and presentation compose the image. Normal GPU physics frames avoid a CPU transform round-trip. |
| Water and terrain | FFT cascades use precomputed twiddles and fused workgroup processing. Terrain decode/mip/shadow preparation supports the active heightfield renderer. Stationary opaque views are cached while water stays live. The separate SVO module is not the central active rendering path. |
| Physics alternatives | `box3d_reference` is the explicit CPU oracle/fallback. `jolt_legacy` remains a separate migration baseline. Integer lockstep state supports deterministic networking and distributed authority foundations. |
| WRECKWATER | The authority runtime joins roster/lifecycle fencing, canonical input, exact GPU ticks, event/pose certification, damage, flooding, characters, snapshots, and replay. Submitted and retired ticks are deliberately distinct. Client prediction and interpolation consume certified state. |
| Generic replication | `MultiplayerSession::sendSnapshots` queries interest per peer, selects bodies, stores history, chooses full/delta snapshots, encodes packets, and sends through a transport. This is the caller measured for the second change. |
| Distributed world | Coordinator/worker ownership, checkpoint, handoff, and replay machinery form reusable foundations. The current bounded match does not exercise all large-world paths. |

The current WRECKWATER slice has three primary physical bodies plus eight significant fragment slots. Its certified snapshot contains three entities and four characters. Neither a 100,000-body benchmark nor 65,536-slot generic replication is that scene.

## Baseline and measurement rules

Host: AMD Ryzen AI 9 HX 370; Radeon 890M integrated GPU; native Vulkan/RADV; Mesa 26.1.5; Linux 7.0.0-30. Vulkan reports a **10.019 ns timestamp period**. Release builds use the Nix toolchain. The native window has a 1600×900 physical framebuffer and uses the real 8192² terrain.

CPU/GPU clocks were not locked. Runs were sequential; A/B order alternated. No outlier runs were removed. Summary tables use the **median of each run's reported statistic**, not pooled frame percentiles. `/usr/bin/time -v` reports process peak RSS, including initialization. GPU scratch and persistent buffer sizes are separate counters.

### Baseline tests

```bash
nix-shell --run 'bazel test -c opt //tests:voxy_tests //tests:wreckwater_vessel_damage //tests:wreckwater_authority_gpu //tests:wreckwater_client_probe //tools:terrain_diffusion_import_test --test_output=errors'
```

Before edits: **1,411 main-suite tests: 1,404 passed, four failed, three skipped**. The other four requested targets passed. The main suite took 118.7 seconds.

After edits: **1,416 main-suite tests: 1,409 passed, the same four failed, the same three skipped**. The other four targets passed. The main suite took 116.7 seconds. Separate networking/session targets also passed.

Existing failures:

- `AuthoredCoveTest.ProfileHasReadableCoastalHierarchy`
- `AuthoredCoveTest.MaterialZonesAreNormalizedAndDistinct`
- `BlitShaderTest.ShorelineBlendAndWetResponseAreContinuous`
- `BlitShaderTest.CoastalFoamUsesDepthCrestAndSpatialDecay`

Skipped: GLFW window creation, dense-grid candidate-clamp benchmark, and LEGO horizon benchmark. Four pre-existing disabled tests also remain disabled.

### Representative workload baselines

Initial native build fallback:

```bash
nix-shell --run 'cmake -S . -B /tmp/voxys-perf-audit/cmake -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_BENCHMARKS=ON && cmake --build /tmp/voxys-perf-audit/cmake --target voxy_native -j8'
nix-shell --run '/usr/bin/time -v /tmp/voxys-perf-audit/cmake/bin/voxy_native --benchmark --no-validation'
nix-shell --run '/usr/bin/time -v /tmp/voxys-perf-followup/original/voxy_native --benchmark-bodies 10000 --benchmark-fixed-hz 400 --no-validation'
```

The zero-body cached five-view baseline measured 2,198 FPS and 495,268 KiB peak RSS. The first 10,000-body run measured 728.5 FPS. Later alternating runs were slower on both versions; the paired results, rather than that first high number, are used for comparisons. Per-view p50/p95/p99 are retained in the logs.

Initial GPU workload:

```bash
nix-shell --run 'bazel build -c opt //tests:gpu_ballistic_benchmark //:wreckwater_server //:wreckwater_client_probe'
nix-shell --run '/usr/bin/time -v env VOXY_GPU_TIMESTAMP_PERIOD_NS=10.019 VOXY_GPU_BALLISTIC_BODIES=100000 VOXY_GPU_BALLISTIC_WARMUP_FRAMES=20 VOXY_GPU_BALLISTIC_MEASURED_FRAMES=500 ./bazel-bin/tests/gpu_ballistic_benchmark'
```

That initial 500-frame run measured p50/p95/p99 **14.090/17.775/18.499 ms**, **67.854 FPS**, and **146,048 KiB peak RSS**. It reached 8,192 contacts/manifolds. It is a capacity stress result, not a clean sparse-world performance gate.

The clean large-world comparison therefore uses 20 warm-up frames and **300 measured frames**, before any pairs or contacts arise. A separate 10,000-body, 1,000-frame stress comparison retains the later contact-heavy behavior explicitly.

Generic replication baseline calls the real `sendSnapshots` path with 65,536 body slots (65,535 live), 12 peers, radius two cells, and a 256-body peer limit. The sparse layout spreads bodies across sectors; the dense control puts them in one cell. A counting transport avoids socket pacing and unbounded retained packets. The world is fixed at tick zero, so these measurements isolate full-snapshot fan-out, not simulation or delta acknowledgement traffic.

## Profiles captured before changes

### GPU workload

CPU sampling used gperftools at 1,000 Hz because `perf_event_paranoid=4`. Heaptrack captured allocation stacks; `strace -f -c` captured syscall/I/O costs. Initialization is included in these process profiles. Timing runs are separate from those profilers.

Top GPU stages in the initial baseline:

| Stage | Median ms | Share of summed stage medians |
|---|---:|---:|
| Culling and drawing | 4.317 | 35.15% |
| Broad-phase index sort/ranges | 3.018 | 24.57% |
| Dynamic solve | 1.703 | 13.86% |
| Islands/sleeping | 1.204 | 9.80% |
| Broad-phase pair count | 0.852 | 6.94% |

The denominator is 12.283 ms, the sum of stage medians. These shares are **not percentages of retired wall time**.

CPU samples: 28.14% unsymbolized, 18.44% `ioctl`, 5.23% `memset`, 2.13% WebGPU buffer usage merging, and 1.57% WebGPU storage lookup. Command sort/coalescing was 3.15% inclusive, with startup included. Incomplete driver symbolization limits CPU attribution.

Heaptrack: **874,732 allocations**, **180,322 temporary allocations**, **179.80 MB peak heap**. These totals include driver/pipeline setup and should not be read as steady-state C++ allocation rates. `strace` attributed **95.33%** of syscall time to `futex` and **2.88%** to `ioctl`; disk I/O was not the dominant frame cost.

A focused 262,144-record, eight-pass radix probe split histogram/prefix/scatter. Before the change, their GPU medians were **0.376 / 2.448 / 1.104 ms**: the prefix alone was **62.3%**. The parallel prototype reduced the prefix to **0.334 ms**, leaving histogram/scatter essentially unchanged. Both versions matched the complete CPU stable-sort output.

A separate 100,000-body culling probe measured **0.313 ms** retired median. Its block prefix was only **0.071 ms**. The much larger combined culling/drawing stage therefore did not justify treating culling prefix work as the next large frame win.

### Generic replication

The 65,536-slot sparse fan-out CPU profile contained 6.675 sampled seconds:

| Function | Self CPU share |
|---|---:|
| `InterestGrid::query` | 96.52% |
| `PacketCodec::encode` | 0.46% |
| Vector insertion | 0.42% |
| `SnapshotCodec::encode` | 0.36% |
| `snapshotStateHash` | 0.30% |

Heaptrack recorded **751,713 allocations**, **145,513 temporary allocations**, and **13.18 MB peak heap** for 2,000 measured rounds plus warm-up. Packet encoding accounted for 169,680 allocation calls. Those allocations were not the dominant CPU cost in this sparse workload. Syscall time totaled just **3.048 ms**, mostly loader file lookups; this counting-transport fixture intentionally performs no socket I/O.

Exact profiler commands and their reports are in the evidence directory. No serialization, lock, or queue change was inferred from allocation counts alone.

## Opportunity matrix

Scores were recorded before each corresponding production edit. Impact is 1–5; confidence is 0–1; effort is relative implementation/proof effort. Score = impact × confidence / effort.

| Candidate | Impact | Confidence | Effort | Score | Decision |
|---|---:|---:|---:|---:|---|
| Restrict interest query to indexed x interval | 4 | 0.99 | 1 | 3.96 | Implement after caller profile |
| Precompute per-body interest distance | 4 | 0.99 | 2 | 1.98 | Implement after dense caller profile |
| Parallel radix histogram prefixes | 5 | 0.90 | 3 | 1.50 | Implement after isolated GPU profile |
| Parallel culling prefix | 1 | 0.98 | 1 | 0.98 | Defer: ~0.071 ms measured |
| Empty/already-sorted command fast path | 1 | 0.95 | 1 | 0.95 | Defer: modest inclusive CPU share, startup mixed in |
| Ordered merge for snapshot delta application | — | — | — | — | Needs a churn/acknowledgement workload |
| Parallel/spatially indexed GPU queries | — | — | — | — | Needs active query load and exact tie-order proof |

The original matrix and subsequent profile-backed updates are retained verbatim in `opportunity-matrix.txt`. Unknown scores are left unscored rather than invented.

## Change 1: parallel radix prefixes

Production files: `physics_deterministic_primitives.wgsl` and the matching `deterministic_primitives.cpp/.hpp` pipeline lifecycle and dispatch code.

The old prefix gave each digit one lane that walked every histogram block. The new large-input path uses 64 workgroups. Each workgroup owns four digits; 64 lanes per digit process contiguous block ranges. An integer workgroup scan supplies each range's starting offset. The existing digit scan then produces global digit bases.

Inputs with at most 32 active histogram blocks retain the original serial prefix. The extra pipeline uses existing histogram, offset, base, and workgroup storage. No new GPU buffers or bind-group layouts are introduced.

### Equivalence proof

Let `h[b,d]` be the unchanged count for block `b`, digit `d`.

1. Each lane totals one contiguous block range. The scan gives it the sum of all earlier ranges for the same digit.
2. Walking that range writes `offset[b,d] = sum(h[k,d], k < b)`, exactly the old value.
3. The last lane writes the digit total. The following dispatch applies the original digit scan, so `base[d] = sum(total[k], k < d)` is unchanged.
4. Ranges and digits have disjoint writers. Workgroup barriers order shared-memory reads/writes; successive dispatches order storage-buffer use.
5. Integer addition is associative here; supported record counts fit the existing capacity. No floating-point expression, key transform, stable local rank, or scatter expression changes.

Thus every scatter destination and all four words of every sorted record are unchanged. The argument composes through every radix pass and every caller. Short/empty dynamic inputs keep the original result; the new preliminary kernel returns uniformly without writes when it is not needed.

This is a proof about successful operations with the same inputs. Timing telemetry, process allocation failures, or device loss are not promised to be identical.

### Equivalence evidence

- Native GPU tests compare **all record bytes** against CPU stable sort, including ties and all-ones sentinels.
- Dynamic counts cross zero and both sides of the 32-block boundary while reusing the same allocation. Tail canaries must stay untouched.
- Workgroup sizes 64, 128, and 256; full 32/64-bit keys; bounded key paths; scaled dynamic counts.
- Chrome/Tint and native/Naga compile the new shader. The browser prefix oracle compares **81,828,864 words** across old/new GPU paths and an independent CPU reference, including 0, 31, 32, 33, 128, 129, and 4,097 blocks. Zero differences. Browser correctness used SwiftShader; no browser hardware-speed claim is made.
- Stable-sort golden FNV-1a: `19354054fd844dea` at 262,144 records; `4f285c36e7c7f28c` at 1,048,576 records. Hashes summarize output; native tests also perform full byte comparisons.
- At native frame 200, 100,000 bodies and a fixed 400 Hz step, baseline/baseline/candidate PNG files are byte-identical: SHA-256 `6db97fdd34b01a9f440feefa655e8b48253cd882ba44b112a457d955c0b74c68`. The image was visually inspected. Screenshot mode deliberately exits before the five-view benchmark completes, so exit status 2 is recorded; this is an image oracle, not a passing benchmark run.

### Measured results

Five alternating A/B repetitions for whole physics; three for isolated sort:

| Workload / version | p50 ms | p95 ms | p99 ms | Throughput/s |
|---|---:|---:|---:|---:|
| 100k physics / before | 14.290 | 15.082 | 15.441 | 69.882 frames |
| 100k physics / after | 12.331 | 13.208 | 14.320 | 80.820 frames |
| 10k sparse physics / before | 2.821 | 3.442 | 3.665 | 350.251 frames |
| 10k sparse physics / after | 2.669 | 3.301 | 3.668 | 357.587 frames |
| 10k contact stress / before | 3.446 | 6.124 | 6.638 | 246.919 frames |
| 10k contact stress / after | 3.209 | 5.770 | 6.249 | 262.646 frames |
| 262,144-record sort / before | 5.186 | 6.258 | 6.381 | 189.128 sorts |
| 262,144-record sort / after | 2.210 | 3.381 | 3.791 | 406.972 sorts |
| 1,048,576-record sort / before | 18.844 | 19.606 | 20.379 | 53.014 sorts |
| 1,048,576-record sort / after | 8.767 | 9.905 | 10.635 | 115.130 sorts |

The 100k comparison has zero pairs, contacts, and manifolds in every run. Its GPU persistent/scratch counters remain **24.125/185.676 MiB**. Median process peak RSS was **138,536 → 138,668 KiB**. The 10k stress fixture reaches the 8,192-contact capacity in both versions, with identical final pair/contact/manifold counts; it is a stress control, not an overflow-free product workload.

At 262,144 records, sort scratch remains exactly **8,406,032 bytes**, cached bind groups remain three, and median peak RSS was **84,336 → 84,648 KiB**. At 1,048,576 records, scratch remains **33,596,432 bytes** and peak RSS **113,908 → 113,776 KiB**.

Small dynamic inputs in a large allocation still incur an extra no-op dispatch. At capacity 262,144, 100 live records measured **0.210 → 0.280 ms** median in the final three-run matrix (the earlier matrix showed **0.203 → 0.217 ms**). This is a real observed small-input cost, with host timing variation; no universal speedup is claimed. Small allocations retain the old dispatch count. At 10,000 live records in a large allocation, the final cutoff improves the median **0.354 → 0.275 ms**.

The initial implementation used a 128-block crossover. Its 10k native control regressed 2.9%. A 72-run cutoff sweep showed that the parallel path helps much earlier: at 10,000 records, cutoff 32 improved p50/p95 **0.360/1.024 → 0.289/0.885 ms** relative to cutoff 128. The final implementation uses 32; both host and shader conditions, boundary tests, and full caller A/B were updated together. Initial results remain in the evidence.

The native five-view terrain/ocean benchmark now measures **585.4 → 615.8 FPS** median at 10,000 bodies (+5.2%, five alternating runs), with peak RSS **448,852 → 449,236 KiB**. At 100,000 bodies it measures **133.3 → 142.2 FPS** (+6.7%, three runs), with peak RSS **484,244 → 484,444 KiB**. View-five p95/p99 medians improve **46.11/47.92 → 42.51/43.81 ms** at 100k and **7.51/9.35 → 7.10/8.98 ms** at 10k. One 10k candidate run remains slower than its paired baseline. These cached, batched-retirement view measurements are not displayed-match frame pacing.

A 4/8/16/32-digit workgroup experiment kept the four-digit implementation. Eight digits improved one isolated median modestly but did not improve the million-record throughput or tail consistently. Wider tiles lost throughput. This is a measured choice on this adapter, not a universal tuning claim.

## Change 2: indexed interest queries

Production file: `src/network/replication.cpp`. Two binary searches restrict the original scan to the eligible x-coordinate interval. The existing three-axis predicate and output handling are unchanged.

### Equivalence proof

`rebuild` sorts entries lexicographically by cell and body ID. Therefore x coordinates are monotone. The original query rejects every entry outside `[center.x - radius, center.x + radius]`. Lower/upper bounds select exactly the remaining contiguous interval.

Inside it, iteration order, three-axis checks, first-N truncation, final body-ID sorting, and overflow flags stay the same. Bounds use signed 64-bit arithmetic, including extreme signed 32-bit centers. Radius clamping remains unchanged. Empty and truncated rebuilt grids preserve their original behavior. No persistent state, allocation shape, snapshot bytes, or packet fields change.

### Equivalence evidence and results

The independent full-scan oracle checks 33,048 queries over populated/empty/populated rebuilds; entry and result limits; duplicate IDs; negative positions; extreme sectors/centers; and radii through `UINT32_MAX`. Networking and multiplayer-session regression tests pass.

Full encoded per-peer snapshot goldens stay `5978e3beece372f6` (sparse) and `2bea129f589aaa69` (dense). Packet and byte counts also match the baseline. Hashes supplement the exact query-vector oracle; they do not substitute for the proof.

Five alternating repetitions, 65,536 slots and 12 peers:

| Layout / version | p50 ms | p95 ms | p99 ms | Fan-out rounds/s |
|---|---:|---:|---:|---:|
| Sparse / before | 0.323161 | 0.361882 | 0.521760 | 3,005.29 |
| Sparse / after | 0.010559 | 0.015910 | 0.023213 | 81,731.50 |
| Dense / before | 5.312070 | 6.053410 | 6.829390 | 183.36 |
| Dense / after | 5.395940 | 5.819190 | 6.440370 | 183.50 |

Sparse median peak RSS: **16,140 → 15,948 KiB**. Dense: **16,140 → 16,140 KiB**. The change targets scanning cost and adds no cache or retained memory. At 4,096 slots, sparse fan-out also improves; dense throughput remains within observed variation. All runs are retained.

## Change 3: precompute snapshot selection distances

Production file: `src/network/multiplayer_session.cpp`. This is a separate lever from the indexed query. The dense caller profile measured **63.14% self CPU in `InterestGrid::cellFor`**, **13.84% in query scanning**, and **10.82% in merge sort**, over 5.572 sampled seconds. The comparator was repeatedly performing the same three integer divisions for each side of each comparison.

The new code computes each candidate's existing unsigned 64-bit squared cell distance once, then stable-sorts `(distance, bodyId)` records. It writes those IDs back into the existing ID vector. Truncation and snapshot construction remain unchanged. No state persists across calls.

### Equivalence proof

For a valid snapshot call, the world and grid do not change during selection. Each stored distance therefore equals every former comparator evaluation for that body. The pair comparison is the same lexicographic `(distance, bodyId)` order as the old comparator, including ties. Stable sorting keeps the same tie order. Subsequent truncation, controlled-body insertion, ID sorting, hashing, and encoding receive the same IDs in the same order. Integer expressions and widths are unchanged; no floating-point reassociation or cache invalidation assumption is introduced.

### Equivalence evidence and cost

The same sparse/dense baseline snapshot-byte goldens and packet counts pass. A new integration test checks exact encoded snapshot bytes for varied 3D distances, equal-distance ID ties, negative cell boundaries, out-of-range bodies, and four peer limits, including controlled-body-only output.

This trades extra bounded temporary memory for fewer repeated calculations. `rankedBodies` uses one 16-byte record per candidate on this ABI; stable-sort scratch can add another half-vector. The old ID vector is reused. There is no retained cache. The default query limit is 4,096; the extra peak temporary storage for that limit is roughly 88 KiB relative to the old sort. Sparse controlled-body-only queries allocate no rank records.

Five rotated-order repetitions isolate the two replication levers:

| Layout / implementation | p50 ms | p95 ms | p99 ms | Fan-out rounds/s |
|---|---:|---:|---:|---:|
| Sparse / original | 0.321087 | 0.463081 | 0.697747 | 2,919.22 |
| Sparse / indexed only | 0.010559 | 0.023213 | 0.030447 | 64,254.70 |
| Sparse / indexed + distances | 0.010810 | 0.015008 | 0.015579 | 84,205.70 |
| Dense / original | 5.232060 | 5.589620 | 8.790110 | 186.60 |
| Dense / indexed only | 5.317690 | 5.769630 | 7.200010 | 184.29 |
| Dense / indexed + distances | 1.970960 | 2.088390 | 2.689160 | 494.90 |

Median peak RSS: sparse **16,140 / 16,140 / 16,116 KiB**; dense **16,140 / 16,208 / 16,184 KiB** in the same order. RSS granularity and allocator variation do not establish that the new temporary vector is free; its bounded storage cost is explicit above. A separate dense heaptrack run measured 200,205 → 204,057 allocation calls for 300 measured rounds plus warm-up; peak heap remained 13.18 MB at the reporting precision. The extra rank-vector allocations are included, not hidden.

## Regression guardrails and exact commands

Both new benchmarks are manual targets in Bazel and CMake. Their default timing threshold is disabled so ordinary machines do not inherit this host's budget. A missing GPU fails the radix benchmark; it is not treated as a passing skip.

```bash
nix-shell --run 'bazel build -c opt //tests:gpu_radix_benchmark //tests:replication_benchmark'

# Calibrated for this host; full stable-byte oracle and cache-size check included.
nix-shell --run '/usr/bin/time -v env VOXY_RADIX_MAX_P95_US=4500 VOXY_RADIX_MAX_SCRATCH_BYTES=8406032 ./bazel-bin/tests/gpu_radix_benchmark'

# Fixed-state 12-peer fan-out, with baseline snapshot-byte goldens.
nix-shell --run '/usr/bin/time -v env VOXY_REPLICATION_MAX_P95_US=100 ./bazel-bin/tests/replication_benchmark'
nix-shell --run '/usr/bin/time -v env VOXY_REPLICATION_DENSE=1 VOXY_REPLICATION_FRAMES=500 VOXY_REPLICATION_MAX_P95_US=3500 ./bazel-bin/tests/replication_benchmark'

# Independent browser prefix oracle. Requires Node 22+ and Chrome.
VOXY_RADIX_REPORT=/tmp/voxys-radix-oracle.json node scripts/test_radix_histogram_prefix.mjs

# CMake build path checked as well.
nix-shell --run 'cmake -S . -B /tmp/voxys-perf-audit/cmake -DCMAKE_BUILD_TYPE=Release -DVOXY_BUILD_BENCHMARKS=ON && cmake --build /tmp/voxys-perf-audit/cmake --target gpu_radix_benchmark replication_benchmark -j8'
```

The candidate passes the sparse and radix guards in both build systems, and the dense guard in Bazel. The original radix implementation fails the same 4.5 ms p95 guard while still passing the byte oracle. The original replication implementation also fails both the 0.1 ms sparse and 3.5 ms dense guards while preserving the same snapshot goldens. These checks confirm that the timing guards detect the removed costs.

For the full physics comparison, run this command from each isolated version's directory so it loads that version's shader tree:

```bash
nix-shell --run '/usr/bin/time -v env VOXY_GPU_TIMESTAMP_PERIOD_NS=10.019 VOXY_GPU_BALLISTIC_BODIES=100000 VOXY_GPU_BALLISTIC_WARMUP_FRAMES=20 VOXY_GPU_BALLISTIC_MEASURED_FRAMES=300 ./gpu_ballistic_static'
```

Baseline/candidate executables were linked to isolated static CMake core versions. Merely copying a Bazel executable is insufficient because its shared-library runfiles can change underneath it. The experiment scripts and exact compile/link argv are retained with the evidence.

## Other patterns checked

| Pattern | Finding |
|---|---|
| N+1 fetch/query | Removed the measured per-peer whole-grid scan. The WRECKWATER publisher already encodes its shared snapshot payload once, then adds peer-specific acknowledgements. |
| Zero-copy / reuse | GPU body state, bind groups, scratch buffers, readback rings, and native receive-buffer compaction already use reuse/bounds. This change adds no CPU transform copy. |
| Serialization | Generic snapshot selection can encode full and delta candidates and then encode the chosen result again. It needs an acknowledged-delta workload before spending a separate diff. Wire-format changes fail the requested output constraint. |
| Backpressure | TCP queues, service quanta, frame limits, and asynchronous GPU readback rings are bounded. Changing drop/admission/cadence policy changes externally observable behavior. |
| Lock striping | GPU profiles are dominated by GPU work and driver synchronization. No measured application lock hotspot justifies added sharding complexity. |
| Memoization | Stationary opaque rendering, water preparation, and bind-group caches already have reuse strategies. New cross-tick gameplay caches need stronger invalidation evidence. |
| Streaming | Terrain decode/mip preparation already streams. File I/O is not the measured frame bottleneck. |
| Precomputation | FFT twiddles and replay event prefixes already exist. Per-query distance precomputation removes the measured dense generic-replication hotspot. |
| Indexing / binary search | Implemented x-interval restriction using the existing ordering. Replay seeking also already uses indexed/binary lookup. |
| Prefix sums | Implemented a parallel integer prefix while preserving exact stable scatter destinations. |
| Two pointers / DP | Ordered snapshot delta merge could replace repeated vector erases/inserts. No measured recurrence problem warrants new dynamic-programming state. |

## Existing blockers and limits

- Bazel `//:voxy_native` fails because `generated/wreckwater_build_content.hpp` is missing from its compilation closure. CMake native builds work. This unrelated build issue was recorded, not hidden by a refactor.
- The four-client native process proof fails at tick zero. Explicit TCP admission emits `ConnectionRequested`; the authority lifecycle path handles `Connected`/`Disconnected` but does not admit the request. All four frames are rejected and the scheduled reconnect finds no active connection. Altering admission semantics is a correctness task, not an isomorphic optimization.
- The existing broad shader validation script reports a stale source manifest (missing `mesh_path` and `physics_attachments`). Targeted native and Chrome validation of the edited module passes. The stale manifest remains unchanged.
- No visible, moving, networked WRECKWATER match was successfully benchmarked here. No 60 FPS product claim is made.
- Performance is demonstrated on one integrated AMD GPU and one CPU. Chrome/Tint correctness is covered through SwiftShader; other hardware needs its own timing calibration.

## Evidence

[Machine-readable summaries and logs](benchmarks/performance-followup-2026-09-05/README.md) contain exact per-run p50/p95/p99, throughput, peak RSS, golden hashes, profile tables, opportunity scoring, build commands, and baseline/final test details. Large raw heap traces and temporary binaries remain under `/tmp/voxys-perf-followup`; compact source probes and text reports are retained in the repository.
