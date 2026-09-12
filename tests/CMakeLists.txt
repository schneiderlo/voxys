# ═══════════════════════════════════════════════════════════════════════════════
# tests/CMakeLists.txt
# ═══════════════════════════════════════════════════════════════════════════════
# Unit test configuration for voxy using Google Test.
# ═══════════════════════════════════════════════════════════════════════════════

# Require Google Test
if(NOT TARGET gtest)
    message(STATUS "Google Test not available - skipping tests")
    return()
endif()

enable_testing()

# ─────────────────────────────────────────────────────────────────────────────
# Test Sources
# ─────────────────────────────────────────────────────────────────────────────
set(TEST_SOURCES
    test_cooked_part_directory.cpp
    test_fixture_registry.cpp
    test_functional_kit.cpp
    test_pontoon_pipeline.cpp
    test_hierarchy_pipeline.cpp
    test_session_events.cpp
    test_session_transactions.cpp
    test_cooked_part_bundle.cpp
    test_rigid_prefab.cpp
    test_sha256.cpp
    test_game_session.cpp
    test_save_generation.cpp
    test_cove_save.cpp
    test_cove_water_clock.cpp
    ${CMAKE_SOURCE_DIR}/tools/salvage_assets/test_gameplay_sidecar.cpp
    test_build_model.cpp
    test_assembly_compiler.cpp
    test_assembly_collision.cpp
    test_assembly_buoyancy.cpp
    test_assembly_functions.cpp
    test_compiled_assembly.cpp
    test_assembly_fracture.cpp
    test_rigid_mass_frame.cpp
    test_authored_shape.cpp
    test_authored_body_frame.cpp
    test_construction_types.cpp
    test_part_catalog.cpp
    test_salvage_preview.cpp
    test_cove_player.cpp
    test_workshop_camera.cpp
    test_bike.cpp
    test_log.cpp
    test_lockstep.cpp
    test_multiplayer_session.cpp
    test_native_tcp_transport.cpp
    test_networking.cpp
    test_replay.cpp
    test_race.cpp
    test_ridgebreak_authority.cpp
    test_ridgebreak_server_runtime.cpp
    test_timer.cpp
    test_wreckwater_client_replication.cpp
    test_wreckwater_client_runtime.cpp
    test_wreckwater_authority_runtime.cpp
    test_wreckwater_character_authority_bridge.cpp
    test_wreckwater_character_movement.cpp
    test_wreckwater_character_step.cpp
    test_wreckwater_content_manifest.cpp
    test_wreckwater_live_world.cpp
    test_wreckwater_match.cpp
    test_wreckwater_protocol.cpp
    test_wreckwater_replay.cpp
    test_wreckwater_replay_playback.cpp
    test_wreckwater_vessel_damage.cpp
    test_config.cpp
    test_window.cpp
    test_input.cpp
    test_heightmap.cpp
    test_world.cpp
    test_compression.cpp
    test_mip_generator.cpp
    test_moto_session.cpp
    test_camera.cpp
    test_controller.cpp
    test_character_controller.cpp
    test_cpu_capsule_mover.cpp
    test_benchmark.cpp
    test_physics_world.cpp
    test_terrain_topology.cpp
    test_lego_surface.cpp
    test_lego_playground.cpp
    test_vertical_slices.cpp
    test_water_clipmap_mesh.cpp
    test_svo_core.cpp
)

# GPU context tests only when WebGPU is available (wgpu-native or Dawn)
if(TARGET wgpu::webgpu OR TARGET dawn::webgpu)
    list(APPEND TEST_SOURCES 
        test_gpu_context.cpp
        test_wreckwater_authority_gpu.cpp
        test_gpu_ccd.cpp
        test_gpu_dynamic_solver.cpp
        test_gpu_event_readback.cpp
        test_gpu_islands.cpp
        test_gpu_resources.cpp
        test_gpu_physics.cpp
        test_gpu_authored_shapes.cpp
        test_gpu_broad_phase.cpp
        test_gpu_narrow_phase.cpp
        test_gpu_queries.cpp
        test_deterministic_gpu_primitives.cpp
        test_distributed_server.cpp
        test_shader_terrain.cpp
        test_shader_raycast.cpp
        test_shader_blit.cpp
        test_triangle_path.cpp
        test_raycast_path.cpp
        test_blit_path.cpp
        test_cove_hud.cpp
        test_water_simulation.cpp
        test_debug_visualizer.cpp
    test_mip_pipeline.cpp
        test_mesh_path.cpp
        test_environment_lighting.cpp
        test_salvage_asset_fixture.cpp
        test_primitive_path.cpp
        test_primitive_gpu_culling.cpp
        test_primitive_culling.cpp
    test_shadow_bake.cpp
        test_terrain_textures.cpp
        test_application.cpp
    )
    message(STATUS "  Including GPU tests (WebGPU available)")
else()
    message(STATUS "  Skipping GPU tests (WebGPU not available)")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Test Executable
# ─────────────────────────────────────────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    list(APPEND TEST_SOURCES test_native_save_store.cpp test_native_save_worker.cpp)
    add_executable(native_save_store_tests test_native_save_store.cpp test_native_save_worker.cpp test_save_generation.cpp
        ${CMAKE_SOURCE_DIR}/src/engine/platform/native/save_store.cpp
        ${CMAKE_SOURCE_DIR}/src/engine/platform/native/save_worker.cpp
        ${CMAKE_SOURCE_DIR}/src/game/expedition/save_generation.cpp
        ${CMAKE_SOURCE_DIR}/src/game/expedition/session_save.cpp
        ${CMAKE_SOURCE_DIR}/src/game/expedition/session_recovery.cpp
        ${CMAKE_SOURCE_DIR}/src/game/expedition/game_session.cpp
        ${CMAKE_SOURCE_DIR}/src/game/expedition/session_events.cpp
        ${CMAKE_SOURCE_DIR}/src/game/expedition/session_transactions.cpp
        ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
        ${CMAKE_SOURCE_DIR}/src/game/construction/build_refit.cpp
        ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
        ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp)
    target_link_libraries(native_save_store_tests PRIVATE gtest gtest_main)
    target_include_directories(native_save_store_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    set_project_warnings(native_save_store_tests)
endif()
add_executable(authored_body_frame_tests test_authored_body_frame.cpp)
target_link_libraries(authored_body_frame_tests PRIVATE voxy_core gtest gtest_main)
set_project_warnings(authored_body_frame_tests)
if(TARGET wgpu::webgpu OR TARGET dawn::webgpu)
    add_executable(gpu_authored_shapes_tests test_gpu_authored_shapes.cpp)
    target_link_libraries(gpu_authored_shapes_tests PRIVATE voxy_core gtest gtest_main)
    set_project_warnings(gpu_authored_shapes_tests)
    add_executable(salvage_asset_fixture_tests test_salvage_asset_fixture.cpp)
    target_link_libraries(salvage_asset_fixture_tests PRIVATE voxy_core tinygltf gtest gtest_main)
    set_project_warnings(salvage_asset_fixture_tests)
endif()

add_executable(fixture_registry_tests test_fixture_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/fixture_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/cooked_part_directory.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/cooked_part_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/gameplay_sidecar.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/rigid_prefab.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/moto/vmesh_io.cpp)
target_include_directories(fixture_registry_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fixture_registry_tests PRIVATE glm tinygltf gtest gtest_main)
set_project_warnings(fixture_registry_tests)

add_executable(pontoon_pipeline_tests test_pontoon_pipeline.cpp)
target_link_libraries(pontoon_pipeline_tests PRIVATE voxy_core tinygltf gtest gtest_main)
set_project_warnings(pontoon_pipeline_tests)

add_executable(functional_kit_tests test_functional_kit.cpp)
target_link_libraries(functional_kit_tests PRIVATE voxy_core tinygltf gtest gtest_main)
set_project_warnings(functional_kit_tests)

add_executable(hierarchy_pipeline_tests test_hierarchy_pipeline.cpp)
target_link_libraries(hierarchy_pipeline_tests PRIVATE voxy_core tinygltf gtest gtest_main)
set_project_warnings(hierarchy_pipeline_tests)

add_executable(session_events_tests
    test_session_events.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/session_events.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(session_events_tests PRIVATE gtest gtest_main)
target_include_directories(session_events_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(session_events_tests)

add_executable(session_transactions_tests
    test_session_transactions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/session_transactions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_refit.cpp
)
target_link_libraries(session_transactions_tests PRIVATE gtest gtest_main)
target_include_directories(session_transactions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(session_transactions_tests)

add_executable(cooked_part_directory_tests
    test_cooked_part_directory.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/cooked_part_directory.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/cooked_part_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/gameplay_sidecar.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/rigid_prefab.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/moto/vmesh_io.cpp
)
target_link_libraries(cooked_part_directory_tests PRIVATE glm tinygltf gtest gtest_main)
target_include_directories(cooked_part_directory_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(cooked_part_directory_tests)

add_executable(rigid_prefab_tests
    test_rigid_prefab.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/rigid_prefab.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(rigid_prefab_tests PRIVATE glm gtest gtest_main)
target_include_directories(rigid_prefab_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(rigid_prefab_tests)

add_executable(cooked_part_bundle_tests
    test_cooked_part_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/cooked_part_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/gameplay_sidecar.cpp
    ${CMAKE_SOURCE_DIR}/src/game/assets/rigid_prefab.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/moto/vmesh_io.cpp
)
target_link_libraries(cooked_part_bundle_tests PRIVATE glm tinygltf gtest gtest_main)
target_include_directories(cooked_part_bundle_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(cooked_part_bundle_tests)

add_executable(sha256_tests test_sha256.cpp)
target_link_libraries(sha256_tests PRIVATE gtest gtest_main)
target_include_directories(sha256_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(sha256_tests)

add_executable(game_session_tests
    test_game_session.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/session_events.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/session_transactions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/game_session.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/session_recovery.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expedition/session_save.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_refit.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(game_session_tests PRIVATE gtest gtest_main)
target_include_directories(game_session_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(game_session_tests)

add_executable(gameplay_sidecar_tests
    ${CMAKE_SOURCE_DIR}/src/game/assets/gameplay_sidecar.cpp
    ${CMAKE_SOURCE_DIR}/tools/salvage_assets/test_gameplay_sidecar.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
)
target_link_libraries(gameplay_sidecar_tests PRIVATE tinygltf gtest gtest_main)
target_include_directories(gameplay_sidecar_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools
)
set_project_warnings(gameplay_sidecar_tests)

add_executable(build_model_tests
    test_build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(build_model_tests PRIVATE gtest gtest_main)
target_include_directories(build_model_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(build_model_tests)

add_executable(assembly_compiler_tests
    test_assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(assembly_compiler_tests PRIVATE gtest gtest_main)
target_include_directories(assembly_compiler_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(assembly_compiler_tests)

add_executable(assembly_collision_tests
    test_assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/box_union.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(assembly_collision_tests PRIVATE gtest gtest_main)
target_include_directories(assembly_collision_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(assembly_collision_tests)

add_executable(assembly_buoyancy_tests
    test_assembly_buoyancy.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_buoyancy.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/orthogonal_coverage.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/box_union.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(assembly_buoyancy_tests PRIVATE gtest gtest_main)
target_include_directories(assembly_buoyancy_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(assembly_buoyancy_tests)

add_executable(assembly_functions_tests
    test_assembly_functions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_functions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_buoyancy.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/orthogonal_coverage.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/box_union.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(assembly_functions_tests PRIVATE gtest gtest_main)
target_include_directories(assembly_functions_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(assembly_functions_tests)

add_executable(compiled_assembly_tests
    test_compiled_assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/compiled_assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_functions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_buoyancy.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/orthogonal_coverage.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/box_union.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(compiled_assembly_tests PRIVATE gtest gtest_main)
target_include_directories(compiled_assembly_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(compiled_assembly_tests)

add_executable(assembly_fracture_tests test_assembly_fracture.cpp)
target_link_libraries(assembly_fracture_tests PRIVATE voxy_core gtest gtest_main)
set_project_warnings(assembly_fracture_tests)

add_executable(rigid_mass_frame_tests
    test_rigid_mass_frame.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/rigid_mass_frame.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/compiled_assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_functions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_buoyancy.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/orthogonal_coverage.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/box_union.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(rigid_mass_frame_tests PRIVATE gtest gtest_main)
target_include_directories(rigid_mass_frame_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(rigid_mass_frame_tests)

add_executable(authored_shape_tests
    test_authored_shape.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/rigid_mass_frame.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/authored_shape.cpp
    ${CMAKE_SOURCE_DIR}/src/physics/authored_shape_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/compiled_assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_functions.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_buoyancy.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/orthogonal_coverage.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/geometry/box_union.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/assembly_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/build_model.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(authored_shape_tests PRIVATE gtest gtest_main)
target_include_directories(authored_shape_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(authored_shape_tests)

add_executable(part_catalog_tests
    test_part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/part_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(part_catalog_tests PRIVATE gtest gtest_main)
target_include_directories(part_catalog_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(part_catalog_tests)

add_executable(construction_types_tests
    test_construction_types.cpp
    ${CMAKE_SOURCE_DIR}/src/game/construction/construction_types.cpp
)
target_link_libraries(construction_types_tests PRIVATE gtest gtest_main)
target_include_directories(construction_types_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_project_warnings(construction_types_tests)

add_executable(deterministic_adversity_transport_tests
    test_deterministic_adversity_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/network/deterministic_adversity_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
)

target_link_libraries(deterministic_adversity_transport_tests PRIVATE
    gtest
    gtest_main
)

target_include_directories(deterministic_adversity_transport_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        deterministic_adversity_transport_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(deterministic_adversity_transport_tests)

add_executable(wreckwater_protocol_tests
    test_wreckwater_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_protocol.cpp
)

target_link_libraries(wreckwater_protocol_tests PRIVATE
    gtest
    gtest_main
)

target_include_directories(wreckwater_protocol_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(wreckwater_protocol_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_protocol_tests)

add_executable(wreckwater_client_replication_tests
    test_wreckwater_client_replication.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_client_replication.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_protocol.cpp
)

target_link_libraries(wreckwater_client_replication_tests PRIVATE
    gtest
    gtest_main
)

target_include_directories(wreckwater_client_replication_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_client_replication_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_client_replication_tests)

add_executable(wreckwater_client_runtime_tests
    test_wreckwater_client_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_client_replication.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_client_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_protocol.cpp
)

target_link_libraries(wreckwater_client_runtime_tests PRIVATE
    gtest
    gtest_main
)

target_include_directories(wreckwater_client_runtime_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_client_runtime_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_client_runtime_tests)

add_executable(wreckwater_client_probe_tests
    test_wreckwater_client_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/client/wreckwater_client_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wreckwater_protocol.cpp
)

target_link_libraries(wreckwater_client_probe_tests PRIVATE
    gtest
    gtest_main
)

target_include_directories(wreckwater_client_probe_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_client_probe_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_client_probe_tests)

add_executable(wreckwater_character_controller_tests
    test_wreckwater_character_controller.cpp
)

target_link_libraries(wreckwater_character_controller_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(wreckwater_character_controller_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_character_controller_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_character_controller_tests)

add_executable(wreckwater_character_platform_timeline_tests
    test_wreckwater_character_platform_timeline.cpp
)

target_link_libraries(
    wreckwater_character_platform_timeline_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(
    wreckwater_character_platform_timeline_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_character_platform_timeline_tests
        PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(
    wreckwater_character_platform_timeline_tests)

add_executable(wreckwater_character_presentation_tests
    test_wreckwater_character_presentation.cpp
)

target_link_libraries(wreckwater_character_presentation_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(wreckwater_character_presentation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_character_presentation_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_character_presentation_tests)

add_executable(wreckwater_application_client_tests
    test_wreckwater_application_client.cpp
)

target_link_libraries(wreckwater_application_client_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(
    wreckwater_application_client_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_application_client_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_application_client_tests)

add_executable(wreckwater_third_person_camera_tests
    test_wreckwater_third_person_camera.cpp
)

target_link_libraries(wreckwater_third_person_camera_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(wreckwater_third_person_camera_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_third_person_camera_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(wreckwater_third_person_camera_tests)

add_executable(wreckwater_graphical_client_loop_tests
    test_wreckwater_graphical_client_loop.cpp
)

target_link_libraries(
    wreckwater_graphical_client_loop_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(
    wreckwater_graphical_client_loop_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_graphical_client_loop_tests
        PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(
    wreckwater_graphical_client_loop_tests)

add_executable(wreckwater_client_visual_clock_tests
    test_wreckwater_client_visual_clock.cpp
)

target_link_libraries(
    wreckwater_client_visual_clock_tests PRIVATE
    voxy_core
    gtest
    gtest_main
)

target_include_directories(
    wreckwater_client_visual_clock_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(
        wreckwater_client_visual_clock_tests
        PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(
    wreckwater_client_visual_clock_tests)

add_executable(voxy_tests ${TEST_SOURCES})

target_link_libraries(voxy_tests PRIVATE
    tinygltf
    voxy_core
    gtest
    gtest_main
)

target_include_directories(voxy_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tools
)

# Define VOXY_NATIVE for native builds
if(NOT EMSCRIPTEN)
    target_compile_definitions(voxy_tests PRIVATE VOXY_NATIVE=1)
endif()

# Ensure voxy_core is built
if(TARGET voxy_core)
    add_dependencies(voxy_tests voxy_core)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Register Tests with CTest
# ─────────────────────────────────────────────────────────────────────────────
include(GoogleTest)
if(TARGET salvage_asset_fixture_tests)
    gtest_discover_tests(salvage_asset_fixture_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST TEST_PREFIX "salvage_asset_fixture.")
endif()
gtest_discover_tests(fixture_registry_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "fixture_registry.")
gtest_discover_tests(pontoon_pipeline_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "pontoon_pipeline.")
gtest_discover_tests(functional_kit_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "functional_kit.")
gtest_discover_tests(hierarchy_pipeline_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "hierarchy_pipeline.")
gtest_discover_tests(assembly_compiler_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "assembly_compiler.")
gtest_discover_tests(assembly_collision_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "assembly_collision.")
gtest_discover_tests(assembly_buoyancy_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "assembly_buoyancy.")
gtest_discover_tests(session_events_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} DISCOVERY_MODE PRE_TEST TEST_PREFIX "session_events.")
gtest_discover_tests(session_transactions_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} DISCOVERY_MODE PRE_TEST TEST_PREFIX "session_transactions.")
gtest_discover_tests(cooked_part_directory_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} DISCOVERY_MODE PRE_TEST TEST_PREFIX "cooked_part_directory.")
gtest_discover_tests(rigid_prefab_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} DISCOVERY_MODE PRE_TEST TEST_PREFIX "rigid_prefab.")
gtest_discover_tests(cooked_part_bundle_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} DISCOVERY_MODE PRE_TEST TEST_PREFIX "cooked_part_bundle.")
gtest_discover_tests(sha256_tests DISCOVERY_MODE PRE_TEST TEST_PREFIX "sha256.")
gtest_discover_tests(game_session_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "game_session."
)
gtest_discover_tests(gameplay_sidecar_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "gameplay_sidecar."
)
if(TARGET gameplay_sidecar_tool AND TARGET gltf_vmesh_tool
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(
        NAME salvage_asset_cook
        COMMAND "${CMAKE_COMMAND}" -E env
            "SALVAGE_CONVERTER=$<TARGET_FILE:gltf_vmesh_tool>"
            "SALVAGE_VALIDATOR=$<TARGET_FILE:gameplay_sidecar_tool>"
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/salvage_assets/test_cook_gameplay_asset.py"
    )
    set_tests_properties(salvage_asset_cook PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 60
    )
endif()
gtest_discover_tests(build_model_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "build_model."
)
gtest_discover_tests(part_catalog_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "part_catalog."
)
gtest_discover_tests(construction_types_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "construction_types."
)
gtest_discover_tests(deterministic_adversity_transport_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "deterministic_adversity_transport."
)
gtest_discover_tests(wreckwater_protocol_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_protocol."
)
gtest_discover_tests(wreckwater_client_replication_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_client_replication."
)
gtest_discover_tests(wreckwater_client_runtime_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_client_runtime."
)
gtest_discover_tests(wreckwater_client_probe_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_client_probe."
)
gtest_discover_tests(wreckwater_character_controller_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_character_controller."
)
gtest_discover_tests(
    wreckwater_character_platform_timeline_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_character_platform_timeline."
)
gtest_discover_tests(wreckwater_character_presentation_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_character_presentation."
)
gtest_discover_tests(wreckwater_application_client_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_application_client."
)
gtest_discover_tests(wreckwater_third_person_camera_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_third_person_camera."
)
gtest_discover_tests(wreckwater_graphical_client_loop_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_graphical_client_loop."
)
gtest_discover_tests(wreckwater_client_visual_clock_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "wreckwater_client_visual_clock."
)
gtest_discover_tests(voxy_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
)
# Offline glTF -> .vmesh converter test. Compiles the tool with main() disabled
# and links it against the core/stb/tinygltf closure.
add_executable(gltf_vmesh_tool_tests
    test_gltf_vmesh_tool.cpp
    ${CMAKE_SOURCE_DIR}/tools/gltf_vmesh_tool.cpp
)

target_link_libraries(gltf_vmesh_tool_tests PRIVATE
    voxy_core
    tinygltf
    stb
    gtest
    gtest_main
)

target_include_directories(gltf_vmesh_tool_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tools
)

target_compile_definitions(gltf_vmesh_tool_tests PRIVATE
    GLTF_VMESH_TOOL_NO_MAIN
)

if(NOT EMSCRIPTEN)
    target_compile_definitions(gltf_vmesh_tool_tests PRIVATE VOXY_NATIVE=1)
endif()

set_project_warnings(gltf_vmesh_tool_tests)

gtest_discover_tests(gltf_vmesh_tool_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "gltf_vmesh_tool."
)

# ─────────────────────────────────────────────────────────────────────────────
# Custom Target to Run Tests
# ─────────────────────────────────────────────────────────────────────────────
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS voxy_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running voxy unit tests..."
)

# Manual performance/golden executables. They mirror Bazel's `manual` targets
# and are deliberately excluded from ordinary CTest runs.
if(VOXY_BUILD_BENCHMARKS)
    set(CPU_BENCHMARK_SOURCES
        physics_snapshot_benchmark.cpp
        water_lock_benchmark.cpp
        primitive_instance_benchmark.cpp
        primitive_culling_benchmark.cpp
        replication_benchmark.cpp
    )
    set(VOXY_BENCHMARK_TARGETS "")
    foreach(BENCHMARK_SOURCE IN LISTS CPU_BENCHMARK_SOURCES)
        get_filename_component(BENCHMARK_NAME "${BENCHMARK_SOURCE}" NAME_WE)
        add_executable(${BENCHMARK_NAME} ${BENCHMARK_SOURCE})
        target_link_libraries(${BENCHMARK_NAME} PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(${BENCHMARK_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(${BENCHMARK_NAME} PRIVATE VOXY_NATIVE=1)
        set_project_warnings(${BENCHMARK_NAME})
        list(APPEND VOXY_BENCHMARK_TARGETS ${BENCHMARK_NAME})
    endforeach()

    if(TARGET wgpu::webgpu OR TARGET dawn::webgpu)
        add_executable(gpu_radix_benchmark gpu_radix_benchmark.cpp)
        target_link_libraries(gpu_radix_benchmark PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(gpu_radix_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(gpu_radix_benchmark PRIVATE VOXY_NATIVE=1)
        set_project_warnings(gpu_radix_benchmark)
        list(APPEND VOXY_BENCHMARK_TARGETS gpu_radix_benchmark)

        add_executable(gpu_ballistic_benchmark
            gpu_ballistic_benchmark.cpp
        )
        target_link_libraries(gpu_ballistic_benchmark PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(gpu_ballistic_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(gpu_ballistic_benchmark PRIVATE
            VOXY_NATIVE=1
        )
        set_project_warnings(gpu_ballistic_benchmark)
        list(APPEND VOXY_BENCHMARK_TARGETS gpu_ballistic_benchmark)

        add_executable(gpu_static_benchmark
            gpu_static_benchmark.cpp
        )
        target_link_libraries(gpu_static_benchmark PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(gpu_static_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(gpu_static_benchmark PRIVATE
            VOXY_NATIVE=1
        )
        set_project_warnings(gpu_static_benchmark)
        list(APPEND VOXY_BENCHMARK_TARGETS gpu_static_benchmark)

        add_executable(gpu_dynamic_benchmark
            gpu_dynamic_benchmark.cpp
        )
        target_link_libraries(gpu_dynamic_benchmark PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(gpu_dynamic_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(gpu_dynamic_benchmark PRIVATE
            VOXY_NATIVE=1
        )
        set_project_warnings(gpu_dynamic_benchmark)
        list(APPEND VOXY_BENCHMARK_TARGETS gpu_dynamic_benchmark)

        add_executable(native_server_batch_benchmark
            native_server_batch_benchmark.cpp
        )
        target_link_libraries(native_server_batch_benchmark PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(native_server_batch_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(native_server_batch_benchmark PRIVATE
            VOXY_NATIVE=1
        )
        set_project_warnings(native_server_batch_benchmark)
        list(APPEND VOXY_BENCHMARK_TARGETS native_server_batch_benchmark)

        add_executable(primitive_upload_benchmark
            primitive_upload_benchmark.cpp
        )
        target_link_libraries(primitive_upload_benchmark PRIVATE
            voxy_core gtest gtest_main
        )
        target_include_directories(primitive_upload_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(primitive_upload_benchmark PRIVATE
            VOXY_NATIVE=1
        )
        set_project_warnings(primitive_upload_benchmark)
        list(APPEND VOXY_BENCHMARK_TARGETS primitive_upload_benchmark)
    endif()

    add_custom_target(voxy_benchmarks
        DEPENDS ${VOXY_BENCHMARK_TARGETS}
    )
endif()

gtest_discover_tests(assembly_functions_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "assembly_functions.")

gtest_discover_tests(compiled_assembly_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "compiled_assembly.")

gtest_discover_tests(assembly_fracture_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "assembly_fracture.")

gtest_discover_tests(rigid_mass_frame_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "rigid_mass_frame.")

gtest_discover_tests(authored_shape_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "authored_shape.")
gtest_discover_tests(authored_body_frame_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "authored_body_frame.")
if(TARGET gpu_authored_shapes_tests)
    gtest_discover_tests(gpu_authored_shapes_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST TEST_PREFIX "gpu_authored_shapes.")
endif()

add_executable(workshop_camera_tests test_workshop_camera.cpp)
target_link_libraries(workshop_camera_tests PRIVATE voxy_core gtest gtest_main)
set_project_warnings(workshop_camera_tests)
gtest_discover_tests(workshop_camera_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(cove_player_tests test_cove_player.cpp)
target_link_libraries(cove_player_tests PRIVATE voxy_core tinygltf gtest gtest_main)

add_executable(cove_save_tests test_cove_save.cpp)
target_link_libraries(cove_save_tests PRIVATE voxy_core gtest gtest_main)
set_project_warnings(cove_save_tests)
gtest_discover_tests(cove_save_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "cove_save.")
set_project_warnings(cove_player_tests)
gtest_discover_tests(cove_player_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST TEST_PREFIX "cove_player.")

add_executable(cove_water_clock_tests test_cove_water_clock.cpp)
target_link_libraries(cove_water_clock_tests PRIVATE voxy_core gtest gtest_main)
set_project_warnings(cove_water_clock_tests)
gtest_discover_tests(cove_water_clock_tests DISCOVERY_MODE PRE_TEST TEST_PREFIX "cove_water_clock.")
