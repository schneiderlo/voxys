// ═══════════════════════════════════════════════════════════════════════════════
// test_mip_generator.cpp - Unit tests for CPU Max-Height Mip Chain Generation
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for mip level generation, max reduction, and mip chain management.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "terrain/mip_generator.hpp"

#include <cmath>
#include <numeric>
#include <vector>

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipGeneratorUtilityTest, CalculateMipLevelCount) {
    // Power of two dimensions
    EXPECT_EQ(calculateMipLevelCount(1, 1), 1u);
    EXPECT_EQ(calculateMipLevelCount(2, 2), 2u);
    EXPECT_EQ(calculateMipLevelCount(4, 4), 3u);
    EXPECT_EQ(calculateMipLevelCount(8, 8), 4u);
    EXPECT_EQ(calculateMipLevelCount(16, 16), 5u);
    EXPECT_EQ(calculateMipLevelCount(256, 256), 9u);
    EXPECT_EQ(calculateMipLevelCount(512, 512), 10u);
    EXPECT_EQ(calculateMipLevelCount(1024, 1024), 11u);
    EXPECT_EQ(calculateMipLevelCount(8192, 8192), 14u);
    
    // Non-square (uses max dimension)
    EXPECT_EQ(calculateMipLevelCount(256, 64), 9u);
    EXPECT_EQ(calculateMipLevelCount(64, 256), 9u);
    EXPECT_EQ(calculateMipLevelCount(1024, 512), 11u);
    EXPECT_EQ(calculateMipLevelCount(1, 256), 9u);
}

TEST(MipGeneratorUtilityTest, GetMipLevelDimensions) {
    // 8×8 base
    auto [w0, h0] = getMipLevelDimensions(8, 8, 0);
    EXPECT_EQ(w0, 8u);
    EXPECT_EQ(h0, 8u);
    
    auto [w1, h1] = getMipLevelDimensions(8, 8, 1);
    EXPECT_EQ(w1, 4u);
    EXPECT_EQ(h1, 4u);
    
    auto [w2, h2] = getMipLevelDimensions(8, 8, 2);
    EXPECT_EQ(w2, 2u);
    EXPECT_EQ(h2, 2u);
    
    auto [w3, h3] = getMipLevelDimensions(8, 8, 3);
    EXPECT_EQ(w3, 1u);
    EXPECT_EQ(h3, 1u);
    
    // Non-square 16×4
    auto [nw0, nh0] = getMipLevelDimensions(16, 4, 0);
    EXPECT_EQ(nw0, 16u);
    EXPECT_EQ(nh0, 4u);
    
    auto [nw1, nh1] = getMipLevelDimensions(16, 4, 1);
    EXPECT_EQ(nw1, 8u);
    EXPECT_EQ(nh1, 2u);
    
    auto [nw2, nh2] = getMipLevelDimensions(16, 4, 2);
    EXPECT_EQ(nw2, 4u);
    EXPECT_EQ(nh2, 1u);
    
    auto [nw3, nh3] = getMipLevelDimensions(16, 4, 3);
    EXPECT_EQ(nw3, 2u);
    EXPECT_EQ(nh3, 1u);
    
    auto [nw4, nh4] = getMipLevelDimensions(16, 4, 4);
    EXPECT_EQ(nw4, 1u);
    EXPECT_EQ(nh4, 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MipLevel Structure Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipLevelTest, DefaultConstruction) {
    MipLevel level;
    
    EXPECT_TRUE(level.data.empty());
    EXPECT_EQ(level.width, 0u);
    EXPECT_EQ(level.height, 0u);
    EXPECT_EQ(level.sampleCount(), 0u);
    EXPECT_EQ(level.sizeBytes(), 0u);
    EXPECT_FALSE(level.isValid());
}

TEST(MipLevelTest, ValidLevel) {
    MipLevel level;
    level.width = 4;
    level.height = 4;
    level.data.resize(16, 1000);
    
    EXPECT_EQ(level.sampleCount(), 16u);
    EXPECT_EQ(level.sizeBytes(), 32u);
    EXPECT_TRUE(level.isValid());
}

TEST(MipLevelTest, Sample) {
    MipLevel level;
    level.width = 4;
    level.height = 4;
    level.data.resize(16);
    
    // Fill with index values
    for (size_t i = 0; i < 16; i++) {
        level.data[i] = static_cast<uint16_t>(i * 100);
    }
    
    EXPECT_EQ(level.sample(0, 0), 0u);
    EXPECT_EQ(level.sample(1, 0), 100u);
    EXPECT_EQ(level.sample(0, 1), 400u);
    EXPECT_EQ(level.sample(3, 3), 1500u);
    
    // Test clamping
    EXPECT_EQ(level.sample(100, 100), 1500u);
}

TEST(MipLevelTest, SampleEmpty) {
    MipLevel level;
    EXPECT_EQ(level.sample(0, 0), 0u);
    EXPECT_EQ(level.sample(10, 10), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// generateNextMipLevel Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GenerateNextMipLevelTest, Simple2x2) {
    // 2×2 -> 1×1 (should be max of all 4 values)
    std::vector<uint16_t> src = {100, 200, 300, 400};
    
    auto result = generateNextMipLevel(src, 2, 2);
    
    EXPECT_EQ(result.width, 1u);
    EXPECT_EQ(result.height, 1u);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.data[0], 400u);
}

TEST(GenerateNextMipLevelTest, Simple4x4) {
    // 4×4 -> 2×2
    // Layout:
    // [ 10, 20, 30, 40 ]
    // [ 50, 60, 70, 80 ]
    // [ 90,100,110,120 ]
    // [130,140,150,160 ]
    //
    // Block (0,0): max(10,20,50,60) = 60
    // Block (1,0): max(30,40,70,80) = 80
    // Block (0,1): max(90,100,130,140) = 140
    // Block (1,1): max(110,120,150,160) = 160
    
    std::vector<uint16_t> src = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
        130, 140, 150, 160
    };
    
    auto result = generateNextMipLevel(src, 4, 4);
    
    EXPECT_EQ(result.width, 2u);
    EXPECT_EQ(result.height, 2u);
    EXPECT_TRUE(result.isValid());
    
    EXPECT_EQ(result.sample(0, 0), 60u);
    EXPECT_EQ(result.sample(1, 0), 80u);
    EXPECT_EQ(result.sample(0, 1), 140u);
    EXPECT_EQ(result.sample(1, 1), 160u);
}

TEST(GenerateNextMipLevelTest, MaxInDifferentPositions) {
    // Test that max is found regardless of position in 2×2 block
    
    // Max in top-left
    std::vector<uint16_t> src1 = {999, 1, 1, 1};
    EXPECT_EQ(generateNextMipLevel(src1, 2, 2).data[0], 999u);
    
    // Max in top-right
    std::vector<uint16_t> src2 = {1, 999, 1, 1};
    EXPECT_EQ(generateNextMipLevel(src2, 2, 2).data[0], 999u);
    
    // Max in bottom-left
    std::vector<uint16_t> src3 = {1, 1, 999, 1};
    EXPECT_EQ(generateNextMipLevel(src3, 2, 2).data[0], 999u);
    
    // Max in bottom-right
    std::vector<uint16_t> src4 = {1, 1, 1, 999};
    EXPECT_EQ(generateNextMipLevel(src4, 2, 2).data[0], 999u);
}

TEST(GenerateNextMipLevelTest, NonSquare) {
    // 4×2 -> 2×1
    std::vector<uint16_t> src = {
        100, 200, 300, 400,
        150, 250, 350, 450
    };
    
    auto result = generateNextMipLevel(src, 4, 2);
    
    EXPECT_EQ(result.width, 2u);
    EXPECT_EQ(result.height, 1u);
    
    // Block (0,0): max(100,200,150,250) = 250
    // Block (1,0): max(300,400,350,450) = 450
    EXPECT_EQ(result.sample(0, 0), 250u);
    EXPECT_EQ(result.sample(1, 0), 450u);
}

TEST(GenerateNextMipLevelTest, OddDimensions) {
    // 5×3 -> 2×1 (should handle edge clamping)
    std::vector<uint16_t> src = {
        10, 20, 30, 40, 50,
        60, 70, 80, 90, 100,
        110, 120, 130, 140, 150
    };
    
    auto result = generateNextMipLevel(src, 5, 3);
    
    EXPECT_EQ(result.width, 2u);
    EXPECT_EQ(result.height, 1u);
    
    // Block (0,0): samples at (0,0), (1,0), (0,1), (1,1) = max(10,20,60,70) = 70
    // Block (1,0): samples at (2,0), (3,0), (2,1), (3,1) = max(30,40,80,90) = 90
    EXPECT_EQ(result.sample(0, 0), 70u);
    EXPECT_EQ(result.sample(1, 0), 90u);
}

TEST(GenerateNextMipLevelTest, AllSameValue) {
    std::vector<uint16_t> src(16, 12345);
    auto result = generateNextMipLevel(src, 4, 4);
    
    EXPECT_EQ(result.width, 2u);
    EXPECT_EQ(result.height, 2u);
    
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            EXPECT_EQ(result.sample(x, y), 12345u);
        }
    }
}

TEST(GenerateNextMipLevelTest, MaxValueU16) {
    std::vector<uint16_t> src = {0, 0, 0, 65535};
    auto result = generateNextMipLevel(src, 2, 2);
    EXPECT_EQ(result.data[0], 65535u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MaxHeightMipChain Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MaxHeightMipChainTest, DefaultConstruction) {
    MaxHeightMipChain chain;
    
    EXPECT_FALSE(chain.isValid());
    EXPECT_EQ(chain.getLevelCount(), 0u);
    EXPECT_EQ(chain.getBaseWidth(), 0u);
    EXPECT_EQ(chain.getBaseHeight(), 0u);
    EXPECT_FALSE(chain.hasBaseLevel());
    EXPECT_EQ(chain.getTotalSizeBytes(), 0u);
    EXPECT_EQ(chain.getLevel(0), nullptr);
}

TEST(MaxHeightMipChainTest, Generate4x4) {
    MaxHeightMipChain chain;
    
    // Create a 4×4 heightmap
    std::vector<uint16_t> data = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
        130, 140, 150, 160
    };
    
    EXPECT_TRUE(chain.generate(data, 4, 4));
    
    EXPECT_TRUE(chain.isValid());
    EXPECT_EQ(chain.getLevelCount(), 3u);  // 4×4, 2×2, 1×1
    EXPECT_EQ(chain.getBaseWidth(), 4u);
    EXPECT_EQ(chain.getBaseHeight(), 4u);
    EXPECT_TRUE(chain.hasBaseLevel());
    
    // Level 0 (4×4)
    const auto* level0 = chain.getLevel(0);
    ASSERT_NE(level0, nullptr);
    EXPECT_EQ(level0->width, 4u);
    EXPECT_EQ(level0->height, 4u);
    EXPECT_EQ(level0->sample(0, 0), 10u);
    EXPECT_EQ(level0->sample(3, 3), 160u);
    
    // Level 1 (2×2)
    const auto* level1 = chain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->width, 2u);
    EXPECT_EQ(level1->height, 2u);
    EXPECT_EQ(level1->sample(0, 0), 60u);
    EXPECT_EQ(level1->sample(1, 1), 160u);
    
    // Level 2 (1×1)
    const auto* level2 = chain.getLevel(2);
    ASSERT_NE(level2, nullptr);
    EXPECT_EQ(level2->width, 1u);
    EXPECT_EQ(level2->height, 1u);
    EXPECT_EQ(level2->sample(0, 0), 160u);  // Max of entire heightmap
    
    // Level 3 doesn't exist
    EXPECT_EQ(chain.getLevel(3), nullptr);
}

TEST(MaxHeightMipChainTest, Generate8x8) {
    MaxHeightMipChain chain;
    
    // Create an 8×8 heightmap with known pattern
    std::vector<uint16_t> data(64);
    for (size_t i = 0; i < 64; i++) {
        data[i] = static_cast<uint16_t>(i * 100);
    }
    
    EXPECT_TRUE(chain.generate(data, 8, 8));
    
    EXPECT_EQ(chain.getLevelCount(), 4u);  // 8×8, 4×4, 2×2, 1×1
    EXPECT_EQ(chain.getBaseWidth(), 8u);
    EXPECT_EQ(chain.getBaseHeight(), 8u);
    
    // Verify level dimensions
    const auto* level0 = chain.getLevel(0);
    ASSERT_NE(level0, nullptr);
    EXPECT_EQ(level0->width, 8u);
    
    const auto* level1 = chain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->width, 4u);
    
    const auto* level2 = chain.getLevel(2);
    ASSERT_NE(level2, nullptr);
    EXPECT_EQ(level2->width, 2u);
    
    const auto* level3 = chain.getLevel(3);
    ASSERT_NE(level3, nullptr);
    EXPECT_EQ(level3->width, 1u);
    
    // Level 3 should contain max value
    EXPECT_EQ(level3->sample(0, 0), 6300u);  // (63 * 100)
}

TEST(MaxHeightMipChainTest, GenerateWithoutBase) {
    MaxHeightMipChain chain;
    
    std::vector<uint16_t> data = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
        130, 140, 150, 160
    };
    
    EXPECT_TRUE(chain.generateWithoutBase(data, 4, 4));
    
    EXPECT_TRUE(chain.isValid());
    EXPECT_EQ(chain.getLevelCount(), 2u);  // Only 2×2 and 1×1 (no base)
    EXPECT_EQ(chain.getBaseWidth(), 4u);
    EXPECT_EQ(chain.getBaseHeight(), 4u);
    EXPECT_FALSE(chain.hasBaseLevel());
    
    // Level 0 (base) should not exist
    EXPECT_EQ(chain.getLevel(0), nullptr);
    
    // Level 1 (2×2) should exist
    const auto* level1 = chain.getLevel(1);
    ASSERT_NE(level1, nullptr);
    EXPECT_EQ(level1->width, 2u);
    EXPECT_EQ(level1->height, 2u);
    
    // Level 2 (1×1) should exist
    const auto* level2 = chain.getLevel(2);
    ASSERT_NE(level2, nullptr);
    EXPECT_EQ(level2->width, 1u);
    EXPECT_EQ(level2->height, 1u);
    EXPECT_EQ(level2->sample(0, 0), 160u);
}

TEST(MaxHeightMipChainTest, GenerateEmptyData) {
    MaxHeightMipChain chain;
    std::vector<uint16_t> empty;
    
    EXPECT_FALSE(chain.generate(empty, 4, 4));
    EXPECT_FALSE(chain.isValid());
}

TEST(MaxHeightMipChainTest, GenerateZeroDimensions) {
    MaxHeightMipChain chain;
    std::vector<uint16_t> data(16);
    
    EXPECT_FALSE(chain.generate(data, 0, 4));
    EXPECT_FALSE(chain.generate(data, 4, 0));
    EXPECT_FALSE(chain.generate(data, 0, 0));
    EXPECT_FALSE(chain.isValid());
}

TEST(MaxHeightMipChainTest, GenerateSizeMismatch) {
    MaxHeightMipChain chain;
    std::vector<uint16_t> data(10);  // Wrong size for 4×4
    
    EXPECT_FALSE(chain.generate(data, 4, 4));
    EXPECT_FALSE(chain.isValid());
}

TEST(MaxHeightMipChainTest, Generate1x1) {
    MaxHeightMipChain chain;
    std::vector<uint16_t> data = {12345};
    
    EXPECT_TRUE(chain.generate(data, 1, 1));
    
    EXPECT_EQ(chain.getLevelCount(), 1u);
    
    const auto* level0 = chain.getLevel(0);
    ASSERT_NE(level0, nullptr);
    EXPECT_EQ(level0->width, 1u);
    EXPECT_EQ(level0->height, 1u);
    EXPECT_EQ(level0->sample(0, 0), 12345u);
}

TEST(MaxHeightMipChainTest, GenerateNonSquare) {
    MaxHeightMipChain chain;
    
    // 8×2 heightmap
    std::vector<uint16_t> data = {
        100, 200, 300, 400, 500, 600, 700, 800,
        150, 250, 350, 450, 550, 650, 750, 850
    };
    
    EXPECT_TRUE(chain.generate(data, 8, 2));
    
    // Expected levels: 8×2 -> 4×1 -> 2×1 -> 1×1
    EXPECT_EQ(chain.getLevelCount(), 4u);
    
    const auto* level0 = chain.getLevel(0);
    EXPECT_EQ(level0->width, 8u);
    EXPECT_EQ(level0->height, 2u);
    
    const auto* level1 = chain.getLevel(1);
    EXPECT_EQ(level1->width, 4u);
    EXPECT_EQ(level1->height, 1u);
    
    const auto* level2 = chain.getLevel(2);
    EXPECT_EQ(level2->width, 2u);
    EXPECT_EQ(level2->height, 1u);
    
    const auto* level3 = chain.getLevel(3);
    EXPECT_EQ(level3->width, 1u);
    EXPECT_EQ(level3->height, 1u);
    EXPECT_EQ(level3->sample(0, 0), 850u);  // Max of entire heightmap
}

TEST(MaxHeightMipChainTest, Clear) {
    MaxHeightMipChain chain;
    std::vector<uint16_t> data(16, 1000);
    
    EXPECT_TRUE(chain.generate(data, 4, 4));
    EXPECT_TRUE(chain.isValid());
    
    chain.clear();
    
    EXPECT_FALSE(chain.isValid());
    EXPECT_EQ(chain.getLevelCount(), 0u);
    EXPECT_EQ(chain.getBaseWidth(), 0u);
    EXPECT_EQ(chain.getBaseHeight(), 0u);
    EXPECT_FALSE(chain.hasBaseLevel());
}

TEST(MaxHeightMipChainTest, Regenerate) {
    MaxHeightMipChain chain;
    
    // Generate first chain
    std::vector<uint16_t> data1(16, 100);
    EXPECT_TRUE(chain.generate(data1, 4, 4));
    EXPECT_EQ(chain.getLevel(2)->sample(0, 0), 100u);
    
    // Regenerate with different data
    std::vector<uint16_t> data2(64, 200);
    EXPECT_TRUE(chain.generate(data2, 8, 8));
    
    EXPECT_EQ(chain.getLevelCount(), 4u);
    EXPECT_EQ(chain.getBaseWidth(), 8u);
    EXPECT_EQ(chain.getLevel(3)->sample(0, 0), 200u);
}

TEST(MaxHeightMipChainTest, GetLevels) {
    MaxHeightMipChain chain;
    std::vector<uint16_t> data(16, 500);
    
    EXPECT_TRUE(chain.generate(data, 4, 4));
    
    auto levels = chain.getLevels();
    EXPECT_EQ(levels.size(), 3u);
    
    EXPECT_EQ(levels[0].width, 4u);
    EXPECT_EQ(levels[1].width, 2u);
    EXPECT_EQ(levels[2].width, 1u);
}

TEST(MaxHeightMipChainTest, GetTotalSizeBytes) {
    MaxHeightMipChain chain;
    
    // 4×4 -> 2×2 -> 1×1
    // Sizes: 16*2 + 4*2 + 1*2 = 32 + 8 + 2 = 42 bytes
    std::vector<uint16_t> data(16, 0);
    EXPECT_TRUE(chain.generate(data, 4, 4));
    
    EXPECT_EQ(chain.getTotalSizeBytes(), 42u);
}

TEST(MaxHeightMipChainTest, MoveConstruction) {
    MaxHeightMipChain chain1;
    std::vector<uint16_t> data(16, 777);
    EXPECT_TRUE(chain1.generate(data, 4, 4));
    
    MaxHeightMipChain chain2(std::move(chain1));
    
    EXPECT_TRUE(chain2.isValid());
    EXPECT_EQ(chain2.getLevelCount(), 3u);
    EXPECT_EQ(chain2.getLevel(2)->sample(0, 0), 777u);
}

TEST(MaxHeightMipChainTest, MoveAssignment) {
    MaxHeightMipChain chain1;
    std::vector<uint16_t> data(16, 888);
    EXPECT_TRUE(chain1.generate(data, 4, 4));
    
    MaxHeightMipChain chain2;
    chain2 = std::move(chain1);
    
    EXPECT_TRUE(chain2.isValid());
    EXPECT_EQ(chain2.getLevelCount(), 3u);
    EXPECT_EQ(chain2.getLevel(2)->sample(0, 0), 888u);
}

TEST(MaxHeightMipChainTest, CopyConstruction) {
    MaxHeightMipChain chain1;
    std::vector<uint16_t> data(16, 999);
    EXPECT_TRUE(chain1.generate(data, 4, 4));
    
    MaxHeightMipChain chain2(chain1);
    
    EXPECT_TRUE(chain1.isValid());
    EXPECT_TRUE(chain2.isValid());
    EXPECT_EQ(chain1.getLevelCount(), chain2.getLevelCount());
    EXPECT_EQ(chain1.getLevel(2)->sample(0, 0), chain2.getLevel(2)->sample(0, 0));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MaxHeightMipChainIntegrationTest, RealisticHeightmap) {
    // Create a 64×64 heightmap with a "hill" in the center
    const uint32_t size = 64;
    std::vector<uint16_t> data(size * size);
    
    const float center = size / 2.0f;
    const float maxHeight = 10000.0f;
    
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            float dx = static_cast<float>(x) - center;
            float dy = static_cast<float>(y) - center;
            float dist = std::sqrt(dx * dx + dy * dy);
            float height = maxHeight * std::max(0.0f, 1.0f - dist / center);
            data[y * size + x] = static_cast<uint16_t>(height);
        }
    }
    
    MaxHeightMipChain chain;
    EXPECT_TRUE(chain.generate(data, size, size));
    
    // Expected levels: 64 -> 32 -> 16 -> 8 -> 4 -> 2 -> 1 (7 levels)
    EXPECT_EQ(chain.getLevelCount(), 7u);
    
    // The top level should contain approximately the max height
    // (center pixel value, which should be close to maxHeight)
    const auto* topLevel = chain.getLevel(6);
    ASSERT_NE(topLevel, nullptr);
    EXPECT_GT(topLevel->sample(0, 0), 9000u);  // Should be close to 10000
}

TEST(MaxHeightMipChainIntegrationTest, MaxValuePropagation) {
    // Create a heightmap with a single spike
    const uint32_t size = 32;
    std::vector<uint16_t> data(size * size, 1000);  // Base height
    data[15 * size + 15] = 50000;  // Single spike
    
    MaxHeightMipChain chain;
    EXPECT_TRUE(chain.generate(data, size, size));
    
    // The spike should propagate to the top level
    const auto* topLevel = chain.getLevel(chain.getLevelCount() - 1);
    EXPECT_EQ(topLevel->sample(0, 0), 50000u);
}

TEST(MaxHeightMipChainIntegrationTest, MemoryEfficiency) {
    // Verify that generateWithoutBase uses less memory
    const uint32_t size = 256;
    std::vector<uint16_t> data(size * size, 1000);
    
    MaxHeightMipChain chainWith;
    EXPECT_TRUE(chainWith.generate(data, size, size));
    
    MaxHeightMipChain chainWithout;
    EXPECT_TRUE(chainWithout.generateWithoutBase(data, size, size));
    
    // Chain without base should use significantly less memory
    size_t withBaseSize = chainWith.getTotalSizeBytes();
    size_t withoutBaseSize = chainWithout.getTotalSizeBytes();
    
    // Without base should be missing 256×256×2 = 131072 bytes
    EXPECT_EQ(withBaseSize - withoutBaseSize, size * size * sizeof(uint16_t));
}

} // namespace voxy::terrain



