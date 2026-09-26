# Follow-up audit contract

The complete root README and working-tree AGENTS.md were read first. AGENTS.md
is empty. The starting revision and dirty source fingerprints are recorded in
the evidence directory; existing shader, physics and asset changes are preserved.

This follows the 24 September audit. Its implemented reservation, membership,
mesh and event-extraction improvements are baseline functionality, not new gains.

## Measurement sequence

1. Run the aggregate native suite and terrain importer, plus UI/startup and
   benchmark-comparator checks. Preserve failures, skips and exact commands.
2. Build an optimized, symbolized free-build benchmark and preserve its binary
   before runtime edits. Run the existing complete 24-case matrix: 0/64/256/768
   parts, idle/edit, three repetitions, 120 warmup and 3,600 measured updates.
3. Capture independent CPU, cumulative-allocation and I/O profiles. Start/stop
   markers separate replay from initialization and golden-file output. Capture
   startup CPU separately. No profiled timings enter speed comparisons.
4. Record the top measured hotspots and rank opportunities by impact times
   confidence divided by effort before changing runtime code.
5. Measure each selected lever independently. Preserve identical compiler flags,
   content, inputs and time steps, and run relevant correctness tests.

Clean timings run after audit builds/tests finish, with ordinary desktop activity
recorded. The replay executes actual terrain, runtime, input, validation and
publication code, but does not render the full application. Its throughput is
CPU updates/second, not displayed FPS or input-to-photon latency.

## Equivalence oracle and proof obligations

Every matched replay must produce byte-identical complete JSON observations and
save archives. Accepted placements/removals, valid/invalid observations, workload
parameters and terrain identity must match. Save decoding must recover the exact
accepted state; creative inventory and dormant combat must remain unchanged.

Independent village tests are added before runtime edits. They compare against
the original strict linear overlap predicate, checking full ordered groups,
pieces, collision boxes and IDs, exact .5 margins, adjacent floating-point
values, duplicate/reordered reservations, exceptional bounds, namespace aliases,
capacity and persistent suppression. They must pass on the original runtime.

A proof for each selected change must establish the same predicate for all
supported inputs, preserve failure/refusal priority and candidate order, and
explain lifetime/invalidation and exceptional-input behavior. A finite golden
replay is evidence, not a universal proof. Equivalence assumes identical ordered
inputs and successful allocation, not identical wall clocks or external schedules.

## Predeclared guardrails

Use the existing same-host comparator without relaxing its limits: median-of-three
p50/p95/p99 regression limits of 10%/15%/20%, with a 25 microsecond noise allowance;
throughput loss at most 15%; RSS increase at most the larger of 10% or 10 MiB.
Require all 24 cases and exact outputs. Accepted-edit latency is guarded separately
because ten committed edits in 3,600 updates fall below the aggregate p99 cutoff.

Retain comparator fault-injection tests and add narrowly scoped checks only where
needed. Runtime changes must have separate patches and proof sketches. No quality,
physics cadence, draw-distance, save format, or asynchronous-admission changes
are implied by this CPU investigation.
