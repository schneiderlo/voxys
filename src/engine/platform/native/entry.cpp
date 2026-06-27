// ═══════════════════════════════════════════════════════════════════════════════
// entry.cpp (Native) - Application Entry Point
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/application.hpp"
#include "engine/platform/window.hpp"
#include "core/log.hpp"
#include "core/config.hpp"

#include <memory>
#include <chrono>
#include <algorithm>
#include <numeric>

int main(int argc, char* argv[]) {
    // Initialize logging
    voxy::log::init();

    // Parse command-line arguments and load config
    voxy::config::init(argc, argv);
    const auto& config = voxy::config::get();

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
    appConfig.enableDecorations = config.terrain.enableDecorations;
    appConfig.decorationSpacingCells = static_cast<uint32_t>(std::max(config.terrain.decorationSpacingCells, 1));
    appConfig.maxTreeInstances = static_cast<uint32_t>(std::max(config.terrain.maxTreeInstances, 0));

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
    appConfig.enableValidation = config.debug.enableValidation;
    appConfig.showFPS = config.debug.showStats;
    appConfig.fpsLogIntervalSeconds = 2.0f;

    // Automation settings
    appConfig.benchmarkOnStartup = config.automation.benchmark;
    appConfig.exitAfterBenchmark = config.automation.benchmark;
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
    voxy::Application app;

    if (!app.init(appConfig)) {
        LOG_ERROR("Failed to initialize application");
        voxy::log::shutdown();
        return 1;
    }

    // Run the main loop (blocking)
    LOG_INFO("Starting main loop (native)...");

    using Clock = std::chrono::high_resolution_clock;
    auto lastTime = Clock::now();

    while (!app.shouldExit() && app.getWindow() && !app.getWindow()->shouldClose()) {
        auto now = Clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Clamp delta time to avoid huge jumps
        deltaTime = std::min(deltaTime, 0.1f);

        app.processFrame(deltaTime);
    }

    LOG_INFO("Main loop ended");

    // Cleanup
    app.shutdown();
    voxy::log::shutdown();

    return 0;
}
