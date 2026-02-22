package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "camera",
    srcs = [
        "camera.cpp",
        "character_controller.cpp",
        "controller.cpp",
    ],
    hdrs = [
        "camera.hpp",
        "character_controller.hpp",
        "controller.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/engine/platform:input",
        "//src/core:core",
        "//src/terrain:terrain",
        "//third_party:glm",
    ],
)
