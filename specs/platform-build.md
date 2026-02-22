# Platform & Build Specification

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

The voxy project targets multiple platforms through a unified CMake build system, supporting both native desktop builds via Dawn and WebAssembly builds via Emscripten.

---

## 1. Language & Standards

### 1.1 C++ Standard

| Requirement | Value |
|-------------|-------|
| Standard | C++20 |
| Compiler Flags | `-std=c++20` / `/std:c++20` |

### 1.2 Required C++20 Features

**From C++17 (still used):**
- `std::optional`
- `std::variant`
- `std::string_view`
- `std::filesystem`
- Structured bindings
- `if constexpr`
- Fold expressions
- Inline variables

**C++20 additions:**
- `std::format` for type-safe formatting
- `std::source_location` for improved diagnostics
- `std::span` for non-owning array views
- Concepts and constraints
- Three-way comparison (`<=>`)
- Designated initializers
- `constexpr` improvements (virtual, try-catch, etc.)
- `[[likely]]` / `[[unlikely]]` attributes
- `std::ranges` library
- Calendar and time zone support in `<chrono>`

### 1.3 Coding Standards

- Use `#pragma once` for header guards
- Prefer `constexpr` where applicable
- Use `[[nodiscard]]` for functions with important return values
- Prefer `enum class` over plain `enum`
- Use `nullptr` instead of `NULL` or `0`

---

## 2. Build System

### 2.1 CMake Requirements

| Requirement | Value |
|-------------|-------|
| Minimum Version | 3.16 |
| Recommended | 3.24+ |

### 2.2 Project Structure

```
voxy/
├── CMakeLists.txt              # Root CMake configuration
├── cmake/
│   ├── FindDawn.cmake          # Dawn discovery module
│   ├── EmscriptenToolchain.cmake
│   └── CompilerWarnings.cmake
├── src/
│   ├── core/                   # Core utilities
│   │   ├── log.hpp
│   │   ├── log.cpp
│   │   ├── timer.hpp
│   │   └── config.hpp
│   ├── gpu/                    # WebGPU abstraction
│   │   ├── context.hpp
│   │   ├── context.cpp
│   │   ├── buffer.hpp
│   │   ├── texture.hpp
│   │   └── pipeline.hpp
│   ├── terrain/                # Terrain systems
│   │   ├── heightmap.hpp
│   │   ├── heightmap.cpp
│   │   ├── compression.hpp
│   │   └── compression.cpp
│   ├── render/                 # Rendering
│   │   ├── renderer.hpp
│   │   ├── renderer.cpp
│   │   ├── triangle_path.hpp
│   │   ├── triangle_path.cpp
│   │   ├── raycast_path.hpp
│   │   └── raycast_path.cpp
│   ├── camera/                 # Camera systems
│   │   ├── camera.hpp
│   │   ├── camera.cpp
│   │   └── controller.hpp
│   ├── app/                    # Application layer
│   │   ├── application.hpp
│   │   ├── application.cpp
│   │   ├── input.hpp
│   │   └── window.hpp
│   └── main.cpp                # Entry point
├── shaders/
│   ├── terrain.wgsl
│   ├── terrain_raycast.wgsl
│   ├── ray_blit.wgsl
│   └── mip_generate.wgsl
├── tools/
│   └── ldh_tool/               # Heightmap compression tool
│       ├── CMakeLists.txt
│       └── main.cpp
├── assets/
│   └── heightmaps/
├── third_party/
│   ├── dawn/                   # Dawn WebGPU (submodule or external)
│   ├── glfw/                   # Window management (native)
│   ├── glm/                    # Math library
│   └── zstd/                   # Compression
└── web/
    ├── index.html
    ├── style.css
    └── loader.js
```

### 2.3 Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(voxy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(VOXY_BUILD_NATIVE "Build native desktop target" ON)
option(VOXY_BUILD_WASM "Build WebAssembly target" OFF)
option(VOXY_BUILD_TOOLS "Build command-line tools" ON)
option(VOXY_ENABLE_VALIDATION "Enable WebGPU validation layers" ON)

# Detect Emscripten
if(EMSCRIPTEN)
    set(VOXY_BUILD_NATIVE OFF)
    set(VOXY_BUILD_WASM ON)
    message(STATUS "Building for WebAssembly with Emscripten")
endif()

# Include modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# Third-party dependencies
add_subdirectory(third_party)

# Core library (shared between targets)
add_subdirectory(src)

# Native target
if(VOXY_BUILD_NATIVE)
    add_executable(voxy_native
        src/main.cpp
    )
    target_link_libraries(voxy_native PRIVATE
        voxy_core
        dawn::webgpu
        glfw
    )
    target_compile_definitions(voxy_native PRIVATE VOXY_NATIVE=1)
    
    if(VOXY_ENABLE_VALIDATION)
        target_compile_definitions(voxy_native PRIVATE VOXY_ENABLE_VALIDATION=1)
    endif()
endif()

# WebAssembly target
if(VOXY_BUILD_WASM)
    add_executable(voxy_wasm
        src/main.cpp
    )
    target_link_libraries(voxy_wasm PRIVATE voxy_core)
    target_compile_definitions(voxy_wasm PRIVATE VOXY_WASM=1)
    
    set_target_properties(voxy_wasm PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "\
            -s USE_WEBGPU=1 \
            -s WASM=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s MAXIMUM_MEMORY=2GB \
            -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
            -s MODULARIZE=1 \
            -s EXPORT_NAME='VoxyModule' \
            --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets \
            --preload-file ${CMAKE_SOURCE_DIR}/shaders@/shaders \
        "
    )
endif()

# Tools
if(VOXY_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# Install rules
install(TARGETS voxy_native RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/voxy/shaders)
install(DIRECTORY assets/ DESTINATION share/voxy/assets)
```

---

## 3. Platform Targets

### 3.1 Native Desktop (voxy_native)

#### 3.1.1 Supported Platforms

| Platform | Backend | Status |
|----------|---------|--------|
| macOS 12+ | Metal | Primary |
| Windows 10+ | D3D12 | Primary |
| Windows 10+ | Vulkan | Secondary |
| Linux | Vulkan | Secondary |

#### 3.1.2 Dawn Integration

Dawn is Google's WebGPU implementation for native platforms.

**Acquisition Options:**

1. **Git Submodule** (Recommended for development)
   ```bash
   git submodule add https://dawn.googlesource.com/dawn third_party/dawn
   ```

2. **Pre-built Binaries** (Recommended for CI)
   - Download from Dawn releases
   - Set `DAWN_ROOT` environment variable

3. **System Package** (Where available)
   - Homebrew: `brew install dawn`
   - vcpkg: `vcpkg install dawn`

**CMake Integration:**
```cmake
# cmake/FindDawn.cmake
if(DEFINED ENV{DAWN_ROOT})
    set(DAWN_ROOT $ENV{DAWN_ROOT})
endif()

find_path(DAWN_INCLUDE_DIR
    NAMES webgpu/webgpu.h
    PATHS ${DAWN_ROOT}/include
)

find_library(DAWN_LIBRARY
    NAMES dawn_native dawn_proc
    PATHS ${DAWN_ROOT}/lib
)

if(DAWN_INCLUDE_DIR AND DAWN_LIBRARY)
    add_library(dawn::webgpu INTERFACE IMPORTED)
    target_include_directories(dawn::webgpu INTERFACE ${DAWN_INCLUDE_DIR})
    target_link_libraries(dawn::webgpu INTERFACE ${DAWN_LIBRARY})
endif()
```

#### 3.1.3 Window Management

GLFW is used for cross-platform window and input handling:

```cmake
# In third_party/CMakeLists.txt
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)
```

#### 3.1.4 Platform-Specific Code

```cpp
// src/app/window.hpp
#pragma once

#if defined(VOXY_NATIVE)
    #include <GLFW/glfw3.h>
    #if defined(__APPLE__)
        #define GLFW_EXPOSE_NATIVE_COCOA
    #elif defined(_WIN32)
        #define GLFW_EXPOSE_NATIVE_WIN32
    #else
        #define GLFW_EXPOSE_NATIVE_X11
    #endif
    #include <GLFW/glfw3native.h>
#endif

namespace voxy {

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();
    
    bool shouldClose() const;
    void pollEvents();
    void* getNativeHandle() const;
    
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    
private:
#if defined(VOXY_NATIVE)
    GLFWwindow* window_ = nullptr;
#endif
    int width_, height_;
};

} // namespace voxy
```

### 3.2 WebAssembly (voxy_wasm)

#### 3.2.1 Requirements

| Component | Version |
|-----------|---------|
| Emscripten | 3.1.50+ |
| Browser | Chrome 113+ / Firefox 115+ / Safari 17+ |

#### 3.2.2 Emscripten Setup

```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Build voxy for WASM
mkdir build-wasm && cd build-wasm
emcmake cmake .. -DVOXY_BUILD_WASM=ON
emmake make
```

#### 3.2.3 Emscripten Flags

```cmake
set(EMSCRIPTEN_FLAGS
    "-s USE_WEBGPU=1"
    "-s WASM=1"
    "-s ALLOW_MEMORY_GROWTH=1"
    "-s MAXIMUM_MEMORY=2GB"
    "-s INITIAL_MEMORY=256MB"
    "-s STACK_SIZE=1MB"
    "-s MODULARIZE=1"
    "-s EXPORT_NAME='VoxyModule'"
    "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']"
    "-s EXPORTED_FUNCTIONS=['_main','_voxy_resize','_voxy_mouse_move']"
    "-s ASYNCIFY"
    "-s ASSERTIONS=1"  # Debug builds only
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND EMSCRIPTEN_FLAGS
        "-s SAFE_HEAP=1"
        "-s STACK_OVERFLOW_CHECK=2"
        "-g4"
        "--source-map-base http://localhost:8080/"
    )
else()
    list(APPEND EMSCRIPTEN_FLAGS
        "-O3"
        "-flto"
        "--closure 1"
    )
endif()
```

#### 3.2.4 HTML Shell

```html
<!-- web/index.html -->
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>voxy - WebGPU Terrain</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <canvas id="voxy-canvas"></canvas>
    <div id="loading">
        <div class="spinner"></div>
        <p id="loading-status">Initializing WebGPU...</p>
    </div>
    <div id="error" style="display: none;">
        <h2>WebGPU Not Supported</h2>
        <p>Your browser does not support WebGPU. Please try:</p>
        <ul>
            <li>Chrome 113+</li>
            <li>Firefox 115+ (with flags enabled)</li>
            <li>Safari 17+</li>
        </ul>
    </div>
    
    <script src="loader.js"></script>
    <script src="voxy_wasm.js"></script>
    <script>
        async function init() {
            if (!navigator.gpu) {
                document.getElementById('loading').style.display = 'none';
                document.getElementById('error').style.display = 'block';
                return;
            }
            
            const canvas = document.getElementById('voxy-canvas');
            const adapter = await navigator.gpu.requestAdapter();
            const device = await adapter.requestDevice();
            const context = canvas.getContext('webgpu');
            
            const module = await VoxyModule({
                canvas: canvas,
                preinitializedWebGPUDevice: device,
            });
            
            document.getElementById('loading').style.display = 'none';
            
            // Handle resize
            function resize() {
                canvas.width = window.innerWidth * devicePixelRatio;
                canvas.height = window.innerHeight * devicePixelRatio;
                module._voxy_resize(canvas.width, canvas.height);
            }
            window.addEventListener('resize', resize);
            resize();
        }
        
        init().catch(console.error);
    </script>
</body>
</html>
```

---

## 4. Dependencies

### 4.1 Required Dependencies

| Dependency | Version | Purpose | Native | WASM |
|------------|---------|---------|--------|------|
| Dawn | latest | WebGPU (native) | ✓ | - |
| GLFW | 3.3+ | Window/Input | ✓ | - |
| GLM | 0.9.9+ | Math | ✓ | ✓ |
| zstd | 1.5+ | Compression | ✓ | ✓ |
| stb_image | 2.28+ | Image loading | ✓ | ✓ |

### 4.2 Third-Party CMake

```cmake
# third_party/CMakeLists.txt

# GLM (header-only)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glm)

# zstd
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(zstd/build/cmake)

# stb (header-only)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# GLFW (native only)
if(NOT EMSCRIPTEN)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(glfw)
endif()
```

### 4.3 Dependency Acquisition Script

```bash
#!/bin/bash
# scripts/fetch_deps.sh

set -e

THIRD_PARTY_DIR="third_party"
mkdir -p $THIRD_PARTY_DIR
cd $THIRD_PARTY_DIR

# GLM
if [ ! -d "glm" ]; then
    git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git
fi

# zstd
if [ ! -d "zstd" ]; then
    git clone --depth 1 --branch v1.5.5 https://github.com/facebook/zstd.git
fi

# GLFW
if [ ! -d "glfw" ]; then
    git clone --depth 1 --branch 3.3.9 https://github.com/glfw/glfw.git
fi

# stb
if [ ! -d "stb" ]; then
    mkdir stb
    curl -o stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
fi

echo "Dependencies fetched successfully!"
```

---

## 5. Build Configurations

### 5.1 Debug Build

```bash
# Native Debug
mkdir build-debug && cd build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DVOXY_ENABLE_VALIDATION=ON
make -j$(nproc)

# WASM Debug  
mkdir build-wasm-debug && cd build-wasm-debug
emcmake cmake .. -DCMAKE_BUILD_TYPE=Debug
emmake make -j$(nproc)
```

**Debug Features:**
- WebGPU validation layers enabled
- Assertions enabled
- Debug symbols included
- Sanitizers (optional)
- Source maps for WASM

### 5.2 Release Build

```bash
# Native Release
mkdir build-release && cd build-release
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# WASM Release
mkdir build-wasm-release && cd build-wasm-release
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc)
```

**Release Optimizations:**
- `-O3` optimization level
- LTO (Link-Time Optimization)
- Validation layers disabled
- Stripped binaries

### 5.3 RelWithDebInfo

For profiling and debugging release builds:

```bash
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

---

## 6. Compiler Warnings

```cmake
# cmake/CompilerWarnings.cmake

function(set_project_warnings target)
    set(CLANG_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wcast-align
        -Wunused
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )
    
    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
    )
    
    set(MSVC_WARNINGS
        /W4
        /w14242
        /w14254
        /w14263
        /w14265
        /w14287
        /we4289
        /w14296
        /w14311
        /w14545
        /w14546
        /w14547
        /w14549
        /w14555
        /w14619
        /w14640
        /w14826
        /w14905
        /w14906
        /w14928
        /permissive-
    )
    
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        set(WARNINGS ${CLANG_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(WARNINGS ${GCC_WARNINGS})
    elseif(MSVC)
        set(WARNINGS ${MSVC_WARNINGS})
    endif()
    
    target_compile_options(${target} PRIVATE ${WARNINGS})
endfunction()
```

---

## 7. Testing & CI

### 7.1 Test Structure

```
tests/
├── CMakeLists.txt
├── test_compression.cpp
├── test_heightmap.cpp
├── test_math.cpp
└── test_raycast.cpp
```

### 7.2 GitHub Actions CI

```yaml
# .github/workflows/ci.yml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-native:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      
      - name: Install Dependencies (Ubuntu)
        if: matrix.os == 'ubuntu-latest'
        run: |
          sudo apt-get update
          sudo apt-get install -y libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
      
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      
      - name: Build
        run: cmake --build build --config Release
      
      - name: Test
        run: ctest --test-dir build --output-on-failure

  build-wasm:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      
      - name: Setup Emscripten
        uses: mymindstorm/setup-emsdk@v13
        with:
          version: 3.1.50
      
      - name: Configure
        run: emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release
      
      - name: Build
        run: cmake --build build-wasm
      
      - name: Upload Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: wasm-build
          path: |
            build-wasm/voxy_wasm.js
            build-wasm/voxy_wasm.wasm
            build-wasm/voxy_wasm.data
```

---

## 8. Development Tools

### 8.1 Shader Compilation

WGSL shaders are loaded at runtime, but can be validated offline:

```bash
# Using Dawn's tint compiler
tint --validate shaders/*.wgsl

# Using naga (Rust-based)
naga shaders/*.wgsl
```

### 8.2 GPU Debugging

| Platform | Tool |
|----------|------|
| macOS | Xcode GPU Frame Debugger |
| Windows | PIX, RenderDoc (Vulkan) |
| Linux | RenderDoc |
| Web | Chrome DevTools → Performance |

### 8.3 Address Sanitizer

```cmake
if(VOXY_ENABLE_ASAN)
    target_compile_options(voxy_native PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(voxy_native PRIVATE -fsanitize=address)
endif()
```

---

## 9. Distribution

### 9.1 Native Packaging

**macOS App Bundle:**
```cmake
if(APPLE)
    set_target_properties(voxy_native PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/platform/macos/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME "Voxy"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()
```

**Windows Installer:**
- Use CPack with NSIS or WiX
- Bundle Visual C++ Redistributable

### 9.2 Web Deployment

```bash
# Build optimized WASM
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web

# Copy to deployment directory
mkdir -p dist
cp build-web/voxy_wasm.{js,wasm,data} dist/
cp web/{index.html,style.css,loader.js} dist/

# Serve locally for testing
python -m http.server 8080 --directory dist
```

**Hosting Requirements:**
- Serve with correct MIME types:
  - `.wasm`: `application/wasm`
  - `.js`: `application/javascript`
- HTTPS required for SharedArrayBuffer (if used)
- Cross-Origin headers for multi-threading

