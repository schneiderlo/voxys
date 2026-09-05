package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "frustum",
    hdrs = ["frustum.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//third_party:glm"],
)

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
    name = "water_clipmap_mesh",
    srcs = ["water_clipmap_mesh.cpp"],
    hdrs = ["water_clipmap_mesh.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "water_simulation",
    srcs = ["water_simulation.cpp"],
    hdrs = ["water_simulation.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
        "//src/gpu:gpu",
        "//third_party:glm",
    ],
)

cc_library(
    name = "primitive_instance_packing",
    srcs = ["primitive_instance_packing.cpp"],
    hdrs = ["primitive_instance_packing.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "primitive_culling",
    srcs = ["primitive_culling.cpp"],
    hdrs = ["primitive_culling.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":frustum",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "render",
    srcs = [
        "blit_path.cpp",
        "debug_visualizer.cpp",
        "primitive_gpu_culling.cpp",
        "raycast_path.cpp",
        "primitive_path.cpp",
        "triangle_path.cpp",
    ],
    hdrs = [
        "blit_path.hpp",
        "periodic_gradient_lut.hpp",
        "debug_visualizer.hpp",
        "primitive_gpu_culling.hpp",
        "primitive_material.hpp",
        "raycast_path.hpp",
        "primitive_path.hpp",
        "triangle_path.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":mip_pipeline",
        ":frustum",
        ":primitive_culling",
        ":primitive_instance_packing",
        ":water_clipmap_mesh",
        ":water_simulation",
        "//src/camera:camera",
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/physics:physics",
        "//src/terrain:terrain",
    ],
)
