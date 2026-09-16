# Free-building performance investigation — 2026-09-16

## Status and contract

Baseline source: `7ea4d7628a6d2245a8563060e5519d19c8926a9d`.
Measurements were prepared while its Pages release was building. Verify this exact
source is live before starting the owner's 90-minute optimization window.
Initial release verified live at 2026-09-16 14:56:33 UTC: public HTML serves
`7ea4d7628a6d2245a8563060e5519d19c8926a9d`; the public browser reaches the
creative hotbar. Pages run `35110094212` passed build, full startup and deploy.
**Optimization window: 14:56:33–16:26:33 UTC (10:56:33–12:26:33 Toronto).**

Preserve game behaviour and saves. Collect baseline, CPU/allocation/I/O profiles,
explicit golden outputs, an opportunity ranking and equivalence proof before
editing runtime code. One performance lever per change. Commit through the normal
full hook, push main and verify the final public release. Stop adding optimizations
at the recorded deadline; finish required validation and publication.

## Baseline

The actual release commit hook passed 2,457 tests; eight opt-in tests were skipped
and four remain disabled by the existing suite. The terrain importer passed.
The main aggregate took 1099.8 seconds. Exact source/binary/terrain hashes, build
flags and commands are in `baseline-context.json`.

This is the real native optimized creative runtime on the installed 8192² terrain,
with isolated saves and public inputs. It is a **CPU component benchmark**, not
rendered browser FPS or input-to-photon latency. No local build/test suite ran
concurrently with formal measurements. Hardware: AMD Ryzen AI 9 HX 370,
Radeon 890M; Linux x86-64. Peak RSS includes terrain, native GPU initialization
and retained golden strings. The benchmark reports their byte count separately.

Three repetitions each, 120 warmup updates, 3,600 measured updates at fixed 1/60 s,
UI serialization every sixth update. `idle` holds the same aim and includes a
refused placement preview. `edit` moves aim, changes piece/rotation/paint/height,
places and removes pieces, pauses/resumes, cycles the hotbar and walks. For the
768-piece edit fixture, each baseline repetition accepts six placements and
six removals, with 285 valid and 315 invalid observations.

Median of three runs; update latency in microseconds:

| Existing pieces | Replay | p50 μs | p95 μs | p99 μs | Updates/s | Peak RSS MiB |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | idle | 9.40 | 9.48 | 11.95 | 82893 | 218.5 |
| 0 | edit | 18.95 | 24.89 | 43.06 | 48347 | 218.2 |
| 64 | idle | 22.49 | 24.00 | 27.68 | 32212 | 217.9 |
| 64 | edit | 35.12 | 43.84 | 79.94 | 24581 | 218.2 |
| 256 | idle | 73.38 | 85.98 | 156.63 | 9880 | 223.4 |
| 256 | edit | 105.08 | 133.73 | 243.31 | 8611 | 223.4 |
| 768 | idle | 257.27 | 291.60 | 547.04 | 3016 | 243.0 |
| 768 | edit | 427.98 | 491.03 | 957.43 | 2456 | 243.4 |

Full per-run update/serialization percentiles, throughput, memory and outcome
counts are in `baseline-metrics.json`. Reproduction:

```sh
nix-shell --run 'bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 //tools:free_build_benchmark'
# Preserve this executable before modifying runtime code (a new destination).
cp bazel-bin/tools/free_build_benchmark /tmp/free-build-baseline-runner
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/free-build-baseline-runner /tmp/free-build-baseline'
```

The warning option keeps an existing GCC 15 optimized-build warning in legacy
`AdventureEncounters::safeRest` visible; it does not change code generation.
Both baseline and candidate must use these exact flags.

## Profiles captured before proposals

```sh
nix-shell --run 'bash scripts/performance/profile_free_build_benchmark.sh /tmp/voxys-perf-20260916/release-baseline-runner /tmp/voxys-perf-20260916/profiles /nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2 /nix/store/6gdpgjrgrg592sgcbzklq2r89mbwk4yl-pprof-0-unstable-2026-03-02/bin/pprof'
```

CPU profiles use gperftools at 1,000 Hz, 768 pieces and 36,000 updates per replay.
Allocation attribution uses a **separate** tcmalloc run, 768/edit/3,600 updates.
I/O uses a separate `strace -f -tt -T` run. Profiling starts after startup/warmup;
never compare instrumented timings with benchmark timings. The longer CPU replay
is for attribution; performance comparisons use the fixed 3,600-update matrix.

Top measured edit replay call paths (inclusive percentages overlap):

| Call path | Sampled CPU |
| --- | ---: |
| `AdventureRuntime::updateTarget` | 82.12% |
| `validateConstruction` within that work | 73.09% |
| `AdventureRuntime::json` | 14.58% |
| `partFor` linear lookup inside validation | 14.36% |
| `terrainRange` inside validation | 8.34% |

`partFor` has 14.35% flat time and validation's own loops have 12.71% flat
time. The idle profile similarly assigns 67.42% inclusive time to construction
validation and 16.11% flat time to part lookup. Full top/cumulative tables are
stored alongside this report; nested percentages must not be added together.

Allocation replay: 1,495.81 MiB allocated cumulatively across 13,803,509 allocation
calls (not peak memory). Placement-preview preparation accounts for 91.24% of
allocated bytes; construction validation 71.06%; JSON serialization 7.17%.
These are nested call-stack attributions, not disjoint categories.

Steady replay I/O: **zero file or network calls** between the markers. The trace
contains 60 GPU fence-query ioctls (0.837 ms summed syscall duration) and 196 heap
extensions (2.417 ms). Initialization reads and later golden-file writes are
outside the measured interval. There is no measured steady-state I/O bottleneck
in this workload. See `io-summary.json` and its trace hash.

## Equivalence oracle and guardrails

Each replay records the entire UI JSON string at the normal 10 Hz polling rate
and the accepted final save archive. No observable field is rounded or normalized.
All three baseline repetitions are byte-identical per workload; hashes are in
`baseline-golden-hashes.json`. Candidate results must match those complete bytes
and placement/removal/validity counts. Save decoding must recover the exact accepted
state; creative inventory must be unchanged and combat must remain stopped.
The preflight used an older executable and is not the formal baseline.

Run the same matrix for a preserved candidate executable, then:

```sh
python3 scripts/performance/compare_free_build_benchmark.py /tmp/free-build-baseline /tmp/free-build-candidate
```

The comparator requires the complete 24-run matrix on both sides. It rejects
median p50/p95/p99 regressions above 10/15/20%, with a fixed 25 μs noise allowance;
RSS growth above the larger of 10% or 10 MiB; and throughput loss above 15%.
Exact output and outcome mismatches always fail, regardless of speed.

## Opportunity matrix — before implementation

Scores use 1–5 for impact, confidence and effort. Confidence is a source-based
assessment of equivalence, not a promised speedup. Score = impact × confidence / effort.

| Candidate | Evidence / scope | Impact | Confidence | Effort | Score | Decision |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Reuse unchanged creative single-piece preview validation results | 82.12% preview CPU, 91.24% allocation bytes; pure speculative preparation repeats for equal state/request | 5 | 4 | 2 | 10 | Investigate first; explicit revision/request key, restore invalidation and fresh actual-placement validation required |
| Index part IDs during validation | 14.36% inclusive lookup CPU, repeated full scans | 3 | 4 | 3 | 4 | Reassess after preview reuse; indexing allocation/sorting costs may outweigh remaining benefit |
| Reuse unchanged serialized building section | JSON 14.58% CPU, repeated numeric formatting of unchanged parts | 3 | 4 | 3 | 4 | Reassess with post-change profile; preserve exact formatting and restore/locale semantics |
| Reuse support-graph/geometry scratch | Validation allocation is large | 4 | 3 | 4 | 3 | Defer unless still hot; more state-lifetime and failure-path complexity |
| I/O batching, streaming or queues | No replay file/network calls | 1 | 4 | 4 | 1 | Not supported by this measured workload |

The matrix above was recorded before implementation. Chosen changes, proof
sketches and measured outcomes follow.

## Change 1 — bounded creative preview result reuse

Proof recorded before implementation: the single-piece speculative `preparePlace`
is const and its candidate is discarded by `updateTarget`. Its only retained
outputs are success and the exact reason string. Within an initialized runtime,
the accepted state changes only through revision-incrementing commands/player
updates. Installed content and terrain are fixed; published geometry has its own
revision. A key containing world, epoch, state revision, request sequence, geometry
revision and every `PlacePart` field therefore identifies the same pure validation
inputs. Reusing only those two outputs for an equal key is equivalent to recomputing.
A key miss executes the original preparation. Initialization and successful restore
explicitly discard the entry because restore can replace contents with identical
revision numbers. Other modes/blueprints remain uncached. Actual placement always
prepares and validates a fresh command; no prepared candidate is retained. Storage
is bounded to one key, bool and reason string. No rendering, input, save format,
validation ordering or accepted-state arithmetic changes.

### Change 1 measured outcome

All 24 runs match the baseline's **entire UI observations and save archive bytes**;
all fixed latency/throughput/memory guards pass (`preview-comparison.json`).
At 768 pieces/edit, update p50/p95/p99 changes from 427.98/491.03/957.43 μs to
22.11/29.11/598.84 μs; total replay throughput rises from 2,456 to 9,414 updates/s
(**3.83×**). At 768/idle, p50 falls from 257.27 to 12.53 μs. Empty-scene timings
remain similar because this replay's empty-ground path already returns early.
These are CPU component results, not rendered frame-rate claims.

Separate post-change profiles attribute 56.24% inclusive CPU to `json`,
30.44% to `updateTarget`, and 18.32% to construction validation (nested).
Float formatting alone has 43.34% inclusive CPU. Cumulative allocation falls
from 1,495.81 MiB / 13,803,509 calls to **202.78 MiB / 945,590 calls**.
The largest remaining measured opportunity is unchanged structure serialization.
Per-run data, profile tables and binary/harness hashes accompany this report.

A real-terrain integration regression restores two checkpoints with identical
world/revision/request sequence/aim but different allocator capacity. It must
replace a previously valid preview with the exact identity-capacity refusal,
refuse actual placement without changing state, then recover after restoring
the original checkpoint. **Passed on Radeon/Vulkan with the full installed
terrain.** Removing only the restore cache reset makes the regression fail with
a stale “Ready to place” result, confirming that the test exercises the boundary.
The tested reset was restored before committing.

Focused command:
```sh
nix-shell --run 'bazel test -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 //tests:adventure_runtime --test_arg=--gtest_filter=FreeBuildRuntimeIntegration.* --test_env=VOXY_ADVENTURE_TEST_TERRAIN=/home/modkin/workspace/schneiderlo/voxys/data/generated/td_seed_1234_8192.r16 --test_env=VOXY_ADVENTURE_TEST_WORKSPACE=/home/modkin/workspace/schneiderlo/voxys --test_output=errors'
python3 scripts/performance/test_compare_free_build_benchmark.py
```
The guard self-tests inject missing runs, altered JSON/save bytes, update and
serialization latency regressions, memory growth and throughput loss; all are
rejected. The identical complete fixture passes.

### Change 1 commit gate

Committed as `5381b18a2cb0f7e387168f6cd3295ab715c7ad61` after the actual
normal hook: 2,457 tests passed, eight opt-in cases skipped, four existing disabled
cases; importer passed. Aggregate suite 1,158.675 s. The opt-in creative test was
also run explicitly on the installed terrain and passed before this full gate.

## Change 2 — unchanged structure JSON

See `serialization-design.md` for the profile-based ranking and pre-implementation
proof, including restore invalidation and locale/rounding boundaries. The classic
number formatter now formats an unchanged structure only once. Other fields and
polling are unchanged. Full 24-run goldens and all latency/memory/throughput guards
pass against **both** the original baseline and Change 1.

At 768/edit, JSON p50/p95/p99 goes from the original 351.19/387.67/748.17 μs to
23.58/31.43/355.31 μs. Throughput is **20,166 updates/s**, 8.21× the original
2,456 and 2.14× Change 1's 9,414. At 768/idle, throughput is 44,056 updates/s.
Rare edits still rebuild the fragment, so tail cost is higher than cache hits.
Cumulative allocation is 205.03 MiB/945,707 calls (slightly above Change 1 due
to fragment rebuilding); this change targets formatting CPU, not allocations.
Peak RSS remains within the original fixed guards.

Post-change CPU profile: JSON 9.67%, preview targeting 63.71%, construction
validation 39.50%, terrain sphere sweep 26.67%, and `partFor` 9.35% inclusive
(9.30% flat); nested percentages overlap. The lookup now warrants considering
a certified sorted-range search with fallback, rather than assuming sorted input.
The new real-terrain restore/locale/directed-rounding regressions pass.

Deliberately removing the serialization restore reset and formatting guards makes
all three boundary checks fail (stale IDs, ignored numeric locale, ignored directed
rounding). The previously passing implementation was restored byte-for-byte.
