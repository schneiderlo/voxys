#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <limits>

namespace voxy::render {

struct Plane {
    glm::vec3 normal;
    float distance;

    // Normalize the plane equation
    [[nodiscard]] bool normalize() noexcept {
        const float mag = glm::length(normal);
        if (!std::isfinite(mag)
            || mag <= std::numeric_limits<float>::min()
            || !std::isfinite(distance)) {
            normal = glm::vec3(0.0f);
            distance = 0.0f;
            return false;
        }
        normal /= mag;
        distance /= mag;
        return true;
    }
};

struct Frustum {
    std::array<Plane, 6> planes;

    static Frustum fromViewProj(const glm::mat4& viewProj) {
        Frustum frustum;
        const auto& m = viewProj;

        // Gribb-Hartmann extraction
        // Left
        frustum.planes[0].normal.x = m[0][3] + m[0][0];
        frustum.planes[0].normal.y = m[1][3] + m[1][0];
        frustum.planes[0].normal.z = m[2][3] + m[2][0];
        frustum.planes[0].distance = m[3][3] + m[3][0];

        // Right
        frustum.planes[1].normal.x = m[0][3] - m[0][0];
        frustum.planes[1].normal.y = m[1][3] - m[1][0];
        frustum.planes[1].normal.z = m[2][3] - m[2][0];
        frustum.planes[1].distance = m[3][3] - m[3][0];

        // Bottom
        frustum.planes[2].normal.x = m[0][3] + m[0][1];
        frustum.planes[2].normal.y = m[1][3] + m[1][1];
        frustum.planes[2].normal.z = m[2][3] + m[2][1];
        frustum.planes[2].distance = m[3][3] + m[3][1];

        // Top
        frustum.planes[3].normal.x = m[0][3] - m[0][1];
        frustum.planes[3].normal.y = m[1][3] - m[1][1];
        frustum.planes[3].normal.z = m[2][3] - m[2][1];
        frustum.planes[3].distance = m[3][3] - m[3][1];

        // Near (WebGPU/D3D/Metal clip space: z >= 0, not z >= -w). The project
        // builds GLM with GLM_FORCE_DEPTH_ZERO_TO_ONE, so the near plane is the
        // z-row alone rather than (w-row + z-row) as in OpenGL's [-1,1] range.
        frustum.planes[4].normal.x = m[0][2];
        frustum.planes[4].normal.y = m[1][2];
        frustum.planes[4].normal.z = m[2][2];
        frustum.planes[4].distance = m[3][2];

        // Far
        frustum.planes[5].normal.x = m[0][3] - m[0][2];
        frustum.planes[5].normal.y = m[1][3] - m[1][2];
        frustum.planes[5].normal.z = m[2][3] - m[2][2];
        frustum.planes[5].distance = m[3][3] - m[3][2];

        for (auto& plane : frustum.planes) {
            (void)plane.normalize();
        }

        return frustum;
    }

    [[nodiscard]] bool valid() const noexcept {
        for (const Plane& plane : planes) {
            const float length = glm::length(plane.normal);
            if (!std::isfinite(length)
                || length <= std::numeric_limits<float>::min()
                || !std::isfinite(plane.distance)) {
                return false;
            }
        }
        return true;
    }
};

} // namespace voxy::render
