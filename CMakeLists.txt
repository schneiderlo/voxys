cmake_minimum_required(VERSION 3.20)
# CMake 4 rejects vendored projects whose own minimum predates 3.5. Keep the
# compatibility floor local to this dependency build while the top-level
# project continues to require 3.20.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
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
option(VOXY_BUILD_BENCHMARKS "Build manual benchmark executables" OFF)
option(VOXY_ENABLE_VALIDATION "Enable WebGPU validation layers" ON)

# ─────────────────────────────────────────────────────────────────────────────
# Platform Detection
# ─────────────────────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set(VOXY_BUILD_NATIVE OFF)
    set(VOXY_BUILD_WASM ON)
    set(VOXY_BUILD_TOOLS OFF)
    # GameSession must unwind reserved work on adapter/allocation exceptions.
    # Use JS-based EH: native WASM EH is incompatible with the current Asyncify path.
    # Fixed-heap malloc must return null so C++ new can throw std::bad_alloc.
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions -sABORTING_MALLOC=0)
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

# A config-package target imported inside third_party/ is scoped to that
# directory. Import it at the root before voxy_core is configured so the
# library can publish GLFW to every native consumer, including tests.
if(NOT EMSCRIPTEN AND NOT TARGET glfw)
    find_package(glfw3 3.4 QUIET CONFIG)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# WRECKWATER authority build-content identity
# ─────────────────────────────────────────────────────────────────────────────
if(VOXY_BUILD_NATIVE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Keep this dependency declaration exact. The generator compares it with
    # the checked-in allowlist and rejects missing or extra inputs.
    set(VOXY_WRECKWATER_AUTHORITY_CONTENT_RELATIVE_INPUTS
        shaders/physics_attachments.wgsl
        shaders/physics_ballistic.wgsl
        shaders/physics_broad_phase.wgsl
        shaders/physics_ccd.wgsl
        shaders/physics_deterministic_primitives.wgsl
        shaders/physics_dynamic_solver.wgsl
        shaders/physics_event_readback.wgsl
        shaders/physics_islands.wgsl
        shaders/physics_narrow_phase.wgsl
        shaders/physics_queries.wgsl
        src/core/log.cpp
        src/core/log.hpp
        src/game/wreckwater_character_authority_bridge.cpp
        src/game/wreckwater_character_authority_bridge.hpp
        src/game/wreckwater_character_movement.cpp
        src/game/wreckwater_character_movement.hpp
        src/game/wreckwater_character_step.cpp
        src/game/wreckwater_character_step.hpp
        src/game/wreckwater_live_world.cpp
        src/game/wreckwater_live_world.hpp
        src/game/wreckwater_match.cpp
        src/game/wreckwater_match.hpp
        src/game/wreckwater_replay.cpp
        src/game/wreckwater_replay.hpp
        src/game/wreckwater_vessel_damage.cpp
        src/game/wreckwater_vessel_damage.hpp
        src/geometry/box_union.cpp
        src/geometry/box_union.hpp
        src/geometry/grid_types.hpp
        src/geometry/orthogonal_geometry.hpp
        src/gpu/context.cpp
        src/gpu/context.hpp
        src/gpu/resources.cpp
        src/gpu/resources.hpp
        src/gpu/shader_source.hpp
        src/gpu/webgpu_compat.hpp
        src/network/native_tcp_transport.cpp
        src/network/native_tcp_transport.hpp
        src/network/protocol.cpp
        src/network/protocol.hpp
        src/network/session_transport.hpp
        src/network/wreckwater_protocol.cpp
        src/network/wreckwater_protocol.hpp
        src/physics/authored_body_frame.cpp
        src/physics/authored_body_frame.hpp
        src/physics/authored_shape.cpp
        src/physics/authored_shape.hpp
        src/physics/authored_shape_pool.cpp
        src/physics/authored_shape_pool.hpp
        src/physics/authored_shape_resources.hpp
        src/physics/character/cpu_capsule_mover.cpp
        src/physics/character/cpu_capsule_mover.hpp
        src/physics/deterministic/lockstep_types.hpp
        src/physics/deterministic/lockstep_world.hpp
        src/physics/deterministic/replay.hpp
        src/physics/gpu/debug_readback_ring.cpp
        src/physics/gpu/debug_readback_ring.hpp
        src/physics/gpu/deterministic_primitives.cpp
        src/physics/gpu/deterministic_primitives.hpp
        src/physics/gpu/gpu_attachments.cpp
        src/physics/gpu/gpu_attachments.hpp
        src/physics/gpu/gpu_authored_shapes.cpp
        src/physics/gpu/gpu_authored_shapes.hpp
        src/physics/gpu/gpu_body_metadata.hpp
        src/physics/gpu/gpu_body_shape.hpp
        src/physics/gpu/gpu_broad_phase.cpp
        src/physics/gpu/gpu_broad_phase.hpp
        src/physics/gpu/gpu_buffer_arena.cpp
        src/physics/gpu/gpu_buffer_arena.hpp
        src/physics/gpu/gpu_ccd.cpp
        src/physics/gpu/gpu_ccd.hpp
        src/physics/gpu/gpu_dynamic_solver.cpp
        src/physics/gpu/gpu_dynamic_solver.hpp
        src/physics/gpu/gpu_event_readback.cpp
        src/physics/gpu/gpu_event_readback.hpp
        src/physics/gpu/gpu_islands.cpp
        src/physics/gpu/gpu_islands.hpp
        src/physics/gpu/gpu_narrow_phase.cpp
        src/physics/gpu/gpu_narrow_phase.hpp
        src/physics/gpu/gpu_physics_backend.cpp
        src/physics/gpu/gpu_physics_backend.hpp
        src/physics/gpu/gpu_queries.cpp
        src/physics/gpu/gpu_queries.hpp
        src/physics/physics_backend.hpp
        src/physics/physics_backend_factory.hpp
        src/physics/physics_types.cpp
        src/physics/physics_types.hpp
        src/physics/physics_world.cpp
        src/physics/physics_world.hpp
        src/physics/rigid_mass_frame.cpp
        src/physics/rigid_mass_frame.hpp
        src/physics/shape_handle.hpp
        src/physics/terrain_topology.hpp
        src/physics/wreckwater_authority_backend_factory.cpp
        src/server/wreckwater_authority_runtime.cpp
        src/server/wreckwater_authority_runtime.hpp
        src/server/wreckwater_server_main.cpp
        src/terrain/lego_surface.hpp
        src/terrain/mip_generator.cpp
        src/terrain/mip_generator.hpp
    )
    set(VOXY_WRECKWATER_AUTHORITY_CONTENT_INPUTS "")
    foreach(VOXY_WRECKWATER_AUTHORITY_CONTENT_INPUT
            IN LISTS VOXY_WRECKWATER_AUTHORITY_CONTENT_RELATIVE_INPUTS)
        list(APPEND VOXY_WRECKWATER_AUTHORITY_CONTENT_INPUTS
            "${CMAKE_SOURCE_DIR}/${VOXY_WRECKWATER_AUTHORITY_CONTENT_INPUT}"
        )
    endforeach()

    set(VOXY_WRECKWATER_BUILD_CONTENT_HEADER
        "${CMAKE_BINARY_DIR}/generated/wreckwater_build_content.hpp")
    add_custom_command(
        OUTPUT "${VOXY_WRECKWATER_BUILD_CONTENT_HEADER}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/generate_wreckwater_build_content.py"
            --root "${CMAKE_SOURCE_DIR}"
            --allowlist
                "${CMAKE_SOURCE_DIR}/wreckwater_authority_content.allowlist"
            --output "${VOXY_WRECKWATER_BUILD_CONTENT_HEADER}"
            --inputs ${VOXY_WRECKWATER_AUTHORITY_CONTENT_INPUTS}
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/generate_wreckwater_build_content.py"
            "${CMAKE_SOURCE_DIR}/wreckwater_authority_content.allowlist"
            ${VOXY_WRECKWATER_AUTHORITY_CONTENT_INPUTS}
        COMMENT "Generating checked WRECKWATER build-content identity"
        VERBATIM
    )
    add_custom_target(wreckwater_build_content_header
        DEPENDS "${VOXY_WRECKWATER_BUILD_CONTENT_HEADER}")
    add_library(wreckwater_build_content INTERFACE)
    add_dependencies(
        wreckwater_build_content wreckwater_build_content_header)
    target_include_directories(wreckwater_build_content INTERFACE
        "${CMAKE_BINARY_DIR}"
        "${CMAKE_SOURCE_DIR}/src")
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
            src/engine/platform/native/cove_saves.cpp
            src/engine/platform/native/inspection_motion.hpp
        )
        
        # Link core library if available
        if(TARGET voxy_core)
            target_link_libraries(
                voxy_native PRIVATE
                voxy_core
                wreckwater_build_content)
            add_dependencies(
                voxy_native wreckwater_build_content_header)
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
        
        # Runtime files must refresh even when the executable does not relink.
        # A custom target runs on every native build; copy_if_different avoids
        # rewriting unchanged configs. Target-relative paths support multi-config
        # generators as well as the normal bin/ output directory.
        add_custom_target(voxy_native_runtime_files
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:voxy_native>/shaders"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/shaders"
                "$<TARGET_FILE_DIR:voxy_native>/shaders"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/voxy.cfg"
                "${CMAKE_SOURCE_DIR}/ridgebreak.cfg"
                "${CMAKE_SOURCE_DIR}/lego_shore.cfg"
                "${CMAKE_SOURCE_DIR}/lego_world.cfg"
                "${CMAKE_SOURCE_DIR}/salvage.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_asset_fixture.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_assembly_fixture.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_hierarchy_fixture.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_rotations_a.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_rotations_b.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_kit_narrow.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_kit_broad.cfg"
                "${CMAKE_SOURCE_DIR}/salvage_kit_cargo.cfg"
                "$<TARGET_FILE_DIR:voxy_native>"
            COMMENT "Refreshing native shaders and launch configs"
            VERBATIM
        )
        add_dependencies(voxy_native voxy_native_runtime_files)
        
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

    if(EXISTS "${CMAKE_SOURCE_DIR}/src/server/wreckwater_server_main.cpp")
        add_executable(wreckwater_server
            src/server/wreckwater_server_main.cpp
        )

        if(NOT TARGET wreckwater_headless_authority)
            message(FATAL_ERROR
                "The narrow WRECKWATER authority target is unavailable")
        endif()
        target_link_libraries(
            wreckwater_server PRIVATE
                wreckwater_build_content
                wreckwater_headless_authority
        )
        add_dependencies(
            wreckwater_server wreckwater_build_content_header)

        target_compile_definitions(wreckwater_server PRIVATE VOXY_NATIVE=1)
        set_project_warnings(wreckwater_server)

        # Linux release/CI proof: compare compile_commands against the exact
        # manifested TUs, then reject client, presentation, Jolt, and Box3D
        # symbols or dynamic dependencies in the linked server.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(VOXY_LDD_EXECUTABLE ldd)
            if(CMAKE_NM AND VOXY_LDD_EXECUTABLE)
                add_custom_target(wreckwater_server_closure_check
                    COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_SOURCE_DIR}/tools/check_wreckwater_authority_cmake_closure.py"
                        --workspace "${CMAKE_SOURCE_DIR}"
                        --compile-commands
                            "${CMAKE_BINARY_DIR}/compile_commands.json"
                        --binary "$<TARGET_FILE:wreckwater_server>"
                        --nm "${CMAKE_NM}"
                        --ldd "${VOXY_LDD_EXECUTABLE}"
                    DEPENDS wreckwater_server
                    COMMENT
                        "Checking narrow WRECKWATER CMake authority closure"
                    VERBATIM
                )
            else()
                message(WARNING
                    "nm or ldd unavailable; CMake authority binary closure "
                    "check target was not created")
            endif()
        endif()
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/src/server/ridgebreak_server_main.cpp")
        add_executable(ridgebreak_server
            src/server/ridgebreak_server_main.cpp
        )
        target_link_libraries(ridgebreak_server PRIVATE voxy_core)
        target_compile_definitions(ridgebreak_server PRIVATE VOXY_NATIVE=1)
        set_project_warnings(ridgebreak_server)
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/src/client/wreckwater_client_probe_main.cpp")
        add_executable(wreckwater_client_probe
            src/client/wreckwater_client_probe.cpp
            src/client/wreckwater_client_probe_main.cpp
        )

        if(TARGET voxy_core)
            target_link_libraries(wreckwater_client_probe PRIVATE voxy_core)
        endif()
        target_link_libraries(
            wreckwater_client_probe PRIVATE wreckwater_build_content)
        add_dependencies(
            wreckwater_client_probe wreckwater_build_content_header)
        target_compile_definitions(
            wreckwater_client_probe PRIVATE VOXY_NATIVE=1)
        set_project_warnings(wreckwater_client_probe)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# WebAssembly Target
# ─────────────────────────────────────────────────────────────────────────────
if(VOXY_BUILD_WASM)
    message(STATUS "Configuring WASM target: voxy_wasm")
    
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/engine/platform/wasm/entry.cpp")
        # Every runtime shader must relink the preload package when it changes.
        file(GLOB VOXY_WASM_SHADER_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/shaders/*.wgsl"
        )
        file(GLOB VOXY_WASM_MATERIAL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/data/materials/*.jpg"
        )
        add_executable(voxy_wasm
            src/engine/platform/wasm/entry.cpp
            voxy.cfg
            ${VOXY_WASM_SHADER_SOURCES}
        )
        # WGSL is preloaded by the Emscripten link step rather than compiled as
        # a CMake source language. Make the dependency explicit so an
        # incremental shader edit rebuilds voxy_wasm.data instead of silently
        # benchmarking stale shader code.
        set_property(TARGET voxy_wasm APPEND PROPERTY
            LINK_DEPENDS
                ${VOXY_WASM_SHADER_SOURCES}
                ${VOXY_WASM_MATERIAL_SOURCES}
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
            # Web APIs such as TextDecoder and WebGPU reject views backed by
            # resizable WebAssembly memory. Keep the heap fixed for browser use.
            "-s ALLOW_MEMORY_GROWTH=0"
            "-s INITIAL_MEMORY=512MB"
            "-s STACK_SIZE=1MB"
            "-s MODULARIZE=1"
            "-s EXPORT_NAME='VoxyModule'"
            "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8','callMain']"
            "-s EXPORTED_FUNCTIONS=['_main','_voxy_resize','_voxy_renderer_set_number','_voxy_renderer_get_number','_voxy_renderer_get_revision','_voxy_renderer_get_applied_revision','_voxy_mouse_move','_voxy_set_camera_pose','_voxy_key_event','_voxy_get_fps','_voxy_get_frame_count','_voxy_get_last_frame_cpu_ms','_voxy_get_last_frame_wall_ms','_voxy_get_gpu_frames_in_flight','_voxy_get_gpu_pacing_skips','_voxy_get_uncapped_fps','_voxy_set_uncapped_fps','_voxy_start_cube_pyramid_experiment','_voxy_start_browser_journey_benchmark','_voxy_get_browser_journey_benchmark_status','_voxy_get_browser_journey_benchmark_json','_voxy_start_render_throughput_benchmark','_voxy_get_render_throughput_status','_voxy_get_render_throughput_fps','_voxy_get_render_throughput_elapsed_ms','_voxy_get_render_throughput_completed_frames','_voxy_get_physics_substeps','_voxy_get_physics_resident_bodies','_voxy_set_throwable_body_limit','_voxy_get_physics_encoded_tick','_voxy_get_physics_stage_ms','_voxy_get_physics_stage_tick','_voxy_poll_physics_stage_timing','_voxy_get_polled_physics_stage_ms','_voxy_poll_render_stage_timing','_voxy_get_polled_render_stage_ms','_voxy_get_telemetry_json','_voxy_get_moto_hud_json','_voxy_is_initialized','_voxy_get_physics_backend','_voxy_start_physics_self_test','_voxy_get_physics_self_test_status','_voxy_get_physics_self_test_tick']"
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
        
        # Populate the virtual filesystem with original asset bytes. Textures
        # are decoded by the shared C++ loaders (stb_image / VMESH), not by
        # Emscripten's browser-image preload plugin. That unused second decode
        # can fail before startup and retains duplicate full-size image data.
        
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
        
        # Preload the data files referenced by voxy.cfg
        foreach(VOXY_WASM_DATA_FILE
            ridgebreak.cfg
            lego_shore.cfg
            lego_world.cfg
            salvage.cfg
            salvage_asset_fixture.cfg
            salvage_assembly_fixture.cfg
            salvage_hierarchy_fixture.cfg
            salvage_rotations_a.cfg
            salvage_rotations_b.cfg
            salvage_kit_narrow.cfg
            data/salvage/fixture-kit-narrow.json
            salvage_kit_broad.cfg
            data/salvage/fixture-kit-broad.json
            salvage_kit_cargo.cfg
            data/salvage/fixture-kit-cargo.json
            salvage_material_fixture.cfg
            salvage_metric_fixture.cfg
            salvage_cove.cfg
            data/salvage/fixture-cove-r01.json
            data/salvage/cove-bricks-r02.json
            data/salvage/bricks/r02/brick_1x2/cooked/cook-manifest.json
            data/salvage/bricks/r02/brick_1x2/cooked/gameplay.json
            data/salvage/bricks/r02/brick_1x2/cooked/lod-1.vmesh
            data/salvage/bricks/r02/brick_1x2/cooked/lod-2.vmesh
            data/salvage/bricks/r02/brick_1x2/cooked/lod-3.vmesh
            data/salvage/bricks/r02/brick_2x2/cooked/cook-manifest.json
            data/salvage/bricks/r02/brick_2x2/cooked/gameplay.json
            data/salvage/bricks/r02/brick_2x2/cooked/lod-1.vmesh
            data/salvage/bricks/r02/brick_2x2/cooked/lod-2.vmesh
            data/salvage/bricks/r02/brick_2x2/cooked/lod-3.vmesh
            data/salvage/bricks/r02/brick_2x4/cooked/cook-manifest.json
            data/salvage/bricks/r02/brick_2x4/cooked/gameplay.json
            data/salvage/bricks/r02/brick_2x4/cooked/lod-1.vmesh
            data/salvage/bricks/r02/brick_2x4/cooked/lod-2.vmesh
            data/salvage/bricks/r02/brick_2x4/cooked/lod-3.vmesh
            data/salvage/fixture-material-metric-r01.json
            data/salvage/fixture-material-shading-r02.json
            data/salvage/material-calibration/r04/kit/winch/cooked/cook-manifest.json
            data/salvage/material-calibration/r04/kit/winch/cooked/gameplay.json
            data/salvage/material-calibration/r04/kit/winch/cooked/lod-1.vmesh
            data/salvage/material-calibration/r04/kit/winch/cooked/lod-2.vmesh
            data/salvage/material-calibration/r04/kit/winch/cooked/lod-3.vmesh
            data/salvage/material-calibration/r04/kit/helm/cooked/cook-manifest.json
            data/salvage/material-calibration/r04/kit/helm/cooked/gameplay.json
            data/salvage/material-calibration/r04/kit/helm/cooked/lod-1.vmesh
            data/salvage/material-calibration/r04/kit/helm/cooked/lod-2.vmesh
            data/salvage/material-calibration/r04/kit/helm/cooked/lod-3.vmesh
            data/salvage/material-calibration/r02/pontoon-r02/cooked/cook-manifest.json
            data/salvage/material-calibration/r02/pontoon-r02/cooked/gameplay.json
            data/salvage/material-calibration/r02/pontoon-r02/cooked/lod-1.vmesh
            data/salvage/material-calibration/r02/pontoon-r02/cooked/lod-2.vmesh
            data/salvage/material-calibration/r02/pontoon-r02/cooked/lod-3.vmesh
            data/salvage/material-calibration/r02/kit/winch/cooked/cook-manifest.json
            data/salvage/material-calibration/r02/kit/winch/cooked/gameplay.json
            data/salvage/material-calibration/r02/kit/winch/cooked/lod-1.vmesh
            data/salvage/material-calibration/r02/kit/winch/cooked/lod-2.vmesh
            data/salvage/material-calibration/r02/kit/winch/cooked/lod-3.vmesh
            data/salvage/material-calibration/r02/kit/helm/cooked/cook-manifest.json
            data/salvage/material-calibration/r02/kit/helm/cooked/gameplay.json
            data/salvage/material-calibration/r02/kit/helm/cooked/lod-1.vmesh
            data/salvage/material-calibration/r02/kit/helm/cooked/lod-2.vmesh
            data/salvage/material-calibration/r02/kit/helm/cooked/lod-3.vmesh
            data/salvage/fixture-material-calibration-r01.json
            data/salvage/material-calibration/r01/pontoon/cooked/cook-manifest.json
            data/salvage/material-calibration/r01/pontoon/cooked/gameplay.json
            data/salvage/material-calibration/r01/pontoon/cooked/lod-1.vmesh
            data/salvage/material-calibration/r01/pontoon/cooked/lod-2.vmesh
            data/salvage/material-calibration/r01/pontoon/cooked/lod-3.vmesh
            data/salvage/material-calibration/r01/kit/winch/cooked/cook-manifest.json
            data/salvage/material-calibration/r01/kit/winch/cooked/gameplay.json
            data/salvage/material-calibration/r01/kit/winch/cooked/lod-1.vmesh
            data/salvage/material-calibration/r01/kit/winch/cooked/lod-2.vmesh
            data/salvage/material-calibration/r01/kit/winch/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/beam/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/beam/cooked/gameplay.json
            data/salvage/functional-kit/r09/beam/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/beam/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/beam/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/plate/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/plate/cooked/gameplay.json
            data/salvage/functional-kit/r09/plate/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/plate/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/plate/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/engine/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/engine/cooked/gameplay.json
            data/salvage/functional-kit/r09/engine/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/engine/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/engine/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/propeller/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/propeller/cooked/gameplay.json
            data/salvage/functional-kit/r09/propeller/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/propeller/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/propeller/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/helm/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/helm/cooked/gameplay.json
            data/salvage/functional-kit/r09/helm/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/helm/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/helm/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/winch/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/winch/cooked/gameplay.json
            data/salvage/functional-kit/r09/winch/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/winch/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/winch/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/cradle/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/cradle/cooked/gameplay.json
            data/salvage/functional-kit/r09/cradle/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/cradle/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/cradle/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/generator/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/generator/cooked/gameplay.json
            data/salvage/functional-kit/r09/generator/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/generator/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/generator/cooked/lod-3.vmesh
            data/salvage/functional-kit/r09/crate/cooked/cook-manifest.json
            data/salvage/functional-kit/r09/crate/cooked/gameplay.json
            data/salvage/functional-kit/r09/crate/cooked/lod-1.vmesh
            data/salvage/functional-kit/r09/crate/cooked/lod-2.vmesh
            data/salvage/functional-kit/r09/crate/cooked/lod-3.vmesh
            data/salvage/fixture-pontoon-rotations-a.json
            data/salvage/fixture-pontoon-rotations-b.json
            data/salvage/fixture-hierarchy-v1.json
            data/salvage/hierarchy_probe/candidates/v1-r02/cooked/cook-manifest.json
            data/salvage/hierarchy_probe/candidates/v1-r02/cooked/gameplay.json
            data/salvage/hierarchy_probe/candidates/v1-r02/cooked/lod-1.vmesh
            data/salvage/fixture-pontoon-v2.json
            data/salvage/fixture-pontoon-assembly.json
            data/salvage/pontoon/release-candidates/v2-rc01/cooked/cook-manifest.json
            data/salvage/pontoon/release-candidates/v2-rc01/cooked/gameplay.json
            data/salvage/pontoon/release-candidates/v2-rc01/cooked/lod-1.vmesh
            data/salvage/pontoon/release-candidates/v2-rc01/cooked/lod-2.vmesh
            data/salvage/pontoon/release-candidates/v2-rc01/cooked/lod-3.vmesh
            data/lego_shore.ldh
            data/generated/td_seed_1234_8192.ldh
            data/generated/td_seed_1234_2048_albedo.jpg
            data/generated/ocean_environment.png
            data/moto/bike.vmesh
            data/moto/rider.vmesh
            data/moto/track.vmesh
        )
            if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${VOXY_WASM_DATA_FILE}")
                message(FATAL_ERROR "Required WASM data file not found: ${VOXY_WASM_DATA_FILE}")
            endif()
            list(APPEND WASM_LINK_FLAGS
                "--preload-file ${CMAKE_SOURCE_DIR}/${VOXY_WASM_DATA_FILE}@/${VOXY_WASM_DATA_FILE}"
            )
            # Preloaded files are link inputs even though Emscripten receives
            # them through flags. Track them so an authored VMESH revision
            # cannot leave a stale voxy_wasm.data package behind.
            set_property(TARGET voxy_wasm APPEND PROPERTY LINK_DEPENDS
                "${CMAKE_SOURCE_DIR}/${VOXY_WASM_DATA_FILE}"
            )
        endforeach()
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/data/materials")
            message(FATAL_ERROR "Required terrain material directory not found")
        endif()
        list(APPEND WASM_LINK_FLAGS
            "--preload-file ${CMAKE_SOURCE_DIR}/data/materials@/data/materials"
        )

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
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
    endif()

    add_test(
        NAME wreckwater_build_content_generator
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/test_generate_wreckwater_build_content.py"
    )
    
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
message(STATUS "  Build benchmarks:  ${VOXY_BUILD_BENCHMARKS}")
message(STATUS "  Validation:        ${VOXY_ENABLE_VALIDATION}")
message(STATUS "  Jolt Physics:      ON")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "")
