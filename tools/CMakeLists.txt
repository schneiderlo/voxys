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

# ─────────────────────────────────────────────────────────────────────────────
# gltf_vmesh_tool - Offline glTF 2.0 -> .vmesh converter
# ─────────────────────────────────────────────────────────────────────────────
if(TARGET voxy_core AND TARGET tinygltf AND TARGET stb)
    add_executable(gltf_vmesh_tool
        gltf_vmesh_tool.cpp
    )
    target_link_libraries(gltf_vmesh_tool PRIVATE
        voxy_core
        tinygltf
        stb
    )
    target_include_directories(gltf_vmesh_tool PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_project_warnings(gltf_vmesh_tool)
endif()
