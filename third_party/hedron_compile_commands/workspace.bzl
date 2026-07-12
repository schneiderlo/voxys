""" Hedron's Compile Commands Extractor for Bazel

Run `bazel run @hedron_compile_commands//:refresh_all` command to create a
compile_commands.json file for IDE integration (clangd, etc.).
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
    http_archive(
        name = "hedron_compile_commands",
        url = "https://github.com/hedronvision/bazel-compile-commands-extractor/archive/0e990032f3c5a866e72615cf67e5ce22186dcb97.tar.gz",
        strip_prefix = "bazel-compile-commands-extractor-0e990032f3c5a866e72615cf67e5ce22186dcb97",
        # sha256 will be provided by bazel on first run
    )

