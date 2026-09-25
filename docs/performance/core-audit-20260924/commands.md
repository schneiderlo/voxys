# Commands and measurement inputs

Run from `/home/modkin/workspace/schneiderlo/voxys`. The audit directory was
`/tmp/voxys-core-audit-20260924`. `baseline-context.json` records the starting
HEAD, source hashes, platform, and terrain. The working tree was already
dirty; resetting to HEAD alone does **not** reproduce its shader/physics inputs.
The original staged and working patches remain in that temporary directory.

The host was an AMD Ryzen AI 9 HX 370 (24 logical CPUs), Radeon 890M with RADV
STRIX1 Vulkan, using the repository Nix shell, GCC 15 and Bazel 8.5. The OS file
cache was warm. There was no competing test/build during clean timed matrices.
No CPU affinity or frequency lock was applied; results are medians of three
runs on this host, not cross-machine guarantees.

## Baseline tests, before runtime edits

```bash
nix-shell --run 'bazel test --jobs=4 //tests:voxy_tests //tools:terrain_diffusion_import_test' > /tmp/voxys-core-audit-20260924/baseline-tests.log 2>&1
node --test ui/test-ui.mjs ui/test-shared.mjs scripts/test_data_packs.mjs scripts/test_loading_roller.mjs scripts/test_webgpu_startup.mjs scripts/test_startup_budget.mjs
python3 scripts/performance/test_compare_free_build_benchmark.py
```

## Optimized builds and preserved stages

```bash
nix-shell --run 'bazel --output_base=/tmp/voxys-core-audit-20260924/opt-bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 --symlink_prefix=/tmp/voxys-core-audit-20260924/opt- //tools:free_build_benchmark //:voxy_native'
```

The one warning exception is needed for existing optimized GCC null-dereference
diagnostics. It was used for **both** baseline and candidate; optimization,
floating-point flags, and installed content did not change. Debug symbols support
profiling; execution is optimized. Before runtime edits, the resulting benchmark
and app were copied to `baseline-runner` and `baseline-native` in the audit
directory. These executables embed the game implementation; their external GPU
library is provided by the Nix shell.

The benchmark was rebuilt after adding only accepted-edit timing and preserved
as `baseline-instrumented-runner`. After the reservation change it was rebuilt as
`scenery-runner`; after the membership index, as `membership-runner`. Their SHA-256
values are in `evidence/binaries.sha256`. Baseline focused test executables are
dynamically linked and are **not** standalone immutable baseline artifacts.

## Clean workload matrices and comparisons

```bash
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/voxys-core-audit-20260924/baseline-runner /tmp/voxys-core-audit-20260924/baseline'
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/voxys-core-audit-20260924/baseline-instrumented-runner /tmp/voxys-core-audit-20260924/baseline-edits'
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/voxys-core-audit-20260924/scenery-runner /tmp/voxys-core-audit-20260924/scenery'
nix-shell --run 'bash scripts/performance/run_free_build_benchmark.sh /tmp/voxys-core-audit-20260924/membership-runner /tmp/voxys-core-audit-20260924/final'
python3 /tmp/voxys-core-audit-20260924/original-comparator.py /tmp/voxys-core-audit-20260924/baseline /tmp/voxys-core-audit-20260924/baseline-edits
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260924/baseline-edits /tmp/voxys-core-audit-20260924/scenery
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260924/scenery /tmp/voxys-core-audit-20260924/final
python3 scripts/performance/compare_free_build_benchmark.py /tmp/voxys-core-audit-20260924/baseline-edits /tmp/voxys-core-audit-20260924/final
```

The original comparator is preserved because the new comparator deliberately
rejects missing accepted-edit metrics. The runner executes each combination of
0/64/256/768 parts, idle/edit, three repetitions, with 3,600 measured updates.
Direct invocation is `BINARY WORKSPACE PARTS FRAMES OUTPUT_PREFIX WORKLOAD`.
Metrics include startup milliseconds and peak RSS from `getrusage`, in addition
to the replay distributions. Golden `.jsonl` and `.save` files remain under the
temporary directory; their byte counts and hashes are archived in the repository.

## CPU, allocation and I/O profiles, separate from clean timing

```bash
nix-shell --run 'bash scripts/performance/profile_free_build_benchmark.sh /tmp/voxys-core-audit-20260924/baseline-runner /tmp/voxys-core-audit-20260924/profiles /nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2 /nix/store/6gdpgjrgrg592sgcbzklq2r89mbwk4yl-pprof-0-unstable-2026-03-02/bin/pprof'
nix-shell --run 'bash scripts/performance/profile_free_build_benchmark.sh /tmp/voxys-core-audit-20260924/membership-runner /tmp/voxys-core-audit-20260924/final-profiles /nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2 /nix/store/6gdpgjrgrg592sgcbzklq2r89mbwk4yl-pprof-0-unstable-2026-03-02/bin/pprof'
```

The script uses `libprofiler.so`, `CPUPROFILE_FREQUENCY=1000`, and replay-bracketed
`VOXY_BENCH_CPU` for 36,000 updates at 768 parts, idle and edit. Allocation uses
`libtcmalloc.so`, `VOXY_BENCH_HEAP`, and one 3,600-update edit replay; automatic heap
dumps are disabled. I/O uses a separate `strace -f -tt -T` replay and inspects only
the interval between `VOXY_REPLAY_BEGIN` and `VOXY_REPLAY_END`. Linux
`perf_event_paranoid=4` prevented hardware perf counters; gperftools was used.

Startup was profiled separately, including initialization and 120 idle updates:

```bash
nix-shell --run 'LD_PRELOAD=/nix/store/brivbydwp5ykjhaw8pnc2kf3jrni24bz-gperftools-2.17.2/lib/libprofiler.so CPUPROFILE_FREQUENCY=1000 CPUPROFILE=/tmp/voxys-core-audit-20260924/profiles/startup.prof /tmp/voxys-core-audit-20260924/baseline-runner "$PWD" 768 120 /tmp/voxys-core-audit-20260924/profiles/startup idle'
```

For human-readable attribution, use `pprof -top -cum -show=voxy:: -nodecount=35
BINARY PROFILE`, `-alloc_space` or `-alloc_objects` for heap reports, and
`-list='CreativeScenery::admit'` for source attribution. Raw captures are archived
compressed beside their readable reports; interpreting raw addresses requires
the matching preserved binary.

## Final build and focused tests

```bash
nix-shell --run 'bazel --output_base=/tmp/voxys-core-audit-20260924/opt-bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 --symlink_prefix=/tmp/voxys-core-audit-20260924/opt- //tools:free_build_benchmark //tests:adventure //tests:adventure_runtime //tests:adventure_doors //tests:adventure_walkable'
nix-shell --run 'bazel --output_base=/tmp/voxys-core-audit-20260924/opt-bazel build -c opt --copt=-g --copt=-Wno-error=null-dereference --strip=never --jobs=4 --symlink_prefix=/tmp/voxys-core-audit-20260924/opt- //:voxy_native //tests:adventure_movement //tests:adventure_navigation //tests:adventure_pointer //tests:adventure_encounters'
```

Tests were invoked from the workspace root in `nix-shell`, with:

```bash
export VOXY_ADVENTURE_TEST_TERRAIN="$PWD/data/generated/td_seed_1234_8192.r16"
export VOXY_ADVENTURE_TEST_WORKSPACE="$PWD" BUILD_WORKSPACE_DIRECTORY="$PWD"
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_walkable
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_doors
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_runtime
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_movement
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_navigation
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_pointer
/tmp/voxys-core-audit-20260924/opt-bin/tests/adventure_encounters
```

The new reservation oracle was also run against the original implementation,
before either runtime edit. The three opt-in cannon-input failures were reproduced
using the untouched original aggregate executable and its original libraries,
under the same environment:

```bash
bazel-bin/tests/voxy_tests --gtest_filter='*ImportedWallRuntimeIntegration*'
```

The original default Bazel output tree was not rebuilt after the optimizations;
all changed code was built in the isolated optimized output base. This distinction
matters when reproducing the baseline test failures. No browser/WASM benchmark
is claimed.

## Aggregate rerun before the requested push

The full suite was subsequently rebuilt and rerun in the optimized output base:

```bash
nix-shell --run 'bazel --output_base=/tmp/voxys-core-audit-20260924/opt-bazel test -c opt --copt=-g --copt=-Wno-error=null-dereference --per_file_copt="tests/test_(native_workshop_menu|native_design_library|design_library).cpp@-Wno-error=stringop-overflow,-Wno-error=array-bounds" --strip=never --jobs=4 --keep_going --symlink_prefix=/tmp/voxys-core-audit-20260924/opt- //tests:voxy_tests //tools:terrain_diffusion_import_test'
```

The three unchanged blueprint test helpers append a digest with `vector::insert`.
GCC 15's optimized build emitted `stringop-overflow` and `array-bounds` diagnostics
inside the standard-library implementation and promoted them to errors. The
file-specific exception above keeps these diagnostics visible while allowing the
test build. It changes neither runtime compilation nor test assertions. Initial
failed build attempts and the final run are retained in the temporary audit
directory. Performance measurements were completed before this rerun and were
not rebuilt with additional flags.
