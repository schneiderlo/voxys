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

#include <cstdint>
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

namespace voxy {

// Forward declarations
class Camera;
class FreeFlyController;
class CharacterController;

namespace gpu {
    class Context;
}

namespace terrain {
    class Heightmap;
    class TerrainTextures;
    struct TerrainDecoration;
}

namespace render {
    class TrianglePath;
    class RaycastPath;
    class BlitPath;
    class DecorationRenderer;
}

namespace perf {
    class BenchmarkRunner;
}

// ─────────────────────────────────────────────────────────────────────────────
// Application Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Render path selection
enum class RenderPath {
    Triangle,   ///< Baseline triangle mesh terrain
    Raycast     ///< Primary compute ray-caster with blit pass
};

/// Convert RenderPath to string
[[nodiscard]] const char* renderPathToString(RenderPath path) noexcept;

/// Debug visualization mode
enum class DebugVisMode : uint32_t {
    Off = 0,         ///< Normal rendering (no debug overlay)
    Depth = 1,       ///< Depth buffer visualization
    Normals = 2,     ///< Surface normal visualization
    MipLevels = 3,   ///< Mip level heat map (raycast path only)
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
    float ambientIntensity = 0.3f;         ///< Ambient light intensity associated with the sunlight
    bool enableDecorations = true;         ///< Render biome-driven trees and vegetation
    uint32_t decorationSpacingCells = 10;  ///< Approximate tree candidate grid spacing
    uint32_t maxTreeInstances = 22000;     ///< Cap for generated tree instances
    std::filesystem::path generatedTextureCacheDir = "data/generated/texture_cache";

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

    /// Render the current frame.
    void render();

    /// End the frame and present. Call after render().
    void endFrame();

    /// Process a single frame (calls beginFrame, update, render, endFrame).
    /// @param deltaTime Time since last frame in seconds
    void processFrame(float deltaTime);

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

    /// Toggle benchmark mode on/off.
    void toggleBenchmark();

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
    void setupCallbacks();

    // ─────────────────────────────────────────────────────────────────────────
    // Rendering Helpers
    // ─────────────────────────────────────────────────────────────────────────

    void renderTrianglePath(WGPUCommandEncoder encoder, WGPUTextureView colorView);
    void renderRaycastPath(WGPUCommandEncoder encoder, WGPUTextureView colorView);
    void updateCameraUniforms();
    void updateStats(float deltaTime);
    WGPUTextureView getOrCreateDepthView();
    void captureScreenshot(const std::string& filepath);
    void startScreenshotTour();
    void scheduleNextTourStep();
    void rebuildTeleportTargetsFromTerrain(const std::vector<terrain::TerrainDecoration>& decorations);

    // ─────────────────────────────────────────────────────────────────────────
    // Input Processing
    // ─────────────────────────────────────────────────────────────────────────

    void processInput(float deltaTime);
    void handleKeyboardShortcuts();

    // ─────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────

    ApplicationConfig config_;
    ApplicationStats stats_;
    bool initialized_ = false;
    bool shouldExit_ = false;
    
    // Debug visualization state
    DebugVisMode debugVisMode_ = DebugVisMode::Off;
    bool wireframeEnabled_ = false;
    bool legoMode_ = false;
    
    // Controller mode state
    ControllerMode controllerMode_ = ControllerMode::FreeFly;

    // Uncapped FPS state
    bool uncappedFPS_ = false;
    
    // Benchmark mode
    std::unique_ptr<perf::BenchmarkRunner> benchmarkRunner_;

    // Subsystems (order matters for destruction)
    std::unique_ptr<Window> window_;
    std::unique_ptr<gpu::Context> gpuContext_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<FreeFlyController> freeFlyController_;
    std::unique_ptr<CharacterController> characterController_;
    std::unique_ptr<terrain::Heightmap> heightmap_;
    std::unique_ptr<terrain::TerrainTextures> terrainTextures_;

    // Renderers
    std::unique_ptr<render::TrianglePath> trianglePath_;
    std::unique_ptr<render::RaycastPath> raycastPath_;
    std::unique_ptr<render::BlitPath> blitPath_;
    std::unique_ptr<render::DecorationRenderer> decorationRenderer_;

    // Depth buffer for triangle path
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    uint32_t depthWidth_ = 0;
    uint32_t depthHeight_ = 0;

    // Placeholder textures for blit path
    WGPUTexture placeholderTerrainTexture_ = nullptr;
    WGPUTextureView placeholderTerrainView_ = nullptr;
    WGPUTexture placeholderLightmapTexture_ = nullptr;
    WGPUTextureView placeholderLightmapView_ = nullptr;

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
