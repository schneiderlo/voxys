# Decision record, before runtime edits

The baseline suite and 24 replays have finished. CPU, allocation, and I/O
profiles were captured from the preserved unmodified runtime before this ranking.
The edit CPU profile has 5,118 one-millisecond samples; idle has 1,317.

## Opportunity matrix

Impact is an upper bound in percentage points of sampled CPU time in the named
workload, not a promised speedup or rendered FPS. Confidence is the probability
that the stated approach can preserve the existing observable contract. Effort
is a relative implementation-and-validation unit (1 = a small local change).
The score is impact × confidence / effort. Different workload scopes are named
explicitly rather than adding their percentages together.

| Rank | Candidate | Measured impact ceiling | Confidence | Effort | Score | Decision |
|---|---|---:|---:|---:|---:|---|
| 1 | Reuse the already-built reservation grid for nearby scenery | 31.3% edit CPU, source line 328 | .99 | 1 | 31.0 | Implement and measure alone |
| 2 | Index accepted `(structure counter, part counter)` membership at geometry publication | 24.53% idle CPU in cannon visibility; 5.74% edit CPU | .99 | 2 | 12.1 | Implement second, measure incrementally |
| 3 | Hardware-accelerated SHA-256 compression with portable fallback | 68.09% startup CPU | .85 | 6 | 9.6 | Defer platform dispatch/dependency work; retain full integrity verification |
| 4 | Spatially index village reservation checks | At most 19.44% edit CPU (entire village admission) | .85 | 4 | 4.1 | Defer; ceiling includes work other than reservations |
| 5 | Binary part lookup inside construction validation, with an unsorted-input fallback | 4.20% edit CPU in `partFor` | .98 | 2 | 2.1 | Defer after the larger measured costs |
| 6 | Remove initialization of candidate-array entries never read | At most 1.44% idle CPU (all `memset`) | .95 | 1 | 1.4 | Too little demonstrated impact |

HUD construction and serialization are also visible. HUD's 27.03% idle inclusive
time contains the cannon scan, so treating both as independent opportunities
would double-count most of that cost. Serialization is 25.82% idle inclusive;
the replay's own validity-string search is another 19.21%, outside the game.
Changing the public serialization format would fail the exact-byte contract.

Construction validation accounts for 61.87% of allocation objects, including its
called geometry compiler; JSON accounts for 26.49% of allocated bytes. These are
allocation-volume percentages, not wall-time percentages or retained memory.
The measured edit interval performs no file/network reads, writes, or syncs
apart from its two trace markers; GPU completion polls and heap growth remain.
There is no measured basis for queue resizing, lock striping, or asynchronous
I/O changes in this workload.

## Equivalence proof obligations

### Change 1: indexed reservation rejection

`Reservations` is already built from the same immutable `reserved` vector and
already queried for every distant tree. Its buckets cover each reservation's
X/Z interval expanded by exactly `.8`, using the same double expressions as the
old overlap predicate. Any strictly overlapping finite prop and expanded
reservation share an inclusive 64-unit bucket, because floor is monotone.
Reservations spanning more than 256 buckets are checked in the existing fallback
list. Every returned candidate is checked with the unchanged three-dimensional
strict overlap predicate; bucket over-inclusion cannot cause a false positive.
Thus `blocks(p)` is precisely the old existential test for supported geometry.
Duplicates and traversal order cannot change a Boolean OR. Neither the reserved
vector nor candidate ordering, capacity decisions, suppression, or floating-point
geometry construction changes. No new index or allocation is introduced.

### Change 2: accepted part-counter membership

At successful publication, construct a sorted unique set of exactly the pairs
`(solid.structure.counter, solid.part.counter)`. Membership in this set is
equivalent to the old `any_of` predicate, including repeated boxes and different
world namespaces with equal counters. The counter-only comparison is deliberate:
adding world filtering would change the old predicate. Keep original solids and
sector ordering untouched. Construct all replacement containers before assigning
any accepted state; failed or stale publication leaves both solids and index
unchanged. Default copy/move of a published snapshot carries its index with it.
Visibility retains the independent optional-position check. There is no new
floating-point arithmetic, epoch invalidation rule, or cross-frame lazy mutation.

The proofs concern identical ordered inputs, fixed time steps, installed content,
and successful resource allocation. Performance measurements and asynchronous
external scheduling are not asserted to be bit-identical game outputs.

## Oracle and guard additions

Keep all 600 JSON observations and the complete final save per replay byte exact,
plus command counts, save decode equality, unchanged creative inventory, and no
combat advancement. Add independent linear-predicate tests at spatial cell and
strict-margin boundaries, and tests of index membership through replacement,
invalid/stale publication, duplicate IDs, namespaces, copying, and moving.

Before changing runtime code, add a benchmark distribution for updates that
actually accept a placement/removal. The existing 10 accepted edits in 3,600
frames lie below the aggregate p99 cutoff: reporting only aggregate quantiles
would hide exactly the expensive commits under investigation. This additional
timer consumes the already-recorded update duration and never changes inputs.
Rebuild the baseline harness and rerun it before using that new metric.

Extend the existing comparator to guard this distribution with the same
p50/p95/p99 limits (10%/15%/20%, minimum allowance 25 microseconds), require equal
nonzero edit sample counts, and retain exact-output, throughput, and RSS guards.
Measure each runtime lever separately. Do not infer displayed FPS from this
headless CPU replay. Native benchmark attempts are diagnostic only: focus changes
camera behavior and the current runner can count deferred, unrendered frames.
