package(default_visibility = ["//visibility:public"])

# Compiler detection for conditional warning flags
config_setting(
    name = "clang_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "clang",
    },
)

config_setting(
    name = "msvc_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "msvc-cl",
    },
)

config_setting(
    name = "clang-cl_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "clang-cl",
    },
)

# ASan suppression file for tests
filegroup(
    name = "asan_suppressions",
    srcs = [".asan_suppressions"],
)

# TSan suppression file for tests
filegroup(
    name = "tsan_suppressions",
    srcs = [".tsan_suppressions"],
)

# MSan suppression file for tests
filegroup(
    name = "msan_suppressions",
    srcs = [".msan_suppressions"],
)

cc_binary(
    name = "voxy_native",
    srcs = ["//src/engine/platform:native/entry.cpp"],
    deps = ["//src:voxy_core"],
    data = [
        "//shaders:shaders",
        "//data:data",
        "voxy.cfg",
    ],
    defines = ["VOXY_NATIVE"],
)

load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

cc_binary(
    name = "voxy_wasm_cc",
    srcs = ["//src/engine/platform:wasm/entry.cpp"],
    deps = [
        "//src:voxy_core",
        "@emdawnwebgpu//:emdawnwebgpu",
    ],
    linkopts = [
        "-sWASM=1",
        # Web APIs reject views backed by resizable WebAssembly memory.
        "-sALLOW_MEMORY_GROWTH=0",
        "-sINITIAL_MEMORY=512MB",
        "-sSTACK_SIZE=1MB",
        "-sMODULARIZE=1",
        "-sEXPORT_NAME=VoxyModule",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']",
        "-sEXPORTED_FUNCTIONS=['_main','_voxy_resize','_voxy_mouse_move','_voxy_key_event','_voxy_get_fps','_voxy_get_last_frame_cpu_ms','_voxy_get_gpu_frames_in_flight','_voxy_get_gpu_pacing_skips','_voxy_get_physics_substeps','_voxy_get_physics_resident_bodies','_voxy_get_physics_stage_ms','_voxy_get_telemetry_json','_voxy_is_initialized','_voxy_get_physics_backend','_voxy_start_physics_self_test','_voxy_get_physics_self_test_status','_voxy_get_physics_self_test_tick']",
        "-sASYNCIFY",
        "--js-library", "$(location @emdawnwebgpu//:src/library_webgpu_generated_struct_info.js)",
        "--js-library", "$(location @emdawnwebgpu//:src/library_webgpu_generated_sig_info.js)",
        "--js-library", "$(location @emdawnwebgpu//:src/library_webgpu_enum_tables.js)",
        "--js-library", "$(location @emdawnwebgpu//:src/library_webgpu.js)",
        "--preload-file", "shaders@/shaders",
        # Only embed the data files referenced by voxy.cfg.
        "--preload-file", "data/generated/td_seed_1234_8192.ldh@/data/generated/td_seed_1234_8192.ldh",
        "--preload-file", "data/generated/td_seed_1234_8192_albedo.jpg@/data/generated/td_seed_1234_8192_albedo.jpg",
        "--preload-file", "voxy.cfg@/voxy.cfg",
    ],
    additional_linker_inputs = [
        "@emdawnwebgpu//:js_files",
        "//shaders:shaders",
        "//data:data",
        "voxy.cfg",
    ],
    data = [
        "//shaders:shaders",
        "//data:data",
        "voxy.cfg",
        "@emdawnwebgpu//:src/library_webgpu.js",
        "@emdawnwebgpu//:src/library_webgpu_enum_tables.js",
        "@emdawnwebgpu//:src/library_webgpu_generated_struct_info.js",
        "@emdawnwebgpu//:src/library_webgpu_generated_sig_info.js",
    ],
    tags = ["no-sandbox"],
)

wasm_cc_binary(
    name = "voxy_wasm",
    cc_target = ":voxy_wasm_cc",
    tags = ["no-sandbox"],
)
