# ═══════════════════════════════════════════════════════════════════════════════
# third_party/CMakeLists.txt
# ═══════════════════════════════════════════════════════════════════════════════
# Third-party dependency configuration for voxy.
# Run scripts/fetch_deps.sh to download dependencies before building.
# ═══════════════════════════════════════════════════════════════════════════════

message(STATUS "Configuring third-party dependencies...")

# ─────────────────────────────────────────────────────────────────────────────
# GLM - OpenGL Mathematics (header-only)
# https://github.com/g-truc/glm
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glm/glm/glm.hpp")
    add_library(glm INTERFACE)
    target_include_directories(glm SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/glm")
    
    # GLM configuration
    target_compile_definitions(glm INTERFACE
        GLM_FORCE_DEPTH_ZERO_TO_ONE      # Vulkan/Metal/D3D depth range [0, 1]
        GLM_FORCE_LEFT_HANDED            # Consistent with WebGPU coordinate system
        GLM_ENABLE_EXPERIMENTAL          # Enable experimental features (hash, etc.)
    )
    
    message(STATUS "  GLM: Found")
else()
    message(STATUS "  GLM: Not found - run scripts/fetch_deps.sh")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# zstd - Fast Compression Library
# https://github.com/facebook/zstd
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zstd/build/cmake/CMakeLists.txt")
    # Disable zstd programs and tests
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    
    add_subdirectory(zstd/build/cmake)
    
    message(STATUS "  zstd: Found")
else()
    message(STATUS "  zstd: Not found - run scripts/fetch_deps.sh")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# stb - Single-file public domain libraries (header-only)
# https://github.com/nothings/stb
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stb/stb_image.h")
    add_library(stb INTERFACE)
    target_include_directories(stb SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
    
    message(STATUS "  stb: Found")
else()
    message(STATUS "  stb: Not found - run scripts/fetch_deps.sh")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# tinyexr - OpenEXR image loader (header-only, but needs miniz for compression)
# https://github.com/syoyo/tinyexr
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tinyexr/tinyexr.h")
    # tinyexr is header-only but requires miniz for ZIP compression
    # miniz is included as miniz_full.c in the tinyexr directory
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tinyexr/miniz_full.c")
        add_library(miniz STATIC "${CMAKE_CURRENT_SOURCE_DIR}/tinyexr/miniz_full.c")
        target_include_directories(miniz PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tinyexr")
        set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
        
        add_library(tinyexr INTERFACE)
        target_include_directories(tinyexr SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/tinyexr")
        target_link_libraries(tinyexr INTERFACE miniz)
        
        message(STATUS "  tinyexr: Found (with miniz)")
    else()
        # Fallback to header-only without ZIP compression support
        add_library(tinyexr INTERFACE)
        target_include_directories(tinyexr SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/tinyexr")
        
        message(STATUS "  tinyexr: Found (without miniz - ZIP compression disabled)")
    endif()
else()
    message(STATUS "  tinyexr: Not found")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# GLFW - Window/Input Library (native only)
# https://github.com/glfw/glfw
# ─────────────────────────────────────────────────────────────────────────────
if(NOT EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/CMakeLists.txt")
        # Disable GLFW extras
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        
        # On Linux, prefer X11 over Wayland to reduce dependencies
        # Set GLFW_BUILD_WAYLAND=OFF if xkbcommon is not available
        if(UNIX AND NOT APPLE)
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(XKBCOMMON QUIET xkbcommon)
                if(NOT XKBCOMMON_FOUND)
                    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
                    message(STATUS "  GLFW: Wayland disabled (xkbcommon not found)")
                endif()
            endif()
        endif()
        
        add_subdirectory(glfw)
        
        message(STATUS "  GLFW: Found")
    else()
        message(STATUS "  GLFW: Not found - run scripts/fetch_deps.sh")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# wgpu-native - WebGPU Implementation (native only)
# https://github.com/gfx-rs/wgpu-native
# ─────────────────────────────────────────────────────────────────────────────
# wgpu-native is a Rust-based WebGPU implementation that's easier to build
# than Dawn and fully compliant with the WebGPU specification.
#
# Integration options:
#   1. Build from source using scripts/build_webgpu.sh (recommended)
#   2. Set WGPU_ROOT to a pre-built wgpu-native installation
if(NOT EMSCRIPTEN)
    # Try to find wgpu-native using FindWGPU.cmake
    include(FindWGPU OPTIONAL RESULT_VARIABLE FIND_WGPU_FOUND)
    
    if(TARGET wgpu::webgpu)
        message(STATUS "  wgpu-native: Found and configured")
    else()
        message(WARNING "  ┌─────────────────────────────────────────────────────────────┐")
        message(WARNING "  │ wgpu-native WebGPU not found!                               │")
        message(WARNING "  │                                                             │")
        message(WARNING "  │ To build voxy, you need to install wgpu-native:             │")
        message(WARNING "  │                                                             │")
        message(WARNING "  │ Option 1: Build from source (recommended):                  │")
        message(WARNING "  │   ./scripts/build_webgpu.sh release                         │")
        message(WARNING "  │                                                             │")
        message(WARNING "  │ Option 2: Set WGPU_ROOT to existing installation:           │")
        message(WARNING "  │   cmake -DWGPU_ROOT=/path/to/wgpu-native ..                 │")
        message(WARNING "  │                                                             │")
        message(WARNING "  │ Note: wgpu-native requires Rust toolchain                   │")
        message(WARNING "  │   curl --proto '=https' --tlsv1.2 -sSf \\                    │")
        message(WARNING "  │     https://sh.rustup.rs | sh                               │")
        message(WARNING "  │                                                             │")
        message(WARNING "  │ See: https://github.com/gfx-rs/wgpu-native                  │")
        message(WARNING "  └─────────────────────────────────────────────────────────────┘")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Google Test - Testing Framework
# https://github.com/google/googletest
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/googletest/CMakeLists.txt")
    # Prevent overriding the parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    
    # Disable installing GoogleTest
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    
    add_subdirectory(googletest)
    
    message(STATUS "  GoogleTest: Found")
else()
    message(STATUS "  GoogleTest: Not found - run scripts/fetch_deps.sh")
endif()

message(STATUS "Third-party configuration complete")

