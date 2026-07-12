package(default_visibility = ["//visibility:public"])

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

# GLFW (Linux/X11)
cc_library(
    name = "glfw",
    srcs = glob([
        "glfw/src/context.c",
        "glfw/src/init.c",
        "glfw/src/input.c",
        "glfw/src/monitor.c",
        "glfw/src/vulkan.c",
        "glfw/src/window.c",
        "glfw/src/platform.c",
        "glfw/src/null_*.c",
        "glfw/src/x11_*.c",
        "glfw/src/xkb_unicode.c",
        "glfw/src/posix_*.c",
        "glfw/src/linux_*.c",
        "glfw/src/egl_context.c",
        "glfw/src/glx_context.c",
        "glfw/src/osmesa_context.c",
        "glfw/src/internal.h",
        "glfw/src/mappings.h",
        "glfw/src/x11_platform.h",
        "glfw/src/xkb_unicode.h",
        "glfw/src/posix_*.h",
        "glfw/src/linux_*.h",
        "glfw/src/null_*.h",
        "glfw/src/platform.h",
    ]),
    hdrs = glob(["glfw/include/GLFW/*.h"]),
    includes = ["glfw/include"],
    defines = ["_GLFW_X11"],
    deps = [":x11"],
    linkopts = ["-lX11", "-lpthread", "-ldl", "-lrt", "-lm"],
)
