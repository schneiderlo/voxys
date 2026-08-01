package(default_visibility = ["//visibility:public"])

sh_binary(
    name = "run_wreckwater_2v2",
    srcs = ["run_wreckwater_2v2.sh"],
    tags = [
        "local",
        "manual",
    ],
)

sh_test(
    name = "wasm_shader_parity",
    srcs = ["validate_wasm_shaders.sh"],
    data = [
        "wait_for_shader_parity.mjs",
        "//shaders:shaders",
        "//web:assets",
    ],
    tags = ["manual"],
)

sh_test(
    name = "wasm_startup_parity",
    srcs = ["validate_wasm_startup.sh"],
    data = [
        "wait_for_wasm_startup.mjs",
        "//:voxy_wasm",
        "//web:assets",
    ],
    tags = ["manual"],
)
