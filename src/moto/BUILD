package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "moto",
    srcs = [
        "bike.cpp",
        "race.cpp",
        "session.cpp",
        "vmesh_io.cpp",
        "world.cpp",
    ],
    hdrs = [
        "bike.hpp",
        "race.hpp",
        "session.hpp",
        "vmesh.hpp",
        "world.hpp",
    ],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = ["//third_party:glm"],
)
