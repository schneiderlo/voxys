// ═══════════════════════════════════════════════════════════════════════════════
// test_terrain_textures.cpp - Unit tests for TerrainTextures
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for the terrain texture loading and management class. These tests verify:
//   - TerrainTextures initialization and shutdown
//   - Placeholder texture generation
//   - Texture accessors
//   - Sampler creation
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "terrain/textures.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════════════════════════

class TerrainTexturesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try to initialize GPU context for rendering tests
        gpuContextInitialized_ = gpuContext_.initHeadless();
    }
    
    void TearDown() override {
        textures_.shutdown();
        
        if (gpuContextInitialized_) {
            gpuContext_.shutdown();
        }
    }
    
    gpu::Context gpuContext_;
    terrain::TerrainTextures textures_;
    bool gpuContextInitialized_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Configuration Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainTexturesTest, DefaultConfigValues) {
    auto config = terrain::TerrainTextureConfig::defaults();
    
    EXPECT_TRUE(config.albedoPath.empty());
    EXPECT_TRUE(config.lightmapPath.empty());
    EXPECT_EQ(config.placeholderWidth, 256u);
    EXPECT_EQ(config.placeholderHeight, 256u);
    EXPECT_TRUE(config.heightSamples.empty());
    EXPECT_EQ(config.heightmapWidth, 0u);
    EXPECT_EQ(config.heightmapHeight, 0u);
    EXPECT_FLOAT_EQ(config.heightScale, 1.0f);
    EXPECT_FLOAT_EQ(config.cellScale, 1.0f);
    EXPECT_TRUE(config.minecraftStyleEnhancement);
    EXPECT_EQ(config.generatedLightmapMaxSize, 2048u);
    EXPECT_TRUE(config.enableGeneratedTextureCache);
    EXPECT_EQ(config.generatedTextureCacheDir, std::filesystem::path("data/generated/texture_cache"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Initialization Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainTexturesTest, NotInitializedByDefault) {
    EXPECT_FALSE(textures_.isInitialized());
}

TEST_F(TerrainTexturesTest, InitWithNullDeviceFails) {
    EXPECT_FALSE(textures_.init(nullptr, nullptr));
}

TEST_F(TerrainTexturesTest, InitializationSucceeds) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    EXPECT_TRUE(textures_.init(device, queue));
    EXPECT_TRUE(textures_.isInitialized());
}

TEST_F(TerrainTexturesTest, DoubleInitFails) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    EXPECT_FALSE(textures_.init(device, queue));
}

TEST_F(TerrainTexturesTest, ShutdownReleasesResources) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    textures_.shutdown();
    
    EXPECT_FALSE(textures_.isInitialized());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture Creation Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainTexturesTest, AlbedoTextureCreated) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    
    EXPECT_NE(textures_.getAlbedoTexture(), nullptr);
    EXPECT_NE(textures_.getAlbedoView(), nullptr);
}

TEST_F(TerrainTexturesTest, LightmapTextureCreated) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    
    EXPECT_NE(textures_.getLightmapTexture(), nullptr);
    EXPECT_NE(textures_.getLightmapView(), nullptr);
}

TEST_F(TerrainTexturesTest, SamplerCreated) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    
    EXPECT_NE(textures_.getSampler(), nullptr);
}

TEST_F(TerrainTexturesTest, DefaultTextureDimensions) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    
    EXPECT_EQ(textures_.getAlbedoWidth(), 256u);
    EXPECT_EQ(textures_.getAlbedoHeight(), 256u);
    EXPECT_EQ(textures_.getLightmapWidth(), 1u);
    EXPECT_EQ(textures_.getLightmapHeight(), 1u);
}

TEST_F(TerrainTexturesTest, CustomPlaceholderDimensions) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    terrain::TerrainTextureConfig config;
    config.placeholderWidth = 512;
    config.placeholderHeight = 512;
    
    ASSERT_TRUE(textures_.init(device, queue, config));
    
    EXPECT_EQ(textures_.getAlbedoWidth(), 512u);
    EXPECT_EQ(textures_.getAlbedoHeight(), 512u);
    EXPECT_EQ(textures_.getLightmapWidth(), 1u);
    EXPECT_EQ(textures_.getLightmapHeight(), 1u);
}

TEST_F(TerrainTexturesTest, GeneratedTextureCacheCreatesAndReusesFiles) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto cacheDir = std::filesystem::temp_directory_path() /
                          ("voxy_texture_cache_test_" + std::to_string(unique));

    std::vector<uint16_t> samples(32u * 32u, 36044u);
    terrain::TerrainTextureConfig config;
    config.placeholderWidth = 32;
    config.placeholderHeight = 32;
    config.heightSamples = samples;
    config.heightmapWidth = 32;
    config.heightmapHeight = 32;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;
    config.generatedLightmapMaxSize = 16;
    config.generatedTextureCacheDir = cacheDir;

    auto countCacheFiles = [&]() {
        size_t count = 0;
        if (!std::filesystem::exists(cacheDir)) {
            return count;
        }
        for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
            if (entry.is_regular_file()) {
                count++;
            }
        }
        return count;
    };

    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();

    ASSERT_TRUE(textures_.init(device, queue, config));
    EXPECT_EQ(textures_.getAlbedoWidth(), 32u);
    EXPECT_EQ(textures_.getAlbedoHeight(), 32u);
    EXPECT_EQ(textures_.getLightmapWidth(), 16u);
    EXPECT_EQ(textures_.getLightmapHeight(), 16u);
    EXPECT_EQ(textures_.getNormalWidth(), 16u);
    EXPECT_EQ(textures_.getNormalHeight(), 16u);
    textures_.shutdown();

    EXPECT_EQ(countCacheFiles(), 3u);

    terrain::TerrainTextures cachedTextures;
    ASSERT_TRUE(cachedTextures.init(device, queue, config));
    EXPECT_EQ(cachedTextures.getAlbedoWidth(), 32u);
    EXPECT_EQ(cachedTextures.getAlbedoHeight(), 32u);
    EXPECT_EQ(cachedTextures.getLightmapWidth(), 16u);
    EXPECT_EQ(cachedTextures.getLightmapHeight(), 16u);
    EXPECT_EQ(cachedTextures.getNormalWidth(), 16u);
    EXPECT_EQ(cachedTextures.getNormalHeight(), 16u);
    cachedTextures.shutdown();

    std::error_code ec;
    std::filesystem::remove_all(cacheDir, ec);
}

TEST_F(TerrainTexturesTest, GeneratedAlbedoCacheContainsBiomeMaterialVariation) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto cacheDir = std::filesystem::temp_directory_path() /
                          ("voxy_texture_material_test_" + std::to_string(unique));

    std::vector<uint16_t> samples(48u * 48u, 36044u);
    for (uint32_t y = 0; y < 48; ++y) {
        for (uint32_t x = 0; x < 48; ++x) {
            if (x > 32 && y > 28) {
                samples[y * 48u + x] = 52000u;
            } else if (x < 12 && y < 18) {
                samples[y * 48u + x] = 31000u;
            }
        }
    }

    terrain::TerrainTextureConfig config;
    config.placeholderWidth = 48;
    config.placeholderHeight = 48;
    config.heightSamples = samples;
    config.heightmapWidth = 48;
    config.heightmapHeight = 48;
    config.heightScale = 80.0f;
    config.cellScale = 1.0f;
    config.generatedLightmapMaxSize = 16;
    config.generatedTextureCacheDir = cacheDir;

    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    ASSERT_TRUE(textures_.init(device, queue, config));
    textures_.shutdown();

    std::filesystem::path albedoPath;
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
        if (entry.path().extension() == ".rgba8" &&
            entry.path().filename().string().find("albedo") != std::string::npos) {
            albedoPath = entry.path();
            break;
        }
    }
    ASSERT_FALSE(albedoPath.empty()) << "Expected generated albedo cache file";

    std::ifstream file(albedoPath, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string marker = "END\n";
    const auto payloadOffset = contents.find(marker);
    ASSERT_NE(payloadOffset, std::string::npos);
    const size_t pixelOffset = payloadOffset + marker.size();
    ASSERT_GE(contents.size(), pixelOffset + 48u * 48u * 4u);

    std::set<uint32_t> uniqueColors;
    for (size_t i = pixelOffset; i + 3 < contents.size(); i += 4) {
        const auto r = static_cast<uint8_t>(contents[i + 0]);
        const auto g = static_cast<uint8_t>(contents[i + 1]);
        const auto b = static_cast<uint8_t>(contents[i + 2]);
        uniqueColors.insert((static_cast<uint32_t>(r) << 16u) |
                            (static_cast<uint32_t>(g) << 8u) |
                            static_cast<uint32_t>(b));
    }
    EXPECT_GT(uniqueColors.size(), 32u)
        << "Biome/material albedo should contain varied surface colors";

    std::error_code ec;
    std::filesystem::remove_all(cacheDir, ec);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Move Semantics Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(TerrainTexturesTest, MoveConstructor) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    
    terrain::TerrainTextures moved(std::move(textures_));
    
    EXPECT_FALSE(textures_.isInitialized());
    EXPECT_TRUE(moved.isInitialized());
    
    moved.shutdown();
}

TEST_F(TerrainTexturesTest, MoveAssignment) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    
    auto device = gpuContext_.getDevice();
    auto queue = gpuContext_.getQueue();
    
    ASSERT_TRUE(textures_.init(device, queue));
    
    terrain::TerrainTextures other;
    other = std::move(textures_);
    
    EXPECT_FALSE(textures_.isInitialized());
    EXPECT_TRUE(other.isInitialized());
    
    other.shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TerrainTextureUtilTest, GenerateTerrainColorData) {
    auto data = terrain::generateTerrainColorData(64, 64);
    
    // Should be RGBA8 format
    EXPECT_EQ(data.size(), 64u * 64u * 4u);
    
    // Check that alpha is always 255
    for (size_t i = 3; i < data.size(); i += 4) {
        EXPECT_EQ(data[i], 255u) << "Alpha should be 255 at index " << i;
    }
    
    // Check that RGB values are reasonable (green-ish terrain)
    // Sample a few pixels to verify they're in expected range
    for (size_t i = 0; i < data.size(); i += 4) {
        EXPECT_GE(data[i], 0u);      // R
        EXPECT_LE(data[i], 255u);
        EXPECT_GE(data[i + 1], 0u);  // G
        EXPECT_LE(data[i + 1], 255u);
        EXPECT_GE(data[i + 2], 0u);  // B
        EXPECT_LE(data[i + 2], 255u);
    }
}

TEST(TerrainTextureUtilTest, GenerateWhiteLightmapData) {
    auto data = terrain::generateWhiteLightmapData(64, 64);
    
    // Should be R8 format
    EXPECT_EQ(data.size(), 64u * 64u);
    
    // All values should be 255 (white = full light visibility)
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], 255u) << "Lightmap value should be 255 at index " << i;
    }
}

TEST(TerrainTextureUtilTest, GenerateTerrainLightmapFallsBackToWhiteWithoutHeightSamples) {
    terrain::TerrainTextureConfig config;
    auto data = terrain::generateTerrainLightmapData(8, 8, config);

    ASSERT_EQ(data.size(), 64u);
    for (uint8_t value : data) {
        EXPECT_EQ(value, 255u);
    }
}

TEST(TerrainTextureUtilTest, GenerateTerrainLightmapUsesHeightSamples) {
    std::vector<uint16_t> samples(16u * 16u, 32768u);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            if (x > 8 && y > 8) {
                samples[y * 16u + x] = 52000u;
            }
        }
    }

    terrain::TerrainTextureConfig config;
    config.heightSamples = samples;
    config.heightmapWidth = 16;
    config.heightmapHeight = 16;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;

    auto data = terrain::generateTerrainLightmapData(16, 16, config);
    ASSERT_EQ(data.size(), 16u * 16u);

    bool sawNonWhite = false;
    for (uint8_t value : data) {
        if (value < 255u) {
            sawNonWhite = true;
            break;
        }
    }
    EXPECT_TRUE(sawNonWhite);
}

TEST(TerrainTextureUtilTest, GenerateTerrainColorDataDifferentSizes) {
    // Test various sizes
    auto data128 = terrain::generateTerrainColorData(128, 128);
    EXPECT_EQ(data128.size(), 128u * 128u * 4u);
    
    auto data512 = terrain::generateTerrainColorData(512, 512);
    EXPECT_EQ(data512.size(), 512u * 512u * 4u);
    
    // Non-square sizes
    auto dataRect = terrain::generateTerrainColorData(256, 128);
    EXPECT_EQ(dataRect.size(), 256u * 128u * 4u);
}

TEST(TerrainTextureUtilTest, GenerateWhiteLightmapDataDifferentSizes) {
    // Test various sizes
    auto data128 = terrain::generateWhiteLightmapData(128, 128);
    EXPECT_EQ(data128.size(), 128u * 128u);
    
    auto data512 = terrain::generateWhiteLightmapData(512, 512);
    EXPECT_EQ(data512.size(), 512u * 512u);
    
    // Non-square sizes
    auto dataRect = terrain::generateWhiteLightmapData(256, 128);
    EXPECT_EQ(dataRect.size(), 256u * 128u);
}

} // namespace voxy
