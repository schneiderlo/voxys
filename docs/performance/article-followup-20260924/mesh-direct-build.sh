set -eu
VOXYS_BENCH_FLAGS='-std=c++20 -O2 -DNDEBUG -fPIC -DVOXY_NATIVE=1 -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_LEFT_HANDED -DGLM_ENABLE_EXPERIMENTAL -Isrc -Ithird_party/glm -Ithird_party/wgpu-native/dist/include -Ithird_party/wgpu-native/dist/include/webgpu'
g++ $VOXYS_BENCH_FLAGS -c src/render/mesh_path.cpp -o /tmp/voxys-mesh-bench/candidate.o
g++ $VOXYS_BENCH_FLAGS -c /tmp/voxys-mesh-bench/mesh_path_baseline.cpp -o /tmp/voxys-mesh-bench/baseline.o
g++ $VOXYS_BENCH_FLAGS -I/home/lschneid/.cache/bazel/_bazel_lschneid/32316bdb2f5c1949f03106d5cf38a916/external/googletest+/googletest/include -c tests/test_mesh_path.cpp -o /tmp/voxys-mesh-bench/tests.o
g++ @/tmp/voxys-mesh-bench/candidate.params
g++ @/tmp/voxys-mesh-bench/baseline.params
