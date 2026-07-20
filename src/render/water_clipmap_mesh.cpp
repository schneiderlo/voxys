#include "render/water_clipmap_mesh.hpp"

#include <cstddef>
#include <limits>

namespace voxy::render::detail {

WaterClipmapMesh makeWaterClipmap() {
    constexpr uint32_t kSegments = 64u;
    constexpr uint32_t kLevels = 5u;
    constexpr float kBasePatchSize = 800.0f;
    constexpr float kFarExtent = 47'500.0f;
    constexpr uint32_t kRowSize = kSegments + 1u;
    constexpr uint32_t kInnerBegin = kSegments / 4u;
    constexpr uint32_t kInnerEnd = kSegments - kInnerBegin;
    constexpr uint32_t kMissing = std::numeric_limits<uint32_t>::max();

    WaterClipmapMesh mesh;
    constexpr size_t kApproximateCells =
        static_cast<size_t>(kSegments) * kSegments *
        (1u + 3u * (kLevels - 1u) / 4u);
    mesh.vertices.reserve(kApproximateCells * 4u);
    mesh.indices.reserve(kApproximateCells * 6u);

    for (uint32_t level = 0u; level < kLevels; ++level) {
        const float extent = kBasePatchSize * static_cast<float>(1u << level);
        const float half = extent * 0.5f;
        const float cell = extent / static_cast<float>(kSegments);
        std::vector<uint32_t> grid(
            static_cast<size_t>(kRowSize) * kRowSize, kMissing);
        const auto vertexAt = [&](uint32_t x, uint32_t z) {
            uint32_t& index = grid[static_cast<size_t>(z) * kRowSize + x];
            if (index == kMissing) {
                index = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({
                    -half + static_cast<float>(x) * cell,
                    0.0f,
                    -half + static_cast<float>(z) * cell});
            }
            return index;
        };
        const auto midpointBetween = [&mesh](uint32_t first,
                                             uint32_t second) {
            const WaterClipmapVertex a = mesh.vertices[first];
            const WaterClipmapVertex b = mesh.vertices[second];
            const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({
                (a.x + b.x) * 0.5f,
                (a.y + b.y) * 0.5f,
                (a.z + b.z) * 0.5f});
            return index;
        };

        for (uint32_t z = 0u; z < kSegments; ++z) {
            for (uint32_t x = 0u; x < kSegments; ++x) {
                if (level > 0u && x >= kInnerBegin && x < kInnerEnd &&
                    z >= kInnerBegin && z < kInnerEnd) {
                    continue;
                }
                const uint32_t a = vertexAt(x, z);
                const uint32_t b = vertexAt(x + 1u, z);
                const uint32_t c = vertexAt(x, z + 1u);
                const uint32_t d = vertexAt(x + 1u, z + 1u);

                // Adjacent clipmap levels differ by 2:1. Split the coarse
                // cell's edge at every fine-level vertex so wave displacement
                // cannot pull a T-junction open into a visible crack.
                const bool alongInnerX = x >= kInnerBegin && x < kInnerEnd;
                const bool alongInnerZ = z >= kInnerBegin && z < kInnerEnd;
                if (level > 0u && z + 1u == kInnerBegin && alongInnerX) {
                    const uint32_t midpoint = midpointBetween(c, d);
                    mesh.indices.insert(mesh.indices.end(),
                        {a, c, b, b, c, midpoint, b, midpoint, d});
                } else if (level > 0u && z == kInnerEnd && alongInnerX) {
                    const uint32_t midpoint = midpointBetween(a, b);
                    mesh.indices.insert(mesh.indices.end(),
                        {a, c, midpoint, midpoint, c, b, b, c, d});
                } else if (level > 0u && x + 1u == kInnerBegin &&
                           alongInnerZ) {
                    const uint32_t midpoint = midpointBetween(b, d);
                    mesh.indices.insert(mesh.indices.end(),
                        {a, c, b, b, c, midpoint, midpoint, c, d});
                } else if (level > 0u && x == kInnerEnd && alongInnerZ) {
                    const uint32_t midpoint = midpointBetween(a, c);
                    mesh.indices.insert(mesh.indices.end(),
                        {a, midpoint, b, midpoint, c, b, b, c, d});
                } else {
                    mesh.indices.insert(mesh.indices.end(),
                                        {a, c, b, b, c, d});
                }
            }
        }
    }

    // Four radially stretched strips close the underwater horizon beyond the
    // last regular ring without increasing its tessellation density.
    constexpr float kOuterLevelExtent =
        kBasePatchSize * static_cast<float>(1u << (kLevels - 1u));
    constexpr float kInnerHalf = kOuterLevelExtent * 0.5f;
    constexpr float kOuterHalf = kFarExtent * 0.5f;
    constexpr float kRadialScale = kOuterHalf / kInnerHalf;
    constexpr float kStep = kOuterLevelExtent / static_cast<float>(kSegments);
    const auto addQuad = [&mesh](WaterClipmapVertex innerA,
                                 WaterClipmapVertex innerB,
                                 WaterClipmapVertex outerA,
                                 WaterClipmapVertex outerB,
                                 bool reverse) {
        const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(),
                             {innerA, innerB, outerA, outerB});
        if (reverse) {
            mesh.indices.insert(mesh.indices.end(),
                                {first, first + 1u, first + 3u,
                                 first, first + 3u, first + 2u});
        } else {
            mesh.indices.insert(mesh.indices.end(),
                                {first, first + 2u, first + 3u,
                                 first, first + 3u, first + 1u});
        }
    };
    for (uint32_t segment = 0u; segment < kSegments; ++segment) {
        const float a = static_cast<float>(segment) * kStep - kInnerHalf;
        const float b = segment + 1u == kSegments
            ? kInnerHalf
            : static_cast<float>(segment + 1u) * kStep - kInnerHalf;
        const float outerA = a * kRadialScale;
        const float outerB = b * kRadialScale;
        addQuad({a, 0.0f, kInnerHalf}, {b, 0.0f, kInnerHalf},
                {outerA, 0.0f, kOuterHalf}, {outerB, 0.0f, kOuterHalf}, false);
        addQuad({a, 0.0f, -kInnerHalf}, {b, 0.0f, -kInnerHalf},
                {outerA, 0.0f, -kOuterHalf}, {outerB, 0.0f, -kOuterHalf}, true);
        addQuad({kInnerHalf, 0.0f, a}, {kInnerHalf, 0.0f, b},
                {kOuterHalf, 0.0f, outerA}, {kOuterHalf, 0.0f, outerB}, true);
        addQuad({-kInnerHalf, 0.0f, a}, {-kInnerHalf, 0.0f, b},
                {-kOuterHalf, 0.0f, outerA}, {-kOuterHalf, 0.0f, outerB}, false);
    }
    return mesh;
}

} // namespace voxy::render::detail
