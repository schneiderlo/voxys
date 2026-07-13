package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "physics",
    srcs = ["physics_world.cpp"],
    hdrs = ["physics_world.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
        "//third_party:glm",
        "//third_party:jolt",
    ],
)
