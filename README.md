# Voxys

## Build Systems

This project supports multiple build systems. **Bazel** is recommended for development, while **CMake** is available for standard integration.

## Terrain Assets

Voxys can import generated terrain from Terrain Diffusion through the offline
tool at `tools/terrain_diffusion_import.py`.

See [Terrain Diffusion Import](docs/terrain_diffusion.md) for the workflow.

## Python Tooling

Python utilities use `uv`.

```bash
uv sync
uv run python tools/test_terrain_diffusion_import.py
uv run python tools/terrain_diffusion_import.py --help
```

### 1. Bazel (Recommended)

Bazel provides hermetic builds, fast incremental compilation, and easy sanitizer integration.

#### Common Commands
| Action | Command |
|--------|---------|
| **Build Native** | `bazel build //:voxy_native` |
| **Run Native** | `bazel run //:voxy_native` |
| **Run Tests** | `bazel test //tests:voxy_tests` |
| **Build ASM** | `bazel build --config=wasm //:voxy_wasm` |
| **Serve ASM** | `bazel run --config=wasm //tools:serve_wasm` |
| **IDE Setup** | `bazel run @hedron_compile_commands//:refresh_all` |

#### Sanitizers (Debug & Verification)
Enable sanitizers using `--config=<sanitizer>`.

*   **AddressSanitizer (ASan)**: Detects memory errors (buffer overflows, use-after-free).
    ```bash
    bazel test --config=asan //tests:voxy_tests
    bazel run --config=asan //:voxy_native
    ```

*   **ThreadSanitizer (TSan)**: Detects data races.
    > **Note**: TSan requires disabling ASLR.
    ```bash
    # Build first
    bazel build --config=tsan //tests:voxy_tests
    # Run manually with ASLR disabled
    TSAN_OPTIONS="suppressions=.tsan_suppressions" setarch $(uname -m) -R ./bazel-bin/tests/voxy_tests
    ```

*   **UndefinedBehaviorSanitizer (UBSan)**: Detects undefined behavior (integer overflow, null usage, etc.).
    ```bash
    bazel test --config=ubsan //tests:voxy_tests
    ```

*   **MemorySanitizer (MSan)**: Detects uninitialized memory reads.
    > **Note**: Requires an instrumented `libc++` toolchain for full accuracy. System libraries may produce false positives.
    ```bash
    bazel test --config=msan //tests:voxy_tests
    ```

### 2. CMake

Standard build system for the project.

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build -j

# Run
./build/bin/voxy_native

# Test
ctest --test-dir build
```

### 3. Make (Wrapper)

Authentic convenience wrapper around CMake commands.

| Target | Description |
|--------|-------------|
| `make native` | Configure & build native target |
| `make run-native` | Build & run native target |
| `make test` | Run unit tests |
| `make wasm` | Build WASM target (requires EMSDK) |
| `make serve-wasm` | Build & serve WASM target at `localhost:8080` |
| `make clean-native` | Clean native build directory |
| `make clean-wasm` | Clean WASM build directory |

## Git Hooks

This project uses git hooks to ensure code quality. After cloning, run:

```bash
./scripts/setup-hooks.sh
```

### Pre-commit Hook

The pre-commit hook runs `bazel test //...` before each commit. Commits are blocked if any tests fail.
