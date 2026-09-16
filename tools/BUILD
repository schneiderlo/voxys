package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS", "PROJECT_TEST_COPTS")

# Explicit same-host CPU component workload; never used as a rendered FPS gate.
cc_binary(
    name = "free_build_benchmark",
    srcs = ["benchmarks/free_build_benchmark.cpp"],
    copts = PROJECT_DEFAULT_COPTS + ["-DVOXY_NATIVE=1"],
    target_compatible_with = ["@platforms//os:linux"],
    linkopts = ["-ldl"],
    deps = ["//src/game:adventure_runtime", "//src/game:adventure_core", "//src/gpu:gpu", "//src/camera:camera", "//src/engine/platform:input", "//third_party:tinygltf"],
    data = ["//data:adventure_runtime", "//shaders:shaders"],
)

cc_library(
    name = "gameplay_sidecar_lib",
    hdrs = ["salvage_assets/gameplay_sidecar.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["."],
    deps = [
        "//src/game:gameplay_sidecar",
    ],
)

cc_binary(
    name = "gameplay_sidecar_tool",
    srcs = ["salvage_assets/gameplay_sidecar_main.cpp"],
    copts = PROJECT_DEFAULT_COPTS,
    deps = [":gameplay_sidecar_lib", "//src/moto:moto"],
)

filegroup(
    name = "gameplay_sidecar_fixture",
    srcs = ["salvage_assets/fixtures/probe.gameplay.json"],
)

cc_test(
    name = "gameplay_sidecar_test",
    srcs = ["salvage_assets/test_gameplay_sidecar.cpp"],
    copts = PROJECT_TEST_COPTS,
    data = [":gameplay_sidecar_fixture"],
    deps = [":gameplay_sidecar_lib", "@googletest//:gtest_main"],
)

py_binary(
    name = "cook_gameplay_asset",
    srcs = ["salvage_assets/cook_gameplay_asset.py"],
    main = "salvage_assets/cook_gameplay_asset.py",
)

py_test(
    name = "cook_gameplay_asset_test",
    size = "small",
    # The no-replace publication and FIFO/shell rejection fixtures are verified
    # on Linux. Windows authoring execution needs its own portable validation.
    target_compatible_with = ["@platforms//os:linux"],
    srcs = [
        "salvage_assets/cook_gameplay_asset.py",
        "salvage_assets/test_cook_gameplay_asset.py",
    ],
    main = "salvage_assets/test_cook_gameplay_asset.py",
    data = [
        ":gameplay_sidecar_fixture",
        ":gameplay_sidecar_tool",
        ":gltf_vmesh_tool",
        "//data:salvage_authoring_probe",
    ],
    env = {
        "SALVAGE_CONVERTER": "$(rootpath :gltf_vmesh_tool)",
        "SALVAGE_VALIDATOR": "$(rootpath :gameplay_sidecar_tool)",
    },
)

# Offline glTF 2.0 -> .vmesh converter. The library form exposes
# convertGltfBytesToVmesh() without a main() so tests can drive it in-memory.
cc_library(
    name = "gltf_vmesh_tool_lib",
    srcs = ["gltf_vmesh_tool.cpp"],
    hdrs = ["gltf_vmesh_tool.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    defines = ["GLTF_VMESH_TOOL_NO_MAIN"],
    includes = ["."],
    deps = [
        "//src/core:core",
        "//src/moto:moto",
        "//third_party:stb",
        "//third_party:tinygltf",
    ],
)

cc_binary(
    name = "gltf_vmesh_tool",
    srcs = [
        "gltf_vmesh_tool.cpp",
        "gltf_vmesh_tool.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["."],
    deps = [
        "//src/core:core",
        "//src/moto:moto",
        "//third_party:stb",
        "//third_party:tinygltf",
    ],
)

py_binary(
    name = "generate_wreckwater_build_content",
    srcs = ["generate_wreckwater_build_content.py"],
    main = "generate_wreckwater_build_content.py",
)

py_binary(
    name = "check_wreckwater_authority_bazel_closure",
    srcs = ["check_wreckwater_authority_bazel_closure.py"],
    main = "check_wreckwater_authority_bazel_closure.py",
)

py_binary(
    name = "check_wreckwater_authority_cmake_closure",
    srcs = ["check_wreckwater_authority_cmake_closure.py"],
    main = "check_wreckwater_authority_cmake_closure.py",
)

py_test(
    name = "check_wreckwater_authority_bazel_closure_test",
    srcs = [
        "check_wreckwater_authority_bazel_closure.py",
        "test_check_wreckwater_authority_bazel_closure.py",
    ],
    main = "test_check_wreckwater_authority_bazel_closure.py",
)

py_test(
    name = "check_wreckwater_authority_cmake_closure_test",
    srcs = [
        "check_wreckwater_authority_cmake_closure.py",
        "test_check_wreckwater_authority_cmake_closure.py",
    ],
    main = "test_check_wreckwater_authority_cmake_closure.py",
)

py_test(
    name = "generate_wreckwater_build_content_test",
    srcs = [
        "generate_wreckwater_build_content.py",
        "test_generate_wreckwater_build_content.py",
    ],
    main = "test_generate_wreckwater_build_content.py",
)

sh_binary(
    name = "terrain_diffusion_import",
    srcs = ["terrain_diffusion_import.py"],
)

sh_binary(
    name = "read_physics_telemetry",
    srcs = ["read_physics_telemetry.py"],
)

sh_test(
    name = "terrain_diffusion_import_test",
    srcs = ["test_terrain_diffusion_import.py"],
    data = ["terrain_diffusion_import.py"],
)

sh_test(
    name = "serve_wasm_test",
    srcs = ["test_serve_wasm.py"],
    data = [
        "read_physics_telemetry.py",
        "serve_wasm.py",
    ],
)

cc_binary(
    name = "check_brick_stack",
    srcs = ["salvage_assets/check_brick_stack.cpp"],
    copts = PROJECT_DEFAULT_COPTS,
    deps = ["//src/game:fixture_registry", "//src/game:compiled_assembly"],
)

sh_binary(
    name = "serve_wasm",
    srcs = ["serve_wasm.sh"],
    data = [
        "//:voxy_wasm",
        "//:voxy.cfg",
        "//shaders:shaders",
        "//web:assets",
        "serve_wasm.py",
        "@bazel_tools//tools/bash/runfiles",
    ],
)
