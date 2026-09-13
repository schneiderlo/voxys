// ═══════════════════════════════════════════════════════════════════════════════
// entry.cpp (Native) - Application Entry Point
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/application.hpp"
#if defined(None)
#undef None
#endif
#include "engine/platform/window.hpp"
#include "core/log.hpp"
#include "core/config.hpp"
#include "generated/wreckwater_build_content.hpp"
#include "engine/platform/native/cove_saves.hpp"
#include "engine/platform/native/inspection_motion.hpp"

#include <memory>
#include <chrono>
#include <algorithm>
#include <numeric>

int main(int argc, char* argv[]) {
    // Initialize logging
    voxy::log::init();

    std::string saveError;
    const auto saveOptions=voxy::NativeCoveSaves::parse(argc,argv,saveError);
    if(!saveOptions){LOG_ERROR("{}",saveError);return 1;}

    // Parse command-line arguments and load config
    // These options belong to the native storage host, not the shared config.
    std::vector<char*> configArguments;
    if(argc>0)configArguments.push_back(argv[0]);
    for(int i=1;i<argc;++i){
        const std::string_view arg=argv[i]?argv[i]:"";
        if(arg=="--expedition-root" || arg=="--expedition-world" || arg=="--expedition-observe"){++i;continue;}
        configArguments.push_back(argv[i]);
    }
    voxy::config::init(static_cast<int>(configArguments.size()),configArguments.data());
    const auto& config = voxy::config::get();
    const auto gameMode = voxy::config::resolveGameMode(config);
    if (!gameMode.ready()) {
        LOG_ERROR("Game mode is {}", voxy::config::gameModeStatusName(gameMode.status));
        return 1;
    }
    const auto wreckwaterStatus =
        voxy::config::validateWreckwaterClientConfig(
            config.wreckwaterClient);
    if (wreckwaterStatus
            != voxy::config::WreckwaterClientConfigStatus::Disabled
        && wreckwaterStatus
            != voxy::config::WreckwaterClientConfigStatus::Ready) {
        LOG_ERROR(
            "WRECKWATER client bootstrap is {}. Supply every "
            "server/port/peer/key/session identity field or none.",
            voxy::config::wreckwaterClientConfigStatusName(
                wreckwaterStatus));
        voxy::log::shutdown();
        return 1;
    }

    // Configure the application from loaded config file
    voxy::ApplicationConfig appConfig;
    if(saveOptions->root)appConfig.salvageDesignLibraryRoot=std::filesystem::path(*saveOptions->root)/"Designs";
    appConfig.motoEnabled = gameMode.mode == voxy::config::GameMode::Ridgebreak;
    appConfig.legoTerrainEnabled = gameMode.legoTerrain();
    appConfig.salvagePreviewEnabled = gameMode.mode == voxy::config::GameMode::Salvage;
    appConfig.adventureEnabled = gameMode.mode == voxy::config::GameMode::Adventure;
    appConfig.salvageAssetFixtureRegistry = config.game.assetFixtureRegistry;
    appConfig.salvageAssetFixtureCatalog = config.game.assetFixtureCatalog;
    appConfig.salvageAssetFixtureGuides = config.game.assetFixtureGuides;
    appConfig.salvageAssetFixtureFilteredLighting = config.game.assetFixtureLighting == "filtered";
    appConfig.salvageAssetFixtureWaterAnchor = config.game.assetFixtureAnchor == "water";
    appConfig.salvageAssetFixtureLod = config.game.assetFixtureLod == "near" ? 1
        : config.game.assetFixtureLod == "middle" ? 2 : config.game.assetFixtureLod == "far" ? 3 : 0;

    // Window settings
    appConfig.windowWidth = config.window.width;
    appConfig.windowHeight = config.window.height;
    appConfig.windowTitle = config.window.title.empty()
                          ? "voxy - WebGPU Terrain Renderer"
                          : config.window.title;
    appConfig.fullscreen = config.window.fullscreen;
    appConfig.vsync = config.render.vsync && !config.automation.benchmark;

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
    appConfig.gpuPhysicsMaxBodies = static_cast<uint32_t>(
        std::max(config.physics.gpuMaxBodies, 2));
    appConfig.gpuPhysicsBroadPhaseCellSize =
        config.physics.broadPhaseCellSize;
    appConfig.physicsCpuFallback = config.physics.allowCpuFallback;
    appConfig.joltJobSystem = voxy::physics::joltJobSystemModeFromName(
        config.physics.joltJobSystem);
    appConfig.joltWorkerThreads = static_cast<uint32_t>(
        std::max(config.physics.joltWorkerThreads, 0));
    appConfig.box3dWorkerThreads = static_cast<uint32_t>(
        std::max(config.physics.box3dWorkerThreads, 1));

    // The playable LEGO crop must not inherit the full landscape's 8K target.
    appConfig.heightmapWidth = gameMode.terrainSizeHint();
    appConfig.heightmapHeight = appConfig.heightmapWidth;

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
    if (wreckwaterStatus
        == voxy::config::WreckwaterClientConfigStatus::Ready) {
        appConfig.wreckwaterClient =
            voxy::WreckwaterApplicationClientConfig{
                .serverAddress =
                    config.wreckwaterClient.server,
                .serverPort = config.wreckwaterClient.port,
                .peerId = config.wreckwaterClient.peerId,
                .authenticationKey =
                    config.wreckwaterClient.authenticationKey,
                .expectedContentDigest =
                    voxy::build_content::
                        kWreckwaterAuthorityContentDigest,
                .sessionId =
                    config.wreckwaterClient.sessionId,
                .matchId = config.wreckwaterClient.matchId,
                .worldId = config.wreckwaterClient.worldId,
                .worldEpoch =
                    config.wreckwaterClient.worldEpoch,
                .authorityEpoch =
                    config.wreckwaterClient.authorityEpoch,
            };
    }

    // Debug settings
    appConfig.enableValidation = config.debug.enableValidation;
    appConfig.showFPS = config.debug.showStats;
    appConfig.fpsLogIntervalSeconds = 2.0f;
    // Automation settings
    appConfig.benchmarkOnStartup = config.automation.benchmark;
    appConfig.exitAfterBenchmark = config.automation.benchmark;
    appConfig.benchmarkBodyCount = static_cast<uint32_t>(
        std::max(config.automation.benchmarkBodies, 0));
    appConfig.benchmarkMinimumFps = static_cast<double>(
        std::max(config.automation.benchmarkMinimumFps, 0.0f));
    if (config.automation.benchmarkFixedHz > 0.0f) {
        appConfig.benchmarkFixedDeltaSeconds =
            1.0f / config.automation.benchmarkFixedHz;
    }
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
    std::string motionError;
    auto motion=voxy::InspectionMotion::create(config,motionError);
    if (!motionError.empty()) {
        LOG_ERROR("Invalid inspection capture: {}",motionError);
        voxy::log::shutdown();return 1;
    }
    auto app=std::make_unique<voxy::Application>();
    std::unique_ptr<voxy::NativeCoveSaves> saves;
    if(appConfig.salvagePreviewEnabled && appConfig.salvageAssetFixtureWaterAnchor){
        saves=voxy::NativeCoveSaves::create(*saveOptions,saveError);
        if(!saves || !saves->prepare(*app,saveError)){
            LOG_ERROR("Expedition startup: {}",saveError);return 1;
        }
    }else if(saveOptions->world || saveOptions->root || saveOptions->observation){
        LOG_ERROR("Expedition options require --config salvage_cove.cfg");return 1;
    }

    using Clock = std::chrono::steady_clock;
    bool startupPassed=true,benchmarkPassed=true;
    for (;;) {
        if(!app->init(appConfig)) {
            LOG_ERROR("Failed to initialize application");startupPassed=false;break;
        }
        if(saves&&!saves->initialized(*app,saveError)) {
            LOG_ERROR("Expedition startup: {}",saveError);startupPassed=false;break;
        }
        if(motion) {
            app->setUpdateCallback([&](float){motion->update(*app);});
            app->setCaptureCallback([&]{motion->capture(*app);});
        }
        LOG_INFO("Starting main loop (native)...");
        auto lastTime=Clock::now();
        while(!app->shouldExit()&&app->getWindow()&&!app->getWindow()->shouldClose()) {
            const auto now=Clock::now();
            const float deltaTime=std::clamp(std::chrono::duration<float>(now-lastTime).count(),0.0f,0.1f);
            lastTime=now;
            if(appConfig.benchmarkOnStartup&&appConfig.benchmarkFixedDeltaSeconds>0.0f)
                app->processFrame(appConfig.benchmarkFixedDeltaSeconds,appConfig.benchmarkFixedDeltaSeconds);
            else app->processFrame(deltaTime);
            if(saves)saves->update(*app);
        }
        LOG_INFO("Main loop ended");
        benchmarkPassed=benchmarkPassed&&(!appConfig.benchmarkOnStartup||app->benchmarkPassed());
        // The staged target retains its already-read archive and its own store
        // lock. Only a completed, explicit Leave can transfer those owners.
        // The old GPU/window is gone before the next one is initialized.
        auto next=saves?saves->takeTransition(*app)
            :std::pair<std::unique_ptr<voxy::Application>,std::unique_ptr<voxy::NativeCoveSaves>>{};
        app->setUpdateCallback({});app->setCaptureCallback({});
        if(saves)saves->close(*app);
        app->shutdown();
        if(!next.first||!next.second)break;
        app=std::move(next.first);saves=std::move(next.second);
        appConfig.screenshotPath.reset();appConfig.screenshotTourIndices.clear();
        appConfig.benchmarkOnStartup=false;appConfig.exitAfterBenchmark=false;
        if(!saves->prepare(*app,saveError)) {
            LOG_ERROR("Prepared expedition could not start: {}",saveError);startupPassed=false;break;
        }
    }
    const bool motionPassed=!motion || motion->finish();
    if(app) {
        app->setUpdateCallback({});app->setCaptureCallback({});
        if(saves)saves->close(*app);
        app->shutdown();
    }
    voxy::log::shutdown();
    return !startupPassed?1:benchmarkPassed&&motionPassed?0:2;
}
