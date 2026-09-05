# Evidence index

See [the report](../../performance-followup-2026-09-05.md) for conclusions, scope, proof sketches, and runnable benchmark commands.

- `summary.json`: medians of per-run statistics, including p50/p95/p99, throughput, peak RSS, scratch size, and output hashes. Final results are in `gpu-final.json`, `radix-final.json`, and `replication-final.json` sections.
- `runs.json.gz`: all A/B run records and complete associated logs, including initial unfavorable results and tuning experiments. Native per-view percentiles are retained here.
- `validation-and-profiles.json`: initial workload logs, final guard logs, build/process/shader failures, and focused prefix/culling measurements.
- `test-details.json.gz`: complete baseline and final main-suite test logs.
- `*-cpu-top.txt`, `*-allocations.txt`, `*-io.txt`: CPU, heaptrack, and syscall profiles. Initialization is included. Do not interpret sampled CPU percentages as GPU or wall-time percentages.
- `cpu-profiles.json.gz`: raw gperftools profiles encoded as hexadecimal JSON values. Decode a value with `bytes.fromhex` to use it with pprof and the corresponding binary.
- `radix-final-web-report.json`: 39 exact old/new/CPU histogram-prefix checks; 81,828,864 compared words; zero differences. Chrome used SwiftShader for correctness.
- `metadata.json`: baseline commit, host, source/binary hashes, screenshot hashes, and exact screenshot commands.
- `opportunity-matrix.txt`: decisions and proof sketches recorded before the respective edits.
- `experiment-sources.tar.gz`: diagnostic probes, orchestration scripts, exact compiler/link argv, and frozen original/initial/final radix shader inputs. These are investigation artifacts, not additional production code.

## Reading compressed logs

```bash
python3 - <<'PY'
import gzip, json
from pathlib import Path
base = Path('docs/benchmarks/performance-followup-2026-09-05')
with gzip.open(base / 'runs.json.gz', 'rt') as source:
    runs = json.load(source)
for row in runs['gpu-final.json']:
    print(row['summary'])
PY
```

## Version labels

The investigation started at commit `6701a85`.

- Early `gpu-ab`, `gpu-confirm`, `native-ab`, `native-large-ab`, and `radix-ab` results use the initial **128-block** radix cutoff.
- `gpu-final`, `native-final`, and `radix-final` use the retained **32-block** cutoff.
- `replication-ab` isolates indexed queries. `replication-final` rotates through original, indexed-only, and indexed-plus-precomputed-distance implementations.
- Tile and cutoff sweeps are diagnostic variants, not retained production alternatives.

Scripts record the exact session commands and `/tmp/voxys-perf-followup` layout. That layout evolved during the session: `candidate-128` now holds the frozen initial binaries, while `candidate` holds the final radix binaries. To replay an early script, point its candidate path at `candidate-128` and use the corresponding frozen shader. Cutoff/tile scripts originally read the 128-block shader from the working tree; use the archived `candidate-128` shader when replaying them. For future regression checks, prefer the two repository benchmark targets documented in the report.

The baseline and candidate physics/native binaries were statically linked against separate CMake core versions. Copying a Bazel executable alone would not isolate its shared-library dependencies.

Large raw heaptrack traces, executable copies, and byte-identical native PNGs remain under `/tmp/voxys-perf-followup`. Their summary evidence is retained here. Timing budgets are calibrated for this AMD host; correctness checks apply independently of those budgets.
