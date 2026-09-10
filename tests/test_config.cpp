// ═══════════════════════════════════════════════════════════════════════════════
// test_config.cpp - Unit tests for configuration system
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "core/config.hpp"

#include <fstream>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>

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
    EXPECT_FLOAT_EQ(parseFloat("3.14junk", 99.0f), 99.0f);
    EXPECT_FLOAT_EQ(parseFloat("", 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(parseFloat("nan", 7.0f), 7.0f);
    EXPECT_FLOAT_EQ(parseFloat("inf", 7.0f), 7.0f);
}

TEST(ConfigUtilsTest, ParseInt) {
    EXPECT_EQ(parseInt("42"), 42);
    EXPECT_EQ(parseInt("-10"), -10);
    EXPECT_EQ(parseInt("0"), 0);
    EXPECT_EQ(parseInt("1920"), 1920);
    
    // Invalid values return default
    EXPECT_EQ(parseInt("invalid", 100), 100);
    EXPECT_EQ(parseInt("42px", 100), 100);
    EXPECT_EQ(parseInt("", 50), 50);
}

TEST(ConfigUtilsTest, GenericParseSupportsItsAdvertisedTypes) {
    EXPECT_EQ(parse<uint64_t>("18446744073709551615", 7u),
              std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(parse<uint64_t>("-1", 7u), 7u);
    EXPECT_DOUBLE_EQ(parse<double>("2.5", 9.0), 2.5);
    EXPECT_DOUBLE_EQ(parse<double>("nan", 9.0), 9.0);
    EXPECT_EQ(parse<std::string>("hello"), "hello");
    static_assert(noexcept(parse<int>("1", 0)));
    static_assert(!noexcept(parse<std::string>("value")));
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

TEST(ConfigDefaultsTest, WaterConfig) {
    WaterConfig config;
    EXPECT_TRUE(config.enabled);
    EXPECT_FLOAT_EQ(config.height, -230.0f);
    EXPECT_FLOAT_EQ(config.shallowColor[0], 0.12f);
    EXPECT_FLOAT_EQ(config.shallowColor[1], 0.46f);
    EXPECT_FLOAT_EQ(config.shallowColor[2], 0.50f);
    EXPECT_FLOAT_EQ(config.deepColor[0], 0.0f);
    EXPECT_FLOAT_EQ(config.deepColor[1], 0.28f);
    EXPECT_FLOAT_EQ(config.deepColor[2], 0.42f);
    EXPECT_FLOAT_EQ(config.roughness, 0.05f);
    EXPECT_FLOAT_EQ(config.waveStrength, 1.0f);
    EXPECT_FLOAT_EQ(config.reflectionStrength, 0.42f);
    EXPECT_FLOAT_EQ(config.shoreFade, 12.0f);
}

TEST(ConfigDefaultsTest, PhysicsConfig) {
    PhysicsConfig config;
    EXPECT_EQ(config.backend, "webgpu");
    EXPECT_EQ(config.gpuMaxBodies, 131072);
    EXPECT_FLOAT_EQ(config.broadPhaseCellSize, 4.0f);
    EXPECT_TRUE(config.allowCpuFallback);
    EXPECT_EQ(config.joltJobSystem, "thread_pool");
    EXPECT_EQ(config.joltWorkerThreads, 0);
    EXPECT_EQ(config.box3dWorkerThreads, 1);
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
    EXPECT_FALSE(args.physicsBackend.has_value());
    EXPECT_FALSE(args.gpuMaxBodies.has_value());
    EXPECT_FALSE(args.physicsCpuFallback.has_value());
    EXPECT_FALSE(args.joltJobSystem.has_value());
    EXPECT_FALSE(args.joltWorkerThreads.has_value());
    EXPECT_FALSE(args.box3dWorkerThreads.has_value());
    EXPECT_FALSE(args.width.has_value());
    EXPECT_FALSE(args.height.has_value());
    EXPECT_FALSE(args.vsync.has_value());
    EXPECT_FALSE(args.fullscreen);
    EXPECT_FALSE(args.logLevel.has_value());
    EXPECT_FALSE(args.noValidation);
    EXPECT_FALSE(args.benchmark);
    EXPECT_EQ(args.benchmarkBodies, 0);
    EXPECT_FLOAT_EQ(args.benchmarkMinimumFps, 0.0f);
    EXPECT_FLOAT_EQ(args.benchmarkFixedHz, 0.0f);
    EXPECT_FALSE(args.help);
    EXPECT_EQ(
        validateWreckwaterClientConfig(args.wreckwaterClient),
        WreckwaterClientConfigStatus::Disabled);
}

TEST(CommandLineArgsTest,
     CompleteWreckwaterBootstrapParsesBinaryKey) {
    char key[] =
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f";
    char* argv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--wreckwater-server"),
        const_cast<char*>("127.0.0.1"),
        const_cast<char*>("--wreckwater-port"),
        const_cast<char*>("45000"),
        const_cast<char*>("--wreckwater-peer"),
        const_cast<char*>("2"),
        const_cast<char*>("--wreckwater-key"),
        key,
        const_cast<char*>("--wreckwater-session"),
        const_cast<char*>("101"),
        const_cast<char*>("--wreckwater-match"),
        const_cast<char*>("202"),
        const_cast<char*>("--wreckwater-world"),
        const_cast<char*>("303"),
        const_cast<char*>("--wreckwater-world-epoch"),
        const_cast<char*>("4"),
        const_cast<char*>("--wreckwater-authority-epoch"),
        const_cast<char*>("5"),
    };
    const auto args = parseArgs(
        static_cast<int>(std::size(argv)), argv);

    EXPECT_EQ(
        validateWreckwaterClientConfig(args.wreckwaterClient),
        WreckwaterClientConfigStatus::Ready);
    EXPECT_EQ(args.wreckwaterClient.server, "127.0.0.1");
    EXPECT_EQ(args.wreckwaterClient.port, 45'000u);
    EXPECT_EQ(args.wreckwaterClient.peerId, 2u);
    EXPECT_EQ(
        args.wreckwaterClient.authenticationKey.front(),
        std::byte{0x00});
    EXPECT_EQ(
        args.wreckwaterClient.authenticationKey.back(),
        std::byte{0x1f});
}

TEST(CommandLineArgsTest,
     WreckwaterBootstrapIsAllOrNoneAndMalformedKeysFail) {
    char* incompleteArgv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--wreckwater-server"),
        const_cast<char*>("localhost"),
    };
    const auto incomplete = parseArgs(
        static_cast<int>(std::size(incompleteArgv)),
        incompleteArgv);
    EXPECT_EQ(
        validateWreckwaterClientConfig(
            incomplete.wreckwaterClient),
        WreckwaterClientConfigStatus::Incomplete);

    char* malformedArgv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--wreckwater-key"),
        const_cast<char*>("not-a-32-byte-hex-key"),
    };
    const auto malformed = parseArgs(
        static_cast<int>(std::size(malformedArgv)),
        malformedArgv);
    EXPECT_EQ(
        validateWreckwaterClientConfig(
            malformed.wreckwaterClient),
        WreckwaterClientConfigStatus::MalformedValue);
}

TEST(ConfigUtilsTest, WreckwaterKeyParserIsExactAndTransactional) {
    std::array<std::byte, 32> key{};
    key.fill(std::byte{0x5a});
    EXPECT_FALSE(parseWreckwaterAuthenticationKey("00", key));
    EXPECT_EQ(key.front(), std::byte{0x5a});

    EXPECT_FALSE(parseWreckwaterAuthenticationKey(
        "zz0102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f",
        key));
    EXPECT_EQ(key.front(), std::byte{0x5a});

    EXPECT_TRUE(parseWreckwaterAuthenticationKey(
        "000102030405060708090A0B0C0D0E0F"
        "101112131415161718191A1B1C1D1E1F",
        key));
    EXPECT_EQ(key.front(), std::byte{0x00});
    EXPECT_EQ(key.back(), std::byte{0x1f});
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

TEST(CommandLineArgsTest, PhysicsBackendAndCpuSchedulers) {
    char* argv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--physics-backend"),
        const_cast<char*>("box3d"),
        const_cast<char*>("--physics-max-bodies"),
        const_cast<char*>("200000"),
        const_cast<char*>("--no-physics-cpu-fallback"),
        const_cast<char*>("--jolt-job-system"),
        const_cast<char*>("thread_pool"),
        const_cast<char*>("--jolt-workers"),
        const_cast<char*>("4"),
        const_cast<char*>("--box3d-workers"),
        const_cast<char*>("8"),
    };
    const auto args = parseArgs(12, argv);
    ASSERT_TRUE(args.physicsBackend.has_value());
    ASSERT_TRUE(args.gpuMaxBodies.has_value());
    ASSERT_TRUE(args.physicsCpuFallback.has_value());
    ASSERT_TRUE(args.joltJobSystem.has_value());
    ASSERT_TRUE(args.joltWorkerThreads.has_value());
    ASSERT_TRUE(args.box3dWorkerThreads.has_value());
    EXPECT_EQ(*args.physicsBackend, "box3d");
    EXPECT_EQ(*args.gpuMaxBodies, 200000);
    EXPECT_FALSE(*args.physicsCpuFallback);
    EXPECT_EQ(*args.joltJobSystem, "thread_pool");
    EXPECT_EQ(*args.joltWorkerThreads, 4);
    EXPECT_EQ(*args.box3dWorkerThreads, 8);
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

TEST(CommandLineArgsTest, BenchmarkBodiesEnableBenchmark) {
    char* argv[] = {const_cast<char*>("voxy"),
                    const_cast<char*>("--benchmark-bodies"),
                    const_cast<char*>("400")};
    const auto args = parseArgs(3, argv);
    EXPECT_TRUE(args.benchmark);
    EXPECT_EQ(args.benchmarkBodies, 400);
}

TEST(CommandLineArgsTest, BenchmarkMinimumFpsEnablesBenchmark) {
    char* argv[] = {const_cast<char*>("voxy"),
                    const_cast<char*>("--benchmark-min-fps"),
                    const_cast<char*>("400.5")};
    const auto args = parseArgs(3, argv);
    EXPECT_TRUE(args.benchmark);
    EXPECT_FLOAT_EQ(args.benchmarkMinimumFps, 400.5f);
}

TEST(CommandLineArgsTest, BenchmarkFixedHzEnablesBenchmark) {
    char* argv[] = {const_cast<char*>("voxy"),
                    const_cast<char*>("--benchmark-fixed-hz"),
                    const_cast<char*>("400")};
    const auto args = parseArgs(3, argv);
    EXPECT_TRUE(args.benchmark);
    EXPECT_FLOAT_EQ(args.benchmarkFixedHz, 400.0f);
}

TEST(CommandLineArgsTest, ScreenshotTourCountIsBoundedBeforeAllocation) {
    char* hugeArgv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--screenshot-tour"),
        const_cast<char*>("2147483647"),
    };
    EXPECT_EQ(parseArgs(3, hugeArgv).screenshotTourCount, 256);

    char* negativeArgv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--screenshot-tour"),
        const_cast<char*>("-1"),
    };
    EXPECT_EQ(parseArgs(3, negativeArgv).screenshotTourCount, 0);
}

TEST(CommandLineArgsTest, InspectionMotionRetainsPairedPathsAndMissingValueForStartupRejection) {
    char* argv[] = {const_cast<char*>("voxy"),const_cast<char*>("--inspection-motion"),
        const_cast<char*>("review/track.json"),const_cast<char*>("--inspection-motion-output"),const_cast<char*>("review/new-capture")};
    const auto args=parseArgs(5,argv);
    EXPECT_EQ(args.inspectionMotionRecipe,"review/track.json");
    EXPECT_EQ(args.inspectionMotionOutput,"review/new-capture");
    EXPECT_FALSE(args.benchmark);EXPECT_FALSE(args.screenshotPath);
    char* missing[]={const_cast<char*>("voxy"),const_cast<char*>("--inspection-motion")};
    const auto invalid=parseArgs(2,missing);
    ASSERT_TRUE(invalid.inspectionMotionRecipe);
    EXPECT_TRUE(invalid.inspectionMotionRecipe->empty());
    EXPECT_FALSE(invalid.inspectionMotionOutput);
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

TEST(CommandLineArgsTest, PresentationOverrides) {
    char* uncappedArgv[] = {
        const_cast<char*>("voxy"), const_cast<char*>("--uncapped")};
    const auto uncapped = parseArgs(2, uncappedArgv);
    ASSERT_TRUE(uncapped.vsync.has_value());
    EXPECT_FALSE(*uncapped.vsync);

    char* vsyncArgv[] = {
        const_cast<char*>("voxy"), const_cast<char*>("--vsync")};
    const auto vsync = parseArgs(2, vsyncArgv);
    ASSERT_TRUE(vsync.vsync.has_value());
    EXPECT_TRUE(*vsync.vsync);
}

TEST(CommandLineArgsTest, InvalidArgcAndNullEntriesAreSafe) {
    const auto empty = parseArgs(-1, nullptr);
    EXPECT_EQ(empty.configPath, "voxy.cfg");

    char* argv[] = {
        const_cast<char*>("voxy"),
        const_cast<char*>("--width"),
        nullptr,
        const_cast<char*>("--fullscreen"),
    };
    const auto args = parseArgs(4, argv);
    EXPECT_FALSE(args.width.has_value());
    EXPECT_TRUE(args.fullscreen);
}

// ─────────────────────────────────────────────────────────────────────────────
// Config File Loading Tests
// ─────────────────────────────────────────────────────────────────────────────

class ConfigFileTest : public ::testing::Test {
protected:
    std::string testConfigPath;

    void SetUp() override {
        const ::testing::TestInfo* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        ASSERT_NE(info, nullptr);
        testConfigPath = (
            std::filesystem::temp_directory_path()
            / (std::string{"voxy_"} + info->test_suite_name()
                + "_" + info->name() + ".cfg"))
                             .string();
        std::error_code ignored;
        std::filesystem::remove(testConfigPath, ignored);
    }
    
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove(testConfigPath, ignored);
    }
    
    void writeTestConfig(const std::string& content) {
        std::ofstream file(testConfigPath);
        file << content;
    }
};

TEST(ConfigGameModeTest, AbsentModePreservesLegacyTitleRoutes) {
    const std::array<std::pair<const char*, GameMode>, 4> cases{{
        {"Voxy", GameMode::Terrain}, {"RIDGEBREAK", GameMode::Ridgebreak},
        {"LEGO SHORE", GameMode::LegoShore}, {"LEGO WORLD", GameMode::LegoWorld}}};
    for (const auto& [title, mode] : cases) {
        Config config; config.window.title = title;
        const auto result = resolveGameMode(config);
        EXPECT_TRUE(result.ready()); EXPECT_EQ(result.mode, mode);
    }
}

TEST(ConfigGameModeTest, ExplicitModeOverridesTitleAndRejectsUnknownOrConflict) {
    Config config; config.window.title = "RIDGEBREAK";
    config.game.mode = "salvage";
    auto result = resolveGameMode(config);
    EXPECT_TRUE(result.ready()); EXPECT_TRUE(result.legoTerrain());
    EXPECT_EQ(result.mode, GameMode::Salvage); EXPECT_EQ(result.terrainSizeHint(), 256u);
    for (const auto* name : {"terrain", "ridgebreak", "lego-shore", "lego-world"}) {
        config.game.mode = name; EXPECT_TRUE(resolveGameMode(config).ready());
    }
    for (const auto* name : {"", "SALVAGE", "salvag", "sandbox"}) {
        config.game.mode = name;
        EXPECT_EQ(resolveGameMode(config).status, GameModeStatus::UnknownMode);
    }
    config.game.mode = "salvage"; config.wreckwaterClient.server = "127.0.0.1";
    config.wreckwaterClient.presentFields = kWreckwaterClientServerField;
    EXPECT_EQ(resolveGameMode(config).status, GameModeStatus::ConflictingBootstrap);
    config.game.mode.reset();
    EXPECT_TRUE(resolveGameMode(config).ready()); // Existing WRECK validation remains separate.
}

TEST_F(ConfigFileTest, ExplicitModeRoundTripsAndMalformedModeNeverFallsBack) {
    Config original; original.game.mode = "salvage";
    ASSERT_TRUE(save(original, testConfigPath));
    const auto loaded = load(testConfigPath);
    EXPECT_EQ(loaded.game.mode, original.game.mode);
    EXPECT_EQ(resolveGameMode(loaded).mode, GameMode::Salvage);
    for (const auto* value : {"\"salvage", "\"salvage\" junk", "\"\"", "salvag"}) {
        writeTestConfig(std::string("[window]\ntitle=\"LEGO WORLD\"\n[game]\nmode=") + value + "\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status, GameModeStatus::UnknownMode);
    }
    original.game.mode.reset();
    ASSERT_TRUE(save(original, testConfigPath));
    EXPECT_FALSE(load(testConfigPath).game.mode.has_value());
}

TEST_F(ConfigFileTest, AssetFixtureRegistryRequiresExplicitSalvageAndRoundTrips) {
    Config original;
    original.game.mode = "salvage";
    original.game.assetFixtureRegistry = "data/salvage/fixture-pontoon-v2.json";
    original.game.assetFixtureGuides = "sockets";
    original.game.assetFixtureLod = "far";
    original.game.assetFixtureLighting = "filtered";
    original.game.assetFixtureAnchor = "water";
    ASSERT_TRUE(save(original, testConfigPath));
    auto loaded = load(testConfigPath);
    EXPECT_EQ(loaded.game.assetFixtureRegistry, original.game.assetFixtureRegistry);
    EXPECT_EQ(loaded.game.assetFixtureGuides, "sockets");
    EXPECT_EQ(loaded.game.assetFixtureLod, "far");
    EXPECT_EQ(loaded.game.assetFixtureLighting, "filtered");
    EXPECT_EQ(loaded.game.assetFixtureAnchor, "water");
    EXPECT_TRUE(resolveGameMode(loaded).ready());
    loaded.game.mode = "lego-world";
    EXPECT_EQ(resolveGameMode(loaded).status, GameModeStatus::InvalidAssetFixture);
    for (const auto* value : {"\"broken", "\"path\" junk", "\"\""}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=") + value + "\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status, GameModeStatus::InvalidAssetFixture);
    }
    for (const auto* value : {"\"broken", "\"sockets\" junk", "\"\"", "other"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_guides=") + value + "\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status, GameModeStatus::InvalidAssetFixture);
    }
    loaded.game.mode = "salvage";
    loaded.game.assetFixtureRegistry.reset();
    EXPECT_EQ(resolveGameMode(loaded).status, GameModeStatus::InvalidAssetFixture);
}

TEST_F(ConfigFileTest, AdditiveFixtureCatalogRoundTripsAndRequiresAnInstalledBase) {
    Config original;original.game.mode="salvage";
    original.game.assetFixtureRegistry="data/salvage/fixture-cove-r01.json";
    original.game.assetFixtureCatalog="data/salvage/cove-bricks-r02.json";
    ASSERT_TRUE(save(original,testConfigPath));auto loaded=load(testConfigPath);
    EXPECT_EQ(loaded.game.assetFixtureCatalog,original.game.assetFixtureCatalog);
    EXPECT_TRUE(resolveGameMode(loaded).ready());
    loaded.game.assetFixtureRegistry.reset();
    EXPECT_EQ(resolveGameMode(loaded).status,GameModeStatus::InvalidAssetFixture);
    for(const auto* value:{"\"broken","\"path\" junk","\"\""}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"base.json\"\nasset_fixture_catalog=")+value+"\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status,GameModeStatus::InvalidAssetFixture);
    }
    original.game.assetFixtureCatalog=std::string(4097,'a');
    EXPECT_EQ(resolveGameMode(original).status,GameModeStatus::InvalidAssetFixture);
    original.game.assetFixtureCatalog=std::string("a\0b",3);
    EXPECT_EQ(resolveGameMode(original).status,GameModeStatus::InvalidAssetFixture);
}

TEST_F(ConfigFileTest, AssetFixtureInitialDetailRejectsMalformedOrUnscopedSelection) {
    for (const auto* value : {"auto", "near", "middle", "far"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_lod=\"") + value + "\"\n");
        const auto loaded = load(testConfigPath);
        EXPECT_EQ(loaded.game.assetFixtureLod, value);
        EXPECT_TRUE(resolveGameMode(loaded).ready());
    }
    for (const auto* value : {"\"near", "\"far\" junk", "\"\"", "other", "1", "1.5"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_lod=") + value + "\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status, GameModeStatus::InvalidAssetFixture);
    }
    Config config;
    config.game.mode = "salvage";
    config.game.assetFixtureLod = "near";
    EXPECT_EQ(resolveGameMode(config).status, GameModeStatus::InvalidAssetFixture);
    config.game.assetFixtureLod = "auto";
    EXPECT_TRUE(resolveGameMode(config).ready());
}

TEST_F(ConfigFileTest, AssetFixtureLightingRejectsMalformedOrUnscopedSelection) {
    for (const auto* value : {"legacy", "filtered"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_lighting=\"") + value + "\"\n");
        const auto loaded=load(testConfigPath);
        EXPECT_EQ(loaded.game.assetFixtureLighting,value);
        EXPECT_TRUE(resolveGameMode(loaded).ready());
    }
    for (const auto* value : {"\"filtered", "\"filtered\" junk", "\"\"", "other", "1", "true"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_lighting=") + value + "\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status,GameModeStatus::InvalidAssetFixture);
    }
    Config config; config.game.mode="salvage"; config.game.assetFixtureLighting="filtered";
    EXPECT_EQ(resolveGameMode(config).status,GameModeStatus::InvalidAssetFixture);
    config.game.assetFixtureLighting="legacy";
    EXPECT_TRUE(resolveGameMode(config).ready());
}

TEST_F(ConfigFileTest, AssetFixtureAnchorRejectsMalformedOrUnscopedSelection) {
    for (const auto* value : {"inspection", "water"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_anchor=\"") + value + "\"\n");
        const auto loaded=load(testConfigPath);
        EXPECT_EQ(loaded.game.assetFixtureAnchor,value);
        EXPECT_TRUE(resolveGameMode(loaded).ready());
    }
    for (const auto* value : {"\"water", "\"water\" junk", "\"\"", "other", "1", "true"}) {
        writeTestConfig(std::string("[game]\nmode=\"salvage\"\nasset_fixture_registry=\"x.json\"\nasset_fixture_anchor=") + value + "\n");
        EXPECT_EQ(resolveGameMode(load(testConfigPath)).status,GameModeStatus::InvalidAssetFixture);
    }
    Config config; config.game.mode="salvage"; config.game.assetFixtureAnchor="water";
    EXPECT_EQ(resolveGameMode(config).status,GameModeStatus::InvalidAssetFixture);
    config.game.assetFixtureAnchor="inspection";
    EXPECT_TRUE(resolveGameMode(config).ready());
}

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

TEST_F(ConfigFileTest,
       LoadsCompleteWreckwaterBootstrapWithoutPrintableKeyState) {
    writeTestConfig(R"(
[wreckwater_client]
server = "10.0.0.12"
port = 45000
peer = 3
key = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
session = 101
match = 202
world = 303
world_epoch = 4
authority_epoch = 5
)");

    const Config config = load(testConfigPath);
    EXPECT_EQ(
        validateWreckwaterClientConfig(config.wreckwaterClient),
        WreckwaterClientConfigStatus::Ready);
    EXPECT_EQ(config.wreckwaterClient.server, "10.0.0.12");
    EXPECT_EQ(
        config.wreckwaterClient.authenticationKey[1],
        std::byte{0x01});

    ASSERT_TRUE(save(config, testConfigPath));
    std::ifstream saved(testConfigPath);
    ASSERT_TRUE(saved.is_open());
    std::string contents;
    for (std::string line; std::getline(saved, line);) {
        contents.append(line);
        contents.push_back('\n');
    }
    EXPECT_EQ(
        contents.find("[wreckwater_client]"),
        std::string::npos);
    EXPECT_EQ(
        contents.find(
            "000102030405060708090a0b0c0d0e0f"),
        std::string::npos);
}

TEST_F(ConfigFileTest,
       CommandLineCanCompleteWreckwaterFileAtomically) {
    writeTestConfig(R"(
[wreckwater_client]
port = 45000
peer = 1
key = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
session = 101
match = 202
world = 303
world_epoch = 4
authority_epoch = 5
)");
    CommandLineArgs args;
    args.wreckwaterClient.server = "localhost";
    args.wreckwaterClient.presentFields =
        kWreckwaterClientServerField;

    const Config config = load(testConfigPath, args);
    EXPECT_EQ(
        validateWreckwaterClientConfig(config.wreckwaterClient),
        WreckwaterClientConfigStatus::Ready);
    EXPECT_EQ(config.wreckwaterClient.server, "localhost");
}

TEST_F(ConfigFileTest,
       ZeroWreckwaterKeyAndIdentityFailValidation) {
    WreckwaterClientConfig config;
    config.presentFields = kWreckwaterClientRequiredFields;
    config.server = "localhost";
    config.port = 45'000u;
    config.peerId = 1u;
    config.sessionId = 1u;
    config.matchId = 2u;
    config.worldId = 3u;
    config.worldEpoch = 4u;
    config.authorityEpoch = 5u;
    EXPECT_EQ(
        validateWreckwaterClientConfig(config),
        WreckwaterClientConfigStatus::InvalidKey);

    config.authenticationKey[0] = std::byte{0x01};
    config.sessionId = 0u;
    EXPECT_EQ(
        validateWreckwaterClientConfig(config),
        WreckwaterClientConfigStatus::InvalidIdentity);
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

[physics]
backend = "box3d"
gpu_max_bodies = 200000
broad_phase_cell_size = 8.0
allow_cpu_fallback = false
jolt_job_system = "thread_pool"
jolt_worker_threads = 3
box3d_worker_threads = 4

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
    EXPECT_EQ(config.physics.backend, "box3d");
    EXPECT_EQ(config.physics.gpuMaxBodies, 200000);
    EXPECT_FLOAT_EQ(config.physics.broadPhaseCellSize, 8.0f);
    EXPECT_FALSE(config.physics.allowCpuFallback);
    EXPECT_EQ(config.physics.joltJobSystem, "thread_pool");
    EXPECT_EQ(config.physics.joltWorkerThreads, 3);
    EXPECT_EQ(config.physics.box3dWorkerThreads, 4);
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
    args.physicsBackend = "webgpu";
    args.width = 1920;
    args.vsync = false;
    
    auto config = load(testConfigPath, args);
    
    EXPECT_EQ(config.render.path, "triangle");  // Overridden
    EXPECT_EQ(config.physics.backend, "webgpu");
    EXPECT_EQ(config.window.width, 1920);        // Overridden
    EXPECT_EQ(config.window.height, 600);        // From file
    EXPECT_FALSE(config.render.vsync);
}

TEST_F(ConfigFileTest, CommandLineBenchmarkEnablesAutomation) {
    CommandLineArgs args;
    args.benchmark = true;
    args.benchmarkBodies = 400;
    args.benchmarkMinimumFps = 400.0f;
    args.benchmarkFixedHz = 400.0f;

    auto config = load(testConfigPath, args);

    EXPECT_TRUE(config.automation.benchmark);
    EXPECT_EQ(config.automation.benchmarkBodies, 400);
    EXPECT_FLOAT_EQ(config.automation.benchmarkMinimumFps, 400.0f);
    EXPECT_FLOAT_EQ(config.automation.benchmarkFixedHz, 400.0f);
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
    original.camera.eyeHeight = 2.25f;
    original.lighting.ambientIntensity = 0.65f;
    original.debug.logLevel = "trace";
    original.physics.backend = "box3d";
    original.physics.gpuMaxBodies = 200000;
    original.physics.broadPhaseCellSize = 2.0f;
    original.physics.allowCpuFallback = false;
    original.physics.joltJobSystem = "thread_pool";
    original.physics.joltWorkerThreads = 6;
    original.physics.box3dWorkerThreads = 8;
    
    EXPECT_TRUE(save(original, testConfigPath));
    
    auto loaded = load(testConfigPath);
    
    EXPECT_EQ(loaded.render.path, original.render.path);
    EXPECT_FLOAT_EQ(loaded.render.resolutionScale, original.render.resolutionScale);
    EXPECT_EQ(loaded.window.width, original.window.width);
    EXPECT_EQ(loaded.window.height, original.window.height);
    EXPECT_FLOAT_EQ(loaded.camera.fov, original.camera.fov);
    EXPECT_FLOAT_EQ(loaded.camera.eyeHeight, original.camera.eyeHeight);
    EXPECT_FLOAT_EQ(loaded.lighting.ambientIntensity, original.lighting.ambientIntensity);
    EXPECT_EQ(loaded.debug.logLevel, original.debug.logLevel);
    EXPECT_EQ(loaded.physics, original.physics);
}

TEST_F(ConfigFileTest, InvalidWaterNumbersKeepCurrentDefaults) {
    writeTestConfig(R"(
[water]
reflection_strength = invalid
shore_fade = invalid
)");

    const auto config = load(testConfigPath);

    EXPECT_FLOAT_EQ(config.water.reflectionStrength, WaterConfig{}.reflectionStrength);
    EXPECT_FLOAT_EQ(config.water.shoreFade, WaterConfig{}.shoreFade);
}

TEST_F(ConfigFileTest, InvalidBroadPhaseCellSizesKeepCurrentValue) {
    writeTestConfig(R"(
[physics]
broad_phase_cell_size = 8.0
broad_phase_cell_size = 3.0
)");

    const auto config = load(testConfigPath);

    EXPECT_FLOAT_EQ(config.physics.broadPhaseCellSize, 8.0f);
}

TEST_F(ConfigFileTest, InvalidDuplicateValuesKeepEarlierValidValues) {
    writeTestConfig(R"(
[render]
resolution_scale = 0.75
resolution_scale = invalid
vsync = false
vsync = invalid

[window]
width = 1600
width = invalid
)"
    );

    const auto config = load(testConfigPath);

    EXPECT_FLOAT_EQ(config.render.resolutionScale, 0.75f);
    EXPECT_FALSE(config.render.vsync);
    EXPECT_EQ(config.window.width, 1600);
}

TEST_F(ConfigFileTest, MalformedVectorsAndQuotesPreserveEarlierValues) {
    writeTestConfig(R"(
[lighting]
sun_direction = [0.1, 0.2, 0.3]
sun_direction = [1.0, 2.0]
sun_direction = [1.0, 2.0, 3.0, 4.0]
sun_direction = [1.0, nan, 3.0]

[window]
title = "Working title"
title = "unterminated
title = "also rejected" trailing-junk
)");

    const auto config = load(testConfigPath);
    EXPECT_EQ(
        config.lighting.sunDirection,
        (std::array<float, 3>{0.1f, 0.2f, 0.3f}));
    EXPECT_EQ(config.window.title, "Working title");
}

TEST_F(ConfigFileTest, SaveAndReloadEscapedStrings) {
    Config original;
    original.render.path = "ray\"cast";
    original.terrain.heightmap = R"(C:\terrain\"quoted\"#1.ldh)";
    original.window.title =
        "Voxy \"Nightly\"\n[window]\nwidth = 1\r\t#1";
    original.debug.logLevel =
        std::string{"tr\0ace", 7u};

    ASSERT_TRUE(save(original, testConfigPath));
    const auto loaded = load(testConfigPath);

    EXPECT_EQ(loaded.render.path, original.render.path);
    EXPECT_EQ(loaded.terrain.heightmap, original.terrain.heightmap);
    EXPECT_EQ(loaded.window.title, original.window.title);
    EXPECT_EQ(loaded.window.width, original.window.width);
    EXPECT_EQ(loaded.debug.logLevel, original.debug.logLevel);
}

TEST_F(ConfigFileTest, ReflectionStrengthIsClampedToDocumentedRange) {
    writeTestConfig(R"(
[water]
reflection_strength = 1.5
)");
    EXPECT_FLOAT_EQ(load(testConfigPath).water.reflectionStrength, 1.0f);

    writeTestConfig(R"(
[water]
reflection_strength = -0.25
)");
    EXPECT_FLOAT_EQ(load(testConfigPath).water.reflectionStrength, 0.0f);
}

TEST_F(ConfigFileTest, SaveReportsDeviceWriteFailure) {
    if (!std::filesystem::exists("/dev/full")) GTEST_SKIP();
    EXPECT_FALSE(save(Config{}, "/dev/full"));
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
