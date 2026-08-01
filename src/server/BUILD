package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

exports_files(["wreckwater_server_main.cpp"])

cc_library(
    name = "wreckwater_authority_runtime",
    srcs = ["wreckwater_authority_runtime.cpp"],
    hdrs = ["wreckwater_authority_runtime.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/game:wreckwater_character_authority_bridge",
        "//src/game:wreckwater_live_world",
        "//src/game:wreckwater_replay",
        "//src/game:wreckwater_vessel_damage",
        "//src/gpu:gpu",
        "//src/network:network",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_authority_runtime_headless",
    srcs = ["wreckwater_authority_runtime.cpp"],
    hdrs = ["wreckwater_authority_runtime.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/game:wreckwater_character_authority_bridge_headless",
        "//src/game:wreckwater_live_world_authority",
        "//src/game:wreckwater_replay",
        "//src/game:wreckwater_vessel_damage_authority",
        "//src/gpu:wreckwater_authority_gpu",
        "//src/network:wreckwater_authority_network",
        "//src/physics:wreckwater_authority_physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_headless_authority",
    deps = [
        ":wreckwater_authority_runtime_headless",
        "//src/gpu:wreckwater_authority_gpu",
        "//src/network:wreckwater_authority_network",
        "//src/physics:wreckwater_authority_physics",
    ],
)

cc_library(
    name = "server",
    srcs = [
        "native_gpu_backend.cpp",
        "world_coordinator.cpp",
    ],
    hdrs = [
        "native_gpu_backend.hpp",
        "world_coordinator.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_authority_runtime",
        "//src/network:network",
        "//src/physics:physics",
    ],
)
