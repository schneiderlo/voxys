package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "sha256",
    hdrs = ["sha256.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "wreckwater_authority_log",
    srcs = ["log.cpp"],
    hdrs = ["log.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    defines = select({
        "@platforms//cpu:wasm32": ["VOXY_WASM"],
        "//conditions:default": ["VOXY_NATIVE"],
    }),
    includes = [".."],
)

cc_library(
    name = "core",
    srcs = [
        "config.cpp",
        "log.cpp",
        "stb_impl.cpp",
    ],
    hdrs = [
        "config.hpp",
        "log.hpp",
        "timer.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    defines = [
        "GLM_FORCE_DEPTH_ZERO_TO_ONE",
        "GLM_FORCE_LEFT_HANDED",
    ] + select({
        "@platforms//cpu:wasm32": ["VOXY_WASM"],
        "//conditions:default": ["VOXY_NATIVE"],
    }),
    includes = [".."],
    deps = [
        "//third_party:stb",
    ],
)
