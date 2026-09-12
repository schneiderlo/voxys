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
set(GAME_SOURCES
    game/assets/cooked_part_directory.cpp
    game/assets/fixture_registry.cpp
    game/expedition/session_events.cpp
    game/expedition/session_transactions.cpp
    game/assets/cooked_part_bundle.cpp
    game/assets/rigid_prefab.cpp
    game/assets/rigid_animation.cpp
    game/assets/robot_asset.cpp
    game/assets/gameplay_sidecar.cpp
    game/expedition/game_session.cpp
    game/expedition/session_recovery.cpp
    game/expedition/session_save.cpp
    game/expedition/cove_save.cpp
    game/expedition/cove_restore.cpp
    game/expedition/save_generation.cpp
    game/construction/build_refit.cpp
    game/construction/build_model.cpp
    game/construction/assembly_compiler.cpp
    geometry/box_union.cpp
    game/construction/assembly_collision.cpp
    game/construction/orthogonal_coverage.cpp
    game/construction/assembly_buoyancy.cpp
    game/construction/assembly_functions.cpp
    game/construction/compiled_assembly.cpp
    game/construction/assembly_fracture.cpp
    game/construction/construction_types.cpp
    game/construction/part_catalog.cpp
    game/expedition/cove_player.cpp
    game/expedition/cove_camera.cpp
    game/expedition/cove_workshop.cpp
    game/expedition/design_library.cpp
    game/expedition/workshop_camera.cpp
    game/expedition/cove_build.cpp
    game/expedition/cove_boat.cpp
    game/expedition/cove_rigid_roots.cpp
    game/expedition/cove_harbor_lift.cpp
    game/expedition/cove_harbor_runtime.cpp
    game/expedition/salvage_preview.cpp
)
if(NOT EMSCRIPTEN)
    list(APPEND GAME_SOURCES engine/platform/native/save_store.cpp engine/platform/native/save_worker.cpp
        engine/platform/native/design_library.cpp engine/platform/native/workshop_menu.cpp)
endif()
set(GAMEPLAY_SOURCES "")
set(MOTO_SOURCES "")
set(TERRAIN_SOURCES "")
set(RENDER_SOURCES "")
list(APPEND RENDER_SOURCES render/cove_hud.cpp)
set(CAMERA_SOURCES "")
set(PHYSICS_SOURCES "")
set(NETWORK_SOURCES "")
set(CLIENT_SOURCES "")
set(SERVER_SOURCES "")
set(APP_SOURCES "")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/wreckwater_match.cpp")
    list(APPEND GAME_SOURCES
        game/wreckwater_character_authority_bridge.cpp
        game/wreckwater_character_movement.cpp
        game/wreckwater_character_step.cpp
        game/wreckwater_content_manifest.cpp
        game/wreckwater_live_world.cpp
        game/wreckwater_match.cpp
        game/wreckwater_replay.cpp
        game/wreckwater_replay_playback.cpp
        game/wreckwater_vessel_damage.cpp
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gameplay/vertical_slices.cpp")
    list(APPEND GAMEPLAY_SOURCES
        gameplay/structural_assembly.cpp
        gameplay/platform_parity.cpp
        gameplay/vertical_slices.cpp
        gameplay/water_dynamics.cpp
    )
endif()

# RIDGEBREAK moto game layer (Phase R: mesh pipeline + motorcycle).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/moto/vmesh_io.cpp")
    list(APPEND MOTO_SOURCES
        moto/bike.cpp
        moto/race.cpp
        moto/session.cpp
        moto/vmesh_io.cpp
        moto/world.cpp
    )
endif()

# Core utilities (Phase 1.4)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/log.cpp")
    list(APPEND CORE_SOURCES
        core/log.cpp
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/server/world_coordinator.cpp")
    list(APPEND SERVER_SOURCES
        server/native_gpu_backend.cpp
        server/ridgebreak_authority.cpp
        server/ridgebreak_server_runtime.cpp
        server/wreckwater_authority_runtime.cpp
        server/world_coordinator.cpp
    )
endif()

# Deterministic realtime protocol and prediction core.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/network/protocol.cpp")
    list(APPEND NETWORK_SOURCES
        network/authoritative_sandbox.cpp
        network/deterministic_adversity_transport.cpp
        network/multiplayer_session.cpp
        network/native_tcp_transport.cpp
        network/protocol.cpp
        network/replication.cpp
        network/ridgebreak_client_replication.cpp
        network/ridgebreak_protocol.cpp
        network/session_transport.hpp
        network/transport.cpp
        network/wreckwater_client_replication.cpp
        network/wreckwater_client_runtime.cpp
        network/wreckwater_protocol.cpp
    )
endif()

if(EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/client/wreckwater_character_controller.cpp")
    list(APPEND CLIENT_SOURCES
        client/wreckwater_character_platform_timeline.cpp
        client/wreckwater_character_controller.cpp
    )
endif()
if(EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/client/wreckwater_character_presentation.cpp")
    list(APPEND CLIENT_SOURCES
        client/wreckwater_character_presentation.cpp
    )
endif()
if(EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/client/wreckwater_third_person_camera.cpp")
    list(APPEND CLIENT_SOURCES
        client/wreckwater_third_person_camera.cpp
    )
endif()
if(EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/client/wreckwater_graphical_client_loop.cpp")
    list(APPEND CLIENT_SOURCES
        client/wreckwater_application_client.cpp
        client/wreckwater_client_visual_clock.cpp
        client/wreckwater_graphical_client_loop.cpp
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
            gpu/context_surface.cpp
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/heightmap.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/heightmap.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/authored_cove.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/authored_cove.cpp
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/terrain/shadow_bake.cpp")
    list(APPEND TERRAIN_SOURCES
        terrain/shadow_bake.cpp
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/mesh_path.cpp")
    list(APPEND RENDER_SOURCES
        render/mesh_path.cpp
        render/environment_lighting.cpp
        render/salvage_asset_fixture.cpp
        render/inspection_guides.cpp
        render/cove_dock_markings.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/blit_path.cpp")
    list(APPEND RENDER_SOURCES
        render/blit_path.cpp
        render/opaque_scene.cpp
        render/water_clipmap_mesh.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/water_simulation.cpp")
    list(APPEND RENDER_SOURCES
        render/water_simulation.cpp
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render/primitive_path.cpp")
    list(APPEND RENDER_SOURCES
        render/primitive_culling.cpp
        render/primitive_gpu_culling.cpp
        render/primitive_instance_packing.cpp
        render/primitive_path.cpp
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

# Physics (Jolt on both native and WebAssembly)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/physics/physics_world.cpp")
    list(APPEND PHYSICS_SOURCES
        physics/box3d/box3d_backend.cpp
        physics/character/cpu_capsule_mover.cpp
        physics/deterministic/lockstep_world.cpp
        physics/deterministic/replay.cpp
        physics/gpu/debug_readback_ring.cpp
        physics/gpu/deterministic_primitives.cpp
        physics/gpu/gpu_buffer_arena.cpp
    physics/gpu/gpu_broad_phase.cpp
    physics/gpu/gpu_ccd.cpp
    physics/gpu/gpu_attachments.cpp
    physics/gpu/gpu_dynamic_solver.cpp
    physics/gpu/gpu_event_readback.cpp
    physics/gpu/gpu_islands.cpp
    physics/gpu/gpu_lockstep.cpp
    physics/gpu/gpu_narrow_phase.cpp
    physics/gpu/gpu_queries.cpp
        physics/gpu/gpu_physics_backend.cpp
        physics/jolt/jolt_backend.cpp
        physics/physics_backend_factory.cpp
        physics/physics_types.cpp
        physics/rigid_mass_frame.cpp
        physics/authored_body_frame.cpp
        physics/authored_shape.cpp
        physics/authored_shape_pool.cpp
        physics/gpu/gpu_authored_shapes.cpp
        physics/physics_world.cpp
    )
endif()

# Native WRECKWATER authority. Keep this target independent from ALL_SOURCES:
# the server must not inherit voxy_core's client, presentation, renderer,
# terrain-presentation, Jolt, or Box3D implementation closure.
if(VOXY_BUILD_NATIVE
   AND NOT EMSCRIPTEN
   AND EXISTS
       "${CMAKE_CURRENT_SOURCE_DIR}/server/wreckwater_authority_runtime.cpp")
    set(VOXY_WRECKWATER_HEADLESS_AUTHORITY_SOURCES
        core/log.cpp
        game/wreckwater_character_authority_bridge.cpp
        game/wreckwater_character_movement.cpp
        game/wreckwater_character_step.cpp
        game/wreckwater_live_world.cpp
        game/wreckwater_match.cpp
        game/wreckwater_replay.cpp
        game/wreckwater_vessel_damage.cpp
        gpu/context.cpp
        gpu/resources.cpp
        network/native_tcp_transport.cpp
        network/protocol.cpp
        network/wreckwater_protocol.cpp
        geometry/box_union.cpp
        physics/authored_body_frame.cpp
        physics/authored_shape.cpp
        physics/authored_shape_pool.cpp
        physics/rigid_mass_frame.cpp
        physics/gpu/gpu_authored_shapes.cpp
        physics/character/cpu_capsule_mover.cpp
        physics/gpu/debug_readback_ring.cpp
        physics/gpu/deterministic_primitives.cpp
        physics/gpu/gpu_attachments.cpp
        physics/gpu/gpu_broad_phase.cpp
        physics/gpu/gpu_buffer_arena.cpp
        physics/gpu/gpu_ccd.cpp
        physics/gpu/gpu_dynamic_solver.cpp
        physics/gpu/gpu_event_readback.cpp
        physics/gpu/gpu_islands.cpp
        physics/gpu/gpu_narrow_phase.cpp
        physics/gpu/gpu_physics_backend.cpp
        physics/gpu/gpu_queries.cpp
        physics/physics_types.cpp
        physics/physics_world.cpp
        physics/wreckwater_authority_backend_factory.cpp
        server/wreckwater_authority_runtime.cpp
        terrain/mip_generator.cpp
    )

    # The manifest is the source contract for both build systems. Fail during
    # configuration if the library TUs plus server main differ from the source
    # TUs represented by the generated authority identity.
    set(VOXY_WRECKWATER_CMAKE_AUTHORITY_TUS
        ${VOXY_WRECKWATER_HEADLESS_AUTHORITY_SOURCES}
        server/wreckwater_server_main.cpp
    )
    set(VOXY_WRECKWATER_MANIFEST_AUTHORITY_TUS "")
    foreach(VOXY_WRECKWATER_MANIFEST_INPUT
            IN LISTS VOXY_WRECKWATER_AUTHORITY_CONTENT_RELATIVE_INPUTS)
        if(VOXY_WRECKWATER_MANIFEST_INPUT
           MATCHES "^src/(.+\\.cpp)$")
            list(APPEND VOXY_WRECKWATER_MANIFEST_AUTHORITY_TUS
                "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(SORT VOXY_WRECKWATER_CMAKE_AUTHORITY_TUS)
    list(SORT VOXY_WRECKWATER_MANIFEST_AUTHORITY_TUS)
    if(NOT "${VOXY_WRECKWATER_CMAKE_AUTHORITY_TUS}"
       STREQUAL "${VOXY_WRECKWATER_MANIFEST_AUTHORITY_TUS}")
        message(FATAL_ERROR
            "CMake WRECKWATER authority TUs differ from the manifest")
    endif()

    add_library(wreckwater_headless_authority STATIC
        ${VOXY_WRECKWATER_HEADLESS_AUTHORITY_SOURCES}
    )
    target_include_directories(wreckwater_headless_authority PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(
        wreckwater_headless_authority PRIVATE VOXY_NATIVE=1)

    if(TARGET glm)
        target_link_libraries(wreckwater_headless_authority PUBLIC glm)
    else()
        message(FATAL_ERROR
            "WRECKWATER headless authority requires the glm target")
    endif()

    if(TARGET wgpu::webgpu)
        target_link_libraries(
            wreckwater_headless_authority PUBLIC wgpu::webgpu)
        # FindWGPU may resolve include/webgpu for webgpu.h. context.cpp also
        # consumes wgpu-native's extension header at include/wgpu/wgpu.h.
        if(WGPU_INCLUDE_DIR)
            get_filename_component(
                VOXY_WGPU_INCLUDE_PARENT "${WGPU_INCLUDE_DIR}" DIRECTORY)
            if(EXISTS "${VOXY_WGPU_INCLUDE_PARENT}/wgpu/wgpu.h")
                target_include_directories(
                    wreckwater_headless_authority SYSTEM PRIVATE
                    "${VOXY_WGPU_INCLUDE_PARENT}"
                )
            endif()
        endif()
    elseif(TARGET dawn::webgpu)
        target_link_libraries(
            wreckwater_headless_authority PUBLIC dawn::webgpu)
    else()
        message(FATAL_ERROR
            "WRECKWATER headless authority requires a WebGPU target")
    endif()

    set_project_warnings(wreckwater_headless_authority)
endif()

# Application layer (Phase 4, 9, 10)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/application.cpp")
    list(APPEND APP_SOURCES
        app/application.cpp
        game/lego_playground.cpp
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
    ${GAME_SOURCES}
    ${GAMEPLAY_SOURCES}
    ${MOTO_SOURCES}
    ${TERRAIN_SOURCES}
    ${VOXEL_SOURCES}
    ${RENDER_SOURCES}
    ${CAMERA_SOURCES}
    ${PHYSICS_SOURCES}
    ${NETWORK_SOURCES}
    ${CLIENT_SOURCES}
    ${SERVER_SOURCES}
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
    
    if(TARGET tinygltf)
        target_link_libraries(voxy_core PUBLIC tinygltf)
    endif()

    if(TARGET stb)
        target_link_libraries(voxy_core PUBLIC stb)
    endif()
    
    if(TARGET tinyexr)
        target_link_libraries(voxy_core PUBLIC tinyexr)
    endif()

    if(TARGET Jolt)
        target_link_libraries(voxy_core PUBLIC Jolt)
        target_compile_definitions(voxy_core PUBLIC VOXY_HAS_JOLT=1)
    endif()

    if(TARGET box3d)
        target_link_libraries(voxy_core PUBLIC box3d)
        target_compile_definitions(voxy_core PUBLIC VOXY_HAS_BOX3D=1)
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
