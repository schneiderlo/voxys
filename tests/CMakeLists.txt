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
    test_log.cpp
    test_lockstep.cpp
    test_networking.cpp
    test_replay.cpp
    test_timer.cpp
    test_config.cpp
    test_window.cpp
    test_input.cpp
    test_heightmap.cpp
    test_compression.cpp
    test_mip_generator.cpp
    test_camera.cpp
    test_controller.cpp
    test_character_controller.cpp
    test_cpu_capsule_mover.cpp
    test_benchmark.cpp
    test_physics_world.cpp
    test_terrain_topology.cpp
    test_vertical_slices.cpp
    test_svo_core.cpp
)

# GPU context tests only when WebGPU is available (wgpu-native or Dawn)
if(TARGET wgpu::webgpu OR TARGET dawn::webgpu)
    list(APPEND TEST_SOURCES 
        test_gpu_context.cpp
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
gtest_discover_tests(voxy_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
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
