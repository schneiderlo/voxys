# voxy — Technical Specifications

> **Project:** voxy  
> **Description:** WebGPU Terrain Renderer & Heightfield Ray-Casting Prototype  
> **Language:** C++20  
> **Build System:** CMake  
> **Version:** 1.0 (Draft)

---

## Overview

**voxy** is a cross-platform terrain rendering engine featuring:

- **WebGPU** for modern, portable GPU access
- **Hierarchical heightfield ray-casting** for efficient terrain rendering
- **Custom heightmap compression** achieving ~10× compression ratios
- **Dual-target builds**: Native (Dawn) and Web (Emscripten/WASM)

---

## Specification Documents

| Document | Description | Status |
|----------|-------------|--------|
| [Rendering Architecture](specs/rendering-architecture.md) | GPU rendering pipeline, three-stage architecture (triangle path, ray-caster, blit pass), uniform layouts, pipeline states | Draft |
| [Heightmap Compression](specs/heightmap-compression.md) | Custom LDH format, planar predictor, bit-stream splitting, zstd compression, tooling | Draft |
| [Platform & Build](specs/platform-build.md) | CMake configuration, Dawn/Emscripten integration, dependencies, CI/CD | Draft |
| [Interaction & Tools](specs/interaction-tools.md) | Free-fly camera, character controller, terrain editing, input system | Draft |
| [Logging & Configuration](specs/logging-config.md) | Logging system, performance instrumentation, configuration files, debug features | Draft |
| [Shader Specifications](specs/shader-specifications.md) | WGSL shader source for all rendering stages, shared structures, bindings | Draft |
| [Virtual File System](specs/virtual-filesystem.md) | Cross-platform file abstraction, mount points, Native/Emscripten backends, async support | Draft |
| [SVO Architecture](specs/svo-architecture.md) | Sparse Voxel Octree data structures, memory layout, traversal algorithms, and shader logic | Draft |

## Implementation Plan

| Document | Description |
|----------|-------------|
| [**IMPLEMENTATION.md**](IMPLEMENTATION.md) | Detailed task checklist organized by phase, with dependencies and testing checkpoints |

---

## Quick Reference

### Target Platforms

| Platform | Backend | Target Binary |
|----------|---------|---------------|
| macOS | Metal (via Dawn) | `voxy_native` |
| Windows | D3D12/Vulkan (via Dawn) | `voxy_native` |
| Linux | Vulkan (via Dawn) | `voxy_native` |
| Web | WebGPU (native browser) | `voxy_wasm` |

### Key Metrics

| Metric | Value |
|--------|-------|
| Heightmap Resolution | 8192 × 8192 |
| Heightmap Bit Depth | 16-bit |
| Uncompressed Size | 128 MB |
| Compressed Size (LDH) | ~12 MB |
| Compression Ratio | ~10.7× |

### Rendering Paths

```
┌─────────────────────────────────────────────────────────────────┐
│                       RENDER FRAME                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Option A: Ray-Cast Path (Primary)                              │
│  ┌──────────────────┐    ┌──────────────────┐                   │
│  │  Compute Pass:   │───▶│  Fullscreen Blit │───▶ Final Image   │
│  │  Ray-Caster      │    │  + Lighting      │                   │
│  └──────────────────┘    └──────────────────┘                   │
│                                                                 │
│  Option B: Triangle Path (Fallback/Debug)                       │
│  ┌──────────────────────────────────────────┐                   │
│  │  Render Pass: Triangle Mesh Terrain      │───▶ Final Image   │
│  └──────────────────────────────────────────┘                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Directory Structure

```
voxy/
├── SPECS.md                    # This file
├── IMPLEMENTATION.md           # Implementation task checklist
├── specs/                      # Technical specifications
│   ├── rendering-architecture.md
│   ├── heightmap-compression.md
│   ├── platform-build.md
│   ├── interaction-tools.md
│   ├── logging-config.md
│   ├── shader-specifications.md
│   └── virtual-filesystem.md
├── CMakeLists.txt              # Root CMake configuration
├── src/                        # C++ source code
│   ├── core/                   # Utilities, logging, config
│   ├── gpu/                    # WebGPU abstraction
│   ├── terrain/                # Heightmap, compression
│   ├── render/                 # Rendering pipelines
│   ├── camera/                 # Camera systems
│   └── app/                    # Application layer
├── shaders/                    # WGSL shader sources
│   ├── terrain.wgsl
│   ├── terrain_raycast.wgsl
│   ├── ray_blit.wgsl
│   └── mip_generate.wgsl
├── tools/                      # CLI tools (ldh_tool)
├── assets/                     # Heightmaps, textures
├── third_party/                # Dependencies
└── web/                        # HTML/JS for WASM build
```

---

## Build Quick Start

### Native Build (macOS/Windows/Linux)

```bash
# Fetch dependencies
./scripts/fetch_deps.sh

# Configure and build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./voxy_native
```

### WebAssembly Build

```bash
# Ensure Emscripten is installed and activated
source /path/to/emsdk/emsdk_env.sh

# Configure and build
mkdir build-wasm && cd build-wasm
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make

# Serve and test
python -m http.server 8080 --directory .
# Open http://localhost:8080 in a WebGPU-enabled browser
```

---

## Development Milestones

### Milestone 1: Foundation
- [ ] Project scaffolding (CMake, dependencies)
- [ ] WebGPU context initialization (Dawn + Emscripten)
- [ ] Basic window/canvas management
- [ ] Logging infrastructure

### Milestone 2: Triangle Terrain Path
- [ ] Heightmap loading (raw/PNG/EXR)
- [ ] Triangle mesh generation and rendering
- [ ] Basic camera controls
- [ ] Simple lighting

### Milestone 3: Heightmap Compression
- [ ] LDH format encoder/decoder
- [ ] `ldh_tool` CLI
- [ ] Runtime decompression
- [ ] Max-height mip generation

### Milestone 4: Ray-Cast Path
- [ ] Compute shader ray-caster
- [ ] Hierarchical DDA traversal
- [ ] Fullscreen blit/lighting pass
- [ ] Performance optimization

### Milestone 5: Polish & Features
- [ ] Character controller
- [ ] Terrain editing
- [ ] Debug overlays
- [ ] Performance profiling

---

## External Resources

- **WebGPU Specification**: https://www.w3.org/TR/webgpu/
- **WGSL Specification**: https://www.w3.org/TR/WGSL/
- **Dawn (Google's WebGPU)**: https://dawn.googlesource.com/dawn
- **Emscripten**: https://emscripten.org/
- **zstd Compression**: https://github.com/facebook/zstd

---

## License

TBD

---

*Last Updated: 2024*

