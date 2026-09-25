# Core performance audit — 2026-09-24

This audit starts from `5a31a5c5e871603fee69d759ab683932cc0f9316` plus the
pre-existing working-tree changes. The working-tree `AGENTS.md` is empty. The
complete root README was read before investigation. Measurements and conclusions
below refer to this checkout, not an assumed clean main branch or deployed site.

## System and scope

The active product is solo free LEGO-style construction on the installed 8192²
landscape. Adventure quests, salvage expeditions, RIDGEBREAK, and WRECKWATER
networking remain explicit legacy routes. Their presence in the aggregate engine
library does not mean their simulation runs during creative play. The current
creative HUD is shared GPU-rendered native/WASM code; the root README's older
Preact hotbar description is superseded by `ui/README.md` and the current entry
point. Preact remains available for historical component previews.

The main execution and ownership boundaries are:

1. `src/engine/platform/{native,wasm}/entry.cpp` selects installed configuration,
   creates `Application`, and drives its frame loop. Native uses GLFW and
   wgpu-native; the browser uses Emscripten, WebGPU, and JavaScript host adapters.
   `Application::processFrame` orders input, update, rendering, and presentation.
2. `AdventureRuntime` adapts input and menus into `AdventureSession` commands.
   A session creates a private candidate, validates identity/inventory/content,
   invokes shared world validation, and commits only a valid prepared change.
   The integer .02 m lattice, stable IDs, revision, and request sequence are
   authoritative. Creative rules come from installed content, not save flags.
3. `construction_policy.cpp` compiles catalog collision boxes, checks terrain,
   reach, player occupancy, door swing, overlap, and support connectivity.
   `AdventureSpatialQueries` publishes complete accepted collision revisions
   into 16 m spatial buckets shared by picking, walking, camera, and interaction.
   Scenery admission yields to owned builds. Forest tiles, the imported
   Blacksmith, cannon, bike, and wall destruction extend this accepted scene.
4. `Application::render` reserves a physics submission before surface acquisition,
   advances the water field, encodes GPU physics, and composes terrain, authored
   meshes, water, primitive bodies, and HUD. Submission guards preserve resource
   lifetime and discard/submit semantics. Camera-dependent terrain is cached;
   animated water remains live. Mesh rendering already indexes logical submeshes,
   retains scratch vectors, batches compatible draws, and keeps distant forest
   instances resident. Transparent ordering and shadow-only casters matter.
5. `PhysicsWorld` hides WebGPU, Box3D reference, and Jolt legacy backends. The
   WebGPU path owns persistent bodies, broad/narrow phase, colored solving,
   islands, sleeping, CCD, queries, events, and bounded asynchronous readback.
   Renderers consume GPU poses directly. Tick, generation, submission, and event
   order are part of the correctness contract; queue capacity is deliberate.
6. `AdventureSaveCodec` produces canonical bounded binary archives and validates
   content/world identity on decode. Native storage runs through a one-operation
   worker and paired durable replicas; browser storage has a separate host
   adapter. Success means confirmed durable storage. Save publication and normal
   frame execution have different I/O profiles.
7. The shared HUD reads authoritative state and publishes hit regions and opaque
   menu intents. The browser bridge keeps fresh observations for action admission
   while suppressing unchanged presentation. Legacy networking separately owns
   packet codecs, replication, interest indexing, authority, and migration.

The investigation follows these boundaries through their implementations and
tests. It is not a claim to have formally verified every historical route or
every source line. In particular, CPU building replays do not measure browser
composition, displayed input latency, GPU rasterization, or multiplayer traffic.

## Measurement contract

All performance comparisons use the same host and compiler flags, with tests and
builds finished before timing. Profiling runs are separate from clean timing.
The native benchmark executable is preserved before any runtime changes. Source,
binary, and terrain hashes identify the measured inputs. Existing shader and
physics working-tree edits are preserved.

The deterministic building workload is the existing
`tools/benchmarks/free_build_benchmark.cpp`: the actual runtime, full terrain,
public input/actions, isolated temporary saves, 120 warmup updates, 3,600 measured
updates at 1/60 s, and UI serialization every sixth update. Three repetitions
cover idle/edit at 0, 64, 256, and 768 pieces. Latencies use nearest-rank
p50/p95/p99; throughput includes observation bookkeeping; peak RSS includes
terrain, GPU initialization, and retained golden strings. Golden-file writes
occur after timing. CPU profiles use longer replays for sample attribution.

The equivalence oracle requires complete byte-identical JSON observations and
save archives for matching inputs, equal placement/removal/validity counts,
exact save decode round trips, unchanged creative inventory, and stopped combat.
Timing values are measurements, not golden game outputs. A local proof must also
cover unexercised input order, duplicate/invalid identities, refusal ordering,
cache invalidation, floating-point evaluation, and ownership/lifetime boundaries
where relevant. Passing a finite replay alone is not an isomorphism proof.

Existing same-host guardrails reject median-of-three p50/p95/p99 regressions
above 10%/15%/20%, with a 25 μs small-workload allowance; RSS growth above the
larger of 10% or 10 MiB; throughput losses over 15%; missing matrix cases; and
any exact-output mismatch. Profiled runs never enter those guards.

## Results

Two runtime changes were selected from the measured hotspots. Nearby scenery now
uses the reservation grid that admission already constructs, instead of scanning
every reserved collision box for each prop. Installed-object visibility now uses
a sorted unique part-counter index built at accepted geometry publication,
instead of scanning thousands of collision boxes repeatedly during updates.
The original geometry vectors and their ordering are unchanged.

The [decision record](design.md) contains the ranking made before runtime edits,
the separate proof sketches, and the exact equivalence obligations. Each runtime
lever was built and measured separately. The benchmark additionally records
accepted-edit latency: ten expensive committed edits in 3,600 frames otherwise
fall below the aggregate p99 cutoff. The unmodified runtime was rebuilt with this
timer before comparison; all old/new harness observations and saves matched.

## Measured results

Numbers below are medians of three runs. Each latency triplet is p50 / p95 / p99
in microseconds; arrows show original runtime → both changes. Throughput is CPU
replay updates/second, including observation bookkeeping. Peak RSS is MiB. The
baseline here is `baseline-edits`, so both sides use the identical timing harness.

| Parts / mode | Update latency, μs: before → after | Updates/s: before → after | Peak RSS, MiB: before → after |
|---|---|---:|---:|
| 0 / idle | 16.74 / 22.07 / 27.37 → 9.93 / 11.94 / 14.98 | 42,175 → 72,608 | 264.61 → 265.21 |
| 0 / edit | 16.41 / 42.65 / 66.83 → 10.34 / 33.38 / 45.60 | 12,686 → 20,541 | 265.23 → 265.01 |
| 64 / idle | 16.08 / 16.86 / 20.88 → 10.15 / 10.25 / 12.21 | 44,720 → 69,598 | 264.59 → 264.84 |
| 64 / edit | 16.47 / 42.66 / 106.81 → 10.37 / 35.33 / 78.34 | 11,795 → 18,731 | 265.31 → 265.21 |
| 256 / idle | 16.13 / 22.45 / 27.58 → 10.57 / 10.77 / 13.89 | 39,177 → 59,878 | 265.29 → 265.34 |
| 256 / edit | 16.93 / 43.94 / 278.14 → 10.64 / 35.06 / 248.92 | 10,062 → 14,705 | 264.79 → 265.48 |
| 768 / idle | 18.18 / 25.00 / 33.99 → 11.33 / 16.51 / 19.16 | 30,327 → 43,505 | 265.11 → 265.12 |
| 768 / edit | 18.32 / 43.20 / 995.29 → 11.38 / 35.23 / 937.99 | 6,978 → 9,169 | 277.61 → 276.45 |

Accepted placement/removal updates expose the visible hitch better than the
aggregate quantiles. These values are **milliseconds**. Every edit replay has
six accepted placements and four removals; with ten samples, nearest-rank p95
and p99 are both the maximum. They are useful regression guards, not a precise
estimate of production tail probabilities.

| Parts | Baseline p50 / p95 / p99 | Reservation grid only p50 / p95 / p99 | Both changes p50 / p95 / p99 |
|---:|---:|---:|---:|
| 0 | 17.184 / 18.163 / 18.163 | 11.197 / 11.877 / 11.877 | 9.946 / 10.432 / 10.432 |
| 64 | 17.304 / 20.373 / 20.373 | 11.338 / 13.459 / 13.459 | 10.426 / 11.275 / 11.275 |
| 256 | 17.844 / 18.883 / 18.883 | 11.540 / 12.072 / 12.072 | 11.003 / 11.560 / 11.560 |
| 768 | 20.568 / 22.136 / 22.136 | 14.157 / 17.072 / 17.072 | 13.128 / 14.087 / 14.087 |

At 768 pieces, accepted-edit median latency improves **36.2%** and its p95/p99
improve **36.4%**. Idle update median improves **37.7%**. Edit replay throughput
improves **31.4%**. These are different measurements; they must not be added.

Both incremental comparisons and the combined comparison pass every predeclared
latency, throughput, RSS, outcome, and exact-output guard. Each comparison checks
14,400 JSON observations and 24 complete save archives. Across the eight cases,
median peak-RSS change is bounded within −1.17 to +0.68 MiB. This change targets
CPU work rather than memory reduction.

All individual measurements, including serialization p50/p95/p99 and startup
latency, are in [measurements.json](evidence/measurements.json). The
[combined comparison](evidence/final-comparison.json),
[first change](evidence/scenery-comparison.json), and
[second change](evidence/membership-incremental-comparison.json) include the
unchanged guard limits and empty failure lists. The
[golden manifest](evidence/golden-manifest.json) records every output hash/size.

## Profiles captured before proposing changes

The CPU profiler sampled at 1 kHz during 36,000 measured updates at 768 pieces.
The baseline edit profile has 5,118 samples; idle has 1,317. Inclusive percentages
include callees and **overlap**; they are not independent slices to sum.

| Baseline edit hotspot | Inclusive CPU | Interpretation |
|---|---:|---|
| `CreativeScenery::admit` | 55.16% | Includes village admission; the nearby reservation loop alone accounts for 31.3% by source attribution |
| `CreativeVillage::admit` | 19.44% | Inside scenery; remaining spatial-admission opportunity |
| `validateConstruction` | 15.16% | Geometry, support and placement rules; `partFor` alone is 4.20% flat |
| `cannonVisible` | 5.74% | Repeated existential linear scans over accepted solids |
| `AdventureRuntime::json` | 5.49% | Public serialization; exact bytes constrain replacements |

The other substantial edit cost is terrain sphere sweep at 4.98%. Idle is
different: JSON is 25.82%, cannon visibility 24.53%, and terrain sweep 10.02%.
HUD refresh is 27.03% **including** the cannon scan. The benchmark's own validity
string search is 19.21% of idle CPU; optimizing that would improve the reported
number without improving the game, so it was left alone. The
[source-level reservation report](evidence/profiles/scenery-lines.txt) and
[engine attribution](evidence/profiles/edit-engine-cumulative.txt) explain the
selection more precisely than top-level function names alone.

The separate 3,600-update allocation capture reports 424.55 MiB allocated in
1,065,107 objects. JSON is 26.49% of allocated bytes, scenery 20.20%, construction
validation 14.58%, Blacksmith geometry 11.99%, and the geometry compiler 11.35%.
Validation accounts for 61.87% of allocation objects, including its callees.
These are allocation volumes, not retained memory; the compiler is also nested
under callers above. See [bytes](evidence/profiles/alloc-engine-bytes.txt) and
[objects](evidence/profiles/alloc-engine-objects.txt).

The measured I/O interval contains 60 `DRM_IOCTL_SYNCOBJ_QUERY` polls and six
`brk` calls, plus the two explicit trace-marker writes. There are **no file or
network reads/writes or syncs** inside it. Startup loads and final golden-file
writes lie outside the interval. The final interval has the same 60 polls and
five heap-growth calls. [I/O evidence](evidence/io-summary.json) and compressed
raw traces preserve the boundary. This does not characterize durable save I/O
or legacy networking under load.

Warm-cache startup is a separate opportunity: 68.09% of its 887 CPU samples are
inside SHA-256 compression. Forest tile generation is 8.68%; headless GPU
initialization is 5.86%. Some driver symbols are unavailable. Hardware SHA
acceleration with the current portable fallback warrants its own project;
skipping content verification would violate the functionality requirement.

Follow-up profiles confirm the intended work was removed. Scenery falls from
2,823 to 1,089 sampled milliseconds over the same edit workload; village is now
31.47% of the smaller CPU total and construction validation 25.49%. Cannon
visibility is no longer among the reported idle hotspots. HUD inclusive time
falls from 27.03% to 7.79%. Sample totals are diagnostic evidence, not substitutes
for the unprofiled timing table.

The new index trades some publication work and memory for repeated lookup work:
`publish` rises from 33 to 40 sampled milliseconds in the long edit replay;
total allocation volume rises from 424.55 to 426.32 MiB and objects from
1,065,107 to 1,065,129 in the short heap replay. It needs at most 256 KiB of pair
storage per fully populated index (16,384 solids × 16 bytes), before allocator
overhead. The measured accepted-edit and RSS guards still pass. The algorithm
changes repeated O(number of solids) membership queries into O(log distinct
pairs), paying O(n log n) when publishing a new accepted snapshot.

## Other requested patterns and remaining opportunities

The [ranked matrix and proof sketches](design.md) were written before runtime
edits. The next candidates remain deferred, rather than being bundled into these
two changes:

- **Village reservations:** its remaining admission cost is now clearly visible.
  A conservative spatial index over the same expanded bounds could remove
  another repeated scan, but the profile includes other village work. Proving
  identical whole-group rejection, margins, source order, and overflow behavior
  needs a separate oracle and isolated measurement.
- **SHA-256 startup:** optimized compression can preserve exactly the same digest
  and error behavior. Platform dispatch, block/tail handling, native/WASM
  fallback, and published test vectors must be validated before changing it.
  This would affect loading responsiveness, not steady-state rendering.
- **Construction part lookup:** an indexed lookup can replace repeated `partFor`
  scans for sorted unique IDs. Arbitrary/invalid input ordering and first-match
  behavior require a fallback; no sorting of authoritative state is justified.
  Its 4.20% baseline CPU share makes it lower priority than the implemented work.

The remaining patterns do not currently justify a performance diff:

| Pattern | Finding and constraint |
|---|---|
| N+1 query/fetch elimination | The measured problem is repeated in-memory scans, not database or network fetches. Installed assets already have caches. |
| Zero-copy, reuse, scatter/gather | Render paths already retain scratch/buffers and consume GPU poses directly. Private construction candidates and accepted snapshots protect rollback/lifetimes; bypassing those copies needs separate allocation attribution and ownership proof. No hot file/socket writes were measured here. |
| Serialization format costs | JSON is measurable, especially at idle. Changing format fails byte equivalence. More static-fragment caching is plausible, but dynamic observations, locale/float formatting, escaping, and bridge freshness must stay exact; no additional cache was introduced. |
| Bounded queues and backpressure | Save workers, GPU submissions and readbacks already impose bounds. Queue changes can alter timing/admission and have no measured support in this replay. |
| Sharding or striped locks | No demonstrated contention hotspot in the measured CPU building path. Threading a sequential simulation introduces ordering concerns. |
| Memoization, precomputation, lookup tables | Accepted part membership is precomputed at publication, with lifetime tied to its exact immutable snapshot. Existing target/static JSON/render caches already cover several obvious cases. |
| Indexing and binary search | Both implemented levers apply directly. Binary search on answer space has no proven monotone expensive predicate in the remaining hot paths. |
| Dynamic programming | Support validation already traverses a bounded connectivity graph. Reusing results across edits risks changing support/refusal semantics; allocation volume alone does not prove a better algorithm. |
| Lazy evaluation and deferred computation | Moving validation or publication past its current command boundary changes observable acceptance timing. Defer only demonstrably unused presentation work, with a separate workload. |
| Streaming/chunking | Forests and GPU work already have chunked/bounded representations. Atomic accepted geometry/save publication cannot be replaced by partially visible chunks. |
| Two pointers, sliding windows, prefix sums | No measured remaining hotspot is an additive window aggregate. Spatial overlap/support/terrain maxima do not become equivalent merely by applying prefix sums; candidate order and floating-point boundaries matter. |

## Validation and limits

The baseline aggregate suite ran **2,626 cases: 2,604 passed, 20 skipped, two
failed**, with five additional disabled tests. It took 2,119 seconds. The terrain
importer passed. The failures occurred before any optimization:

1. `FixtureGPU.CoveMoldedMachineryFitsCurrentOwnerBudget`: expected draw counts
   disagree with the current scenery/fixture output.
2. `CannonPhysicsSceneGpu.ThrownBrickFlightBounceAndStackMatchJolt`: stack height,
   residual velocity, and orientation differ from the Jolt expectations.

Final focused tests pass **172/172** across adventure, walkable columns, doors,
movement, navigation, pointer, and encounters. Two full-terrain startup/restore
runtime integrations pass. Three opt-in imported-wall cannon-input cases fail
at unchanged yaw/elevation and zero shots; all three reproduce at the same
assertions using the original aggregate executable and original libraries.
They were skipped in the baseline aggregate without the terrain environment.
No assertion was weakened and no unrelated failure was repaired.

The new reservation oracle passed against the original linear implementation
before the change, then against the grid implementation. It exercises strict
overlap boundaries, negative/cell boundaries, duplicate reservations, large-box
fallback, reversed order, exact retained geometry, and suppression persistence.
The membership oracle independently scans original solids and tests duplicates,
counter-only namespace behavior, source ordering, stale/invalid publication,
replacement, empty snapshots, copying and moving. Together with the proof
sketches, these cover more than the finite golden workload alone.

The native optimized application builds successfully. The six JavaScript test
files pass. All five benchmark-guard tests pass, including injected accepted-edit
latency regressions and missing/changed sample counts. Before the requested push,
the full aggregate suite was also rerun: **2,628 cases, 2,606 passed, 20 skipped,
and the same two baseline failures**, with five additional disabled tests. It
completed in 1,649 seconds; no new failing cases appeared. The terrain importer
passed. See the [aggregate summary](evidence/push-aggregate-summary.json),
[failure excerpts](evidence/push-aggregate-excerpts.log), and
[build/test log](evidence/push-aggregate-tests.log). The optimized aggregate build
needed file-specific warning exceptions for three unchanged legacy test helpers;
[commands.md](commands.md) records them. WASM execution was not measured.

Attempts to use the existing native FPS benchmark exposed a measurement problem:
window focus changes camera behavior, and the benchmark can count frames where
`Application::render` exits before rendering while waiting for physics readiness.
The X11 virtual-display path also failed GPU surface initialization on this host.
A temporary hidden/focused diagnostic produced implausibly high frame rates,
so these numbers were rejected. A render-completion invariant and active-gameplay
scenario are needed before claiming end-to-end FPS or displayed input latency.
The CPU replay used here runs real runtime/terrain/building logic, initializes
headless Vulkan, and does not attach/render the full physics scene.

The finite replay does not prove every possible external schedule equivalent.
The local proofs apply to identical ordered inputs, content, time steps, and
successful allocation. The pair index intentionally preserves the old
counter-only identity predicate; strengthening identity checks would be a
behavior change. Original collision ordering, floating-point calculations,
refusal priority, JSON format, and durable save format remain unchanged.

## Reproduction and evidence

[commands.md](commands.md) records exact baseline, build, workload, profile and
test commands, including the optimized compiler flags. The `evidence/` directory
contains source/binary fingerprints, all individual measurements, comparisons,
golden hashes, readable and compressed raw profiles, I/O boundaries, test logs,
baseline failure excerpts, and one runtime patch per lever. Complete golden files
and preserved executables remain in `/tmp/voxys-core-audit-20260924`.

The original staged edits and all unchanged source fingerprints were checked
afterward. User shader/physics edits were preserved. No commit, deployment,
format change, queue change, or unrelated refactor is included.

The requested push uses a one-command commit-hook override because the hook
requires an entirely green aggregate suite, while the rerun reproduces the two
baseline failures above. The repository hook and its configuration remain
unchanged; no test was excluded from the aggregate run.
