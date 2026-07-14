#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

bazel test \
    //tests:determinism_ci \
    //tests:networking_ci \
    //tests:distributed_ci \
    //tests:vertical_slices_ci \
    //tests:physics_world \
    --test_output=errors

bazel run //src/gameplay:platform_parity_probe
bazel build //:voxy_native

bazel build --config=wasm \
    //src/gameplay:platform_parity_probe \
    //:voxy_wasm

temporary_directory="$(mktemp -d)"
trap 'rm -rf "$temporary_directory"' EXIT
tar -xf bazel-bin/src/gameplay/platform_parity_probe \
    -C "$temporary_directory"
node "$temporary_directory/platform_parity_probe.js"

./scripts/validate_wasm_shaders.sh
./scripts/validate_wasm_startup.sh
