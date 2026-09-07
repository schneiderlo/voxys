package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "app",
    srcs = [
        "application.cpp",
        "debug_overlay.cpp",
    ],
    hdrs = [
        "application.hpp",
        "debug_overlay.hpp",
        "salvage_preview_readback.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/engine/platform:input",
        "//src/engine/platform:window",
        "//src/camera:camera",
        "//src/client:wreckwater_application_client",
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/game:lego_playground",
        "//src/game:salvage_preview",
        "//src/moto:moto",
        "//src/network:native_tcp_transport",
        "//src/perf:perf",
        "//src/physics:physics",
        "//src/render:render",
        "//src/terrain:terrain",
        "//third_party:glm",
        "//third_party:stb",
    ],
)
