package(default_visibility = ["//visibility:public"])

exports_files(["main.cpp"])

# Main library that aggregates all components
cc_library(
    name = "voxy_core",
    includes = ["."],
    deps = [
        "//src/app:app",
        "//src/camera:camera",
        "//src/client:wreckwater_application_client",
        "//src/client:wreckwater_character_controller",
        "//src/client:wreckwater_character_presentation",
        "//src/client:wreckwater_client_visual_clock",
        "//src/client:wreckwater_graphical_client_loop",
        "//src/client:wreckwater_third_person_camera",
        "//src/core:core",
        "//src/game:game",
        "//src/gpu:gpu",
        "//src/gameplay:gameplay",
        "//src/network:network",
        "//src/perf:perf",
        "//src/physics:physics",
        "//src/render:render",
        "//src/server:server",
        "//src/terrain:terrain",
        "//src/voxel:voxel",
        "//src/engine/platform:window",
        "//src/engine/platform:input",
    ],
)
