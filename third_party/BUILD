package(default_visibility = ["//visibility:public"])

# Jolt Physics. Keep the instruction set portable on native builds and use
# standardized wasm128 SIMD in browsers. The project uses Jolt's single-threaded
# scheduler on both targets, so WASM does not require pthreads/SharedArrayBuffer.
cc_library(
    name = "jolt",
    srcs = glob(["JoltPhysics/Jolt/**/*.cpp"]),
    hdrs = glob([
        "JoltPhysics/Jolt/**/*.h",
        "JoltPhysics/Jolt/**/*.inl",
    ]),
    copts = select({
        "@platforms//cpu:wasm32": ["-msimd128", "-msse4.2", "-w"],
        "//conditions:default": ["-msse2", "-w"],
    }),
    includes = ["JoltPhysics"],
    linkopts = select({
        "@platforms//cpu:wasm32": [],
        "//conditions:default": ["-pthread"],
    }),
)

# Box3D 3D CPU reference backend. This checkout is pinned by fetch_deps.sh.
# Keep floating-point contraction disabled to match its determinism contract.
cc_library(
    name = "box3d",
    srcs = glob(["box3d/src/*.c"]),
    hdrs = glob([
        "box3d/include/box3d/*.h",
        "box3d/src/*.h",
        "box3d/src/*.inl",
    ]),
    copts = select({
        "@platforms//cpu:wasm32": [
            "-ffp-contract=off",
            "-msimd128",
            "-msse2",
            "-std=gnu17",
            "-w",
        ],
        "//conditions:default": [
            "-ffp-contract=off",
            "-msse2",
            "-std=gnu17",
            "-w",
        ],
    }),
    includes = [
        "box3d/include",
        "box3d/src",
    ],
    linkopts = select({
        "@platforms//cpu:wasm32": [],
        "//conditions:default": ["-lm", "-pthread"],
    }),
)

# GLM
cc_library(
    name = "glm",
    hdrs = glob(["glm/glm/**/*.hpp", "glm/glm/**/*.h", "glm/glm/**/*.inl"]),
    includes = ["glm"],
)

# STB
cc_library(
    name = "stb",
    hdrs = glob(["stb/*.h"]),
    includes = ["stb"],
)

# TinyEXR
cc_library(
    name = "tinyexr",
    srcs = ["tinyexr/miniz_full.c"],
    hdrs = ["tinyexr/tinyexr.h", "tinyexr/miniz.h"],
    includes = ["tinyexr"],
)

# WGPU Native
# WGPU (Platform-aware abstraction)
cc_library(
    name = "wgpu",
    srcs = select({
        "@platforms//cpu:wasm32": [],
        "//conditions:default": ["wgpu-native/dist/lib/libwgpu_native.so"],
    }),
    hdrs = select({
        "@platforms//cpu:wasm32": [],
        "//conditions:default": glob(["wgpu-native/dist/include/**/*.h"]),
    }),
    includes = select({
        "@platforms//cpu:wasm32": [],
        "//conditions:default": [
            "wgpu-native/dist/include", 
            "wgpu-native/dist/include/webgpu"
        ],
    }),
    deps = select({
        "@platforms//cpu:wasm32": ["@emdawnwebgpu//:emdawnwebgpu"],
        "//conditions:default": [],
    }),
)

# ZSTD
cc_library(
    name = "zstd",
    srcs = glob([
        "zstd/lib/common/*.c", "zstd/lib/common/*.h",
        "zstd/lib/compress/*.c", "zstd/lib/compress/*.h",
        "zstd/lib/decompress/*.c", "zstd/lib/decompress/*.h",
        "zstd/lib/dictBuilder/*.c", "zstd/lib/dictBuilder/*.h",
    ]),
    hdrs = ["zstd/lib/zstd.h", "zstd/lib/zdict.h", "zstd/lib/zstd_errors.h"],
    includes = ["zstd/lib", "zstd/lib/common"],
    defines = ["ZSTD_MULTITHREAD", "ZSTD_DISABLE_ASM"],
)

# X11 Headers (Hermetic)
cc_library(
    name = "x11",
    hdrs = glob(["x11_headers/include/**/*.h"]),
    includes = ["x11_headers/include"],
)

# GLFW 3.4 supplied by the native toolchain.  The Nix shell provides a build
# with both Wayland and X11 backends; keeping the vendored headers here also
# preserves Bazel include paths for non-Nix system installations.
cc_library(
    name = "glfw",
    hdrs = glob(["glfw/include/GLFW/*.h"]),
    includes = ["glfw/include"],
    deps = [":x11"],
    linkopts = ["-lglfw"],
)
