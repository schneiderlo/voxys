#!/usr/bin/env bash
# Separate CPU, allocation and I/O runs; do not treat their timings as benchmarks.
set -euo pipefail
if [[ $# != 4 ]]; then
    echo 'Usage: profile_free_build_benchmark.sh EXECUTABLE OUTPUT_DIRECTORY GPERFTOOLS_ROOT PPROF_EXECUTABLE' >&2
    exit 2
fi
benchmark_executable=$(realpath "$1")
benchmark_output=$(realpath -m "$2")
profiler_root=$(realpath "$3")
profile_reader=$(realpath "$4")
benchmark_workspace=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$benchmark_output"
export LC_ALL=C TZ=UTC
for workload in idle edit; do
    prefix="$benchmark_output/cpu-$workload"
    LD_PRELOAD="$profiler_root/lib/libprofiler.so" CPUPROFILE_FREQUENCY=1000 VOXY_BENCH_CPU="$prefix.prof" \
        "$benchmark_executable" "$benchmark_workspace" 768 36000 "$prefix" "$workload" > "$prefix.log" 2>&1
    "$profile_reader" -top -nodecount=20 "$benchmark_executable" "$prefix.prof" > "$prefix.top.txt" 2>&1
    "$profile_reader" -top -cum -nodecount=20 "$benchmark_executable" "$prefix.prof" > "$prefix.cumulative.txt" 2>&1
    echo "Completed CPU profile: $workload"
done
prefix="$benchmark_output/alloc-edit"
LD_PRELOAD="$profiler_root/lib/libtcmalloc.so" VOXY_BENCH_HEAP="$prefix" \
    HEAP_PROFILE_ALLOCATION_INTERVAL=0 HEAP_PROFILE_INUSE_INTERVAL=0 HEAP_PROFILE_TIME_INTERVAL=0 \
    "$benchmark_executable" "$benchmark_workspace" 768 3600 "$prefix" edit > "$prefix.log" 2>&1
heap_profiles=("$prefix".*.heap)
[[ ${#heap_profiles[@]} == 1 && -f ${heap_profiles[0]} ]]
"$profile_reader" -top -alloc_space -nodecount=20 "$benchmark_executable" "${heap_profiles[0]}" > "$prefix.bytes.txt" 2>&1
"$profile_reader" -top -alloc_objects -nodecount=20 "$benchmark_executable" "${heap_profiles[0]}" > "$prefix.objects.txt" 2>&1
echo 'Completed allocation profile'
prefix="$benchmark_output/io-edit"
strace -f -tt -T -o "$prefix.strace" \
    "$benchmark_executable" "$benchmark_workspace" 768 3600 "$prefix" edit > "$prefix.log" 2>&1
echo 'Completed I/O trace'
