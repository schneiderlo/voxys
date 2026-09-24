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
#include <array>
#include <filesystem>
#include <limits>

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
    EXPECT_EQ(config.materialDirectory, "data/materials");
    EXPECT_EQ(config.placeholderWidth, 256u);
    EXPECT_EQ(config.placeholderHeight, 256u);
}

TEST(TerrainTextureGenerationTest, RejectsZeroAndOverflowingDimensions) {
    EXPECT_TRUE(terrain::generateTerrainColorData(0u, 32u).empty());
    EXPECT_TRUE(terrain::generateWhiteLightmapData(32u, 0u).empty());
    EXPECT_TRUE(terrain::generateTerrainColorData(8'193u, 1u).empty());
    EXPECT_TRUE(terrain::generateWhiteLightmapData(1u, 8'193u).empty());
    EXPECT_TRUE(terrain::generateTerrainColorData(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max()).empty());
    EXPECT_TRUE(terrain::generateWhiteLightmapData(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max()).empty());
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
    EXPECT_EQ(
        wgpuTextureGetMipLevelCount(textures_.getAlbedoTexture()),
        gpu::calculateMipLevelCount(
            textures_.getAlbedoWidth(), textures_.getAlbedoHeight()));
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

TEST_F(TerrainTexturesTest, MaterialArraysAreCompleteAndMipmapped) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }

    ASSERT_TRUE(textures_.init(
        gpuContext_.getDevice(), gpuContext_.getQueue()));
    ASSERT_NE(textures_.getMaterialAlbedoTexture(), nullptr);
    ASSERT_NE(textures_.getMaterialAlbedoView(), nullptr);
    ASSERT_NE(textures_.getMaterialNormalRoughnessTexture(), nullptr);
    ASSERT_NE(textures_.getMaterialNormalRoughnessView(), nullptr);
    EXPECT_EQ(
        wgpuTextureGetFormat(textures_.getMaterialAlbedoTexture()),
        WGPUTextureFormat_RGBA8UnormSrgb);
    EXPECT_EQ(
        wgpuTextureGetFormat(
            textures_.getMaterialNormalRoughnessTexture()),
        WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(
        wgpuTextureGetDepthOrArrayLayers(
            textures_.getMaterialAlbedoTexture()),
        terrain::TerrainTextures::kMaterialLayerCount);
    EXPECT_EQ(
        wgpuTextureGetMipLevelCount(
            textures_.getMaterialAlbedoTexture()),
        gpu::calculateMipLevelCount(
            textures_.getMaterialWidth(), textures_.getMaterialHeight()));
    EXPECT_EQ(
        wgpuTextureGetMipLevelCount(
            textures_.getMaterialNormalRoughnessTexture()),
        wgpuTextureGetMipLevelCount(
            textures_.getMaterialAlbedoTexture()));
}

TEST_F(TerrainTexturesTest, MissingMaterialPackUsesCompleteFallback) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }

    terrain::TerrainTextureConfig config;
    config.placeholderWidth = 8u;
    config.placeholderHeight = 8u;
    config.materialDirectory =
        "data/materials/intentionally_missing_test_pack";
    ASSERT_TRUE(textures_.init(
        gpuContext_.getDevice(), gpuContext_.getQueue(), config));

    EXPECT_EQ(textures_.getMaterialWidth(), 64u);
    EXPECT_EQ(textures_.getMaterialHeight(), 64u);
    EXPECT_EQ(
        wgpuTextureGetDepthOrArrayLayers(
            textures_.getMaterialAlbedoTexture()),
        terrain::TerrainTextures::kMaterialLayerCount);
    EXPECT_EQ(
        wgpuTextureGetMipLevelCount(
            textures_.getMaterialAlbedoTexture()),
        gpu::calculateMipLevelCount(64u, 64u));
}

TEST_F(TerrainTexturesTest, SeabedOnlyNeedsJustTheSandAlbedo) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }
    const std::filesystem::path sand =
        "data/materials/Ground054_1K-JPG_Color.jpg";
    if (!std::filesystem::exists(sand)) {
        GTEST_SKIP() << "terrain material pack not available";
    }
    // A web build ships only the sand albedo for LEGO terrain.
    const auto directory = std::filesystem::path(::testing::TempDir())
        / "seabed_only_material_pack";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    std::filesystem::copy_file(sand, directory / sand.filename());

    terrain::TerrainTextureConfig config;
    config.placeholderWidth = 8u;
    config.placeholderHeight = 8u;
    config.materialDirectory = directory;
    config.seabedAlbedoOnly = true;
    ASSERT_TRUE(textures_.init(
        gpuContext_.getDevice(), gpuContext_.getQueue(), config));
    std::filesystem::remove_all(directory);

    EXPECT_GT(textures_.getMaterialWidth(), 64u)
        << "the sand layer must keep its source resolution";
    EXPECT_EQ(
        wgpuTextureGetDepthOrArrayLayers(
            textures_.getMaterialAlbedoTexture()),
        terrain::TerrainTextures::kMaterialLayerCount);
    EXPECT_EQ(
        wgpuTextureGetMipLevelCount(
            textures_.getMaterialNormalRoughnessTexture()),
        gpu::calculateMipLevelCount(
            textures_.getMaterialWidth(), textures_.getMaterialHeight()));
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

TEST_F(TerrainTexturesTest, RejectedOversizedReplacementPreservesTextures) {
    if (!gpuContextInitialized_) {
        GTEST_SKIP() << "GPU context not available";
    }

    ASSERT_TRUE(textures_.init(
        gpuContext_.getDevice(), gpuContext_.getQueue()));

    const auto albedoTexture = textures_.getAlbedoTexture();
    const auto albedoView = textures_.getAlbedoView();
    const auto lightmapTexture = textures_.getLightmapTexture();
    const auto lightmapView = textures_.getLightmapView();

    EXPECT_FALSE(textures_.createPlaceholderAlbedo(8'193u, 1u));
    EXPECT_FALSE(textures_.createWhiteLightmap(1u, 8'193u));

    EXPECT_EQ(textures_.getAlbedoTexture(), albedoTexture);
    EXPECT_EQ(textures_.getAlbedoView(), albedoView);
    EXPECT_EQ(textures_.getLightmapTexture(), lightmapTexture);
    EXPECT_EQ(textures_.getLightmapView(), lightmapView);
    EXPECT_TRUE(textures_.isInitialized());
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

TEST(TerrainTextureUtilTest, AlbedoMipAveragesSrgbInLinearLight) {
    // Two black and two white pixels have a linear-light midpoint which
    // encodes to roughly sRGB 188, not the gamma-incorrect value 128.
    const std::array<uint8_t, 16> checker{
        0u, 0u, 0u, 0u,
        255u, 255u, 255u, 64u,
        255u, 255u, 255u, 128u,
        0u, 0u, 0u, 255u,
    };

    const auto mip = terrain::downsampleTerrainAlbedoSrgb(checker, 2u, 2u);

    ASSERT_EQ(mip.size(), 4u);
    EXPECT_NEAR(mip[0], 188, 1);
    EXPECT_NEAR(mip[1], 188, 1);
    EXPECT_NEAR(mip[2], 188, 1);
    EXPECT_EQ(mip[3], 112u);
}

TEST(TerrainTextureUtilTest, AlbedoMipKeepsOddDimensionEdges) {
    // A 3x1 mip becomes 1x1. Area filtering must include the final blue texel
    // instead of silently dropping it.
    const std::array<uint8_t, 12> colors{
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
    };

    const auto mip = terrain::downsampleTerrainAlbedoSrgb(colors, 3u, 1u);

    ASSERT_EQ(mip.size(), 4u);
    EXPECT_NEAR(mip[0], 156, 1);
    EXPECT_NEAR(mip[1], 156, 1);
    EXPECT_NEAR(mip[2], 156, 1);
    EXPECT_EQ(mip[3], 255u);
}

TEST(TerrainTextureUtilTest, AlbedoMipRejectsInvalidOrTerminalInput) {
    const std::array<uint8_t, 4> pixel{1u, 2u, 3u, 4u};
    EXPECT_TRUE(
        terrain::downsampleTerrainAlbedoSrgb(pixel, 1u, 1u).empty());
    EXPECT_TRUE(
        terrain::downsampleTerrainAlbedoSrgb({}, 2u, 2u).empty());
}

TEST(TerrainTextureUtilTest, NormalRoughnessMipRenormalizesFlatNormal) {
    const std::array<uint8_t, 16> flat{
        128u, 128u, 255u, 128u,
        128u, 128u, 255u, 128u,
        128u, 128u, 255u, 128u,
        128u, 128u, 255u, 128u,
    };

    const auto mip =
        terrain::downsampleTerrainNormalRoughness(flat, 2u, 2u);

    ASSERT_EQ(mip.size(), 4u);
    EXPECT_NEAR(mip[0], 128, 1);
    EXPECT_NEAR(mip[1], 128, 1);
    EXPECT_NEAR(mip[2], 255, 1);
    EXPECT_NEAR(mip[3], 128, 1);
}

TEST(TerrainTextureUtilTest, NormalVarianceRaisesMipRoughness) {
    // Opposing high-frequency normals average to low length. The filtered mip
    // must become rougher instead of producing a distant specular sparkle.
    const std::array<uint8_t, 16> opposed{
        252u, 128u, 153u, 0u,
        3u, 127u, 153u, 0u,
        252u, 128u, 153u, 0u,
        3u, 127u, 153u, 0u,
    };

    const auto mip =
        terrain::downsampleTerrainNormalRoughness(opposed, 2u, 2u);

    ASSERT_EQ(mip.size(), 4u);
    EXPECT_NEAR(mip[0], 128, 2);
    EXPECT_NEAR(mip[1], 128, 2);
    EXPECT_GT(mip[2], 250u);
    EXPECT_GT(mip[3], 150u);
}

TEST(TerrainTextureUtilTest, NormalRoughnessMipRejectsInvalidInput) {
    const std::array<uint8_t, 4> pixel{128u, 128u, 255u, 200u};
    EXPECT_TRUE(
        terrain::downsampleTerrainNormalRoughness(
            pixel, 1u, 1u).empty());
    EXPECT_TRUE(
        terrain::downsampleTerrainNormalRoughness(
            {}, 2u, 2u).empty());
}

} // namespace voxy
