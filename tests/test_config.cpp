// ═══════════════════════════════════════════════════════════════════════════════
// test_config.cpp - Unit tests for configuration system
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "core/config.hpp"

#include <fstream>
#include <cstdio>
#include <filesystem>

namespace voxy::config {

// ─────────────────────────────────────────────────────────────────────────────
// Utility Function Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(ConfigUtilsTest, Trim) {
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("\t\ntest\r\n"), "test");
    EXPECT_EQ(trim("nowhitespace"), "nowhitespace");
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("   "), "");
}

TEST(ConfigUtilsTest, ParseBool) {
    // True values
    EXPECT_TRUE(parseBool("true"));
    EXPECT_TRUE(parseBool("TRUE"));
    EXPECT_TRUE(parseBool("True"));
    EXPECT_TRUE(parseBool("yes"));
    EXPECT_TRUE(parseBool("YES"));
    EXPECT_TRUE(parseBool("1"));
    EXPECT_TRUE(parseBool("on"));
    EXPECT_TRUE(parseBool("ON"));
    
    // False values
    EXPECT_FALSE(parseBool("false"));
    EXPECT_FALSE(parseBool("FALSE"));
    EXPECT_FALSE(parseBool("no"));
    EXPECT_FALSE(parseBool("0"));
    EXPECT_FALSE(parseBool("off"));
    
    // Invalid values return default
    EXPECT_FALSE(parseBool("invalid", false));
    EXPECT_TRUE(parseBool("invalid", true));
}

TEST(ConfigUtilsTest, ParseFloat) {
    EXPECT_FLOAT_EQ(parseFloat("3.14"), 3.14f);
    EXPECT_FLOAT_EQ(parseFloat("42"), 42.0f);
    EXPECT_FLOAT_EQ(parseFloat("-1.5"), -1.5f);
    EXPECT_FLOAT_EQ(parseFloat("0.001"), 0.001f);
    
    // Invalid values return default
    EXPECT_FLOAT_EQ(parseFloat("invalid", 99.0f), 99.0f);
    EXPECT_FLOAT_EQ(parseFloat("", 1.0f), 1.0f);
}

TEST(ConfigUtilsTest, ParseInt) {
    EXPECT_EQ(parseInt("42"), 42);
    EXPECT_EQ(parseInt("-10"), -10);
    EXPECT_EQ(parseInt("0"), 0);
    EXPECT_EQ(parseInt("1920"), 1920);
    
    // Invalid values return default
    EXPECT_EQ(parseInt("invalid", 100), 100);
    EXPECT_EQ(parseInt("", 50), 50);
}

// ─────────────────────────────────────────────────────────────────────────────
// Default Config Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(ConfigDefaultsTest, RenderConfig) {
    RenderConfig config;
    EXPECT_EQ(config.path, "raycast");
    EXPECT_FLOAT_EQ(config.resolutionScale, 1.0f);
    EXPECT_TRUE(config.vsync);
    EXPECT_EQ(config.maxFps, 0);
}

TEST(ConfigDefaultsTest, TerrainConfig) {
    TerrainConfig config;
    EXPECT_EQ(config.heightmap, "data/Rugged Terrain with Rocky Peaks Height Map PNG.png");
    EXPECT_FLOAT_EQ(config.heightScale, 500.0f);
    EXPECT_FLOAT_EQ(config.cellScale, 1.0f);
}

TEST(ConfigDefaultsTest, CameraConfig) {
    CameraConfig config;
    EXPECT_FLOAT_EQ(config.fov, 60.0f);
    EXPECT_FLOAT_EQ(config.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(config.farPlane, 10000.0f);
    EXPECT_FLOAT_EQ(config.moveSpeed, 50.0f);
    EXPECT_FLOAT_EQ(config.mouseSensitivity, 0.002f);
}

TEST(ConfigDefaultsTest, LightingConfig) {
    LightingConfig config;
    EXPECT_FLOAT_EQ(config.sunDirection[0], 0.5f);
    EXPECT_FLOAT_EQ(config.sunDirection[1], 0.8f);
    EXPECT_FLOAT_EQ(config.sunDirection[2], 0.3f);
    EXPECT_FLOAT_EQ(config.fogDensity, 0.0001f);
}

TEST(ConfigDefaultsTest, DebugConfig) {
    DebugConfig config;
    EXPECT_TRUE(config.showStats);
    EXPECT_FALSE(config.showWireframe);
    EXPECT_EQ(config.logLevel, "info");
    EXPECT_TRUE(config.enableValidation);
}

TEST(ConfigDefaultsTest, WindowConfig) {
    WindowConfig config;
    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_FALSE(config.fullscreen);
    EXPECT_EQ(config.title, "voxy");
}

// ─────────────────────────────────────────────────────────────────────────────
// Command-Line Argument Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(CommandLineArgsTest, DefaultValues) {
    char* argv[] = {const_cast<char*>("voxy")};
    auto args = parseArgs(1, argv);
    
    EXPECT_EQ(args.configPath, "voxy.cfg");
    EXPECT_FALSE(args.renderPath.has_value());
    EXPECT_FALSE(args.heightmap.has_value());
    EXPECT_FALSE(args.width.has_value());
    EXPECT_FALSE(args.height.has_value());
    EXPECT_FALSE(args.fullscreen);
    EXPECT_FALSE(args.logLevel.has_value());
    EXPECT_FALSE(args.noValidation);
    EXPECT_FALSE(args.benchmark);
    EXPECT_FALSE(args.help);
}

TEST(CommandLineArgsTest, HelpFlag) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--help")};
    auto args = parseArgs(2, argv);
    EXPECT_TRUE(args.help);
    
    char* argv2[] = {const_cast<char*>("voxy"), const_cast<char*>("-h")};
    auto args2 = parseArgs(2, argv2);
    EXPECT_TRUE(args2.help);
}

TEST(CommandLineArgsTest, ConfigPath) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--config"), const_cast<char*>("custom.cfg")};
    auto args = parseArgs(3, argv);
    EXPECT_EQ(args.configPath, "custom.cfg");
}

TEST(CommandLineArgsTest, RenderPath) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--render-path"), const_cast<char*>("triangle")};
    auto args = parseArgs(3, argv);
    ASSERT_TRUE(args.renderPath.has_value());
    EXPECT_EQ(*args.renderPath, "triangle");
}

TEST(CommandLineArgsTest, WindowSize) {
    char* argv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--width"), const_cast<char*>("1920"),
        const_cast<char*>("--height"), const_cast<char*>("1080")
    };
    auto args = parseArgs(5, argv);
    ASSERT_TRUE(args.width.has_value());
    ASSERT_TRUE(args.height.has_value());
    EXPECT_EQ(*args.width, 1920);
    EXPECT_EQ(*args.height, 1080);
}

TEST(CommandLineArgsTest, Fullscreen) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--fullscreen")};
    auto args = parseArgs(2, argv);
    EXPECT_TRUE(args.fullscreen);
}

TEST(CommandLineArgsTest, LogLevel) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--log-level"), const_cast<char*>("debug")};
    auto args = parseArgs(3, argv);
    ASSERT_TRUE(args.logLevel.has_value());
    EXPECT_EQ(*args.logLevel, "debug");
}

TEST(CommandLineArgsTest, NoValidation) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--no-validation")};
    auto args = parseArgs(2, argv);
    EXPECT_TRUE(args.noValidation);
}

TEST(CommandLineArgsTest, Benchmark) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--benchmark")};
    auto args = parseArgs(2, argv);
    EXPECT_TRUE(args.benchmark);
}

TEST(CommandLineArgsTest, MultipleArgs) {
    char* argv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--fullscreen"),
        const_cast<char*>("--log-level"), const_cast<char*>("trace"),
        const_cast<char*>("--no-validation"),
        const_cast<char*>("--width"), const_cast<char*>("2560")
    };
    auto args = parseArgs(7, argv);
    
    EXPECT_TRUE(args.fullscreen);
    EXPECT_TRUE(args.noValidation);
    ASSERT_TRUE(args.logLevel.has_value());
    EXPECT_EQ(*args.logLevel, "trace");
    ASSERT_TRUE(args.width.has_value());
    EXPECT_EQ(*args.width, 2560);
}

// ─────────────────────────────────────────────────────────────────────────────
// Config File Loading Tests
// ─────────────────────────────────────────────────────────────────────────────

class ConfigFileTest : public ::testing::Test {
protected:
    std::string testConfigPath = "test_config_temp.cfg";
    
    void TearDown() override {
        std::remove(testConfigPath.c_str());
    }
    
    void writeTestConfig(const std::string& content) {
        std::ofstream file(testConfigPath);
        file << content;
    }
};

TEST_F(ConfigFileTest, LoadNonexistentFile) {
    auto config = load("nonexistent_file.cfg");
    
    // Should return defaults
    EXPECT_EQ(config.render.path, "raycast");
    EXPECT_EQ(config.window.width, 1280);
}

TEST_F(ConfigFileTest, LoadBasicConfig) {
    writeTestConfig(R"(
[render]
path = "triangle"
vsync = false

[window]
width = 1920
height = 1080
fullscreen = true
)");
    
    auto config = load(testConfigPath);
    
    EXPECT_EQ(config.render.path, "triangle");
    EXPECT_FALSE(config.render.vsync);
    EXPECT_EQ(config.window.width, 1920);
    EXPECT_EQ(config.window.height, 1080);
    EXPECT_TRUE(config.window.fullscreen);
}

TEST_F(ConfigFileTest, LoadAllSections) {
    writeTestConfig(R"(
[render]
path = "raycast"
resolution_scale = 0.5
max_fps = 60

[terrain]
heightmap = "custom.ldh"
height_scale = 1000.0

[camera]
fov = 90.0
move_speed = 100.0

[debug]
show_stats = false
log_level = "debug"
)");
    
    auto config = load(testConfigPath);
    
    EXPECT_EQ(config.render.path, "raycast");
    EXPECT_FLOAT_EQ(config.render.resolutionScale, 0.5f);
    EXPECT_EQ(config.render.maxFps, 60);
    EXPECT_EQ(config.terrain.heightmap, "custom.ldh");
    EXPECT_FLOAT_EQ(config.terrain.heightScale, 1000.0f);
    EXPECT_FLOAT_EQ(config.camera.fov, 90.0f);
    EXPECT_FLOAT_EQ(config.camera.moveSpeed, 100.0f);
    EXPECT_FALSE(config.debug.showStats);
    EXPECT_EQ(config.debug.logLevel, "debug");
}

TEST_F(ConfigFileTest, IgnoresComments) {
    writeTestConfig(R"(
# This is a comment
[render]
# Another comment
path = "triangle"  # Inline comment should work
)");
    
    auto config = load(testConfigPath);
    EXPECT_EQ(config.render.path, "triangle");
}

TEST_F(ConfigFileTest, CommandLineOverrides) {
    writeTestConfig(R"(
[render]
path = "raycast"

[window]
width = 800
height = 600
)");
    
    CommandLineArgs args;
    args.configPath = testConfigPath;
    args.renderPath = "triangle";
    args.width = 1920;
    
    auto config = load(testConfigPath, args);
    
    EXPECT_EQ(config.render.path, "triangle");  // Overridden
    EXPECT_EQ(config.window.width, 1920);        // Overridden
    EXPECT_EQ(config.window.height, 600);        // From file
}

// ─────────────────────────────────────────────────────────────────────────────
// Config Save Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ConfigFileTest, SaveAndReload) {
    Config original;
    original.render.path = "triangle";
    original.render.resolutionScale = 0.75f;
    original.window.width = 1600;
    original.window.height = 900;
    original.camera.fov = 75.0f;
    original.debug.logLevel = "trace";
    
    EXPECT_TRUE(save(original, testConfigPath));
    
    auto loaded = load(testConfigPath);
    
    EXPECT_EQ(loaded.render.path, original.render.path);
    EXPECT_FLOAT_EQ(loaded.render.resolutionScale, original.render.resolutionScale);
    EXPECT_EQ(loaded.window.width, original.window.width);
    EXPECT_EQ(loaded.window.height, original.window.height);
    EXPECT_FLOAT_EQ(loaded.camera.fov, original.camera.fov);
    EXPECT_EQ(loaded.debug.logLevel, original.debug.logLevel);
}

// ─────────────────────────────────────────────────────────────────────────────
// Config Integration Tests (Phase 9.3)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ConfigFileTest, AllSettingsPreserved) {
    // Create a complete config with all settings
    writeTestConfig(R"(
[render]
path = "triangle"
resolution_scale = 0.75
vsync = false
max_fps = 120

[terrain]
heightmap = "custom/terrain.ldh"
height_scale = 1000.0
cell_scale = 2.0

[camera]
fov = 90.0
near_plane = 0.5
far_plane = 5000.0
move_speed = 100.0
mouse_sensitivity = 0.005

[lighting]
sun_direction = [0.3, 0.7, 0.2]
fog_density = 0.0005

[debug]
show_stats = false
show_wireframe = true
log_level = "trace"
enable_validation = false

[window]
width = 1920
height = 1080
fullscreen = true
title = "Custom Title"
)");
    
    auto config = load(testConfigPath);
    
    // Verify render settings
    EXPECT_EQ(config.render.path, "triangle");
    EXPECT_FLOAT_EQ(config.render.resolutionScale, 0.75f);
    EXPECT_FALSE(config.render.vsync);
    EXPECT_EQ(config.render.maxFps, 120);
    
    // Verify terrain settings
    EXPECT_EQ(config.terrain.heightmap, "custom/terrain.ldh");
    EXPECT_FLOAT_EQ(config.terrain.heightScale, 1000.0f);
    EXPECT_FLOAT_EQ(config.terrain.cellScale, 2.0f);
    
    // Verify camera settings
    EXPECT_FLOAT_EQ(config.camera.fov, 90.0f);
    EXPECT_FLOAT_EQ(config.camera.nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(config.camera.farPlane, 5000.0f);
    EXPECT_FLOAT_EQ(config.camera.moveSpeed, 100.0f);
    EXPECT_FLOAT_EQ(config.camera.mouseSensitivity, 0.005f);
    
    // Verify lighting settings
    EXPECT_FLOAT_EQ(config.lighting.sunDirection[0], 0.3f);
    EXPECT_FLOAT_EQ(config.lighting.sunDirection[1], 0.7f);
    EXPECT_FLOAT_EQ(config.lighting.sunDirection[2], 0.2f);
    EXPECT_FLOAT_EQ(config.lighting.fogDensity, 0.0005f);
    
    // Verify debug settings
    EXPECT_FALSE(config.debug.showStats);
    EXPECT_TRUE(config.debug.showWireframe);
    EXPECT_EQ(config.debug.logLevel, "trace");
    EXPECT_FALSE(config.debug.enableValidation);
    
    // Verify window settings
    EXPECT_EQ(config.window.width, 1920);
    EXPECT_EQ(config.window.height, 1080);
    EXPECT_TRUE(config.window.fullscreen);
    EXPECT_EQ(config.window.title, "Custom Title");
}

TEST_F(ConfigFileTest, CommandLineOverridesAllSettings) {
    // Create config file with specific values
    writeTestConfig(R"(
[render]
path = "raycast"

[terrain]
heightmap = "original.ldh"

[window]
width = 800
height = 600
fullscreen = false

[debug]
log_level = "info"
enable_validation = true
)");
    
    // Create command-line args that override everything
    CommandLineArgs args;
    args.configPath = testConfigPath;
    args.renderPath = "triangle";
    args.heightmap = "override.ldh";
    args.width = 1920;
    args.height = 1080;
    args.fullscreen = true;
    args.logLevel = "debug";
    args.noValidation = true;
    
    auto config = load(testConfigPath, args);
    
    // All command-line values should override config file
    EXPECT_EQ(config.render.path, "triangle");
    EXPECT_EQ(config.terrain.heightmap, "override.ldh");
    EXPECT_EQ(config.window.width, 1920);
    EXPECT_EQ(config.window.height, 1080);
    EXPECT_TRUE(config.window.fullscreen);
    EXPECT_EQ(config.debug.logLevel, "debug");
    EXPECT_FALSE(config.debug.enableValidation);
}

TEST(CommandLineArgsTest, HeightmapPath) {
    char* argv[] = {const_cast<char*>("voxy"), const_cast<char*>("--heightmap"), const_cast<char*>("custom/terrain.ldh")};
    auto args = parseArgs(3, argv);
    ASSERT_TRUE(args.heightmap.has_value());
    EXPECT_EQ(*args.heightmap, "custom/terrain.ldh");
}

TEST(CommandLineArgsTest, AllOverridesTogether) {
    char* argv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--render-path"), const_cast<char*>("triangle"),
        const_cast<char*>("--heightmap"), const_cast<char*>("test.ldh"),
        const_cast<char*>("--width"), const_cast<char*>("1920"),
        const_cast<char*>("--height"), const_cast<char*>("1080"),
        const_cast<char*>("--fullscreen"),
        const_cast<char*>("--log-level"), const_cast<char*>("trace"),
        const_cast<char*>("--no-validation"),
        const_cast<char*>("--benchmark")
    };
    auto args = parseArgs(14, argv);
    
    ASSERT_TRUE(args.renderPath.has_value());
    EXPECT_EQ(*args.renderPath, "triangle");
    
    ASSERT_TRUE(args.heightmap.has_value());
    EXPECT_EQ(*args.heightmap, "test.ldh");
    
    ASSERT_TRUE(args.width.has_value());
    EXPECT_EQ(*args.width, 1920);
    
    ASSERT_TRUE(args.height.has_value());
    EXPECT_EQ(*args.height, 1080);
    
    EXPECT_TRUE(args.fullscreen);
    
    ASSERT_TRUE(args.logLevel.has_value());
    EXPECT_EQ(*args.logLevel, "trace");
    
    EXPECT_TRUE(args.noValidation);
    EXPECT_TRUE(args.benchmark);
}

} // namespace voxy::config

