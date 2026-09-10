#pragma once

#include "physics/authored_shape.hpp"
#include "physics/physics_types.hpp"
#include "physics/shape_handle.hpp"

namespace voxy::physics {

// Named frames prevent a root-origin velocity or pose from being passed as a
// COM/principal-body state by accident. Angular velocity uses world axes in both.
struct AuthoredRootMotion {
    WorldPosition position{};
    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 originVelocity{0};
    glm::vec3 angularVelocity{0};
};
struct AuthoredBodyMotion {
    WorldPosition centerPosition{};
    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 centerVelocity{0};
    glm::vec3 angularVelocity{0};
};

enum class AuthoredBodyMotionType : uint8_t { Dynamic, Static };
// Static admission uses the shape's reference frame but zero inverse mass and
// inertia. Initial velocity, later pose/velocity changes and water drivers are
// refused; replace the fixed body to move scenery.
struct AuthoredBodySpawnDesc {
    ShapeHandle shape{};
    AuthoredRootMotion motion{};
    std::optional<PhysicsMaterial> material{};
    AuthoredBodyMotionType motionType = AuthoredBodyMotionType::Dynamic;
};
enum class AuthoredBodyError : uint8_t {
    None, Unsupported, NotInitialized, NotReady, InvalidShape, InvalidMotion,
    InvalidMaterial, InvalidWater, Capacity, Busy,
};
struct AuthoredBodySpawnResult {
    BodyHandle body{};
    AuthoredBodyError error = AuthoredBodyError::Unsupported;
    [[nodiscard]] explicit operator bool() const noexcept {
        return body.valid() && error == AuthoredBodyError::None;
    }
};

struct AuthoredWaterCell { glm::vec3 minimum{}, maximum{}; };
// Bounded water driver for up to sixteen independent authored bodies. Cells must be disjoint, root-local actual
// displacement volumes. Sealed regions are intact; flooding is a separate task.
// This uses a local tangent plane per cell and still-water drag. It does not
// claim a shared per-tick ocean epoch or fluid velocity sampling.
struct AuthoredWaterBodyDesc {
    BodyHandle body{};
    std::span<const AuthoredWaterCell> cells{};
    glm::vec3 propellerPoint{}, propellerDirection{0,0,-1};
    float maximumThrustNewtons = 0;
    float maximumSteeringRadians = 0;
    float densityKgPerM3 = 1000;
    float linearDragPerSecond = .4f, angularDragPerSecond = 2;
};
enum class AuthoredFrameError : uint8_t {
    None, InvalidPosition, InvalidOrientation, InvalidVelocity, InvalidPoint,
    WorldOverflow, Unrepresentable,
};

// Translate a physical origin by a world-axis offset without forming a far
// absolute float or clamping the world boundary. Shares the mass-frame path's
// checked sector carry and preserves local precision for distant fragments.
[[nodiscard]] std::optional<WorldPosition> translateAuthoredPosition(
    const WorldPosition&, glm::dvec3 offset, AuthoredFrameError&) noexcept;

// Allocation-free value copied from the shape's actual GPU mass representation.
// It owns no geometry, world handle, provenance, revision or activation rights.
// The caller must pair this frame with the same immutable shape/revision.
class AuthoredBodyFrame {
public:
    explicit AuthoredBodyFrame(const AuthoredShape& shape) noexcept : mass_(shape.packedMass()) {}

    // Inputs must have canonical sector-local positions and finite motion.
    // Quaternion squared norm must be within .001 of one; accepted values are
    // normalized. Outputs have canonical quaternion sign and positive zeros.
    // Refusals never clamp position/speed, mutate inputs or publish partial state.
    // This checks representability; configured speed/tick/capability checks are
    // still required at the backend admission boundary.
    [[nodiscard]] std::optional<AuthoredBodyMotion> bodyMotion(
        const AuthoredRootMotion&, AuthoredFrameError&) const noexcept;
    [[nodiscard]] std::optional<AuthoredRootMotion> rootMotion(
        const AuthoredBodyMotion&, AuthoredFrameError&) const noexcept;

    // For root-local force/attachment points. Apply the COM shift and inverse
    // principal rotation exactly once. Point/profile eligibility is separate.
    [[nodiscard]] std::optional<glm::vec3> bodyPoint(
        glm::vec3 rootPoint, AuthoredFrameError&) const noexcept;
    [[nodiscard]] std::optional<glm::vec3> rootPoint(
        glm::vec3 bodyPoint, AuthoredFrameError&) const noexcept;
private:
    PackedShapeMass mass_;
};

} // namespace voxy::physics
