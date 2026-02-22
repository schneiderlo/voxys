# voxy Implementation Plan

> **Last Updated:** 2024  
> **Status:** In Progress

This document tracks the implementation progress of the voxy WebGPU terrain renderer.

---

## Phase 1: Project Foundation

### 1.1 Build System Setup ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Sections 2.2, 2.3

- [x] Create root `CMakeLists.txt` with project configuration
- [x] Set up C++20 standard and compiler warnings
- [x] Create `cmake/` directory with helper modules
  - [x] `CompilerWarnings.cmake`
  - [x] `FindDawn.cmake`
- [x] Add build options (`VOXY_BUILD_NATIVE`, `VOXY_BUILD_WASM`, `VOXY_BUILD_TOOLS`)

### 1.2 Directory Structure ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Section 2.2

- [x] Create `src/` directory structure
  - [x] `src/core/` — Utilities, logging, config
  - [x] `src/gpu/` — WebGPU abstraction
  - [x] `src/terrain/` — Heightmap, compression
  - [x] `src/render/` — Rendering pipelines
  - [x] `src/camera/` — Camera systems
  - [x] `src/app/` — Application layer
- [x] Create `shaders/` directory
- [x] Create `assets/` directory
- [x] Create `tools/` directory
- [x] Create `web/` directory for WASM build

### 1.3 Third-Party Dependencies ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Section 4

- [x] Set up `third_party/` directory
- [x] Add/configure GLM (math library)
- [x] Add/configure zstd (compression)
- [x] Add/configure stb_image (image loading)
- [x] Add/configure GLFW (windowing, native only)
- [x] Configure Dawn integration (native only)

### 1.4 Core Utilities ✅
> 📖 **Spec:** [Logging & Configuration](specs/logging-config.md) — Sections 1, 2, 3

- [x] Implement `src/core/log.hpp` / `log.cpp`
  - [x] Log levels (Trace, Debug, Info, Warn, Error, Fatal)
  - [x] Platform-specific output (console, browser console)
  - [x] Scoped logging context
- [x] Implement `src/core/timer.hpp`
  - [x] High-resolution timer
  - [x] Scoped timer for profiling
- [x] Implement `src/core/config.hpp`
  - [x] Config file parsing
  - [x] Command-line argument parsing

---

## Phase 2: WebGPU Foundation

### 2.1 Native WebGPU (wgpu-native) ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Section 3.1

- [x] Implement `src/gpu/context.hpp` / `context.cpp`
  - [x] Adapter request and selection
  - [x] Device creation with required features
  - [x] Surface/swapchain setup (GLFW integration)
  - [x] Error callback handling
- [x] Implement `src/app/window.hpp` / `window.cpp` (GLFW wrapper)
  - [x] Window creation and management
  - [x] Native handle extraction for WebGPU surface

### 2.2 WASM WebGPU (Emscripten) ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Section 3.2

- [x] Add Emscripten toolchain support to CMake
- [x] Implement platform-specific WebGPU initialization
- [x] Create `web/index.html` shell
- [x] Create `web/loader.js` for module loading
- [x] Create `web/style.css` for basic styling

### 2.3 GPU Resource Abstractions ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 5

- [x] Implement buffer creation helpers
- [x] Implement texture creation helpers
- [x] Implement sampler creation helpers
- [x] Implement bind group layout/bind group helpers
- [x] Implement shader module loading

---

## Phase 3: Triangle Terrain Path (Baseline)

### 3.1 Heightmap Loading ✅
> 📖 **Spec:** [Heightmap Compression](specs/heightmap-compression.md) — Sections 1, 6

- [x] Implement `src/terrain/heightmap.hpp` / `heightmap.cpp`
  - [x] Load raw 16-bit heightmaps
  - [x] Load PNG 16-bit heightmaps (via stb_image)
  - [x] Store as `std::vector<uint16_t>`
- [x] Implement GPU texture upload
  - [x] Create R16Uint texture
  - [x] Upload heightmap data

### 3.2 Triangle Path Shader ✅
> 📖 **Spec:** [Shader Specifications](specs/shader-specifications.md) — Section 3  
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 1

- [x] Create `shaders/terrain.wgsl`
  - [x] Vertex shader with tiled instancing
  - [x] Height sampling from texture
  - [x] Normal computation via central differences
  - [x] Fragment shader with basic lighting
- [x] Verify shader compiles on both Dawn and browser

### 3.3 Triangle Path Renderer ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Sections 1, 4, 5.1  
> 📖 **Spec:** [Shader Specifications](specs/shader-specifications.md) — Section 2

- [x] Implement `src/render/triangle_path.hpp` / `triangle_path.cpp`
  - [x] Create index buffer for 64×64 tile
  - [x] Create render pipeline
  - [x] Create bind group layout and bind groups
  - [x] Implement draw calls with instancing
- [x] Implement uniform buffer management
  - [x] `CameraUniforms` struct (C++ side)
  - [x] Buffer creation and updates

### 3.4 Basic Camera ✅
> 📖 **Spec:** [Interaction & Tools](specs/interaction-tools.md) — Sections 1.2, 1.3

- [x] Implement `src/camera/camera.hpp` / `camera.cpp`
  - [x] Camera state (position, orientation, FOV)
  - [x] View matrix computation
  - [x] Projection matrix computation
  - [x] Inverse matrices for ray generation

---

## Phase 4: Input & Camera Controls

### 4.1 Input System ✅
> 📖 **Spec:** [Interaction & Tools](specs/interaction-tools.md) — Section 4

- [x] Implement `src/app/input.hpp` / `input.cpp`
  - [x] Keyboard state tracking
  - [x] Mouse position and delta tracking
  - [x] Mouse button state tracking
  - [x] Mouse capture/release
- [x] GLFW input callbacks (native)
- [x] Emscripten input callbacks (WASM)

### 4.2 Free-Fly Camera Controller ✅
> 📖 **Spec:** [Interaction & Tools](specs/interaction-tools.md) — Sections 1.1, 1.4, 1.5

- [x] Implement `src/camera/controller.hpp` / `controller.cpp`
  - [x] WASD movement
  - [x] Mouse look (yaw/pitch)
  - [x] Speed boost (Shift)
  - [x] Up/Down movement (E/Q or Space/Ctrl)
- [x] Integrate with main loop (via update method)

---

## Phase 5: Heightmap Compression

### 5.1 Compression Algorithm ✅
> 📖 **Spec:** [Heightmap Compression](specs/heightmap-compression.md) — Sections 2, 4

- [x] Implement `src/terrain/compression.hpp` / `compression.cpp`
  - [x] Planar predictor encoding
  - [x] Delta computation with wraparound
  - [x] Byte-stream splitting (low/high)
  - [x] zstd compression of streams
- [x] Implement decompression
  - [x] zstd decompression
  - [x] Byte-stream recombination
  - [x] Planar predictor decoding

### 5.2 LDH File Format ✅
> 📖 **Spec:** [Heightmap Compression](specs/heightmap-compression.md) — Section 3

- [x] Define `LDHHeader` structure
- [x] Implement file writing (encode)
- [x] Implement file reading (decode)
- [x] Add checksum support (optional)

### 5.3 Command-Line Tool ✅
> 📖 **Spec:** [Heightmap Compression](specs/heightmap-compression.md) — Section 5

- [x] Create `tools/ldh_tool/CMakeLists.txt`
- [x] Create `tools/ldh_tool/main.cpp`
  - [x] `compress` command
  - [x] `decompress` command
  - [x] `validate` command
  - [x] `info` command
- [x] Test with sample heightmaps

---

## Phase 6: Mip Chain Generation

### 6.1 CPU Mip Generation ✅
> 📖 **Spec:** [Heightmap Compression](specs/heightmap-compression.md) — Section 6.3

- [x] Implement max-height mip generation (CPU fallback)
  - [x] 2×2 block max reduction
  - [x] Full mip chain for power-of-two textures

### 6.2 GPU Mip Generation ✅
> 📖 **Spec:** [Shader Specifications](specs/shader-specifications.md) — Section 6

- [x] Create `shaders/mip_generate.wgsl`
  - [x] Compute shader for 2×2 max reduction
- [x] Implement mip generation pipeline
  - [x] Per-level dispatch
  - [x] Texture view creation per mip level

### 6.3 Texture with Mip Chain ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 7.2

- [x] Update heightmap texture creation with mip levels
- [x] Upload base level + generate mips
- [x] Verify mip chain correctness

---

## Phase 7: Compute Ray-Caster

### 7.1 Ray-Cast Shader ✅
> 📖 **Spec:** [Shader Specifications](specs/shader-specifications.md) — Section 4  
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Sections 2.4–2.8

- [x] Create `shaders/terrain_raycast.wgsl`
  - [x] Ray generation from pixel coordinates
  - [x] AABB intersection
  - [x] Hierarchical DDA traversal
  - [x] Mip level up/down transitions
  - [x] LOD-based termination
  - [x] Output depth texture

### 7.2 Ray-Cast Renderer ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Sections 2.2, 2.3, 5.2

- [x] Implement `src/render/raycast_path.hpp` / `raycast_path.cpp`
  - [x] Create compute pipeline
  - [x] Create depth output texture (R32Float storage)
  - [x] Create bind groups
  - [x] Implement dispatch
- [x] Integrate with frame loop

### 7.3 Depth Output Verification ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 8

- [x] Add debug visualization (depth as grayscale)
- [x] Verify ray-terrain intersections
- [x] Compare with triangle path results

---

## Phase 8: Fullscreen Blit / Lighting Pass

### 8.1 Blit Shader ✅
> 📖 **Spec:** [Shader Specifications](specs/shader-specifications.md) — Section 5  
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Sections 3.2–3.6

- [x] Create `shaders/ray_blit.wgsl`
  - [x] Fullscreen triangle vertex shader
  - [x] Depth texture sampling
  - [x] Sky rendering (gradient)
  - [x] Position reconstruction
  - [x] Screen-space normal reconstruction
  - [x] Terrain UV computation
  - [x] Lighting (diffuse, ambient, specular)
  - [x] Fog

### 8.2 Blit Renderer ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Sections 3.1, 5.3

- [x] Implement fullscreen blit pipeline
  - [x] Create render pipeline
  - [x] Bind depth texture from ray-cast
  - [x] Bind terrain texture and lightmap
  - [x] Draw fullscreen triangle

### 8.3 Terrain Textures ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 7.3

- [x] Load terrain albedo texture
- [x] Load/generate lightmap texture (placeholder)
- [x] Create samplers

---

## Phase 9: Application Shell

### 9.1 Main Application ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Sections 3.1, 3.2

- [x] Implement `src/app/application.hpp` / `application.cpp`
  - [x] Initialization sequence
  - [x] Main loop (update, render, present)
  - [x] Shutdown/cleanup
- [x] Implement `src/main.cpp`
  - [x] Native entry point
  - [x] WASM entry point (Emscripten main loop)

### 9.2 Render Path Switching ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 6

- [x] Add runtime toggle between triangle and ray-cast paths
- [x] Add command-line flag for path selection (`--render-path <raycast|triangle>`)
- [x] Add keyboard shortcut (F3 toggles between paths)

### 9.3 Configuration ✅
> 📖 **Spec:** [Logging & Configuration](specs/logging-config.md) — Sections 3.1–3.5

- [x] Implement config file loading (`voxy.cfg`)
- [x] Apply config to renderer settings
- [x] Command-line overrides

---

## Phase 10: Polish & Debug Features

### 10.1 Debug Overlays ✅
> 📖 **Spec:** [Logging & Configuration](specs/logging-config.md) — Section 5.1  
> 📖 **Spec:** [Interaction & Tools](specs/interaction-tools.md) — Section 5

- [x] FPS/frame time display
- [x] Camera position display
- [x] Active render path indicator
- [x] Memory usage (if available)

### 10.2 Debug Visualizations ✅
> 📖 **Spec:** [Rendering Architecture](specs/rendering-architecture.md) — Section 8  
> 📖 **Spec:** [Shader Specifications](specs/shader-specifications.md) — Section 7

- [x] Mip level heat map
- [x] Normal visualization
- [x] Depth visualization
- [x] Wireframe mode (triangle path)

### 10.3 Performance Instrumentation ✅
> 📖 **Spec:** [Logging & Configuration](specs/logging-config.md) — Sections 2, 6

- [x] CPU timing per frame section
- [x] GPU timing (timestamp queries, if supported)
- [x] Benchmark mode (automated camera path)

---

## Phase 11: Web Deployment

### 11.1 WASM Optimization ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Sections 3.2.3, 5.2

- [x] Enable `-O3` and LTO for release
- [x] Configure memory limits (`INITIAL_MEMORY`, `MAXIMUM_MEMORY`)
- [x] Enable `ASYNCIFY` for async operations
- [ ] Test on target browsers (Chrome, Firefox, Safari) — *blocked: requires 11.4 WebGPU API compatibility fixes*

### 11.2 Asset Packaging ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Section 9.2

- [x] Configure `--preload-file` for assets
- [x] Compress assets for web delivery (`--use-preload-plugins` for LZ4)
- [x] Implement loading progress indicator

### 11.3 Web UI ✅
> 📖 **Spec:** [Platform & Build](specs/platform-build.md) — Section 3.2.4

- [x] WebGPU availability check
- [x] Fallback message for unsupported browsers
- [x] Fullscreen toggle (F11 key + button)
- [x] Touch input support (dual virtual joysticks)

### 11.4 WebGPU API Compatibility ✅
> 📖 **Note:** Emscripten's emdawnwebgpu port uses a newer WebGPU API

- [x] Update string handling (WGPUStringView instead of const char*)
- [x] Update texture copy types (WGPUTexelCopyTextureInfo, WGPUTexelCopyBufferLayout)
- [x] Update any other API differences between wgpu-native and emdawnwebgpu
- [x] Verify WASM build compiles successfully

---

## Phase 12: Character Controller ✅
> 📖 **Spec:** [Interaction & Tools](specs/interaction-tools.md) — Sections 2.1–2.4

- [x] Ground detection via heightmap sampling
- [x] Gravity and jumping
- [x] Slope handling
- [x] Collision with terrain

## Phase 13: Virtual File System (Engine Core)

> 📖 **Spec:** [Virtual File System](specs/virtual-file-system.md)

### 13.1 Core Infrastructure
- [ ] Create `src/engine/vfs/` directory structure.
- [ ] Add `tl::expected` and `miniz` to `third_party/`.
- [ ] Implement `vfs::Path` (normalization logic).
- [ ] Implement `vfs::Result`, `vfs::Error`, and `vfs::Buffer` (zero-copy abstraction).
- [ ] Implement `vfs::FileSystem` singleton, mount point registry, and priority resolution logic.

### 13.2 Backends Implementation
- [ ] Implement `NativeBackend` (wrapper around `std::filesystem`).
- [ ] Implement `EmscriptenMemFSBackend` (sync access to preloaded files).
- [ ] Implement `ArchiveBackend` (ZIP reading via `miniz` with seeking support).
- [ ] Implement `HttpBackend` (Async-only fetch, LRU caching, browser cache integration).

### 13.3 Integration & Migration
- [ ] Refactor `src/core/config.cpp` to use `vfs::readTextFile`.
- [ ] Refactor `src/gpu/resources.cpp` (`loadShaderModule`) to use VFS.
- [ ] Refactor `src/terrain/heightmap.cpp` to use `vfs::readFile` (binary streaming).
- [ ] Update `entry.cpp` (Native & WASM) to initialize VFS with correct platform mounts.
- [ ] Create `data.pak` generation step in CMake/Bazel for production builds.

---

## Phase 14: SVO Terrain Architecture
> 📖 **Spec:** [SVO Architecture](specs/svo-architecture.md)

### 14.1 Core Data Structures ✅
> 📖 **Spec:** Sections 2.1, 2.2

- [x] Create `src/voxel/` directory
- [x] Implement `src/voxel/svo_types.hpp`
  - [x] Define `SVOInteriorNode` struct (8 bytes: childMask, leafMask, parentIndex, childOffset)
  - [x] Define `SVOLeafBrick` struct (16 bytes: occupancy u64, materialBase, flags, padding)
  - [x] Define `SVOUniforms` struct for GPU upload
  - [x] Define child ordering constants (Morton Z-order within 2×2×2)
- [x] Implement `src/voxel/morton.hpp`
  - [x] `expandBits()` function (10 bits → 30 bits)
  - [x] `compactBits()` function (30 bits → 10 bits)
  - [x] `morton3D()` encode function
  - [x] `inverseMorton3D()` decode function
  - [x] `brickMorton()` for 4×4×4 local coordinates (6 bits)
- [x] Implement `src/voxel/svo_buffers.hpp`
  - [x] SoA buffer container struct (`nodeMasks`, `nodeChildPtrs`, `brickOccLo`, `brickOccHi`, `brickMeta`)
  - [x] GPU buffer upload helpers
  - [x] Buffer alignment utilities (16-byte for WebGPU)

### 14.2 SVO Builder (CPU)
> 📖 **Spec:** Section 9.1, 9.2

- [ ] Rest of the work will be done in a new branch `feature/svo-terrain`
- [ ] Implement `src/voxel/svo_builder.hpp` / `svo_builder.cpp`
  - [ ] **Voxelization stage:**
    - [ ] `voxelizeHeightmap()` — Convert heightmap to sparse voxel list
    - [ ] Surface extraction (mark boundary voxels only)
    - [ ] Optional: mesh voxelization for imported structures
  - [ ] **Brick grouping stage:**
    - [ ] Group sparse voxels into 4×4×4 brick clusters
    - [ ] Encode `uint64_t` occupancy mask per brick
    - [ ] Assign material indices
  - [ ] **Tree construction stage:**
    - [ ] Bottom-up octree building from bricks to root
    - [ ] Compute parentIndex for each interior node
    - [ ] Populate childMask and leafMask
  - [ ] **Flattening stage:**
    - [ ] Convert pointer-based tree to index-based SoA arrays
    - [ ] Morton-order sort for spatial locality
    - [ ] Validate tree integrity (parent-child consistency)

### 14.3 SVO Serialization
> 📖 **Spec:** Section 10

- [ ] Implement `src/voxel/svo_file.hpp` / `svo_file.cpp`
  - [ ] Define `.svo` file format header (64 bytes)
  - [ ] `saveSVO()` — Write SoA buffers to binary file
  - [ ] `loadSVO()` — Read binary file to SoA buffers
  - [ ] Optional: zstd compression for storage
  - [ ] Checksum/validation support

### 14.4 GPU Traversal Shader
> 📖 **Spec:** Section 4

- [ ] Create `shaders/svo_raycast.wgsl`
  - [ ] **64-bit emulation (Critical):**
    - [ ] `checkBit64(lo, hi, idx)` — Branchless bit check
    - [ ] `countBits64(lo, hi)` — Population count
  - [ ] **Node accessors:**
    - [ ] `getChildMask()`, `getLeafMask()`, `getParentIndex()`, `getChildPtr()`
    - [ ] `childExists()`, `childIsLeaf()`
  - [ ] **Ray-box intersection:**
    - [ ] `intersectBox()` — AABB intersection returning tMin/tMax
  - [ ] **Brick DDA traversal:**
    - [ ] `traverseBrick()` — 3D DDA within 4×4×4 brick
    - [ ] Morton index lookup for occupancy check
    - [ ] Return hit position and normal
  - [ ] **Main SVO traversal:**
    - [ ] `traverseSVO()` — Stackless traversal with parent-pointer backtracking
    - [ ] Child coordinate calculation from ray position
    - [ ] Descent/backtrack logic
    - [ ] MAX_ITERATIONS safety limit (256)
  - [ ] **Main entry point:**
    - [ ] Ray generation from camera uniforms
    - [ ] Output depth + material to storage texture

### 14.5 LOD System
> 📖 **Spec:** Section 5

- [ ] Implement distance-based LOD termination
  - [ ] `shouldTerminate(t, depth)` — Pixel-size threshold check
  - [ ] LOD_BIAS uniform for quality control
- [ ] Implement mip-like coarsening
  - [ ] Treat distant interior nodes as solid (skip leaf descent)

### 14.6 Render Path Integration
> 📖 **Spec:** Sections 2.2, 7.2

- [ ] Implement `src/render/svo_path.hpp` / `svo_path.cpp`
  - [ ] GPU buffer creation (6 storage buffers for SoA layout)
  - [ ] Bind group layout and bind group creation
  - [ ] Compute pipeline creation
  - [ ] Uniform buffer for `SVOUniforms`
  - [ ] `dispatch()` method for ray-casting pass
  - [ ] Integration with existing blit pass (shared depth texture format)
- [ ] Add render path toggle
  - [ ] F4 key or command-line flag `--render-path <svo|raycast|triangle>`
  - [ ] Seamless switching during runtime

### 14.7 Testing & Validation

- [ ] Unit tests for Morton code utilities
  - [ ] Roundtrip encode/decode tests
  - [ ] Brick-local Morton validation
- [ ] Unit tests for SVO builder
  - [ ] Simple geometry test (8×8×8 cube)
  - [ ] Heightmap conversion test
  - [ ] Tree integrity validation
- [ ] Visual regression tests
  - [ ] Render simple shapes (sphere, box, terrain patch)
  - [ ] Compare against reference images
- [ ] Performance benchmarks
  - [ ] Iteration count heat map
  - [ ] Memory bandwidth profiling
  - [ ] Compare with heightfield ray-caster

### 14.8 Optional Enhancements (Post-Core)
> 📖 **Spec:** Sections 6, 7, 8

- [ ] **Dynamic updates:**
  - [ ] `carveVoxel()` — Remove voxel from brick
  - [ ] `spawnVoxel()` — Add voxel to brick (allocate if needed)
  - [ ] GPU buffer partial update (dirty region tracking)
- [ ] **Contour enhancement:**
  - [ ] Add optional contour normal buffer
  - [ ] Subvoxel intersection refinement in shader
  - [ ] Smooth normal interpolation
- [ ] **Performance optimizations:**
  - [ ] Beam optimization (2×2 ray coherence)
  - [ ] Wavefront scheduling (direction-sorted rays)
  - [ ] Shared memory node caching in workgroup

### 14.9 Merge svo-terrain into main

- [ ] Merge branch `feature/svo-terrain` into `main`

---

## Future Phases (Post-MVP)

### Phase 15: Terrain Editing
> 📖 **Spec:** [Interaction & Tools](specs/interaction-tools.md) — Sections 3.1–3.6

- [ ] Brush system (raise, lower, smooth, flatten)
- [ ] Ray-cast for brush placement
- [ ] Partial heightmap texture updates
- [ ] Partial mip regeneration
- [ ] **SVO Dynamic Updates (Carve/Spawn)**

### Phase 16: Advanced Features
> 📖 **Spec:** (Future specifications needed)

- [ ] Terrain LOD for triangle path
- [ ] Texture splatting
- [ ] Normal mapping
- [ ] Shadow mapping
- [ ] Water rendering
- [ ] **SVO-Heightmap Hybrid Rendering**

---

## Spec Reference Quick Links

| Spec Document | Primary Coverage |
|---------------|------------------|
| [Rendering Architecture](specs/rendering-architecture.md) | GPU pipeline, uniforms, ray-caster algorithm, lighting |
| [Shader Specifications](specs/shader-specifications.md) | WGSL code for all shaders |
| [Heightmap Compression](specs/heightmap-compression.md) | LDH format, compression algorithm, ldh_tool |
| [Platform & Build](specs/platform-build.md) | CMake, Dawn, Emscripten, dependencies |
| [Interaction & Tools](specs/interaction-tools.md) | Camera, input, character controller, editing |
| [Logging & Configuration](specs/logging-config.md) | Logging, profiling, config files, debug features |

---

## Notes

### Priority Order
1. **Phases 1-3**: Get something rendering on screen
2. **Phase 4**: Make it interactive
3. **Phases 5-6**: Compression and mip chain for large terrains
4. **Phases 7-8**: Main rendering path (ray-caster)
5. **Phases 9-11**: Production-ready application

### Testing Checkpoints
- After Phase 3: Triangle terrain visible with camera orbit
- After Phase 4: Free-fly navigation working
- After Phase 6: Large heightmap loads efficiently
- After Phase 8: Ray-cast path produces correct image
- After Phase 11: Web demo deployable

### Known Risks
- Dawn API changes (track Dawn releases)
- Browser WebGPU compatibility differences
- Memory limits in WASM for large heightmaps
- Mip generation performance on startup
