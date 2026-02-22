# Version 0.1.0
workspace(name = "voxys")

# Load third-party dependencies
load("//settings:dependencies.bzl", "initialize_third_party")
initialize_third_party()

# Setup hedron_compile_commands for generating compile_commands.json
load("@hedron_compile_commands//:workspace_setup.bzl", "hedron_compile_commands_setup")
hedron_compile_commands_setup()
load("@hedron_compile_commands//:workspace_setup_transitive.bzl", "hedron_compile_commands_setup_transitive")
hedron_compile_commands_setup_transitive()
load("@hedron_compile_commands//:workspace_setup_transitive_transitive.bzl", "hedron_compile_commands_setup_transitive_transitive")
hedron_compile_commands_setup_transitive_transitive()
load("@hedron_compile_commands//:workspace_setup_transitive_transitive_transitive.bzl", "hedron_compile_commands_setup_transitive_transitive_transitive")
hedron_compile_commands_setup_transitive_transitive_transitive()

new_local_repository(
    name = "emdawnwebgpu",
    # Path relative to workspace root - uses third_party/emsdk installed locally
    path = "third_party/emsdk/upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu",
    build_file_content = """
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "emdawnwebgpu",
    srcs = ["src/webgpu.cpp"],
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    copts = ["-std=c++20"],
)

exports_files(glob(["src/*.js"]))

filegroup(
    name = "js_files",
    srcs = glob(["src/*.js"]),
)
""",
)
