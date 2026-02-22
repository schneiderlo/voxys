package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "gpu",
    srcs = [
        "context.cpp",
        "resources.cpp",
    ],
    hdrs = [
        "context.hpp",
        "resources.hpp",
        "webgpu_compat.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
    ] + select({
        "@platforms//cpu:wasm32": [
            "@emdawnwebgpu//:emdawnwebgpu",
        ],
        "//conditions:default": [
            "//src/engine/platform:window",
            "//third_party:wgpu",
        ],
    }),
)
