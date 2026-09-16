#!/usr/bin/env bash
# Run inside the repository's nix-shell, after building the optimized target.
set -euo pipefail
if [[ $# != 2 ]]; then
    echo 'Usage: run_free_build_benchmark.sh EXECUTABLE OUTPUT_DIRECTORY' >&2
    exit 2
fi
benchmark_executable=$(realpath "$1")
benchmark_output=$(realpath -m "$2")
benchmark_workspace=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$benchmark_output"
export LC_ALL=C TZ=UTC
for repeat in 1 2 3; do
    for parts in 0 64 256 768; do
        for workload in idle edit; do
            prefix="$benchmark_output/${parts}-${workload}-${repeat}"
            "$benchmark_executable" "$benchmark_workspace" "$parts" 3600 "$prefix" "$workload" > "$prefix.log" 2>&1
            echo "Completed $parts parts / $workload / repeat $repeat"
        done
    done
done
