package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "perf",
    srcs = [
        "benchmark.cpp",
        "gpu_timer.cpp",
    ],
    hdrs = [
        "benchmark.hpp",
        "gpu_timer.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        "//src/core:core",
        "//src/gpu:gpu",
        "//third_party:glm",
    ],
)
