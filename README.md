# Voxys

Voxys is the engine and active game prototype for
[RIDGEBREAK](docs/ridgebreak.md): a physics-driven motocross game about big
air, speed, and surviving your own mistakes, built around a purpose-built
motorcycle simulation, an open mixed-biome world, trick scoring, and
shareable replay clips.
The linked completion contract is intentionally explicit about what is proven
and what remains unfinished. Current engine tests and terrain captures are
foundations; they are not a claim that the 2v2 slice or the 12-player session
is complete.

## Build Systems

This project supports multiple build systems. **Bazel** is recommended for development, while **CMake** is available for standard integration.

## Nix Development Shell

The repo includes a flake and `shell.nix` for local toolchain setup. Use it
before fetching vendored third-party dependencies or building:

```bash
nix-shell
./scripts/fetch_deps.sh
bazel build -c opt //:voxy_native
bazel test //tests:config
```

`nix develop` provides the same toolchain through `flake.nix`. `nix-shell` is
recommended for this asset-heavy checkout because it does not copy the whole
working tree into the Nix store.

The shell provides GCC, Bazelisk-backed `bazel`, CMake, Rust, `uv`, dual-backend
GLFW 3.4, Wayland/X11, Vulkan, and runtime paths for native WebGPU runs. Native
Linux sessions prefer Wayland so fractional desktop scaling does not make the
renderer process an oversized XWayland framebuffer; X11 remains the fallback.

For physical-resolution fullscreen rendering without a display-refresh cap:

```bash
bazel run -c opt //:voxy_native -- --fullscreen --uncapped
```

Use `--vsync` to opt back into FIFO presentation. Neither option changes the
internal resolution scale or scene quality.

The fullscreen performance path keeps the terrain, sky, shadow field, and
depth result cached while the camera is stationary. The animated water stays
live in a separate full-resolution indexed-geometry pass. Five nested 64×64
camera-following clipmap rings and a stretched horizon ring are displaced by
two 256² directional spectrum cascades plus four long swells. The same live
surface drives geometry and material normals. The final ocean path includes exact
dielectric Fresnel/TIR, Beer–Lambert refraction and scattering, filtered HDR
environment reflection, foam, ACES output, and a dedicated underwater
distortion/shaft/particle pass. The cloud environment, foam field, and
infinite-ocean sand/rock material are generated procedurally in code. Moving
the camera refreshes camera-dependent opaque data without switching the ocean
back to the legacy fullscreen material.

Run the five-view native benchmark with:

```bash
bazel run -c opt //:voxy_native -- --fullscreen --benchmark --no-validation
```

Run the deployed, real-interaction browser matrix with:

```bash
node scripts/benchmark_browser.mjs --headed
```

It throws exact 100, 1,000, 5,000, and 10,000 body workloads from one player
view onto the real terrain. It records uncapped headroom and GPU stage
diagnostics, and rejects a profiler-induced throughput collapse. Add
`--modes score,headroom,diagnose` only while the headed Chrome window is
genuinely visible; the runner rejects compositor-throttled RAF.

For the authoritative WASM renderer-throughput gate, open the app in a hardware
WebGPU Chrome instance with `renderThroughput=1`, then run:

```bash
node scripts/benchmark_wasm_render.mjs \
  --port 9333 \
  --expected-width 3440 --expected-height 1454 \
  --warmup-frames 128 --measured-frames 1500 \
  --batch-frames 64 --repeats 3 --minimum-fps 700
```

Historical isolated renderer-throughput runs on an AMD Radeon 890M (RDNA 3,
Chrome 149, Vulkan/ANGLE) measured 944.76, 944.52, and 949.73 FPS at 3440×1454.
All 4,500 measured frames retired through the WebGPU queue. A historical native
five-view run measured 393.6 FPS overall at 3440×1440.

Those numbers are retained only as engine microbenchmark evidence. They use a
physical-size offscreen target or heavily cached views and do not represent a
moving, displayed, composed RIDGEBREAK session. They must not be used for the
current 60 FPS product gate; that gate requires the exact visible scene,
simulation, clients, server, frame-time percentiles, and reference hardware
defined in the RIDGEBREAK completion contract.

Physics is selected through a backend facade:

- `webgpu_soft`: GPU-resident simulation and direct GPU rendering.
- `box3d_reference`: pinned CPU oracle and explicit WebGPU fallback.
- `jolt_legacy`: migration baseline; retained until removal is approved.

The WebGPU path includes terrain and water contacts, dynamic broad/narrow
phase, a deterministic colored Soft Step solver, islands/sleeping, CCD,
asynchronous queries/events, replay, and telemetry. Normal frames do not copy
all body transforms through the CPU.

Useful options:

```text
--physics-backend webgpu
--physics-backend box3d
--physics-backend jolt
--physics-cpu-fallback
--no-physics-cpu-fallback
```

See [the implementation plan](specs/gpu-physics-distributed-world-implementation-plan.md),
[the measured physics report](docs/physics-baseline.md), and
[the product slices](docs/product-vertical-slices.md).

Physics sandbox controls:

- Mouse wheel: select ball, cube, rectangle, capsule, or cylinder.
- First left click: capture the pointer.
- Later left clicks: throw the selected object. Hold for continuous fire.
- Right click: throw 128 selected objects at once.

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
| **Build WASM** | `bazel build --config=wasm //:voxy_wasm` |
| **Serve WASM** | `bazel run --config=wasm //tools:serve_wasm` |
| **IDE Setup** | `bazel run @hedron_compile_commands//:refresh_all` |

### Browser Physics Telemetry

The local WASM server receives a structured optimization snapshot from the
browser once per second. No F1 overlay is required.

```bash
bazel run --config=wasm //tools:serve_wasm

# In another terminal:
bazel run //tools:read_physics_telemetry
bazel run //tools:read_physics_telemetry -- --watch
```

The latest sample is stored in `/tmp/voxys-telemetry.json`. A bounded history
is stored in `/tmp/voxys-telemetry.ndjson`. Set `VOXY_TELEMETRY_FILE` or
`VOXY_TELEMETRY_HISTORY_FILE` to override those paths.

Telemetry is enabled automatically on localhost. Use `?telemetry=0` to disable
it, or `?telemetry=1` to enable it explicitly on another development host.

For reproducible WebGPU stage captures, scaling matrices, A/B confidence
intervals, and regression thresholds, see
[WebGPU performance profiling](docs/performance-profiling.md).

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

The pre-commit hook runs `bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test`
before each commit. Commits are blocked if either suite fails.
