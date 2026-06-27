// ═══════════════════════════════════════════════════════════════════════════════
// test_biomes.cpp - Tests for Shared Terrain Biome Sampling
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include "terrain/biomes.hpp"

namespace voxy::terrain {

TEST(BiomeSamplerTest, OceanClassifiesAsWater) {
    const BiomeSample sample = sampleBiome(BiomeSampleInput{
        .worldX = 0.0f,
        .worldZ = 0.0f,
        .heightM = -8.0f,
        .slope = 0.0f,
    });

    EXPECT_TRUE(sample.isWater());
    EXPECT_EQ(sample.role, BiomeSurfaceRole::Water);
    EXPECT_GT(sample.water, 0.9f);
    EXPECT_FLOAT_EQ(sample.treeDensity, 0.0f);
}

TEST(BiomeSamplerTest, TreeDensityRejectsUnsuitableTerrain) {
    EXPECT_FLOAT_EQ(estimateBiomeTreeDensity(-3.0f, 0.0f, 0.9f, 18.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(estimateBiomeTreeDensity(10.0f, 0.8f, 0.9f, 18.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(estimateBiomeTreeDensity(10.0f, 0.0f, 0.9f, 18.0f, 0.3f), 0.0f);
}

TEST(BiomeSamplerTest, MoistLowlandsSupportTrees) {
    const float density = estimateBiomeTreeDensity(5.0f, 0.0f, 0.82f, 18.0f, 0.0f);
    EXPECT_GT(density, 0.5f);
}

TEST(BiomeSamplerTest, SamplingIsDeterministic) {
    const BiomeSampleInput input{
        .worldX = 123.0f,
        .worldZ = -456.0f,
        .heightM = 12.0f,
        .slope = 0.1f,
        .surfaceMoistureBias = 0.03f,
        .seed = 42u,
    };

    const BiomeSample a = sampleBiome(input);
    const BiomeSample b = sampleBiome(input);

    EXPECT_FLOAT_EQ(a.moisture, b.moisture);
    EXPECT_FLOAT_EQ(a.temperature, b.temperature);
    EXPECT_FLOAT_EQ(a.river, b.river);
    EXPECT_FLOAT_EQ(a.treeDensity, b.treeDensity);
    EXPECT_EQ(a.role, b.role);
}

} // namespace voxy::terrain
