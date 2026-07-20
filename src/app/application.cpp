// ═══════════════════════════════════════════════════════════════════════════════
// application.cpp - Main Application Shell Implementation
// ═══════════════════════════════════════════════════════════════════════════════

// NOTE: Include terrain/heightmap.hpp BEFORE any headers that might include X11
// because X11 defines "None" as a macro which conflicts with HeightmapError::None
#include "terrain/heightmap.hpp"

#include "app/application.hpp"
#include "engine/platform/window.hpp"
#include "engine/platform/input.hpp"
#include "app/debug_overlay.hpp"
#include "camera/camera.hpp"
#include "camera/controller.hpp"
#include "camera/character_controller.hpp"
#include "core/timer.hpp"
#include "perf/benchmark.hpp"
#include "core/log.hpp"
#include "core/timer.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "terrain/textures.hpp"
#include "terrain/shadow_bake.hpp"
#include "render/triangle_path.hpp"
#include "render/raycast_path.hpp"
#include "render/blit_path.hpp"
#include "render/water_simulation.hpp"
#include "render/primitive_path.hpp"
#include "render/primitive_culling.hpp"
#include "physics/physics_world.hpp"

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>
#include <span>
#include <system_error>
#include <vector>

#if defined(VOXY_NATIVE)
    #include <stb_image_write.h>
    // Platform-specific sleep includes for async polling
    #if defined(_WIN32)
        #include <windows.h>
    #else
        #include <unistd.h>
    #endif
    // wgpu-native extras (not in the core WebGPU header)
    #ifndef WGPUWrappedSubmissionIndex
    using WGPUSubmissionIndex = uint64_t;
    typedef struct WGPUWrappedSubmissionIndex {
        WGPUQueue queue;
        WGPUSubmissionIndex submissionIndex;
    } WGPUWrappedSubmissionIndex;
    #endif
    extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(WGPUQueue queue, size_t commandCount, const WGPUCommandBuffer* commands);
    extern "C" WGPUBool wgpuDevicePoll(WGPUDevice device, WGPUBool wait, const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);
#endif

#if defined(VOXY_WASM)
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

namespace voxy {

// World-space direction toward the sun. Single source of truth: the per-frame
// light uniform and the baked shadow height field must agree.
constexpr glm::vec3 kSunDirection = {
    0.6040228f, 0.7660444f, 0.2198463f};
constexpr size_t kPrimitiveOverlayHeadroom = 1u + 5u * 7u;
constexpr uint32_t kRenderGpuQueriesPerStage = 2u;
constexpr uint32_t kRenderGpuTimestampCount =
    static_cast<uint32_t>(kRenderGpuStageCount)
        * kRenderGpuQueriesPerStage;
constexpr uint32_t kRenderGpuProfilingIntervalFrames = 30u;

void appendObjectCount(
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& instances,
    size_t objectCount, const glm::vec3& planeCenter,
    const glm::vec3& cameraRight, const glm::vec3& cameraUp,
    const glm::quat& cameraRotation, float halfWidth, float halfHeight) {
    // Segment bits: top, upper-right, lower-right, bottom,
    // lower-left, upper-left, middle.
    constexpr std::array<uint8_t, 10> digitMasks = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };

    const std::string digits = std::to_string(objectCount);
    const float screenScale = std::min(halfWidth, halfHeight);
    const float digitHeight = screenScale * 0.18f;
    const float digitWidth = digitHeight * 0.52f;
    const float thickness = digitHeight * 0.11f;
    const float verticalLength = digitHeight * 0.39f;
    const float gap = thickness * 1.4f;
    const float advance = digitWidth + gap;
    const float totalWidth = digitWidth * static_cast<float>(digits.size())
                           + gap * static_cast<float>(digits.size() - 1);
    const float leftEdge = halfWidth * 0.90f - totalWidth;
    const float centerY = -halfHeight * 0.38f;

    const auto addSegment = [&](float centerX, float segmentY,
                                float width, float height) {
        instances.push_back({
            physics::PhysicsWorld::ThrowableShape::Cube,
            planeCenter + cameraRight * centerX + cameraUp * segmentY,
            cameraRotation,
            glm::vec3(width, height, thickness * 0.35f)});
    };

    for (size_t digitIndex = 0; digitIndex < digits.size(); ++digitIndex) {
        const uint8_t mask = digitMasks[static_cast<size_t>(digits[digitIndex] - '0')];
        const float centerX = leftEdge + digitWidth * 0.5f
                            + static_cast<float>(digitIndex) * advance;
        const float xSide = digitWidth * 0.5f;
        const float ySide = digitHeight * 0.25f;
        const float yEdge = digitHeight * 0.5f;

        if (mask & (1u << 0u)) addSegment(centerX, centerY + yEdge, digitWidth, thickness);
        if (mask & (1u << 1u)) addSegment(centerX + xSide, centerY + ySide, thickness, verticalLength);
        if (mask & (1u << 2u)) addSegment(centerX + xSide, centerY - ySide, thickness, verticalLength);
        if (mask & (1u << 3u)) addSegment(centerX, centerY - yEdge, digitWidth, thickness);
        if (mask & (1u << 4u)) addSegment(centerX - xSide, centerY - ySide, thickness, verticalLength);
        if (mask & (1u << 5u)) addSegment(centerX - xSide, centerY + ySide, thickness, verticalLength);
        if (mask & (1u << 6u)) addSegment(centerX, centerY, digitWidth, thickness);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

const char* renderPathToString(RenderPath path) noexcept {
    switch (path) {
        case RenderPath::Triangle: return "triangle";
        case RenderPath::Raycast:  return "raycast";
    }
    return "unknown";
}

const char* debugVisModeToString(DebugVisMode mode) noexcept {
    switch (mode) {
        case DebugVisMode::Off:       return "off";
        case DebugVisMode::Depth:     return "depth";
        case DebugVisMode::Normals:   return "normals";
        case DebugVisMode::MipLevels: return "mip_levels";
    }
    return "unknown";
}

const char* controllerModeToString(ControllerMode mode) noexcept {
    switch (mode) {
        case ControllerMode::FreeFly:   return "free-fly";
        case ControllerMode::Character: return "character";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// WASM Global State (for Emscripten main loop)
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Application::Application() = default;

Application::~Application() {
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool Application::init(const ApplicationConfig& config) {
    if (initialized_) {
        LOG_WARN("Application already initialized");
        return true;
    }

    config_ = config;
    uncappedFPS_ = !config_.vsync;
    primitiveCullController_.reset();

    // Initialize teleport targets
    // Paste recorded positions here!
    teleportTargets_ = {
        { { 202.53f, 120.92f, -27.16f }, 1.4578f, -0.0944f },
        { { 202.53f, 120.92f, -27.16f }, -0.2070f, -0.3556f },
        { { 179.01f, 121.64f, -28.72f }, -1.3958f, -0.1900f },
        { { 179.01f, 121.64f, -28.72f }, -2.0606f, -0.0380f },
        { { -885.41f, -59.61f, 170.87f }, -1.3456f, -0.0578f },
        { { -876.38f, -61.58f, 158.43f }, -4.3228f, -0.2176f },
        { { -885.41f, -180.0f, 170.87f }, -1.3456f, -0.0578f },
        { { 5000.0f, -215.0f, 0.0f }, 1.7960f, -0.3000f },
        { { 5000.0f, -180.0f, 0.0f }, 1.7960f, -0.0578f },
    };

    LOG_INFO("═══════════════════════════════════════════════════════════════");
    LOG_INFO("  voxy v0.1.0 - WebGPU Terrain Renderer");
#if defined(VOXY_NATIVE)
    LOG_INFO("  Build target: native");
#elif defined(VOXY_WASM)
    LOG_INFO("  Build target: wasm");
#endif
    LOG_INFO("  Render path: {}", renderPathToString(config_.renderPath));
    LOG_INFO("═══════════════════════════════════════════════════════════════");

    // Initialize subsystems in order
    {
        LOG_SCOPE("Application::init");

        if (!initWindow()) {
            LOG_ERROR("Failed to initialize window");
            return false;
        }

        if (!initGPU()) {
            LOG_ERROR("Failed to initialize GPU context");
            return false;
        }

        if (!initRenderGpuProfiling()) {
            LOG_ERROR("Failed to initialize render GPU profiling");
            return false;
        }

        if (!initInput()) {
            LOG_ERROR("Failed to initialize input system");
            return false;
        }

        if (!initCamera()) {
            LOG_ERROR("Failed to initialize camera");
            return false;
        }

        if (!initTerrain()) {
            LOG_ERROR("Failed to initialize terrain");
            return false;
        }

        if (!initRenderers()) {
            LOG_ERROR("Failed to initialize renderers");
            return false;
        }

        setupCallbacks();
    }

    initialized_ = true;
    shouldExit_ = false;

    LOG_INFO("Application initialized successfully");
    LOG_INFO("  Window: {}x{}", config_.windowWidth, config_.windowHeight);
    LOG_INFO("  Terrain: {}x{} (mips: {})", 
             stats_.terrainWidth, stats_.terrainHeight, stats_.terrainMipLevels);
    LOG_INFO("  Render path: {}", renderPathToString(config_.renderPath));
    LOG_INFO("");
    LOG_INFO("Controls:");
    LOG_INFO("  WASD      - Move camera");
    LOG_INFO("  Mouse     - Look around (click to capture)");
    LOG_INFO("  Shift     - Speed boost / Run");
    LOG_INFO("  E/Space   - Move up / Jump");
    LOG_INFO("  Q/Ctrl    - Move down");
    LOG_INFO("  F1        - Toggle debug overlay");
    LOG_INFO("  F2        - Toggle wireframe mode");
    LOG_INFO("  F3        - Toggle render path");
    LOG_INFO("  F4        - Toggle depth visualization");
    LOG_INFO("  F5        - Toggle normal visualization");
    LOG_INFO("  F6        - Toggle mip level heat map");
    LOG_INFO("  F7        - Toggle benchmark mode");
    LOG_INFO("  F8        - Toggle controller (free-fly/character)");
    LOG_INFO("  F9        - Toggle uncapped/VSync presentation");
    LOG_INFO("  Escape    - Release mouse / Exit");
    LOG_INFO("  Wheel     - Select throwable object");
    LOG_INFO("  Left click- Capture mouse / throw selected object");
    LOG_INFO("  Right click- Throw 128 selected objects");
    LOG_INFO("");

#if defined(VOXY_WASM)
    getDebugOverlay().setVisible(true);
    // On web, default to character controller for better mobile experience
    setControllerMode(ControllerMode::Character);
#endif

    // Handle initial teleportation
    if (config_.initialTeleportIndex.has_value() && camera_) {
        int index = config_.initialTeleportIndex.value();
        if (index >= 0 && static_cast<size_t>(index) < teleportTargets_.size()) {
            const auto& target = teleportTargets_[static_cast<size_t>(index)];
            camera_->setWorldPosition(target.sector, target.position);
            camera_->setYaw(target.yaw);
            camera_->setPitch(target.pitch);
            LOG_INFO("Applied initial teleport to index {} (Pos: {:.2f}, {:.2f}, {:.2f})",
                     index, target.position.x, target.position.y, target.position.z);
        } else {
            LOG_WARN("Invalid initial teleport index: {}", index);
        }
    }

    // If a screenshot tour is requested, prepare it now (after initial teleport).
    startScreenshotTour();

    if (!spawnBenchmarkBodies()) {
        LOG_ERROR("Failed to create the deterministic benchmark body set");
        return false;
    }

    if (config_.benchmarkOnStartup) {
        startBenchmark();
    }

    return true;
}

// Run is now handled by the platform entry point (entry.cpp)
// This method is deprecated and should be removed or made empty if the interface requires it.
// For now, we will leave it empty as the logic is moved to entry.cpp
void Application::run() {
    LOG_WARN("Application::run() is deprecated. Use platform entry point instead.");
}

void Application::requestExit() {
    LOG_INFO("Exit requested");
    shouldExit_ = true;

#if defined(VOXY_NATIVE)
    if (window_) {
        window_->requestClose();
    }
#endif
}

void Application::shutdown() {
    if (!initialized_) {
        return;
    }

    LOG_INFO("Shutting down application...");

    retireBenchmarkSubmissions(true);

    renderGpuReadback_.shutdown();
    if (renderGpuResolveBuffer_) {
        wgpuBufferDestroy(renderGpuResolveBuffer_);
        wgpuBufferRelease(renderGpuResolveBuffer_);
        renderGpuResolveBuffer_ = nullptr;
    }
    if (renderGpuQuerySet_) {
        wgpuQuerySetRelease(renderGpuQuerySet_);
        renderGpuQuerySet_ = nullptr;
    }

// No WASM global state needed in Application anymore

    // Release GPU resources
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
    if (benchmarkTargetView_) {
        wgpuTextureViewRelease(benchmarkTargetView_);
        benchmarkTargetView_ = nullptr;
    }
    if (benchmarkTargetTexture_) {
        wgpuTextureDestroy(benchmarkTargetTexture_);
        wgpuTextureRelease(benchmarkTargetTexture_);
        benchmarkTargetTexture_ = nullptr;
    }

    if (placeholderTerrainView_) {
        wgpuTextureViewRelease(placeholderTerrainView_);
        placeholderTerrainView_ = nullptr;
    }
    if (placeholderTerrainTexture_) {
        wgpuTextureDestroy(placeholderTerrainTexture_);
        wgpuTextureRelease(placeholderTerrainTexture_);
        placeholderTerrainTexture_ = nullptr;
    }

    if (placeholderLightmapView_) {
        wgpuTextureViewRelease(placeholderLightmapView_);
        placeholderLightmapView_ = nullptr;
    }
    if (placeholderLightmapTexture_) {
        wgpuTextureDestroy(placeholderLightmapTexture_);
        wgpuTextureRelease(placeholderLightmapTexture_);
        placeholderLightmapTexture_ = nullptr;
    }

    if (shadowMapView_) {
        wgpuTextureViewRelease(shadowMapView_);
        shadowMapView_ = nullptr;
    }
    if (shadowMapTexture_) {
        wgpuTextureDestroy(shadowMapTexture_);
        wgpuTextureRelease(shadowMapTexture_);
        shadowMapTexture_ = nullptr;
    }

    // Shutdown renderers
    if (primitivePath_) {
        primitivePath_->shutdown();
        primitivePath_.reset();
    }
    if (blitPath_) {
        blitPath_->shutdown();
        blitPath_.reset();
    }
    if (raycastPath_) {
        raycastPath_->shutdown();
        raycastPath_.reset();
    }
    if (trianglePath_) {
        trianglePath_->shutdown();
        trianglePath_.reset();
    }
    if (waterSimulation_) {
        if (physicsWorld_) {
            physicsWorld_->setWaterSurfaceSampler({});
            physicsWorld_->setWaterGpuResources({});
        }
        waterSimulation_->shutdown();
        waterSimulation_.reset();
    }

    if (terrainTextures_) {
        terrainTextures_->shutdown();
        terrainTextures_.reset();
    }

    // Shutdown other subsystems
    characterController_.reset();
    if (physicsWorld_) {
        physicsWorld_->shutdown();
        physicsWorld_.reset();
    }

    // Jolt streams directly from the heightmap, so release it after physics.
    if (heightmap_) {
        heightmap_->release();
        heightmap_.reset();
    }

    freeFlyController_.reset();
    camera_.reset();
    input_.reset();

    if (gpuContext_) {
        gpuContext_->shutdown();
        gpuContext_.reset();
    }

    if (window_) {
        window_->shutdown();
        window_.reset();
    }

#if defined(VOXY_NATIVE)
    Window::terminateGLFW();
#endif

    initialized_ = false;
    LOG_INFO("Application shutdown complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Processing
// ─────────────────────────────────────────────────────────────────────────────

void Application::beginFrame() {
    // Save previous input state BEFORE polling new events
    if (input_) {
        input_->beginFrame();
    }

#if defined(VOXY_NATIVE)
    // Poll events to get fresh input state
    if (window_) {
        window_->pollEvents();
    }
#endif

    // Compute input deltas AFTER events are polled
    if (input_) {
        input_->computeDeltas();
    }
}

void Application::update(float deltaTime) {
    update(deltaTime, deltaTime);
}

void Application::update(float simulationDeltaTime, float frameDeltaTime) {
    // Command-line benchmarks are scripted workloads. Ignoring gameplay input
    // keeps their camera and body count stable even if the window has focus.
    const bool scriptedBenchmark =
        (config_.benchmarkOnStartup || config_.exitAfterBenchmark)
        && isBenchmarkRunning();
    if (!scriptedBenchmark) {
        processInput(simulationDeltaTime);
        handleKeyboardShortcuts();

        // Update camera controller based on active mode
        if (input_) {
            switch (controllerMode_) {
                case ControllerMode::FreeFly:
                    if (freeFlyController_) {
                        freeFlyController_->update(simulationDeltaTime, *input_);
                    }
                    break;
                case ControllerMode::Character:
                    if (characterController_) {
                        characterController_->update(simulationDeltaTime, *input_);
                    }
                    break;
            }
        }
    }

    if (physicsWorld_) {
        physicsWorld_->update(simulationDeltaTime);
        const physics::PhysicsStepStats stepStats =
            physicsWorld_->lastStepStats();
        stats_.physicsSimulationMs = stepStats.simulationMs;
        stats_.physicsWaterMs = stepStats.waterMs;
    } else {
        stats_.physicsSimulationMs = 0.0;
        stats_.physicsWaterMs = 0.0;
    }

    // Custom update callback
    if (updateCallback_) {
        updateCallback_(simulationDeltaTime);
    }

    // Update statistics
    updateStats(frameDeltaTime);
}

void Application::render() {
    stats_.physicsSnapshotMs = 0.0;
    stats_.primitiveCullMs = 0.0;
    stats_.primitivePackingMs = 0.0;
    stats_.primitiveUploadMs = 0.0;
    stats_.primitiveRenderMs = 0.0;
    if (!gpuContext_ || !gpuContext_->isInitialized()) {
        return;
    }
    
    // Skip rendering if window is minimized (zero-size swapchain)
    // Requesting a texture from a zero-sized or unconfigured surface is undefined behavior
#if defined(VOXY_NATIVE)
    if (window_ && (window_->getWidth() == 0 || window_->getHeight() == 0)) {
        return;  // Window is minimized, skip rendering
    }
#endif
    
    // Also check swapchain dimensions
    if (gpuContext_->getSwapchainWidth() == 0 || gpuContext_->getSwapchainHeight() == 0) {
        return;  // Invalid swapchain dimensions
    }

    // Scripted performance runs render the same frame offscreen so window
    // server presentation and occlusion cannot contaminate GPU throughput.
    WGPUTextureView targetView = config_.benchmarkOnStartup
        ? benchmarkTargetView_ : gpuContext_->getCurrentTextureView();
    if (!targetView) {
        return;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext_->getDevice(), &encoderDesc);

    // Update camera uniforms for all renderers
    updateCameraUniforms();

    renderGpuProfilingFrame_ = renderGpuQuerySet_
        && config_.renderPath == RenderPath::Raycast
        && raycastPath_ && raycastPath_->isInitialized()
        && blitPath_ && blitPath_->isInitialized()
        && stats_.frameCount % kRenderGpuProfilingIntervalFrames == 0u;

    // Evolve the authoritative surface before physics samples it. Keeping
    // this outside the raycast path also gives the triangle renderer and GPU
    // physics the same animated ocean instead of a never-updated texture.
    if (config_.waterEnabled && waterSimulation_
        && waterSimulation_->isInitialized()) {
        constexpr uint32_t stage =
            static_cast<uint32_t>(RenderGpuStage::WaterSimulation);
        waterSimulation_->update(
            encoder,
            static_cast<float>(std::fmod(stats_.totalTimeSeconds, 4096.0)),
            renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
            stage * kRenderGpuQueriesPerStage,
            stage * kRenderGpuQueriesPerStage + 1u);
    }

    // GPU physics writes persistent poses into this frame's command stream.
    // CPU backends intentionally no-op here.
    if (physicsWorld_) {
        physicsWorld_->encodeGpuStep(encoder);
    }

    // Render based on active path
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            renderTrianglePath(encoder, targetView);
            break;
        case RenderPath::Raycast:
            renderRaycastPath(encoder, targetView);
            break;
    }

    if (primitivePath_ && primitivePath_->isInitialized() && physicsWorld_) {
        perf::Timer primitiveStageTimer;
        const auto capabilities = physicsWorld_->capabilities();
        size_t objectCount = physicsWorld_->stats().residentBodies;
        if (capabilities.gpuResidentState && capabilities.directRenderView) {
            // Persistent GPU buffers flow straight into culling/rendering.
            // No body snapshot or transform upload occurs on this path.
            primitivePath_->setPhysicsRenderView(physicsWorld_->renderView());
            stats_.physicsSnapshotMs = 0.0;
            stats_.primitiveBodyLockedReadCount = 0;
            stats_.primitiveBodyCachedReadCount = 0;
        } else {
            primitiveStageTimer.start();
            auto bodies = physicsWorld_->dynamicBodies();
            primitiveStageTimer.stop();
            stats_.physicsSnapshotMs = primitiveStageTimer.elapsedMs();
            const auto bodyReadStats = physicsWorld_->lastDynamicBodyReadStats();
            stats_.primitiveBodyLockedReadCount =
                static_cast<uint32_t>(bodyReadStats.lockedBodyCount);
            stats_.primitiveBodyCachedReadCount =
                static_cast<uint32_t>(bodyReadStats.cachedBodyCount);
            objectCount = bodies.size();
            primitivePath_->setCompactPhysicsInstances(bodies);
        }

        std::vector<physics::PhysicsWorld::DynamicBodySnapshot> overlays;
        overlays.reserve(kPrimitiveOverlayHeadroom);
        const auto selectedShape =
            static_cast<physics::PhysicsWorld::ThrowableShape>(selectedThrowable_);
        const float previewAngle = static_cast<float>(
            std::fmod(stats_.totalTimeSeconds * 1.8, 2.0 * std::numbers::pi));
        const float previewDistance = 1.35f;
        const float viewportWidth = static_cast<float>(
            std::max(gpuContext_->getSwapchainWidth(), 1u));
        const float viewportHeight = static_cast<float>(
            std::max(gpuContext_->getSwapchainHeight(), 1u));
        const float halfHeight = std::tan(camera_->fovY() * 0.5f) * previewDistance;
        const float halfWidth = halfHeight * viewportWidth / viewportHeight;
        const float previewScale = std::min(halfWidth, halfHeight) * 0.22f;
        const glm::vec3 previewPlane =
            camera_->position() + camera_->forward() * previewDistance;
        const glm::quat cameraRotation = glm::quat_cast(glm::mat3(
            camera_->right(), camera_->up(), camera_->forward()));
        overlays.push_back({
            selectedShape,
            previewPlane + camera_->right() * (halfWidth * 0.76f)
                         - camera_->up() * (halfHeight * 0.72f),
            glm::angleAxis(previewAngle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))),
            physics::PhysicsWorld::throwableShapeDimensions(selectedShape) * previewScale});
        appendObjectCount(overlays, objectCount, previewPlane, camera_->right(),
                          camera_->up(), cameraRotation, halfWidth, halfHeight);
        stats_.primitiveCullMs = 0.0;
        stats_.primitiveInputCount = static_cast<uint32_t>(objectCount);
        // The exact visible count remains GPU-resident by design.
        stats_.primitiveSubmittedCount = static_cast<uint32_t>(objectCount);
        stats_.primitiveCullRejectionRatio = 0.0f;
        stats_.primitiveCullEvaluated = objectCount != 0;
        stats_.primitiveCullingEnabled = true;
        primitivePath_->setInstances(overlays);
        const auto& cpuTimings = primitivePath_->lastCpuTimings();
        stats_.primitivePackingMs = cpuTimings.packingMs;
        stats_.primitiveUploadMs = cpuTimings.uploadMs;
        const auto& uploadStats = primitivePath_->lastUploadStats();
        const auto& compactUploadStats =
            primitivePath_->lastCompactUploadStats();
        stats_.primitiveInstanceUploadBytes = uploadStats.bytesUploaded
                                            + compactUploadStats.bytesUploaded;
        stats_.primitiveInstanceUploadCalls = uploadStats.writeCalls
                                            + compactUploadStats.writeCalls;
        stats_.primitiveInstanceFullUpload = uploadStats.fullUpload
                                           || compactUploadStats.fullUpload;
        WGPUTextureView objectDepth = getOrCreateDepthView();
        if (objectDepth) {
            primitiveStageTimer.restart();
            primitivePath_->render(
                encoder, targetView, objectDepth, camera_->viewMatrix(),
                camera_->projectionMatrix(), camera_->position(), kSunDirection,
                gpuContext_->getSwapchainWidth(), gpuContext_->getSwapchainHeight(),
                config_.renderPath == RenderPath::Raycast,
                camera_->worldSector(),
                renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
                static_cast<uint32_t>(RenderGpuStage::Primitives)
                    * kRenderGpuQueriesPerStage,
                static_cast<uint32_t>(RenderGpuStage::Primitives)
                    * kRenderGpuQueriesPerStage + 1u);
            primitiveStageTimer.stop();
            stats_.primitiveRenderMs = primitiveStageTimer.elapsedMs();
        }
    }

    if (renderGpuProfilingFrame_) {
        wgpuCommandEncoderResolveQuerySet(
            encoder, renderGpuQuerySet_, 0u, kRenderGpuTimestampCount,
            renderGpuResolveBuffer_, 0u);
        static_cast<void>(renderGpuReadback_.encodeCopy(
            encoder, renderGpuResolveBuffer_, 0u,
            kRenderGpuTimestampCount * sizeof(uint64_t), stats_.frameCount,
            0u, 0u));
    }

    // Submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
#if defined(VOXY_NATIVE)
    if (config_.benchmarkOnStartup) {
        benchmarkSubmissionIndices_.push_back(wgpuQueueSubmitForIndex(
            gpuContext_->getQueue(), 1, &cmdBuffer));
    } else {
        wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);
    }
#else
    wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);
#endif

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Automated screenshots
    if (tourActive_) {
        if (tourFrameCounter_ >= static_cast<uint64_t>(config_.screenshotFrameDelay)) {
            captureScreenshot(tourCurrentPath_);
            tourStep_++;
            scheduleNextTourStep();
        } else {
            tourFrameCounter_++;
        }
    } else if (config_.screenshotPath.has_value() &&
               stats_.frameCount >= static_cast<uint64_t>(config_.screenshotFrameDelay)) {

        captureScreenshot(config_.screenshotPath.value());
        config_.screenshotPath.reset(); // Capture once
        requestExit();
    }
}

void Application::endFrame() {
    if (input_) {
        input_->endFrame();
    }

    if (gpuContext_) {
        if (!config_.benchmarkOnStartup) {
            gpuContext_->present();
        }
#if defined(VOXY_NATIVE)
        if (config_.benchmarkOnStartup) {
            const bool scenarioBoundary = benchmarkRunner_
                && benchmarkRunner_->willCompleteScenarioAfterCurrentFrame();
            retireBenchmarkSubmissions(scenarioBoundary);
        }
#endif
        gpuContext_->tick();
    }

    stats_.frameCount++;
}

void Application::retireBenchmarkSubmissions(bool drain) {
#if defined(VOXY_NATIVE)
    if (!gpuContext_ || benchmarkSubmissionIndices_.empty()) return;
    constexpr size_t kMaximumFramesInFlight = 3u;
    if (drain) {
        const WGPUWrappedSubmissionIndex submission{
            gpuContext_->getQueue(), benchmarkSubmissionIndices_.back()};
        static_cast<void>(wgpuDevicePoll(
            gpuContext_->getDevice(), true, &submission));
        benchmarkSubmissionIndices_.clear();
        return;
    }
    if (benchmarkSubmissionIndices_.size() < kMaximumFramesInFlight) return;
    const WGPUWrappedSubmissionIndex submission{
        gpuContext_->getQueue(), benchmarkSubmissionIndices_.front()};
    static_cast<void>(wgpuDevicePoll(
        gpuContext_->getDevice(), true, &submission));
    benchmarkSubmissionIndices_.pop_front();
#else
    static_cast<void>(drain);
#endif
}

void Application::startScreenshotTour() {
    if (config_.screenshotTourIndices.empty()) {
        return;
    }

    // Ensure output directory exists
    std::error_code ec;
    std::filesystem::create_directories(config_.screenshotTourDir, ec);
    if (ec) {
        LOG_WARN("Failed to create screenshot tour directory '{}': {}", config_.screenshotTourDir.string(), ec.message());
    }

    tourActive_ = true;
    tourStep_ = 0;
    scheduleNextTourStep();
}

void Application::scheduleNextTourStep() {
    if (!tourActive_) {
        return;
    }

    if (tourStep_ >= config_.screenshotTourIndices.size()) {
        // Tour complete
        tourActive_ = false;
        requestExit();
        return;
    }

    int index = config_.screenshotTourIndices[tourStep_];
    if (camera_ && index >= 0 && static_cast<size_t>(index) < teleportTargets_.size()) {
        const auto& target = teleportTargets_[static_cast<size_t>(index)];
        camera_->setWorldPosition(target.sector, target.position);
        camera_->setYaw(target.yaw);
        camera_->setPitch(target.pitch);
        LOG_INFO("Screenshot tour: teleported to index {} (step {})", index, tourStep_);
    } else {
        LOG_WARN("Screenshot tour: invalid teleport index {} (step {}), skipping", index, tourStep_);
        tourStep_++;
        scheduleNextTourStep();
        return;
    }

    tourCurrentPath_ = (config_.screenshotTourDir / ("view_" + std::to_string(tourStep_) + ".png")).string();
    tourFrameCounter_ = 0;
}

void Application::processFrame(float deltaTime) {
    processFrame(deltaTime, deltaTime);
}

void Application::processFrame(float simulationDeltaTime,
                               float frameDeltaTime) {
    // Static frame timer for performance instrumentation
    static perf::FrameTimer frameTimer;
    
    // Begin frame timing
    frameTimer.beginFrame();
    
    beginFrame();
    
    // Update phase
    update(simulationDeltaTime, frameDeltaTime);
    frameTimer.markUpdate();
    
    // Render phase
    render();
    frameTimer.markRender();
    
    // Present phase
    endFrame();
    frameTimer.markPresent();
    
    // End frame timing
    frameTimer.endFrame();
    
    // Update benchmark if running (use frame timer stats)
    if (benchmarkRunner_ && benchmarkRunner_->isRunning()) {
        perf::FrameStats frameStats = frameTimer.getLastFrameStats();
        frameStats.physicsSimulationMs = stats_.physicsSimulationMs;
        frameStats.physicsWaterMs = stats_.physicsWaterMs;
        frameStats.physicsSnapshotMs = stats_.physicsSnapshotMs;
        frameStats.primitiveCullMs = stats_.primitiveCullMs;
        frameStats.primitivePackingMs = stats_.primitivePackingMs;
        frameStats.primitiveUploadMs = stats_.primitiveUploadMs;
        frameStats.primitiveRenderMs = stats_.primitiveRenderMs;
        frameStats.physicsResidentBodies = stats_.physicsResidentBodies;
        frameStats.physicsActiveBodies = stats_.physicsActiveBodies;
        frameStats.physicsActiveBodiesObserved =
            stats_.physicsBackend != physics::BackendType::WebGpuSoft
            || stats_.physics.telemetryTick != 0u;
        const bool stillRunning = benchmarkRunner_->onFrame(frameStats);
        if (!stillRunning && config_.exitAfterBenchmark) {
            requestExit();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Path Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::setRenderPath(RenderPath path) {
    if (config_.renderPath != path) {
        config_.renderPath = path;
        stats_.activeRenderPath = path;
        LOG_INFO("Render path changed to: {}", renderPathToString(path));
    }
}

void Application::toggleRenderPath() {
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            setRenderPath(RenderPath::Raycast);
            break;
        case RenderPath::Raycast:
            setRenderPath(RenderPath::Triangle);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Uncapped FPS Mode
// ─────────────────────────────────────────────────────────────────────────────

void Application::toggleUncappedFPS() {
    uncappedFPS_ = !uncappedFPS_;
    config_.vsync = !uncappedFPS_;
    LOG_INFO("Uncapped FPS mode: {}", uncappedFPS_
        ? "ENABLED (immediate presentation)"
        : "DISABLED (FIFO VSync)");

    // Update GPU context immediately
    if (gpuContext_) {
        gpuContext_->setPresentMode(uncappedFPS_ ? WGPUPresentMode_Immediate : WGPUPresentMode_Fifo);
    }
#if defined(VOXY_WASM)
    // Loop strategy update is handled by the platform entry point (entry.cpp) via isUncappedFPS()
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize Handling
// ─────────────────────────────────────────────────────────────────────────────

void Application::onResize(uint32_t width, uint32_t height) {
    LOG_DEBUG("Application resize: {}x{}", width, height);
    
    // Ignore zero-size (minimized window)
    if (width == 0 || height == 0) {
        return;
    }
    
    // Resize swapchain
    if (gpuContext_) {
        gpuContext_->resizeSwapchain(width, height);
    }

    if (config_.benchmarkOnStartup
        && !createBenchmarkTarget(width, height)) {
        LOG_ERROR("Failed to resize the benchmark render target to {}x{}",
                  width, height);
    }
    
    // Update camera aspect ratio
    if (camera_) {
        camera_->setAspectRatio(width, height);
    }
    
    // Release the blit's framebuffer-sized cache bindings before the ray-caster
    // replaces the borrowed depth/shadow views.
    if (blitPath_) {
        [[maybe_unused]] bool resized = blitPath_->resize(width, height);
    }

    // Resize raycast path output textures
    if (raycastPath_) {
        [[maybe_unused]] bool resized = raycastPath_->resize(width, height);
        // Rebind depth texture after resize
        if (blitPath_) {
            blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
            blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
            blitPath_->setMaterialTexture(raycastPath_->getMaterialOutputView());
            blitPath_->setStaticTerrainTextures(
                raycastPath_->getTerrainDepthCacheView(),
                raycastPath_->getTerrainShadowCacheView());
        }
        if (primitivePath_) {
            primitivePath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
        }
    }
    
    // Invalidate depth buffer for triangle path
    depthWidth_ = 0;
    depthHeight_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::setDebugVisMode(DebugVisMode mode) {
    if (debugVisMode_ != mode) {
        debugVisMode_ = mode;
        LOG_INFO("Debug visualization mode: {}", debugVisModeToString(mode));
        
        // Update blit path with new debug mode
        if (blitPath_ && blitPath_->isInitialized()) {
            blitPath_->setDebugMode(static_cast<uint32_t>(mode));
        }
    }
}

void Application::cycleDebugVisMode() {
    uint32_t nextMode = (static_cast<uint32_t>(debugVisMode_) + 1) % 4;
    setDebugVisMode(static_cast<DebugVisMode>(nextMode));
}

void Application::toggleWireframe() {
    wireframeEnabled_ = !wireframeEnabled_;
    LOG_INFO("Wireframe mode: {}", wireframeEnabled_ ? "enabled" : "disabled");
    
    // Update triangle path wireframe state
    if (trianglePath_ && trianglePath_->isInitialized()) {
        trianglePath_->setWireframe(wireframeEnabled_);
    }
}

void Application::toggleLegoMode() {
    legoMode_ = !legoMode_;
    LOG_INFO("Lego mode: {}", legoMode_ ? "enabled" : "disabled");
    // Updates will be propagated in updateCameraUniforms()
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark Mode
// ─────────────────────────────────────────────────────────────────────────────

bool Application::spawnBenchmarkBodies() {
    const uint32_t bodyCount = config_.benchmarkBodyCount;
    if (bodyCount == 0u) return true;
    if (!physicsWorld_ || !characterController_) return false;

    uint32_t columns = 1u;
    while (uint64_t{columns} * columns < bodyCount) ++columns;
    const uint32_t rows = (bodyCount + columns - 1u) / columns;
    constexpr float spacing = 2.2f;
    const float halfColumns = 0.5f * static_cast<float>(columns - 1u);
    const float halfRows = 0.5f * static_cast<float>(rows - 1u);
    constexpr uint32_t shapeCount = static_cast<uint32_t>(
        physics::ThrowableShape::Count);

    for (uint32_t index = 0u; index < bodyCount; ++index) {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        const float x = (static_cast<float>(column) - halfColumns) * spacing;
        const float z = (static_cast<float>(row) - halfRows) * spacing;
        const float terrainHeight =
            characterController_->sampleTerrainHeight(x, z);

        physics::BodySpawnDesc body;
        body.shape = static_cast<physics::ThrowableShape>(index % shapeCount);
        // Keep every body active for the complete five-scenario benchmark.
        // At 400 Hz the 1,500 frames advance 3.75 simulated seconds; the
        // initial downward velocity plus gravity covers less than 73 metres.
        body.position = {x, terrainHeight + 100.0f, z};
        body.linearVelocity = {
            static_cast<float>(static_cast<int32_t>(index % 7u) - 3) * 0.2f,
            -1.0f,
            static_cast<float>(static_cast<int32_t>(index % 5u) - 2) * 0.2f};
        body.angularVelocity = {0.1f, 0.2f, 0.05f};
        body.dimensions = physics::throwableShapeDimensions(body.shape);
        if (!physicsWorld_->spawnBody(body).valid()) return false;
    }

    LOG_INFO("Spawned {} benchmark bodies 100 m above terrain with linear and angular velocity",
             bodyCount);
    return true;
}

void Application::startBenchmark() {
    if (!benchmarkRunner_) {
        benchmarkRunner_ = std::make_unique<perf::BenchmarkRunner>();
        
        // Set up camera callback for benchmark
        benchmarkRunner_->setCameraCallback([this](const glm::vec3& pos, const glm::vec3& target) {
            if (camera_) {
                camera_->setWorldPosition(glm::ivec3(0), pos);
                camera_->lookAt(target);
            }
        });
    }

    benchmarkRunner_->setPhysicsBackend(
        physicsWorld_
            ? physics::backendTypeName(physicsWorld_->backendType())
            : "none");
    benchmarkRunner_->setExpectedBodyCount(config_.benchmarkBodyCount);
    benchmarkRunner_->setMinimumThroughputFps(config_.benchmarkMinimumFps);
    if (config_.benchmarkFixedDeltaSeconds > 0.0f) {
        LOG_INFO("Benchmark scripted time step: {:.6f} s ({:.1f} Hz)",
                 config_.benchmarkFixedDeltaSeconds,
                 1.0f / config_.benchmarkFixedDeltaSeconds);
    }
    benchmarkRunner_->start();
    LOG_INFO("Benchmark started");
}

void Application::stopBenchmark() {
    if (benchmarkRunner_ && benchmarkRunner_->isRunning()) {
        benchmarkRunner_->stop();
        LOG_INFO("Benchmark stopped");
    }
}

bool Application::isBenchmarkRunning() const noexcept {
    return benchmarkRunner_ && benchmarkRunner_->isRunning();
}

bool Application::benchmarkPassed() const noexcept {
    return benchmarkRunner_ && benchmarkRunner_->passed();
}

void Application::toggleBenchmark() {
    if (isBenchmarkRunning()) {
        stopBenchmark();
    } else {
        startBenchmark();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Controller Mode
// ─────────────────────────────────────────────────────────────────────────────

void Application::setControllerMode(ControllerMode mode) {
    if (controllerMode_ != mode) {
        controllerMode_ = mode;
        stats_.activeController = mode;
        
        // Log current camera position for debugging
        if (camera_) {
            const auto& pos = camera_->position();
            LOG_INFO("Controller mode changed to: {} (camera at {:.1f}, {:.1f}, {:.1f})", 
                     controllerModeToString(mode), pos.x, pos.y, pos.z);
        } else {
            LOG_INFO("Controller mode changed to: {}", controllerModeToString(mode));
        }
        
        // When switching to character mode, log terrain info for debugging
        if (mode == ControllerMode::Character && characterController_ && camera_) {
            characterController_->syncPhysicsPosition();
            const auto& cfg = characterController_->config();
            LOG_INFO("Character config: terrainWidth={:.1f}, terrainHeight={:.1f}, heightScale={:.1f}", 
                     cfg.terrainWidth, cfg.terrainHeight, cfg.heightScale);
            
            const auto& pos = camera_->position();
            float terrainH = characterController_->sampleTerrainHeight(pos.x, pos.z);
            LOG_INFO("Terrain height at camera XZ ({:.1f}, {:.1f}) = {:.1f}", 
                     pos.x, pos.z, terrainH);
        }
    }
}

void Application::toggleControllerMode() {
    switch (controllerMode_) {
        case ControllerMode::FreeFly:
            setControllerMode(ControllerMode::Character);
            break;
        case ControllerMode::Character:
            setControllerMode(ControllerMode::FreeFly);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Application::initWindow() {
#if defined(VOXY_NATIVE)
    LOG_DEBUG("Initializing window...");

    window_ = std::make_unique<Window>();

    WindowConfig windowConfig;
    windowConfig.width = config_.windowWidth;
    windowConfig.height = config_.windowHeight;
    windowConfig.title = config_.windowTitle.c_str();
    windowConfig.fullscreen = config_.fullscreen;
    windowConfig.vsync = config_.vsync;
    windowConfig.resizable = true;

    if (!window_->init(windowConfig)) {
        return false;
    }

    LOG_DEBUG("Window created: {}x{}", window_->getWidth(), window_->getHeight());
    return true;

#elif defined(VOXY_WASM)
    // WASM uses canvas, no window needed
    LOG_DEBUG("WASM build - no window initialization needed");
    return true;
#else
    return false;
#endif
}

bool Application::initGPU() {
    LOG_DEBUG("Initializing GPU context...");

    gpuContext_ = std::make_unique<gpu::Context>();

    gpu::ContextConfig gpuConfig;
    gpuConfig.powerPreference = WGPUPowerPreference_HighPerformance;
    gpuConfig.enableValidation = config_.enableValidation;
    gpuConfig.enableTimestamps = config_.gpuPhysicsStageProfiling
        || config_.gpuRenderStageProfiling;
    gpuConfig.preferredFormat = config_.colorFormat;
    gpuConfig.presentMode = config_.vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;

#if defined(VOXY_NATIVE)
    if (!window_) {
        LOG_ERROR("Window must be initialized before GPU context");
        return false;
    }

    gpuConfig.swapchainWidth = static_cast<uint32_t>(window_->getFramebufferWidth());
    gpuConfig.swapchainHeight = static_cast<uint32_t>(window_->getFramebufferHeight());

    if (!gpuContext_->init(*window_, gpuConfig)) {
        return false;
    }

#elif defined(VOXY_WASM)
    gpuConfig.swapchainWidth = static_cast<uint32_t>(config_.windowWidth);
    gpuConfig.swapchainHeight = static_cast<uint32_t>(config_.windowHeight);

    if (!gpuContext_->initFromCanvas("#voxy-canvas", gpuConfig)) {
        return false;
    }
#endif

    if (config_.benchmarkOnStartup) {
        if (!createBenchmarkTarget(gpuContext_->getSwapchainWidth(),
                                   gpuContext_->getSwapchainHeight())) {
            LOG_ERROR("Failed to create the benchmark offscreen target");
            return false;
        }
    }

    LOG_DEBUG("GPU context initialized");
    return true;
}

bool Application::createBenchmarkTarget(uint32_t width, uint32_t height) {
    if (!gpuContext_ || width == 0u || height == 0u) return false;

    // Releasing the application handles is safe with in-flight submissions:
    // WebGPU command buffers retain the resources they reference.
    if (benchmarkTargetView_) {
        wgpuTextureViewRelease(benchmarkTargetView_);
        benchmarkTargetView_ = nullptr;
    }
    if (benchmarkTargetTexture_) {
        wgpuTextureRelease(benchmarkTargetTexture_);
        benchmarkTargetTexture_ = nullptr;
    }

    gpu::TextureDesc targetDesc = gpu::TextureDesc::renderTarget(
        width, height, gpuContext_->getSwapchainFormat(),
        "benchmark_offscreen_target");
    targetDesc.usage = WGPUTextureUsage_RenderAttachment
                     | WGPUTextureUsage_CopySrc;
    benchmarkTargetTexture_ = gpu::createTexture(
        gpuContext_->getDevice(), targetDesc);
    if (!benchmarkTargetTexture_) return false;
    benchmarkTargetView_ = gpu::createTextureView(benchmarkTargetTexture_);
    return benchmarkTargetTexture_ && benchmarkTargetView_;
}

bool Application::initRenderGpuProfiling() {
    if (!config_.gpuRenderStageProfiling || !gpuContext_
        || !wgpuDeviceHasFeature(
            gpuContext_->getDevice(), WGPUFeatureName_TimestampQuery)) {
        return true;
    }

    WGPUQuerySetDescriptor queryDesc{};
    WGPU_SET_LABEL(queryDesc, "render_stage_timestamps");
    queryDesc.type = WGPUQueryType_Timestamp;
    queryDesc.count = kRenderGpuTimestampCount;
    renderGpuQuerySet_ = wgpuDeviceCreateQuerySet(
        gpuContext_->getDevice(), &queryDesc);
    renderGpuResolveBuffer_ = gpu::createBuffer(
        gpuContext_->getDevice(), gpu::BufferDesc{
            .label = "render_stage_timestamp_resolve",
            .size = kRenderGpuTimestampCount * sizeof(uint64_t),
            .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        });
    if (!renderGpuQuerySet_ || !renderGpuResolveBuffer_
        || !renderGpuReadback_.initialize(
            gpuContext_->getDevice(), 4u,
            kRenderGpuTimestampCount * sizeof(uint64_t))) {
        renderGpuReadback_.shutdown();
        if (renderGpuResolveBuffer_) {
            wgpuBufferDestroy(renderGpuResolveBuffer_);
            wgpuBufferRelease(renderGpuResolveBuffer_);
            renderGpuResolveBuffer_ = nullptr;
        }
        if (renderGpuQuerySet_) {
            wgpuQuerySetRelease(renderGpuQuerySet_);
            renderGpuQuerySet_ = nullptr;
        }
        return false;
    }
    return true;
}

bool Application::initInput() {
    LOG_DEBUG("Initializing input system...");

    input_ = std::make_unique<Input>();

#if defined(VOXY_NATIVE)
    if (window_) {
        input_->attachToWindow(*window_);
    }
#elif defined(VOXY_WASM)
    input_->setupEmscriptenCallbacks("#voxy-canvas");
#endif

    LOG_DEBUG("Input system initialized");
    return true;
}

bool Application::initCamera() {
    LOG_DEBUG("Initializing camera...");

    // Create camera
    CameraConfig camConfig;
    camConfig.fovY = glm::radians(config_.cameraFovDegrees);
    camConfig.nearPlane = config_.cameraNear;
    camConfig.farPlane = config_.cameraFar;

#if defined(VOXY_NATIVE)
    if (window_) {
        camConfig.aspectRatio = static_cast<float>(window_->getFramebufferWidth()) /
                                static_cast<float>(window_->getFramebufferHeight());
    }
#else
    camConfig.aspectRatio = static_cast<float>(config_.windowWidth) /
                            static_cast<float>(config_.windowHeight);
#endif

    const glm::vec3 defaultStart{0.0f, 80.0f, 0.0f};
    glm::vec3 startPos = config_.cameraStartPos;
    if (glm::length(startPos - defaultStart) < 0.001f) {
        const float viewDistance = std::max(200.0f, config_.cellScale * 256.0f);
        const float viewHeight = std::max(80.0f, config_.heightScale * 1.25f);
        startPos = glm::vec3(0.0f, viewHeight, -viewDistance);
    }

    camera_ = std::make_unique<Camera>(startPos, camConfig);

    // The renderer centers the terrain at (0,0,0)
    // So we should look at the origin, not the calculated positive center
    camera_->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

    // Create free-fly camera controller
    FreeFlyConfig flyConfig;
    flyConfig.baseSpeed = config_.cameraMoveSpeed;
    flyConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    flyConfig.boostMultiplier = 5.0f;

    freeFlyController_ = std::make_unique<FreeFlyController>(*camera_, flyConfig);

    physicsWorld_ = std::make_unique<physics::PhysicsWorld>();
    physics::PhysicsInitContext physicsContext;
    physicsContext.requestedBackend = config_.physicsBackend;
    physicsContext.allowCpuFallback = config_.physicsCpuFallback;
    physicsContext.device = gpuContext_->getDevice();
    physicsContext.queue = gpuContext_->getQueue();
    if (config_.physicsBackend == physics::BackendType::WebGpuSoft) {
        physicsContext.maxBodies = config_.gpuPhysicsMaxBodies;
        physicsContext.maxActiveBodies = config_.gpuPhysicsMaxBodies;
        physicsContext.gpu.broadPhaseCellSize =
            config_.gpuPhysicsBroadPhaseCellSize;
        physicsContext.gpu.maximumCatchUpTicks =
            config_.gpuPhysicsMaximumCatchUpTicks;
    }
    physicsContext.gpu.shaderPath =
        (config_.shaderDir / "physics_ballistic.wgsl").string();
    physicsContext.gpu.enableStageProfiling =
        config_.gpuPhysicsStageProfiling;
    // Benchmarks sample every tick. Interactive play samples asynchronously at
    // a low rate so body, solver, and awake/sleeping counts remain useful
    // without paying readback overhead on every frame.
    constexpr uint32_t kInteractiveTelemetryIntervalTicks = 30u;
    const uint32_t diagnosticsInterval = config_.benchmarkBodyCount != 0u
        ? 1u : kInteractiveTelemetryIntervalTicks;
    physicsContext.gpu.stageProfilingIntervalTicks = diagnosticsInterval;
    physicsContext.gpu.enableTelemetryReadback = true;
    physicsContext.gpu.telemetryReadbackIntervalTicks = diagnosticsInterval;
    physicsContext.gpu.stageProfilingTimestampPeriodNanoseconds =
        config_.gpuPhysicsTimestampPeriodNanoseconds;
    physicsContext.enableValidation = config_.enableValidation;
    physicsContext.joltJobSystem = config_.joltJobSystem;
    physicsContext.joltWorkerThreads = config_.joltWorkerThreads;
    physicsContext.box3dWorkerThreads = config_.box3dWorkerThreads;
    if (!physicsWorld_->initialize(physicsContext)) {
        LOG_ERROR("Failed to initialize '{}' physics backend",
                  physics::backendTypeName(config_.physicsBackend));
        return false;
    }
    const char* scheduler = config_.physicsBackend
            == physics::BackendType::JoltLegacy
        ? physics::joltJobSystemModeName(config_.joltJobSystem)
        : "internal";
    LOG_INFO("Physics backend: {} (scheduler {}, concurrency {})",
             physics::backendTypeName(physicsWorld_->backendType()),
             scheduler, physicsWorld_->stats().workerConcurrency);
    physicsWorld_->setWaterPlane(config_.waterHeight, config_.waterEnabled);

    // Create character controller (will be fully initialized after terrain loads)
    CharacterConfig charConfig;
    charConfig.walkSpeed = config_.cameraMoveSpeed;
    charConfig.runSpeed = config_.cameraMoveSpeed * 2.0f;
    charConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    charConfig.heightScale = config_.heightScale;
    charConfig.cellScale = config_.cellScale;
    charConfig.groundOffset = config_.cameraEyeHeight;
    charConfig.collisionHeight = std::max(config_.cameraEyeHeight, 0.82f);
    charConfig.terrainWidth = terrainSampleExtent(config_.heightmapWidth, config_.cellScale);
    charConfig.terrainHeight = terrainSampleExtent(config_.heightmapHeight, config_.cellScale);
    
    // Character controller is created without heightmap first, will be set after terrain init
    characterController_ = std::make_unique<CharacterController>();
    characterController_->attachCamera(*camera_);
    characterController_->setConfig(charConfig);
    characterController_->attachPhysicsWorld(*physicsWorld_);

    LOG_DEBUG("Camera initialized at ({}, {}, {})",
              config_.cameraStartPos.x, config_.cameraStartPos.y, config_.cameraStartPos.z);
    return true;
}

bool Application::initTerrain() {
    LOG_DEBUG("Initializing terrain...");

    heightmap_ = std::make_unique<terrain::Heightmap>();

    if (!config_.heightmapPath.empty()) {
        // Load from file
        auto result = heightmap_->loadFromFile(config_.heightmapPath);
        if (!result) {
            LOG_ERROR("Failed to load heightmap: {}", 
                      terrain::errorToString(result.error()));
            return false;
        }
        LOG_INFO("Loaded heightmap: {}", config_.heightmapPath.string());
        
        // Check if upscaling is needed (e.g. loaded 4k, requested 8k)
        if (heightmap_->getWidth() < config_.heightmapWidth || 
            heightmap_->getHeight() < config_.heightmapHeight) {
            
            LOG_INFO("Upscaling heightmap from {}x{} to {}x{}", 
                     heightmap_->getWidth(), heightmap_->getHeight(),
                     config_.heightmapWidth, config_.heightmapHeight);
                     
            auto resizeResult = heightmap_->resize(config_.heightmapWidth, config_.heightmapHeight);
            if (!resizeResult) {
                LOG_ERROR("Failed to upscale heightmap: {}", 
                          terrain::errorToString(resizeResult.error()));
                return false;
            }
        }
        
        // Resize to power of 2 if needed (required for mip chain generation)
        if (!terrain::isPowerOfTwo(heightmap_->getWidth()) || 
            !terrain::isPowerOfTwo(heightmap_->getHeight())) {
            LOG_INFO("Heightmap dimensions {}x{} are not power of 2, resizing...",
                     heightmap_->getWidth(), heightmap_->getHeight());
            auto resizeResult = heightmap_->resizeToPowerOfTwo();
            if (!resizeResult) {
                LOG_ERROR("Failed to resize heightmap: {}", 
                          terrain::errorToString(resizeResult.error()));
                return false;
            }
        }
    } else {
        // Create procedural wavy heightmap
        uint32_t w = config_.heightmapWidth;
        uint32_t h = config_.heightmapHeight;
        std::vector<uint16_t> data(w * h);
        
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                // Create some sine waves
                float u = static_cast<float>(x) / static_cast<float>(w) * 10.0f;
                float v = static_cast<float>(y) / static_cast<float>(h) * 10.0f;
                float height = 0.5f + 0.2f * std::sin(u) + 0.2f * std::cos(v);
                data[y * w + x] = static_cast<uint16_t>(height * 65535.0f);
            }
        }
        *heightmap_ = terrain::Heightmap::createFromData(std::move(data), w, h);
        LOG_INFO("Created procedural heightmap: {}x{}", 
                 config_.heightmapWidth, config_.heightmapHeight);
    }

    // Upload to GPU with mip chain
    // Note: Use CPU mip generation for better compatibility across drivers
    // (GPU mip generation requires StorageBinding on R16Uint which isn't universally supported)
    auto uploadResult = heightmap_->uploadToGPUWithMips(
        gpuContext_->getDevice(),
        gpuContext_->getQueue(),
        false,  // Use CPU mip generation for compatibility
        config_.shaderDir / "mip_generate.wgsl",
        "terrain_heightmap"
    );

    if (!uploadResult) {
        LOG_ERROR("Failed to upload heightmap to GPU: {}", 
                  terrain::errorToString(uploadResult.error()));
        return false;
    }

    // Update stats
    stats_.terrainWidth = heightmap_->getWidth();
    stats_.terrainHeight = heightmap_->getHeight();
    stats_.terrainMipLevels = heightmap_->getMipLevelCount();
    
    // Bind heightmap to character controller
    if (characterController_) {
        characterController_->setHeightmap(heightmap_.get());
        
        // Update terrain dimensions in character controller config
        CharacterConfig charConfig = characterController_->config();
        charConfig.terrainWidth = terrainSampleExtent(stats_.terrainWidth, config_.cellScale);
        charConfig.terrainHeight = terrainSampleExtent(stats_.terrainHeight, config_.cellScale);
        charConfig.heightScale = config_.heightScale;
        charConfig.cellScale = config_.cellScale;
        charConfig.groundOffset = config_.cameraEyeHeight;
        characterController_->setConfig(charConfig);
    }

    if (physicsWorld_) {
        physicsWorld_->setTerrainGpuResources({
            heightmap_->getTextureView(), heightmap_->getMipLevelCount()});
    }
    if (!physicsWorld_ || !physicsWorld_->setTerrain(
            heightmap_->getData(), heightmap_->getWidth(), heightmap_->getHeight(),
            config_.heightScale, config_.cellScale)) {
        LOG_ERROR("Failed to attach terrain to Jolt Physics");
        return false;
    }

    LOG_DEBUG("Terrain initialized: {}x{} with {} mip levels",
              stats_.terrainWidth, stats_.terrainHeight, stats_.terrainMipLevels);
    return true;
}

bool Application::initRenderers() {
    LOG_DEBUG("Initializing renderers...");

    WGPUDevice device = gpuContext_->getDevice();
    WGPUQueue queue = gpuContext_->getQueue();
    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // The spectral ocean is shared by ray intersection and final shading.
    // Initialize it before either consumer creates its bind group.
    waterSimulation_ = std::make_unique<render::WaterSimulation>();
    if (!waterSimulation_->init(device, queue, config_.shaderDir,
                                heightmap_->getData(), heightmap_->getWidth(),
                                heightmap_->getHeight(), config_.heightScale,
                                config_.cellScale, config_.waterHeight)) {
        LOG_ERROR("Failed to initialize FFT water simulation");
        return false;
    }
    if (physicsWorld_) {
        const float waveStrength = config_.waterWaveStrength;
        if (physicsWorld_->backendType() == physics::BackendType::WebGpuSoft) {
            physicsWorld_->setWaterGpuResources({
                waterSimulation_->getOutputView(),
                waterSimulation_->getSampler(), waveStrength});
        } else {
            physicsWorld_->setWaterSurfaceSampler(
                [simulation = waterSimulation_.get(), waveStrength](
                    glm::vec2 position, float timeSeconds) {
                    const auto sample = simulation->sampleSurface(
                        position, timeSeconds, waveStrength);
                    return physics::PhysicsWorld::WaterSurfaceSample{
                        sample.heightOffset, sample.slope, sample.velocity};
                });
        }
    }

    // Initialize triangle path
    {
        render::TrianglePathConfig triConfig = render::TrianglePathConfig::defaults();
        triConfig.shaderPath = config_.shaderDir / "terrain.wgsl";
        triConfig.colorFormat = config_.colorFormat;
        triConfig.heightScale = config_.heightScale;
        triConfig.cellScale = config_.cellScale;

        trianglePath_ = std::make_unique<render::TrianglePath>();
        if (!trianglePath_->init(device, queue, triConfig)) {
            LOG_ERROR("Failed to initialize triangle path");
            return false;
        }

        // Bind heightmap
        trianglePath_->setHeightmap(
            heightmap_->getTextureView(),
            heightmap_->getWidth(),
            heightmap_->getHeight(),
            heightmap_->getData()
        );
    }

    // Initialize raycast path
    {
        render::RaycastPathConfig rayConfig = render::RaycastPathConfig::defaults();
        rayConfig.shaderPath = config_.shaderDir / "terrain_raycast.wgsl";
        rayConfig.heightScale = config_.heightScale;
        rayConfig.cellScale = config_.cellScale;

        raycastPath_ = std::make_unique<render::RaycastPath>();
        if (!raycastPath_->init(device, queue, width, height, rayConfig)) {
            LOG_ERROR("Failed to initialize raycast path");
            return false;
        }

        // Bind heightmap
        raycastPath_->setHeightmap(
            heightmap_->getTextureView(),
            heightmap_->getWidth(),
            heightmap_->getHeight()
        );
        raycastPath_->setWaterSimulation(waterSimulation_->getOutputView(),
                                         waterSimulation_->getCoastView(),
                                         waterSimulation_->getSampler());

        // Bake the static sun shadow height field. The raycast shader then
        // replaces its per-pixel shadow DDA with a single texture lookup.
        {
            perf::Timer bakeTimer;
            bakeTimer.start();
            terrain::ShadowBakeConfig bakeConfig;
            bakeConfig.lightDir = kSunDirection;
            bakeConfig.heightScale = config_.heightScale;
            bakeConfig.cellScale = config_.cellScale;

            const auto baked = terrain::bakeShadowHeightField(
                heightmap_->getData(), heightmap_->getWidth(),
                heightmap_->getHeight(), bakeConfig);

            if (!baked.data.empty()) {
                gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
                    baked.width, baked.height, WGPUTextureFormat_R16Uint,
                    WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                    "baked_shadow_height");
                shadowMapTexture_ = gpu::createTextureWithData(
                    device, queue, desc,
                    std::as_bytes(std::span<const uint16_t>(baked.data)),
                    baked.width * sizeof(uint16_t));
                if (shadowMapTexture_) {
                    shadowMapView_ = gpu::createTextureView(shadowMapTexture_);
                }
                if (shadowMapView_) {
                    raycastPath_->setShadowMap(shadowMapView_);
                    LOG_INFO("Baked shadow height field: {}x{} ({:.1f} ms)",
                             baked.width, baked.height, bakeTimer.elapsedMs());
                } else {
                    LOG_WARN("Failed to upload baked shadow map; shadows disabled");
                }
            } else {
                LOG_WARN("Shadow bake produced no data; shadows disabled");
            }
        }
    }

    // Initialize terrain textures
    terrainTextures_ = std::make_unique<terrain::TerrainTextures>();
    
    terrain::TerrainTextureConfig textureConfig;
    textureConfig.albedoPath = config_.albedoPath;
    textureConfig.lightmapPath = config_.lightmapPath;
    textureConfig.placeholderWidth = config_.heightmapWidth;
    textureConfig.placeholderHeight = config_.heightmapHeight;

    if (!terrainTextures_->init(device, queue, textureConfig)) {
        LOG_WARN("Failed to initialize terrain textures (using defaults/placeholders)");
    }

    // Pass textures to triangle path
    trianglePath_->setAlbedo(terrainTextures_->getAlbedoView());
    trianglePath_->setLightmap(terrainTextures_->getLightmapView());
    trianglePath_->setSampler(terrainTextures_->getSampler());

    // Initialize blit path
    {
        render::BlitPathConfig blitConfig = render::BlitPathConfig::defaults();
        blitConfig.shaderPath = config_.shaderDir / "ray_blit.wgsl";
        blitConfig.colorFormat = config_.colorFormat;
        blitConfig.heightScale = config_.heightScale;
        blitConfig.cellScale = config_.cellScale;

        blitPath_ = std::make_unique<render::BlitPath>();
        if (!blitPath_->init(device, queue, blitConfig)) {
            LOG_ERROR("Failed to initialize blit path");
            return false;
        }
        if (!blitPath_->resize(raycastPath_->getOutputWidth(),
                               raycastPath_->getOutputHeight())) {
            LOG_ERROR("Failed to create blit background cache");
            return false;
        }

        // Create placeholder terrain texture (white)
        {
            WGPUTextureDescriptor desc = {};
            WGPU_SET_LABEL(desc, "placeholder_terrain");
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {1, 1, 1};
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;

            placeholderTerrainTexture_ = wgpuDeviceCreateTexture(device, &desc);

            uint8_t greenPixel[4] = {50, 160, 50, 255}; // Forest Green
            gpu::CompatImageCopyTexture dstTexture = gpu::makeTextureCopyDest(
                placeholderTerrainTexture_, 0, {0, 0, 0});
            gpu::CompatTextureDataLayout layout = gpu::makeTextureDataLayout(0, 4, 1);

            WGPUExtent3D writeSize = {1, 1, 1};
            wgpuQueueWriteTexture(queue, &dstTexture, greenPixel, 4, &layout, &writeSize);

            WGPUTextureViewDescriptor viewDesc = {};
            viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            placeholderTerrainView_ = wgpuTextureCreateView(placeholderTerrainTexture_, &viewDesc);
        }

        // Create placeholder lightmap texture (white/fully lit)
        {
            WGPUTextureDescriptor desc = {};
            WGPU_SET_LABEL(desc, "placeholder_lightmap");
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {1, 1, 1};
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;

            placeholderLightmapTexture_ = wgpuDeviceCreateTexture(device, &desc);

            uint8_t whitePixel[4] = {255, 255, 255, 255};
            gpu::CompatImageCopyTexture dstTexture = gpu::makeTextureCopyDest(
                placeholderLightmapTexture_, 0, {0, 0, 0});
            gpu::CompatTextureDataLayout layout = gpu::makeTextureDataLayout(0, 4, 1);

            WGPUExtent3D writeSize = {1, 1, 1};
            wgpuQueueWriteTexture(queue, &dstTexture, whitePixel, 4, &layout, &writeSize);

            WGPUTextureViewDescriptor viewDesc = {};
            viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            placeholderLightmapView_ = wgpuTextureCreateView(placeholderLightmapTexture_, &viewDesc);
        }

        // Bind textures to blit path
        blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
        blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
        blitPath_->setMaterialTexture(raycastPath_->getMaterialOutputView());
        blitPath_->setStaticTerrainTextures(
            raycastPath_->getTerrainDepthCacheView(),
            raycastPath_->getTerrainShadowCacheView());
        blitPath_->setWaterCompositeResources(
            heightmap_->getTextureView(), raycastPath_->getShadowMapView(),
            waterSimulation_->getOutputView(), waterSimulation_->getSampler());
        
        // TerrainTextures guarantees valid views after init (either loaded or placeholder)
        blitPath_->setTerrainTexture(terrainTextures_->getAlbedoView());
        blitPath_->setLightmapTexture(terrainTextures_->getLightmapView()); 
        blitPath_->setTerrainSize(heightmap_->getWidth(), heightmap_->getHeight());
    }

    {
        render::PrimitivePathConfig primitiveConfig;
        primitiveConfig.shaderPath = config_.shaderDir / "physics_primitives.wgsl";
        primitiveConfig.colorFormat = config_.colorFormat;
        primitivePath_ = std::make_unique<render::PrimitivePath>();
        if (!primitivePath_->init(device, queue, primitiveConfig)) {
            LOG_ERROR("Failed to initialize physics primitive renderer");
            return false;
        }
        primitivePath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
    }

    LOG_DEBUG("Renderers initialized");
    return true;
}

void Application::setupCallbacks() {
#if defined(VOXY_NATIVE)
    if (!window_) return;

    // Resize callback - use shared onResize() method
    window_->setResizeCallback([this](int width, int height) {
        onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    });

    // Close callback
    window_->setCloseCallback([this]() {
        LOG_DEBUG("Window close requested");
        requestExit();
    });
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering Helpers
// ─────────────────────────────────────────────────────────────────────────────

void Application::pollRenderGpuTimings() {
    auto raw = renderGpuReadback_.poll();
    if (!raw || raw->bytes.size()
        != kRenderGpuTimestampCount * sizeof(uint64_t)) return;

    std::array<uint64_t, kRenderGpuTimestampCount> timestamps{};
    std::memcpy(timestamps.data(), raw->bytes.data(), raw->bytes.size());
    RenderGpuStageTiming timing;
    timing.frame = raw->tick;
    const double tickToMilliseconds =
        config_.gpuPhysicsTimestampPeriodNanoseconds * 1.0e-6;
    for (size_t stage = 0; stage < kRenderGpuStageCount; ++stage) {
        const uint64_t start =
            timestamps[stage * kRenderGpuQueriesPerStage];
        const uint64_t end =
            timestamps[stage * kRenderGpuQueriesPerStage + 1u];
        if (end >= start) {
            timing.milliseconds[stage] = static_cast<double>(
                end - start) * tickToMilliseconds;
        }
    }
    stats_.renderGpuTiming = timing;
    if (renderGpuTimingSampleCount_ == kRenderGpuTimingSampleCapacity) {
        renderGpuTimingSampleHead_ =
            (renderGpuTimingSampleHead_ + 1u)
            % kRenderGpuTimingSampleCapacity;
        --renderGpuTimingSampleCount_;
    }
    const size_t destination =
        (renderGpuTimingSampleHead_ + renderGpuTimingSampleCount_)
        % kRenderGpuTimingSampleCapacity;
    renderGpuTimingSamples_[destination] = timing;
    ++renderGpuTimingSampleCount_;
}

void Application::renderTrianglePath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!trianglePath_ || !trianglePath_->isInitialized()) {
        return;
    }

    WGPUTextureView depthView = getOrCreateDepthView();
    if (!depthView) {
        return;
    }

    trianglePath_->render(encoder, colorView, depthView);
}

void Application::renderRaycastPath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!raycastPath_ || !raycastPath_->isInitialized()) {
        return;
    }
    if (!blitPath_ || !blitPath_->isInitialized()) {
        return;
    }

    // Dispatch ray-cast compute shader
    constexpr uint32_t raycastStage =
        static_cast<uint32_t>(RenderGpuStage::TerrainRaycast);
    raycastPath_->dispatch(
        encoder, renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
        raycastStage * kRenderGpuQueriesPerStage,
        raycastStage * kRenderGpuQueriesPerStage + 1u,
        true);

    if (raycastPath_->isUsingStaticCache()) {
        ++stats_.raycastStaticCacheFrames;
    }
    if (raycastPath_->didRefreshStaticCache()) {
        ++stats_.raycastTerrainCacheRefreshes;
    }

    blitPath_->setStaticCacheState(
        raycastPath_->isUsingStaticCache(),
        raycastPath_->didRefreshStaticCache());
    // Render blit pass
    constexpr uint32_t blitStage =
        static_cast<uint32_t>(RenderGpuStage::LightingBlit);
    blitPath_->render(
        encoder, colorView,
        renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
        blitStage * kRenderGpuQueriesPerStage,
        blitStage * kRenderGpuQueriesPerStage + 1u);
    if (blitPath_->didUseGeometryWaterPath()) {
        ++stats_.geometryWaterFrames;
    }
}

void Application::updateCameraUniforms() {
    if (!camera_) return;

    const auto& cameraView = camera_->viewMatrix();
    const auto& proj = camera_->projectionMatrix();
    const auto& pos = camera_->position();
    const physics::WorldPosition cameraWorld =
        physics::canonicalWorldPosition(
            camera_->worldSector(), glm::dvec3(pos));
    const glm::dvec3 terrainPosition64 =
        physics::worldPositionToAbsolute(cameraWorld);
    const glm::vec3 terrainPosition{
        static_cast<float>(terrainPosition64.x),
        static_cast<float>(terrainPosition64.y),
        static_cast<float>(terrainPosition64.z)};
    // Terrain remains authored in sector-zero coordinates. Keep the camera's
    // stable rotation, but reconstruct its terrain-space translation after the
    // camera local position crosses a sector boundary. Physics primitives use
    // cameraView separately and remain camera-relative.
    const glm::mat4 terrainViewRotation{glm::mat3(cameraView)};
    const glm::mat4 terrainView = terrainViewRotation
        * glm::translate(glm::mat4(1.0f), -terrainPosition);
    const float ambient = config_.ambientIntensity;
    const uint32_t terrainWidth = heightmap_ ? heightmap_->getWidth() : stats_.terrainWidth;
    const uint32_t terrainHeight = heightmap_ ? heightmap_->getHeight() : stats_.terrainHeight;
    const uint32_t lodStep =
        (config_.renderPath == RenderPath::Triangle && trianglePath_) ? trianglePath_->getLODStep() : 1u;
    const glm::vec3 worldLightDir = glm::normalize(kSunDirection);
    constexpr float fogDensity = 0.0001f;

    render::CameraUniforms uniforms;
    uniforms.setTerrain(
        std::max(terrainWidth, 1u),
        std::max(terrainHeight, 1u),
        config_.heightScale,
        config_.cellScale,
        static_cast<float>(lodStep),
        fogDensity
    );
    uniforms.setCamera(terrainView, proj, terrainPosition);
    uniforms.setLightDirection(worldLightDir, terrainView, ambient);
    uniforms.setLegoMode(legoMode_);
    uniforms.setWater(config_.waterEnabled,
                      config_.waterHeight,
                      config_.waterShallowColor,
                      config_.waterDeepColor,
                      config_.waterRoughness,
                      config_.waterWaveStrength,
                      config_.waterReflectionStrength,
                      config_.waterShoreFade);
    // Wrap before fp32 loses the sub-frame precision used by short waves.
    const float waterTime = static_cast<float>(
        std::fmod(stats_.totalTimeSeconds, 4096.0));
    uniforms.setWaterTime(waterTime);
    float cameraSurfaceOffset = 0.0f;
    if (config_.waterEnabled && waterSimulation_ &&
        waterSimulation_->isInitialized()) {
        cameraSurfaceOffset = waterSimulation_->sampleSurface(
            glm::vec2{terrainPosition.x, terrainPosition.z}, waterTime,
            config_.waterWaveStrength).heightOffset;
    }
    uniforms.setCameraWaterSurfaceOffset(cameraSurfaceOffset);

    if (config_.renderPath == RenderPath::Triangle &&
        trianglePath_ && trianglePath_->isInitialized()) {
        trianglePath_->setCameraUniforms(uniforms);
    }

    if (config_.renderPath == RenderPath::Raycast) {
        if (raycastPath_ && raycastPath_->isInitialized()) {
            raycastPath_->setCameraUniforms(uniforms);
        }

        if (blitPath_ && blitPath_->isInitialized()) {
            blitPath_->setCameraUniforms(uniforms);
        }
    }
}

void Application::updateStats(float deltaTime) {
    pollRenderGpuTimings();
    stats_.frameTimeMs = static_cast<double>(deltaTime) * 1000.0;
    stats_.totalTimeSeconds += static_cast<double>(deltaTime);
    stats_.activeRenderPath = config_.renderPath;
    if (physicsWorld_) {
        const physics::PhysicsStats physicsStats = physicsWorld_->stats();
        const uint32_t previousVisibleHigh =
            stats_.physics.visibleBodyUsage.highWater;
        stats_.physics = physicsStats;
        stats_.physics.visibleBodyUsage.current =
            stats_.primitiveSubmittedCount;
        stats_.physics.visibleBodyUsage.capacity =
            physicsStats.bodyCapacity;
        stats_.physics.visibleBodyUsage.highWater = std::max(
            previousVisibleHigh, stats_.primitiveSubmittedCount);
        while (auto timing = physicsWorld_->pollGpuStageTimings()) {
            stats_.physicsGpuTiming = *timing;
            if (physicsGpuTimingSampleCount_
                == kPhysicsGpuTimingSampleCapacity) {
                physicsGpuTimingSampleHead_ =
                    (physicsGpuTimingSampleHead_ + 1u)
                    % kPhysicsGpuTimingSampleCapacity;
                --physicsGpuTimingSampleCount_;
            }
            const size_t destination =
                (physicsGpuTimingSampleHead_ + physicsGpuTimingSampleCount_)
                % kPhysicsGpuTimingSampleCapacity;
            physicsGpuTimingSamples_[destination] = std::move(*timing);
            ++physicsGpuTimingSampleCount_;
        }
        stats_.physicsBackend = physicsStats.backend;
        stats_.physicsResidentBodies = physicsStats.residentBodies;
        stats_.physicsActiveBodies = physicsStats.activeBodies;
        stats_.physicsBodyCapacity = physicsStats.bodyCapacity;
        stats_.physicsEstimatedPersistentBytes =
            physicsStats.estimatedPersistentBytes;
        stats_.physicsScratchBytes = physicsStats.scratchBytes;
    }

    // FPS calculation
    fpsAccumulator_ += static_cast<double>(deltaTime);
    fpsFrameCount_++;

    if (fpsAccumulator_ >= static_cast<double>(config_.fpsLogIntervalSeconds)) {
        stats_.avgFrameTimeMs = (fpsAccumulator_ * 1000.0) / fpsFrameCount_;
        stats_.fps = 1000.0 / stats_.avgFrameTimeMs;

        if (config_.showFPS && !getDebugOverlay().isVisible()) {
            // Only log FPS if debug overlay is not visible
            LOG_DEBUG("FPS: {:.1f} ({:.2f} ms) | Path: {}", 
                      stats_.fps, stats_.avgFrameTimeMs, 
                      renderPathToString(config_.renderPath));
        }

        fpsAccumulator_ = 0.0;
        fpsFrameCount_ = 0;
    }
    
    // Update debug overlay
    DebugOverlayStats overlayStats;
    overlayStats.fps = stats_.fps;
    overlayStats.frameTimeMs = stats_.frameTimeMs;
    overlayStats.avgFrameTimeMs = stats_.avgFrameTimeMs;
    overlayStats.frameCount = stats_.frameCount;
    overlayStats.renderPath = config_.renderPath;
    overlayStats.terrainWidth = stats_.terrainWidth;
    overlayStats.terrainHeight = stats_.terrainHeight;
    overlayStats.terrainMipLevels = stats_.terrainMipLevels;
    overlayStats.physics = stats_.physics;
    overlayStats.physicsGpuTiming = stats_.physicsGpuTiming;
    if (stats_.renderGpuTiming) {
        overlayStats.renderGpuMilliseconds =
            stats_.renderGpuTiming->milliseconds;
    }
    
    if (camera_) {
        overlayStats.cameraPosition = camera_->position();
        overlayStats.cameraYaw = camera_->yaw();
        overlayStats.cameraPitch = camera_->pitch();
    }
    
    // Estimate memory usage (heightmap texture only for now)
    if (heightmap_) {
        // Heightmap is R16Uint (2 bytes per pixel)
        size_t baseSize = static_cast<size_t>(stats_.terrainWidth) * 
                          static_cast<size_t>(stats_.terrainHeight) * 2;
        // Account for mip chain (roughly 1.33x base size)
        overlayStats.estimatedMemoryBytes = static_cast<size_t>(static_cast<double>(baseSize) * 1.33);
    }
    overlayStats.estimatedMemoryBytes +=
        stats_.physics.estimatedPersistentBytes + stats_.physics.scratchBytes;
    
    getDebugOverlay().update(overlayStats);
    getDebugOverlay().render();
}

std::optional<physics::PhysicsGpuStageTiming>
Application::pollPhysicsGpuTimingSample() noexcept {
    if (physicsGpuTimingSampleCount_ == 0u) return std::nullopt;
    physics::PhysicsGpuStageTiming result =
        std::move(physicsGpuTimingSamples_[physicsGpuTimingSampleHead_]);
    physicsGpuTimingSampleHead_ =
        (physicsGpuTimingSampleHead_ + 1u)
        % kPhysicsGpuTimingSampleCapacity;
    --physicsGpuTimingSampleCount_;
    return result;
}

std::optional<RenderGpuStageTiming>
Application::pollRenderGpuTimingSample() noexcept {
    if (renderGpuTimingSampleCount_ == 0u) return std::nullopt;
    RenderGpuStageTiming result =
        renderGpuTimingSamples_[renderGpuTimingSampleHead_];
    renderGpuTimingSampleHead_ =
        (renderGpuTimingSampleHead_ + 1u)
        % kRenderGpuTimingSampleCapacity;
    --renderGpuTimingSampleCount_;
    return result;
}

WGPUTextureView Application::getOrCreateDepthView() {
    if (!gpuContext_) return nullptr;

    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // Check if we need to recreate
    if (depthView_ && depthWidth_ == width && depthHeight_ == height) {
        return depthView_;
    }

    // Release old resources
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }

    // Create depth texture
    WGPUTextureDescriptor desc = {};
    WGPU_SET_LABEL(desc, "depth_texture");
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {width, height, 1};
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    depthTexture_ = wgpuDeviceCreateTexture(gpuContext_->getDevice(), &desc);
    if (!depthTexture_) {
        LOG_ERROR("Failed to create depth texture");
        return nullptr;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    WGPU_SET_LABEL(viewDesc, "depth_view");
    viewDesc.format = WGPUTextureFormat_Depth32Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    depthView_ = wgpuTextureCreateView(depthTexture_, &viewDesc);
    depthWidth_ = width;
    depthHeight_ = height;

    return depthView_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Processing
// ─────────────────────────────────────────────────────────────────────────────

void Application::processInput(float deltaTime) {
    processThrowableInput(deltaTime);
}

void Application::processThrowableInput(float deltaTime) {
    if (!input_ || !camera_ || !physicsWorld_) {
        return;
    }

    const float wheel = input_->scrollDelta();
    if (wheel != 0.0f) {
        if (throwableWheelAccumulator_ * wheel < 0.0f) {
            throwableWheelAccumulator_ = 0.0f;
        }
        throwableWheelAccumulator_ += wheel;
    }

    constexpr float wheelStep = 1.0f;
    if (std::abs(throwableWheelAccumulator_) >= wheelStep) {
        constexpr uint32_t count = static_cast<uint32_t>(
            physics::PhysicsWorld::ThrowableShape::Count);
        if (throwableWheelAccumulator_ > 0.0f) {
            selectedThrowable_ = (selectedThrowable_ + 1u) % count;
        } else {
            selectedThrowable_ = (selectedThrowable_ + count - 1u) % count;
        }
        throwableWheelAccumulator_ = 0.0f;
        const auto shape = static_cast<physics::PhysicsWorld::ThrowableShape>(
            selectedThrowable_);
        LOG_INFO("Selected throwable: {}",
                 physics::PhysicsWorld::throwableShapeName(shape));
    }

    const bool mouseCaptured = input_->isMouseCaptured();
    const bool batchRequested = mouseCaptured
                             && input_->wasMouseButtonPressed(MouseButton::Right);
    const bool firing = mouseCaptured
                     && input_->isMouseButtonDown(MouseButton::Left);
    if (!batchRequested && !firing) {
        throwableCooldown_ = 0.0f;
        return;
    }

    const auto shape = static_cast<physics::PhysicsWorld::ThrowableShape>(
        selectedThrowable_);
    const glm::vec3 direction = glm::normalize(camera_->forward());
    const glm::vec3 origin = camera_->position() + direction * 2.2f;
    constexpr float throwSpeed = 28.0f;
    const glm::vec3 dimensions =
        physics::PhysicsWorld::throwableShapeDimensions(shape);
    const auto throwSelected = [&](const glm::vec3& spawnOrigin,
                                   const glm::vec3& spawnDirection) {
        physics::BodySpawnDesc desc;
        desc.shape = shape;
        desc.position = spawnOrigin;
        desc.sector = camera_->worldSector();
        desc.linearVelocity = spawnDirection * throwSpeed;
        desc.angularVelocity = {3.5f, 5.0f, 2.5f};
        desc.dimensions = dimensions;
        if (physicsWorld_->backendType()
            != physics::BackendType::Box3DReference) {
            return physicsWorld_->spawnBody(desc).valid();
        }
        return physicsWorld_->throwBody(
            shape, spawnOrigin, spawnDirection * throwSpeed);
    };

    if (batchRequested) {
        constexpr uint32_t columns = 16u;
        constexpr uint32_t rows = 8u;
        constexpr uint32_t batchSize = columns * rows;
        const float maximumDimension = std::max(
            dimensions.x, std::max(dimensions.y, dimensions.z));
        const float spacing = maximumDimension * 1.08f + 0.02f;
        const float halfWidth = 0.5f * static_cast<float>(columns - 1u)
                              * spacing + 0.5f * maximumDimension;
        const float halfHeight = 0.5f * static_cast<float>(rows - 1u)
                               * spacing + 0.5f * maximumDimension;
        const float tanHalfFov = std::max(
            std::tan(camera_->fovY() * 0.5f), 1.0e-3f);
        const float verticalDistance = halfHeight / tanHalfFov;
        const float horizontalDistance = halfWidth
            / (tanHalfFov * std::max(camera_->aspectRatio(), 1.0e-3f));
        const uint32_t batchLane =
            (physicsWorld_->stats().residentBodies / batchSize) % 4u;
        const float batchDistance = std::max(
            2.2f, std::max(verticalDistance, horizontalDistance) + 0.5f)
            + static_cast<float>(batchLane) * spacing * 1.5f;
        const glm::vec3 batchCenter =
            camera_->position() + direction * batchDistance;
        const glm::vec3 cameraRight = glm::normalize(camera_->right());
        const glm::vec3 cameraUp = glm::normalize(camera_->up());
        uint32_t thrown = 0;
        for (uint32_t i = 0; i < batchSize; ++i) {
            const uint32_t column = i % columns;
            const uint32_t row = i / columns;
            const float x = (static_cast<float>(column)
                - 0.5f * static_cast<float>(columns - 1u)) * spacing;
            const float y = (static_cast<float>(row)
                - 0.5f * static_cast<float>(rows - 1u)) * spacing;
            const float coneX = x / std::max(halfWidth, 1.0e-3f);
            const float coneY = y / std::max(halfHeight, 1.0e-3f);
            glm::vec3 launchDirection = glm::normalize(
                direction + cameraRight * (coneX * 0.08f)
                          + cameraUp * (coneY * 0.08f));
            glm::vec3 spawnOrigin = batchCenter
                                  + cameraRight * x + cameraUp * y;
            if (characterController_) {
                const glm::ivec3 cameraSector = camera_->worldSector();
                const float worldX = spawnOrigin.x
                    + static_cast<float>(cameraSector.x)
                    * physics::kWorldSectorSize;
                const float worldZ = spawnOrigin.z
                    + static_cast<float>(cameraSector.z)
                    * physics::kWorldSectorSize;
                const float terrainHeight =
                    characterController_->sampleTerrainHeight(
                        worldX, worldZ);
                const float clearance = dimensions.y * 0.5f + 0.05f;
                const float minimumY = terrainHeight
                    - static_cast<float>(cameraSector.y)
                    * physics::kWorldSectorSize
                    + clearance;
                if (spawnOrigin.y < minimumY) {
                    spawnOrigin.y = minimumY;
                    const glm::vec3 terrainNormal = glm::normalize(
                        characterController_->sampleTerrainNormal(
                            worldX, worldZ));
                    const float intoTerrain =
                        glm::dot(launchDirection, terrainNormal);
                    if (intoTerrain < 0.0f) {
                        launchDirection = glm::normalize(
                            launchDirection - terrainNormal * intoTerrain
                            + terrainNormal * 0.1f);
                    }
                }
            }
            thrown += throwSelected(spawnOrigin, launchDirection) ? 1u : 0u;
        }
        LOG_INFO("Threw {} x {}", thrown,
                 physics::PhysicsWorld::throwableShapeName(shape));
    }

    if (!firing) {
        throwableCooldown_ = 0.0f;
        return;
    }

    const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
    throwableCooldown_ -= frameTime;
    constexpr float throwInterval = 1.0f / 100.0f;
    while (throwableCooldown_ <= 0.0f) {
        if (throwableBodyLimit_ != 0u
            && physicsWorld_->stats().residentBodies
                >= throwableBodyLimit_) {
            break;
        }
        if (throwSelected(origin, direction)) {
            LOG_INFO("Threw {}", physics::PhysicsWorld::throwableShapeName(shape));
        }
        throwableCooldown_ += throwInterval;
    }
}

void Application::handleKeyboardShortcuts() {
    if (!input_) return;

    // Escape - release mouse or exit
    if (input_->wasKeyPressed(Key::Escape)) {
        if (input_->isMouseCaptured()) {
            input_->releaseMouse();
        } else {
#if !defined(VOXY_WASM)
            requestExit();
#endif
        }
    }

    // F1 - toggle debug overlay
    if (input_->wasKeyPressed(Key::F1)) {
        getDebugOverlay().toggle();
    }

    // F2 - toggle wireframe mode (triangle path)
    if (input_->wasKeyPressed(Key::F2)) {
        toggleWireframe();
    }

    // F3 - toggle render path
    if (input_->wasKeyPressed(Key::F3)) {
        toggleRenderPath();
    }

    // F4 - toggle depth visualization
    if (input_->wasKeyPressed(Key::F4)) {
        if (debugVisMode_ == DebugVisMode::Depth) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::Depth);
        }
    }

    // F5 - toggle normal visualization
    if (input_->wasKeyPressed(Key::F5)) {
        if (debugVisMode_ == DebugVisMode::Normals) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::Normals);
        }
    }

    // F6 - toggle mip level heat map (raycast path)
    if (input_->wasKeyPressed(Key::F6)) {
        if (debugVisMode_ == DebugVisMode::MipLevels) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::MipLevels);
        }
    }

    // F7 - toggle benchmark mode
    if (input_->wasKeyPressed(Key::F7)) {
        toggleBenchmark();
    }
    
    // F8 - toggle controller mode (free-fly / character)
    if (input_->wasKeyPressed(Key::F8)) {
        toggleControllerMode();
    }

    // F9 - toggle uncapped FPS mode
    if (input_->wasKeyPressed(Key::F9)) {
        toggleUncappedFPS();
    }

    // K - toggle Lego Mode (F10 is reserved by browser)
    if (input_->wasKeyPressed(Key::K)) {
        toggleLegoMode();
    }

    // R - Record camera position
    if (input_->wasKeyPressed(Key::R)) {
        if (camera_) {
            CameraState state;
            state.position = camera_->position();
            state.yaw = camera_->yaw();
            state.pitch = camera_->pitch();
            state.sector = camera_->worldSector();

            recordedPositions_.push_back(state);
            LOG_INFO("Recorded state [{}] : Pos({:.2f}, {:.2f}, {:.2f}) Yaw({:.2f}) Pitch({:.2f})",
                     recordedPositions_.size() - 1, state.position.x, state.position.y, state.position.z,
                     state.yaw, state.pitch);

            LOG_INFO("All recorded positions:");
            LOG_INFO("teleportTargets_ = {{");
            for (const auto& s : recordedPositions_) {
                LOG_INFO("    {{ {{ {:.2f}f, {:.2f}f, {:.2f}f }}, {:.4f}f, {:.4f}f }},",
                         s.position.x, s.position.y, s.position.z, s.yaw, s.pitch);
            }
            LOG_INFO("}};");
        }
    }

    // 1-9 - Teleport
    auto checkTeleport = [&](Key key, size_t index) {
        if (input_->wasKeyPressed(key)) {
            if (index < teleportTargets_.size()) {
                if (camera_) {
                    const auto& target = teleportTargets_[index];
                    camera_->setWorldPosition(target.sector, target.position);
                    camera_->setYaw(target.yaw);
                    camera_->setPitch(target.pitch);
                    LOG_INFO("Teleported to position {}: {:.2f}, {:.2f}, {:.2f}",
                             index + 1, target.position.x, target.position.y, target.position.z);
                }
            } else {
                LOG_WARN("No teleport target for index {} (defined: {})", index + 1, teleportTargets_.size());
            }
        }
    };

    checkTeleport(Key::Num1, 0);
    checkTeleport(Key::Num2, 1);
    checkTeleport(Key::Num3, 2);
    checkTeleport(Key::Num4, 3);
    checkTeleport(Key::Num5, 4);
    checkTeleport(Key::Num6, 5);
    checkTeleport(Key::Num7, 6);
    checkTeleport(Key::Num8, 7);
    checkTeleport(Key::Num9, 8);
}

void Application::captureScreenshot(const std::string& filepath) {
#if defined(VOXY_WASM)
    static_cast<void>(filepath);
    LOG_WARN("Screenshots are not supported on WebAssembly builds");
    return;
#else
    if (!gpuContext_) return;

    LOG_INFO("Capturing screenshot to: {}", filepath);

    WGPUDevice device = gpuContext_->getDevice();
    WGPUQueue queue = gpuContext_->getQueue();
    WGPUTexture sourceTexture = gpuContext_->getCurrentTexture();

    if (!sourceTexture) {
        LOG_ERROR("No current texture to capture");
        return;
    }

    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // Bytes per row must be multiple of 256
    uint32_t bytesPerPixel = 4; // BGRA8
    uint32_t unalignedBytesPerRow = width * bytesPerPixel;
    uint32_t align = 256;
    uint32_t bytesPerRow = (unalignedBytesPerRow + align - 1) & ~(align - 1);
    uint32_t size = bytesPerRow * height;

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = "screenshot_buffer";
    bufferDesc.size = size;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label = "screenshot_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPUImageCopyTexture src = {};
    src.texture = sourceTexture;
    src.origin = {0, 0, 0};

    WGPUImageCopyBuffer dst = {};
    dst.buffer = buffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmdDesc);

#if defined(VOXY_USE_DAWN)
    wgpuQueueSubmit(queue, 1, &cmd);
    WGPUWrappedSubmissionIndex submission = {queue, 0}; // unused for Dawn path
#else
    // Capture submission index so we can explicitly wait for completion.
    WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(queue, 1, &cmd);
    WGPUWrappedSubmissionIndex submission = {queue, submissionIndex};
#endif

    // Map buffer
    bool done = false;
    auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        if (status == WGPUBufferMapAsyncStatus_Success) {
            *static_cast<bool*>(userdata) = true;
        } else {
            LOG_ERROR("Failed to map buffer: status={}", static_cast<int>(status));
            *static_cast<bool*>(userdata) = true; // Unblock but fail
        }
    };

    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size, callback, &done);

    // Wait for mapping
    // Process events to allow callbacks to fire
    // wgpu-native callbacks are typically synchronous, but Dawn requires polling
    constexpr int maxPollAttempts = 1000;
    int pollAttempt = 0;
    while (!done && pollAttempt < maxPollAttempts) {
#if defined(VOXY_USE_DAWN)
        WGPUInstance instance = gpuContext_->getInstance();
        wgpuInstanceProcessEvents(instance);
#else
        // Pump the wgpu-native device, waiting on our submission.
        wgpuDevicePoll(device, /*wait=*/true, &submission);
#endif

        pollAttempt++;
    }
    
    if (pollAttempt >= maxPollAttempts) {
        LOG_ERROR("Buffer mapping timed out after {} poll attempts", maxPollAttempts);
        return;
    }

    // Read data
    const uint8_t* data = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(buffer, 0, size));
    if (!data) {
        LOG_ERROR("Failed to map buffer range");
        return;
    }

    // Convert BGRA to RGBA and remove padding
    std::vector<uint8_t> pngData(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t srcIndex = y * bytesPerRow + x * 4;
            uint32_t dstIndex = (y * width + x) * 4;

            // Swap B and R (assuming BGRA input)
            // Note: Check swapchain format. Usually BGRA8Unorm.
            pngData[dstIndex + 0] = data[srcIndex + 2]; // R
            pngData[dstIndex + 1] = data[srcIndex + 1]; // G
            pngData[dstIndex + 2] = data[srcIndex + 0]; // B
            pngData[dstIndex + 3] = data[srcIndex + 3]; // A
        }
    }

    wgpuBufferUnmap(buffer);
    wgpuBufferRelease(buffer);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    if (stbi_write_png(filepath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pngData.data(), static_cast<int>(width) * 4)) {
        LOG_INFO("Saved screenshot to: {}", filepath);
    } else {
        LOG_ERROR("Failed to save screenshot to: {}", filepath);
    }
#endif
}

} // namespace voxy
