# Exact dependency-level bound: native measurement

The serial solver now computes the exact maximum populated dependency level during its existing deterministic level build. One workgroup scalar is read with `workgroupUniformLoad` and passed to every stage. Contact ordering, barriers between populated levels, and the fallback outside 128–512 contacts are unchanged. At 16 substeps, avoiding an empty trailing level removes 68 barrier calls per workgroup/tick. This is a structural bound, not a measured speedup claim.

The synthetic 39-part source wall uses 16 substeps; the simple catalog fixtures remain at four. Optional `VOXY_IMPORTED_WALL_PROFILE=/tmp/name.json` enables timestamp queries and records the complete 120–239 tick window after release. Under Bazel, the JSON is retained in the test's undeclared outputs directory. Profiling is off by default. Both before and after runs used the same zero-tick owner certification behavior and settled after 717 simulated ticks.

No adapter timestamp period was independently established, so values below are raw GPU timestamp ticks, **not milliseconds**. Percentiles use nearest-rank p95. Each run includes exactly 120 identical relative simulation ticks, excluding compilation, setup, CPU waits, and later sleeping frames.

| GPU stage | Before median | After median | Before p95 | After p95 |
|---|---:|---:|---:|---:|
| Dynamic solver (including its commit boundary) | 231278 | 230242 | 531528 | 532528 |
| All physics stages | 938300 | 938958 | 1743556 | 1742076 |

The solver median changed -0.45%, p95 +0.19%, and total median +0.07%. These samples show **no meaningful performance improvement** in this arrangement. They do not support an extrapolated frame-rate claim. There may be few/no empty eligible levels in this particular workload; dependency-depth telemetry was not added to production for this experiment.

Artifacts: `solver-levels-before.json/.log/.xml`, `solver-levels-after.json/.log/.xml`. Full native dynamic solver suite also passed after the change: `actual-level-solver-suite.log/.xml`. The same imported-wall acceptance test passed both profiling runs. This synthetic result is not a substitute for the full-house and browser gates.
