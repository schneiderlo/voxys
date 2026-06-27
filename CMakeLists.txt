cmake_minimum_required(VERSION 3.16)
project(voxy VERSION 0.1.0 LANGUAGES CXX C)

# ─────────────────────────────────────────────────────────────────────────────
# C++ Standard Configuration
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─────────────────────────────────────────────────────────────────────────────
# GLM Configuration for WebGPU
# ─────────────────────────────────────────────────────────────────────────────
# WebGPU uses [0, 1] depth range (like Direct3D/Metal), not OpenGL's [-1, 1].
# Without this, projection matrices will have incorrect depth mapping, causing
# clipping issues and distorted ray-caster position reconstruction.
# Also use left-handed coordinate system (standard for WebGPU/D3D/Metal).
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
add_compile_definitions(GLM_FORCE_LEFT_HANDED)

# Export compile_commands.json for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────────────────
# Build Options
# ─────────────────────────────────────────────────────────────────────────────
option(VOXY_BUILD_NATIVE "Build native desktop target" ON)
option(VOXY_BUILD_WASM "Build WebAssembly target" OFF)
option(VOXY_BUILD_TOOLS "Build command-line tools" ON)
option(VOXY_BUILD_TESTS "Build unit tests" ON)
option(VOXY_ENABLE_VALIDATION "Enable WebGPU validation layers" ON)

# ─────────────────────────────────────────────────────────────────────────────
# Platform Detection
# ─────────────────────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set(VOXY_BUILD_NATIVE OFF)
    set(VOXY_BUILD_WASM ON)
    set(VOXY_BUILD_TOOLS OFF)
    message(STATUS "Building for WebAssembly with Emscripten")
else()
    message(STATUS "Building for native platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# CMake Modules
# ─────────────────────────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# ─────────────────────────────────────────────────────────────────────────────
# Output Directories
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ─────────────────────────────────────────────────────────────────────────────
# Third-Party Dependencies
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/CMakeLists.txt")
    add_subdirectory(third_party)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Core Library
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Native Target
# ─────────────────────────────────────────────────────────────────────────────
if(VOXY_BUILD_NATIVE)
    message(STATUS "Configuring native target: voxy_native")
    
    # For now, check if entry.cpp exists before adding executable
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/engine/platform/native/entry.cpp")
        add_executable(voxy_native
            src/engine/platform/native/entry.cpp
        )
        
        # Link core library if available
        if(TARGET voxy_core)
            target_link_libraries(voxy_native PRIVATE voxy_core)
        endif()
        
        # Link Dawn if available
        if(TARGET dawn::webgpu)
            target_link_libraries(voxy_native PRIVATE dawn::webgpu)
        endif()
        
        # Link GLFW if available
        if(TARGET glfw)
            target_link_libraries(voxy_native PRIVATE glfw)
        endif()
        
        target_compile_definitions(voxy_native PRIVATE VOXY_NATIVE=1)
        
        if(VOXY_ENABLE_VALIDATION)
            target_compile_definitions(voxy_native PRIVATE VOXY_ENABLE_VALIDATION=1)
        endif()
        
        # Senior Dev Fix: Ensure shaders/assets are always fresh in the build directory
        # WGSL files are loaded at runtime, so changes won't be picked up unless copied
        add_custom_command(TARGET voxy_native POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/shaders
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
            COMMENT "Copying shaders to build output directory"
        )
        
        # Copy assets if they exist
        if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
            add_custom_command(TARGET voxy_native POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_SOURCE_DIR}/assets
                    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
                COMMENT "Copying assets to build output directory"
            )
        endif()

        # Copy data if they exist
        if(EXISTS "${CMAKE_SOURCE_DIR}/data")
            add_custom_command(TARGET voxy_native POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_SOURCE_DIR}/data
                    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
                COMMENT "Copying data to build output directory"
            )
        endif()
        
        # Apply compiler warnings
        set_project_warnings(voxy_native)
    else()
        message(STATUS "  src/engine/platform/native/entry.cpp not found - native target deferred")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# WebAssembly Target
# ─────────────────────────────────────────────────────────────────────────────
if(VOXY_BUILD_WASM)
    message(STATUS "Configuring WASM target: voxy_wasm")
    
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/engine/platform/wasm/entry.cpp")
        # Add configuration and shaders as sources to ensure they are tracked for changes
        add_executable(voxy_wasm
            src/engine/platform/wasm/entry.cpp
            voxy.cfg
            shaders/terrain.wgsl
            shaders/ray_blit.wgsl
        )
        
        if(TARGET voxy_core)
            target_link_libraries(voxy_wasm PRIVATE voxy_core)
        endif()
        
        target_compile_definitions(voxy_wasm PRIVATE VOXY_WASM=1)
        
        # Emscripten-specific settings
        set_target_properties(voxy_wasm PROPERTIES
            SUFFIX ".js"
        )
        
        # Build link flags string
        # See specs/platform-build.md Section 3.2.3 for details
        set(WASM_LINK_FLAGS
            "--use-port=emdawnwebgpu"
            "-s WASM=1"
            "-s ALLOW_MEMORY_GROWTH=1"
            "-s MAXIMUM_MEMORY=2GB"
            "-s INITIAL_MEMORY=256MB"
            "-s STACK_SIZE=1MB"
            "-s MODULARIZE=1"
            "-s EXPORT_NAME='VoxyModule'"
            "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']"
            "-s EXPORTED_FUNCTIONS=['_main','_voxy_resize','_voxy_mouse_move','_voxy_key_event','_voxy_get_fps']"
            "-s ASYNCIFY"
        )
        
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND WASM_LINK_FLAGS
                "-s ASSERTIONS=1"
                "-s SAFE_HEAP=1"
                "-s STACK_OVERFLOW_CHECK=2"
                "-g4"
                "--source-map-base http://localhost:8080/"
            )
        else()
            # Release optimizations: -O3, LTO, Closure Compiler
            list(APPEND WASM_LINK_FLAGS
                "-O3"
                "-flto"
                "--closure 1"
            )
        endif()
        
        # Asset preloading with compression
        # See specs/platform-build.md Section 9.2 for details
        # --use-preload-plugins enables automatic processing of preloaded files
        # LZ4 compression is applied to the .data file for smaller downloads
        list(APPEND WASM_LINK_FLAGS
            "--use-preload-plugins"
        )
        
        # Core assets that must always be present
        list(APPEND WASM_LINK_FLAGS
            "--preload-file ${CMAKE_SOURCE_DIR}/voxy.cfg@/voxy.cfg"
        )
        
        # Preload entire directory if it exists
        if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
            )
        endif()
        
        # Preload shaders directory
        if(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/shaders@/shaders"
            )
        endif()
        
        # Preload specific data files
        if(EXISTS "${CMAKE_SOURCE_DIR}/data/canyon_4k.ldh")
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/data/canyon_4k.ldh@/data/canyon_4k.ldh"
            )
        endif()
        
        # Canyon diffuse texture
        if(EXISTS "${CMAKE_SOURCE_DIR}/data/canyon_diffuse.jpg")
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/data/canyon_diffuse.jpg@/data/canyon_diffuse.jpg"
            )
        endif()

        # Terrain Diffusion terrain and generated relief texture
        if(EXISTS "${CMAKE_SOURCE_DIR}/data/generated/td_seed_1234_8192.ldh")
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/data/generated/td_seed_1234_8192.ldh@/data/generated/td_seed_1234_8192.ldh"
            )
        endif()

        if(EXISTS "${CMAKE_SOURCE_DIR}/data/generated/td_seed_1234_8192_relief.png")
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/data/generated/td_seed_1234_8192_relief.png@/data/generated/td_seed_1234_8192_relief.png"
            )
        endif()
        
        string(JOIN " " WASM_LINK_FLAGS_STR ${WASM_LINK_FLAGS})
        set_target_properties(voxy_wasm PROPERTIES
            LINK_FLAGS "${WASM_LINK_FLAGS_STR}"
        )
    else()
        message(STATUS "  src/engine/platform/wasm/entry.cpp not found - WASM target deferred")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Tools
# ─────────────────────────────────────────────────────────────────────────────
if(VOXY_BUILD_TOOLS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/CMakeLists.txt")
        add_subdirectory(tools)
    else()
        message(STATUS "Tools directory not configured yet")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────────────
if(VOXY_BUILD_TESTS AND NOT EMSCRIPTEN)
    # Enable CTest
    include(CTest)
    enable_testing()
    
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    else()
        message(STATUS "Tests directory not configured yet")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install Rules
# ─────────────────────────────────────────────────────────────────────────────
if(TARGET voxy_native)
    install(TARGETS voxy_native RUNTIME DESTINATION bin)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
    install(DIRECTORY shaders/ DESTINATION share/voxy/shaders)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    install(DIRECTORY assets/ DESTINATION share/voxy/assets)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "  voxy ${PROJECT_VERSION} Configuration Summary")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Native build:      ${VOXY_BUILD_NATIVE}")
message(STATUS "  WASM build:        ${VOXY_BUILD_WASM}")
message(STATUS "  Build tools:       ${VOXY_BUILD_TOOLS}")
message(STATUS "  Build tests:       ${VOXY_BUILD_TESTS}")
message(STATUS "  Validation:        ${VOXY_ENABLE_VALIDATION}")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "")
