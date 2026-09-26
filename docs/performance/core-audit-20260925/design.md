# Decision record before runtime edits

The original optimized executable is preserved. Its full 24-case preliminary
replay, CPU/heap/I/O profiles and independent village oracle have completed.
The original aggregate suite continues in its separate unchanged output tree.
Preliminary timings and profiles overlap that suite: profiles establish work
attribution, but preliminary wall times are excluded from speed claims. Final
matched timing will rerun the preserved executables after audit tests/builds.

## Fresh evidence

The 36,000-update, 768-part edit CPU capture contains 3,370 samples at 1 kHz.
Village admission is 31.39% inclusive; its reservation loop alone is 969 samples
(28.75%). Construction validation is 25.25% inclusive, including 4.96% in
`partFor`. Terrain sphere sweep is 7.89%; JSON is 6.47%. These are overlapping
inclusive attributions, not additive categories. Idle has 900 samples: JSON
22.00%, terrain sweep 15.22%, HUD 10.33%, target update 6.56%, input routing 5.44%.
The benchmark's own string search is 28.78% of idle CPU and is excluded as an
engine optimization target.

The separate heap capture allocates 426.32 MiB across 1,065,129 objects. JSON
accounts for 26.38% of bytes, scenery 20.11%, validation 14.52%; validation
accounts for 61.87% of objects including callees. This is cumulative allocation,
not retained heap or RSS. The replay I/O interval contains 60 GPU query ioctls
and six brk calls, with no file/network operations apart from trace markers.
Startup has 906 CPU samples: SHA-256 compression is 70.86% inclusive, forest
generation 8.28%, headless GPU setup 5.74%. Some driver symbols are unavailable.

## Ranked opportunities

Impact is sampled CPU percentage in the explicitly named workload, an upper
bound rather than a promised reduction. Confidence is engineering confidence in
preserving the contract, not a measured probability. Effort is relative work
including verification (1 = a small local lever). Score = impact × confidence /
effort. Startup, idle and edit scopes are deliberately not added together.

| Rank | Candidate | Impact ceiling | Confidence | Effort | Score | Decision |
|---|---|---:|---:|---:|---:|---|
| 1 | Village reservation sorted intervals plus prefix maxima | 28.75% edit CPU | .99 | 2 | 14.23 | Implement and measure independently |
| 2 | Accelerated SHA-256 with unchanged portable fallback | 70.86% startup CPU | .85 | 8 | 7.53 | Separate startup project; preserve all digest checks |
| 3 | Certified sorted part lookup during construction validation | 4.96% edit CPU | .99 | 1 | 4.91 | Implement second, measure incremental effect |
| 4 | More exact JSON fragment caching | 22.00% idle CPU | .80 | 5 | 3.52 | Defer; dynamic fields/format/cache lifetime need separate oracle |
| 5 | Terrain sweep broad-phase refinement | 7.89% edit CPU | .65 | 6 | .85 | Defer; touching/ties/floating-point iteration must stay exact |

Repeated geometry compilation and tree-map allocation also deserve targeted
follow-up. The current profiles do not isolate an independently removable time
share sufficient to justify changing their ownership/validation boundaries.
GPU bind-group caches, serial event packing and legacy multiplayer indexing need
their own real workload profiles; they are not ranked as measured CPU replay wins.

## Change 1 proof: village reservation index

Build a local index of references to the original immutable reservations. For
each finite expanded X interval, retain `minimum.x - .5` and `maximum.x + .5`;
sort by the former and compute prefix maxima of the latter. For query bounds
`[lo, hi]`, an original match necessarily satisfies lower < hi and upper > lo.
Binary searches eliminate entries whose lower is >= hi and the initial prefix
whose maximum upper is <= lo. Therefore no original match is excluded, even
for finite inverted intervals. Test every remaining reference with the original
strict three-dimensional overlap expression: false positives from the broad
phase cannot affect results. Nonfinite expanded X values and nonfinite query
bounds use linear fallback with that same predicate.

Reservation traversal order and duplicates cannot alter an existential Boolean
with no side effects. Do not change original reservations, group generation or
ordering, aliases, capacity accounting, player clearance, or persistent
suppression. The index is rebuilt for each admission call and destroyed before
the referenced span dies: no cross-frame invalidation or pointer ownership
change. Complexity becomes O(n log n + g log n + visited candidates), with O(n)
temporary storage; very wide intervals can still produce linear candidate scans.

## Change 2 proof: certified part lookup

Within one validation call, certify whether each structure's part sequence is
nondecreasing by ID. Search a certified sequence with lower_bound, and search
an uncertified sequence linearly. Visit structures in their original order.
Both paths return precisely the first matching element of the original nested
linear scan, including duplicate IDs, invalid/unvalidated before-state, missing
IDs and arbitrary structure order. Do not sort or mutate authoritative state,
reject previously tolerated inputs, or reorder validation/error checks.

Keep the borrowed state immutable for the lookup's lifetime and use it only
within that validation call. Certification is O(n); repeated sorted lookups
become O(s log n) for s structures. Retain linear behavior for unsorted data.
If certification is shared across all structures, one unsorted vector may
conservatively force all searches to use the original linear path.

## Oracle and guard changes

Village oracle tests passed on the original implementation before edits. The
full byte-exact JSON/save matrix and accepted-edit/update/serialization/RSS/
throughput thresholds remain as declared in plan.md. Add construction tests for
arbitrary order and duplicate-first semantics before its runtime lever.

The comparator separately rejects nonfinite/nonpositive measurements, invalid
types, decreasing quantiles and invalid counts before median aggregation.
Valid measurements follow the original comparisons unchanged; malformed data
now fails explicitly. This tooling change cannot affect game output. All nine
fault-injection tests pass, and no threshold has been relaxed.
