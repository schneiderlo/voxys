#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace voxy::physics {

using MassVector = std::array<double, 3>;
using MassMatrix = std::array<double, 9>; // Row-major; column vectors.
struct RigidMassInput {
    double massKg = 0.0;
    MassVector rootCenterOfMass{}; // Metres, authored root frame.
    MassMatrix inertiaAboutCenter{}; // kg m², full symmetric tensor in root axes.
};
enum class MassFrameError : uint8_t {
    None, NonFinite, InvalidMass, Asymmetric, NonPhysical, IllConditioned, NonConvergent,
};
struct MassFrameDiagnostics {
    uint32_t rotations = 0;
    double normalizedReconstructionError = 0.0;
    double orthogonalityError = 0.0;
};
struct MassPose {
    // Relative to the caller's chosen common sector/frame, not a far f32 world position.
    MassVector position{};
    MassMatrix orientation{1,0,0,0,1,0,0,0,1};
};
struct MassVelocityDelta { MassVector linear{}, angular{}; };

// Fixed-size, allocation-free CPU preparation. It preserves full inertia in a
// principal-axis body frame: Iroot = R diag(Ibody) R^T. Body origin is COM.
// It neither creates a solver body nor certifies f32/GPU representability.
// Runtime conversion, admission and every consumer's body/root mapping still
// require SIM-02/SIM-03 integration before advertising authored shape support.
class RigidMassFrame {
public:
    [[nodiscard]] static std::optional<RigidMassFrame> prepare(const RigidMassInput&, MassFrameError&) noexcept;
    [[nodiscard]] double inverseMass() const noexcept { return inverseMass_; }
    [[nodiscard]] const MassVector& center() const noexcept { return center_; }
    [[nodiscard]] const MassVector& principalInertia() const noexcept { return inertia_; }
    [[nodiscard]] const MassVector& principalInverseInertia() const noexcept { return inverseInertia_; }
    [[nodiscard]] const MassMatrix& rootFromBodyRotation() const noexcept { return rotation_; }
    [[nodiscard]] MassFrameDiagnostics diagnostics() const noexcept { return diagnostics_; }
    // Mathematical transforms on finite caller-validated inputs. Not command
    // admission APIs; input force/pose validity and tick ownership belong there.
    [[nodiscard]] MassVector rootVector(MassVector bodyVector) const noexcept;
    [[nodiscard]] MassVector bodyVector(MassVector rootVector) const noexcept;
    [[nodiscard]] MassVector rootPoint(MassVector bodyPoint) const noexcept;
    [[nodiscard]] MassVector bodyPoint(MassVector rootPoint) const noexcept;
    [[nodiscard]] MassPose bodyPose(const MassPose& worldFromRoot) const noexcept;
    [[nodiscard]] MassPose rootPose(const MassPose& worldFromBody) const noexcept;
    [[nodiscard]] MassVector centerVelocity(MassVector rootVelocity, MassVector worldAngularVelocity,
        const MassMatrix& worldFromRoot) const noexcept;
    [[nodiscard]] MassVector rootVelocity(MassVector centerVelocity, MassVector worldAngularVelocity,
        const MassMatrix& worldFromRoot) const noexcept;
    [[nodiscard]] MassVector angularMomentumRoot(MassVector rootAngularVelocity) const noexcept;
    [[nodiscard]] MassVector angularResponseRoot(MassVector rootTorqueOrImpulse) const noexcept;
    [[nodiscard]] MassVelocityDelta impulseAtRootPoint(MassVector rootImpulse, MassVector rootPoint) const noexcept;
private:
    RigidMassFrame() = default;
    double inverseMass_ = 0.0;
    MassVector center_{}, inertia_{}, inverseInertia_{};
    MassMatrix rotation_{};
    MassFrameDiagnostics diagnostics_{};
};
} // namespace voxy::physics
