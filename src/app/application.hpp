// ═══════════════════════════════════════════════════════════════════════════════
// application.hpp - Main Application Shell (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Encapsulates the complete voxy application lifecycle: initialization, main loop,
// and shutdown. Integrates all subsystems including window, GPU, input, camera,
// terrain, and rendering paths.
//
// Usage:
//   Application app;
//   if (!app.init(config)) return 1;
//   app.run();  // Blocks until exit (native) or returns (WASM)
//   app.shutdown();
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

#include "engine/platform/window.hpp"
#include "engine/platform/input.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/physics_types.hpp"
#include "render/primitive_culling.hpp"

namespace voxy {

// Forward declarations
class Camera;
class FreeFlyController;
class CharacterController;

namespace physics {
    class PhysicsWorld;
}

namespace gpu {
    class Context;
}

namespace terrain {
    class Heightmap;
    class TerrainTextures;
}

namespace render {
    class TrianglePath;
    class RaycastPath;
    class BlitPath;
    class WaterSimulation;
    class PrimitivePath;
}

namespace perf {
    class BenchmarkRunner;
    class BrowserJourneyBenchmark;
    enum class BrowserJourneyLayout : uint32_t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Application Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Render path selection
enum class RenderPath {
    Triangle,   ///< Baseline triangle mesh terrain
    Raycast     ///< Primary compute ray-caster with blit pass
};

inline constexpr uint32_t kMaximumBenchmarkBodyCount = 131'072u;

/// Convert RenderPath to string
[[nodiscard]] const char* renderPathToString(RenderPath path) noexcept;

/// Debug visualization mode
enum class DebugVisMode : uint32_t {
    Off = 0,         ///< Normal rendering (no debug overlay)
    Depth = 1,       ///< Depth buffer visualization
    Normals = 2,     ///< Surface normal visualization
    MipLevels = 3,   ///< Mip level heat map (raycast path only)
};

enum class RenderGpuStage : uint32_t {
    WaterSimulation = 0,
    TerrainRaycast,
    LightingBlit,
    Primitives,
    Count,
};

inline constexpr size_t kRenderGpuStageCount =
    static_cast<size_t>(RenderGpuStage::Count);

struct RenderGpuStageTiming {
    uint64_t frame = 0;
    std::array<double, kRenderGpuStageCount> milliseconds{};
};

/// Controller mode selection
enum class ControllerMode {
    FreeFly,    ///< Unrestricted free-fly camera (default)
    Character   ///< Grounded character controller with physics
};

/// Convert ControllerMode to string
[[nodiscard]] const char* controllerModeToString(ControllerMode mode) noexcept;

/// Convert DebugVisMode to string
[[nodiscard]] const char* debugVisModeToString(DebugVisMode mode) noexcept;

/// Runtime-tunable spectral ocean parameters. Values which alter the spectrum
/// are rebuilt on commit; amplitude, choppiness and speed remain uniform-hot.
struct WaterSpectrumSettings {
    float significantWaveHeight = 25.9f;
    float directionDegrees = 57.0f;
    float choppiness = 2.24f;
    float peakEnhancement = 0.65f;
    float windAlignment = 0.32f;
    float animationSpeed = 2.0f;
    glm::vec2 patchLengths = {1949.0f, 326.0f};
    glm::vec2 cascadeAmplitudes = {0.33f, 0.07f};
    float directionalSineScale = 0.68f;
};

/// The renderer state exposed to the browser inspector. This is deliberately
/// separate from startup/asset configuration: every member is safe to edit
/// while the application is running.
struct RendererRuntimeSettings {
    glm::vec3 sunDirection = {0.6040228f, 0.7660444f, 0.2198463f};
    glm::vec3 sunColor = {1.0f, 0.95f, 0.9f};
    float sunIntensity = 1.0f;
    glm::vec3 ambientColor = {0.1f, 0.12f, 0.15f};
    float ambientIntensity = 1.3f;
    glm::vec3 fogColor = {0.36f, 0.58f, 0.64f};
    float fogDensity = 0.0001f;
    float exposure = 1.0f;

    bool waterEnabled = true;
    float waterHeight = -230.0f;
    glm::vec3 waterShallowColor = {0.12f, 0.46f, 0.50f};
    glm::vec3 waterDeepColor = {0.0f, 0.28f, 0.42f};
    float waterRoughness = 0.05f;
    float waterWaveStrength = 1.0f;
    float waterReflectionStrength = 0.42f;
    float waterShoreFade = 30.0f;
    float waterIor = 1.31f;
    float waterDistortion = 0.20f;
    float waterAbsorptionScale = 1.0f;
    float waterScatterStrength = 1.0f;
    float waterFoamSize = 261.0f;
    float waterFoamOpacity = 0.30f;
    float waterFoamCoverage = 0.21f;
    float waterReflectionDistance = 1500.0f;
    WaterSpectrumSettings waterSpectrum{};

    float cameraFovDegrees = 60.0f;
    float cameraNear = 0.1f;
    float cameraFar = 10000.0f;
    float cameraMoveSpeed = 4.0f;
    float cameraMouseSensitivity = 0.002f;
    float cameraEyeHeight = 1.8f;
};

/// Application configuration
struct ApplicationConfig {
    // Window settings
    int windowWidth = 1280;
    int windowHeight = 720;
    std::string windowTitle = "voxy - WebGPU Terrain Renderer";
    bool fullscreen = false;
    bool vsync = true;

    // Rendering settings
    RenderPath renderPath = RenderPath::Raycast;
    float resolutionScale = 1.0f;          ///< Internal resolution scale
    WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;

    // Terrain settings
    std::filesystem::path heightmapPath;   ///< Path to heightmap file (empty = procedural)
    std::filesystem::path albedoPath;      ///< Path to albedo texture (optional)
    std::filesystem::path lightmapPath;    ///< Path to lightmap texture (optional)
    uint32_t heightmapWidth = 256;         ///< Width for RAW or procedural heightmaps
    uint32_t heightmapHeight = 256;        ///< Height for RAW or procedural heightmaps
    float heightScale = 1.0f;             ///< Reduced height for realistic hills
    float cellScale = 1.0f;                ///< World-space size per heightmap cell
    float ambientIntensity = 1.3f;         ///< Ambient light intensity associated with the sunlight
    glm::vec3 sunDirection = {0.6040228f, 0.7660444f, 0.2198463f};
    glm::vec3 sunColor = {1.0f, 0.95f, 0.9f};
    glm::vec3 ambientColor = {0.1f, 0.12f, 0.15f};
    float fogDensity = 0.0001f;
    glm::vec3 fogColor = {0.36f, 0.58f, 0.64f};

    // Water settings
    bool waterEnabled = true;
    float waterHeight = -230.0f;
    glm::vec3 waterShallowColor = {0.12f, 0.46f, 0.50f};
    glm::vec3 waterDeepColor = {0.0f, 0.28f, 0.42f};
    float waterRoughness = 0.05f;
    float waterWaveStrength = 1.0f;
    float waterReflectionStrength = 0.42f;
    float waterShoreFade = 30.0f;
    WaterSpectrumSettings waterSpectrum{};

    // Physics backend and baseline scheduler selection.
    physics::BackendType physicsBackend = physics::BackendType::WebGpuSoft;
    uint32_t gpuPhysicsMaxBodies = 131'072;
    uint32_t gpuPhysicsMaxPairs = 65'536;
    uint32_t gpuPhysicsMaxCandidatePairs = 262'144;
    uint32_t gpuPhysicsSolverWorkgroupSize = 256;
    float gpuPhysicsBroadPhaseCellSize = 4.0f;
    uint32_t gpuPhysicsMaximumCatchUpTicks = 8;
    bool gpuPhysicsStageProfiling = false;
    bool gpuRenderStageProfiling = false;
    double gpuPhysicsTimestampPeriodNanoseconds = 1.0;
    bool physicsCpuFallback = true;
    physics::JoltJobSystemMode joltJobSystem =
        physics::JoltJobSystemMode::ThreadPool;
    uint32_t joltWorkerThreads = 0;
    uint32_t box3dWorkerThreads = 1;

    // Camera settings
    glm::vec3 cameraStartPos = {0.0f, 80.0f, 0.0f}; // Start lower, near center
    float cameraFovDegrees = 60.0f;
    float cameraNear = 0.1f;
    float cameraFar = 10000.0f;
    float cameraMoveSpeed = 4.0f;          // Walking speed (was 50.0)
    float cameraMouseSensitivity = 0.002f;
    float cameraEyeHeight = 1.8f;          // Eye height above ground

    // Debug settings
    bool enableValidation = true;          ///< WebGPU validation layers
    bool showFPS = true;                   ///< Log FPS periodically
    float fpsLogIntervalSeconds = 2.0f;    ///< FPS logging interval

    // Paths
    std::filesystem::path shaderDir = "shaders";
    std::filesystem::path assetDir = "assets";

    // Automation
    bool benchmarkOnStartup = false;
    bool exitAfterBenchmark = false;
    uint32_t benchmarkBodyCount = 0;
    double benchmarkMinimumFps = 0.0;
    float benchmarkFixedDeltaSeconds = 0.0f;
    std::optional<int> initialTeleportIndex;
    std::optional<std::string> screenshotPath;
    int screenshotFrameDelay = 10;
    // Automated tour (single run without relaunch)
    std::vector<int> screenshotTourIndices;
    std::filesystem::path screenshotTourDir = "screenshots";

    /// Create default config
    static ApplicationConfig defaults() { return ApplicationConfig{}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Application Statistics
// ─────────────────────────────────────────────────────────────────────────────

/// Runtime statistics
struct ApplicationStats {
    double fps = 0.0;                     ///< Current frames per second
    double frameTimeMs = 0.0;             ///< Current frame time in milliseconds
    double avgFrameTimeMs = 0.0;          ///< Average frame time over window
    uint64_t frameCount = 0;              ///< Total frames rendered
    double totalTimeSeconds = 0.0;        ///< Total runtime in seconds

    // Terrain stats
    uint32_t terrainWidth = 0;
    uint32_t terrainHeight = 0;
    uint32_t terrainMipLevels = 0;

    // Render path stats
    RenderPath activeRenderPath = RenderPath::Raycast;
    uint32_t primitiveInputCount = 0;
    uint32_t primitiveSubmittedCount = 0;
    float primitiveCullRejectionRatio = 0.0f;
    bool primitiveCullEvaluated = false;
    bool primitiveCullingEnabled = true;
    uint64_t primitiveInstanceUploadBytes = 0;
    uint32_t primitiveInstanceUploadCalls = 0;
    bool primitiveInstanceFullUpload = false;
    uint32_t primitiveBodyLockedReadCount = 0;
    uint32_t primitiveBodyCachedReadCount = 0;
    uint64_t raycastTerrainCacheRefreshes = 0;
    uint64_t raycastStaticCacheFrames = 0;
    uint64_t geometryWaterFrames = 0;

    physics::BackendType physicsBackend = physics::BackendType::JoltLegacy;
    physics::PhysicsStats physics{};
    std::optional<physics::PhysicsGpuStageTiming> physicsGpuTiming;
    std::optional<RenderGpuStageTiming> renderGpuTiming;
    uint32_t physicsResidentBodies = 0;
    uint32_t physicsActiveBodies = 0;
    uint32_t physicsBodyCapacity = 0;
    size_t physicsEstimatedPersistentBytes = 0;
    size_t physicsScratchBytes = 0;
    double physicsSimulationMs = 0.0;
    double physicsWaterMs = 0.0;
    double physicsSnapshotMs = 0.0;
    double primitiveCullMs = 0.0;
    double primitivePackingMs = 0.0;
    double primitiveUploadMs = 0.0;
    double primitiveRenderMs = 0.0;
    
    // Controller stats
    ControllerMode activeController = ControllerMode::FreeFly;
};

// ─────────────────────────────────────────────────════════════════════════────
// Application Class
// ─────────────────────────────────────────────────────────────────────────────

/// Main application shell that manages the complete lifecycle of voxy.
/// Coordinates window, GPU, input, camera, terrain, and rendering.
class Application {
public:
    /// Callback type for custom update logic
    using UpdateCallback = std::function<void(float deltaTime)>;

    Application();
    ~Application();

    // Non-copyable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Non-movable (due to internal references)
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize the application with configuration.
    /// @param config Application configuration
    /// @return true on success
    [[nodiscard]] bool init(const ApplicationConfig& config = ApplicationConfig::defaults());

    /// Run the main loop.
    /// On native: blocks until the window is closed.
    /// On WASM: sets up Emscripten main loop and returns immediately.
    void run();

    /// Request application to exit (sets shouldExit flag).
    void requestExit();

    /// Check if application should exit.
    [[nodiscard]] bool shouldExit() const noexcept { return shouldExit_; }

    /// Shutdown and release all resources.
    void shutdown();

    /// Check if initialized.
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Frame Processing (for custom main loops)
    // ─────────────────────────────────────────────────────────────────────────

    /// Begin a new frame. Call before update() and render().
    void beginFrame();

    /// Update game state for this frame.
    /// @param deltaTime Time since last frame in seconds
    void update(float deltaTime);

    /// Update game state while accounting for a different submitted-frame
    /// interval. This is used by the browser queue pacer: simulation time may
    /// be dropped while the GPU drains, but FPS still reflects wall time.
    /// @param simulationDeltaTime Time advanced by controllers and physics
    /// @param frameDeltaTime Wall time since the previous submitted frame
    void update(float simulationDeltaTime, float frameDeltaTime);

    /// Render the current frame.
    void render();

    /// End the frame and present. Call after render().
    void endFrame();

    /// Process a single frame (calls beginFrame, update, render, endFrame).
    /// @param deltaTime Time since last frame in seconds
    void processFrame(float deltaTime);

    /// Process a frame with separate simulation and submitted-frame timing.
    /// @param simulationDeltaTime Time advanced by controllers and physics
    /// @param frameDeltaTime Wall time since the previous submitted frame
    void processFrame(float simulationDeltaTime, float frameDeltaTime);

    // ─────────────────────────────────────────────────────────────────────────
    // Render Path Control
    // ─────────────────────────────────────────────────────────────────────────

    /// Get the current render path.
    [[nodiscard]] RenderPath getRenderPath() const noexcept { return config_.renderPath; }

    /// Set the render path.
    void setRenderPath(RenderPath path);

    /// Toggle between render paths.
    void toggleRenderPath();

    // ─────────────────────────────────────────────────────────────────────────
    // Debug Visualization Control
    // ─────────────────────────────────────────────────────────────────────────

    /// Get the current debug visualization mode.
    [[nodiscard]] DebugVisMode getDebugVisMode() const noexcept { return debugVisMode_; }

    /// Set the debug visualization mode.
    void setDebugVisMode(DebugVisMode mode);

    /// Cycle through debug visualization modes.
    void cycleDebugVisMode();

    /// Check if wireframe mode is enabled (triangle path only).
    [[nodiscard]] bool isWireframeEnabled() const noexcept { return wireframeEnabled_; }

    /// Toggle wireframe mode (triangle path only).
    void toggleWireframe();

    /// Toggle Lego Mode.
    void toggleLegoMode();

    /// Check if Lego Mode is enabled.
    [[nodiscard]] bool isLegoModeEnabled() const noexcept { return legoMode_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Benchmark Mode
    // ─────────────────────────────────────────────────────────────────────────

    /// Start benchmark mode with default scenarios.
    void startBenchmark();

    /// Stop benchmark mode.
    void stopBenchmark();

    /// Check if benchmark mode is active.
    [[nodiscard]] bool isBenchmarkRunning() const noexcept;

    /// True only after every configured benchmark guardrail has passed.
    [[nodiscard]] bool benchmarkPassed() const noexcept;

    /// Toggle benchmark mode on/off.
    void toggleBenchmark();

    /// Start the production-browser journey. It reuses the real throwable
    /// volley path and records submitted-frame timing inside the application.
    [[nodiscard]] bool startBrowserJourneyBenchmark(
        uint32_t targetBodies,
        uint32_t warmupTicks,
        uint32_t impactTicks,
        uint32_t settleTicks,
        uint32_t bodiesPerVolley,
        uint32_t ticksPerVolley,
        perf::BrowserJourneyLayout layout);

    /// Stable integer state consumed by browser automation.
    [[nodiscard]] int browserJourneyBenchmarkStatus() const noexcept;

    /// Complete summary plus compact raw frame samples.
    [[nodiscard]] std::string browserJourneyBenchmarkJson() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Controller Mode
    // ─────────────────────────────────────────────────────────────────────────

    /// Get the current controller mode.
    [[nodiscard]] ControllerMode getControllerMode() const noexcept { return controllerMode_; }

    /// Set the controller mode.
    void setControllerMode(ControllerMode mode);

    /// Toggle between controller modes.
    void toggleControllerMode();

    // ─────────────────────────────────────────────────────────────────────────
    // Uncapped FPS Mode (for WASM)
    // ─────────────────────────────────────────────────────────────────────────

    /// Toggle uncapped FPS mode (VSync Off + Immediate loop).
    void toggleUncappedFPS();

    /// Set browser loop timing explicitly for deterministic automation.
    void setUncappedFPS(bool enabled) noexcept { uncappedFPS_ = enabled; }

    /// Check if uncapped FPS mode is enabled.
    [[nodiscard]] bool isUncappedFPS() const noexcept { return uncappedFPS_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Resize Handling
    // ─────────────────────────────────────────────────────────────────────────

    /// Handle canvas/window resize.
    /// Updates swapchain, camera aspect ratio, and render target textures.
    /// @param width New width in physical pixels
    /// @param height New height in physical pixels
    void onResize(uint32_t width, uint32_t height);

    // ─────────────────────────────────────────────────────────────────────────
    // Runtime Renderer Inspector
    // ─────────────────────────────────────────────────────────────────────────

    /// Queue a validated browser-facing renderer setting. Cheap settings are
    /// visible on the next frame. Expensive resources rebuild only on commit.
    [[nodiscard]] bool setRendererSetting(std::string_view name, double value,
                                          bool commit);

    /// Read a browser-facing renderer setting. Unknown names return nullopt.
    [[nodiscard]] std::optional<double>
        getRendererSetting(std::string_view name) const noexcept;

    [[nodiscard]] uint64_t getRendererSettingsRevision() const noexcept {
        return rendererSettingsRevision_;
    }
    [[nodiscard]] uint64_t getAppliedRendererSettingsRevision() const noexcept {
        return appliedRendererSettingsRevision_;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks
    // ─────────────────────────────────────────────────────────────────────────

    /// Set custom update callback (called each frame before rendering).
    void setUpdateCallback(UpdateCallback callback) { updateCallback_ = std::move(callback); }

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Get application configuration.
    [[nodiscard]] const ApplicationConfig& getConfig() const noexcept { return config_; }

    /// Get runtime statistics.
    [[nodiscard]] const ApplicationStats& getStats() const noexcept { return stats_; }

    /// Drain one raw GPU physics timing packet retained by updateStats().
    /// A fixed ring preserves every profiled tick without allocating per frame.
    [[nodiscard]] std::optional<physics::PhysicsGpuStageTiming>
        pollPhysicsGpuTimingSample() noexcept;

    [[nodiscard]] std::optional<RenderGpuStageTiming>
        pollRenderGpuTimingSample() noexcept;

    /// Get the window (may be null before init or on WASM).
    [[nodiscard]] Window* getWindow() noexcept { return window_.get(); }
    [[nodiscard]] const Window* getWindow() const noexcept { return window_.get(); }

    /// Get the GPU context.
    [[nodiscard]] gpu::Context* getGPUContext() noexcept { return gpuContext_.get(); }
    [[nodiscard]] const gpu::Context* getGPUContext() const noexcept { return gpuContext_.get(); }

    /// Get the input system.
    [[nodiscard]] Input* getInput() noexcept { return input_.get(); }
    [[nodiscard]] const Input* getInput() const noexcept { return input_.get(); }

    /// Get the camera.
    [[nodiscard]] Camera* getCamera() noexcept { return camera_.get(); }
    [[nodiscard]] const Camera* getCamera() const noexcept { return camera_.get(); }

    /// Get the heightmap.
    [[nodiscard]] terrain::Heightmap* getHeightmap() noexcept { return heightmap_.get(); }
    [[nodiscard]] const terrain::Heightmap* getHeightmap() const noexcept { return heightmap_.get(); }

    /// Get the physics world.
    [[nodiscard]] physics::PhysicsWorld* getPhysicsWorld() noexcept {
        return physicsWorld_.get();
    }
    [[nodiscard]] const physics::PhysicsWorld* getPhysicsWorld() const noexcept {
        return physicsWorld_.get();
    }

    /// Optional absolute cap used by deterministic interactive benchmarks.
    void setThrowableBodyLimit(uint32_t limit) noexcept {
        throwableBodyLimit_ = limit;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Initialization Helpers
    // ─────────────────────────────────────────────────────────────────────────

    bool initWindow();
    bool initGPU();
    bool initInput();
    bool initCamera();
    bool initTerrain();
    bool initRenderers();
    bool initRenderGpuProfiling();
    bool createBenchmarkTarget(uint32_t width, uint32_t height);
    bool spawnBenchmarkBodies();
    void prepareBrowserJourneyCamera();
    void aimBrowserJourneyVolley(uint32_t volleyIndex);
    void prepareBrowserJourneyOverview(uint32_t volleyCount);
    void updateBrowserJourneyBenchmark();
    void retireBenchmarkSubmissions(bool drain);
    void setupCallbacks();

    // ─────────────────────────────────────────────────────────────────────────
    // Rendering Helpers
    // ─────────────────────────────────────────────────────────────────────────

    void renderTrianglePath(WGPUCommandEncoder encoder, WGPUTextureView colorView);
    void renderRaycastPath(WGPUCommandEncoder encoder, WGPUTextureView colorView);
    void pollRenderGpuTimings();
    void updateCameraUniforms();
    void applyRendererSettings();
    void updateWaterPhysicsBindings();
    [[nodiscard]] bool rebuildWaterCoastField();
    [[nodiscard]] bool rebuildSunShadowMap();
    void updateStats(float deltaTime);
    WGPUTextureView getOrCreateDepthView();
    void captureScreenshot(const std::string& filepath);
    void startScreenshotTour();
    void scheduleNextTourStep();

    // ─────────────────────────────────────────────────────────────────────────
    // Input Processing
    // ─────────────────────────────────────────────────────────────────────────

    void processInput(float deltaTime);
    void processThrowableInput(float deltaTime);
    [[nodiscard]] bool spawnThrowable(
        physics::ThrowableShape shape,
        const glm::vec3& origin,
        const glm::vec3& direction,
        const glm::ivec3& sector);
    [[nodiscard]] uint32_t throwThrowableBatch(
        physics::ThrowableShape shape, uint32_t maximumBodies);
    void handleKeyboardShortcuts();

    // ─────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────

    ApplicationConfig config_;
    RendererRuntimeSettings rendererSettings_{};
    uint64_t rendererSettingsRevision_ = 0;
    uint64_t appliedRendererSettingsRevision_ = 0;
    uint32_t rendererSettingsDirty_ = 0;
    float appliedWaterCoastHeight_ = -230.0f;
    glm::vec3 appliedShadowSunDirection_ =
        RendererRuntimeSettings{}.sunDirection;
    ApplicationStats stats_;
    static constexpr size_t kPhysicsGpuTimingSampleCapacity = 64u;
    std::array<physics::PhysicsGpuStageTiming,
               kPhysicsGpuTimingSampleCapacity> physicsGpuTimingSamples_{};
    size_t physicsGpuTimingSampleHead_ = 0u;
    size_t physicsGpuTimingSampleCount_ = 0u;
    static constexpr size_t kRenderGpuTimingSampleCapacity = 64u;
    std::array<RenderGpuStageTiming,
               kRenderGpuTimingSampleCapacity> renderGpuTimingSamples_{};
    size_t renderGpuTimingSampleHead_ = 0u;
    size_t renderGpuTimingSampleCount_ = 0u;
    bool initialized_ = false;
    bool shouldExit_ = false;
    
    // Debug visualization state
    DebugVisMode debugVisMode_ = DebugVisMode::Off;
    bool wireframeEnabled_ = false;
    bool legoMode_ = false;
    
    // Controller mode state
    ControllerMode controllerMode_ = ControllerMode::FreeFly;
    uint32_t selectedThrowable_ = 0;
    uint32_t throwableBodyLimit_ = 0;
    float throwableWheelAccumulator_ = 0.0f;
    float throwableCooldown_ = 0.0f;

    // Uncapped FPS state
#if defined(VOXY_WASM)
    bool uncappedFPS_ = true;
#else
    bool uncappedFPS_ = false;
#endif
    
    // Benchmark mode
    std::unique_ptr<perf::BenchmarkRunner> benchmarkRunner_;
    std::unique_ptr<perf::BrowserJourneyBenchmark> browserJourneyBenchmark_;
    std::deque<uint64_t> benchmarkSubmissionIndices_;

    // Subsystems (order matters for destruction)
    std::unique_ptr<Window> window_;
    std::unique_ptr<gpu::Context> gpuContext_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<FreeFlyController> freeFlyController_;
    std::unique_ptr<physics::PhysicsWorld> physicsWorld_;
    std::unique_ptr<CharacterController> characterController_;
    std::unique_ptr<terrain::Heightmap> heightmap_;
    std::unique_ptr<terrain::TerrainTextures> terrainTextures_;

    // Renderers
    std::unique_ptr<render::WaterSimulation> waterSimulation_;
    std::unique_ptr<render::PrimitivePath> primitivePath_;
    render::PrimitiveCullController primitiveCullController_;
    std::unique_ptr<render::TrianglePath> trianglePath_;
    std::unique_ptr<render::RaycastPath> raycastPath_;
    std::unique_ptr<render::BlitPath> blitPath_;
    WGPUQuerySet renderGpuQuerySet_ = nullptr;
    WGPUBuffer renderGpuResolveBuffer_ = nullptr;
    physics::DebugReadbackRing renderGpuReadback_;
    bool renderGpuProfilingFrame_ = false;

    // Depth buffer for triangle path
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    uint32_t depthWidth_ = 0;
    uint32_t depthHeight_ = 0;
    WGPUTexture benchmarkTargetTexture_ = nullptr;
    WGPUTextureView benchmarkTargetView_ = nullptr;

    // Baked shadow height field for the raycast path (static sun)
    WGPUTexture shadowMapTexture_ = nullptr;
    WGPUTextureView shadowMapView_ = nullptr;

    // Frame timing
    double lastFrameTime_ = 0.0;
    double fpsAccumulator_ = 0.0;
    int fpsFrameCount_ = 0;

    // Callbacks
    UpdateCallback updateCallback_;

    // Teleportation recording
    struct CameraState {
        glm::vec3 position;
        float yaw;
        float pitch;
        glm::ivec3 sector{0};
    };
    std::vector<CameraState> recordedPositions_;

    // Automated screenshot tour state
    bool tourActive_ = false;
    size_t tourStep_ = 0;
    uint64_t tourFrameCounter_ = 0;
    std::string tourCurrentPath_;
    std::vector<CameraState> teleportTargets_;
};

} // namespace voxy
