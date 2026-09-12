package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "assembly_fracture",
    srcs = ["construction/assembly_fracture.cpp"],
    hdrs = ["construction/assembly_fracture.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":compiled_assembly", "//src/physics:rigid_mass_frame"],
)

cc_library(
    name = "build_refit",
    srcs = ["construction/build_refit.cpp"],
    hdrs = ["construction/build_refit.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":build_model"],
)

cc_library(
    name = "cove_build",
    srcs = ["expedition/cove_build.cpp"],
    hdrs = ["expedition/cove_build.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cove_boat", ":build_refit", ":session_transactions"],
)

cc_library(
    name = "cove_harbor_state",
    hdrs = ["expedition/cove_harbor_state.hpp"],
    includes = [".."],
)

cc_library(
    name = "cove_harbor_lift",
    srcs = ["expedition/cove_harbor_lift.cpp"],
    hdrs = ["expedition/cove_harbor_lift.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cove_boat", ":cove_player", ":cove_harbor_state", "//src/physics:physics"],
)

cc_library(
    name = "cove_harbor_runtime",
    srcs = ["expedition/cove_harbor_runtime.cpp"],
    hdrs = ["expedition/cove_harbor_runtime.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cove_harbor_lift", "//src/physics:physics"],
)

cc_library(
    name = "cove_restore",
    srcs = ["expedition/cove_restore.cpp"],
    hdrs = ["expedition/cove_restore.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cove_save", ":cove_build", ":cove_player", ":cove_rigid_roots", ":cove_harbor_lift", "//src/physics:physics"],
)

cc_library(
    name = "workshop_camera",
    srcs = ["expedition/workshop_camera.cpp"],
    hdrs = ["expedition/workshop_camera.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//third_party:glm"],
)

cc_library(
    name = "cove_workshop",
    srcs = ["expedition/cove_workshop.cpp"],
    hdrs = ["expedition/cove_workshop.hpp", "expedition/brick_paint.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cove_boat", ":cove_player", ":workshop_camera"],
)

cc_library(
    name = "cove_boat",
    srcs = ["expedition/cove_boat.cpp"],
    hdrs = ["expedition/cove_boat.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":fixture_registry", ":compiled_assembly", "//src/physics:authored_shape", "//src/physics:physics"],
)

cc_library(
    name = "cove_rigid_roots",
    srcs = ["expedition/cove_rigid_roots.cpp"],
    hdrs = ["expedition/cove_rigid_roots.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cove_boat", ":cove_player", ":assembly_fracture", ":build_refit", ":orthogonal_coverage", "//src/physics:physics"],
)

cc_library(
    name = "cove_mechanisms",
    hdrs = ["expedition/cove_mechanisms.hpp"],
    includes = [".."],
    deps = ["//src/physics:physics"],
)

cc_library(
    name = "cove_water_clock",
    hdrs = ["expedition/cove_water_clock.hpp"],
    includes = [".."],
)

cc_library(
    name = "cove_player",
    srcs = ["expedition/cove_player.cpp"],
    hdrs = ["expedition/cove_player.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":fixture_registry"],
)

cc_library(
    name = "fixture_limits",
    hdrs = ["assets/fixture_limits.hpp"],
    includes = [".."],
)

cc_library(
    name = "fixture_registry",
    srcs = ["assets/fixture_registry.cpp"],
    hdrs = ["assets/fixture_registry.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":fixture_limits", ":cooked_part_directory", ":build_model", "//src/core:sha256", "//third_party:tinygltf"],
)

cc_library(
    name = "cooked_part_directory",
    srcs = ["assets/cooked_part_directory.cpp"],
    hdrs = ["assets/cooked_part_directory.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":cooked_part_bundle"],
)

cc_library(
    name = "session_events",
    srcs = ["expedition/session_events.cpp"],
    hdrs = ["expedition/session_events.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":session_transactions"],
)

cc_library(
    name = "session_transactions",
    srcs = ["expedition/session_transactions.cpp"],
    hdrs = ["expedition/session_transactions.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":construction_types", ":part_catalog", ":build_model", ":build_refit"],
)

cc_library(
    name = "rigid_prefab",
    srcs = ["assets/rigid_prefab.cpp"],
    hdrs = ["assets/rigid_prefab.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":construction_types", "//src/moto:moto", "//third_party:glm"],
)

cc_library(
    name = "cooked_part_bundle",
    srcs = ["assets/cooked_part_bundle.cpp"],
    hdrs = ["assets/cooked_part_bundle.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":gameplay_sidecar", ":rigid_prefab", "//src/core:sha256", "//src/moto:moto", "//third_party:tinygltf"],
)

cc_library(
    name = "gameplay_sidecar",
    srcs = ["assets/gameplay_sidecar.cpp"],
    hdrs = ["assets/gameplay_sidecar.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":construction_types", ":part_catalog", "//third_party:tinygltf"],
)

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
    deps = ["//src/geometry:grid_types"],
)

cc_library(
    name = "session_save",
    srcs = ["expedition/session_save.cpp"],
    hdrs = ["expedition/session_save.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":game_session", "//src/core:sha256"],
)

cc_library(
    name = "cove_save",
    srcs = ["expedition/cove_save.cpp"],
    hdrs = ["expedition/cove_save.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":fixture_limits", ":session_save", ":cove_harbor_state", ":assembly_compiler", "//src/core:sha256"],
)

cc_library(
    name = "game_session",
    srcs = ["expedition/game_session.cpp", "expedition/session_recovery.cpp"],
    hdrs = ["expedition/game_session.hpp", "expedition/session_recovery.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":session_events", ":session_transactions", ":construction_types", ":part_catalog", ":build_model"],
)

cc_library(
    name = "compiled_assembly",
    srcs = ["construction/compiled_assembly.cpp"],
    hdrs = ["construction/compiled_assembly.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":assembly_functions", "//src/core:sha256"],
)

cc_library(
    name = "assembly_functions",
    srcs = ["construction/assembly_functions.cpp"],
    hdrs = ["construction/assembly_functions.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":assembly_buoyancy"],
)

cc_library(
    name = "assembly_buoyancy",
    srcs = ["construction/assembly_buoyancy.cpp"],
    hdrs = ["construction/assembly_buoyancy.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":assembly_collision", ":orthogonal_coverage"],
)

cc_library(
    name = "orthogonal_coverage",
    srcs = ["construction/orthogonal_coverage.cpp"],
    hdrs = ["construction/orthogonal_coverage.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":orthogonal_union"],
)

cc_library(
    name = "orthogonal_union",
    hdrs = ["construction/orthogonal_union.hpp", "construction/orthogonal_geometry.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":part_catalog", "//src/geometry:box_union"],
)

cc_library(
    name = "assembly_collision",
    srcs = ["construction/assembly_collision.cpp"],
    hdrs = ["construction/assembly_collision.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":assembly_compiler", ":orthogonal_union"],
)

cc_library(
    name = "assembly_compiler",
    srcs = ["construction/assembly_compiler.cpp"],
    hdrs = ["construction/assembly_compiler.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":build_model"],
)

cc_library(
    name = "build_model",
    srcs = ["construction/build_model.cpp"],
    hdrs = ["construction/build_model.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":construction_types", ":part_catalog", "//src/core:sha256"],
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
    deps = ["//src/core:sha256"],
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
        ":cooked_part_directory",
        ":cooked_part_bundle",
        ":rigid_prefab",
        ":gameplay_sidecar",
        ":game_session",
        ":session_events",
        ":build_model",
        ":assembly_compiler",
        ":assembly_collision",
        ":assembly_buoyancy",
        ":assembly_functions",
        ":compiled_assembly",
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

cc_library(
    name = "save_generation",
    srcs = ["expedition/save_generation.cpp"],
    hdrs = ["expedition/save_generation.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":construction_types", "//src/core:sha256"],
)
