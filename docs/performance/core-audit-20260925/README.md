# Core performance follow-up — 25 September 2026

Two remaining repeated scans are addressed: village reservation rejection now
uses sorted intervals with prefix maxima, and the two hot construction-validation
lookups use certified binary search. They preserve original predicates, input
order, failure behavior and atomic publication. The changes are separate runtime
patches with separate equivalence arguments and measurements.

At 768 parts, the combined change reduced median accepted-edit update latency
from **14.18 ms to 5.17 ms (63.6%)**, improved edit-replay throughput **39.8%**,
and changed peak RSS from **277.76 MiB to 278.00 MiB**. All exact-output and
predeclared performance guards passed. These are CPU component measurements.

The full root README was read first; the working-tree AGENTS.md is empty. This
audit starts from the existing working tree, preserving its shader/physics edits
and deleted/untracked assets. The previous audit's optimizations are already part
of this baseline. Source and executable fingerprints identify the measured state.

Additional concurrent edits to application, saves, catalogs, HUD and build files
appeared after the final executable was preserved at 00:44:45 UTC on 26 September.
They are untouched and outside this audit's validation. The comparisons and test
results below apply to the preserved snapshot, not the subsequently changing
working tree. `evidence/final-source-check.json` records that distinction.
Later edits also reached other functions in `construction_policy.cpp`; a final
check confirms this audit's lookup helper and construction-validation body remain
identical to the measured source. Those additional interaction changes are not
covered by these results; see `evidence/final-verification.json`.

## System understanding and scope

Voxys is a C++20 engine targeting native GLFW/wgpu-native and browser
Emscripten/WebGPU. The active product is solo free LEGO-style building on the
installed 8192² landscape, with a toy figure, shared GPU HUD, forest/village,
imported Blacksmith, bike, cannon and partial wall destruction. Creative material
stock is unlimited; accepted construction and GPU resources remain bounded.
The root README's earlier Preact hotbar description is superseded by the current
shared HUD implementation and `ui/README.md`.

The main ownership and execution chain is:

1. Platform entry selects configuration and drives `Application::processFrame`:
   input, update, render and presentation. Active free-build input is routed to
   `AdventureRuntime`; explicit legacy routes select other adapters.
2. `AdventureSession` creates private prepared candidates. Stable identities,
   request sequence, revisions, ownership and the integer .02 m lattice remain
   authoritative. `construction_policy.cpp` checks terrain, reach, occupancy,
   doors, overlap and support before a command can commit.
3. `AdventureRuntime::prepareGeometry` compiles owned solids and door reservations,
   admits installed scenery, then prepares a complete query snapshot. Commit
   replaces state, walking/picking/camera geometry and presentation together.
   Reserved visual envelopes are distinct from physical collision geometry.
4. `AdventureSpatialQueries` retains original solids plus 16-unit spatial buckets
   and a sorted membership index. Player, camera, bike and interaction queries
   share accepted geometry. Forest source tiles and picking/preview/JSON caches
   already have explicit identity/revision invalidation.
5. `PhysicsWorld` selects WebGPU, Box3D reference or Jolt legacy. The GPU path owns
   persistent body state, broad/narrow phase, colored solving, CCD, sleeping,
   queries and events. Submission/readback bounds and generation/fence lifetime
   are part of correctness. Rendering normally consumes GPU poses directly.
6. Rendering combines camera-dependent cached terrain, authored meshes, live
   water, primitive bodies and the shared HUD. Mesh submesh indexing, retained
   scratch storage and distant forest instances already avoid several repeated
   costs. Surface acquisition waits for physics admission; a loop iteration is
   not necessarily a rendered/presented frame.
7. Save codecs produce bounded canonical archives with content/world validation.
   Native transport uses a one-operation worker and durable replicas; browser
   transport uses host storage. Only confirmed completion may report a save.
   The UI bridge preserves fresh authoritative action admission while suppressing
   unchanged presentation.

Adventure quests, salvage Cove, RIDGEBREAK and WRECKWATER remain separate routes.
Their networking/replication/coordinator and authored-asset paths were also
reviewed. They are not all executing during free building. Asset admission already
uses integrity hashes, bounded decoded snapshots, cached packs and fenced GPU
resources. TCP already handles partial nonblocking I/O with bounded queues.

This traces the important architecture and ownership boundaries; it does not
claim formal verification of every historical source line. The measured workload
is actual CPU building/runtime logic with real terrain, not rendered browser FPS,
GPU throughput, displayed input latency, durable-save latency or multiplayer load.

## Method and evidence

[The measurement contract](plan.md) was written before runtime edits.
[The ranked decision record](design.md) contains the fresh hotspot percentages,
impact × confidence / effort matrix, and a proof sketch for each change.
[Exact commands](commands.md) cover tests, preserved binaries, all replay stages,
CPU/heap/I/O captures and matched comparisons.

The deterministic workload has 120 warmup updates and 3,600 measured updates,
fixed 1/60 s steps and JSON observation every sixth update. It covers idle/edit
at 0, 64, 256 and 768 parts, three repetitions each. Timings use nearest-rank
p50/p95/p99, with accepted-edit updates measured separately. Ten accepted edits
would otherwise disappear below the whole-replay p99 cutoff. With ten samples,
accepted-edit p95 and p99 are both the run maximum, not reliable population-tail
estimates. Throughput includes
observation bookkeeping. Peak RSS includes terrain, headless GPU initialization
and retained golden strings; golden files are written after the timed interval.

The first matrices and diagnostic profiles overlapped the original GPU test
suite and are explicitly labeled contended. Only the final interleaved matrices,
run after audit tests/builds, support latency/throughput claims. The preserved
baseline, village-only and combined executables rotate execution order for each
of three repetitions. Ordinary desktop activity remains; no clock/frequency lock
or cold-cache claim is made.

One sequencing limitation: the aggregate suite started before edits but completed
afterward, so its completion did not precede implementation as strict step A
requested. Original binaries stayed untouched, pre-change oracles passed before
each lever, and final timing waited for the original suite to finish.

The oracle requires byte-identical complete JSON observations and save archives,
equal outcomes, exact archive round trips, unchanged creative inventory and dormant
combat. Independent tests cover ordered village geometry, strict floating-point
boundaries, exceptional bounds, duplicate/reordered inputs, namespace aliases,
capacity and suppression; construction tests cover first-match duplicates,
unsorted inputs, missing IDs, refusal strings and immutability. These tests run
against the original implementations before their respective runtime changes.

Proofs are local semantic arguments for identical ordered inputs/content/time
steps and successful allocation. A finite replay does not prove every possible
external schedule. Neither simulation cadence, validation order, save/JSON format,
quality, draw distance nor resource admission policy changes.

## What the profiles establish

The baseline edit profile attributes 28.75% of sampled CPU to the village's
reservation loop. Construction validation is 25.25% inclusive, including 4.96%
in linear part lookup. Terrain sphere sweep is 7.89%; JSON is 6.47%. These
inclusive numbers overlap. Idle differs: JSON 22.00%, terrain sweep 15.22%, HUD
10.33%, target update 6.56%, input routing 5.44%. The harness's own string search
is excluded as an engine optimization target.

Allocation is 426.32 MiB across 1,065,129 objects for the short edit replay;
validation accounts for 61.87% of objects including callees. The measured I/O
interval contains 60 GPU queries and six heap-growth calls, with no file/network
data operations. Startup is separate: SHA-256 compression is 70.86% of its sampled
CPU. Full [profile attribution](evidence/profile-summary.md) and raw captures
retain denominators and limitations.

Baseline and final profiles attribute 1,058 and 120 samples (at 1 kHz) to village
admission over the same long edit workload. The remaining `partFor` samples
come from the unchanged door-clearance accepted-solids loop, not the certified
lookup fallback. Total allocation grows by 2,118,048 bytes (2.02 MiB) and 11
objects over the short replay, consistent with the temporary reservation index. That index
uses one entry per reservation; no persistent cache is introduced. Sample counts
are diagnostic work evidence, not substitutes for clean timing comparisons.

## Other requested patterns and remaining work

| Pattern | Finding and next step |
|---|---|
| N+1 query/fetch | The measured repeated work is in-memory scans, now indexed. No hot file/network fetch chain appears in this replay. |
| Zero-copy, reuse, scatter/gather | GPU poses and renderer scratch already stay resident. Candidate copies protect rollback; no measured hot scatter/gather I/O is present. Allocation totals alone do not justify bypassing ownership. |
| Serialization | Existing structure fragments are cached. JSON remains measurable, but a new format or different float/locale formatting violates exact output. Further caching needs an explicit dynamic-field/invalidation oracle. |
| Bounded queues/backpressure | GPU/readback/save/TCP paths already have bounds. Changing admission, supersession or completion timing is a behavior change without separate proof and load evidence. |
| Sharded/striped locks | No contention hotspot is demonstrated in active CPU building. Threading this sequential command path would add ordering obligations. |
| Memoization/precomputation | Existing terrain, forest, preview, ray and HUD caches were checked. The new local indexes need no cross-frame invalidation. |
| Indexing/binary search/prefix aggregates | Applied to reservations and parts. Prefix **maxima** exclude impossible X intervals; sums cannot answer general overlap. Wide intervals retain a worst-case scan. |
| Dynamic programming | Support validation already traverses a bounded connectivity graph. Incremental reuse must preserve removal/support/refusal semantics; no such rewrite is bundled here. |
| Lazy/deferred computation | Geometry publication and validation remain atomic. Deferring them changes when commands become accepted. Presentation-only work needs a separately measured case. |
| Streaming/chunking | Forest tiles and GPU work already use bounded/chunked forms. Browser packs are cached per experience as whole ArrayBuffers; this is not streaming decode. Save or collision snapshots cannot become partially visible. |
| Two pointers/sliding windows | Sorted snapshot replication could use a stable two-pointer merge under a multiplayer workload. No measured active-game additive window justifies a sliding-window change. |
| Binary search on answer space | No measured expensive monotone predicate has been established. It is not a replacement for ordered contact/support decisions. |

The next strong separate project is hardware-accelerated SHA-256 with a portable
fallback, preserving all digest/error behavior and testing every chunk/padding
boundary on native and WASM. It targets startup, not frame rendering. Remaining
door-clearance membership scans can reuse certified lookups in a focused door-heavy
workload. Construction graph/map allocation and duplicate geometry compilation
also deserve isolated attribution before changing their semantics.

GPU bind-group creation is a plausible cache target, and serial event packing
could potentially use stable integer prefix scans. Both need stage profiles,
exact event-order/overflow oracles and resource-lifetime checks. Header-first
readback would change asynchronous completion/backpressure and is not assumed
equivalent. Multiplayer coordinator pair generation and replication merges have
clear algorithmic alternatives, but are not measured free-build bottlenecks.

## Regression guardrails

The comparator retains its predeclared same-host limits: p50/p95/p99 at most
10%/15%/20% slower, with a 25 microsecond allowance for small timings; throughput
loss at most 15%; RSS growth at most the larger of 10% or 10 MiB. All 24 cases
and exact outputs are mandatory. It now also rejects invalid/nonfinite metrics
before aggregation; all nine fault-injection tests pass. No guard is weakened.

## Measured results

All values are medians of three per-run quantiles or scalars, not pooled
quantiles. The complete 72 raw metric records, serialization quantiles and all
three stage comparisons are in [the measurement summary](evidence/clean-summary.md)
and its [machine-readable source](evidence/clean-summary.json).

### Accepted-edit update latency


Each triple is p50 / p95 / p99 in ms. Approximately ten samples per run means p95/p99 are observed maxima, not established population-tail estimates.

| Parts | Baseline | Village only | Combined | Samples/repeat (B; V; F) |
|---:|---:|---:|---:|---|
| 0 | 12.06 / 12.72 / 12.72 | 2.93 / 3.11 / 3.11 | 2.90 / 3.19 / 3.19 | 10/10/10; 10/10/10; 10/10/10 |
| 64 | 11.60 / 12.17 / 12.17 | 3.10 / 3.35 / 3.35 | 3.07 / 3.37 / 3.37 | 10/10/10; 10/10/10; 10/10/10 |
| 256 | 12.13 / 12.67 / 12.67 | 3.59 / 3.90 / 3.90 | 3.53 / 3.71 / 3.71 | 10/10/10; 10/10/10; 10/10/10 |
| 768 | 14.18 / 15.71 / 15.71 | 5.33 / 6.57 / 6.57 | 5.17 / 6.65 / 6.65 | 10/10/10; 10/10/10; 10/10/10 |

### All-update latency, throughput and memory

Latency triples are p50 / p95 / p99 in µs. Arrows show baseline → combined.

| Parts | Replay | Baseline latency | Combined latency | Updates/s | Peak RSS MiB |
|---:|---|---:|---:|---:|---:|
| 0 | idle | 10.79 / 12.04 / 15.22 | 10.85 / 11.63 / 15.83 | 66,770.38 → 67,000.28 | 265.16 → 264.81 |
| 0 | edit | 12.07 / 40.09 / 53.31 | 10.52 / 34.92 / 45.80 | 17,318.42 → 37,337.10 | 265.11 → 265.18 |
| 64 | idle | 11.02 / 11.35 / 15.64 | 11.04 / 11.66 / 17.00 | 63,479.60 → 63,794.25 | 264.74 → 264.96 |
| 64 | edit | 11.66 / 39.27 / 86.35 | 10.76 / 36.62 / 81.44 | 17,286.68 → 32,155.86 | 265.05 → 265.29 |
| 256 | idle | 11.35 / 12.51 / 17.29 | 11.32 / 11.62 / 16.28 | 55,990.83 → 56,057.76 | 265.29 → 265.32 |
| 256 | edit | 11.75 / 38.38 / 263.98 | 11.30 / 36.94 / 224.20 | 13,403.24 → 22,014.04 | 264.87 → 265.02 |
| 768 | idle | 12.00 / 12.27 / 16.46 | 12.02 / 13.11 / 19.47 | 42,759.17 → 42,216.54 | 264.80 → 265.07 |
| 768 | edit | 12.11 / 37.51 / 941.73 | 12.20 / 37.21 / 763.44 | 8,515.27 → 11,902.06 | 277.76 → 278.00 |

The village index delivers most of the improvement. At 768 parts its p50 falls
from 14.18 to 5.33 ms. The separate certified-lookup stage reduces that further
to 5.17 ms (3.1% incremental); its p50 gains range from 1.0% to 3.1% across the
four sizes. That smaller stage does not uniformly improve the observed maxima,
so no stronger tail-latency claim is made. Ordinary idle latency is broadly
unchanged; this work targets accepted edits and repeated validation work.

All three comparisons pass every unchanged threshold: baseline → village,
village → combined, and baseline → combined. No timing matrix was discarded
or rerun to obtain a pass. The earlier contended matrices are clearly separated.
No compiler, test or profiler processes appear in the 72 process snapshots.
Normal desktop/agent activity remained, including a short-lived Node process in
the final snapshot; these are sampled process inventories, not continuous load
traces. The small lookup-stage gain should not be generalized beyond this replay.

### Exact outputs and test results

Each pairwise comparison covers all 24 replay cases: 14,400 complete JSON
observations and 24 save archives. Across the three variants, all 144 exact
cross-variant file comparisons pass; repeated golden hashes and sizes also
match. See the [golden manifest](evidence/golden-manifest.json). Snapshot round
trips, inventory, combat and accepted/refused outcome invariants all pass.

| Validation | Result |
|---|---|
| Original aggregate, 2,628 tests in 361 suites | 2,606 passed, 20 skipped, 2 failed; 5 additional disabled tests |
| Terrain importer | 10 passed, 1 skipped |
| Original village oracle before its runtime diff | 2 passed |
| Village plus construction oracle before the lookup diff | 18 passed |
| Seven focused candidate targets with actual terrain | 180 passed |
| Focused runtime target with actual terrain | 2 passed, 3 failed; all three failures reproduced on the original binary |
| UI/startup/performance JavaScript checks | 78 passed |
| Comparator fault-injection checks | 9 passed; final rerun also passed |
| Native application and focused optimized targets | Build passed |
| Final byte-exact/performance comparisons | All three complete comparisons passed |
| Whitespace check | `git diff --check` passed |

The original aggregate failures are
`FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget` and
`CannonPhysicsSceneGpu.ThrownBrickFlightBounceAndStackMatchJolt`. The three opt-in
runtime failures are the source-house cannon input/admission variants: unchanged
yaw/elevation and zero shots. Original and candidate logs reproduce the same
assertion sites; no failing assertion was weakened. The default aggregate skips
those opt-in variants. Full names, skips and logs are retained in
[the test summary](evidence/test-summary.json).

The full aggregate took 2,416 seconds; it was the original binary, not a final
candidate aggregate rerun. Candidate validation used the focused targets above.
No WASM/browser execution or rendered-frame benchmark was performed. Later
concurrent source changes are outside these build/test claims. The original
shader staging and pre-existing physics logging changes remain untouched.
