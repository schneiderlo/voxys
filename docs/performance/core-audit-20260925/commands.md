# Reproduction commands

Working directory: `/home/modkin/workspace/schneiderlo/voxys`.
Audit directory: `/tmp/voxys-core-audit-20260925`.
The baseline is revision `45eada5` plus the pre-existing working-tree changes,
identified in `evidence/baseline-context.json` and `evidence/source-hashes.json`.
Resetting to HEAD does not reproduce the shader/physics inputs. Original runtime
working/staged patches are archived in evidence; unrelated AGENTS.md and screenshot
deletion hunks are omitted from the published patch. The full original diff remains
in the temporary audit directory. They do not package the installed
terrain and other untracked assets; those inputs must remain available. Complete
golden files and preserved executables remain in the audit directory.
The Nix build used GCC 15.2.0, Bazel 8.5.0 and Bazelisk 1.28.1; store paths and
version commands are recorded in `evidence/toolchain.txt`.

For reconstruction after workspace drift, use a separate checkout of the recorded
revision with the archived pre-existing source patches and installed asset inputs.
The archived `village.patch`, `part-lookup.patch`, `equivalence-tests.patch` and
`benchmark-guard.patch` identify this audit's changes separately. The commands
below record the actual original working directory; they do not imply that
rebuilding today's concurrently modified tree recreates those executables.

## Baseline

```bash
mkdir -p /tmp/voxys-core-audit-20260925
nix-shell --run 'bazel test --jobs=4 //tests:voxy_tests //tools:terrain_diffusion_import_test' > /tmp/voxys-core-audit-20260925/baseline-tests.log 2>&1
node --test ui/test-ui.mjs ui/test-shared.mjs scripts/test_data_packs.mjs scripts/test_loading_roller.mjs scripts/test_webgpu_startup.mjs scripts/test_startup_budget.mjs scripts/test_frame_budget.mjs scripts/test_webgpu_timestamp_compat.mjs scripts/performance/test_gameplay_work.mjs scripts/performance/test_gpu_hang_trace.mjs
python3 scripts/performance/test_compare_free_build_benchmark.py
nix-shell --run 'bazel --output_base=/tmp/voxys-core-audit-20260925/opt-bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 --symlink_prefix=/tmp/voxys-core-audit-20260925/opt- //tools:free_build_benchmark //tests:adventure //tests:adventure_runtime //tests:adventure_doors //tests:adventure_walkable'
cp /tmp/voxys-core-audit-20260925/opt-bin/tools/free_build_benchmark /tmp/voxys-core-audit-20260925/baseline-runner
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/voxys-core-audit-20260925/baseline-runner /tmp/voxys-core-audit-20260925/baseline-contended'
nix-shell --run '/tmp/voxys-core-audit-20260925/opt-bin/tests/adventure --gtest_filter="CreativeVillage.*"'
```

All three executables use identical optimized compiler flags. The existing GCC
null-dereference diagnostic in legacy encounter code remains visible, but is
not promoted to an error. No floating-point flags or assertions change. The
benchmark links game code into its preserved executable; only external native
libraries are dynamic. Tests in the two Bazel trees are separate. The original
default tree is not rebuilt after runtime edits.

An earlier oracle invocation used the already-built default aggregate and
selected zero tests. It was rejected as evidence; the optimized oracle above
then ran both new tests. The first candidate build hit a sign-conversion warning
in iterator arithmetic; using the iterator's difference type fixed it without
relaxing compiler checks. Attempt logs are retained.

## CPU, allocation, I/O and startup profiles

```bash
nix-build '<nixpkgs>' -A gperftools -A pprof --max-jobs 0 --no-out-link
nix-shell --run 'bash scripts/performance/profile_free_build_benchmark.sh /tmp/voxys-core-audit-20260925/baseline-runner /tmp/voxys-core-audit-20260925/profiles /nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2 /nix/store/6gdpgjrgrg592sgcbzklq2r89mbwk4yl-pprof-0-unstable-2026-03-02/bin/pprof'
nix-shell --run 'LD_PRELOAD=/nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2/lib/libprofiler.so CPUPROFILE_FREQUENCY=1000 CPUPROFILE=/tmp/voxys-core-audit-20260925/profiles/startup.prof /tmp/voxys-core-audit-20260925/baseline-runner "$PWD" 768 120 /tmp/voxys-core-audit-20260925/profiles/startup idle'
nix-shell --run 'bash scripts/performance/profile_free_build_benchmark.sh /tmp/voxys-core-audit-20260925/final-runner /tmp/voxys-core-audit-20260925/final-profiles /nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2 /nix/store/6gdpgjrgrg592sgcbzklq2r89mbwk4yl-pprof-0-unstable-2026-03-02/bin/pprof'
```

The helper captures CPU at 1 kHz over 36,000 updates at 768 parts (idle/edit),
heap allocation over 3,600 edit updates, and separate `strace -f -tt -T` I/O.
Replay markers bracket the relevant interval. `perf_event_paranoid=4` prevents
hardware-counter profiling. The profile captures overlap the original GPU test
suite and are used for attribution, never wall-time speed claims. Startup is a
separate full-process CPU profile including initialization and a short replay.

Use `pprof -top -cum -show=voxy:: -nodecount=35 BINARY PROFILE` for engine callers,
`-list='CreativeVillage::admit'` or `-list='partFor'` for source attribution,
and `-alloc_space` / `-alloc_objects` for cumulative heap allocation. Filtered
flat columns reattribute hidden callees; raw exclusive percentages come from
unfiltered reports. Complete analysis commands are retained in evidence.

## Isolated runtime stages and tests

Rebuild `//tools:free_build_benchmark //tests:adventure` with the baseline flags
after the village change, copy the executable to `village-runner`, and run:

```bash
nix-shell --run '/tmp/voxys-core-audit-20260925/opt-bin/tests/adventure --gtest_filter="CreativeVillage.*:AdventureConstructionPolicy.*"'
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/voxys-core-audit-20260925/village-runner /tmp/voxys-core-audit-20260925/village-contended'
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260925/baseline-contended /tmp/voxys-core-audit-20260925/village-contended
nix-shell --run 'bazel --output_base=/tmp/voxys-core-audit-20260925/opt-bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 --symlink_prefix=/tmp/voxys-core-audit-20260925/opt- //tools:free_build_benchmark //tests:adventure //tests:adventure_runtime //tests:adventure_doors //tests:adventure_walkable //tests:adventure_movement //tests:adventure_navigation //tests:adventure_pointer //tests:adventure_encounters //:voxy_native'
cp /tmp/voxys-core-audit-20260925/opt-bin/tools/free_build_benchmark /tmp/voxys-core-audit-20260925/final-runner
nix-shell --run 'python3 /tmp/voxys-core-audit-20260925/run-focused.py'
nix-shell --run 'VOXY_ADVENTURE_TEST_TERRAIN="$PWD/data/generated/td_seed_1234_8192.r16" VOXY_ADVENTURE_TEST_WORKSPACE="$PWD" BUILD_WORKSPACE_DIRECTORY="$PWD" bazel-bin/tests/voxy_tests --gtest_filter="*ImportedWallRuntimeIntegration*"'
```

The construction oracle runs before changing its lookup. The focused runner
sets the real terrain/workspace environment, executes the eight named targets
individually, and retains every return code and log. Its exact script is archived.
The last command uses the original aggregate and original libraries to reproduce
the three opt-in cannon failures, without rebuilding that tree.

## Final matched timing

```bash
nix-shell --run 'python3 /tmp/voxys-core-audit-20260925/run-clean-matrix.py'
```

The archived script waits for the original aggregate process to finish; it never
suspends tests with wall-clock deadlines. All audit builds and focused/profile
runs are finished before final timing. For each part count/workload/repetition,
it runs the preserved baseline, village-only and final executables consecutively.
Their order rotates per repetition: B/V/F, V/F/B, F/B/V. This creates a complete
24-case matrix per variant. Each individual command is exactly:

```text
VARIANT-runner WORKSPACE PARTS 3600 OUTPUT_PREFIX idle|edit
```

The script records process snapshots and commands per run, and writes these
unchanged-threshold comparisons:

```bash
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260925/baseline /tmp/voxys-core-audit-20260925/village
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260925/village /tmp/voxys-core-audit-20260925/final
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260925/baseline /tmp/voxys-core-audit-20260925/final
```

Only these final interleaved matrices support timing claims. Preliminary
contended matrices remain useful exact-output evidence and are retained with
their explicit labels. The ordinary desktop remains running; there is no CPU
affinity/frequency lock, cold-cache claim, or browser/displayed-FPS measurement.

The final runner exited zero; all three comparison reports contain no failures.
The evidence summary was generated after timing with:

```bash
python3 /tmp/voxys-core-audit-20260925/summarize_clean.py --workspace /home/modkin/workspace/schneiderlo/voxys --aggregate-log /tmp/voxys-core-audit-20260925/original-aggregate-test.log
python3 scripts/performance/test_compare_free_build_benchmark.py
git diff --check
```
