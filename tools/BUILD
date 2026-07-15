package(default_visibility = ["//visibility:public"])

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
        "//web:assets",
        "serve_wasm.py",
        "@bazel_tools//tools/bash/runfiles",
    ],
)
