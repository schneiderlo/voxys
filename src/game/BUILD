package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "salvage_preview",
    srcs = ["expedition/salvage_preview.cpp"],
    hdrs = ["expedition/salvage_preview.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/physics:physics",
        "//src/render:primitive_material",
        "//third_party:glm",
    ],
)

cc_library(
    name = "construction_types",
    srcs = ["construction/construction_types.cpp"],
    hdrs = ["construction/construction_types.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "part_catalog",
    srcs = ["construction/part_catalog.cpp"],
    hdrs = ["construction/part_catalog.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":construction_types"],
)

cc_library(
    name = "wreckwater_content_manifest",
    srcs = ["wreckwater_content_manifest.cpp"],
    hdrs = ["wreckwater_content_manifest.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "wreckwater_match",
    srcs = ["wreckwater_match.cpp"],
    hdrs = ["wreckwater_match.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "wreckwater_replay",
    srcs = ["wreckwater_replay.cpp"],
    hdrs = ["wreckwater_replay.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_match",
        "//src/network:wreckwater_protocol",
    ],
)

cc_library(
    name = "wreckwater_replay_playback",
    srcs = ["wreckwater_replay_playback.cpp"],
    hdrs = ["wreckwater_replay_playback.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_replay",
        "//src/network:wreckwater_protocol",
    ],
)

cc_library(
    name = "wreckwater_character_movement",
    srcs = [
        "wreckwater_character_movement.cpp",
        "wreckwater_character_step.cpp",
    ],
    hdrs = [
        "wreckwater_character_movement.hpp",
        "wreckwater_character_step.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_match",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_character_authority_bridge",
    srcs = ["wreckwater_character_authority_bridge.cpp"],
    hdrs = ["wreckwater_character_authority_bridge.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_character_movement",
    ],
)

cc_library(
    name = "wreckwater_character_movement_authority",
    srcs = [
        "wreckwater_character_movement.cpp",
        "wreckwater_character_step.cpp",
    ],
    hdrs = [
        "wreckwater_character_movement.hpp",
        "wreckwater_character_step.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_match",
        "//src/physics:wreckwater_authority_physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_character_authority_bridge_headless",
    srcs = ["wreckwater_character_authority_bridge.cpp"],
    hdrs = ["wreckwater_character_authority_bridge.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":wreckwater_character_movement_authority"],
)

cc_library(
    name = "wreckwater_live_world_authority",
    srcs = ["wreckwater_live_world.cpp"],
    hdrs = ["wreckwater_live_world.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_match",
        "//src/physics:wreckwater_authority_physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_vessel_damage_authority",
    srcs = ["wreckwater_vessel_damage.cpp"],
    hdrs = ["wreckwater_vessel_damage.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/physics:wreckwater_authority_physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_live_world",
    srcs = ["wreckwater_live_world.cpp"],
    hdrs = ["wreckwater_live_world.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_match",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_vessel_damage",
    srcs = ["wreckwater_vessel_damage.cpp"],
    hdrs = ["wreckwater_vessel_damage.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "game",
    deps = [
        ":construction_types",
        ":part_catalog",
        ":salvage_preview",
        ":wreckwater_character_authority_bridge",
        ":wreckwater_character_movement",
        ":wreckwater_content_manifest",
        ":wreckwater_live_world",
        ":wreckwater_match",
        ":wreckwater_replay",
        ":wreckwater_replay_playback",
        ":wreckwater_vessel_damage",
    ],
)

cc_library(
    name = "lego_playground",
    srcs = ["lego_playground.cpp"],
    hdrs = ["lego_playground.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//src/physics:physics", "//src/render:primitive_material", "//third_party:glm"],
)
