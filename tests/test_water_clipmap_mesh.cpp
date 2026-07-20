#include <gtest/gtest.h>

#include "render/water_clipmap_mesh.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace voxy::render::detail {
namespace {

using ClipmapEdge = std::array<int32_t, 4>;

[[nodiscard]] ClipmapEdge clipmapEdge(const WaterClipmapVertex& first,
                                      const WaterClipmapVertex& second) {
    std::array<int32_t, 2> a{
        static_cast<int32_t>(std::lround(first.x * 2.0f)),
        static_cast<int32_t>(std::lround(first.z * 2.0f))};
    std::array<int32_t, 2> b{
        static_cast<int32_t>(std::lround(second.x * 2.0f)),
        static_cast<int32_t>(std::lround(second.z * 2.0f))};
    if (b < a) {
        std::swap(a, b);
    }
    return {a[0], a[1], b[0], b[1]};
}

TEST(WaterClipmapMeshTest, LevelTransitionsHaveNoDisplacedTJunctions) {
    const WaterClipmapMesh mesh = makeWaterClipmap();
    ASSERT_FALSE(mesh.vertices.empty());
    ASSERT_EQ(mesh.indices.size() % 3u, 0u);

    std::map<ClipmapEdge, uint32_t> edgeUses;
    for (size_t index = 0; index < mesh.indices.size(); index += 3u) {
        const uint32_t ia = mesh.indices[index];
        const uint32_t ib = mesh.indices[index + 1u];
        const uint32_t ic = mesh.indices[index + 2u];
        ASSERT_LT(ia, mesh.vertices.size());
        ASSERT_LT(ib, mesh.vertices.size());
        ASSERT_LT(ic, mesh.vertices.size());

        const auto& a = mesh.vertices[ia];
        const auto& b = mesh.vertices[ib];
        const auto& c = mesh.vertices[ic];
        const float winding = (b.z - a.z) * (c.x - a.x) -
                              (b.x - a.x) * (c.z - a.z);
        ASSERT_GT(winding, 0.0f);

        ++edgeUses[clipmapEdge(a, b)];
        ++edgeUses[clipmapEdge(b, c)];
        ++edgeUses[clipmapEdge(c, a)];
    }

    constexpr uint32_t kRegularLevels = 5u;
    constexpr uint32_t kSegments = 64u;
    constexpr float kBaseHalfExtent = 400.0f;
    constexpr float kBaseCell = 12.5f;
    for (uint32_t level = 0u; level + 1u < kRegularLevels; ++level) {
        const float scale = static_cast<float>(1u << level);
        const float half = kBaseHalfExtent * scale;
        const float cell = kBaseCell * scale;
        for (uint32_t segment = 0u; segment < kSegments; ++segment) {
            const float begin = -half + static_cast<float>(segment) * cell;
            const float end = begin + cell;
            const WaterClipmapVertex horizontalA{begin, 0.0f, half};
            const WaterClipmapVertex horizontalB{end, 0.0f, half};
            const WaterClipmapVertex verticalA{half, 0.0f, begin};
            const WaterClipmapVertex verticalB{half, 0.0f, end};

            EXPECT_EQ(edgeUses[clipmapEdge(horizontalA, horizontalB)], 2u);
            EXPECT_EQ(edgeUses[clipmapEdge(
                          {begin, 0.0f, -half}, {end, 0.0f, -half})],
                      2u);
            EXPECT_EQ(edgeUses[clipmapEdge(verticalA, verticalB)], 2u);
            EXPECT_EQ(edgeUses[clipmapEdge(
                          {-half, 0.0f, begin}, {-half, 0.0f, end})],
                      2u);
        }
    }
}

} // namespace
} // namespace voxy::render::detail
