# Radix scatter: remove redundant prefix synchronization

Status: candidate optimization; GPU validation and hardware performance
acceptance are pending. No FPS improvement is claimed.

Baseline: `db72d9c4e4a6e734cdf2da78b6eb5d80f5d5f080`.
Changed production code: `shaders/physics_deterministic_primitives.wgsl`,
`radix_scatter_impl` only.

## Change

The eight worker chunks already write separate rows of `radixLocalCounts`.
After the count-phase barrier, each digit belongs to exactly one invocation:
`digit = lid.x + k * groupSize`. Keep that digit's running prefix private and
scan all eight worker rows in that invocation. Retain a final workgroup
barrier before scatter workers consume prefixes written by other invocations.

The original worker-major prefix loop used shared atomic loads/stores and a
whole-workgroup barrier after each worker row. The new digit-major loop
requires neither inside the prefix scan.

| Per workgroup, per radix-scatter pass | Baseline | Candidate |
| --- | ---: | ---: |
| Workgroup barrier calls | 11 | 3 |
| Prefix-related WGSL atomic loads/stores | 4,352 | 0 |
| Input/output record reads/writes in scatter | Unchanged | Unchanged |
| Sort order, entry points, buffer layouts, dispatches | Unchanged | Unchanged |

These are source-level operation counts, not measured machine instructions or
GPU speedups. The removed atomic operations are 256 initialization stores plus
8 rows × 256 digits × (one load + one store). The histogram kernel still uses
its existing atomics; only the scatter prefix changes.

## Correctness argument

A prefix slot `(worker, digit)` has one owner during the prefix phase. Its
value remains `sum(counts[earlier_worker][digit])`. The count barrier makes all
worker rows available before reading. The final barrier makes every prefix
available before scatter. The scatter worker then advances its own row in
input order, preserving stable ranks within and between contiguous chunks.

Empty worker chunks contribute zero. Partial and overdispatched empty groups
do not change the ownership argument. Workgroup sizes 64, 128 and 256 retain
the same digit partition. No subgroups or adapter-specific features are added.

## Validation performed

```bash
uv run python tools/test_radix_scatter_prefix.py
```

The standard-library CPU model passed all four test groups under Python 3.13:
unique digit ownership; 2,706 partial-workgroup/pattern prefix comparisons;
all eight key bytes across boundary-sized and 8,193-record inputs; and complete
stable low-word, high-word and logical 64-bit sorts. Both prefix implementations
are checked against an independent mathematical prefix oracle. Whole-pass
outputs are checked against Python's stable sort, including payload and
ordinal words, destination uniqueness, unwritten slots and output canaries.

This model does NOT execute WGSL, validate GPU memory ordering, compile the
engine, or measure performance. The local browser blocked the test page and a
native WebGPU runtime was unavailable. `git diff --check` also passed.

## Required acceptance before merge

On a WebGPU-capable machine, run the existing production GPU tests. A skipped
headless fixture is not a pass for this change:

```bash
nix-shell --run 'bazel test -c opt \
  //tests:deterministic_gpu_primitives \
  //tests:gpu_broad_phase \
  //tests:gpu_narrow_phase \
  //tests:gpu_dynamic_solver \
  --test_output=all --runs_per_test=2'
```

Capture baseline and candidate on the same hardware, Chrome session, canvas,
body counts and fixed simulation ticks using the procedure in
[WebGPU performance profiling](performance-profiling.md). Inspect
`broad_index_sort_ranges` and `broad_pair_sort_unique`, but decide acceptance
from the complete scaling matrix and deterministic equivalence checks.
Require a confidence interval excluding zero for the relevant improvement,
with no correctness, overflow or tail-latency regression.

Also run the visible dense-pile browser journey, not just the cached renderer
throughput gate. Do not reduce body counts, resolution, physics quality or
profiling validation to make this candidate pass.
