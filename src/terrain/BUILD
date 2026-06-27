package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "terrain",
    srcs = [
        "biomes.cpp",
        "compression.cpp",
        "decorations.cpp",
        "heightmap.cpp",
        "mip_generator.cpp",
        "textures.cpp",
    ],
    hdrs = [
        "biomes.hpp",
        "compression.hpp",
        "decorations.hpp",
        "heightmap.hpp",
        "mip_generator.hpp",
        "textures.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/render:mip_pipeline",
        "//third_party:zstd",
    ],
)
