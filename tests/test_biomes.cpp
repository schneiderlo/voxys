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

TEST(BiomeSamplerTest, ClassifiesMinecraftStyleBiomeVariety) {
    struct RoleCase {
        BiomeSurfaceRole role;
        BiomeSampleInput input;
    };

    const RoleCase cases[] = {
        {BiomeSurfaceRole::Jungle, {.worldX = -850.0f, .worldZ = -1650.0f, .heightM = 5.0f, .slope = 0.05f, .surfaceMoistureBias = 0.40f}},
        {BiomeSurfaceRole::Swamp, {.worldX = -2000.0f, .worldZ = -2000.0f, .heightM = 5.0f, .slope = 0.05f, .surfaceMoistureBias = 0.40f}},
        {BiomeSurfaceRole::Savanna, {.worldX = -2000.0f, .worldZ = -2000.0f, .heightM = 5.0f, .slope = 0.05f, .surfaceMoistureBias = -0.35f}},
        {BiomeSurfaceRole::Badlands, {.worldX = -650.0f, .worldZ = 500.0f, .heightM = 5.0f, .slope = 0.30f, .surfaceMoistureBias = -0.60f}},
        {BiomeSurfaceRole::Taiga, {.worldX = -2000.0f, .worldZ = -2000.0f, .heightM = 80.0f, .slope = 0.10f, .surfaceMoistureBias = 0.40f}},
        {BiomeSurfaceRole::Tundra, {.worldX = -2000.0f, .worldZ = -1950.0f, .heightM = 60.0f, .slope = 0.10f, .surfaceMoistureBias = -0.40f}},
        {BiomeSurfaceRole::Meadow, {.worldX = -2000.0f, .worldZ = -2000.0f, .heightM = 20.0f, .slope = 0.05f}},
    };

    for (const RoleCase& roleCase : cases) {
        const BiomeSample sample = sampleBiome(roleCase.input);
        EXPECT_EQ(sample.role, roleCase.role);
    }
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
