# List of test source files
TEST_SRCS = [
    "test_bike.cpp",
    "test_log.cpp",
    "test_lockstep.cpp",
    "test_networking.cpp",
    "test_replay.cpp",
    "test_timer.cpp",
    "test_config.cpp",
    "test_window.cpp",
    "test_input.cpp",
    "test_heightmap.cpp",
    "test_world.cpp",
    "test_compression.cpp",
    "test_mip_generator.cpp",
    "test_camera.cpp",
    "test_controller.cpp",
    "test_character_controller.cpp",
    "test_cpu_capsule_mover.cpp",
    "test_physics_world.cpp",
    "test_terrain_topology.cpp",
    "test_lego_surface.cpp",
    "test_vertical_slices.cpp",
    "test_gpu_context.cpp",
    "test_gpu_ccd.cpp",
    "test_gpu_dynamic_solver.cpp",
    "test_gpu_event_readback.cpp",
    "test_gpu_islands.cpp",
    "test_gpu_resources.cpp",
    "test_gpu_physics.cpp",
    "test_lego_playground.cpp",
    "test_salvage_preview.cpp",
    "test_gpu_broad_phase.cpp",
    "test_gpu_narrow_phase.cpp",
    "test_gpu_queries.cpp",
    "test_deterministic_gpu_primitives.cpp",
    "test_distributed_server.cpp",
    "test_shader_terrain.cpp",
    "test_shader_raycast.cpp",
    "test_shader_blit.cpp",
    "test_triangle_path.cpp",
    "test_raycast_path.cpp",
    "test_blit_path.cpp",
    "test_benchmark.cpp",
    "test_debug_visualizer.cpp",
    "test_mip_pipeline.cpp",
    "test_multiplayer_session.cpp",
    "test_primitive_path.cpp",
    "test_primitive_gpu_culling.cpp",
    "test_primitive_culling.cpp",
    "test_shadow_bake.cpp",
    "test_terrain_textures.cpp",
    "test_application.cpp",
    "test_svo_core.cpp",
]

load("//settings:configure_copts.bzl", "PROJECT_TEST_COPTS")

cc_test(
    name = "workshop_camera",
    srcs = ["test_workshop_camera.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:workshop_camera", "//third_party:glm", "@googletest//:gtest_main"],
)

cc_test(
    name = "cove_player",
    srcs = ["test_cove_player.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:cove_player", "//src/game:cove_boat", "//src/game:cove_rigid_roots", "//src/game:cove_harbor_lift", "//src/game:cove_harbor_runtime", "//src/game:cove_workshop", "//src/game:cove_build", "//src/game:cove_restore", "//src/game:game_session", "//src/game:assembly_fracture", "//third_party:tinygltf", "@googletest//:gtest_main"],
    data = ["//shaders:shaders", "//data:salvage_asset_fixture", "//data:salvage_functional_kit", "//data:salvage_material_metric"],
)

cc_test(
    name = "functional_kit",
    srcs = ["test_functional_kit.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:fixture_registry", "//src/game:compiled_assembly", "//third_party:tinygltf", "@googletest//:gtest_main"],
    data = ["//data:salvage_functional_kit", "//data:salvage_asset_fixture"],
)

cc_test(
    name = "fixture_registry",
    srcs = ["test_fixture_registry.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:fixture_registry", "//third_party:tinygltf", "@googletest//:gtest_main"],
    data = ["//data:salvage_asset_fixture", "//data:salvage_functional_kit", "//data:salvage_material_metric"],
)

cc_test(
    name = "hierarchy_pipeline",
    srcs = ["test_hierarchy_pipeline.cpp"],
    copts = PROJECT_TEST_COPTS,
    data = ["//data:salvage_hierarchy_probe"],
    deps = ["//src/game:cooked_part_bundle", "//src/core:sha256", "//third_party:tinygltf", "@googletest//:gtest_main"],
)

cc_test(
    name = "pontoon_pipeline",
    srcs = ["test_pontoon_pipeline.cpp"],
    copts = PROJECT_TEST_COPTS,
    data = ["//data:salvage_asset_fixture"],
    deps = ["//src/game:fixture_registry", "//src/game:build_model", "//src/core:sha256", "//third_party:tinygltf", "@googletest//:gtest_main"],
)

cc_test(
    name = "cooked_part_directory",
    srcs = ["test_cooked_part_directory.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:cooked_part_directory", "@googletest//:gtest_main"],
    data = ["//data:salvage_runtime_probe"],
)

cc_test(
    name = "session_events",
    srcs = ["test_session_events.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:session_events", "@googletest//:gtest_main"],
)

cc_test(
    name = "session_transactions",
    srcs = ["test_session_transactions.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:session_transactions", "@googletest//:gtest_main"],
)

cc_test(
    name = "cooked_part_bundle",
    srcs = ["test_cooked_part_bundle.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:cooked_part_bundle", "//src/core:sha256", "//third_party:tinygltf", "@googletest//:gtest_main"],
    data = ["//tools:gameplay_sidecar_fixture", "//data:salvage_runtime_probe"],
)

cc_test(
    name = "rigid_prefab",
    srcs = ["test_rigid_prefab.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:rigid_prefab", "@googletest//:gtest_main"],
)

cc_test(
    name = "sha256",
    srcs = ["test_sha256.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/core:sha256", "@googletest//:gtest_main"],
)

cc_test(
    name = "game_session",
    srcs = ["test_game_session.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:game_session", "//src/game:session_save", "//src/core:sha256", "@googletest//:gtest_main"],
)

cc_test(
    name = "authored_body_frame",
    srcs = ["test_authored_body_frame.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/physics:physics", "//src/physics:authored_shape", "@googletest//:gtest_main"],
)

cc_test(
    name = "gpu_authored_shapes",
    srcs = ["test_gpu_authored_shapes.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = ["//src/physics:gpu_authored_shapes", "//src/physics:body_shape_layout", "//src/physics:physics", "//src/gpu:gpu", "@googletest//:gtest_main"],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "authored_shape",
    srcs = ["test_authored_shape.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/physics:authored_shape", "//src/game:compiled_assembly", "@googletest//:gtest_main"],
)

cc_test(
    name = "rigid_mass_frame",
    srcs = ["test_rigid_mass_frame.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/physics:rigid_mass_frame", "//src/game:compiled_assembly", "@googletest//:gtest_main"],
)

cc_test(
    name = "compiled_assembly",
    srcs = ["test_compiled_assembly.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:compiled_assembly", "@googletest//:gtest_main"],
)

cc_test(
    name = "assembly_fracture",
    srcs = ["test_assembly_fracture.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:assembly_fracture", "@googletest//:gtest_main"],
)

cc_test(
    name = "assembly_functions",
    srcs = ["test_assembly_functions.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:assembly_functions", "@googletest//:gtest_main"],
)

cc_test(
    name = "assembly_buoyancy",
    srcs = ["test_assembly_buoyancy.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:assembly_buoyancy", "@googletest//:gtest_main"],
)

cc_test(
    name = "assembly_collision",
    srcs = ["test_assembly_collision.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:assembly_collision", "@googletest//:gtest_main"],
)

cc_test(
    name = "assembly_compiler",
    srcs = ["test_assembly_compiler.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:assembly_compiler", "@googletest//:gtest_main"],
)

cc_test(
    name = "build_model",
    srcs = ["test_build_model.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = [
        "//src/game:build_model",
        "//src/game:construction_types",
        "//src/game:part_catalog",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "part_catalog",
    srcs = ["test_part_catalog.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = [
        "//src/game:part_catalog",
        "//src/game:construction_types",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "construction_types",
    srcs = ["test_construction_types.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = [
        "//src/game:construction_types",
        "@googletest//:gtest_main",
    ],
)

# Generate a separate test target for each test file
[cc_test(
    name = src.replace(".cpp", "").replace("test_", ""),
    srcs = [src],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src:voxy_core",
        "@googletest//:gtest_main",
    ],
    data = [
        "//shaders:shaders",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
) for src in TEST_SRCS]

cc_test(
    name = "ridgebreak_authority",
    srcs = ["test_ridgebreak_authority.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:protocol",
        "//src/network:ridgebreak_client_replication",
        "//src/network:ridgebreak_protocol",
        "//src/server:ridgebreak_authority",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "ridgebreak_server_runtime",
    srcs = ["test_ridgebreak_server_runtime.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:deterministic_adversity_transport",
        "//src/network:native_tcp_transport",
        "//src/network:protocol",
        "//src/network:ridgebreak_client_replication",
        "//src/network:ridgebreak_protocol",
        "//src/server:ridgebreak_server_runtime",
        "@googletest//:gtest_main",
    ],
)

test_suite(
    name = "determinism_ci",
    tests = [
        ":lockstep",
        ":replay",
    ],
)

test_suite(
    name = "networking_ci",
    tests = [
        ":deterministic_adversity_transport",
        ":multiplayer_session",
        ":native_tcp_transport",
        ":networking",
        ":wreckwater_character_controller",
        ":wreckwater_application_client",
        ":wreckwater_character_platform_timeline",
        ":wreckwater_character_presentation",
        ":wreckwater_graphical_client_loop",
        ":wreckwater_client_visual_clock",
        ":wreckwater_third_person_camera",
        ":wreckwater_client_probe",
        ":wreckwater_client_replication",
        ":wreckwater_client_runtime",
        ":wreckwater_protocol",
    ],
)

test_suite(
    name = "distributed_ci",
    tests = [":distributed_server"],
)

test_suite(
    name = "vertical_slices_ci",
    tests = [":vertical_slices"],
)

test_suite(
    name = "wreckwater_match_ci",
    tests = [
        ":wreckwater_authority_gpu",
        ":wreckwater_authority_runtime",
        ":wreckwater_character_authority_bridge",
        ":wreckwater_character_movement",
        ":wreckwater_character_step",
        ":wreckwater_content_manifest",
        ":wreckwater_live_world",
        ":wreckwater_match",
        ":wreckwater_replay",
        ":wreckwater_replay_playback",
        ":wreckwater_vessel_damage",
    ],
)

# Offline glTF -> .vmesh converter test. Links the library form of the tool
# (no main()) and drives it entirely in-memory.
cc_test(
    name = "gltf_vmesh_tool_test",
    srcs = ["test_gltf_vmesh_tool.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//tools:gltf_vmesh_tool_lib",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "moto_session",
    srcs = ["test_moto_session.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/moto:moto",
        "@googletest//:gtest_main",
    ],
    data = [
        "//data:data",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "race",
    srcs = ["test_race.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/moto:moto",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# RIDGEBREAK bike dynamics are isolated from the monolithic engine target so
# fixed-step gameplay tests stay fast and do not require a GPU toolchain.
cc_test(
    name = "moto_bike",
    srcs = ["test_bike.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/moto:moto",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "salvage_asset_fixture",
    srcs = ["test_salvage_asset_fixture.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/render:salvage_asset_fixture",
        "//src/core:sha256",
        "//src/gpu:gpu",
        "//third_party:tinygltf",
        "@googletest//:gtest_main",
    ],
    data = ["//data:salvage_runtime_probe", "//shaders:shaders"],
)

cc_test(
    name = "mesh_path_test",
    srcs = ["test_mesh_path.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/gpu:gpu",
        "//src/moto:moto",
        "//src/render:render",
        "@googletest//:gtest_main",
    ],
    data = [
        "//data:data",
        "//shaders:shaders",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "environment_lighting",
    srcs = ["test_environment_lighting.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/gpu:gpu",
        "//src/render:render",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

# Standalone fixed-capacity certified skiff prediction timeline test.
cc_test(
    name = "wreckwater_character_platform_timeline",
    srcs = ["test_wreckwater_character_platform_timeline.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_character_platform_timeline",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-capacity local character prediction/reconciliation test.
cc_test(
    name = "wreckwater_character_controller",
    srcs = ["test_wreckwater_character_controller.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_character_controller",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-capacity four-avatar presentation adapter test.
cc_test(
    name = "wreckwater_application_client",
    srcs = ["test_wreckwater_application_client.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_application_client",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone bounded client presentation-buffer test.
cc_test(
    name = "wreckwater_character_presentation",
    srcs = ["test_wreckwater_character_presentation.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_character_presentation",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-storage third-person camera and async-probe test.
cc_test(
    name = "wreckwater_third_person_camera",
    srcs = ["test_wreckwater_third_person_camera.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_third_person_camera",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone certified visual-clock test.
cc_test(
    name = "wreckwater_client_visual_clock",
    srcs = ["test_wreckwater_client_visual_clock.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_client_visual_clock",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-step graphical client orchestration test.
cc_test(
    name = "wreckwater_graphical_client_loop",
    srcs = ["test_wreckwater_graphical_client_loop.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_graphical_client_loop",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-capacity application-frame adversity scheduler test.
cc_test(
    name = "deterministic_adversity_transport",
    srcs = ["test_deterministic_adversity_transport.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:network",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone native TCP wire/handshake test. Keeping this independent from
# voxy_core avoids coupling transport admission checks to renderer or replay
# compilation.
cc_test(
    name = "native_tcp_transport",
    srcs = ["test_native_tcp_transport.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:native_tcp_transport",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone native client-probe parser and scripted-role test.
cc_test(
    name = "wreckwater_client_probe",
    srcs = ["test_wreckwater_client_probe.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/client:wreckwater_client_probe",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-capacity certified-pose character authority bridge test.
cc_test(
    name = "wreckwater_character_authority_bridge",
    srcs = ["test_wreckwater_character_authority_bridge.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_character_authority_bridge",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-capacity moving-skiff character movement test.
cc_test(
    name = "wreckwater_character_movement",
    srcs = ["test_wreckwater_character_movement.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_character_movement",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone authority/shared-step exact-parity test.
cc_test(
    name = "wreckwater_character_step",
    srcs = ["test_wreckwater_character_step.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_character_movement",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone deterministic content-compatibility manifest test.
cc_test(
    name = "wreckwater_content_manifest",
    srcs = ["test_wreckwater_content_manifest.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_content_manifest",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone authoritative match test. Keeping it independent from voxy_core
# gives command, replay, and state-machine sanitizer failures a clean signal
# even while renderer or transport work is in flight.
cc_test(
    name = "wreckwater_match",
    srcs = ["test_wreckwater_match.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_match",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone bounded certified-state replay archive test.
cc_test(
    name = "wreckwater_replay",
    srcs = ["test_wreckwater_replay.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_replay",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone fixed-capacity certified replay playback/WreckCam test.
cc_test(
    name = "wreckwater_replay_playback",
    srcs = ["test_wreckwater_replay_playback.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_replay_playback",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone live GPU-authority adapter transaction test.
cc_test(
    name = "wreckwater_live_world",
    srcs = ["test_wreckwater_live_world.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_live_world",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Fixed-capacity production vessel damage/flooding authority.
cc_test(
    name = "wreckwater_vessel_damage",
    srcs = ["test_wreckwater_vessel_damage.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:wreckwater_vessel_damage",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone serial-qualified headless authority orchestration test.
cc_test(
    name = "wreckwater_authority_runtime",
    srcs = ["test_wreckwater_authority_runtime.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/server:wreckwater_authority_runtime_headless",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Native WebGPU smoke test for the real headless authority submit path.
cc_test(
    name = "wreckwater_authority_gpu",
    srcs = ["test_wreckwater_authority_gpu.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/gpu:wreckwater_authority_gpu",
        "//src/server:wreckwater_authority_runtime_headless",
        "@googletest//:gtest_main",
    ],
    data = [
        "//shaders:shaders",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone hostile-input wire-codec test. It links only the two protocol
# targets used by authority and proves the payload fits PacketCodec's MTU.
cc_test(
    name = "wreckwater_protocol",
    srcs = ["test_wreckwater_protocol.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:protocol",
        "//src/network:wreckwater_protocol",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone bounded client presentation-buffer test.
cc_test(
    name = "wreckwater_client_replication",
    srcs = ["test_wreckwater_client_replication.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:wreckwater_client_replication",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Standalone serial-aware WRECKWATER client runtime test.
cc_test(
    name = "wreckwater_client_runtime",
    srcs = ["test_wreckwater_client_runtime.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/network:network",
        "@googletest//:gtest_main",
    ],
    data = [
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

# Combined test target that runs all tests
cc_test(
    name = "voxy_tests",
    # The 2,041-case CPU/GPU suite exceeded 900 seconds in default fastbuild
    # on the reference host (2026-09-10: 1,585 cases started, no assertions
    # failed before timeout). Keep all cases enabled with a bounded one-hour
    # timeout; focused targets remain available for development checks.
    size = "large",
    timeout = "eternal",
    srcs = TEST_SRCS + [
        "test_workshop_camera.cpp",
        "test_environment_lighting.cpp",
        "test_fixture_registry.cpp",
        "test_cove_player.cpp",
        "test_functional_kit.cpp",
        "test_pontoon_pipeline.cpp",
        "test_hierarchy_pipeline.cpp",
        "test_salvage_asset_fixture.cpp",
        "test_cooked_part_directory.cpp",
        "test_session_events.cpp",
        "test_session_transactions.cpp",
        "test_cooked_part_bundle.cpp",
        "test_rigid_prefab.cpp",
        "test_sha256.cpp",
        "test_game_session.cpp",
        "test_save_generation.cpp",
        "test_cove_save.cpp",
        "test_cove_water_clock.cpp",
        "//tools:salvage_assets/test_gameplay_sidecar.cpp",
        "test_build_model.cpp",
        "test_assembly_compiler.cpp",
        "test_assembly_collision.cpp",
        "test_assembly_buoyancy.cpp",
        "test_assembly_functions.cpp",
        "test_compiled_assembly.cpp",
        "test_assembly_fracture.cpp",
        "test_rigid_mass_frame.cpp",
        "test_authored_shape.cpp",
        "test_authored_body_frame.cpp",
        "test_gpu_authored_shapes.cpp",
        "test_construction_types.cpp",
        "test_part_catalog.cpp",
        "test_deterministic_adversity_transport.cpp",
        "test_moto_session.cpp",
        "test_native_tcp_transport.cpp",
        "test_race.cpp",
        "test_ridgebreak_authority.cpp",
        "test_ridgebreak_server_runtime.cpp",
        "test_water_clipmap_mesh.cpp",
        "test_wreckwater_authority_runtime.cpp",
        "test_wreckwater_character_authority_bridge.cpp",
        "test_wreckwater_application_client.cpp",
        "test_wreckwater_character_controller.cpp",
        "test_wreckwater_character_platform_timeline.cpp",
        "test_wreckwater_character_presentation.cpp",
        "test_wreckwater_graphical_client_loop.cpp",
        "test_wreckwater_client_visual_clock.cpp",
        "test_wreckwater_third_person_camera.cpp",
        "test_wreckwater_character_movement.cpp",
        "test_wreckwater_character_step.cpp",
        "test_wreckwater_content_manifest.cpp",
        "test_wreckwater_live_world.cpp",
        "test_wreckwater_match.cpp",
        "test_wreckwater_replay.cpp",
        "test_wreckwater_replay_playback.cpp",
        "test_wreckwater_client_replication.cpp",
        "test_wreckwater_client_runtime.cpp",
        "test_wreckwater_protocol.cpp",
    ] + select({"@platforms//os:linux": ["test_native_save_store.cpp", "test_native_save_worker.cpp"], "//conditions:default": []}),
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/game:cove_rigid_roots",
        "//src:voxy_core",
        "//tools:gameplay_sidecar_lib",
        "//src/game:session_save",
        "//src/game:cove_save",
        "//src/game:cove_water_clock",
        "//src/engine/platform:native_save_store",
        "//src/engine/platform:native_save_worker",
        "//src/game:save_generation",
        "//src/core:sha256",
        "//src/render:salvage_asset_fixture",
        "//src/game:fixture_registry",
        "//src/game:workshop_camera",
        "//src/game:cove_player",
        "//src/game:cove_boat", "//src/game:cove_harbor_lift", "//src/game:cove_harbor_runtime", "//src/game:cove_workshop", "//src/game:cove_build", "//src/game:cove_restore",
        "//src/game:assembly_compiler",
        "//src/game:assembly_collision",
        "//src/game:assembly_buoyancy",
        "//src/game:assembly_functions",
        "//src/game:compiled_assembly",
        "//src/game:assembly_fracture",
        "//src/physics:rigid_mass_frame",
        "//src/physics:authored_shape",
        "//src/physics:gpu_authored_shapes",
        "//third_party:tinygltf",
        "@googletest//:gtest_main",
    ],
    data = [
        "//data:data",
        "//shaders:shaders",
        "//tools:gameplay_sidecar_fixture",
        "//data:salvage_runtime_probe",
        "//data:salvage_asset_fixture",
        "//data:salvage_functional_kit",
        "//data:salvage_hierarchy_probe",
        "//data:salvage_material_metric",
        "//:asan_suppressions",
        "//:tsan_suppressions",
        "//:msan_suppressions",
    ],
)

cc_test(
    name = "performance_goldens",
    srcs = ["test_performance_goldens.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src:voxy_core",
        "@googletest//:gtest_main",
    ],
    data = ["//data:performance_golden"],
)

cc_test(
    name = "primitive_instance_benchmark",
    srcs = ["primitive_instance_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/render:primitive_instance_packing",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "primitive_upload_benchmark",
    srcs = ["primitive_upload_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/gpu:gpu",
        "//src/render:primitive_instance_packing",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "gpu_ballistic_benchmark",
    srcs = ["gpu_ballistic_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/gpu:gpu",
        "//src/physics:physics",
        "//src/render:render",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "gpu_static_benchmark",
    srcs = ["gpu_static_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/gpu:gpu",
        "//src/physics:physics",
        "//src/render:render",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "gpu_dynamic_benchmark",
    srcs = ["gpu_dynamic_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/gpu:gpu",
        "//src/physics:physics",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "native_server_batch_benchmark",
    srcs = ["native_server_batch_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/gpu:gpu",
        "//src/server:server",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "physics_snapshot_benchmark",
    srcs = ["physics_snapshot_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/physics:physics",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "water_lock_benchmark",
    srcs = ["water_lock_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/physics:physics",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "primitive_culling_benchmark",
    srcs = ["primitive_culling_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/render:primitive_culling",
        "//src/render:primitive_instance_packing",
        "@googletest//:gtest_main",
    ],
)

# Kept independent from //src:voxy_core so renderer validation does not pull in
# unrelated application/physics dependencies.
cc_test(
    name = "water_simulation",
    srcs = ["test_water_simulation.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/gpu:gpu",
        "//src/render:water_simulation",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "water_clipmap_mesh",
    srcs = ["test_water_clipmap_mesh.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    deps = [
        "//src/render:water_clipmap_mesh",
        "@googletest//:gtest_main",
    ],
)

# Standalone SVO core test - uses only header-only voxel_types (no GPU/GLFW deps)
cc_test(
    name = "svo_core_standalone",
    srcs = ["test_svo_core.cpp"],
    copts = PROJECT_TEST_COPTS + [
        "-DVOXY_NATIVE=1",
        "-DVOXY_SVO_STANDALONE=1",
    ],
    deps = [
        "//src/voxel:voxel_types",
        "@googletest//:gtest_main",
    ],
)

# Manual retired-GPU sort gate with a full stable-record oracle.
cc_test(
    name = "gpu_radix_benchmark",
    srcs = ["gpu_radix_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/gpu:gpu",
        "//src/physics:physics",
        "@googletest//:gtest_main",
    ],
    data = ["//shaders:shaders"],
)

cc_test(
    name = "replication_benchmark",
    srcs = ["replication_benchmark.cpp"],
    copts = PROJECT_TEST_COPTS + ["-DVOXY_NATIVE=1"],
    tags = ["manual"],
    deps = [
        "//src/network:network",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "save_generation",
    srcs = ["test_save_generation.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:save_generation", "//src/core:sha256", "@googletest//:gtest_main"],
)
cc_test(
    name = "cove_water_clock",
    srcs = ["test_cove_water_clock.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:cove_water_clock", "@googletest//:gtest_main"],
)
cc_test(
    name = "cove_save",
    srcs = ["test_cove_save.cpp"],
    copts = PROJECT_TEST_COPTS,
    deps = ["//src/game:cove_save", "//src/core:sha256", "@googletest//:gtest_main"],
)
cc_test(
    name = "native_save_store",
    srcs = ["test_native_save_store.cpp", "test_native_save_worker.cpp"],
    copts = PROJECT_TEST_COPTS,
    target_compatible_with = ["@platforms//os:linux"],
    deps = ["//src/engine/platform:native_save_store", "//src/engine/platform:native_save_worker", "//src/game:session_save", "@googletest//:gtest_main"],
)
