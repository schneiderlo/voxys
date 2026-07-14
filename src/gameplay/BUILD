package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "gameplay",
    srcs = [
        "structural_assembly.cpp",
        "platform_parity.cpp",
        "vertical_slices.cpp",
        "water_dynamics.cpp",
    ],
    hdrs = [
        "gameplay_types.hpp",
        "platform_parity.hpp",
        "structural_assembly.hpp",
        "vertical_slices.hpp",
        "water_dynamics.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
)

cc_binary(
    name = "platform_parity_probe",
    srcs = ["parity_probe_main.cpp"],
    copts = PROJECT_DEFAULT_COPTS,
    deps = [":gameplay"],
)
