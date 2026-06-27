// ═══════════════════════════════════════════════════════════════════════════════
// test_decorations.cpp - Tests for Biome-Driven Decoration Placement
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "terrain/decorations.hpp"

namespace voxy::terrain {

namespace {

size_t countKind(const std::vector<TerrainDecoration>& decorations, DecorationKind kind) {
    return static_cast<size_t>(std::count_if(
        decorations.begin(),
        decorations.end(),
        [kind](const TerrainDecoration& decoration) {
            return decoration.kind == kind;
        }
    ));
}

size_t countTrees(const std::vector<TerrainDecoration>& decorations) {
    return static_cast<size_t>(std::count_if(
        decorations.begin(),
        decorations.end(),
        [](const TerrainDecoration& decoration) {
            return isTreeDecoration(decoration.kind);
        }
    ));
}

} // namespace

TEST(DecorationGenerationTest, InvalidConfigProducesNoTrees) {
    DecorationConfig config;
    EXPECT_FALSE(hasDecorationHeightSamples(config));
    EXPECT_TRUE(generateTreeDecorations(config).empty());
}

TEST(DecorationGenerationTest, BiomeTreeSpeciesAreTreeDecorations) {
    EXPECT_TRUE(isTreeDecoration(DecorationKind::BroadleafTree));
    EXPECT_TRUE(isTreeDecoration(DecorationKind::PineTree));
    EXPECT_TRUE(isTreeDecoration(DecorationKind::JungleTree));
    EXPECT_TRUE(isTreeDecoration(DecorationKind::AcaciaTree));
    EXPECT_TRUE(isTreeDecoration(DecorationKind::CypressTree));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::GrassClump));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::FlowerPatch));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::ReedCluster));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::RockShard));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::DryShrub));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::MushroomCluster));
    EXPECT_FALSE(isTreeDecoration(DecorationKind::CactusColumn));
}

TEST(DecorationGenerationTest, UnderwaterTerrainProducesNoTrees) {
    std::vector<uint16_t> samples(64u * 64u, 0u);

    DecorationConfig config;
    config.heightSamples = samples;
    config.width = 64;
    config.height = 64;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;
    config.spacingCells = 8;
    config.maxTrees = 1000;

    EXPECT_TRUE(hasDecorationHeightSamples(config));
    EXPECT_TRUE(generateTreeDecorations(config).empty());
}

TEST(DecorationGenerationTest, FlatLowlandProducesDeterministicTrees) {
    std::vector<uint16_t> samples(256u * 256u, 36044u); // Around +5m at heightScale 50.

    DecorationConfig config;
    config.heightSamples = samples;
    config.width = 256;
    config.height = 256;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;
    config.spacingCells = 6;
    config.maxTrees = 5000;

    auto first = generateTreeDecorations(config);
    auto second = generateTreeDecorations(config);

    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first.size(), second.size());
    EXPECT_LE(first.size(), static_cast<size_t>(config.maxTrees));

    ASSERT_FALSE(second.empty());
    EXPECT_FLOAT_EQ(first.front().x, second.front().x);
    EXPECT_FLOAT_EQ(first.front().y, second.front().y);
    EXPECT_FLOAT_EQ(first.front().z, second.front().z);
    EXPECT_FLOAT_EQ(first.front().radius, second.front().radius);
    EXPECT_FLOAT_EQ(first.front().height, second.front().height);
    EXPECT_EQ(countTrees(first), first.size());
}

TEST(DecorationGenerationTest, RespectsMaxTreeCap) {
    std::vector<uint16_t> samples(128u * 128u, 36044u);

    DecorationConfig config;
    config.heightSamples = samples;
    config.width = 128;
    config.height = 128;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;
    config.spacingCells = 4;
    config.maxTrees = 12;

    auto trees = generateTreeDecorations(config);
    EXPECT_LE(trees.size(), 12u);
}

TEST(DecorationGenerationTest, TerrainDecorationsAddDeterministicGroundCover) {
    std::vector<uint16_t> samples(192u * 192u, 36044u); // Around +5m at heightScale 50.

    DecorationConfig config;
    config.heightSamples = samples;
    config.width = 192;
    config.height = 192;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;
    config.spacingCells = 6;
    config.maxTrees = 0;
    config.maxGroundDecorations = 900;

    auto first = generateTerrainDecorations(config);
    auto second = generateTerrainDecorations(config);

    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first.size(), second.size());
    EXPECT_EQ(countTrees(first), 0u);
    EXPECT_LE(first.size(), static_cast<size_t>(config.maxGroundDecorations));
    EXPECT_GT(countKind(first, DecorationKind::GrassClump) +
              countKind(first, DecorationKind::FlowerPatch) +
              countKind(first, DecorationKind::MushroomCluster) +
              countKind(first, DecorationKind::CactusColumn), 0u);
    EXPECT_FLOAT_EQ(first.front().x, second.front().x);
    EXPECT_FLOAT_EQ(first.front().z, second.front().z);
}

TEST(DecorationGenerationTest, RockySlopesProduceRockShards) {
    std::vector<uint16_t> samples(128u * 128u);
    for (uint32_t y = 0; y < 128u; ++y) {
        for (uint32_t x = 0; x < 128u; ++x) {
            samples[static_cast<size_t>(y) * 128u + x] = static_cast<uint16_t>(8000u + x * 420u);
        }
    }

    DecorationConfig config;
    config.heightSamples = samples;
    config.width = 128;
    config.height = 128;
    config.heightScale = 50.0f;
    config.cellScale = 1.0f;
    config.spacingCells = 6;
    config.maxTrees = 0;
    config.maxGroundDecorations = 500;

    auto decorations = generateTerrainDecorations(config);
    EXPECT_GT(countKind(decorations, DecorationKind::RockShard), 0u);
    EXPECT_LE(decorations.size(), static_cast<size_t>(config.maxGroundDecorations));
}

} // namespace voxy::terrain
