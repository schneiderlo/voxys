package(default_visibility = ["//visibility:public"])

exports_files(["main.cpp"])

# Main library that aggregates all components
cc_library(
    name = "voxy_core",
    includes = ["."],
    deps = [
        "//src/app:app",
        "//src/camera:camera",
        "//src/core:core",
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
