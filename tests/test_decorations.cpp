// ═══════════════════════════════════════════════════════════════════════════════
// test_decorations.cpp - Tests for Biome-Driven Decoration Placement
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "terrain/decorations.hpp"

namespace voxy::terrain {

TEST(DecorationGenerationTest, InvalidConfigProducesNoTrees) {
    DecorationConfig config;
    EXPECT_FALSE(hasDecorationHeightSamples(config));
    EXPECT_TRUE(generateTreeDecorations(config).empty());
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

} // namespace voxy::terrain
