package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_binary(
    name = "ldh_tool",
    srcs = ["main.cpp"],
    copts = PROJECT_DEFAULT_COPTS,
    deps = [
        "//src/core:core",
        "//src/terrain:terrain",
        "//third_party:stb",
        "//third_party:tinyexr",
        "//third_party:zstd",
    ],
)
