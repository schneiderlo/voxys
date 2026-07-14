// ═══════════════════════════════════════════════════════════════════════════════
// entry.cpp (WASM) - Application Entry Point and Exports
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/application.hpp"
#include "core/log.hpp"
#include "core/config.hpp"
#include "engine/platform/input.hpp"
#include "gpu/context.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
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
    WGPUDevice g_physicsSelfTestDevice = nullptr;
    WGPUQueue g_physicsSelfTestQueue = nullptr;
    PhysicsSelfTestState g_physicsSelfTest;
    bool g_physicsSelfTestRequested = false;
    bool g_physicsSelfTestRuntimeReady = false;

    constexpr int32_t kPhysicsSelfTestBaseSector = 1'500'000;
    constexpr float kPhysicsSelfTestHeight = 100.0f;

    int physicsSelfTestStatus() noexcept {
        return static_cast<int>(g_physicsSelfTest.status);
    }

    voxy::physics::PhysicsWorld* physicsWorldForSelfTest() noexcept {
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
int voxy_is_initialized() {
    return g_app != nullptr || g_physicsSelfTestRuntimeReady ? 1 : 0;
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
