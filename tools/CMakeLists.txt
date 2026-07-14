# ═══════════════════════════════════════════════════════════════════════════════
# tools/CMakeLists.txt
# ═══════════════════════════════════════════════════════════════════════════════
# Command-line tools for voxy project.
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# ldh_tool - Heightmap compression utility
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ldh_tool/CMakeLists.txt")
    add_subdirectory(ldh_tool)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/physics_benchmark/CMakeLists.txt")
    add_subdirectory(physics_benchmark)
endif()

if(TARGET voxy_core)
    add_executable(platform_parity_probe
        ${CMAKE_SOURCE_DIR}/src/gameplay/parity_probe_main.cpp
    )
    target_link_libraries(platform_parity_probe PRIVATE voxy_core)
    target_include_directories(platform_parity_probe PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    set_project_warnings(platform_parity_probe)
endif()
