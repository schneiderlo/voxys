// ═══════════════════════════════════════════════════════════════════════════════
// test_heightmap.cpp - Unit tests for Heightmap Loading
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for loading raw and PNG heightmaps, sampling, and utility functions.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "terrain/heightmap.hpp"
#include "terrain/mip_generator.hpp"

#include <cmath>
#include <cstring>
#include <vector>

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// Error String Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapErrorTest, ErrorToString) {
    EXPECT_EQ(errorToString(HeightmapError::None), "No error");
    EXPECT_EQ(errorToString(HeightmapError::FileNotFound), "File not found");
    EXPECT_EQ(errorToString(HeightmapError::ReadError), "Failed to read file");
    EXPECT_EQ(errorToString(HeightmapError::InvalidFormat), "Invalid or unrecognized format");
    EXPECT_EQ(errorToString(HeightmapError::InvalidDimensions), "Invalid dimensions");
    EXPECT_EQ(errorToString(HeightmapError::DecodeFailed), "Image decoding failed");
    EXPECT_EQ(errorToString(HeightmapError::Not16Bit), "PNG is not 16-bit depth");
    EXPECT_EQ(errorToString(HeightmapError::TextureCreationFailed), "GPU texture creation failed");
    EXPECT_EQ(errorToString(HeightmapError::UploadFailed), "GPU texture upload failed");
}

// ═══════════════════════════════════════════════════════════════════════════════
// LoadResult Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LoadResultTest, DefaultConstruction) {
    LoadResult result;
    
    EXPECT_TRUE(result.data.empty());
    EXPECT_EQ(result.width, 0u);
    EXPECT_EQ(result.height, 0u);
    EXPECT_DOUBLE_EQ(result.loadTimeMs, 0.0);
    EXPECT_EQ(result.sampleCount(), 0u);
    EXPECT_EQ(result.sizeBytes(), 0u);
    EXPECT_FALSE(result.isValid());
}

TEST(LoadResultTest, ValidResult) {
    LoadResult result;
    result.width = 64;
    result.height = 32;
    result.data.resize(64 * 32, 1000);
    
    EXPECT_EQ(result.sampleCount(), 64u * 32u);
    EXPECT_EQ(result.sizeBytes(), 64u * 32u * sizeof(uint16_t));
    EXPECT_TRUE(result.isValid());
}

TEST(LoadResultTest, Sample) {
    LoadResult result;
    result.width = 4;
    result.height = 4;
    result.data.resize(16);
    
    // Fill with index values
    for (size_t i = 0; i < 16; i++) {
        result.data[i] = static_cast<uint16_t>(i * 1000);
    }
    
    EXPECT_EQ(result.sample(0, 0), 0u);
    EXPECT_EQ(result.sample(1, 0), 1000u);
    EXPECT_EQ(result.sample(0, 1), 4000u);
    EXPECT_EQ(result.sample(3, 3), 15000u);
    
    // Test clamping
    EXPECT_EQ(result.sample(10, 10), 15000u);
}

TEST(LoadResultTest, SampleNormalized) {
    LoadResult result;
    result.width = 2;
    result.height = 1;
    result.data = {0, 65535};
    
    EXPECT_FLOAT_EQ(result.sampleNormalized(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(result.sampleNormalized(1, 0), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Result Type Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ResultTest, VoidResultSuccess) {
    VoidResult result;
    
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(result.has_error());
    EXPECT_TRUE(static_cast<bool>(result));
}

TEST(ResultTest, VoidResultError) {
    VoidResult result(HeightmapError::FileNotFound);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.has_error());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error(), HeightmapError::FileNotFound);
}

TEST(ResultTest, ValueResultSuccess) {
    Result<int, HeightmapError> result(42);
    
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(result.has_error());
    EXPECT_EQ(result.value(), 42);
}

TEST(ResultTest, ValueResultError) {
    Result<int, HeightmapError> result(HeightmapError::ReadError);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error(), HeightmapError::ReadError);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapUtilityTest, CalculateMipLevels) {
    EXPECT_EQ(calculateMipLevels(1, 1), 1u);
    EXPECT_EQ(calculateMipLevels(2, 2), 2u);
    EXPECT_EQ(calculateMipLevels(4, 4), 3u);
    EXPECT_EQ(calculateMipLevels(256, 256), 9u);
    EXPECT_EQ(calculateMipLevels(512, 512), 10u);
    EXPECT_EQ(calculateMipLevels(1024, 1024), 11u);
    EXPECT_EQ(calculateMipLevels(8192, 8192), 14u);
    
    // Non-square (uses max dimension)
    EXPECT_EQ(calculateMipLevels(256, 64), 9u);
    EXPECT_EQ(calculateMipLevels(1024, 512), 11u);
}

TEST(HeightmapUtilityTest, IsPowerOfTwo) {
    EXPECT_FALSE(isPowerOfTwo(0));
    EXPECT_TRUE(isPowerOfTwo(1));
    EXPECT_TRUE(isPowerOfTwo(2));
    EXPECT_FALSE(isPowerOfTwo(3));
    EXPECT_TRUE(isPowerOfTwo(4));
    EXPECT_FALSE(isPowerOfTwo(5));
    EXPECT_TRUE(isPowerOfTwo(256));
    EXPECT_FALSE(isPowerOfTwo(255));
    EXPECT_TRUE(isPowerOfTwo(1024));
    EXPECT_TRUE(isPowerOfTwo(8192));
    EXPECT_FALSE(isPowerOfTwo(8191));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Heightmap Class Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapTest, DefaultConstruction) {
    Heightmap hm;
    
    EXPECT_FALSE(hm.isLoaded());
    EXPECT_EQ(hm.getWidth(), 0u);
    EXPECT_EQ(hm.getHeight(), 0u);
    EXPECT_EQ(hm.getSampleCount(), 0u);
    EXPECT_EQ(hm.getSizeBytes(), 0u);
    EXPECT_DOUBLE_EQ(hm.getLoadTimeMs(), 0.0);
    EXPECT_TRUE(hm.getData().empty());
    EXPECT_FALSE(hm.hasGPUTexture());
    EXPECT_EQ(hm.getTexture(), nullptr);
}

TEST(HeightmapTest, CreateFlat) {
    auto hm = Heightmap::createFlat(64, 32, 12345);
    
    EXPECT_TRUE(hm.isLoaded());
    EXPECT_EQ(hm.getWidth(), 64u);
    EXPECT_EQ(hm.getHeight(), 32u);
    EXPECT_EQ(hm.getSampleCount(), 64u * 32u);
    EXPECT_EQ(hm.getSizeBytes(), 64u * 32u * sizeof(uint16_t));
    
    // All samples should be the fill value
    for (uint32_t y = 0; y < 32; y++) {
        for (uint32_t x = 0; x < 64; x++) {
            EXPECT_EQ(hm.sample(x, y), 12345u);
        }
    }
}

TEST(HeightmapTest, CreateFromData) {
    std::vector<uint16_t> data(16);
    for (size_t i = 0; i < 16; i++) {
        data[i] = static_cast<uint16_t>(i * 100);
    }
    
    auto hm = Heightmap::createFromData(std::move(data), 4, 4);
    
    EXPECT_TRUE(hm.isLoaded());
    EXPECT_EQ(hm.getWidth(), 4u);
    EXPECT_EQ(hm.getHeight(), 4u);
    EXPECT_EQ(hm.sample(0, 0), 0u);
    EXPECT_EQ(hm.sample(3, 3), 1500u);
}

TEST(HeightmapTest, Sample) {
    std::vector<uint16_t> testData(16);
    for (size_t i = 0; i < 16; i++) {
        testData[i] = static_cast<uint16_t>(i * 1000);
    }
    auto hm = Heightmap::createFromData(std::move(testData), 4, 4);
    
    EXPECT_EQ(hm.sample(0, 0), 0u);
    EXPECT_EQ(hm.sample(1, 0), 1000u);
    EXPECT_EQ(hm.sample(0, 1), 4000u);
    EXPECT_EQ(hm.sample(3, 3), 15000u);
    
    // Test clamping to bounds
    EXPECT_EQ(hm.sample(100, 100), 15000u);
}

TEST(HeightmapTest, SampleNormalized) {
    std::vector<uint16_t> data = {0, 32767, 65535, 16383};
    auto hm = Heightmap::createFromData(std::move(data), 2, 2);
    
    EXPECT_FLOAT_EQ(hm.sampleNormalized(0, 0), 0.0f);
    EXPECT_NEAR(hm.sampleNormalized(1, 0), 0.5f, 0.0001f);
    EXPECT_FLOAT_EQ(hm.sampleNormalized(0, 1), 1.0f);
    EXPECT_NEAR(hm.sampleNormalized(1, 1), 0.25f, 0.0001f);
}

TEST(HeightmapTest, SampleBilinear) {
    // Create a 2x2 heightmap with known values
    std::vector<uint16_t> data = {0, 10000, 20000, 30000};
    auto hm = Heightmap::createFromData(std::move(data), 2, 2);
    
    // Exact corners
    EXPECT_FLOAT_EQ(hm.sampleBilinear(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(hm.sampleBilinear(1.0f, 0.0f), 10000.0f);
    EXPECT_FLOAT_EQ(hm.sampleBilinear(0.0f, 1.0f), 20000.0f);
    EXPECT_FLOAT_EQ(hm.sampleBilinear(1.0f, 1.0f), 30000.0f);
    
    // Midpoints
    EXPECT_NEAR(hm.sampleBilinear(0.5f, 0.0f), 5000.0f, 1.0f);
    EXPECT_NEAR(hm.sampleBilinear(0.0f, 0.5f), 10000.0f, 1.0f);
    
    // Center
    EXPECT_NEAR(hm.sampleBilinear(0.5f, 0.5f), 15000.0f, 1.0f);
}

TEST(HeightmapTest, GetMinMax) {
    std::vector<uint16_t> data = {100, 500, 200, 50, 1000, 300};
    auto hm = Heightmap::createFromData(std::move(data), 3, 2);
    
    auto [minVal, maxVal] = hm.getMinMax();
    EXPECT_EQ(minVal, 50u);
    EXPECT_EQ(maxVal, 1000u);
}

TEST(HeightmapTest, GetMinMaxEmpty) {
    Heightmap hm;
    auto [minVal, maxVal] = hm.getMinMax();
    EXPECT_EQ(minVal, 0u);
    EXPECT_EQ(maxVal, 0u);
}

TEST(HeightmapTest, MoveConstructor) {
    auto hm1 = Heightmap::createFlat(32, 16, 5000);
    EXPECT_TRUE(hm1.isLoaded());
    EXPECT_EQ(hm1.getWidth(), 32u);
    
    Heightmap hm2(std::move(hm1));
    
    EXPECT_FALSE(hm1.isLoaded());
    EXPECT_EQ(hm1.getWidth(), 0u);
    
    EXPECT_TRUE(hm2.isLoaded());
    EXPECT_EQ(hm2.getWidth(), 32u);
    EXPECT_EQ(hm2.getHeight(), 16u);
    EXPECT_EQ(hm2.sample(0, 0), 5000u);
}

TEST(HeightmapTest, MoveAssignment) {
    auto hm1 = Heightmap::createFlat(32, 16, 5000);
    Heightmap hm2;
    
    hm2 = std::move(hm1);
    
    EXPECT_FALSE(hm1.isLoaded());
    EXPECT_TRUE(hm2.isLoaded());
    EXPECT_EQ(hm2.getWidth(), 32u);
    EXPECT_EQ(hm2.sample(0, 0), 5000u);
}

TEST(HeightmapTest, Release) {
    auto hm = Heightmap::createFlat(64, 64, 1000);
    EXPECT_TRUE(hm.isLoaded());
    
    hm.release();
    
    EXPECT_FALSE(hm.isLoaded());
    EXPECT_EQ(hm.getWidth(), 0u);
    EXPECT_EQ(hm.getHeight(), 0u);
    EXPECT_TRUE(hm.getData().empty());
}

TEST(HeightmapTest, GetTextureFormat) {
    EXPECT_EQ(Heightmap::getTextureFormat(), WGPUTextureFormat_R16Uint);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Load from Memory Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapTest, LoadRawFromMemory) {
    // Create test data
    std::vector<uint16_t> srcData(16);
    for (size_t i = 0; i < 16; i++) {
        srcData[i] = static_cast<uint16_t>(i * 1000);
    }
    
    // Convert to bytes
    std::vector<std::byte> bytes(srcData.size() * sizeof(uint16_t));
    std::memcpy(bytes.data(), srcData.data(), bytes.size());
    
    Heightmap hm;
    auto result = hm.loadRawFromMemory(bytes, 4, 4);
    
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(hm.isLoaded());
    EXPECT_EQ(hm.getWidth(), 4u);
    EXPECT_EQ(hm.getHeight(), 4u);
    EXPECT_EQ(hm.sample(0, 0), 0u);
    EXPECT_EQ(hm.sample(3, 3), 15000u);
}

TEST(HeightmapTest, LoadRawFromMemoryInvalidSize) {
    std::vector<std::byte> bytes(100);  // Wrong size for any reasonable dimensions
    
    Heightmap hm;
    auto result = hm.loadRawFromMemory(bytes, 8, 8);  // Would need 128 bytes
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::InvalidDimensions);
    EXPECT_FALSE(hm.isLoaded());
}

TEST(HeightmapTest, LoadRawFromMemoryZeroDimensions) {
    std::vector<std::byte> bytes(32);
    
    Heightmap hm;
    auto result = hm.loadRawFromMemory(bytes, 0, 0);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::InvalidDimensions);
}

// ═══════════════════════════════════════════════════════════════════════════════
// File Loading Tests (Error Cases)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapTest, LoadFromNonExistentFile) {
    Heightmap hm;
    auto result = hm.loadFromFile("/nonexistent/path/heightmap.ldh");
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::FileNotFound);
}

TEST(HeightmapTest, LoadRawWithoutDimensions) {
    Heightmap hm;
    auto result = hm.loadFromFile("test.raw");
    
    EXPECT_FALSE(result.has_value());
    // Since loadFromFile now only supports .ldh and returns InvalidFormat for others
    EXPECT_EQ(result.error(), HeightmapError::InvalidFormat);
}

TEST(HeightmapTest, LoadUnsupportedFormat) {
    Heightmap hm;
    auto result = hm.loadFromFile("test.unknown");
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::InvalidFormat);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Static Load Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapTest, StaticLoadNonExistent) {
    auto result = Heightmap::load("/nonexistent/file.ldh");
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::FileNotFound);
}

// ═══════════════════════════════════════════════════════════════════════════════
// GPU Upload Error Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapTest, UploadToGPUNoData) {
    Heightmap hm;
    auto result = hm.uploadToGPU(nullptr, nullptr);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::UploadFailed);
}

TEST(HeightmapTest, UploadToGPUNullDevice) {
    auto hm = Heightmap::createFlat(64, 64, 1000);
    auto result = hm.uploadToGPU(nullptr, nullptr);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::UploadFailed);
}

TEST(HeightmapTest, GetTextureViewWithoutTexture) {
    Heightmap hm;
    EXPECT_EQ(hm.getTextureView(), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Data Access Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapTest, GetDataBytes) {
    std::vector<uint16_t> srcData = {100, 200, 300, 400};
    auto hm = Heightmap::createFromData(std::move(srcData), 2, 2);
    
    auto bytes = hm.getDataBytes();
    EXPECT_EQ(bytes.size(), 4 * sizeof(uint16_t));
    
    // Verify first value
    uint16_t firstValue;
    std::memcpy(&firstValue, bytes.data(), sizeof(uint16_t));
    EXPECT_EQ(firstValue, 100u);
}

TEST(HeightmapTest, GetData) {
    std::vector<uint16_t> srcData = {1, 2, 3, 4, 5, 6};
    auto hm = Heightmap::createFromData(std::move(srcData), 3, 2);
    
    auto data = hm.getData();
    EXPECT_EQ(data.size(), 6u);
    EXPECT_EQ(data[0], 1u);
    EXPECT_EQ(data[5], 6u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mip Chain Tests (No GPU Required)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HeightmapMipTest, DefaultMipLevelCount) {
    Heightmap hm;
    EXPECT_EQ(hm.getMipLevelCount(), 0u);
    EXPECT_FALSE(hm.hasMipChain());
}

TEST(HeightmapMipTest, MipLevelCountAfterCreate) {
    auto hm = Heightmap::createFlat(256, 256, 1000);
    
    // Before GPU upload, mip level count should be 0
    EXPECT_EQ(hm.getMipLevelCount(), 0u);
    EXPECT_FALSE(hm.hasMipChain());
}

TEST(HeightmapMipTest, GetMipViewWithoutTexture) {
    Heightmap hm;
    EXPECT_EQ(hm.getMipView(0), nullptr);
    EXPECT_EQ(hm.getMipView(1), nullptr);
}

TEST(HeightmapMipTest, UploadToGPUWithMipsNoData) {
    Heightmap hm;
    auto result = hm.uploadToGPUWithMips(nullptr, nullptr);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::UploadFailed);
}

TEST(HeightmapMipTest, UploadToGPUWithMipsNullDevice) {
    auto hm = Heightmap::createFlat(64, 64, 1000);
    auto result = hm.uploadToGPUWithMips(nullptr, nullptr);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HeightmapError::UploadFailed);
}

TEST(HeightmapMipTest, CalculateMipLevelsVariousSizes) {
    // Verify the utility function works correctly
    EXPECT_EQ(calculateMipLevels(1, 1), 1u);
    EXPECT_EQ(calculateMipLevels(2, 2), 2u);
    EXPECT_EQ(calculateMipLevels(4, 4), 3u);
    EXPECT_EQ(calculateMipLevels(8, 8), 4u);
    EXPECT_EQ(calculateMipLevels(64, 64), 7u);
    EXPECT_EQ(calculateMipLevels(256, 256), 9u);
    EXPECT_EQ(calculateMipLevels(8192, 8192), 14u);
    
    // Non-square
    EXPECT_EQ(calculateMipLevels(256, 64), 9u);  // Uses max dimension
    EXPECT_EQ(calculateMipLevels(1, 128), 8u);
}

TEST(HeightmapMipTest, MoveSemanticsMipCount) {
    auto hm1 = Heightmap::createFlat(32, 32, 5000);
    
    // Before move
    EXPECT_TRUE(hm1.isLoaded());
    EXPECT_EQ(hm1.getMipLevelCount(), 0u);  // No GPU upload yet
    
    Heightmap hm2(std::move(hm1));
    
    // After move
    EXPECT_FALSE(hm1.isLoaded());
    EXPECT_EQ(hm1.getMipLevelCount(), 0u);
    
    EXPECT_TRUE(hm2.isLoaded());
    EXPECT_EQ(hm2.getMipLevelCount(), 0u);
}

TEST(HeightmapMipTest, ReleaseResetsMipCount) {
    auto hm = Heightmap::createFlat(64, 64, 1000);
    EXPECT_TRUE(hm.isLoaded());
    EXPECT_EQ(hm.getMipLevelCount(), 0u);
    
    hm.release();
    
    EXPECT_FALSE(hm.isLoaded());
    EXPECT_EQ(hm.getMipLevelCount(), 0u);
    EXPECT_FALSE(hm.hasMipChain());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mip Chain Correctness Verification Tests
// These tests verify that the CPU mip generation produces correct max-height values
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipChainCorrectnessTest, VerifyMaxHeightPropagation) {
    // Create a 4×4 heightmap with a known pattern
    // Layout:
    // [ 10, 20, 30, 40 ]
    // [ 50, 60, 70, 80 ]
    // [ 90,100,110,120 ]
    // [130,140,150,160 ]
    //
    // Expected mip level 1 (2×2):
    // Block (0,0): max(10,20,50,60) = 60
    // Block (1,0): max(30,40,70,80) = 80
    // Block (0,1): max(90,100,130,140) = 140
    // Block (1,1): max(110,120,150,160) = 160
    //
    // Expected mip level 2 (1×1):
    // max(60,80,140,160) = 160
    
    std::vector<uint16_t> data = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
        130, 140, 150, 160
    };
    
    auto hm = Heightmap::createFromData(std::move(data), 4, 4);
    EXPECT_TRUE(hm.isLoaded());
    
    // Generate mip chain using CPU generator (same code path as Heightmap uses)
    MaxHeightMipChain mipChain;
    EXPECT_TRUE(mipChain.generate(hm.getData(), 4, 4));
    
    // Verify level count
    EXPECT_EQ(mipChain.getLevelCount(), 3u);  // 4×4, 2×2, 1×1
    
    // Verify level 0 (base - unchanged)
    const auto* level0 = mipChain.getLevel(0);
    ASSERT_NE(level0, nullptr);
    EXPECT_EQ(level0->sample(0, 0), 10u);
    EXPECT_EQ(level0->sample(3, 3), 160u);
    
    // Verify level 1 (2×2 max reduction)
    const auto* level1 = mipChain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->width, 2u);
    EXPECT_EQ(level1->height, 2u);
    EXPECT_EQ(level1->sample(0, 0), 60u);   // max(10,20,50,60)
    EXPECT_EQ(level1->sample(1, 0), 80u);   // max(30,40,70,80)
    EXPECT_EQ(level1->sample(0, 1), 140u);  // max(90,100,130,140)
    EXPECT_EQ(level1->sample(1, 1), 160u);  // max(110,120,150,160)
    
    // Verify level 2 (1×1 - global max)
    const auto* level2 = mipChain.getLevel(2);
    ASSERT_NE(level2, nullptr);
    EXPECT_EQ(level2->width, 1u);
    EXPECT_EQ(level2->height, 1u);
    EXPECT_EQ(level2->sample(0, 0), 160u);  // Global maximum
}

TEST(MipChainCorrectnessTest, VerifySingleSpikeMaxPropagates) {
    // Create an 8×8 heightmap with a single spike
    // All values are 100 except one cell which is 50000
    const uint32_t size = 8;
    std::vector<uint16_t> data(size * size, 100);
    data[3 * size + 3] = 50000;  // Spike at (3,3)
    
    auto hm = Heightmap::createFromData(std::move(data), size, size);
    
    MaxHeightMipChain mipChain;
    EXPECT_TRUE(mipChain.generate(hm.getData(), size, size));
    
    // The spike should propagate to the top level
    const auto* topLevel = mipChain.getLevel(mipChain.getLevelCount() - 1);
    ASSERT_NE(topLevel, nullptr);
    EXPECT_EQ(topLevel->sample(0, 0), 50000u);  // Spike value at top
    
    // Verify intermediate levels contain the spike
    // Level 1 (4×4): spike at (3,3) is in block (1,1) of the 2×2 reduction
    const auto* level1 = mipChain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->sample(1, 1), 50000u);  // Block containing (3,3)
    
    // Level 2 (2×2): spike propagates to (0,0) of this level
    const auto* level2 = mipChain.getLevel(2);
    ASSERT_NE(level2, nullptr);
    // The spike at level1 (1,1) should be in block (0,0) of level2
    EXPECT_EQ(level2->sample(0, 0), 50000u);
}

TEST(MipChainCorrectnessTest, VerifyMaxInEachCorner) {
    // Create a 4×4 heightmap with max values in each corner
    // to verify all quadrants are processed correctly
    std::vector<uint16_t> data = {
        1000, 1,    1,    2000,  // Row 0: max at (0,0) and (3,0)
        1,    1,    1,    1,
        1,    1,    1,    1,
        3000, 1,    1,    4000   // Row 3: max at (0,3) and (3,3)
    };
    
    MaxHeightMipChain mipChain;
    EXPECT_TRUE(mipChain.generate(data, 4, 4));
    
    // Level 1 (2×2) should have:
    // (0,0) = max(1000,1,1,1) = 1000
    // (1,0) = max(1,2000,1,1) = 2000
    // (0,1) = max(1,1,3000,1) = 3000
    // (1,1) = max(1,1,1,4000) = 4000
    const auto* level1 = mipChain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->sample(0, 0), 1000u);
    EXPECT_EQ(level1->sample(1, 0), 2000u);
    EXPECT_EQ(level1->sample(0, 1), 3000u);
    EXPECT_EQ(level1->sample(1, 1), 4000u);
    
    // Level 2 (1×1) = max of all = 4000
    const auto* level2 = mipChain.getLevel(2);
    ASSERT_NE(level2, nullptr);
    EXPECT_EQ(level2->sample(0, 0), 4000u);
}

TEST(MipChainCorrectnessTest, VerifyUniformHeightmap) {
    // A uniform heightmap should have the same value at all mip levels
    const uint16_t uniformValue = 12345;
    std::vector<uint16_t> data(64, uniformValue);  // 8×8
    
    MaxHeightMipChain mipChain;
    EXPECT_TRUE(mipChain.generate(data, 8, 8));
    
    // All levels should have the same uniform value
    for (uint32_t level = 0; level < mipChain.getLevelCount(); level++) {
        const auto* mipLevel = mipChain.getLevel(level);
        ASSERT_NE(mipLevel, nullptr);
        
        for (uint32_t y = 0; y < mipLevel->height; y++) {
            for (uint32_t x = 0; x < mipLevel->width; x++) {
                EXPECT_EQ(mipLevel->sample(x, y), uniformValue) 
                    << "Mismatch at level " << level << ", position (" << x << "," << y << ")";
            }
        }
    }
}

TEST(MipChainCorrectnessTest, VerifyMaxU16Value) {
    // Verify that max uint16_t values propagate correctly
    std::vector<uint16_t> data = {0, 0, 0, 65535};  // 2×2, max in bottom-right
    
    MaxHeightMipChain mipChain;
    EXPECT_TRUE(mipChain.generate(data, 2, 2));
    
    EXPECT_EQ(mipChain.getLevelCount(), 2u);  // 2×2, 1×1
    
    const auto* level1 = mipChain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->sample(0, 0), 65535u);
}

TEST(MipChainCorrectnessTest, VerifyNonSquareDimensions) {
    // 8×4 heightmap
    // Layout (showing max of each row for simplicity):
    // Row 0: [ 10, 20, 30, 40, 50, 60, 70, 80 ]
    // Row 1: [ 15, 25, 35, 45, 55, 65, 75, 85 ]
    // Row 2: [ 20, 30, 40, 50, 60, 70, 80, 90 ]
    // Row 3: [ 25, 35, 45, 55, 65, 75, 85, 95 ]
    
    std::vector<uint16_t> data = {
        10, 20, 30, 40, 50, 60, 70, 80,
        15, 25, 35, 45, 55, 65, 75, 85,
        20, 30, 40, 50, 60, 70, 80, 90,
        25, 35, 45, 55, 65, 75, 85, 95
    };
    
    MaxHeightMipChain mipChain;
    EXPECT_TRUE(mipChain.generate(data, 8, 4));
    
    // Level 0: 8×4
    // Level 1: 4×2
    // Level 2: 2×1
    // Level 3: 1×1
    EXPECT_EQ(mipChain.getLevelCount(), 4u);
    
    // Verify level 1 dimensions
    const auto* level1 = mipChain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->width, 4u);
    EXPECT_EQ(level1->height, 2u);
    
    // Block (0,0) at level 1: max(10,20,15,25) = 25
    EXPECT_EQ(level1->sample(0, 0), 25u);
    // Block (3,1) at level 1: max(80,90,85,95) = 95
    EXPECT_EQ(level1->sample(3, 1), 95u);
    
    // Top level should be the global max = 95
    const auto* topLevel = mipChain.getLevel(3);
    ASSERT_NE(topLevel, nullptr);
    EXPECT_EQ(topLevel->sample(0, 0), 95u);
}

} // namespace voxy::terrain

