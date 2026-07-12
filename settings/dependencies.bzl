"""Load dependencies needed to compile the project."""

load("//third_party/hedron_compile_commands:workspace.bzl", hedron_compile_commands = "repo")

def initialize_third_party():
    """Load all third-party dependencies."""
    hedron_compile_commands()
