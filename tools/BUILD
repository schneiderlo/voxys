package(default_visibility = ["//visibility:public"])

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
