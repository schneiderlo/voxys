#pragma once

#include <box3d/box3d.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>

namespace voxy::physics::box3d_conversion {

[[nodiscard]] inline b3Vec3 toVector(const glm::vec3& value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] inline b3Pos toPosition(const glm::vec3& value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] inline glm::vec3 toGlmVector(b3Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] inline glm::vec3 toGlmPosition(b3Pos value) noexcept {
    return {static_cast<float>(value.x), static_cast<float>(value.y),
            static_cast<float>(value.z)};
}

[[nodiscard]] inline glm::quat toGlm(b3Quat value) noexcept {
    return {value.s, value.v.x, value.v.y, value.v.z};
}

// Box3D splits a heightfield cell along TR-BL. Rotating its local grid +90
// degrees around Y maps that edge to Voxys' canonical world-space TL-BR edge.
// Samples are rotated in the opposite direction so their world coordinates and
// heights remain unchanged.
struct CanonicalHeightFieldLayout {
    uint32_t worldWidth = 0;
    uint32_t worldHeight = 0;

    [[nodiscard]] constexpr uint32_t localCountX() const noexcept {
        return worldHeight;
    }

    [[nodiscard]] constexpr uint32_t localCountZ() const noexcept {
        return worldWidth;
    }

    [[nodiscard]] constexpr size_t sourceIndex(
        uint32_t localX, uint32_t localZ) const noexcept {
        const uint32_t worldX = localZ;
        const uint32_t worldZ = worldHeight - 1u - localX;
        return static_cast<size_t>(worldZ) * worldWidth + worldX;
    }
};

} // namespace voxy::physics::box3d_conversion
