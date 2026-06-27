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
    test_timer.cpp
    test_config.cpp
    test_biomes.cpp
    test_decorations.cpp
    test_window.cpp
    test_input.cpp
    test_heightmap.cpp
    test_compression.cpp
    test_mip_generator.cpp
    test_camera.cpp
    test_controller.cpp
    test_character_controller.cpp
    test_svo_core.cpp
)

# GPU context tests only when WebGPU is available (wgpu-native or Dawn)
if(TARGET wgpu::webgpu OR TARGET dawn::webgpu)
    list(APPEND TEST_SOURCES 
        test_gpu_context.cpp
        test_gpu_resources.cpp
        test_shader_terrain.cpp
        test_shader_raycast.cpp
        test_shader_blit.cpp
        test_shader_decorations.cpp
        test_triangle_path.cpp
        test_raycast_path.cpp
        test_blit_path.cpp
        test_debug_visualizer.cpp
        test_mip_pipeline.cpp
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
gtest_discover_tests(voxy_tests)

# ─────────────────────────────────────────────────────────────────────────────
# Custom Target to Run Tests
# ─────────────────────────────────────────────────────────────────────────────
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS voxy_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running voxy unit tests..."
)
