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
#include "terrain/biomes.hpp"
#include "terrain/decorations.hpp"
#include "terrain/textures.hpp"
#include "render/decoration_renderer.hpp"
#include "render/triangle_path.hpp"
#include "render/raycast_path.hpp"
#include "render/blit_path.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <system_error>
#include <unordered_map>
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

    // Initialize teleport targets
    // Paste recorded positions here!
    teleportTargets_ = {
        { { 202.53f, 120.92f, -27.16f }, 1.4578f, -0.0944f },
        { { 202.53f, 120.92f, -27.16f }, -0.2070f, -0.3556f },
        { { 179.01f, 121.64f, -28.72f }, -1.3958f, -0.1900f },
        { { 179.01f, 121.64f, -28.72f }, -2.0606f, -0.0380f },
        { { -885.41f, -59.61f, 170.87f }, -1.3456f, -0.0578f },
        { { -876.38f, -61.58f, 158.43f }, -4.3228f, -0.2176f },
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
    LOG_INFO("  Escape    - Release mouse / Exit");
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
            camera_->setPosition(target.position);
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

    // Shutdown renderers
    if (decorationRenderer_) {
        decorationRenderer_->shutdown();
        decorationRenderer_.reset();
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

    // Release terrain
    if (terrainTextures_) {
        terrainTextures_->shutdown();
        terrainTextures_.reset();
    }
    if (heightmap_) {
        heightmap_->release();
        heightmap_.reset();
    }

    // Shutdown other subsystems
    freeFlyController_.reset();
    characterController_.reset();
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
    // Process input
    processInput(deltaTime);

    // Handle keyboard shortcuts
    handleKeyboardShortcuts();

    // Update camera controller based on active mode
    if (input_) {
        switch (controllerMode_) {
            case ControllerMode::FreeFly:
                if (freeFlyController_) {
                    freeFlyController_->update(deltaTime, *input_);
                }
                break;
            case ControllerMode::Character:
                if (characterController_) {
                    characterController_->update(deltaTime, *input_);
                }
                break;
        }
    }

    // Custom update callback
    if (updateCallback_) {
        updateCallback_(deltaTime);
    }

    // Update statistics
    updateStats(deltaTime);
}

void Application::render() {
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

    // Get current swapchain texture
    WGPUTextureView targetView = gpuContext_->getCurrentTextureView();
    if (!targetView) {
        return;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext_->getDevice(), &encoderDesc);

    // Update camera uniforms for all renderers
    updateCameraUniforms();

    // Render based on active path
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            renderTrianglePath(encoder, targetView);
            break;
        case RenderPath::Raycast:
            renderRaycastPath(encoder, targetView);
            break;
    }

    // Submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);

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
        gpuContext_->present();
        gpuContext_->tick();
    }

    stats_.frameCount++;
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
        camera_->setPosition(target.position);
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

void Application::rebuildTeleportTargetsFromTerrain(const std::vector<terrain::TerrainDecoration>& decorations) {
    if (!heightmap_ || !heightmap_->isLoaded() ||
        heightmap_->getWidth() < 2u || heightmap_->getHeight() < 2u) {
        return;
    }

    const uint32_t width = heightmap_->getWidth();
    const uint32_t height = heightmap_->getHeight();
    const float cellScale = std::max(config_.cellScale, 0.001f);
    const float worldWidth = static_cast<float>(width - 1u) * cellScale;
    const float worldHeight = static_cast<float>(height - 1u) * cellScale;
    const float maxWorldExtent = std::max(worldWidth, worldHeight);
    const float originX = worldWidth * 0.5f;
    const float originZ = worldHeight * 0.5f;

    auto rawToMeters = [&](float raw) {
        const float normalized = raw / 65535.0f;
        return (normalized * 2.0f - 1.0f) * config_.heightScale;
    };

    auto worldToSample = [&](float worldX, float worldZ) {
        const float sampleX = std::clamp((worldX + originX) / cellScale, 0.0f, static_cast<float>(width - 1u));
        const float sampleY = std::clamp((worldZ + originZ) / cellScale, 0.0f, static_cast<float>(height - 1u));
        return glm::vec2(sampleX, sampleY);
    };

    auto sampleWorldHeight = [&](float worldX, float worldZ) {
        const glm::vec2 sample = worldToSample(worldX, worldZ);
        return rawToMeters(heightmap_->sampleBilinear(sample.x, sample.y));
    };

    auto finite = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };

    auto normalized2 = [](glm::vec2 value, glm::vec2 fallback) {
        const float length = glm::length(value);
        if (length < 0.001f) {
            return glm::normalize(fallback);
        }
        return value / length;
    };

    constexpr size_t maxTeleportTargets = 8u;
    std::vector<CameraState> targets;
    targets.reserve(maxTeleportTargets);

    auto addLookAt = [&](glm::vec3 position, glm::vec3 target) {
        if (targets.size() >= maxTeleportTargets || !finite(position) || !finite(target)) {
            return;
        }

        const float ground = sampleWorldHeight(position.x, position.z);
        const float clearance = std::max(config_.cameraEyeHeight + 4.0f, 6.0f);
        position.y = std::max(position.y, ground + clearance);

        glm::vec3 direction = target - position;
        const float distance = glm::length(direction);
        if (distance < 0.001f) {
            return;
        }
        direction /= distance;

        CameraState state;
        state.position = position;
        state.yaw = std::atan2(direction.x, direction.z);
        state.pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
        constexpr float maxPitch = 1.5607964f;
        state.pitch = std::clamp(state.pitch, -maxPitch, maxPitch);
        targets.push_back(state);
    };

    struct Candidate {
        glm::vec3 position{0.0f};
        float score = -std::numeric_limits<float>::infinity();
        bool valid = false;
    };

    terrain::DecorationConfig scanConfig;
    scanConfig.heightSamples = heightmap_->getData();
    scanConfig.width = width;
    scanConfig.height = height;
    scanConfig.heightScale = config_.heightScale;
    scanConfig.cellScale = cellScale;

    auto sampleBiomeAt = [&](float worldX, float worldZ) {
        const glm::vec2 sample = worldToSample(worldX, worldZ);
        const uint32_t sampleX = static_cast<uint32_t>(std::clamp(sample.x,
                                                                  0.0f,
                                                                  static_cast<float>(width - 1u)));
        const uint32_t sampleY = static_cast<uint32_t>(std::clamp(sample.y,
                                                                  0.0f,
                                                                  static_cast<float>(height - 1u)));
        const float heightM = terrain::sampleDecorationHeightMeters(scanConfig, sampleX, sampleY);
        const float slope = terrain::estimateDecorationSlope(scanConfig, sampleX, sampleY);
        return terrain::sampleBiome(terrain::BiomeSampleInput{
            .worldX = worldX,
            .worldZ = worldZ,
            .heightM = heightM,
            .slope = slope,
        });
    };

    Candidate ridge;
    Candidate shore;
    const uint32_t scanStep = std::max(4u, std::max(width, height) / 96u);
    for (uint32_t y = scanStep / 2u; y < height; y += scanStep) {
        for (uint32_t x = scanStep / 2u; x < width; x += scanStep) {
            const float heightM = terrain::sampleDecorationHeightMeters(scanConfig, x, y);
            const float slope = terrain::estimateDecorationSlope(scanConfig, x, y);
            const float worldX = static_cast<float>(x) * cellScale - originX;
            const float worldZ = static_cast<float>(y) * cellScale - originZ;
            const terrain::BiomeSample biome = terrain::sampleBiome(terrain::BiomeSampleInput{
                .worldX = worldX,
                .worldZ = worldZ,
                .heightM = heightM,
                .slope = slope,
            });

            const float edgePenalty = std::max(
                std::abs(worldX) / std::max(originX, 1.0f),
                std::abs(worldZ) / std::max(originZ, 1.0f)
            );
            if (!biome.isWater()) {
                const float score = heightM + slope * 24.0f - edgePenalty * 18.0f;
                if (score > ridge.score) {
                    ridge = Candidate{glm::vec3(worldX, heightM, worldZ), score, true};
                }
            }

            if (biome.water > 0.18f || biome.river > 0.40f) {
                const float waterline = 1.0f - std::clamp(std::abs(heightM) / std::max(config_.heightScale, 1.0f),
                                                          0.0f,
                                                          1.0f);
                const float score = biome.water * 1.30f + biome.river * 1.05f + waterline * 0.25f -
                                    edgePenalty * 0.12f;
                if (score > shore.score) {
                    shore = Candidate{glm::vec3(worldX, heightM, worldZ), score, true};
                }
            }
        }
    }

    const float centerHeight = sampleWorldHeight(0.0f, 0.0f);
    const float overviewDistance = std::clamp(maxWorldExtent * 0.13f, 180.0f, 980.0f);
    const float overviewHeight = std::max(centerHeight + config_.heightScale * 1.2f, 110.0f);

    if (shore.valid && ridge.valid) {
        const glm::vec2 waterSide = normalized2(glm::vec2(shore.position.x - ridge.position.x,
                                                          shore.position.z - ridge.position.z),
                                                glm::vec2(0.25f, -1.0f));
        const float distance = std::clamp(maxWorldExtent * 0.045f, 90.0f, 250.0f);
        const glm::vec3 position(shore.position.x + waterSide.x * distance,
                                 3.0f,
                                 shore.position.z + waterSide.y * distance);
        glm::vec3 target = glm::mix(shore.position, ridge.position, 0.68f);
        target.y = std::max(target.y, ridge.position.y * 0.52f);
        addLookAt(position, target);
    }

    addLookAt(glm::vec3(-overviewDistance * 0.35f, overviewHeight, -overviewDistance),
              glm::vec3(0.0f, centerHeight + config_.heightScale * 0.12f, 0.0f));

    if (ridge.valid) {
        const glm::vec2 outward = normalized2(glm::vec2(ridge.position.x, ridge.position.z),
                                              glm::vec2(-0.7f, -1.0f));
        const float distance = std::clamp(maxWorldExtent * 0.05f, 85.0f, 220.0f);
        const float lift = std::max(config_.heightScale * 0.32f, 38.0f);
        addLookAt(ridge.position + glm::vec3(outward.x * distance, lift, outward.y * distance),
                  ridge.position + glm::vec3(0.0f, 18.0f, 0.0f));
    }

    if (shore.valid) {
        const glm::vec2 acrossWater = normalized2(glm::vec2(-shore.position.z, shore.position.x),
                                                  glm::vec2(1.0f, -0.35f));
        const float distance = std::clamp(maxWorldExtent * 0.035f, 65.0f, 150.0f);
        const size_t before = targets.size();
        addLookAt(shore.position + glm::vec3(acrossWater.x * distance, 6.0f, acrossWater.y * distance),
                  shore.position + glm::vec3(-acrossWater.x * distance * 0.35f, 4.0f,
                                             -acrossWater.y * distance * 0.35f));
        if (targets.size() > before) {
            std::rotate(targets.begin(), targets.end() - 1, targets.end());
        }
    }

    const float treeClusterCell = std::clamp(maxWorldExtent * 0.025f, 28.0f, 54.0f);
    auto treeBinKey = [](int32_t x, int32_t z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u) |
               static_cast<uint32_t>(z);
    };
    auto treeBin = [&](const terrain::TerrainDecoration& tree) {
        return glm::ivec2(static_cast<int32_t>(std::floor(tree.x / treeClusterCell)),
                          static_cast<int32_t>(std::floor(tree.z / treeClusterCell)));
    };

    std::unordered_map<uint64_t, uint32_t> broadleafBins;
    std::unordered_map<uint64_t, uint32_t> pineBins;
    broadleafBins.reserve(decorations.size());
    pineBins.reserve(decorations.size());
    for (const auto& tree : decorations) {
        if (!terrain::isTreeDecoration(tree.kind)) {
            continue;
        }
        const glm::ivec2 bin = treeBin(tree);
        auto& bins = tree.kind == terrain::DecorationKind::PineTree ? pineBins : broadleafBins;
        bins[treeBinKey(bin.x, bin.y)]++;
    }

    auto treeClusterCount = [&](const terrain::TerrainDecoration& tree) {
        const auto& bins = tree.kind == terrain::DecorationKind::PineTree ? pineBins : broadleafBins;
        const glm::ivec2 bin = treeBin(tree);
        uint32_t count = 0;
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const auto it = bins.find(treeBinKey(bin.x + dx, bin.y + dz));
                if (it != bins.end()) {
                    count += it->second;
                }
            }
        }
        return count;
    };

    struct ScenicCameraCandidate {
        glm::vec3 position{0.0f};
        glm::vec3 target{0.0f};
        float score = -std::numeric_limits<float>::infinity();
        bool valid = false;
    };

    ScenicCameraCandidate scenicForest;
    const glm::vec2 scenicDirections[] = {
        {1.0f, 0.0f},
        {-1.0f, 0.0f},
        {0.0f, 1.0f},
        {0.0f, -1.0f},
        {0.72f, 0.72f},
        {-0.72f, 0.72f},
        {0.72f, -0.72f},
        {-0.72f, -0.72f},
    };
    const float scenicDistances[] = {
        std::clamp(maxWorldExtent * 0.036f, 82.0f, 170.0f),
        std::clamp(maxWorldExtent * 0.052f, 120.0f, 245.0f),
        std::clamp(maxWorldExtent * 0.070f, 165.0f, 320.0f),
    };
    for (const auto& tree : decorations) {
        if (!terrain::isTreeDecoration(tree.kind)) {
            continue;
        }

        const float cluster = static_cast<float>(treeClusterCount(tree));
        if (cluster < 5.0f) {
            continue;
        }

        for (glm::vec2 direction : scenicDirections) {
            direction = normalized2(direction, glm::vec2(0.0f, -1.0f));

            for (const float distance : scenicDistances) {
                const glm::vec2 waterPos(tree.x + direction.x * distance,
                                         tree.z + direction.y * distance);
                const terrain::BiomeSample waterBiome = sampleBiomeAt(waterPos.x, waterPos.y);
                if (waterBiome.water < 0.24f || waterBiome.heightM > 4.0f) {
                    continue;
                }
                const float waterScore = waterBiome.water * 1.40f +
                                         waterBiome.river * 0.20f +
                                         (1.0f - std::clamp(std::abs(waterBiome.heightM) / 28.0f,
                                                           0.0f,
                                                           1.0f)) * 0.35f;

                float ridgeBackdrop = 0.0f;
                if (ridge.valid) {
                    const glm::vec2 treeToRidge = normalized2(glm::vec2(ridge.position.x - tree.x,
                                                                        ridge.position.z - tree.z),
                                                              -direction);
                    ridgeBackdrop = std::max(glm::dot(treeToRidge, -direction), 0.0f);
                }

                const float distanceFromCenter = glm::length(glm::vec2(tree.x, tree.z));
                const float score = cluster * 7.0f +
                                    tree.height * 1.8f +
                                    waterScore * 42.0f +
                                    ridgeBackdrop * 34.0f -
                                    distanceFromCenter * 0.0016f;
                if (score <= scenicForest.score) {
                    continue;
                }

                glm::vec3 target(tree.x, tree.y + tree.height * 0.72f, tree.z);
                if (ridge.valid && ridgeBackdrop > 0.25f) {
                    glm::vec3 ridgeTarget = ridge.position;
                    ridgeTarget.y += 18.0f;
                    target = glm::mix(target, ridgeTarget, 0.32f);
                }

                scenicForest = ScenicCameraCandidate{
                    glm::vec3(waterPos.x, 3.0f, waterPos.y),
                    target,
                    score,
                    true,
                };
            }
        }
    }

    if (scenicForest.valid) {
        addLookAt(scenicForest.position, scenicForest.target);
    }

    auto addTreeTarget = [&](terrain::DecorationKind kind) {
        const terrain::TerrainDecoration* best = nullptr;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (const auto& tree : decorations) {
            if (tree.kind != kind) {
                continue;
            }
            const float distanceFromCenter = glm::length(glm::vec2(tree.x, tree.z));
            const float cluster = static_cast<float>(treeClusterCount(tree));
            const float score = cluster * 7.5f + tree.height * 1.4f + tree.radius * 2.0f +
                                tree.y * 0.04f - distanceFromCenter * 0.0025f;
            if (score > bestScore) {
                best = &tree;
                bestScore = score;
            }
        }

        if (!best) {
            return;
        }

        const glm::vec2 outward = normalized2(glm::vec2(best->x, best->z), glm::vec2(0.35f, -1.0f));
        const bool broadCanopy = best->kind == terrain::DecorationKind::JungleTree ||
                                  best->kind == terrain::DecorationKind::AcaciaTree;
        const bool denseConifer = best->kind == terrain::DecorationKind::PineTree ||
                                  best->kind == terrain::DecorationKind::CypressTree;
        const float distance = std::clamp(best->height * (broadCanopy ? 10.0f : 8.5f),
                                          denseConifer ? 54.0f : 44.0f,
                                          broadCanopy ? 130.0f : 104.0f);
        const glm::vec3 canopy(best->x, best->y + best->height * 0.56f, best->z);
        const glm::vec3 position = canopy + glm::vec3(outward.x * distance,
                                                      best->height * (broadCanopy ? 1.18f : 0.98f) + 12.0f,
                                                      outward.y * distance);
        const glm::vec3 target = canopy - glm::vec3(outward.x * distance * 0.45f,
                                                    -best->height * 0.10f,
                                                    outward.y * distance * 0.45f);
        addLookAt(position, target);
    };

    addTreeTarget(terrain::DecorationKind::JungleTree);
    addTreeTarget(terrain::DecorationKind::AcaciaTree);
    addTreeTarget(terrain::DecorationKind::CypressTree);
    addTreeTarget(terrain::DecorationKind::BroadleafTree);
    addTreeTarget(terrain::DecorationKind::PineTree);

    const std::vector<glm::vec2> fallbackDirections = {
        {0.0f, -1.0f},
        {0.9f, -0.35f},
        {-0.75f, 0.65f},
        {0.45f, 0.85f},
    };
    for (glm::vec2 direction : fallbackDirections) {
        if (targets.size() >= maxTeleportTargets) {
            break;
        }
        direction = normalized2(direction, glm::vec2(0.0f, -1.0f));
        const float distance = std::clamp(maxWorldExtent * 0.16f, 190.0f, 760.0f);
        addLookAt(glm::vec3(direction.x * distance,
                            centerHeight + std::max(config_.heightScale * 0.95f, 90.0f),
                            direction.y * distance),
                  glm::vec3(0.0f, centerHeight + config_.heightScale * 0.08f, 0.0f));
    }

    if (targets.size() >= 3u) {
        teleportTargets_ = std::move(targets);
        LOG_INFO("Generated {} terrain-aware teleport targets", teleportTargets_.size());
        for (size_t index = 0; index < teleportTargets_.size(); ++index) {
            const auto& target = teleportTargets_[index];
            LOG_INFO("  target {}: pos({:.2f}, {:.2f}, {:.2f}) yaw {:.3f} pitch {:.3f}",
                     index,
                     target.position.x,
                     target.position.y,
                     target.position.z,
                     target.yaw,
                     target.pitch);
        }
    } else {
        LOG_WARN("Terrain-aware teleport generation produced {} targets; keeping fallback positions",
                 targets.size());
    }
}

void Application::processFrame(float deltaTime) {
    // Static frame timer for performance instrumentation
    static perf::FrameTimer frameTimer;
    
    // Begin frame timing
    frameTimer.beginFrame();
    
    beginFrame();
    
    // Update phase
    update(deltaTime);
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
        const bool stillRunning = benchmarkRunner_->onFrame(frameTimer.getLastFrameStats());
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
#if defined(VOXY_WASM)
    uncappedFPS_ = !uncappedFPS_;
    LOG_INFO("Uncapped FPS mode: {}", uncappedFPS_ ? "ENABLED (VSync Off, Immediate Loop)" : "DISABLED (VSync On, RAF Loop)");

    // Update GPU context immediately
    if (gpuContext_) {
        gpuContext_->setPresentMode(uncappedFPS_ ? WGPUPresentMode_Immediate : WGPUPresentMode_Fifo);
    }
    // Loop strategy update is handled by the platform entry point (entry.cpp) via isUncappedFPS()
#else
    LOG_WARN("Uncapped FPS toggling is currently only implemented for WASM builds.");
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
    
    // Update camera aspect ratio
    if (camera_) {
        camera_->setAspectRatio(width, height);
    }
    
    // Resize raycast path output textures
    if (raycastPath_) {
        [[maybe_unused]] bool resized = raycastPath_->resize(width, height);
        // Rebind depth texture after resize
        if (blitPath_) {
            blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
            blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
        }
        if (decorationRenderer_) {
            decorationRenderer_->setRaycastDepthTexture(raycastPath_->getDepthOutputView());
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

void Application::startBenchmark() {
    if (!benchmarkRunner_) {
        benchmarkRunner_ = std::make_unique<perf::BenchmarkRunner>();
        
        // Set up camera callback for benchmark
        benchmarkRunner_->setCameraCallback([this](const glm::vec3& pos, const glm::vec3& target) {
            if (camera_) {
                camera_->setPosition(pos);
                camera_->lookAt(target);
            }
        });
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

    LOG_DEBUG("GPU context initialized");
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

    // Create character controller (will be fully initialized after terrain loads)
    CharacterConfig charConfig;
    charConfig.walkSpeed = config_.cameraMoveSpeed;
    charConfig.runSpeed = config_.cameraMoveSpeed * 2.0f;
    charConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    charConfig.heightScale = config_.heightScale;
    charConfig.cellScale = config_.cellScale;
    charConfig.groundOffset = config_.cameraEyeHeight;
    charConfig.terrainWidth = static_cast<float>(config_.heightmapWidth) * config_.cellScale;
    charConfig.terrainHeight = static_cast<float>(config_.heightmapHeight) * config_.cellScale;
    
    // Character controller is created without heightmap first, will be set after terrain init
    characterController_ = std::make_unique<CharacterController>();
    characterController_->attachCamera(*camera_);
    characterController_->setConfig(charConfig);

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
        charConfig.terrainWidth = static_cast<float>(stats_.terrainWidth) * config_.cellScale;
        charConfig.terrainHeight = static_cast<float>(stats_.terrainHeight) * config_.cellScale;
        charConfig.heightScale = config_.heightScale;
        charConfig.cellScale = config_.cellScale;
        charConfig.groundOffset = config_.cameraEyeHeight;
        characterController_->setConfig(charConfig);
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
            heightmap_->getHeight()
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
    }

    // Initialize terrain textures
    terrainTextures_ = std::make_unique<terrain::TerrainTextures>();
    
    terrain::TerrainTextureConfig textureConfig;
    textureConfig.albedoPath = config_.albedoPath;
    textureConfig.lightmapPath = config_.lightmapPath;
    textureConfig.placeholderWidth = config_.heightmapWidth;
    textureConfig.placeholderHeight = config_.heightmapHeight;
    textureConfig.heightSamples = heightmap_->getData();
    textureConfig.heightmapWidth = heightmap_->getWidth();
    textureConfig.heightmapHeight = heightmap_->getHeight();
    textureConfig.heightScale = config_.heightScale;
    textureConfig.cellScale = config_.cellScale;
    textureConfig.generatedTextureCacheDir = config_.generatedTextureCacheDir;

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
        
        // TerrainTextures guarantees valid views after init (either loaded or placeholder)
        blitPath_->setTerrainTexture(terrainTextures_->getAlbedoView());
        blitPath_->setLightmapTexture(terrainTextures_->getLightmapView());
        blitPath_->setNormalTexture(terrainTextures_->getNormalView());
        blitPath_->setTerrainSize(heightmap_->getWidth(), heightmap_->getHeight());
    }

    std::vector<terrain::TerrainDecoration> decorations;
    if (config_.enableDecorations) {
        terrain::DecorationConfig decorationConfig;
        decorationConfig.heightSamples = heightmap_->getData();
        decorationConfig.width = heightmap_->getWidth();
        decorationConfig.height = heightmap_->getHeight();
        decorationConfig.heightScale = config_.heightScale;
        decorationConfig.cellScale = config_.cellScale;
        decorationConfig.spacingCells = config_.decorationSpacingCells;
        decorationConfig.maxTrees = config_.maxTreeInstances;
        decorationConfig.maxGroundDecorations = config_.maxTreeInstances / 2u;

        decorations = terrain::generateTerrainDecorations(decorationConfig);
        if (!decorations.empty()) {
            const auto treeCount = std::count_if(
                decorations.begin(),
                decorations.end(),
                [](const terrain::TerrainDecoration& decoration) {
                    return terrain::isTreeDecoration(decoration.kind);
                }
            );
            const size_t groundCount = decorations.size() - static_cast<size_t>(treeCount);
            render::DecorationRendererConfig decorationRendererConfig =
                render::DecorationRendererConfig::defaults();
            decorationRendererConfig.shaderPath = config_.shaderDir / "decorations.wgsl";
            decorationRendererConfig.colorFormat = config_.colorFormat;
            decorationRendererConfig.maxInstances = config_.maxTreeInstances +
                                                    decorationConfig.maxGroundDecorations;
            decorationRendererConfig.maxVisibleInstances = std::min<uint32_t>(
                3200u,
                std::max<uint32_t>(1800u, decorationRendererConfig.maxInstances / 10u)
            );

            decorationRenderer_ = std::make_unique<render::DecorationRenderer>();
            if (decorationRenderer_->init(device, queue, decorationRendererConfig) &&
                decorationRenderer_->uploadDecorations(decorations)) {
                decorationRenderer_->setRaycastDepthTexture(raycastPath_->getDepthOutputView());
                LOG_INFO("Initialized biome decorations with {} tree instances and {} ground details",
                         treeCount,
                         groundCount);
            } else {
                LOG_WARN("Failed to initialize biome decorations");
                decorationRenderer_.reset();
            }
        } else {
            LOG_INFO("Biome decoration generation produced no instances");
        }
    }
    rebuildTeleportTargetsFromTerrain(decorations);

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

void Application::renderTrianglePath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!trianglePath_ || !trianglePath_->isInitialized()) {
        return;
    }

    WGPUTextureView depthView = getOrCreateDepthView();
    if (!depthView) {
        return;
    }

    trianglePath_->render(encoder, colorView, depthView);
    if (decorationRenderer_ && decorationRenderer_->isInitialized()) {
        decorationRenderer_->renderWithDepth(encoder, colorView, depthView);
    }
}

void Application::renderRaycastPath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!raycastPath_ || !raycastPath_->isInitialized()) {
        return;
    }
    if (!blitPath_ || !blitPath_->isInitialized()) {
        return;
    }

    // Dispatch ray-cast compute shader
    raycastPath_->dispatch(encoder);

    // Render blit pass
    blitPath_->render(encoder, colorView);
    if (decorationRenderer_ && decorationRenderer_->isInitialized()) {
        decorationRenderer_->renderRaycast(encoder, colorView);
    }
}

void Application::updateCameraUniforms() {
    if (!camera_) return;

    const auto& view = camera_->viewMatrix();
    const auto& proj = camera_->projectionMatrix();
    const auto& pos = camera_->position();
    const float ambient = config_.ambientIntensity;
    const uint32_t terrainWidth = heightmap_ ? heightmap_->getWidth() : stats_.terrainWidth;
    const uint32_t terrainHeight = heightmap_ ? heightmap_->getHeight() : stats_.terrainHeight;
    const uint32_t lodStep =
        (config_.renderPath == RenderPath::Triangle && trianglePath_) ? trianglePath_->getLODStep() : 1u;
    glm::vec3 worldLightDir = config_.sunDirection;
    if (glm::length(worldLightDir) < 0.001f) {
        worldLightDir = glm::vec3(0.3f, 0.8f, 0.4f);
    }
    worldLightDir = glm::normalize(worldLightDir);
    const float fogDensity = std::max(config_.fogDensity, 0.0f);

    render::CameraUniforms uniforms;
    uniforms.setTerrain(
        std::max(terrainWidth, 1u),
        std::max(terrainHeight, 1u),
        config_.heightScale,
        config_.cellScale,
        static_cast<float>(lodStep),
        fogDensity
    );
    uniforms.setCamera(view, proj, pos);
    uniforms.setLightDirection(worldLightDir, view, ambient);
    uniforms.setLegoMode(legoMode_);
    uniforms.invProjParams.w = static_cast<float>(stats_.totalTimeSeconds);

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

    if (decorationRenderer_ && decorationRenderer_->isInitialized()) {
        decorationRenderer_->setCameraUniforms(uniforms);
    }
}

void Application::updateStats(float deltaTime) {
    stats_.frameTimeMs = static_cast<double>(deltaTime) * 1000.0;
    stats_.totalTimeSeconds += static_cast<double>(deltaTime);
    stats_.activeRenderPath = config_.renderPath;

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
    
    getDebugOverlay().update(overlayStats);
    getDebugOverlay().render();
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

void Application::processInput([[maybe_unused]] float deltaTime) {
    // Input is processed by the camera controller in update()
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

    // F9 - toggle uncapped FPS mode (WASM only)
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
                    camera_->setPosition(target.position);
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
