// ═══════════════════════════════════════════════════════════════════════════════
// test_shadow_bake.cpp - Unit tests for the shadow height field bake
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "terrain/shadow_bake.hpp"

namespace voxy::terrain {

namespace {

ShadowBakeConfig testConfig() {
    ShadowBakeConfig config;
    config.lightDir = {0.3f, 0.8f, 0.4f};
    config.heightScale = 500.0f;
    config.cellScale = 1.0f;
    config.downsample = 1;  // full resolution keeps assertions exact
    return config;
}

} // namespace

TEST(ShadowBakeTest, RejectsInvalidInput) {
    std::vector<uint16_t> heights(16, 0);
    EXPECT_TRUE(bakeShadowHeightField(heights, 0, 4, testConfig()).data.empty());
    EXPECT_TRUE(bakeShadowHeightField(heights, 4, 0, testConfig()).data.empty());
    EXPECT_TRUE(bakeShadowHeightField(heights, 8, 8, testConfig()).data.empty());

    auto badScale = testConfig();
    badScale.heightScale = 0.0f;
    EXPECT_TRUE(bakeShadowHeightField(heights, 4, 4, badScale).data.empty());

    badScale = testConfig();
    badScale.cellScale = std::numeric_limits<float>::max();
    EXPECT_TRUE(bakeShadowHeightField(
        heights, 4, 4, badScale).data.empty());

    EXPECT_TRUE(bakeShadowHeightField(
        heights, 8'193u, 1u, testConfig()).data.empty());
}

TEST(ShadowBakeTest, OutputDimensionsFollowDownsample) {
    std::vector<uint16_t> heights(64 * 64, 32768);
    auto config = testConfig();
    config.downsample = 2;
    const auto result = bakeShadowHeightField(heights, 64, 64, config);
    EXPECT_EQ(result.width, 32u);
    EXPECT_EQ(result.height, 32u);
    EXPECT_EQ(result.data.size(), 32u * 32u);
    EXPECT_LE(result.scratchBytes, 2u * 64u * sizeof(float));
}

TEST(ShadowBakeTest, FlatTerrainCastsNoShadowAboveItself) {
    constexpr uint16_t kFlat = 32768;
    std::vector<uint16_t> heights(64 * 64, kFlat);
    const auto result = bakeShadowHeightField(heights, 64, 64, testConfig());
    ASSERT_EQ(result.data.size(), 64u * 64u);

    // The boundary excludes each cell's own terrain, so on flat ground it
    // must sit at or below the surface: no point on or above it is shadowed.
    for (const uint16_t boundary : result.data) {
        EXPECT_LE(boundary, kFlat);
    }
}

TEST(ShadowBakeTest, SunStraightUpProducesNoShadow) {
    std::vector<uint16_t> heights(32 * 32, 40000);
    auto config = testConfig();
    config.lightDir = {0.0f, 1.0f, 0.0f};
    const auto result = bakeShadowHeightField(heights, 32, 32, config);
    ASSERT_EQ(result.data.size(), 32u * 32u);
    for (const uint16_t boundary : result.data) {
        EXPECT_EQ(boundary, 0u);
    }
}

TEST(ShadowBakeTest, RejectsZeroOrNonFiniteLightDirection) {
    std::vector<uint16_t> heights(16, 40000);

    auto zeroLight = testConfig();
    zeroLight.lightDir = {0.0f, 0.0f, 0.0f};
    EXPECT_TRUE(bakeShadowHeightField(heights, 4, 4, zeroLight).data.empty());

    auto nanLight = testConfig();
    nanLight.lightDir.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(bakeShadowHeightField(heights, 4, 4, nanLight).data.empty());
}

TEST(ShadowBakeTest, WallShadowsDownstreamAndFades) {
    // Flat floor with a tall wall. The sun azimuth is (+x, +z), so the
    // shadow falls toward -x/-z, i.e. rows with smaller z than the wall.
    constexpr uint32_t kSize = 64;
    constexpr uint16_t kFloor = 20000;
    constexpr uint16_t kWall = 60000;
    std::vector<uint16_t> heights(kSize * kSize, kFloor);
    const uint32_t wallZ = 48;
    for (uint32_t x = 0; x < kSize; ++x) {
        heights[wallZ * kSize + x] = kWall;
    }

    const auto result = bakeShadowHeightField(heights, kSize, kSize, testConfig());
    ASSERT_EQ(result.data.size(), kSize * kSize);

    const auto at = [&](uint32_t x, uint32_t z) {
        return result.data[z * kSize + x];
    };

    // Just downstream of the wall the boundary is close to the wall height
    // (one step of sun-elevation drop, ~131 units for this configuration).
    EXPECT_GT(at(32, wallZ - 1), kWall - 400);
    EXPECT_LT(at(32, wallZ - 1), kWall);

    // The boundary decreases monotonically with distance from the wall.
    uint16_t prev = at(32, wallZ - 1);
    for (uint32_t z = wallZ - 2; z > wallZ - 12; --z) {
        const uint16_t current = at(32, z);
        EXPECT_LE(current, prev);
        prev = current;
    }

    // Upstream of the wall (sun side) the floor stays unshadowed.
    EXPECT_LE(at(32, wallZ + 4), kFloor);
}

} // namespace voxy::terrain
