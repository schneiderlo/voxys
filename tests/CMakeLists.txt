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
        test_water_simulation.cpp
        test_debug_visualizer.cpp
    test_mip_pipeline.cpp
        test_mesh_path.cpp
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
    voxy_core
    gtest
    gtest_main
)

target_include_directories(voxy_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
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
