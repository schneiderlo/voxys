package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

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
