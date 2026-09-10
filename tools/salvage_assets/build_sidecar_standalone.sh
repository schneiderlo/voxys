#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
sidecar_cxx="${SALVAGE_CXX:-g++}"
sidecar_out="${SALVAGE_SIDECAR_BUILD_DIR:-/tmp/salvage-sidecar-build}"
mkdir -p "$sidecar_out"
sidecar_flags=(-std=c++20 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
  -Wunused -Woverloaded-virtual -Wpedantic -Wconversion -Wsign-conversion -Wnull-dereference
  -Wdouble-promotion -Wformat=2 -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches
  -Wlogical-op -Wuseless-cast -Werror -O1 -g -fsanitize=undefined,float-cast-overflow
  -fno-sanitize-recover=all -Isrc -Itools -isystem third_party/tinygltf)
sidecar_sources=(src/game/construction/construction_types.cpp src/game/construction/part_catalog.cpp
  src/game/assets/gameplay_sidecar.cpp)
"$sidecar_cxx" "${sidecar_flags[@]}" "${sidecar_sources[@]}" src/moto/vmesh_io.cpp \
  tools/salvage_assets/gameplay_sidecar_main.cpp -o "$sidecar_out/gameplay_sidecar_tool"
"$sidecar_cxx" "${sidecar_flags[@]}" -isystem third_party/googletest/googletest/include \
  "${sidecar_sources[@]}" tools/salvage_assets/test_gameplay_sidecar.cpp \
  build-lego-native/lib/libgtest_main.a build-lego-native/lib/libgtest.a -pthread \
  -o "$sidecar_out/gameplay_sidecar_tests"
