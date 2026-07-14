#include <gtest/gtest.h>

#include "physics/terrain_topology.hpp"

#include <array>
#include <cstdint>

namespace voxy::physics::terrain_topology {
namespace {

TEST(TerrainTopologyTest, UsesCanonicalHeightMapping) {
    constexpr float heightScale = 32.0f;
    EXPECT_FLOAT_EQ(worldHeight(0.0f, heightScale), -heightScale);
    EXPECT_FLOAT_EQ(worldHeight(65'535.0f, heightScale), heightScale);
    EXPECT_NEAR(worldHeight(32'768.0f, heightScale),
                heightScale / 65'535.0f, 1.0e-6f);
}

TEST(TerrainTopologyTest, CentersSamplesAroundWorldOrigin) {
    const glm::vec2 origin = centeredOrigin(3, 5, 2.0f);
    EXPECT_EQ(origin, glm::vec2(2.0f, 4.0f));
    EXPECT_EQ(sampleWorldXZ(0, 0, 3, 5, 2.0f),
              glm::vec2(-2.0f, -4.0f));
    EXPECT_EQ(sampleWorldXZ(1, 2, 3, 5, 2.0f), glm::vec2(0.0f));
    EXPECT_EQ(sampleWorldXZ(2, 4, 3, 5, 2.0f),
              glm::vec2(2.0f, 4.0f));
}

TEST(TerrainTopologyTest, UsesTopLeftToBottomRightDiagonal) {
    constexpr std::array<TriangleCorners, 2> expected{{
        {CellCorner::TopLeft, CellCorner::BottomRight,
         CellCorner::BottomLeft},
        {CellCorner::TopLeft, CellCorner::TopRight,
         CellCorner::BottomRight},
    }};
    EXPECT_EQ(kCellTriangles, expected);

    // The two shared vertices define the canonical TL-BR diagonal.
    EXPECT_EQ(kCellTriangles[0][0], CellCorner::TopLeft);
    EXPECT_EQ(kCellTriangles[0][1], CellCorner::BottomRight);
    EXPECT_EQ(kCellTriangles[1][0], CellCorner::TopLeft);
    EXPECT_EQ(kCellTriangles[1][2], CellCorner::BottomRight);
}

} // namespace
} // namespace voxy::physics::terrain_topology
