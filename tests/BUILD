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
    "test_gpu_context.cpp",
    "test_gpu_resources.cpp",
    "test_shader_terrain.cpp",
    "test_shader_raycast.cpp",
    "test_shader_blit.cpp",
    "test_triangle_path.cpp",
    "test_raycast_path.cpp",
    "test_blit_path.cpp",
    "test_debug_visualizer.cpp",
    "test_mip_pipeline.cpp",
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

