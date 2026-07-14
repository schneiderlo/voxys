#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace voxy::physics::terrain_topology {

inline constexpr float kHeightSampleMaximum = 65'535.0f;

enum class CellCorner : uint8_t {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
};

using TriangleCorners = std::array<CellCorner, 3>;

// Canonical Voxys TL-BR diagonal. Winding is counter-clockwise from above.
inline constexpr std::array<TriangleCorners, 2> kCellTriangles{{
    {CellCorner::TopLeft, CellCorner::BottomRight,
     CellCorner::BottomLeft},
    {CellCorner::TopLeft, CellCorner::TopRight,
     CellCorner::BottomRight},
}};

[[nodiscard]] constexpr float normalizedHeight(float sample) noexcept {
    return sample / kHeightSampleMaximum;
}

[[nodiscard]] constexpr float worldHeight(float sample,
                                          float heightScale) noexcept {
    return heightScale * (2.0f * normalizedHeight(sample) - 1.0f);
}

// Positive half extent used by shaders; sample (0, 0) is at -origin.
[[nodiscard]] inline glm::vec2 centeredOrigin(uint32_t width, uint32_t height,
                                              float cellScale) noexcept {
    return 0.5f * glm::vec2(
        static_cast<float>(width > 0 ? width - 1 : 0),
        static_cast<float>(height > 0 ? height - 1 : 0)) * cellScale;
}

[[nodiscard]] inline glm::vec2 sampleWorldXZ(uint32_t x, uint32_t z,
                                             uint32_t width, uint32_t height,
                                             float cellScale) noexcept {
    return glm::vec2(static_cast<float>(x), static_cast<float>(z)) * cellScale
         - centeredOrigin(width, height, cellScale);
}

} // namespace voxy::physics::terrain_topology
