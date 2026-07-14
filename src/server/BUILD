package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

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
        "//src/network:network",
        "//src/physics:physics",
    ],
)
