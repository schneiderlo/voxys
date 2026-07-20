// ═══════════════════════════════════════════════════════════════════════════════
// test_application.cpp - Unit tests for Application class
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for the main Application shell. These tests verify:
//   - ApplicationConfig defaults and customization
//   - RenderPath enum and string conversion
//   - ApplicationStats initialization
//   - Application configuration handling
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

#include <glm/glm.hpp>

#include "app/application.hpp"

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// RenderPath Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RenderPathTest, ToStringTriangle) {
    EXPECT_STREQ(renderPathToString(RenderPath::Triangle), "triangle");
}

TEST(RenderPathTest, ToStringRaycast) {
    EXPECT_STREQ(renderPathToString(RenderPath::Raycast), "raycast");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ApplicationConfig Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ApplicationConfigTest, DefaultsHaveReasonableValues) {
    ApplicationConfig config = ApplicationConfig::defaults();
    
    // Window defaults
    EXPECT_GT(config.windowWidth, 0);
    EXPECT_GT(config.windowHeight, 0);
    EXPECT_FALSE(config.windowTitle.empty());
    
    // Rendering defaults
    EXPECT_EQ(config.renderPath, RenderPath::Raycast);
    EXPECT_GT(config.resolutionScale, 0.0f);
    EXPECT_LE(config.resolutionScale, 2.0f);
    
    // Terrain defaults
    EXPECT_GT(config.heightmapWidth, 0);
    EXPECT_GT(config.heightmapHeight, 0);
    EXPECT_GT(config.heightScale, 0.0f);
    EXPECT_GT(config.cellScale, 0.0f);
    
    // Camera defaults
    EXPECT_GT(config.cameraFovDegrees, 0.0f);
    EXPECT_LT(config.cameraFovDegrees, 180.0f);
    EXPECT_GT(config.cameraNear, 0.0f);
    EXPECT_GT(config.cameraFar, config.cameraNear);
    EXPECT_GT(config.cameraMoveSpeed, 0.0f);
    EXPECT_GT(config.cameraMouseSensitivity, 0.0f);
    EXPECT_EQ(config.benchmarkBodyCount, 0u);
    EXPECT_DOUBLE_EQ(config.benchmarkMinimumFps, 0.0);
    EXPECT_FLOAT_EQ(config.benchmarkFixedDeltaSeconds, 0.0f);
    EXPECT_FLOAT_EQ(config.gpuPhysicsBroadPhaseCellSize, 4.0f);
    
    // Paths
    EXPECT_FALSE(config.shaderDir.empty());
    EXPECT_FALSE(config.assetDir.empty());
}

TEST(ApplicationConfigTest, CustomConfiguration) {
    ApplicationConfig config;
    
    config.windowWidth = 1920;
    config.windowHeight = 1080;
    config.windowTitle = "Custom Title";
    config.fullscreen = true;
    config.renderPath = RenderPath::Triangle;
    config.heightmapWidth = 512;
    config.heightmapHeight = 512;
    config.heightScale = 1000.0f;
    config.cameraFovDegrees = 90.0f;
    
    EXPECT_EQ(config.windowWidth, 1920);
    EXPECT_EQ(config.windowHeight, 1080);
    EXPECT_EQ(config.windowTitle, "Custom Title");
    EXPECT_TRUE(config.fullscreen);
    EXPECT_EQ(config.renderPath, RenderPath::Triangle);
    EXPECT_EQ(config.heightmapWidth, 512);
    EXPECT_EQ(config.heightmapHeight, 512);
    EXPECT_FLOAT_EQ(config.heightScale, 1000.0f);
    EXPECT_FLOAT_EQ(config.cameraFovDegrees, 90.0f);
}

TEST(ApplicationConfigTest, HeightmapPathHandling) {
    ApplicationConfig config;
    
    // Empty path means procedural
    EXPECT_TRUE(config.heightmapPath.empty());
    
    // Can set a path
    config.heightmapPath = "assets/test.raw";
    EXPECT_FALSE(config.heightmapPath.empty());
    EXPECT_EQ(config.heightmapPath.filename(), "test.raw");
}

// ═══════════════════════════════════════════════════════════════════════════════
// ApplicationStats Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ApplicationStatsTest, InitializesToZero) {
    ApplicationStats stats;
    
    EXPECT_DOUBLE_EQ(stats.fps, 0.0);
    EXPECT_DOUBLE_EQ(stats.frameTimeMs, 0.0);
    EXPECT_DOUBLE_EQ(stats.avgFrameTimeMs, 0.0);
    EXPECT_EQ(stats.frameCount, 0);
    EXPECT_DOUBLE_EQ(stats.totalTimeSeconds, 0.0);
    EXPECT_EQ(stats.terrainWidth, 0);
    EXPECT_EQ(stats.terrainHeight, 0);
    EXPECT_EQ(stats.terrainMipLevels, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Application Lifecycle Tests (No GPU)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ApplicationTest, DefaultConstruction) {
    Application app;
    
    EXPECT_FALSE(app.isInitialized());
    EXPECT_FALSE(app.shouldExit());
    EXPECT_EQ(app.getWindow(), nullptr);
    EXPECT_EQ(app.getGPUContext(), nullptr);
    EXPECT_EQ(app.getInput(), nullptr);
    EXPECT_EQ(app.getCamera(), nullptr);
    EXPECT_EQ(app.getHeightmap(), nullptr);
}

TEST(ApplicationTest, RequestExitBeforeInit) {
    Application app;
    
    // Should be safe to call even before initialization
    app.requestExit();
    EXPECT_TRUE(app.shouldExit());
}

TEST(ApplicationTest, ShutdownBeforeInit) {
    Application app;
    
    // Should be safe to call even if never initialized
    app.shutdown();
    EXPECT_FALSE(app.isInitialized());
}

TEST(ApplicationTest, StatsAccessBeforeInit) {
    Application app;
    
    const auto& stats = app.getStats();
    EXPECT_EQ(stats.frameCount, 0);
    EXPECT_DOUBLE_EQ(stats.fps, 0.0);
}

TEST(ApplicationTest, ConfigAccessBeforeInit) {
    Application app;
    
    const auto& config = app.getConfig();
    // Should return defaults
    EXPECT_GT(config.windowWidth, 0);
    EXPECT_EQ(config.renderPath, RenderPath::Raycast);
}

TEST(ApplicationTest, RenderPathAccessBeforeInit) {
    Application app;
    
    // Default should be Raycast
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
}

TEST(ApplicationTest, RuntimeRendererSettingsValidateAndClamp) {
    Application app;

    EXPECT_TRUE(app.setRendererSetting("water.waveStrength", 1.75, false));
    ASSERT_TRUE(app.getRendererSetting("water.waveStrength").has_value());
    EXPECT_DOUBLE_EQ(*app.getRendererSetting("water.waveStrength"), 1.75);

    EXPECT_TRUE(app.setRendererSetting("camera.fov", 500.0, true));
    EXPECT_DOUBLE_EQ(*app.getRendererSetting("camera.fov"), 120.0);

    EXPECT_FALSE(app.setRendererSetting("not.a.setting", 1.0, true));
    EXPECT_FALSE(app.getRendererSetting("not.a.setting").has_value());
    EXPECT_FALSE(app.setRendererSetting(
        "camera.fov", std::numeric_limits<double>::quiet_NaN(), true));
}

TEST(ApplicationTest, RuntimeRendererSchemaRoundTripsEveryEngineSetting) {
    Application app;
    constexpr std::array<std::pair<std::string_view, double>, 52> settings{{
        {"lighting.sunAzimuth", -42.0},
        {"lighting.sunElevation", 37.0},
        {"lighting.sunColor.r", 0.91},
        {"lighting.sunColor.g", 0.82},
        {"lighting.sunColor.b", 0.73},
        {"lighting.sunIntensity", 2.2},
        {"lighting.ambientColor.r", 0.12},
        {"lighting.ambientColor.g", 0.18},
        {"lighting.ambientColor.b", 0.24},
        {"lighting.ambientIntensity", 0.6},
        {"lighting.fogColor.r", 0.2},
        {"lighting.fogColor.g", 0.3},
        {"lighting.fogColor.b", 0.4},
        {"lighting.fogDensity", 0.0002},
        {"lighting.exposure", 1.2},
        {"water.enabled", 0.0},
        {"water.height", -100.0},
        {"water.shallowColor.r", 0.11},
        {"water.shallowColor.g", 0.32},
        {"water.shallowColor.b", 0.54},
        {"water.deepColor.r", 0.03},
        {"water.deepColor.g", 0.16},
        {"water.deepColor.b", 0.28},
        {"water.roughness", 0.18},
        {"water.waveStrength", 1.1},
        {"water.reflectionStrength", 0.6},
        {"water.shoreFade", 45.0},
        {"water.ior", 1.34},
        {"water.distortion", 0.1},
        {"water.absorptionScale", 1.2},
        {"water.scatterStrength", 1.3},
        {"water.foamSize", 200.0},
        {"water.foamOpacity", 0.4},
        {"water.foamCoverage", 0.3},
        {"water.reflectionDistance", 1300.0},
        {"water.spectrum.significantHeight", 12.0},
        {"water.spectrum.direction", -25.0},
        {"water.spectrum.choppiness", 1.8},
        {"water.spectrum.peakEnhancement", 1.4},
        {"water.spectrum.windAlignment", 0.7},
        {"water.spectrum.speed", 1.5},
        {"water.spectrum.largePatch", 2048.0},
        {"water.spectrum.detailPatch", 384.0},
        {"water.spectrum.largeAmplitude", 0.4},
        {"water.spectrum.detailAmplitude", 0.09},
        {"water.spectrum.directionalSine", 0.55},
        {"camera.fov", 72.0},
        {"camera.near", 0.2},
        {"camera.far", 20000.0},
        {"camera.moveSpeed", 16.0},
        {"camera.mouseSensitivity", 0.003},
        {"camera.eyeHeight", 1.9},
    }};

    for (const auto& [name, value] : settings) {
        EXPECT_TRUE(app.setRendererSetting(name, value, true)) << name;
        const auto actual = app.getRendererSetting(name);
        ASSERT_TRUE(actual.has_value()) << name;
        EXPECT_NEAR(*actual, value, 1.0e-4) << name;
    }
}

TEST(ApplicationTest, RuntimeRendererPathUsesTheExistingSwitch) {
    Application app;

    EXPECT_TRUE(app.setRendererSetting("render.path", 0.0, true));
    EXPECT_EQ(app.getRenderPath(), RenderPath::Triangle);
    EXPECT_DOUBLE_EQ(*app.getRendererSetting("render.path"), 0.0);

    EXPECT_TRUE(app.setRendererSetting("render.path", 1.0, true));
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
    EXPECT_FALSE(app.setRendererSetting("render.path", -0.5, true));
    EXPECT_FALSE(app.setRendererSetting("render.path", 1.5, true));
    EXPECT_FALSE(app.setRendererSetting("render.path", 3.0, true));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Render Path Switching Tests (Phase 9.2)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RenderPathSwitchingTest, ToggleRenderPathBeforeInit) {
    Application app;
    
    // Default is Raycast
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
    
    // Toggle should work even before init (changes config)
    app.toggleRenderPath();
    EXPECT_EQ(app.getRenderPath(), RenderPath::Triangle);
    
    app.toggleRenderPath();
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
}

TEST(RenderPathSwitchingTest, SetRenderPathBeforeInit) {
    Application app;
    
    app.setRenderPath(RenderPath::Triangle);
    EXPECT_EQ(app.getRenderPath(), RenderPath::Triangle);
    
    app.setRenderPath(RenderPath::Raycast);
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
    
    // Setting same path should be idempotent
    app.setRenderPath(RenderPath::Raycast);
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
}

TEST(RenderPathSwitchingTest, ConfigWithTrianglePath) {
    ApplicationConfig config;
    config.renderPath = RenderPath::Triangle;
    
    Application app;
    EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);  // Default before config applied
}

TEST(RenderPathSwitchingTest, ConfigWithRaycastPath) {
    ApplicationConfig config;
    config.renderPath = RenderPath::Raycast;
    
    EXPECT_EQ(config.renderPath, RenderPath::Raycast);
}

TEST(RenderPathSwitchingTest, StatsTrackActiveRenderPath) {
    ApplicationStats stats;
    
    // Default
    EXPECT_EQ(stats.activeRenderPath, RenderPath::Raycast);
    
    // Can be changed
    stats.activeRenderPath = RenderPath::Triangle;
    EXPECT_EQ(stats.activeRenderPath, RenderPath::Triangle);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Application with GPU Tests (Requires WebGPU)
// ═══════════════════════════════════════════════════════════════════════════════

#if defined(VOXY_NATIVE)

class ApplicationGPUTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Find shader directory
        std::vector<std::filesystem::path> searchPaths = {
            "shaders",
            "../shaders",
            "../../shaders",
            "../../../shaders",
        };
        
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path) && 
                std::filesystem::exists(path / "terrain.wgsl")) {
                shaderDir_ = path;
                break;
            }
        }
    }
    
    ApplicationConfig getTestConfig() {
        ApplicationConfig config = ApplicationConfig::defaults();
        config.windowWidth = 320;
        config.windowHeight = 240;
        config.windowTitle = "Test Window";
        config.enableValidation = true;
        config.showFPS = false;
        config.heightmapWidth = 64;
        config.heightmapHeight = 64;
        
        if (!shaderDir_.empty()) {
            config.shaderDir = shaderDir_;
        }
        
        return config;
    }
    
    std::filesystem::path shaderDir_;
};

// Note: These tests create actual windows and GPU resources.
// They are disabled by default because they require a display
// and may be slow. Enable with --gtest_also_run_disabled_tests

TEST_F(ApplicationGPUTest, DISABLED_InitAndShutdown) {
    Application app;
    
    auto config = getTestConfig();
    
    bool initResult = app.init(config);
    
    // Initialization may fail if no GPU is available
    if (initResult) {
        EXPECT_TRUE(app.isInitialized());
        EXPECT_NE(app.getWindow(), nullptr);
        EXPECT_NE(app.getGPUContext(), nullptr);
        EXPECT_NE(app.getInput(), nullptr);
        EXPECT_NE(app.getCamera(), nullptr);
        EXPECT_NE(app.getHeightmap(), nullptr);
        
        app.shutdown();
        EXPECT_FALSE(app.isInitialized());
    }
}

TEST_F(ApplicationGPUTest, DISABLED_RenderPathToggle) {
    Application app;
    
    auto config = getTestConfig();
    config.renderPath = RenderPath::Triangle;
    
    if (app.init(config)) {
        EXPECT_EQ(app.getRenderPath(), RenderPath::Triangle);
        
        app.toggleRenderPath();
        EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
        
        app.toggleRenderPath();
        EXPECT_EQ(app.getRenderPath(), RenderPath::Triangle);
        
        app.setRenderPath(RenderPath::Raycast);
        EXPECT_EQ(app.getRenderPath(), RenderPath::Raycast);
        
        app.shutdown();
    }
}

TEST_F(ApplicationGPUTest, DISABLED_ProcessSingleFrame) {
    Application app;
    
    auto config = getTestConfig();
    
    if (app.init(config)) {
        // Process a few frames
        for (int i = 0; i < 5; ++i) {
            app.processFrame(1.0f / 60.0f);
        }
        
        // Stats should be updated
        EXPECT_GT(app.getStats().frameCount, 0);
        
        app.shutdown();
    }
}

TEST_F(ApplicationGPUTest, DISABLED_UpdateCallback) {
    Application app;
    
    auto config = getTestConfig();
    
    if (app.init(config)) {
        int callCount = 0;
        float lastDeltaTime = 0.0f;
        
        app.setUpdateCallback([&](float dt) {
            callCount++;
            lastDeltaTime = dt;
        });
        
        app.processFrame(0.016f);
        
        EXPECT_EQ(callCount, 1);
        EXPECT_FLOAT_EQ(lastDeltaTime, 0.016f);
        
        app.processFrame(0.033f);
        
        EXPECT_EQ(callCount, 2);
        EXPECT_FLOAT_EQ(lastDeltaTime, 0.033f);
        
        app.shutdown();
    }
}

#endif // VOXY_NATIVE

} // namespace voxy
