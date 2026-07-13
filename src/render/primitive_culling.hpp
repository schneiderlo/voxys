#pragma once

#include "physics/physics_world.hpp"
#include "render/frustum.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>

namespace voxy::render {

struct PrimitiveCullStats {
    size_t inputCount = 0;
    size_t submittedCount = 0;
    float measuredRejectionRatio = 0.0f;
    bool evaluated = false;
    bool enabled = true;
};

/// Conservative fail-open test. A true result proves the complete primitive
/// lies outside one frustum plane and therefore cannot produce a fragment.
[[nodiscard]] bool primitiveOutsideFrustum(
    const physics::PhysicsWorld::DynamicBodySnapshot& body,
    const Frustum& frustum) noexcept;

/// Stable in-place rejection. Surviving snapshots retain their exact bits and
/// relative order. Returns the number of rejected snapshots.
size_t cullPrimitiveSnapshotsInPlace(
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& bodies,
    const Frustum& frustum);

/// Avoids the all-visible culling cost. Low-rejection scenes are bypassed for a
/// bounded interval, then reprobed so camera or world changes are discovered.
class PrimitiveCullController {
public:
    [[nodiscard]] PrimitiveCullStats cull(
        std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& bodies,
        const glm::mat4& viewProjection);

    void reset() noexcept;

private:
    static constexpr size_t kMinimumBodyCount = 256;
    static constexpr float kEnableRejectionRatio = 0.25f;
    static constexpr uint32_t kProbeIntervalFrames = 30;

    float rollingRejectionRatio_ = 0.0f;
    uint32_t framesUntilProbe_ = 0;
    bool hasMeasurement_ = false;
    bool enabled_ = true;
};

} // namespace voxy::render
