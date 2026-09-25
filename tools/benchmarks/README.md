# Free-building CPU benchmark

This uses the actual creative `AdventureRuntime`, the installed 8192×8192 terrain,
the public input/action interface, and normal snapshot validation. It creates an
isolated temporary save directory and never opens a player's saved worlds.
It does **not** render frames: these numbers are CPU component measurements,
not browser FPS, input-to-photon latency, or GPU throughput.

## Reproduce

From the repository root:

```sh
nix-shell --run 'bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 //tools:free_build_benchmark'
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh bazel-bin/tools/free_build_benchmark /tmp/free-build-baseline'
```

The warning flag leaves an existing GCC optimized-build null-dereference warning
in legacy `AdventureEncounters::safeRest` visible without promoting it to an error.
It does not alter generated program behaviour. Use identical compiler flags for
baseline and candidate. The ordinary commit hook retains its normal flags.

Preserve the baseline executable before changing the runtime. Run preserved
executables inside `nix-shell` from the repository root so the native WebGPU
library can be resolved. Run a candidate with the same workload script into a
different directory, then:

```sh
python3 scripts/performance/compare_free_build_benchmark.py /tmp/free-build-baseline /tmp/free-build-candidate
```

The runner executes three repetitions at 0, 64, 256 and 768 existing bricks,
with both stationary aiming and a deterministic edit sequence. Each repetition
warms up for 120 updates, then measures 3,600 updates at a fixed 1/60 s simulation
step. It serializes the UI every sixth update, matching the UI's 100 ms polling
interval. Edit actions cover selection, rotation, paint, placement, undo, height,
pause/resume, wheel selection and a short walk before returning to building.
All fixtures must pass real geometry admission.

Each output prefix contains exact UI observations (`.jsonl`), an accepted save
archive (`.save`), metrics and a process log. A fixed synthetic world namespace
makes outputs reproducible. No observable fields are normalized or rounded away.
Archive decoding must exactly recover the accepted state; creative inventory and
combat invariants are checked. The comparison requires byte-identical observations
and save archives, equal outcome counters, and matching workload parameters.

Latency uses nearest-rank p50/p95/p99 in microseconds. `accepted_edit_update`
separately measures updates whose accepted part count changes, using the same
update timer. Rare placement/removal commits can otherwise disappear below the
aggregate p99 cutoff. This field is null for idle runs; edit runs must have a
nonzero, matching sample count. Rebuild and rerun older baselines that lack it.
Throughput covers the whole
replay loop, including the benchmark's observation bookkeeping. Peak RSS includes
terrain, native GPU initialization and retained golden strings; their byte count
is reported separately. Do not describe this as browser heap usage. Golden file
writes happen after the measured replay, so they do not masquerade as game I/O.

The same-host guard allows median-of-three regressions of at most 10% / 15% / 20%
for p50 / p95 / p99 of updates, serialization, and accepted-edit updates, with a
25 μs noise allowance for small workloads. Peak RSS may
grow by at most the larger of 10% or 10 MiB; replay throughput may fall by at
most 15%. Both inputs must contain the complete 24-run workload matrix. These are opt-in comparison guards,
not a universal machine-independent CI speed threshold. Never compare profiled
runs against unprofiled timing results.

## Profiles

Optional `dlsym` hooks permit gperftools without adding a production dependency:

- Preload `libprofiler.so`, set `VOXY_BENCH_CPU=/tmp/replay.cpu`, optionally
  `CPUPROFILE_FREQUENCY=1000`. Sampling starts after warmup and stops after replay.
- In a separate run, preload `libtcmalloc.so` and set
  `VOXY_BENCH_HEAP=/tmp/replay-heap`. The final heap profile includes cumulative
  allocation attribution; use `pprof -alloc_space` and `-alloc_objects`, not only
  retained objects (golden strings intentionally live until the end).
- Run a separate `strace -f -tt -T` trace. `VOXY_REPLAY_BEGIN` and
  `VOXY_REPLAY_END` writes delimit steady replay; distinguish resource loading
  before that interval and benchmark output writes after it.

The binary accepts `WORKSPACE PARTS FRAMES OUTPUT_PREFIX idle|edit` for longer
profile runs. Preserve binary/source hashes, exact commands and profile output
with the report for each investigated change. Capture profiles and rank measured
opportunities before implementing runtime optimizations.

Guard fault-injection checks (no GPU):

```sh
python3 scripts/performance/test_compare_free_build_benchmark.py
```
