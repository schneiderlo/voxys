package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

exports_files(["wreckwater_client_probe_main.cpp"])

cc_library(
    name = "wreckwater_character_platform_timeline",
    srcs = ["wreckwater_character_platform_timeline.cpp"],
    hdrs = ["wreckwater_character_platform_timeline.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/game:wreckwater_character_movement",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_character_controller",
    srcs = ["wreckwater_character_controller.cpp"],
    hdrs = ["wreckwater_character_controller.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_character_platform_timeline",
        "//src/game:wreckwater_character_movement",
        "//src/network:wreckwater_client_replication",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_character_presentation",
    srcs = ["wreckwater_character_presentation.cpp"],
    hdrs = ["wreckwater_character_presentation.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_character_controller",
        "//src/network:wreckwater_client_replication",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_third_person_camera",
    srcs = ["wreckwater_third_person_camera.cpp"],
    hdrs = ["wreckwater_third_person_camera.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_character_presentation",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_client_visual_clock",
    srcs = ["wreckwater_client_visual_clock.cpp"],
    hdrs = ["wreckwater_client_visual_clock.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//src/network:wreckwater_client_replication"],
)

cc_library(
    name = "wreckwater_graphical_client_loop",
    srcs = ["wreckwater_graphical_client_loop.cpp"],
    hdrs = ["wreckwater_graphical_client_loop.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_character_controller",
        ":wreckwater_character_presentation",
        ":wreckwater_client_visual_clock",
        "//src/network:network",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_application_client",
    srcs = ["wreckwater_application_client.cpp"],
    hdrs = ["wreckwater_application_client.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_graphical_client_loop",
        ":wreckwater_third_person_camera",
        "//src/network:network",
        "//src/physics:physics",
        "//third_party:glm",
    ],
)

cc_library(
    name = "wreckwater_client_probe",
    srcs = ["wreckwater_client_probe.cpp"],
    hdrs = ["wreckwater_client_probe.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//src/network:network"],
)
