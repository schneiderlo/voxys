package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "network",
    srcs = [
        "authoritative_sandbox.cpp",
        "protocol.cpp",
        "replication.cpp",
        "transport.cpp",
    ],
    hdrs = [
        "authoritative_sandbox.hpp",
        "protocol.hpp",
        "replication.hpp",
        "transport.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//src/physics:physics"],
)
