// ═══════════════════════════════════════════════════════════════════════════════
// entry.cpp (WASM) - Application Entry Point and Exports
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/application.hpp"
#include "core/log.hpp"
#include "core/config.hpp"
#include "engine/platform/input.hpp"

#include <memory>
#include <algorithm>
#include <numeric>
#include <emscripten.h>
#include <emscripten/html5.h>

namespace {
    // Hold the Application instance for the lifetime of the page
    std::unique_ptr<voxy::Application> g_wasmAppInstance;
    voxy::Application* g_app = nullptr;
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
    appConfig.sunDirection = glm::vec3(config.lighting.sunDirection[0],
                                       config.lighting.sunDirection[1],
                                       config.lighting.sunDirection[2]);
    appConfig.fogDensity = config.lighting.fogDensity;
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
    appConfig.enableValidation = false; // Browser handles validation
    appConfig.showFPS = config.debug.showStats;
    appConfig.fpsLogIntervalSeconds = 2.0f;

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

    // Run the main loop (returns immediately in WASM)
    // WASM: set up Emscripten main loop and return
    LOG_INFO("Starting Emscripten main loop...");

    static double lastTime = emscripten_get_now() / 1000.0;
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

        double now = emscripten_get_now() / 1000.0;
        float deltaTime = static_cast<float>(now - lastTime);
        lastTime = now;

        // Clamp delta time
        deltaTime = std::min(deltaTime, 0.1f);

        g_app->processFrame(deltaTime);
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
