package(default_visibility = ["//visibility:public"])

load("//settings:configure_copts.bzl", "PROJECT_DEFAULT_COPTS")

cc_binary(
    name = "physics_benchmark",
    srcs = ["main.cpp"],
    copts = PROJECT_DEFAULT_COPTS + ["-DVOXY_NATIVE=1"],
    deps = ["//src/physics:physics"],
)

[cc_test(
    name = "smoke_" + backend,
    srcs = ["main.cpp"],
    args = [
        "--backend", backend,
        "--scene", "junkyard",
        "--bodies", "24",
        "--warmup", "1",
        "--frames", "2",
        "--format", "json",
    ],
    copts = PROJECT_DEFAULT_COPTS + ["-DVOXY_NATIVE=1"],
    deps = ["//src/physics:physics"],
) for backend in ["jolt", "box3d"]]
