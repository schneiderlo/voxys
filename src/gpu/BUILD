package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "shader_source",
    hdrs = ["shader_source.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "wreckwater_authority_gpu",
    srcs = [
        "context.cpp",
        "resources.cpp",
    ],
    hdrs = [
        "context.hpp",
        "resources.hpp",
        "pipeline.hpp",
        "webgpu_compat.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":shader_source",
        "//src/core:wreckwater_authority_log",
        "//third_party:wgpu",
    ],
)

cc_library(
    name = "gpu",
    srcs = [
        "context.cpp",
        "context_surface.cpp",
        "resources.cpp",
    ],
    hdrs = [
        "context.hpp",
        "resources.hpp",
        "pipeline.hpp",
        "webgpu_compat.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":shader_source",
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
