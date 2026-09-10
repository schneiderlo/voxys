package(default_visibility = ["//visibility:public"])
load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_library(
    name = "grid_types",
    hdrs = ["grid_types.hpp"],
    includes = [".."],
)

cc_library(
    name = "box_union",
    srcs = ["box_union.cpp"],
    hdrs = ["box_union.hpp", "orthogonal_geometry.hpp"],
    copts = PROJECT_DEFAULT_COPTS,
    includes = [".."],
    deps = [":grid_types"],
)
