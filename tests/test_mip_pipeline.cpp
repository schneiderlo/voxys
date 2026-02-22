// ═══════════════════════════════════════════════════════════════════════════════
// test_mip_pipeline.cpp - Unit tests for GPU Mip Generation Pipeline
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for MipGeneratorPipeline class and utility functions.
// Note: GPU pipeline functionality tests require a valid WebGPU device
// and are marked with appropriate skip conditions.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "render/mip_pipeline.hpp"

#include <cmath>
#include <vector>

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipPipelineUtilityTest, CalculateMipLevelCount) {
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

TEST(MipPipelineUtilityTest, GetMipDimensions) {
    // 8×8 base
    auto [w0, h0] = getMipDimensions(8, 8, 0);
    EXPECT_EQ(w0, 8u);
    EXPECT_EQ(h0, 8u);
    
    auto [w1, h1] = getMipDimensions(8, 8, 1);
    EXPECT_EQ(w1, 4u);
    EXPECT_EQ(h1, 4u);
    
    auto [w2, h2] = getMipDimensions(8, 8, 2);
    EXPECT_EQ(w2, 2u);
    EXPECT_EQ(h2, 2u);
    
    auto [w3, h3] = getMipDimensions(8, 8, 3);
    EXPECT_EQ(w3, 1u);
    EXPECT_EQ(h3, 1u);
    
    // Non-square 16×4
    auto [nw0, nh0] = getMipDimensions(16, 4, 0);
    EXPECT_EQ(nw0, 16u);
    EXPECT_EQ(nh0, 4u);
    
    auto [nw1, nh1] = getMipDimensions(16, 4, 1);
    EXPECT_EQ(nw1, 8u);
    EXPECT_EQ(nh1, 2u);
    
    auto [nw2, nh2] = getMipDimensions(16, 4, 2);
    EXPECT_EQ(nw2, 4u);
    EXPECT_EQ(nh2, 1u);
    
    // Large dimensions (8192×8192)
    auto [large0_w, large0_h] = getMipDimensions(8192, 8192, 0);
    EXPECT_EQ(large0_w, 8192u);
    EXPECT_EQ(large0_h, 8192u);
    
    auto [large7_w, large7_h] = getMipDimensions(8192, 8192, 7);
    EXPECT_EQ(large7_w, 64u);
    EXPECT_EQ(large7_h, 64u);
    
    auto [large13_w, large13_h] = getMipDimensions(8192, 8192, 13);
    EXPECT_EQ(large13_w, 1u);
    EXPECT_EQ(large13_h, 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MipParams Structure Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipParamsTest, SizeAndAlignment) {
    // MipParams must be 16 bytes for proper GPU alignment
    EXPECT_EQ(sizeof(MipParams), 16u);
    
    // Verify struct layout
    MipParams params = {
        .srcWidth = 256,
        .srcHeight = 256,
        .dstWidth = 128,
        .dstHeight = 128
    };
    
    EXPECT_EQ(params.srcWidth, 256u);
    EXPECT_EQ(params.srcHeight, 256u);
    EXPECT_EQ(params.dstWidth, 128u);
    EXPECT_EQ(params.dstHeight, 128u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MipGeneratorPipeline Tests (No GPU Required)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipGeneratorPipelineTest, DefaultConstruction) {
    MipGeneratorPipeline pipeline;
    
    EXPECT_FALSE(pipeline.isInitialized());
}

TEST(MipGeneratorPipelineTest, WorkgroupSize) {
    // Verify workgroup size constants match shader
    EXPECT_EQ(MipGeneratorPipeline::getWorkgroupSizeX(), 8u);
    EXPECT_EQ(MipGeneratorPipeline::getWorkgroupSizeY(), 8u);
}

TEST(MipGeneratorPipelineTest, CalculateDispatch) {
    // Exact multiples of workgroup size
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(8), 1u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(16), 2u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(64), 8u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(256), 32u);
    
    // Non-multiples (should round up)
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(1), 1u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(7), 1u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(9), 2u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(15), 2u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(17), 3u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(63), 8u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchX(65), 9u);
    
    // Same for Y
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchY(8), 1u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchY(1), 1u);
    EXPECT_EQ(MipGeneratorPipeline::calculateDispatchY(9), 2u);
}

TEST(MipGeneratorPipelineTest, InitWithNullDevice) {
    MipGeneratorPipeline pipeline;
    
    EXPECT_FALSE(pipeline.initWithSource(nullptr, "// empty shader"));
    EXPECT_FALSE(pipeline.isInitialized());
}

TEST(MipGeneratorPipelineTest, MoveConstruction) {
    MipGeneratorPipeline pipeline1;
    // Can't really test move semantics without a device,
    // but we can verify the move doesn't crash
    
    MipGeneratorPipeline pipeline2(std::move(pipeline1));
    EXPECT_FALSE(pipeline2.isInitialized());
}

TEST(MipGeneratorPipelineTest, MoveAssignment) {
    MipGeneratorPipeline pipeline1;
    MipGeneratorPipeline pipeline2;
    
    pipeline2 = std::move(pipeline1);
    EXPECT_FALSE(pipeline2.isInitialized());
}

TEST(MipGeneratorPipelineTest, ShutdownWithoutInit) {
    MipGeneratorPipeline pipeline;
    // Should not crash
    pipeline.shutdown();
    EXPECT_FALSE(pipeline.isInitialized());
}

TEST(MipGeneratorPipelineTest, GenerateMipChainWithoutInit) {
    MipGeneratorPipeline pipeline;
    
    // Should fail gracefully without initialization
    EXPECT_FALSE(pipeline.generateMipChain(nullptr, nullptr, nullptr, 4));
}

TEST(MipGeneratorPipelineTest, GenerateSingleMipWithoutInit) {
    MipGeneratorPipeline pipeline;
    
    // Should fail gracefully without initialization
    EXPECT_FALSE(pipeline.generateSingleMip(nullptr, nullptr, nullptr, 0, 1));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Dispatch Calculation Integration Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipGeneratorPipelineTest, DispatchForMipChain) {
    // Simulate dispatch calculations for a realistic mip chain (256×256)
    uint32_t baseWidth = 256;
    uint32_t baseHeight = 256;
    uint32_t mipCount = calculateMipLevelCount(baseWidth, baseHeight);
    
    EXPECT_EQ(mipCount, 9u);  // 256, 128, 64, 32, 16, 8, 4, 2, 1
    
    // For each mip level, verify dispatch dimensions
    for (uint32_t level = 1; level < mipCount; level++) {
        auto [dstWidth, dstHeight] = getMipDimensions(baseWidth, baseHeight, level);
        uint32_t dispatchX = MipGeneratorPipeline::calculateDispatchX(dstWidth);
        uint32_t dispatchY = MipGeneratorPipeline::calculateDispatchY(dstHeight);
        
        // Each dispatch should cover all destination texels
        EXPECT_GE(dispatchX * MipGeneratorPipeline::getWorkgroupSizeX(), dstWidth);
        EXPECT_GE(dispatchY * MipGeneratorPipeline::getWorkgroupSizeY(), dstHeight);
        
        // But shouldn't be more than one workgroup larger than needed
        EXPECT_LT(dispatchX * MipGeneratorPipeline::getWorkgroupSizeX(), 
                  dstWidth + MipGeneratorPipeline::getWorkgroupSizeX());
    }
}

TEST(MipGeneratorPipelineTest, DispatchForLargeTerrain) {
    // Verify dispatch for 8192×8192 terrain
    uint32_t baseWidth = 8192;
    uint32_t baseHeight = 8192;
    
    // Mip level 7 (64×64 used by ray-caster)
    auto [mip7_w, mip7_h] = getMipDimensions(baseWidth, baseHeight, 7);
    EXPECT_EQ(mip7_w, 64u);
    EXPECT_EQ(mip7_h, 64u);
    
    uint32_t dispatchX = MipGeneratorPipeline::calculateDispatchX(mip7_w);
    uint32_t dispatchY = MipGeneratorPipeline::calculateDispatchY(mip7_h);
    EXPECT_EQ(dispatchX, 8u);  // 64 / 8 = 8 workgroups
    EXPECT_EQ(dispatchY, 8u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Memory Efficiency Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MipGeneratorPipelineTest, MipChainMemoryCalculation) {
    // Calculate total memory for a mip chain (256×256 R16Uint)
    uint32_t baseWidth = 256;
    uint32_t baseHeight = 256;
    uint32_t mipCount = calculateMipLevelCount(baseWidth, baseHeight);
    
    size_t totalBytes = 0;
    for (uint32_t level = 0; level < mipCount; level++) {
        auto [w, h] = getMipDimensions(baseWidth, baseHeight, level);
        totalBytes += w * h * sizeof(uint16_t);
    }
    
    // 256*256*2 + 128*128*2 + ... + 1*1*2
    // = 131072 + 32768 + 8192 + 2048 + 512 + 128 + 32 + 8 + 2 = 174762 bytes
    EXPECT_EQ(totalBytes, 174762u);
}

TEST(MipGeneratorPipelineTest, LargeTerrainMipChainMemory) {
    // Calculate total memory for 8192×8192 R16Uint mip chain
    uint32_t baseWidth = 8192;
    uint32_t baseHeight = 8192;
    uint32_t mipCount = calculateMipLevelCount(baseWidth, baseHeight);
    
    EXPECT_EQ(mipCount, 14u);
    
    size_t totalBytes = 0;
    for (uint32_t level = 0; level < mipCount; level++) {
        auto [w, h] = getMipDimensions(baseWidth, baseHeight, level);
        totalBytes += w * h * sizeof(uint16_t);
    }
    
    // Should be approximately 170 MB (128 MB base + ~42 MB mips)
    // Exact: 134217728 + 33554432 + 8388608 + ... = ~178956970 bytes
    EXPECT_GT(totalBytes, 170 * 1024 * 1024);  // > 170 MB
    EXPECT_LT(totalBytes, 180 * 1024 * 1024);  // < 180 MB
}

} // namespace voxy::render



