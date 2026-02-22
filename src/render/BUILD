package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "mip_pipeline",
    srcs = ["mip_pipeline.cpp"],
    hdrs = ["mip_pipeline.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
        "//src/gpu:gpu",
    ],
)

cc_library(
    name = "render",
    srcs = [
        "blit_path.cpp",
        "debug_visualizer.cpp",
        "raycast_path.cpp",
        "triangle_path.cpp",
    ],
    hdrs = [
        "blit_path.hpp",
        "debug_visualizer.hpp",
        "frustum.hpp",
        "raycast_path.hpp",
        "triangle_path.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":mip_pipeline",
        "//src/camera:camera",
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/terrain:terrain",
    ],
)
