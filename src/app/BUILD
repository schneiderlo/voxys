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
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/engine/platform:input",
        "//src/engine/platform:window",
        "//src/camera:camera",
        "//src/core:core",
        "//src/gpu:gpu",
        "//src/perf:perf",
        "//src/render:render",
        "//src/terrain:terrain",
        "//third_party:glm",
        "//third_party:stb",
    ],
)
