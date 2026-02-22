package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "voxel",
    srcs = [
        "svo_buffers.cpp",
    ],
    hdrs = [
        "morton.hpp",
        "svo_buffers.hpp",
        "svo_types.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
        "//third_party:glm",
        "//third_party:wgpu",
    ],
)

# Header-only library for modules that only need types/morton (no GPU dependency)
cc_library(
    name = "voxel_types",
    hdrs = [
        "morton.hpp",
        "svo_types.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//third_party:glm",
    ],
)
