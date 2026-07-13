# List of test source files
TEST_SRCS = [
    "test_log.cpp",
    "test_timer.cpp",
    "test_config.cpp",
    "test_window.cpp",
    "test_input.cpp",
    "test_heightmap.cpp",
    "test_compression.cpp",
    "test_mip_generator.cpp",
    "test_camera.cpp",
    "test_controller.cpp",
    "test_character_controller.cpp",
    "test_physics_world.cpp",
    "test_gpu_context.cpp",
    "test_gpu_resources.cpp",
    "test_shader_terrain.cpp",
    "test_shader_raycast.cpp",
    "test_shader_blit.cpp",
    "test_triangle_path.cpp",
    "test_raycast_path.cpp",
    "test_blit_path.cpp",
    "test_benchmark.cpp",
    "test_debug_visualizer.cpp",
    "test_mip_pipeline.cpp",
    "test_primitive_path.cpp",
    "test_shadow_bake.cpp",
    "test_terrain_textures.cpp",
    "test_application.cpp",
    "test_svo_core.cpp",
]

load("//settings:configure_copts.bzl", "PROJECT_TEST_COPTS")

# Generate a separate test target for each test file
[cc_test(
    name = src.replace(".cpp", "").replace("test_", ""),
    srcs = [src],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src:voxy_core",
        "@googletest//:gtest_main",
    ],
    data = [
        "//shaders:shaders",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
) for src in TEST_SRCS]

# Combined test target that runs all tests
cc_test(
    name = "voxy_tests",
    srcs = TEST_SRCS,
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src:voxy_core",
        "@googletest//:gtest_main",
    ],
    data = [
        "//shaders:shaders",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "performance_goldens",
    srcs = ["test_performance_goldens.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src:voxy_core",
        "@googletest//:gtest_main",
    ],
    data = ["//data:performance_golden"],
)

cc_test(
    name = "primitive_instance_benchmark",
    srcs = ["primitive_instance_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/render:primitive_instance_packing",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "physics_snapshot_benchmark",
    srcs = ["physics_snapshot_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/physics:physics",
        "@googletest//:gtest_main",
    ],
)

# Kept independent from //src:voxy_core so renderer validation does not pull in
# unrelated application/physics dependencies.
cc_test(
    name = "water_simulation",
    srcs = ["test_water_simulation.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/gpu:gpu",
        "//src/render:water_simulation",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

# Standalone SVO core test - uses only header-only voxel_types (no GPU/GLFW deps)
cc_test(
    name = "svo_core_standalone",
    srcs = ["test_svo_core.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/voxel:voxel_types",
        "@googletest//:gtest_main",
    ],
)
