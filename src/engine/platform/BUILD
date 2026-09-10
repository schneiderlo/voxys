package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

exports_files([
    "native/entry.cpp",
    "native/inspection_motion.hpp",
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

cc_library(
    name = "native_save_store",
    srcs = ["native/save_store.cpp"],
    hdrs = ["native/save_store.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["../.."],
    deps = ["//src/game:save_generation"],
)

cc_library(
    name = "native_save_worker",
    srcs = ["native/save_worker.cpp"],
    hdrs = ["native/save_worker.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["../.."],
    deps = [":native_save_store"],
)

cc_library(
    name = "native_cove_saves",
    srcs = ["native/cove_saves.cpp"],
    hdrs = ["native/cove_saves.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = ["../.."],
    deps = [":native_save_worker", "//src/app:app", "//src/core:sha256", "//src/game:cove_save", "//third_party:tinygltf"],
)
