# ═══════════════════════════════════════════════════════════════════════════════
# src/CMakeLists.txt
# ═══════════════════════════════════════════════════════════════════════════════
# Core library build configuration for voxy.
# This library is shared between native and WASM targets.
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# Gather source files by module (to be populated as modules are implemented)
# ─────────────────────────────────────────────────────────────────────────────

set(CORE_SOURCES "")
set(GPU_SOURCES "")
set(TERRAIN_SOURCES "")
set(RENDER_SOURCES "")
set(CAMERA_SOURCES "")
set(APP_SOURCES "")

# Core utilities (Phase 1.4)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/log.cpp")
    list(APPEND CORE_SOURCES
        core/log.cpp
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/config.cpp")
    list(APPEND CORE_SOURCES
        core/config.cpp
    )
endif()

# STB implementation (always include if available)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/stb_impl.cpp")
    list(APPEND CORE_SOURCES
        core/stb_impl.cpp
    )
endif()

# GPU abstraction (Phase 2) - available via wgpu-native, Dawn, or Emscripten
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gpu/context.cpp")
    if(TARGET wgpu::webgpu OR TARGET dawn::webgpu OR EMSCRIPTEN)
        list(APPEND GPU_SOURCES
            gpu/context.cpp
        )
        set(VOXY_HAS_WEBGPU ON)
        if(EMSCRIPTEN)
            message(STATUS "WebGPU available via Emscripten")
        endif()
    endif()
endif()

# GPU resource helpers (Phase 2.3)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gpu/resources.cpp")
    if(TARGET wgpu::webgpu OR TARGET dawn::webgpu OR EMSCRIPTEN)
        list(APPEND GPU_SOURCES
            gpu/resources.cpp
        )
    endif()
endif()

# Application layer - Window (Phase 2)
if(EMSCRIPTEN)
    list(APPEND APP_SOURCES
        engine/platform/wasm/window.cpp
        engine/platform/wasm/input.cpp
    )
else()
    list(APPEND APP_SOURCES
        engine/platform/native/window.cpp
        engine/platform/native/input.cpp
    )
endif()

# Terrain systems (Phase 3, 5, 6)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/biomes.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/biomes.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/heightmap.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/heightmap.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/compression.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/compression.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/mip_generator.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/mip_generator.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/textures.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/textures.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/decorations.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/decorations.cpp
    )
endif()

# Voxel systems (Phase 14 - SVO)
set(VOXEL_SOURCES "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voxel/svo_buffers.cpp")
    list(APPEND VOXEL_SOURCES
        voxel/svo_buffers.cpp
    )
endif()

# Rendering (Phase 3, 6, 7, 8)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/renderer.cpp")
    list(APPEND RENDER_SOURCES
        render/renderer.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/triangle_path.cpp")
    list(APPEND RENDER_SOURCES
        render/triangle_path.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/mip_pipeline.cpp")
    if(TARGET wgpu::webgpu OR TARGET dawn::webgpu OR EMSCRIPTEN)
        list(APPEND RENDER_SOURCES
            render/mip_pipeline.cpp
        )
    endif()
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/raycast_path.cpp")
    list(APPEND RENDER_SOURCES
        render/raycast_path.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/debug_visualizer.cpp")
    list(APPEND RENDER_SOURCES
        render/debug_visualizer.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/blit_path.cpp")
    list(APPEND RENDER_SOURCES
        render/blit_path.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/decoration_renderer.cpp")
    list(APPEND RENDER_SOURCES
        render/decoration_renderer.cpp
    )
endif()

# Camera (Phase 3, 4)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/camera/camera.cpp")
    list(APPEND CAMERA_SOURCES
        camera/camera.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/camera/controller.cpp")
    list(APPEND CAMERA_SOURCES
        camera/controller.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/camera/character_controller.cpp")
    list(APPEND CAMERA_SOURCES
        camera/character_controller.cpp
    )
endif()

# Application layer (Phase 4, 9, 10)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/application.cpp")
    list(APPEND APP_SOURCES
        app/application.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/debug_overlay.cpp")
    list(APPEND APP_SOURCES
        app/debug_overlay.cpp
    )
endif()

# Performance instrumentation (Phase 10.3)
set(PERF_SOURCES "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/perf/gpu_timer.cpp")
    list(APPEND PERF_SOURCES
        perf/gpu_timer.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/perf/benchmark.cpp")
    list(APPEND PERF_SOURCES
        perf/benchmark.cpp
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Combine all sources
# ─────────────────────────────────────────────────────────────────────────────
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${GPU_SOURCES}
    ${TERRAIN_SOURCES}
    ${VOXEL_SOURCES}
    ${RENDER_SOURCES}
    ${CAMERA_SOURCES}
    ${APP_SOURCES}
    ${PERF_SOURCES}
)

# ─────────────────────────────────────────────────────────────────────────────
# Create core library (only if we have sources)
# ─────────────────────────────────────────────────────────────────────────────
if(ALL_SOURCES)
    add_library(voxy_core STATIC ${ALL_SOURCES})
    
    target_include_directories(voxy_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(EMSCRIPTEN)
        if(DEFINED EMSCRIPTEN_ROOT_PATH)
            set(VOXY_WEBGPU_PORT_INCLUDE
                "${EMSCRIPTEN_ROOT_PATH}/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include")
            if(EXISTS "${VOXY_WEBGPU_PORT_INCLUDE}")
                target_include_directories(voxy_core PUBLIC
                    "${VOXY_WEBGPU_PORT_INCLUDE}"
                )
            else()
                message(WARNING "Emscripten WebGPU headers not found at ${VOXY_WEBGPU_PORT_INCLUDE}")
            endif()
        endif()
        
        # Ensure we link against the WebGPU port
        target_link_options(voxy_core PUBLIC "SHELL:--use-port=emdawnwebgpu")
    else()
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/wgpu-native/dist/include")
            target_include_directories(voxy_core SYSTEM PUBLIC
                "${CMAKE_SOURCE_DIR}/third_party/wgpu-native/dist/include"
            )
        endif()
    endif()
    
    # Link third-party dependencies if available
    if(TARGET glm)
        target_link_libraries(voxy_core PUBLIC glm)
    endif()
    
    if(TARGET libzstd_static)
        target_link_libraries(voxy_core PUBLIC libzstd_static)
        # Add zstd include directory (libzstd_static doesn't export it)
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/zstd/lib")
            target_include_directories(voxy_core PUBLIC 
                "${CMAKE_SOURCE_DIR}/third_party/zstd/lib"
            )
        endif()
    elseif(TARGET zstd::libzstd_static)
        target_link_libraries(voxy_core PUBLIC zstd::libzstd_static)
    endif()
    
    if(TARGET stb)
        target_link_libraries(voxy_core PUBLIC stb)
    endif()
    
    if(TARGET tinyexr)
        target_link_libraries(voxy_core PUBLIC tinyexr)
    endif()
    
    # GLFW for windowing (native only)
    if(TARGET glfw AND NOT EMSCRIPTEN)
        target_link_libraries(voxy_core PUBLIC glfw)
    endif()
    
    # WebGPU (native only - wgpu-native or Dawn)
    if(NOT EMSCRIPTEN)
        if(TARGET wgpu::webgpu)
            target_link_libraries(voxy_core PUBLIC wgpu::webgpu)
        elseif(TARGET dawn::webgpu)
            target_link_libraries(voxy_core PUBLIC dawn::webgpu)
        endif()
    endif()
    
    # Platform definitions for conditional compilation
    if(EMSCRIPTEN)
        target_compile_definitions(voxy_core PUBLIC VOXY_WASM=1)
    else()
        target_compile_definitions(voxy_core PUBLIC VOXY_NATIVE=1)
    endif()
    
    # Apply compiler warnings
    set_project_warnings(voxy_core)
    
    message(STATUS "Building voxy_core with sources: ${ALL_SOURCES}")
else()
    message(STATUS "No voxy_core sources found yet (will be added in later phases)")
endif()
