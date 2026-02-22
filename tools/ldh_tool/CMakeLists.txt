# ═══════════════════════════════════════════════════════════════════════════════
# tools/ldh_tool/CMakeLists.txt
# ═══════════════════════════════════════════════════════════════════════════════
# LDH Tool - Command-line heightmap compression utility
# ═══════════════════════════════════════════════════════════════════════════════

add_executable(ldh_tool main.cpp)

target_link_libraries(ldh_tool PRIVATE voxy_core)

target_include_directories(ldh_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link zstd (needed for compression)
if(TARGET libzstd_static)
    target_link_libraries(ldh_tool PRIVATE libzstd_static)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/zstd/lib")
        target_include_directories(ldh_tool PRIVATE
            "${CMAKE_SOURCE_DIR}/third_party/zstd/lib"
        )
    endif()
elseif(TARGET zstd::libzstd_static)
    target_link_libraries(ldh_tool PRIVATE zstd::libzstd_static)
endif()

# Apply compiler warnings
set_project_warnings(ldh_tool)

# Install rule
install(TARGETS ldh_tool RUNTIME DESTINATION bin)

message(STATUS "Configuring ldh_tool - heightmap compression utility")



