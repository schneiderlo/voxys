// ═══════════════════════════════════════════════════════════════════════════════
// entry.cpp (WASM) - Application Entry Point and Exports
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/application.hpp"
#include "camera/camera.hpp"
#include "core/log.hpp"
#include "core/config.hpp"
#include "engine/platform/input.hpp"
#include "gpu/context.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <emscripten.h>
#include <emscripten/html5.h>

extern "C" WGPUDevice emscripten_webgpu_get_device(void);

namespace {
    enum class PhysicsSelfTestStatus : int {
        NotStarted = 0,
        Running = 1,
        Passed = 2,
        ApplicationUnavailable = -1,
        WrongBackend = -2,
        SpawnFailed = -3,
        InvalidSnapshot = -4,
        IntegrationFailed = -5,
        SectorCollisionFailed = -6,
        DistantSectorAliased = -7,
    };

    struct PhysicsSelfTestState {
        PhysicsSelfTestStatus status = PhysicsSelfTestStatus::NotStarted;
        voxy::physics::BodyHandle left{};
        voxy::physics::BodyHandle right{};
        voxy::physics::BodyHandle distant{};
        uint64_t tick = 0;
    };

    // Hold the Application instance for the lifetime of the page
    std::unique_ptr<voxy::Application> g_wasmAppInstance;
    voxy::Application* g_app = nullptr;
    std::unique_ptr<voxy::physics::PhysicsWorld> g_physicsSelfTestWorld;
    std::unique_ptr<voxy::physics::PhysicsWorld> g_physicsBenchmarkWorld;
    WGPUDevice g_physicsSelfTestDevice = nullptr;
    WGPUQueue g_physicsSelfTestQueue = nullptr;
    PhysicsSelfTestState g_physicsSelfTest;
    bool g_physicsSelfTestRequested = false;
    bool g_physicsSelfTestRuntimeReady = false;
    bool g_physicsBenchmarkRuntimeReady = false;
    bool g_physicsBenchmarkAwaitingTiming = false;
    uint32_t g_physicsBenchmarkBodies = 0;
    uint64_t g_physicsBenchmarkSamples = 0;
    std::optional<voxy::physics::PhysicsGpuStageTiming>
        g_physicsBenchmarkTiming;
    std::optional<voxy::physics::PhysicsGpuStageTiming>
        g_polledPhysicsTiming;
    std::optional<voxy::RenderGpuStageTiming> g_polledRenderTiming;
    double g_lastFrameCpuMilliseconds = 0.0;
    uint32_t g_gpuFramesInFlight = 0;
    uint64_t g_gpuPacingSkips = 0;

    constexpr uint32_t kMaximumGpuFramesInFlight = 4;

    void appendJsonNumber(std::ostream& out, double value) {
        if (std::isfinite(value)) {
            out << value;
        } else {
            out << "null";
        }
    }

    void appendCapacityUsage(
        std::ostream& out,
        const voxy::physics::PhysicsCapacityUsage& usage) {
        out << "{\"current\":" << usage.current
            << ",\"capacity\":" << usage.capacity
            << ",\"high_water\":" << usage.highWater
            << ",\"overflow\":"
            << (usage.overflow ? "true" : "false") << '}';
    }

    std::string makeTelemetryJson() {
        if (!g_app && !g_physicsBenchmarkWorld) return {};
        voxy::ApplicationStats app = g_app
            ? g_app->getStats() : voxy::ApplicationStats{};
        const voxy::physics::PhysicsWorld* world = g_app
            ? g_app->getPhysicsWorld() : g_physicsBenchmarkWorld.get();
        if (!g_app && world) {
            app.frameCount = g_physicsBenchmarkSamples;
            app.physics = world->stats();
            app.physicsBackend = app.physics.backend;
            app.physicsResidentBodies = app.physics.residentBodies;
            app.physicsActiveBodies = app.physics.activeBodies;
            app.physicsBodyCapacity = app.physics.bodyCapacity;
            app.physicsEstimatedPersistentBytes =
                app.physics.estimatedPersistentBytes;
            app.physicsScratchBytes = app.physics.scratchBytes;
            app.physicsGpuTiming = g_physicsBenchmarkTiming;
            if (g_physicsBenchmarkTiming) {
                app.frameTimeMs =
                    g_physicsBenchmarkTiming->totalMilliseconds();
                app.avgFrameTimeMs = app.frameTimeMs;
                app.fps = app.frameTimeMs > 0.0
                    ? 1000.0 / app.frameTimeMs : 0.0;
            }
        }
        const voxy::physics::PhysicsStats& physics = app.physics;

        std::ostringstream out;
        out << std::setprecision(10);
        out << "{\"schema_version\":1";
        out << ",\"frame\":{\"count\":" << app.frameCount;
        out << ",\"fps\":";
        appendJsonNumber(out, app.fps);
        out << ",\"current_ms\":";
        appendJsonNumber(out, app.frameTimeMs);
        out << ",\"average_ms\":";
        appendJsonNumber(out, app.avgFrameTimeMs);
        out << ",\"cpu_ms\":";
        appendJsonNumber(out, g_lastFrameCpuMilliseconds);
        out << ",\"gpu_queue\":" << g_gpuFramesInFlight
            << ",\"pacing_skips\":" << g_gpuPacingSkips << '}';

        out << ",\"render\":{\"path\":\""
            << voxy::renderPathToString(app.activeRenderPath) << "\""
            << ",\"terrain_width\":" << app.terrainWidth
            << ",\"terrain_height\":" << app.terrainHeight
            << ",\"terrain_mips\":" << app.terrainMipLevels
            << ",\"submitted_primitives\":"
            << app.primitiveSubmittedCount << '}';

        out << ",\"physics\":{\"backend\":\""
            << voxy::physics::backendTypeName(physics.backend) << "\""
            << ",\"arithmetic\":\""
            << voxy::physics::physicsArithmeticModeName(
                   physics.arithmeticMode) << "\""
            << ",\"tick\":" << physics.telemetryTick
            << ",\"substeps\":" << physics.substeps
            << ",\"scheduled_substeps\":"
            << (world ? world->lastStepStats().substepCount : 0u);

        out << ",\"bodies\":";
        appendCapacityUsage(out, physics.residentBodyUsage);
        out << ",\"active_bodies\":";
        appendCapacityUsage(out, physics.activeBodyUsage);
        out << ",\"sleeping_bodies\":" << physics.sleepingBodies
            << ",\"kinematic_bodies\":" << physics.kinematicBodies;
        out << ",\"commands\":";
        appendCapacityUsage(out, physics.commandUsage);

        out << ",\"grid_entries\":";
        appendCapacityUsage(out, physics.gridEntryUsage);
        out << ",\"occupied_cells\":" << physics.occupiedCells;
        out << ",\"candidate_pairs\":";
        appendCapacityUsage(out, physics.candidatePairUsage);
        out << ",\"pairs\":";
        appendCapacityUsage(out, physics.uniquePairUsage);
        out << ",\"sleeping_pairs\":" << physics.activeSleepingPairs
            << ",\"oversized_bodies\":" << physics.oversizedBodies;

        out << ",\"contacts\":";
        appendCapacityUsage(out, physics.contactUsage);
        out << ",\"manifolds\":";
        appendCapacityUsage(out, physics.manifoldUsage);
        out << ",\"manifold_points\":" << physics.manifoldPoints
            << ",\"speculative_manifolds\":"
            << physics.speculativeManifolds
            << ",\"invalid_manifolds\":" << physics.invalidManifolds;
        out << ",\"terrain_contacts\":";
        appendCapacityUsage(out, physics.terrainContactUsage);
        out << ",\"terrain_contact_bodies\":"
            << physics.terrainContactBodies
            << ",\"maximum_terrain_contacts_per_body\":"
            << physics.maximumTerrainContactsPerBody;

        out << ",\"solver_mode\":\""
            << (physics.serialWorldSolver ? "serial" : "global") << "\""
            << ",\"compact_contacts\":"
            << physics.compactIslandContacts
            << ",\"compact_bodies\":" << physics.compactIslandBodies
            << ",\"graph_colors\":" << physics.activeGraphColors;
        out << ",\"solver_overflow\":";
        appendCapacityUsage(out, physics.overflowConstraintUsage);
        out << ",\"maximum_body_degree\":" << physics.maximumBodyDegree
            << ",\"color_conflicts\":" << physics.colorConflictErrors;

        out << ",\"islands\":{\"total\":" << physics.islandCount
            << ",\"awake\":" << physics.awakeIslands
            << ",\"sleeping\":" << physics.sleepingIslands
            << ",\"maximum_bodies\":" << physics.maximumIslandBodies
            << ",\"root_errors\":" << physics.islandRootErrors << '}';
        out << ",\"sleeping_grid\":";
        appendCapacityUsage(out, physics.sleepingGridUsage);
        out << ",\"sleeping_grid_cells\":" << physics.sleepingGridCells
            << ",\"sleep_transitions\":" << physics.sleepTransitions
            << ",\"wake_transitions\":" << physics.wakeTransitions;

        out << ",\"bullets\":";
        appendCapacityUsage(out, physics.bulletUsage);
        out << ",\"ccd_hits\":" << physics.ccdHits
            << ",\"ccd_stalls\":" << physics.ccdStalls
            << ",\"ccd_failures\":" << physics.ccdFailures
            << ",\"ccd_maximum_iterations\":"
            << physics.ccdMaximumIterations
            << ",\"submerged_bodies\":" << physics.submergedBodies;
        out << ",\"water_samples\":";
        appendCapacityUsage(out, physics.waterSampleUsage);
        out << ",\"events\":";
        appendCapacityUsage(out, physics.eventUsage);
        out << ",\"contact_begin_events\":" << physics.contactBeginEvents
            << ",\"contact_end_events\":" << physics.contactEndEvents
            << ",\"island_events\":" << physics.islandEvents;

        out << ",\"memory\":{\"persistent_bytes\":"
            << physics.estimatedPersistentBytes
            << ",\"scratch_bytes\":" << physics.scratchBytes << '}';
        out << ",\"io\":{\"upload_bytes\":" << physics.gpuUploadBytes
            << ",\"readback_bytes\":" << physics.gpuReadbackBytes << '}';
        out << ",\"device_limits\":{\"storage_bindings\":"
            << physics.deviceMaxStorageBuffersPerShaderStage
            << ",\"compute_invocations\":"
            << physics.deviceMaxComputeInvocationsPerWorkgroup
            << ",\"storage_buffer_bytes\":"
            << physics.deviceMaxStorageBufferBindingSize
            << ",\"maximum_buffer_bytes\":"
            << physics.deviceMaxBufferSize << '}';

        out << ",\"stages\":{\"available\":"
            << (app.physicsGpuTiming ? "true" : "false");
        if (app.physicsGpuTiming) {
            const auto& timing = *app.physicsGpuTiming;
            out << ",\"tick\":" << timing.tick << ",\"total_ms\":";
            appendJsonNumber(out, timing.totalMilliseconds());
            for (size_t index = 0;
                 index < voxy::physics::kPhysicsGpuStageCount; ++index) {
                out << ",\"" << voxy::physics::physicsGpuStageName(
                    static_cast<voxy::physics::PhysicsGpuStage>(index))
                    << "\":";
                appendJsonNumber(out, timing.milliseconds[index]);
            }
        } else {
            out << ",\"tick\":0,\"total_ms\":0";
        }
        out << "}}";
        if (g_app && g_app->getCamera()) {
            const voxy::Camera& camera = *g_app->getCamera();
            const glm::vec3& position = camera.position();
            const glm::ivec3& sector = camera.worldSector();
            const glm::vec3& forward = camera.forward();
            out << ",\"camera\":{\"position\":[";
            appendJsonNumber(out, position.x);
            out << ',';
            appendJsonNumber(out, position.y);
            out << ',';
            appendJsonNumber(out, position.z);
            out << "],\"sector\":[" << sector.x << ',' << sector.y << ','
                << sector.z << "],\"forward\":[";
            appendJsonNumber(out, forward.x);
            out << ',';
            appendJsonNumber(out, forward.y);
            out << ',';
            appendJsonNumber(out, forward.z);
            out << "],\"yaw\":";
            appendJsonNumber(out, camera.yaw());
            out << ",\"pitch\":";
            appendJsonNumber(out, camera.pitch());
            out << ",\"fov_y\":";
            appendJsonNumber(out, camera.fovY());
            out << ",\"aspect_ratio\":";
            appendJsonNumber(out, camera.aspectRatio());
            out << '}';
        } else {
            out << ",\"camera\":null";
        }
        out << '}';
        return out.str();
    }

    void gpuFrameCompleted(WGPUQueueWorkDoneStatus /*status*/,
                           WGPUStringView /*message*/, void* /*userdata1*/,
                           void* /*userdata2*/) {
        if (g_gpuFramesInFlight != 0u) --g_gpuFramesInFlight;
    }

    constexpr int32_t kPhysicsSelfTestBaseSector = 1'500'000;
    constexpr float kPhysicsSelfTestHeight = 100.0f;

    int physicsSelfTestStatus() noexcept {
        return static_cast<int>(g_physicsSelfTest.status);
    }

    voxy::physics::PhysicsWorld* physicsWorldForSelfTest() noexcept {
        if (g_physicsBenchmarkWorld) return g_physicsBenchmarkWorld.get();
        if (g_physicsSelfTestWorld) return g_physicsSelfTestWorld.get();
        return g_app ? g_app->getPhysicsWorld() : nullptr;
    }

    WGPUDevice deviceForSelfTest() noexcept {
        if (g_physicsSelfTestDevice) return g_physicsSelfTestDevice;
        return g_app && g_app->getGPUContext()
            ? g_app->getGPUContext()->getDevice() : nullptr;
    }

    WGPUQueue queueForSelfTest() noexcept {
        if (g_physicsSelfTestQueue) return g_physicsSelfTestQueue;
        return g_app && g_app->getGPUContext()
            ? g_app->getGPUContext()->getQueue() : nullptr;
    }

    void failPhysicsSelfTest(PhysicsSelfTestStatus status,
                             const char* message) {
        g_physicsSelfTest.status = status;
        LOG_ERROR("WASM GPU physics self-test failed ({}): {}",
                  physicsSelfTestStatus(), message);
    }

    int startPhysicsSelfTest() {
        if (g_physicsSelfTest.status != PhysicsSelfTestStatus::NotStarted) {
            return physicsSelfTestStatus();
        }
        voxy::physics::PhysicsWorld* selfTestWorld =
            physicsWorldForSelfTest();
        WGPUDevice selfTestDevice = deviceForSelfTest();
        WGPUQueue selfTestQueue = queueForSelfTest();
        if (!selfTestWorld || !selfTestWorld->isInitialized()
            || !selfTestDevice || !selfTestQueue) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::ApplicationUnavailable,
                "browser device or physics world is unavailable");
            return physicsSelfTestStatus();
        }

        voxy::physics::PhysicsWorld& world = *selfTestWorld;
        if (world.backendType() != voxy::physics::BackendType::WebGpuSoft) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::WrongBackend,
                "WebGpuSoft is not the active backend");
            return physicsSelfTestStatus();
        }

        voxy::physics::BodySpawnDesc leftDesc;
        leftDesc.shape = voxy::physics::ThrowableShape::Sphere;
        leftDesc.dimensions =
            voxy::physics::throwableShapeDimensions(leftDesc.shape);
        leftDesc.position = {127.6f, kPhysicsSelfTestHeight, 0.0f};
        leftDesc.sector = {kPhysicsSelfTestBaseSector, 0, 0};

        voxy::physics::BodySpawnDesc rightDesc = leftDesc;
        rightDesc.position.x = -127.6f;
        ++rightDesc.sector.x;

        voxy::physics::BodySpawnDesc distantDesc = rightDesc;
        distantDesc.sector.x += 3;

        g_physicsSelfTest.left = world.spawnBody(leftDesc);
        g_physicsSelfTest.right = world.spawnBody(rightDesc);
        g_physicsSelfTest.distant = world.spawnBody(distantDesc);
        if (!g_physicsSelfTest.left.valid()
            || !g_physicsSelfTest.right.valid()
            || !g_physicsSelfTest.distant.valid()
            || g_physicsSelfTest.right.index
                != g_physicsSelfTest.left.index + 1u
            || g_physicsSelfTest.distant.index
                != g_physicsSelfTest.right.index + 1u) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::SpawnFailed,
                "could not allocate three contiguous probe bodies");
            return physicsSelfTestStatus();
        }

        // Use the production backend and browser device, but submit a dedicated
        // probe command buffer so the result does not depend on RAF scheduling
        // or the cost of the terrain render running on SwiftShader.
        world.update(1.0f / 60.0f);
        world.requestDebugSnapshot({g_physicsSelfTest.left.index, 3u});
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            selfTestDevice, &encoderDesc);
        if (!encoder) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::ApplicationUnavailable,
                "could not create the browser probe command encoder");
            return physicsSelfTestStatus();
        }
        world.encodeGpuStep(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        if (!command) {
            wgpuCommandEncoderRelease(encoder);
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::ApplicationUnavailable,
                "could not finish the browser probe command buffer");
            return physicsSelfTestStatus();
        }
        wgpuQueueSubmit(selfTestQueue, 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        g_physicsSelfTest.status = PhysicsSelfTestStatus::Running;
        LOG_INFO("WASM GPU physics self-test started");
        return physicsSelfTestStatus();
    }

    void finishPhysicsSelfTestFrame() {
        voxy::physics::PhysicsWorld* selfTestWorld =
            physicsWorldForSelfTest();
        if (g_physicsSelfTest.status != PhysicsSelfTestStatus::Running
            || !selfTestWorld) return;

        voxy::physics::PhysicsWorld& world = *selfTestWorld;
        const auto snapshot = world.pollDebugSnapshot();
        if (!snapshot) return;
        g_physicsSelfTest.tick = snapshot->tick;
        if (snapshot->tick == 0u || snapshot->bodies.size() != 3u) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::InvalidSnapshot,
                "debug readback did not contain the simulated probe range");
            return;
        }

        const auto& left = snapshot->bodies[0];
        const auto& right = snapshot->bodies[1];
        const auto& distant = snapshot->bodies[2];
        if (left.handle != g_physicsSelfTest.left
            || right.handle != g_physicsSelfTest.right
            || distant.handle != g_physicsSelfTest.distant
            || !left.alive || !right.alive || !distant.alive
            || !(left.position.y < kPhysicsSelfTestHeight)
            || !(right.position.y < kPhysicsSelfTestHeight)
            || !(distant.position.y < kPhysicsSelfTestHeight)) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::IntegrationFailed,
                "spawned bodies were not integrated by the GPU");
            return;
        }

        const voxy::physics::WorldPosition leftPosition{
            left.sector, left.position};
        const voxy::physics::WorldPosition rightPosition{
            right.sector, right.position};
        const voxy::physics::WorldPosition distantPosition{
            distant.sector, distant.position};
        glm::vec3 leftInFrame;
        glm::vec3 rightInFrame;
        if (!voxy::physics::isValidWorldPosition(leftPosition)
            || !voxy::physics::isValidWorldPosition(rightPosition)
            || !voxy::physics::isValidWorldPosition(distantPosition)
            || !voxy::physics::worldPositionRelativeToSector(
                leftPosition, left.sector, leftInFrame)
            || !voxy::physics::worldPositionRelativeToSector(
                rightPosition, left.sector, rightInFrame)) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::SectorCollisionFailed,
                "probe positions were not canonical sector-local values");
            return;
        }

        const float separation = rightInFrame.x - leftInFrame.x;
        if (!(separation > 0.8001f && separation < 1.3f)) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::SectorCollisionFailed,
                "overlapping bodies did not resolve across the sector boundary");
            return;
        }
        if (distant.sector.x != kPhysicsSelfTestBaseSector + 4
            || std::abs(distant.position.x + 127.6f) > 0.05f) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::DistantSectorAliased,
                "distant wrapped grid key produced a false collision");
            return;
        }

        static_cast<void>(world.destroyBody(g_physicsSelfTest.left));
        static_cast<void>(world.destroyBody(g_physicsSelfTest.right));
        static_cast<void>(world.destroyBody(g_physicsSelfTest.distant));
        g_physicsSelfTest.status = PhysicsSelfTestStatus::Passed;
        LOG_INFO("WASM GPU physics self-test passed at tick {}",
                 g_physicsSelfTest.tick);
    }

    bool submitPhysicsBenchmarkStep() {
        if (!g_physicsBenchmarkWorld || !g_physicsSelfTestDevice
            || !g_physicsSelfTestQueue) {
            return false;
        }

        const double startMilliseconds = emscripten_get_now();
        g_physicsBenchmarkWorld->update(1.0f / 60.0f);
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            g_physicsSelfTestDevice, &encoderDesc);
        if (!encoder) return false;
        g_physicsBenchmarkWorld->encodeGpuStep(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        if (!command) {
            wgpuCommandEncoderRelease(encoder);
            return false;
        }
        wgpuQueueSubmit(g_physicsSelfTestQueue, 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        g_lastFrameCpuMilliseconds =
            emscripten_get_now() - startMilliseconds;
        g_physicsBenchmarkAwaitingTiming = true;
        return true;
    }

    void physicsBenchmarkFrame() {
        if (!g_physicsBenchmarkWorld) return;
        bool retiredTiming = false;
        while (auto timing =
                   g_physicsBenchmarkWorld->pollGpuStageTimings()) {
            g_physicsBenchmarkTiming = std::move(*timing);
            retiredTiming = true;
        }
        if (g_physicsBenchmarkAwaitingTiming && !retiredTiming) return;
        if (retiredTiming) {
            g_physicsBenchmarkAwaitingTiming = false;
            ++g_physicsBenchmarkSamples;
        }
        if (!submitPhysicsBenchmarkStep()) {
            LOG_ERROR("Could not submit the browser physics benchmark step");
            emscripten_cancel_main_loop();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Entry Point
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Initialize logging
    voxy::log::init();

    // Parse command-line arguments and load config
    voxy::config::init(argc, argv);
    const auto& config = voxy::config::get();
    g_physicsSelfTestRequested = EM_ASM_INT({
        return new URLSearchParams(globalThis.location.search)
            .get("physicsSelfTest") === "1" ? 1 : 0;
    }) != 0;
    g_physicsBenchmarkBodies = static_cast<uint32_t>(EM_ASM_INT({
        const value = Number.parseInt(
            new URLSearchParams(globalThis.location.search)
                .get("physicsBenchmarkBodies") ?? "0",
            10);
        return Number.isInteger(value) && value > 0 ? value : 0;
    }));
    if (g_physicsBenchmarkBodies != 0u) {
        g_physicsBenchmarkRuntimeReady = true;
        g_physicsSelfTestDevice = emscripten_webgpu_get_device();
        if (!g_physicsSelfTestDevice) {
            LOG_ERROR("Browser physics benchmark has no WebGPU device");
            return 0;
        }
        g_physicsSelfTestQueue =
            wgpuDeviceGetQueue(g_physicsSelfTestDevice);
        if (!g_physicsSelfTestQueue) {
            LOG_ERROR("Browser physics benchmark has no WebGPU queue");
            return 0;
        }

        voxy::physics::PhysicsInitContext physicsContext;
        physicsContext.requestedBackend =
            voxy::physics::BackendType::WebGpuSoft;
        physicsContext.device = g_physicsSelfTestDevice;
        physicsContext.queue = g_physicsSelfTestQueue;
        physicsContext.maxBodies = static_cast<uint32_t>(std::max(
            config.physics.gpuMaxBodies,
            static_cast<int>(g_physicsBenchmarkBodies)));
        physicsContext.maxActiveBodies = physicsContext.maxBodies;
        physicsContext.gpu.enableStageProfiling = true;
        physicsContext.gpu.stageProfilingIntervalTicks = 1;
        physicsContext.gpu.enableTelemetryReadback = true;
        physicsContext.gpu.telemetryReadbackIntervalTicks = 1;
        physicsContext.gpu.shaderPath =
            "shaders/physics_ballistic.wgsl";

        g_physicsBenchmarkWorld =
            std::make_unique<voxy::physics::PhysicsWorld>();
        if (!g_physicsBenchmarkWorld->initialize(physicsContext)) {
            LOG_ERROR("Browser WebGPU physics benchmark did not initialize");
            return 0;
        }

        const voxy::physics::ThrowableShape shape =
            voxy::physics::ThrowableShape::Sphere;
        const glm::vec3 dimensions =
            voxy::physics::throwableShapeDimensions(shape);
        const float maximumDimension = std::max(
            dimensions.x, std::max(dimensions.y, dimensions.z));
        const float spacing = maximumDimension * 1.08f + 0.02f;
        constexpr uint32_t columns = 16u;
        constexpr uint32_t rows = 8u;
        constexpr uint32_t batchSize = columns * rows;
        for (uint32_t index = 0; index < g_physicsBenchmarkBodies; ++index) {
            const uint32_t batch = index / batchSize;
            const uint32_t batchIndex = index % batchSize;
            const uint32_t column = batchIndex % columns;
            const uint32_t row = batchIndex / columns;
            const uint32_t lane = batch % 4u;
            voxy::physics::BodySpawnDesc body;
            body.shape = shape;
            body.dimensions = dimensions;
            body.position = {
                (static_cast<float>(column) - 7.5f) * spacing,
                100.0f + (static_cast<float>(row) - 3.5f) * spacing,
                static_cast<float>(lane) * spacing * 1.5f,
            };
            body.linearVelocity = {0.0f, 0.0f, 28.0f};
            if (!g_physicsBenchmarkWorld->spawnBody(body).valid()) {
                LOG_ERROR("Browser physics benchmark stopped spawning at {}",
                          index);
                return 0;
            }
        }
        LOG_INFO("Spawned {} renderless browser benchmark bodies",
                 g_physicsBenchmarkBodies);
        emscripten_set_main_loop([]() {
            physicsBenchmarkFrame();
        }, 0, false);
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 0);
        return 0;
    }
    if (g_physicsSelfTestRequested) {
        g_physicsSelfTestRuntimeReady = true;
        g_physicsSelfTest = {};
        g_physicsSelfTestDevice = emscripten_webgpu_get_device();
        if (!g_physicsSelfTestDevice) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::ApplicationUnavailable,
                "Emscripten did not provide the preinitialized WebGPU device");
            return 0;
        }
        g_physicsSelfTestQueue =
            wgpuDeviceGetQueue(g_physicsSelfTestDevice);
        if (!g_physicsSelfTestQueue) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::ApplicationUnavailable,
                "the browser WebGPU device did not provide a queue");
            return 0;
        }

        voxy::physics::PhysicsInitContext physicsContext;
        physicsContext.requestedBackend =
            voxy::physics::BackendType::WebGpuSoft;
        physicsContext.device = g_physicsSelfTestDevice;
        physicsContext.queue = g_physicsSelfTestQueue;
        physicsContext.maxBodies = 64;
        physicsContext.maxActiveBodies = 64;
        physicsContext.maxPairs = 256;
        physicsContext.maxContacts = 128;
        physicsContext.maxManifolds = 256;
        physicsContext.gpu.commandCapacity = 64;
        physicsContext.gpu.debugReadbackSlots = 2;
        physicsContext.gpu.debugReadbackBodyCapacity = 8;
        physicsContext.gpu.asyncQueryCapacity = 8;
        physicsContext.gpu.asyncQueryReadbackSlots = 2;
        physicsContext.gpu.telemetryReadbackSlots = 1;
        physicsContext.gpu.ccdBulletCapacity = 64;
        physicsContext.gpu.shaderPath =
            "shaders/physics_ballistic.wgsl";

        g_physicsSelfTestWorld =
            std::make_unique<voxy::physics::PhysicsWorld>();
        if (!g_physicsSelfTestWorld->initialize(physicsContext)) {
            failPhysicsSelfTest(
                PhysicsSelfTestStatus::ApplicationUnavailable,
                "the browser WebGPU physics backend did not initialize");
            return 0;
        }
        static_cast<void>(startPhysicsSelfTest());
        emscripten_set_main_loop([]() {
            finishPhysicsSelfTestFrame();
        }, 0, false);
        return 0;
    }

    // Configure the application from loaded config file
    voxy::ApplicationConfig appConfig;
    
    // Window settings
    appConfig.windowWidth = config.window.width;
    appConfig.windowHeight = config.window.height;
    appConfig.windowTitle = config.window.title.empty() 
                          ? "voxy - WebGPU Terrain Renderer" 
                          : config.window.title;
    appConfig.fullscreen = config.window.fullscreen;
    appConfig.vsync = config.render.vsync;

    // Render path selection
    if (config.render.path == "triangle") {
        appConfig.renderPath = voxy::RenderPath::Triangle;
    } else {
        appConfig.renderPath = voxy::RenderPath::Raycast;
    }
    appConfig.resolutionScale = config.render.resolutionScale;

    // Terrain settings
    appConfig.heightmapPath = config.terrain.heightmap;
    appConfig.albedoPath = config.terrain.albedo;
    appConfig.lightmapPath = config.terrain.lightmap;
    appConfig.heightScale = config.terrain.heightScale;
    appConfig.cellScale = config.terrain.cellScale;
    appConfig.ambientIntensity = config.lighting.ambientIntensity;
    appConfig.waterEnabled = config.water.enabled;
    appConfig.waterHeight = config.water.height;
    appConfig.waterShallowColor = {
        config.water.shallowColor[0],
        config.water.shallowColor[1],
        config.water.shallowColor[2]
    };
    appConfig.waterDeepColor = {
        config.water.deepColor[0],
        config.water.deepColor[1],
        config.water.deepColor[2]
    };
    appConfig.waterRoughness = config.water.roughness;
    appConfig.waterWaveStrength = config.water.waveStrength;
    appConfig.waterReflectionStrength = config.water.reflectionStrength;
    appConfig.waterShoreFade = config.water.shoreFade;
    appConfig.physicsBackend = voxy::physics::backendTypeFromName(
        config.physics.backend);
    appConfig.gpuPhysicsMaxBodies = static_cast<uint32_t>(
        std::max(config.physics.gpuMaxBodies, 2));
    // A long GPU tick must not trigger a self-sustaining catch-up spiral in
    // the single browser queue. Interactive WASM advances at most one fixed
    // tick per rendered frame and drops excess wall-clock backlog.
    appConfig.gpuPhysicsMaximumCatchUpTicks = 1;
    appConfig.physicsCpuFallback = config.physics.allowCpuFallback;
    // This WASM build has no pthreads. Native Jolt defaults to its thread pool,
    // while browser CPU backends use their single-threaded schedulers.
    appConfig.joltJobSystem =
        voxy::physics::JoltJobSystemMode::SingleThreaded;
    appConfig.joltWorkerThreads = 0;
    appConfig.box3dWorkerThreads = 1;
    
    // Enforce 8K resolution
    appConfig.heightmapWidth = 8192;
    appConfig.heightmapHeight = 8192;

    if (appConfig.heightmapPath.empty() || 
        appConfig.heightmapPath == "assets/heightmaps/terrain.ldh") {
        appConfig.heightmapPath.clear();
        appConfig.heightmapWidth = 256;
        appConfig.heightmapHeight = 256;
    }

    // Camera settings
    appConfig.cameraFovDegrees = config.camera.fov;
    appConfig.cameraNear = config.camera.nearPlane;
    appConfig.cameraFar = config.camera.farPlane;
    appConfig.cameraMoveSpeed = config.camera.moveSpeed;
    appConfig.cameraMouseSensitivity = config.camera.mouseSensitivity;
    appConfig.cameraEyeHeight = config.camera.eyeHeight;

    // Debug settings
    appConfig.enableValidation = false; // Browser handles validation
    appConfig.showFPS = config.debug.showStats;
    appConfig.fpsLogIntervalSeconds = 2.0f;
    appConfig.gpuPhysicsStageProfiling = EM_ASM_INT({
        return new URLSearchParams(globalThis.location.search)
            .get("physicsProfile") === "0" ? 0 : 1;
    }) != 0;
    appConfig.gpuRenderStageProfiling = EM_ASM_INT({
        return new URLSearchParams(globalThis.location.search)
            .get("renderProfile") === "1" ? 1 : 0;
    }) != 0;

    // Automation settings
    appConfig.initialTeleportIndex = config.automation.teleportIndex;
    appConfig.screenshotPath = config.automation.screenshotPath;
    appConfig.screenshotFrameDelay = config.automation.screenshotFrames;
    if (config.automation.screenshotTourCount > 0) {
        int count = config.automation.screenshotTourCount;
        appConfig.screenshotTourIndices.resize(static_cast<size_t>(count));
        std::iota(appConfig.screenshotTourIndices.begin(), appConfig.screenshotTourIndices.end(), 0);
        if (config.automation.screenshotDir) {
            appConfig.screenshotTourDir = *config.automation.screenshotDir;
        } else {
            appConfig.screenshotTourDir = "screenshots";
        }
    }

    // Create and initialize application
    if (!g_wasmAppInstance) {
        g_wasmAppInstance = std::make_unique<voxy::Application>();
    }
    voxy::Application* app = g_wasmAppInstance.get();
    
    if (!app->init(appConfig)) {
        LOG_ERROR("Failed to initialize application");
        voxy::log::shutdown();
        g_app = nullptr;
        g_wasmAppInstance.reset();
        return 1;
    }

    g_app = app;
    g_physicsSelfTest = {};

    // Run the main loop (returns immediately in WASM)
    // WASM: set up Emscripten main loop and return
    LOG_INFO("Starting Emscripten main loop...");

    static double lastSubmittedTime = emscripten_get_now() / 1000.0;
    static double lastSimulationTime = lastSubmittedTime;
    static bool currentUncapped = false;

    auto mainLoop = []() {
        if (!g_app) {
            emscripten_cancel_main_loop();
            return;
        }

        if (g_app->shouldExit()) {
            g_app->shutdown();
            emscripten_cancel_main_loop();
            return;
        }

        // Browser WebGPU submissions are otherwise allowed to grow without
        // bound. Once the GPU falls a little behind, surface acquisition can
        // block for hundreds of milliseconds and the fixed-step scheduler
        // responds by encoding six catch-up ticks, creating a feedback loop.
        if (g_gpuFramesInFlight >= kMaximumGpuFramesInFlight) {
            // Do not turn time spent waiting for the GPU into another burst of
            // GPU work. The next submitted frame resumes from this RAF edge.
            lastSimulationTime = emscripten_get_now() / 1000.0;
            ++g_gpuPacingSkips;
            return;
        }

        // Check for loop timing changes
        if (g_app->isUncappedFPS() != currentUncapped) {
            currentUncapped = g_app->isUncappedFPS();
            if (currentUncapped) {
                // Switch to Immediate/SetTimeout loop (uncapped)
                // EM_TIMING_SETIMMEDIATE attempts to run as fast as possible but starves the browser event loop
                // EM_TIMING_SETTIMEOUT (0ms) is more cooperative, allowing compositing and input processing
                emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 0);
                LOG_INFO("Switched to Uncapped Loop (SETTIMEOUT)");
            } else {
                // Switch back to RAF loop (capped)
                emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
                LOG_INFO("Switched to Capped Loop (RAF)");
            }
        }

        const double now = emscripten_get_now() / 1000.0;
        const float frameDeltaTime =
            static_cast<float>(now - lastSubmittedTime);
        float simulationDeltaTime =
            static_cast<float>(now - lastSimulationTime);
        lastSubmittedTime = now;
        lastSimulationTime = now;

        // Keep simulation stable after a suspended tab. The frame interval is
        // intentionally not clamped: the FPS counter must include pacing skips.
        simulationDeltaTime = std::min(simulationDeltaTime, 0.1f);

        const double frameStartMilliseconds = emscripten_get_now();
        g_app->processFrame(simulationDeltaTime, frameDeltaTime);
        g_lastFrameCpuMilliseconds =
            emscripten_get_now() - frameStartMilliseconds;

        WGPUQueueWorkDoneCallbackInfo callbackInfo =
            WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        callbackInfo.callback = gpuFrameCompleted;
        ++g_gpuFramesInFlight;
        static_cast<void>(wgpuQueueOnSubmittedWorkDone(
            g_app->getGPUContext()->getQueue(), callbackInfo));
    };

    // 0 = use requestAnimationFrame, false = don't simulate infinite loop
    emscripten_set_main_loop(mainLoop, 0, false);

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Exported C Functions for JavaScript
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

EMSCRIPTEN_KEEPALIVE
void voxy_resize(int width, int height) {
    LOG_DEBUG("Canvas resized: {}x{}", width, height);
    if (g_app && width > 0 && height > 0) {
        g_app->onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
}

EMSCRIPTEN_KEEPALIVE
void voxy_mouse_move(float dx, float dy) {
    if (g_app && g_app->getInput()) {
        voxy::Input* input = g_app->getInput();
        if (!input->isMouseCaptured()) {
            input->captureMouse();
        }
        glm::vec2 pos = input->mousePosition();
        input->onMouseMove(pos.x + dx, pos.y + dy);
    }
}

// Helper to convert JS keyCode to voxy::Key
int keyCodeToVoxyKey(int keyCode) {
    if ((keyCode >= 65 && keyCode <= 90) || (keyCode >= 48 && keyCode <= 57)) {
        return keyCode;
    }
    if (keyCode >= 112 && keyCode <= 123) {
        return 290 + (keyCode - 112);
    }
    switch (keyCode) {
        case 32: return 32;  // Space
        case 27: return 256; // Escape
        case 13: return 257; // Enter
        case 9:  return 258; // Tab
        case 8:  return 259; // Backspace
        case 45: return 260; // Insert
        case 46: return 261; // Delete
        case 39: return 262; // Right
        case 37: return 263; // Left
        case 40: return 264; // Down
        case 38: return 265; // Up
        case 16: return 340; // Shift (Left)
        case 17: return 341; // Control (Left)
        case 18: return 342; // Alt (Left)
    }
    return keyCode;
}

EMSCRIPTEN_KEEPALIVE
void voxy_key_event(int key, int down) {
    if (g_app && g_app->getInput()) {
        int voxyKey = keyCodeToVoxyKey(key);
        if (down) {
            g_app->getInput()->onKeyDown(voxyKey);
        } else {
            g_app->getInput()->onKeyUp(voxyKey);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
float voxy_get_fps() {
    if (g_app) {
        return static_cast<float>(g_app->getStats().fps);
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_last_frame_cpu_ms() {
    return g_lastFrameCpuMilliseconds;
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_gpu_frames_in_flight() {
    return static_cast<int>(g_gpuFramesInFlight);
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_gpu_pacing_skips() {
    return static_cast<double>(g_gpuPacingSkips);
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_physics_substeps() {
    const voxy::physics::PhysicsWorld* world =
        g_app ? g_app->getPhysicsWorld() : nullptr;
    return world
        ? static_cast<int>(world->lastStepStats().substepCount) : 0;
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_physics_resident_bodies() {
    if (g_physicsBenchmarkWorld) {
        return static_cast<int>(
            g_physicsBenchmarkWorld->stats().residentBodies);
    }
    return g_app
        ? static_cast<int>(g_app->getStats().physicsResidentBodies) : 0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_physics_stage_ms(int stage) {
    if (stage < 0
        || stage >= static_cast<int>(voxy::physics::kPhysicsGpuStageCount)) {
        return -1.0;
    }
    if (g_physicsBenchmarkTiming) {
        return g_physicsBenchmarkTiming->milliseconds[
            static_cast<size_t>(stage)];
    }
    if (!g_app) return -1.0;
    const auto& timing = g_app->getStats().physicsGpuTiming;
    return timing
        ? timing->milliseconds[static_cast<size_t>(stage)] : -1.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_physics_stage_tick() {
    if (g_physicsBenchmarkTiming) {
        return static_cast<double>(g_physicsBenchmarkTiming->tick);
    }
    if (!g_app) return 0.0;
    const auto& timing = g_app->getStats().physicsGpuTiming;
    return timing ? static_cast<double>(timing->tick) : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_frame_count() {
    return g_app ? static_cast<double>(g_app->getStats().frameCount) : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_physics_encoded_tick() {
    const voxy::physics::PhysicsWorld* world = g_app
        ? g_app->getPhysicsWorld() : g_physicsBenchmarkWorld.get();
    return world ? static_cast<double>(world->encodedTick()) : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_poll_physics_stage_timing() {
    g_polledPhysicsTiming = g_app
        ? g_app->pollPhysicsGpuTimingSample() : std::nullopt;
    return g_polledPhysicsTiming
        ? static_cast<double>(g_polledPhysicsTiming->tick) : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_polled_physics_stage_ms(int stage) {
    if (!g_polledPhysicsTiming || stage < 0
        || stage >= static_cast<int>(voxy::physics::kPhysicsGpuStageCount)) {
        return -1.0;
    }
    return g_polledPhysicsTiming->milliseconds[static_cast<size_t>(stage)];
}

EMSCRIPTEN_KEEPALIVE
double voxy_poll_render_stage_timing() {
    g_polledRenderTiming = g_app
        ? g_app->pollRenderGpuTimingSample() : std::nullopt;
    return g_polledRenderTiming
        ? static_cast<double>(g_polledRenderTiming->frame) : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_polled_render_stage_ms(int stage) {
    if (!g_polledRenderTiming || stage < 0
        || stage >= static_cast<int>(voxy::kRenderGpuStageCount)) {
        return -1.0;
    }
    return g_polledRenderTiming->milliseconds[static_cast<size_t>(stage)];
}

EMSCRIPTEN_KEEPALIVE
const char* voxy_get_telemetry_json() {
    static std::string snapshot;
    snapshot = makeTelemetryJson();
    return snapshot.empty() ? nullptr : snapshot.c_str();
}

EMSCRIPTEN_KEEPALIVE
int voxy_is_initialized() {
    return g_app != nullptr || g_physicsSelfTestRuntimeReady
        || g_physicsBenchmarkRuntimeReady ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_physics_backend() {
    const voxy::physics::PhysicsWorld* world = physicsWorldForSelfTest();
    return world
        ? static_cast<int>(world->backendType())
        : -1;
}

EMSCRIPTEN_KEEPALIVE
int voxy_start_physics_self_test() {
    return startPhysicsSelfTest();
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_physics_self_test_status() {
    finishPhysicsSelfTestFrame();
    return physicsSelfTestStatus();
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_physics_self_test_tick() {
    return static_cast<double>(g_physicsSelfTest.tick);
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_render_path() {
    if (g_app) {
        return static_cast<int>(g_app->getRenderPath());
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void voxy_set_render_path(int path) {
    if (g_app) {
        g_app->setRenderPath(static_cast<voxy::RenderPath>(path));
    }
}

EMSCRIPTEN_KEEPALIVE
void voxy_toggle_render_path() {
    if (g_app) {
        g_app->toggleRenderPath();
    }
}

} // extern "C"
