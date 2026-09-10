#include "physics/authored_body_frame.hpp"

#include <cmath>
#include <limits>

namespace voxy::physics {
namespace {

bool finite(glm::vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool unpackOrientation(glm::quat value, glm::dquat& result) noexcept {
    if (!std::isfinite(value.x) || !std::isfinite(value.y)
        || !std::isfinite(value.z) || !std::isfinite(value.w)) return false;
    const glm::dquat wide(value);
    const double lengthSquared = glm::dot(wide, wide);
    if (std::abs(lengthSquared - 1.0) > .001) return false;
    result = wide / std::sqrt(lengthSquared);
    return true;
}
glm::quat packOrientation(glm::dquat value) noexcept {
    value /= std::sqrt(glm::dot(value, value));
    glm::quat result(value);
    // Choose sign on the rounded representation, including exact half turns.
    const bool negative = result.w < 0 || (result.w == 0 &&
        (result.x < 0 || (result.x == 0 &&
        (result.y < 0 || (result.y == 0 && result.z < 0)))));
    if (negative) result = -result;
    for (int i = 0; i < 4; ++i) if (result[i] == 0) result[i] = 0.0f;
    return result;
}
glm::dvec3 center(const PackedShapeMass& mass) noexcept {
    return {mass.centerInverseMass[0], mass.centerInverseMass[1], mass.centerInverseMass[2]};
}
glm::dquat principal(const PackedShapeMass& mass) noexcept {
    const auto& q = mass.rootFromBodyQuaternion;
    return {static_cast<double>(q[3]), static_cast<double>(q[0]), static_cast<double>(q[1]), static_cast<double>(q[2])};
}
// Match the GPU helper's unit-quaternion convention, using the packed q as-is.
// Double intermediates avoid additional CPU error before the f32 output cast.
glm::dvec3 rotate(glm::dquat q, glm::dvec3 value) noexcept {
    const glm::dvec3 xyz(q.x, q.y, q.z);
    const glm::dvec3 t = 2.0 * glm::cross(xyz, value);
    return value + q.w * t + glm::cross(xyz, t);
}
bool packVector(glm::dvec3 value, glm::vec3& result) noexcept {
    constexpr double largest = static_cast<double>(std::numeric_limits<float>::max());
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(value[axis]) || std::abs(value[axis]) > largest) return false;
    }
    result = glm::vec3(value);
    return true;
}
AuthoredFrameError shiftPosition(const WorldPosition& input, glm::dvec3 offset,
    WorldPosition& output) noexcept {
    // Both the sector and shift stay separate. Even INT32_MAX sectors retain
    // the same local precision as sector zero; no far absolute float is formed.
    WorldPosition candidate;
    constexpr int64_t minimum = std::numeric_limits<int32_t>::min();
    constexpr int64_t maximum = std::numeric_limits<int32_t>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const double local = static_cast<double>(input.local[axis]) + offset[axis];
        const double shift = std::floor((local + 128.0) / 256.0);
        if (!std::isfinite(shift)
            || shift < static_cast<double>(minimum - input.sector[axis])
            || shift > static_cast<double>(maximum - input.sector[axis])) return AuthoredFrameError::WorldOverflow;
        int64_t sector = int64_t{input.sector[axis]} + static_cast<int64_t>(shift);
        const double relative = local - shift * 256.0;
        float packed = static_cast<float>(relative);
        // A value just below +128 can round up in f32. Preserve the position
        // by carrying, never return the forbidden +128 or saturate the world.
        if (packed >= kWorldSectorHalf) { ++sector; packed -= kWorldSectorSize; }
        if (sector > maximum || sector < minimum) return AuthoredFrameError::WorldOverflow;
        candidate.sector[axis] = static_cast<int32_t>(sector);
        candidate.local[axis] = packed;
    }
    if (!isValidWorldPosition(candidate)) return AuthoredFrameError::Unrepresentable;
    output = candidate;
    return AuthoredFrameError::None;
}

} // namespace

std::optional<WorldPosition> translateAuthoredPosition(
    const WorldPosition& input, glm::dvec3 offset, AuthoredFrameError& error) noexcept {
    if(!isValidWorldPosition(input)){error=AuthoredFrameError::InvalidPosition;return {};}
    WorldPosition output;error=shiftPosition(input,offset,output);
    if(error!=AuthoredFrameError::None)return {};
    return output;
}

std::optional<AuthoredBodyMotion> AuthoredBodyFrame::bodyMotion(
    const AuthoredRootMotion& input, AuthoredFrameError& error) const noexcept {
    error = AuthoredFrameError::None;
    if (!isValidWorldPosition(input.position)) { error = AuthoredFrameError::InvalidPosition; return {}; }
    glm::dquat root;
    if (!unpackOrientation(input.orientation, root)) { error = AuthoredFrameError::InvalidOrientation; return {}; }
    if (!finite(input.originVelocity) || !finite(input.angularVelocity)) { error = AuthoredFrameError::InvalidVelocity; return {}; }
    const glm::dvec3 offset = rotate(root, center(mass_));
    AuthoredBodyMotion output;
    error = shiftPosition(input.position, offset, output.centerPosition);
    if (error != AuthoredFrameError::None) return {};
    if (!packVector(glm::dvec3(input.originVelocity)
        + glm::cross(glm::dvec3(input.angularVelocity), offset), output.centerVelocity)) {
        error = AuthoredFrameError::Unrepresentable; return {};
    }
    output.orientation = packOrientation(root * principal(mass_));
    output.angularVelocity = input.angularVelocity;
    return output;
}

std::optional<AuthoredRootMotion> AuthoredBodyFrame::rootMotion(
    const AuthoredBodyMotion& input, AuthoredFrameError& error) const noexcept {
    error = AuthoredFrameError::None;
    if (!isValidWorldPosition(input.centerPosition)) { error = AuthoredFrameError::InvalidPosition; return {}; }
    glm::dquat body;
    if (!unpackOrientation(input.orientation, body)) { error = AuthoredFrameError::InvalidOrientation; return {}; }
    if (!finite(input.centerVelocity) || !finite(input.angularVelocity)) { error = AuthoredFrameError::InvalidVelocity; return {}; }
    AuthoredRootMotion output;
    output.orientation = packOrientation(body * glm::conjugate(principal(mass_)));
    glm::dquat root;
    if (!unpackOrientation(output.orientation, root)) { error = AuthoredFrameError::Unrepresentable; return {}; }
    const glm::dvec3 offset = rotate(root, center(mass_));
    error = shiftPosition(input.centerPosition, -offset, output.position);
    if (error != AuthoredFrameError::None) return {};
    if (!packVector(glm::dvec3(input.centerVelocity)
        - glm::cross(glm::dvec3(input.angularVelocity), offset), output.originVelocity)) {
        error = AuthoredFrameError::Unrepresentable; return {};
    }
    output.angularVelocity = input.angularVelocity;
    return output;
}

std::optional<glm::vec3> AuthoredBodyFrame::bodyPoint(
    glm::vec3 input, AuthoredFrameError& error) const noexcept {
    error = AuthoredFrameError::None;
    if (!finite(input)) { error = AuthoredFrameError::InvalidPoint; return {}; }
    glm::vec3 output;
    if (!packVector(rotate(glm::conjugate(principal(mass_)), glm::dvec3(input) - center(mass_)), output)) {
        error = AuthoredFrameError::Unrepresentable; return {};
    }
    return output;
}
std::optional<glm::vec3> AuthoredBodyFrame::rootPoint(
    glm::vec3 input, AuthoredFrameError& error) const noexcept {
    error = AuthoredFrameError::None;
    if (!finite(input)) { error = AuthoredFrameError::InvalidPoint; return {}; }
    glm::vec3 output;
    if (!packVector(center(mass_) + rotate(principal(mass_), glm::dvec3(input)), output)) {
        error = AuthoredFrameError::Unrepresentable; return {};
    }
    return output;
}

} // namespace voxy::physics
