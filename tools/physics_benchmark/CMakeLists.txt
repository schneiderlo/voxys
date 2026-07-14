add_executable(voxy_physics_benchmark main.cpp)
target_link_libraries(voxy_physics_benchmark PRIVATE voxy_core)
target_include_directories(voxy_physics_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(voxy_physics_benchmark PRIVATE VOXY_NATIVE=1)
set_project_warnings(voxy_physics_benchmark)

if(BUILD_TESTING)
    add_test(
        NAME physics_benchmark_smoke_jolt
        COMMAND voxy_physics_benchmark --backend jolt --scene junkyard
                --bodies 24 --warmup 1 --frames 2 --format json
    )
    add_test(
        NAME physics_benchmark_smoke_box3d
        COMMAND voxy_physics_benchmark --backend box3d --scene junkyard
                --bodies 24 --warmup 1 --frames 2 --format json
    )
endif()
