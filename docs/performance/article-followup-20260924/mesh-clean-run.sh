set -eu
run=0
for variant in baseline candidate candidate baseline baseline candidate; do
    run=$((run + 1))
    env LD_LIBRARY_PATH="/home/lschneid/workspace/schneiderlo/voxys/bazel-out/k8-opt/bin/_solib_k8:/home/lschneid/workspace/schneiderlo/voxys/third_party/wgpu-native/dist/lib:$LD_LIBRARY_PATH" VK_ICD_FILENAMES=/nix/store/gqpsfqzgbcbwbh12n3sjxk2x4gqgy8lq-mesa-26.1.3/share/vulkan/icd.d/lvp_icd.x86_64.json VK_DRIVER_FILES=/nix/store/gqpsfqzgbcbwbh12n3sjxk2x4gqgy8lq-mesa-26.1.3/share/vulkan/icd.d/lvp_icd.x86_64.json XDG_RUNTIME_DIR=/tmp "/tmp/voxys-mesh-bench/$variant" --gtest_also_run_disabled_tests --gtest_filter=MeshPathGPUTest.DISABLED_ForestCpuEncodingBenchmark > "/tmp/voxys-mesh-bench/clean-$run-$variant.log" 2>&1
done
