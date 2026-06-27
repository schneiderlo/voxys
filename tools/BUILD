package(default_visibility = ["//visibility:public"])

sh_binary(
    name = "terrain_diffusion_import",
    srcs = ["terrain_diffusion_import.py"],
)

sh_binary(
    name = "download_cc0_texture_pack",
    srcs = ["download_cc0_texture_pack.py"],
)

sh_test(
    name = "terrain_diffusion_import_test",
    srcs = ["test_terrain_diffusion_import.py"],
    data = ["terrain_diffusion_import.py"],
)

sh_binary(
    name = "serve_wasm",
    srcs = ["serve_wasm.sh"],
    data = [
        "//:voxy_wasm",
        "//:voxy.cfg",
        "//web:assets",
        "@bazel_tools//tools/bash/runfiles",
    ],
)
