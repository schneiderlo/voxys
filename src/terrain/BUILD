package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "lego_surface",
    hdrs = ["lego_surface.hpp"],
    includes = [".."],
    deps = ["//third_party:glm"],
)

cc_library(
    name = "wreckwater_authority_mip_generator",
    srcs = ["mip_generator.cpp"],
    hdrs = ["mip_generator.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//src/core:wreckwater_authority_log"],
)

# The shadow bake sweeps the full 8k heightmap on the CPU at load time.
# Always compile it optimized: at -O0 (fastbuild) it takes ~24 s, at -O2
# about a second.
cc_library(
    name = "shadow_bake",
    srcs = ["shadow_bake.cpp"],
    hdrs = ["shadow_bake.hpp"],
    copts = PROJECT_DEFAULT_COPTS + ["-O2"],
    includes = [".."],
    deps = [
        "//third_party:glm",
    ],
)

cc_library(
    name = "terrain",
    srcs = [
        "authored_cove.cpp",
        "compression.cpp",
        "heightmap.cpp",
        "mip_generator.cpp",
        "textures.cpp",
    ],
    hdrs = [
        "authored_cove.hpp",
        "compression.hpp",
        "heightmap.hpp",
        "lego_layout_cache.hpp",
        "mip_generator.hpp",
        "textures.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":shadow_bake",
        ":lego_surface",
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/render:mip_pipeline",
        "//third_party:glm",
        "//third_party:zstd",
    ],
)
