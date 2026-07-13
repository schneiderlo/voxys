#include "render/primitive_culling.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace voxy::render {
namespace {

bool finiteSnapshot(
    const physics::PhysicsWorld::DynamicBodySnapshot& body) noexcept {
    return std::isfinite(body.position.x)
        && std::isfinite(body.position.y)
        && std::isfinite(body.position.z)
        && std::isfinite(body.rotation.w)
        && std::isfinite(body.rotation.x)
        && std::isfinite(body.rotation.y)
        && std::isfinite(body.rotation.z)
        && std::isfinite(body.dimensions.x)
        && std::isfinite(body.dimensions.y)
        && std::isfinite(body.dimensions.z);
}

bool prepareFrustum(const Frustum& frustum,
                    std::array<float, 6>& normalLengths) noexcept {
    for (size_t index = 0; index < frustum.planes.size(); ++index) {
        const Plane& plane = frustum.planes[index];
        normalLengths[index] = glm::length(plane.normal);
        if (!std::isfinite(normalLengths[index])
            || normalLengths[index] <= 0.0f
            || !std::isfinite(plane.distance)) {
            return false;
        }
    }
    return true;
}

bool outsidePreparedFrustum(
    const physics::PhysicsWorld::DynamicBodySnapshot& body,
    const Frustum& frustum,
    const std::array<float, 6>& normalLengths) noexcept {
    if (!finiteSnapshot(body)) return false;

    // Every primitive vertex lies inside local [-0.5, 0.5]^3. For GLM's
    // non-unit quaternion matrix, s + |1 - s| bounds the rotation operator.
    const float quaternionLengthSquared = glm::dot(body.rotation, body.rotation);
    const float rotationBound = quaternionLengthSquared
                              + std::abs(1.0f - quaternionLengthSquared);
    const float radius = 0.5f * glm::length(glm::abs(body.dimensions))
                       * rotationBound;
    if (!std::isfinite(radius)) return false;

    for (size_t index = 0; index < frustum.planes.size(); ++index) {
        const Plane& plane = frustum.planes[index];
        const float normalLength = normalLengths[index];
        const float scaledRadius = radius * normalLength;
        const float distance = glm::dot(plane.normal, body.position)
                             + plane.distance;
        if (!std::isfinite(scaledRadius) || !std::isfinite(distance)) {
            return false;
        }
        if (distance >= -scaledRadius) continue;

        const float magnitude = glm::dot(glm::abs(plane.normal),
                                         glm::abs(body.position))
                              + std::abs(plane.distance) + scaledRadius;
        const float margin = 64.0f * std::numeric_limits<float>::epsilon()
                           * magnitude + 0.001f * normalLength;
        if (!std::isfinite(margin)) return false;
        if (distance < -scaledRadius - margin) return true;
    }
    return false;
}

} // namespace

bool primitiveOutsideFrustum(
    const physics::PhysicsWorld::DynamicBodySnapshot& body,
    const Frustum& frustum) noexcept {
    std::array<float, 6> normalLengths{};
    return prepareFrustum(frustum, normalLengths)
        && outsidePreparedFrustum(body, frustum, normalLengths);
}

size_t cullPrimitiveSnapshotsInPlace(
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& bodies,
    const Frustum& frustum) {
    const size_t inputCount = bodies.size();
    std::array<float, 6> normalLengths{};
    if (!prepareFrustum(frustum, normalLengths)) return 0;
    std::erase_if(bodies, [&](const auto& body) {
        return outsidePreparedFrustum(body, frustum, normalLengths);
    });
    return inputCount - bodies.size();
}

PrimitiveCullStats PrimitiveCullController::cull(
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& bodies,
    const glm::mat4& viewProjection) {
    PrimitiveCullStats stats;
    stats.inputCount = bodies.size();
    stats.submittedCount = bodies.size();
    stats.measuredRejectionRatio = rollingRejectionRatio_;
    stats.enabled = enabled_;

    if (bodies.size() < kMinimumBodyCount) return stats;
    if (!enabled_ && framesUntilProbe_ != 0) {
        --framesUntilProbe_;
        return stats;
    }

    stats.evaluated = true;
    const Frustum frustum = Frustum::fromViewProj(viewProjection);
    const size_t rejected = cullPrimitiveSnapshotsInPlace(bodies, frustum);
    stats.submittedCount = bodies.size();
    const float measured = static_cast<float>(rejected)
                         / static_cast<float>(stats.inputCount);

    if (!hasMeasurement_ || !enabled_) {
        rollingRejectionRatio_ = measured;
        hasMeasurement_ = true;
    } else {
        constexpr float kNewMeasurementWeight = 0.25f;
        rollingRejectionRatio_ += kNewMeasurementWeight
                                * (measured - rollingRejectionRatio_);
    }

    if (rollingRejectionRatio_ >= kEnableRejectionRatio) {
        enabled_ = true;
        framesUntilProbe_ = 0;
    } else {
        enabled_ = false;
        framesUntilProbe_ = kProbeIntervalFrames;
    }

    stats.measuredRejectionRatio = rollingRejectionRatio_;
    stats.enabled = enabled_;
    return stats;
}

void PrimitiveCullController::reset() noexcept {
    rollingRejectionRatio_ = 0.0f;
    framesUntilProbe_ = 0;
    hasMeasurement_ = false;
    enabled_ = true;
}

} // namespace voxy::render
