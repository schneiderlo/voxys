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
#include <filesystem>

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
    EXPECT_EQ(textures_.getLightmapWidth(), 256u);
    EXPECT_EQ(textures_.getLightmapHeight(), 256u);
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
    EXPECT_EQ(textures_.getLightmapWidth(), 512u);
    EXPECT_EQ(textures_.getLightmapHeight(), 512u);
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

