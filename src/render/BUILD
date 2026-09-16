package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "salvage_asset_fixture",
    srcs = ["salvage_asset_fixture.cpp", "inspection_guides.cpp", "cove_dock_markings.cpp"],
    hdrs = ["salvage_asset_fixture.hpp", "inspection_guides.hpp", "cove_dock_markings.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":render", "//src/game:fixture_limits", "//src/game:cooked_part_bundle", "//src/game:fixture_registry", "//src/game:rigid_animation", "//src/game:cove_environment", "//third_party:glm"],
)

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
        "cove_hud.cpp",
        "adventure_hud.cpp",
        "opaque_scene.cpp",
        "debug_visualizer.cpp",
        "environment_lighting.cpp",
        "mesh_path.cpp",
        "primitive_gpu_culling.cpp",
        "raycast_path.cpp",
        "primitive_path.cpp",
        "triangle_path.cpp",
    ],
    hdrs = [
        "blit_path.hpp",
        "cove_hud.hpp",
        "adventure_hud.hpp",
        "generated/adventure_piece_thumbnails.hpp",
        "cove_recovery_guidance.hpp",
        "cove_hud_font.inc",
        "opaque_scene.hpp",
        "periodic_gradient_lut.hpp",
        "debug_visualizer.hpp",
        "environment_lighting.hpp",
        "mesh_path.hpp",
        "scene_shadows.hpp",
        "primitive_gpu_culling.hpp",
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
        ":primitive_material",
        ":water_clipmap_mesh",
        ":water_simulation",
        "//src/camera:camera",
        "//src/game:adventure_core",
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/moto:moto",
        "//src/physics:physics",
        "//src/terrain:terrain",
    ],
)

cc_library(
    name = "primitive_material",
    hdrs = ["primitive_material.hpp"],
    includes = [".."],
    deps = ["//third_party:glm"],
)

cc_library(
    name = "cove_effects_path",
    srcs = ["cove_effects_path.cpp"],
    hdrs = ["cove_effects_path.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":render", ":water_simulation", "//src/game:cove_effects", "//src/gpu:gpu"],
)
