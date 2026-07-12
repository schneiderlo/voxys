package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

exports_files([
    "native/entry.cpp",
    "wasm/entry.cpp",
])

config_setting(
    name = "wasm",
    constraint_values = [
        "@platforms//cpu:wasm32",
    ],
)

cc_library(
    name = "window",
    srcs = select({
        ":wasm": ["wasm/window.cpp"],
        "//conditions:default": ["native/window.cpp"],
    }),
    hdrs = ["window.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["../.."],
    deps = [
        "//src/core:core",
    ] + select({
        ":wasm": [],
        "//conditions:default": [
            "//third_party:glfw",
        ],
    }),
)

cc_library(
    name = "input",
    srcs = select({
        ":wasm": ["wasm/input.cpp"],
        "//conditions:default": ["native/input.cpp"],
    }),
    hdrs = ["input.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["../.."],
    deps = [
        ":window",
        "//src/core:core",
        "//third_party:glm",
    ],
)
