// ═══════════════════════════════════════════════════════════════════════════════
// entry.cpp (WASM) - Application Entry Point and Exports
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/application.hpp"
#include "camera/camera.hpp"
#include "core/log.hpp"
#include "core/config.hpp"
#include "engine/platform/input.hpp"
#include "gpu/context.hpp"
#include "perf/benchmark.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
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
    uint64_t g_gpuSubmittedFrames = 0;
    uint64_t g_gpuCompletedFrames = 0;
    uint64_t g_gpuCompletionTarget = 0;
    uint64_t g_gpuPacingSkips = 0;
    bool g_gpuPacingPaused = false;
    bool g_renderThroughputMode = false;
    bool g_renderThroughputFullQuality = false;

    // Bound both GPU submissions and notification storage. Independent
    // notifications let completed work retire while an older callback is late.
    // Pair cheap frames to avoid registering a callback for every submission.
    constexpr uint32_t kMaximumGpuFramesInFlight = 12;
    constexpr uint32_t kGpuCompletionIntervalFrames = 2;
    std::array<uint64_t, kMaximumGpuFramesInFlight> g_gpuCompletionTargets{};
    constexpr int kMaximumRenderThroughputFrames = 1'000'000;
    constexpr int kMaximumRenderThroughputBatchFrames = 256;

    enum class RenderThroughputStatus : int {
        Failed = -1,
        Idle = 0,
        Draining = 1,
        Warming = 2,
        Measuring = 3,
        Complete = 4,
    };

    struct RenderThroughputState {
        RenderThroughputStatus status = RenderThroughputStatus::Idle;
        uint32_t warmupRemaining = 0;
        uint32_t measuredRemaining = 0;
        uint32_t batchFrames = 0;
        uint32_t pendingBatchFrames = 0;
        uint64_t submittedMeasuredFrames = 0;
        uint64_t completedMeasuredFrames = 0;
        uint64_t terrainCacheRefreshesAtStart = 0;
        uint64_t staticCacheFramesAtStart = 0;
        uint64_t geometryWaterFramesAtStart = 0;
        double startMilliseconds = 0.0;
        double endMilliseconds = 0.0;
        double encodingMilliseconds = 0.0;
    };

    RenderThroughputState g_renderThroughput;

    [[nodiscard]] bool renderThroughputRunning() noexcept {
        return g_renderThroughput.status ==
                   RenderThroughputStatus::Draining ||
               g_renderThroughput.status ==
                   RenderThroughputStatus::Warming ||
               g_renderThroughput.status ==
                   RenderThroughputStatus::Measuring;
    }

    void submitRenderThroughputBatch();

    void renderThroughputBatchCompleted(
        WGPUQueueWorkDoneStatus status, WGPUStringView /*message*/,
        void* /*userdata1*/, void* /*userdata2*/) {
        if (status != WGPUQueueWorkDoneStatus_Success) {
            g_renderThroughput.status = RenderThroughputStatus::Failed;
            return;
        }

        if (g_renderThroughput.status == RenderThroughputStatus::Warming) {
            if (g_renderThroughput.warmupRemaining == 0u) {
                // Measure the same five deterministic camera scenarios on
                // every run, independent of how many warmup frames were used.
                g_app->startBenchmark();
                const voxy::ApplicationStats stats = g_app->getStats();
                g_renderThroughput.terrainCacheRefreshesAtStart =
                    stats.raycastTerrainCacheRefreshes;
                g_renderThroughput.staticCacheFramesAtStart =
                    stats.raycastStaticCacheFrames;
                g_renderThroughput.geometryWaterFramesAtStart =
                    stats.geometryWaterFrames;
                g_renderThroughput.status =
                    RenderThroughputStatus::Measuring;
            }
        } else if (g_renderThroughput.status ==
                   RenderThroughputStatus::Measuring) {
            g_renderThroughput.completedMeasuredFrames +=
                g_renderThroughput.pendingBatchFrames;
            if (g_renderThroughput.measuredRemaining == 0u) {
                g_renderThroughput.endMilliseconds = emscripten_get_now();
                g_renderThroughput.status =
                    RenderThroughputStatus::Complete;
                return;
            }
        }
        submitRenderThroughputBatch();
    }

    void renderThroughputDrainCompleted(
        WGPUQueueWorkDoneStatus status, WGPUStringView /*message*/,
        void* /*userdata1*/, void* /*userdata2*/) {
        if (status != WGPUQueueWorkDoneStatus_Success || !g_app) {
            g_renderThroughput.status = RenderThroughputStatus::Failed;
            return;
        }
        g_gpuFramesInFlight = 0u;
        g_gpuCompletedFrames = g_gpuSubmittedFrames;
        if (g_renderThroughput.warmupRemaining != 0u) {
            g_renderThroughput.status = RenderThroughputStatus::Warming;
        } else {
            g_app->startBenchmark();
            const voxy::ApplicationStats stats = g_app->getStats();
            g_renderThroughput.terrainCacheRefreshesAtStart =
                stats.raycastTerrainCacheRefreshes;
            g_renderThroughput.staticCacheFramesAtStart =
                stats.raycastStaticCacheFrames;
            g_renderThroughput.geometryWaterFramesAtStart =
                stats.geometryWaterFrames;
            g_renderThroughput.status =
                RenderThroughputStatus::Measuring;
        }
        submitRenderThroughputBatch();
    }

    void submitRenderThroughputBatch() {
        if (!g_app || !renderThroughputRunning() ||
            g_renderThroughput.status == RenderThroughputStatus::Draining) {
            return;
        }

        uint32_t frameCount = 0u;
        if (g_renderThroughput.status == RenderThroughputStatus::Warming) {
            frameCount = std::min(g_renderThroughput.batchFrames,
                                  g_renderThroughput.warmupRemaining);
            g_renderThroughput.warmupRemaining -= frameCount;
        } else {
            frameCount = std::min(g_renderThroughput.batchFrames,
                                  g_renderThroughput.measuredRemaining);
            if (g_renderThroughput.submittedMeasuredFrames == 0u) {
                g_renderThroughput.startMilliseconds = emscripten_get_now();
            }
            g_renderThroughput.measuredRemaining -= frameCount;
            g_renderThroughput.submittedMeasuredFrames += frameCount;
        }
        if (frameCount == 0u) {
            g_renderThroughput.status = RenderThroughputStatus::Failed;
            return;
        }
        g_renderThroughput.pendingBatchFrames = frameCount;

        // Fixed visual time advances all analytic waves every frame while the
        // full 256x256 spectral state retains its independent 120 Hz cadence.
        constexpr float kBenchmarkDeltaSeconds = 1.0f / 700.0f;
        const double encodingStartMilliseconds = emscripten_get_now();
        for (uint32_t frame = 0u; frame < frameCount; ++frame) {
            g_app->processFrame(kBenchmarkDeltaSeconds,
                                kBenchmarkDeltaSeconds);
        }
        if (g_renderThroughput.status ==
            RenderThroughputStatus::Measuring) {
            g_renderThroughput.encodingMilliseconds +=
                emscripten_get_now() - encodingStartMilliseconds;
        }

        WGPUQueueWorkDoneCallbackInfo callbackInfo =
            WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        callbackInfo.callback = renderThroughputBatchCompleted;
        static_cast<void>(wgpuQueueOnSubmittedWorkDone(
            g_app->getGPUContext()->getQueue(), callbackInfo));
    }

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
        const double renderThroughputElapsed = std::max(
            g_renderThroughput.endMilliseconds -
                g_renderThroughput.startMilliseconds,
            0.0);
        const double renderThroughputFps =
            renderThroughputElapsed > 0.0
            ? static_cast<double>(
                  g_renderThroughput.completedMeasuredFrames) *
                  1000.0 / renderThroughputElapsed
            : 0.0;
        const voxy::gpu::Context* gpuContext =
            g_app ? g_app->getGPUContext() : nullptr;

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
            << ",\"gpu_queue_limit\":" << kMaximumGpuFramesInFlight
            << ",\"pacing_skips\":" << g_gpuPacingSkips << '}';

        out << ",\"render\":{\"path\":\""
            << voxy::renderPathToString(app.activeRenderPath) << "\""
            << ",\"terrain_width\":" << app.terrainWidth
            << ",\"terrain_height\":" << app.terrainHeight
            << ",\"terrain_mips\":" << app.terrainMipLevels
            << ",\"terrain_cache_refreshes\":"
            << app.raycastTerrainCacheRefreshes
            << ",\"static_cache_frames\":"
            << app.raycastStaticCacheFrames
            << ",\"geometry_water_frames\":"
            << app.geometryWaterFrames
            << ",\"submitted_primitives\":"
            << app.primitiveSubmittedCount << '}';

        // Timestamp packets are asynchronous. Expose their source frame so
        // consumers do not subtract stale samples from a current CPU frame.
        out << ",\"render_gpu\":{\"available\":"
            << (app.renderGpuTiming ? "true" : "false");
        if (app.renderGpuTiming) {
            const auto& timing = *app.renderGpuTiming;
            out << ",\"frame\":" << timing.frame
                << ",\"age_frames\":"
                << (app.frameCount >= timing.frame
                        ? app.frameCount - timing.frame : 0u);
            constexpr const char* names[] = {
                "water_simulation_ms", "terrain_raycast_ms",
                "lighting_composite_ms", "primitives_ms"};
            static_assert(std::size(names) == voxy::kRenderGpuStageCount);
            double total = 0.0;
            for (size_t stage = 0; stage < voxy::kRenderGpuStageCount; ++stage) {
                out << ",\"" << names[stage] << "\":";
                appendJsonNumber(out, timing.milliseconds[stage]);
                total += timing.milliseconds[stage];
            }
            out << ",\"total_ms\":";
            appendJsonNumber(out, total);
            out << ",\"frame_interval_available\":"
                << (timing.frameIntervalAvailable ? "true" : "false")
                << ",\"gpu_frame_ms\":";
            if (timing.frameIntervalAvailable) appendJsonNumber(out, timing.frameMilliseconds);
            else out << "null";
            out << ",\"render_width\":" << timing.renderWidth
                << ",\"render_height\":" << timing.renderHeight
                << ",\"includes_gpu_physics\":true";
        }
        out << '}';

        out << ",\"render_throughput\":{\"status\":"
            << static_cast<int>(g_renderThroughput.status)
            << ",\"full_quality\":"
            << (g_renderThroughputFullQuality ? "true" : "false")
            << ",\"width\":"
            << (gpuContext ? gpuContext->getSwapchainWidth() : 0u)
            << ",\"height\":"
            << (gpuContext ? gpuContext->getSwapchainHeight() : 0u)
            << ",\"batch_frames\":" << g_renderThroughput.batchFrames
            << ",\"submitted_frames\":"
            << g_renderThroughput.submittedMeasuredFrames
            << ",\"completed_frames\":"
            << g_renderThroughput.completedMeasuredFrames
            << ",\"encoding_ms\":";
        appendJsonNumber(out, g_renderThroughput.encodingMilliseconds);
        out << ",\"terrain_cache_refreshes\":"
            << (app.raycastTerrainCacheRefreshes -
                g_renderThroughput.terrainCacheRefreshesAtStart)
            << ",\"static_cache_frames\":"
            << (app.raycastStaticCacheFrames -
                g_renderThroughput.staticCacheFramesAtStart)
            << ",\"geometry_water_frames\":"
            << (app.geometryWaterFrames -
                g_renderThroughput.geometryWaterFramesAtStart)
            << ",\"elapsed_ms\":";
        appendJsonNumber(out, renderThroughputElapsed);
        out << ",\"fps\":";
        appendJsonNumber(out, renderThroughputFps);
        out << '}';

        out << ",\"physics\":{\"backend\":\""
            << voxy::physics::backendTypeName(physics.backend) << "\""
            << ",\"arithmetic\":\""
            << voxy::physics::physicsArithmeticModeName(
                   physics.arithmeticMode) << "\""
            << ",\"tick\":" << physics.telemetryTick
            << ",\"substeps\":" << physics.substeps
            << ",\"fixed_tick_seconds\":";
        appendJsonNumber(out, physics.fixedTickSeconds);
        out << ",\"maximum_catch_up_ticks\":"
            << physics.maximumCatchUpTicks
            << ",\"broad_phase_cell_size\":";
        appendJsonNumber(out, physics.broadPhaseCellSize);
        out << ",\"solver_workgroup_size\":"
            << physics.solverWorkgroupSize
            << ",\"solver_color_count\":"
            << physics.solverColorCount
            << ",\"solver_parallel_color_count\":"
            << physics.solverParallelColorCount
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
        out << ",\"occupied_cells\":" << physics.occupiedCells
            << ",\"maximum_cell_bodies\":"
            << physics.maximumCellBodies
            << ",\"pair_driving_bodies\":"
            << physics.broadPhasePairDrivingBodies;
        out << ",\"candidate_pairs\":";
        appendCapacityUsage(out, physics.candidatePairUsage);
        out << ",\"pairs\":";
        appendCapacityUsage(out, physics.uniquePairUsage);
        out << ",\"sleeping_pairs\":" << physics.activeSleepingPairs
            << ",\"oversized_bodies\":" << physics.oversizedBodies;

        out << ",\"contacts\":";
        appendCapacityUsage(out, physics.contactUsage);
        out << ",\"narrow_pair_classes\":[";
        for (size_t index = 0; index < physics.narrowPairClasses.size();
             ++index) {
            if (index != 0u) out << ',';
            out << physics.narrowPairClasses[index];
        }
        out << "],\"narrow_collision_pair_classes\":[";
        for (size_t index = 0;
             index < physics.narrowCollisionPairClasses.size(); ++index) {
            if (index != 0u) out << ',';
            out << physics.narrowCollisionPairClasses[index];
        }
        out << ']';
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

    void requestGpuFrameCompletion();

    void gpuFrameCompleted(WGPUQueueWorkDoneStatus /*status*/,
                           WGPUStringView /*message*/, void* userdata1,
                           void* /*userdata2*/) {
        auto& target = *static_cast<uint64_t*>(userdata1);
        g_gpuCompletedFrames = std::max(g_gpuCompletedFrames, target);
        target = 0u;
        const uint64_t outstanding =
            g_gpuSubmittedFrames - g_gpuCompletedFrames;
        g_gpuFramesInFlight = static_cast<uint32_t>(std::min<uint64_t>(
            outstanding, std::numeric_limits<uint32_t>::max()));
        if (!g_app || !g_app->isInitialized()) return;
        if (g_gpuPacingPaused
            && g_gpuFramesInFlight < kMaximumGpuFramesInFlight
            && !renderThroughputRunning()) {
            g_gpuPacingPaused = false;
            emscripten_resume_main_loop();
        }
        if (!renderThroughputRunning()) requestGpuFrameCompletion();
    }

    void requestGpuFrameCompletion() {
        if (!g_app || !g_app->isInitialized()
            || g_gpuCompletionTarget == g_gpuSubmittedFrames
            || g_gpuCompletedFrames == g_gpuSubmittedFrames) {
            return;
        }
        // Always cover a full queue, including an unpaired final frame.
        if (g_gpuSubmittedFrames - g_gpuCompletionTarget
                < kGpuCompletionIntervalFrames
            && g_gpuSubmittedFrames - g_gpuCompletedFrames
                < kMaximumGpuFramesInFlight) {
            return;
        }
        const auto slot = std::find(g_gpuCompletionTargets.begin(),
            g_gpuCompletionTargets.end(), 0u);
        if (slot == g_gpuCompletionTargets.end()) return;
        g_gpuCompletionTarget = g_gpuSubmittedFrames;
        *slot = g_gpuSubmittedFrames;
        WGPUQueueWorkDoneCallbackInfo callbackInfo =
            WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        callbackInfo.callback = gpuFrameCompleted;
        callbackInfo.userdata1 = &*slot;
        static_cast<void>(wgpuQueueOnSubmittedWorkDone(
            g_app->getGPUContext()->getQueue(), callbackInfo));
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
        const value = Number(
            new URLSearchParams(globalThis.location.search)
                .get("physicsBenchmarkBodies") ?? "0");
        return Number.isSafeInteger(value) && value > 0 && value <= $0
            ? value : 0;
    }, voxy::kMaximumBenchmarkBodyCount));
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
    appConfig.motoEnabled = config.window.title == "RIDGEBREAK";
    
    // Window settings
    appConfig.windowWidth = config.window.width;
    appConfig.windowHeight = config.window.height;
    appConfig.windowTitle = config.window.title.empty()
                          ? "RIDGEBREAK"
                          : config.window.title;
    appConfig.fullscreen = config.window.fullscreen;
    // The web build historically starts with the immediate Emscripten loop.
    // Using the native VSync default here silently caps a normal browser tab
    // to the monitor refresh rate (about 85 FPS on the development display).
    // F9 remains available for users who prefer RAF pacing.
    appConfig.vsync = false;

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
    appConfig.sunDirection = {
        config.lighting.sunDirection[0], config.lighting.sunDirection[1],
        config.lighting.sunDirection[2]};
    appConfig.sunColor = {
        config.lighting.sunColor[0], config.lighting.sunColor[1],
        config.lighting.sunColor[2]};
    appConfig.ambientColor = {
        config.lighting.ambientColor[0], config.lighting.ambientColor[1],
        config.lighting.ambientColor[2]};
    appConfig.fogDensity = config.lighting.fogDensity;
    appConfig.fogColor = {
        config.lighting.fogColor[0], config.lighting.fogColor[1],
        config.lighting.fogColor[2]};
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
    const int browserPhysicsBackend = EM_ASM_INT({
        const value = new URLSearchParams(globalThis.location.search)
            .get("physicsBackend")?.toLowerCase();
        if (value === "jolt" || value === "jolt_legacy") return 1;
        if (value === "box3d" || value === "box3d_reference") return 2;
        if (value === "webgpu" || value === "webgpu_soft") return 3;
        return 0;
    });
    if (browserPhysicsBackend == 1) {
        appConfig.physicsBackend = voxy::physics::BackendType::JoltLegacy;
    } else if (browserPhysicsBackend == 2) {
        appConfig.physicsBackend = voxy::physics::BackendType::Box3DReference;
    } else if (browserPhysicsBackend == 3) {
        appConfig.physicsBackend = voxy::physics::BackendType::WebGpuSoft;
    }
    appConfig.gpuPhysicsMaxBodies = static_cast<uint32_t>(
        std::max(config.physics.gpuMaxBodies, 2));
    appConfig.gpuPhysicsBroadPhaseCellSize =
        config.physics.broadPhaseCellSize;
    appConfig.gpuPhysicsBroadPhaseCellSize = static_cast<float>(
        EM_ASM_DOUBLE({
            const value = Number(new URLSearchParams(globalThis.location.search)
                .get("broadPhaseCellSize") ?? $0);
            return Number.isFinite(value) && value > 0 ? value : $0;
        }, appConfig.gpuPhysicsBroadPhaseCellSize));
    appConfig.cubePyramidBodyCount =
        static_cast<uint32_t>(EM_ASM_INT({
            const parameters =
                new URLSearchParams(globalThis.location.search);
            const experiment =
                (parameters.get("experiment") ?? "").toLowerCase();
            const fallback =
                experiment === "triangle"
                || experiment === "cube-triangle"
                || experiment === "pyramid"
                || experiment === "cube-pyramid" ? $0 : 0;
            const value = Number(
                parameters.get("triangleBodies")
                ?? parameters.get("pyramidBodies")
                ?? fallback);
            return Number.isSafeInteger(value) && value > 0 && value <= $1
                ? value : 0;
        }, voxy::kDefaultCubePyramidBodyCount,
           voxy::kMaximumBenchmarkBodyCount));
    if (appConfig.cubePyramidBodyCount != 0u) {
        LOG_INFO("Browser requested a {}-cube triangle",
                 appConfig.cubePyramidBodyCount);
    }
    if (appConfig.cubePyramidBodyCount != 0u) {
        const bool broadPhaseCellSizeExplicit = EM_ASM_INT({
            return new URLSearchParams(globalThis.location.search)
                .has("broadPhaseCellSize");
        }) != 0;
        if (!broadPhaseCellSizeExplicit) {
            appConfig.gpuPhysicsBroadPhaseCellSize = 2.0f;
        }
        // The staged one-thick 20k triangular wall stays below four pairs per
        // body in measured runs. Other exploratory sizes keep a wider reserve.
        const uint64_t pairsPerBody =
            appConfig.cubePyramidBodyCount
                == voxy::kDefaultCubePyramidBodyCount ? 4u : 6u;
        const uint64_t desiredPairs = std::max(
            uint64_t{appConfig.gpuPhysicsMaxPairs},
            uint64_t{appConfig.cubePyramidBodyCount} * pairsPerBody);
        uint32_t pyramidPairCapacity = 1u;
        while (pyramidPairCapacity < desiredPairs
               && pyramidPairCapacity <= (1u << 30u)) {
            pyramidPairCapacity <<= 1u;
        }
        appConfig.gpuPhysicsMaxPairs =
            std::max(appConfig.gpuPhysicsMaxPairs, pyramidPairCapacity);
        appConfig.gpuPhysicsMaxCandidatePairs = std::max(
            appConfig.gpuPhysicsMaxCandidatePairs,
            appConfig.gpuPhysicsMaxPairs * 2u);
    }
    appConfig.gpuPhysicsMaxPairs = static_cast<uint32_t>(EM_ASM_INT({
        const value = Number(new URLSearchParams(globalThis.location.search)
            .get("physicsPairCapacity") ?? $0);
        return Number.isSafeInteger(value) && value > 0
            && value <= 2147483647 ? value : $0;
    }, appConfig.gpuPhysicsMaxPairs));
    appConfig.gpuPhysicsMaxCandidatePairs =
        static_cast<uint32_t>(EM_ASM_INT({
            const value = Number(
                new URLSearchParams(globalThis.location.search)
                    .get("physicsCandidatePairCapacity") ?? $0);
            return Number.isSafeInteger(value) && value > 0
                && value <= 2147483647 ? value : $0;
        }, appConfig.gpuPhysicsMaxCandidatePairs));
    appConfig.gpuPhysicsSolverWorkgroupSize =
        static_cast<uint32_t>(EM_ASM_INT({
            const value = Number(
                new URLSearchParams(globalThis.location.search)
                    .get("physicsSolverWorkgroup") ?? $0);
            return value === 128 || value === 256 ? value : $0;
        }, appConfig.gpuPhysicsSolverWorkgroupSize));
    appConfig.benchmarkBodyCount = static_cast<uint32_t>(EM_ASM_INT({
        const value = Number(
            new URLSearchParams(globalThis.location.search)
                .get("benchmarkBodies") ?? "0");
        return Number.isSafeInteger(value) && value > 0 && value <= $0
            ? value : 0;
    }, voxy::kMaximumBenchmarkBodyCount));
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
        appConfig.heightmapWidth = appConfig.motoEnabled ? 2048u : 256u;
        appConfig.heightmapHeight = appConfig.heightmapWidth;
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
        const params = new URLSearchParams(globalThis.location.search);
        const value = params.get("physicsProfile");
        return value === "1"
            || (value !== "0" && params.get("renderThroughput") !== "1");
    }) != 0;
    appConfig.gpuRenderStageProfiling = EM_ASM_INT({
        const params = new URLSearchParams(globalThis.location.search);
        const value = params.get("renderProfile");
        return value === "1"
            || (value !== "0" && params.get("renderThroughput") !== "1");
    }) != 0;

    g_renderThroughputMode = EM_ASM_INT({
        return new URLSearchParams(globalThis.location.search)
            .get("renderThroughput") === "1" ? 1 : 0;
    }) != 0;
    if (g_renderThroughputMode) {
        // Use a physical-size offscreen target so compositor refresh rate,
        // occlusion, and scan-out do not contaminate completed GPU throughput.
        appConfig.benchmarkOnStartup = true;
        appConfig.benchmarkFixedDeltaSeconds = 1.0f / 700.0f;
        g_renderThroughputFullQuality =
            appConfig.renderPath == voxy::RenderPath::Raycast &&
            std::abs(appConfig.resolutionScale - 1.0f) < 1.0e-6f &&
            appConfig.waterEnabled;
    }

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

        // Bound GPU queue latency. Keep elapsed time across pacing waits;
        // the physics backend separately bounds catch-up ticks and clock debt.
        if (g_gpuFramesInFlight >= kMaximumGpuFramesInFlight) {
            ++g_gpuPacingSkips;
            if (g_app->isUncappedFPS()) {
                g_gpuPacingPaused = true;
                emscripten_pause_main_loop();
            }
            return;
        }

        // Check for loop timing changes
        if (g_app->isUncappedFPS() != currentUncapped) {
            currentUncapped = g_app->isUncappedFPS();
            if (currentUncapped) {
                // Submit while queue headroom exists. Completion notifications
                // wake the paused loop directly when room becomes available.
                emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
                LOG_INFO("Switched to Uncapped Loop (SETIMMEDIATE)");
            } else {
                // Switch back to RAF loop (capped)
                emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
                LOG_INFO("Switched to Capped Loop (RAF)");
            }
        }

        const double now = emscripten_get_now() / 1000.0;
        const float frameDeltaTime =
            static_cast<float>(now - lastSubmittedTime);
        lastSubmittedTime = now;

        // Pacing waits belong to simulation time too. Clamp only long tab
        // suspensions; the FPS counter retains the full frame interval.
        const float simulationDeltaTime = std::min(frameDeltaTime, 0.1f);

        const double frameStartMilliseconds = emscripten_get_now();
        g_app->processFrame(simulationDeltaTime, frameDeltaTime);
        g_lastFrameCpuMilliseconds =
            emscripten_get_now() - frameStartMilliseconds;

        ++g_gpuSubmittedFrames;
        const uint64_t outstanding =
            g_gpuSubmittedFrames - g_gpuCompletedFrames;
        g_gpuFramesInFlight = static_cast<uint32_t>(std::min<uint64_t>(
            outstanding, std::numeric_limits<uint32_t>::max()));
        requestGpuFrameCompletion();
    };

    // Select the initial scheduler here. Waiting for the first RAF callback
    // to discover that the application is uncapped can deadlock a compositor-
    // throttled browser at frame zero (the deployed benchmark caught this).
    currentUncapped = g_app->isUncappedFPS();

    // A positive initial rate makes Emscripten schedule the very first call
    // with a timer. Setting SETIMMEDIATE after a zero-rate registration can
    // leave one already-scheduled RAF callback in front of it.
    const int initialLoopRate = currentUncapped ? 1'000 : 0;
    emscripten_set_main_loop(mainLoop, initialLoopRate, false);
    if (currentUncapped) {
        emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
        LOG_INFO("Started Uncapped Loop (SETIMMEDIATE)");
    } else {
        emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
        LOG_INFO("Started Capped Loop (RAF)");
    }

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
int voxy_renderer_set_number(const char* name, double value, int commit) {
    if (!g_app || !name) return 0;
    return g_app->setRendererSetting(name, value, commit != 0) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_renderer_get_number(const char* name) {
    if (!g_app || !name) return std::numeric_limits<double>::quiet_NaN();
    const auto value = g_app->getRendererSetting(name);
    return value.value_or(std::numeric_limits<double>::quiet_NaN());
}

EMSCRIPTEN_KEEPALIVE
double voxy_renderer_get_revision() {
    return g_app
        ? static_cast<double>(g_app->getRendererSettingsRevision()) : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_renderer_get_applied_revision() {
    return g_app
        ? static_cast<double>(g_app->getAppliedRendererSettingsRevision()) : 0.0;
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

EMSCRIPTEN_KEEPALIVE
int voxy_set_camera_pose(float x, float y, float z,
                         float yaw, float pitch) {
    if (!g_app || !g_app->getCamera()
        || !std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(z) || !std::isfinite(yaw)
        || !std::isfinite(pitch)) {
        return 0;
    }
    voxy::Camera* camera = g_app->getCamera();
    camera->setWorldPosition(glm::ivec3(0), glm::vec3(x, y, z));
    camera->setYaw(yaw);
    camera->setPitch(pitch);
    return 1;
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
double voxy_get_last_frame_wall_ms() {
    return g_app ? g_app->getStats().frameTimeMs : 0.0;
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_gpu_frames_in_flight() {
    return static_cast<int>(g_gpuFramesInFlight);
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_gpu_frame_limit() {
    return static_cast<int>(kMaximumGpuFramesInFlight);
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_gpu_pacing_skips() {
    return static_cast<double>(g_gpuPacingSkips);
}

EMSCRIPTEN_KEEPALIVE
int voxy_start_render_throughput_benchmark(
    int warmupFrames, int measuredFrames, int batchFrames) {
    if (!g_app || !g_app->getGPUContext() || !g_renderThroughputMode ||
        !g_renderThroughputFullQuality || warmupFrames < 0 ||
        measuredFrames <= 0 || batchFrames <= 0 ||
        warmupFrames > kMaximumRenderThroughputFrames ||
        measuredFrames > kMaximumRenderThroughputFrames ||
        batchFrames > kMaximumRenderThroughputBatchFrames ||
        batchFrames > measuredFrames || renderThroughputRunning()) {
        return 0;
    }

    g_renderThroughput = RenderThroughputState{
        .status = RenderThroughputStatus::Draining,
        .warmupRemaining = static_cast<uint32_t>(warmupFrames),
        .measuredRemaining = static_cast<uint32_t>(measuredFrames),
        .batchFrames = static_cast<uint32_t>(batchFrames),
    };
    // Stop only Emscripten's interactive scheduling task. Queue callbacks stay
    // live and drive completion-verified batches until every measured frame
    // has retired on the GPU.
    g_gpuPacingPaused = false;
    emscripten_pause_main_loop();

    WGPUQueueWorkDoneCallbackInfo callbackInfo =
        WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = renderThroughputDrainCompleted;
    static_cast<void>(wgpuQueueOnSubmittedWorkDone(
        g_app->getGPUContext()->getQueue(), callbackInfo));
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_render_throughput_status() {
    return static_cast<int>(g_renderThroughput.status);
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_render_throughput_fps() {
    const double elapsed = g_renderThroughput.endMilliseconds -
                           g_renderThroughput.startMilliseconds;
    return elapsed > 0.0
        ? static_cast<double>(
              g_renderThroughput.completedMeasuredFrames) * 1000.0 / elapsed
        : 0.0;
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_render_throughput_elapsed_ms() {
    return std::max(g_renderThroughput.endMilliseconds -
                        g_renderThroughput.startMilliseconds,
                    0.0);
}

EMSCRIPTEN_KEEPALIVE
double voxy_get_render_throughput_completed_frames() {
    return static_cast<double>(
        g_renderThroughput.completedMeasuredFrames);
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_uncapped_fps() {
    return g_app && g_app->isUncappedFPS() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void voxy_set_uncapped_fps(int enabled) {
    if (g_app) g_app->setUncappedFPS(enabled != 0);
}

EMSCRIPTEN_KEEPALIVE
int voxy_start_cube_pyramid_experiment() {
    return g_app && g_app->startCubePyramidExperiment() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int voxy_start_browser_journey_benchmark(
    int targetBodies, int warmupTicks, int impactTicks, int settleTicks,
    int bodiesPerVolley, int ticksPerVolley, int layout, int shape) {
    const bool observesCubePyramid =
        layout == 2 && targetBodies == 0 && shape == 1;
    const bool throwsBodies = layout >= 0 && layout <= 2 && targetBodies > 0;
    if (!g_app || (!observesCubePyramid && !throwsBodies)
        || targetBodies > static_cast<int>(voxy::kMaximumBenchmarkBodyCount)
        || warmupTicks < 0 || impactTicks <= 0 || settleTicks <= 0
        || bodiesPerVolley <= 0
        || bodiesPerVolley > 128 || ticksPerVolley <= 0
        || ticksPerVolley > 3'600 || layout < 0 || layout > 2
        || shape < 0 || shape > 5) {
        return 0;
    }
    return g_app->startBrowserJourneyBenchmark(
        static_cast<uint32_t>(targetBodies),
        static_cast<uint32_t>(warmupTicks),
        static_cast<uint32_t>(impactTicks),
        static_cast<uint32_t>(settleTicks),
        static_cast<uint32_t>(bodiesPerVolley),
        static_cast<uint32_t>(ticksPerVolley),
        static_cast<voxy::perf::BrowserJourneyLayout>(layout),
        static_cast<voxy::perf::BrowserJourneyShape>(shape)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int voxy_get_browser_journey_benchmark_status() {
    return g_app ? g_app->browserJourneyBenchmarkStatus() : 0;
}

EMSCRIPTEN_KEEPALIVE
const char* voxy_get_browser_journey_benchmark_json() {
    static std::string snapshot;
    snapshot = g_app ? g_app->browserJourneyBenchmarkJson() : std::string{};
    return snapshot.empty() ? nullptr : snapshot.c_str();
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
void voxy_set_throwable_body_limit(int limit) {
    if (g_app) {
        g_app->setThrowableBodyLimit(
            static_cast<uint32_t>(std::max(limit, 0)));
    }
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
const char* voxy_get_moto_hud_json() {
    static std::string snapshot;
    if (!g_app) return nullptr;
    const voxy::MotoHudState hud = g_app->getMotoHudState();
    if (!hud.active) return nullptr;
    std::ostringstream out;
    out << "{\"active\":true"
        << ",\"speed_kph\":" << hud.speedKilometersPerHour
        << ",\"rpm\":" << hud.engineRpm
        << ",\"gear\":" << hud.gear
        << ",\"crash\":" << hud.crashState
        << ",\"combo\":" << hud.combo
        << ",\"score\":" << hud.score
        << ",\"mode\":" << (hud.raceActive ? "\"circuit\""
                                                  : "\"practice\"")
        << ",\"race_active\":" << (hud.raceActive ? "true" : "false");
    if (hud.raceActive) {
        out << ",\"race_phase\":" << hud.racePhase
            << ",\"next_checkpoint\":" << hud.nextCheckpoint
            << ",\"checkpoint_count\":" << hud.checkpointCount
            << ",\"completed_laps\":" << hud.completedLaps
            << ",\"lap_count\":" << hud.lapCount
            << ",\"countdown_ticks\":" << hud.countdownTicksRemaining
            << ",\"finish_place\":" << hud.finishPlace
            << ",\"dnf\":" << (hud.didNotFinish ? "true" : "false");
    }
    out << '}';
    snapshot = out.str();
    return snapshot.c_str();
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
