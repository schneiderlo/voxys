package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "protocol",
    srcs = ["protocol.cpp"],
    hdrs = [
        "protocol.hpp",
        "session_transport.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    deps = ["//src/physics:wreckwater_protocol_types"],
    includes = [".."],
)

cc_library(
    name = "wreckwater_protocol",
    srcs = ["wreckwater_protocol.cpp"],
    hdrs = ["wreckwater_protocol.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_library(
    name = "wreckwater_client_replication",
    srcs = ["wreckwater_client_replication.cpp"],
    hdrs = ["wreckwater_client_replication.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":wreckwater_protocol"],
)

cc_library(
    name = "deterministic_adversity_transport",
    srcs = ["deterministic_adversity_transport.cpp"],
    hdrs = [
        "deterministic_adversity_transport.hpp",
        "protocol.hpp",
        "session_transport.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//src/physics:physics"],
)

cc_library(
    name = "native_tcp_transport",
    srcs = ["native_tcp_transport.cpp"],
    hdrs = ["native_tcp_transport.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":protocol"],
)

cc_library(
    name = "wreckwater_authority_network",
    deps = [
        ":native_tcp_transport",
        ":wreckwater_protocol",
    ],
)

cc_library(
    name = "network",
    srcs = [
        "authoritative_sandbox.cpp",
        "multiplayer_session.cpp",
        "native_tcp_transport.cpp",
        "protocol.cpp",
        "replication.cpp",
        "transport.cpp",
        "wreckwater_client_runtime.cpp",
    ],
    hdrs = [
        "authoritative_sandbox.hpp",
        "multiplayer_session.hpp",
        "native_tcp_transport.hpp",
        "protocol.hpp",
        "replication.hpp",
        "session_transport.hpp",
        "transport.hpp",
        "wreckwater_client_runtime.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [
        ":wreckwater_client_replication",
        ":wreckwater_protocol",
        ":deterministic_adversity_transport",
        "//src/physics:physics",
    ],
)
