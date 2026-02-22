"""Set compilation flags for the project."""

load(
    "//settings:copts.bzl",
    "CLANG_WARNINGS",
    "EMSCRIPTEN_DISABLED_WARNINGS",
    "GCC_WARNINGS",
    "MSVC_WARNINGS",
)

MSVC_WERROR = ["/WX"]

CLANG_WERROR = ["-Werror"]

MSVC_FLAGS = (
    ["/std:c++20"] +
    MSVC_WARNINGS +
    MSVC_WERROR
)

CLANG_FLAGS = (
    ["-std=c++20"] +
    CLANG_WARNINGS +
    CLANG_WERROR
)

GCC_FLAGS = (
    ["-std=c++20"] +
    GCC_WARNINGS +
    CLANG_WERROR
)

EMSCRIPTEN_FLAGS = (
    ["-std=c++20"] +
    CLANG_WARNINGS +
    EMSCRIPTEN_DISABLED_WARNINGS
    # Note: Not enabling -Werror for Emscripten to avoid build failures from third-party code
)

# Default project compilation options (native builds)
PROJECT_DEFAULT_COPTS = select({
    "@platforms//cpu:wasm32": EMSCRIPTEN_FLAGS,
    "//:msvc_compiler": MSVC_FLAGS,
    "//:clang-cl_compiler": MSVC_FLAGS,
    "//:clang_compiler": CLANG_FLAGS,
    "//conditions:default": GCC_FLAGS,
})

# Compilation options for tests
PROJECT_TEST_COPTS = select({
    "@platforms//cpu:wasm32": EMSCRIPTEN_FLAGS,
    "//:msvc_compiler": MSVC_FLAGS,
    "//:clang-cl_compiler": MSVC_FLAGS,
    "//:clang_compiler": CLANG_FLAGS,
    "//conditions:default": GCC_FLAGS,
})

# Compilation options for WASM builds (Emscripten)
PROJECT_WASM_COPTS = EMSCRIPTEN_FLAGS
