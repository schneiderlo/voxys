#include "client/wreckwater_third_person_camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace voxy::client {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kMaximumFrequencyHz = 240.0f;
constexpr float kMaximumControlSpeed = 100.0f;
constexpr float kMaximumCameraDistance = 64.0f;
constexpr float kMaximumShoulderOffset = 16.0f;
constexpr float kMaximumProbeRadius = 4.0f;
constexpr float kMaximumProbeEndpointDrift = 1.0f;
constexpr float kMaximumLookAhead = 64.0f;
constexpr float kMaximumTargetSpeed = 10'000.0f;
constexpr float kMaximumTrackingDistance = 1'000'000.0f;

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

[[nodiscard]] bool finiteFloat(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finiteVector(const glm::vec2& value) noexcept {
    return finiteFloat(value.x) && finiteFloat(value.y);
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return finiteFloat(value.x) && finiteFloat(value.y)
        && finiteFloat(value.z);
}

[[nodiscard]] bool finitePositive(float value) noexcept {
    return finiteFloat(value) && value > 0.0f;
}

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return finiteFloat(value) && value >= 0.0f;
}

[[nodiscard]] double lengthSquared(
    const glm::dvec3& value) noexcept {
    return value.x * value.x + value.y * value.y
        + value.z * value.z;
}

[[nodiscard]] double lengthSquared(
    const glm::vec3& value) noexcept {
    return static_cast<double>(value.x)
            * static_cast<double>(value.x)
        + static_cast<double>(value.y)
            * static_cast<double>(value.y)
        + static_cast<double>(value.z)
            * static_cast<double>(value.z);
}

[[nodiscard]] bool validShoulder(
    WreckwaterCameraShoulder shoulder) noexcept {
    return shoulder == WreckwaterCameraShoulder::Left
        || shoulder == WreckwaterCameraShoulder::Right;
}

[[nodiscard]] bool validMode(
    game::WreckwaterCharacterMode mode) noexcept {
    return mode == game::WreckwaterCharacterMode::Airborne
        || mode == game::WreckwaterCharacterMode::OnSkiff
        || mode == game::WreckwaterCharacterMode::Swimming;
}

[[nodiscard]] bool validQueryFlags(uint32_t flags) noexcept {
    return (flags & ~physics::kPhysicsQueryKnownFlags) == 0u
        && !((flags & physics::PhysicsQueryExcludeStatic) != 0u
             && (flags & physics::PhysicsQueryExcludeDynamic) != 0u)
        && !((flags & physics::PhysicsQueryExcludeSleeping) != 0u
             && (flags & physics::PhysicsQueryExcludeAwake) != 0u);
}

[[nodiscard]] bool validConfig(
    const WreckwaterThirdPersonCameraRig::Config& config) noexcept {
    if (!finitePositive(config.maximumDeltaSeconds)
        || config.maximumDeltaSeconds > 0.25f
        || !finitePositive(config.targetSmoothingFrequencyHz)
        || config.targetSmoothingFrequencyHz > kMaximumFrequencyHz
        || !finitePositive(config.boomSmoothingFrequencyHz)
        || config.boomSmoothingFrequencyHz > kMaximumFrequencyHz
        || !finitePositive(config.fovSmoothingFrequencyHz)
        || config.fovSmoothingFrequencyHz > kMaximumFrequencyHz
        || !finitePositive(config.obstructionReleaseRate)
        || config.obstructionReleaseRate > 1'000.0f
        || !finitePositive(config.yawSpeedRadiansPerSecond)
        || config.yawSpeedRadiansPerSecond > kMaximumControlSpeed
        || !finitePositive(config.pitchSpeedRadiansPerSecond)
        || config.pitchSpeedRadiansPerSecond > kMaximumControlSpeed
        || !finitePositive(config.zoomSpeedMetersPerSecond)
        || config.zoomSpeedMetersPerSecond > kMaximumControlSpeed
        || static_cast<double>(
                config.yawSpeedRadiansPerSecond)
                * static_cast<double>(
                    config.maximumDeltaSeconds)
            >= static_cast<double>(kPi)) {
        return false;
    }

    constexpr float pitchLimit = 0.5f * kPi - 0.01f;
    if (!finiteFloat(config.minimumPitchRadians)
        || !finiteFloat(config.initialPitchRadians)
        || !finiteFloat(config.maximumPitchRadians)
        || config.minimumPitchRadians < -pitchLimit
        || config.maximumPitchRadians > pitchLimit
        || config.minimumPitchRadians
            >= config.maximumPitchRadians
        || config.initialPitchRadians
            < config.minimumPitchRadians
        || config.initialPitchRadians
            > config.maximumPitchRadians
        || !finitePositive(config.minimumDistance)
        || !finitePositive(config.initialDistance)
        || !finitePositive(config.maximumDistance)
        || config.minimumDistance > config.initialDistance
        || config.initialDistance > config.maximumDistance
        || config.maximumDistance > kMaximumCameraDistance
        || !finiteNonNegative(config.shoulderOffset)
        || config.shoulderOffset > kMaximumShoulderOffset
        || !validShoulder(config.initialShoulder)) {
        return false;
    }

    if (!finiteNonNegative(config.velocityLookAheadSeconds)
        || config.velocityLookAheadSeconds > 2.0f
        || !finiteNonNegative(config.maximumLookAheadDistance)
        || config.maximumLookAheadDistance > kMaximumLookAhead
        || !finitePositive(config.maximumTargetSpeed)
        || config.maximumTargetSpeed > kMaximumTargetSpeed
        || !finitePositive(config.teleportDistance)
        || config.teleportDistance > kMaximumTrackingDistance) {
        return false;
    }

    if (!finitePositive(config.minimumFovYRadians)
        || !finitePositive(config.baseFovYRadians)
        || !finitePositive(config.maximumFovYRadians)
        || config.minimumFovYRadians > config.baseFovYRadians
        || config.baseFovYRadians > config.maximumFovYRadians
        || config.maximumFovYRadians >= kPi - 0.01f
        || !finiteNonNegative(
            config.maximumSpeedFovBoostRadians)
        || config.baseFovYRadians
                + config.maximumSpeedFovBoostRadians
            > config.maximumFovYRadians
        || !finitePositive(config.speedForMaximumFovBoost)) {
        return false;
    }

    return finitePositive(config.obstructionProbeRadius)
        && finiteNonNegative(config.maximumProbeEndpointDrift)
        && config.maximumProbeEndpointDrift
            <= kMaximumProbeEndpointDrift
        && config.obstructionProbeRadius
                + config.maximumProbeEndpointDrift
            <= kMaximumProbeRadius
        && finiteNonNegative(config.obstructionPadding)
        && config.obstructionPadding <= config.maximumDistance
        && finitePositive(config.minimumObstructedDistance)
        && config.minimumObstructedDistance
            <= config.minimumDistance
        && validQueryFlags(config.obstructionQueryFlags);
}

[[nodiscard]] bool validInput(
    const WreckwaterThirdPersonCameraInput& input) noexcept {
    return finiteVector(input.orbit)
        && finiteFloat(input.zoom)
        && input.orbit.x >= -1.0f && input.orbit.x <= 1.0f
        && input.orbit.y >= -1.0f && input.orbit.y <= 1.0f
        && input.zoom >= -1.0f && input.zoom <= 1.0f;
}

[[nodiscard]] bool validTarget(
    const WreckwaterThirdPersonCameraTarget& target,
    float maximumTargetSpeed) noexcept {
    if (!target.valid
        || target.playerId == 0u
        || target.characterHandle == 0u
        || target.connectionGeneration == 0u
        || !physics::isValidWorldPosition(
            target.worldTargetPosition)
        || !finiteVector(target.cameraSectorTargetPosition)
        || !finiteVector(target.worldVelocity)
        || !finiteVector(target.facing)
        || !validMode(target.mode)) {
        return false;
    }

    const double velocitySquared =
        lengthSquared(target.worldVelocity);
    const double maximumSquared =
        static_cast<double>(maximumTargetSpeed)
        * static_cast<double>(maximumTargetSpeed);
    const double facingSquared =
        static_cast<double>(target.facing.x)
            * static_cast<double>(target.facing.x)
        + static_cast<double>(target.facing.z)
            * static_cast<double>(target.facing.z);
    return std::isfinite(velocitySquared)
        && velocitySquared <= maximumSquared
        && std::isfinite(facingSquared)
        && facingSquared >= 1.0e-8
        && facingSquared <= 4.0;
}

[[nodiscard]] bool translatedWorldPosition(
    const physics::WorldPosition& base,
    const glm::dvec3& offset,
    physics::WorldPosition& output) noexcept {
    if (!physics::isValidWorldPosition(base)
        || !std::isfinite(offset.x)
        || !std::isfinite(offset.y)
        || !std::isfinite(offset.z)) {
        return false;
    }

    constexpr int64_t minimumSector =
        std::numeric_limits<int32_t>::min();
    constexpr int64_t maximumSector =
        std::numeric_limits<int32_t>::max();
    physics::WorldPosition candidate;
    for (int axis = 0; axis < 3; ++axis) {
        double local =
            static_cast<double>(base.local[axis]) + offset[axis];
        if (!std::isfinite(local)) return false;
        const double shiftValue = std::floor(
            (local
             + static_cast<double>(
                 physics::kWorldSectorHalf))
            / static_cast<double>(physics::kWorldSectorSize));
        const double minimumShift = static_cast<double>(
            minimumSector
            - static_cast<int64_t>(base.sector[axis]));
        const double maximumShift = static_cast<double>(
            maximumSector
            - static_cast<int64_t>(base.sector[axis]));
        if (!std::isfinite(shiftValue)
            || shiftValue < minimumShift
            || shiftValue > maximumShift) {
            return false;
        }
        const int64_t shift =
            static_cast<int64_t>(shiftValue);
        int64_t sector =
            static_cast<int64_t>(base.sector[axis]) + shift;
        local -= static_cast<double>(shift)
            * static_cast<double>(physics::kWorldSectorSize);

        if (local
            >= static_cast<double>(physics::kWorldSectorHalf)) {
            if (sector == maximumSector) return false;
            ++sector;
            local -= static_cast<double>(
                physics::kWorldSectorSize);
        } else if (
            local
            < -static_cast<double>(physics::kWorldSectorHalf)) {
            if (sector == minimumSector) return false;
            --sector;
            local += static_cast<double>(
                physics::kWorldSectorSize);
        }

        float localFloat = static_cast<float>(local);
        if (!finiteFloat(localFloat)) return false;
        if (localFloat >= physics::kWorldSectorHalf) {
            if (sector == maximumSector) return false;
            ++sector;
            localFloat = -physics::kWorldSectorHalf;
        } else if (localFloat < -physics::kWorldSectorHalf) {
            if (sector == minimumSector) return false;
            --sector;
            localFloat = std::nextafter(
                physics::kWorldSectorHalf,
                -std::numeric_limits<float>::infinity());
        }
        candidate.sector[axis] = static_cast<int32_t>(sector);
        candidate.local[axis] = localFloat;
    }
    if (!physics::isValidWorldPosition(candidate)) return false;
    output = candidate;
    return true;
}

[[nodiscard]] bool worldDisplacement(
    const physics::WorldPosition& from,
    const physics::WorldPosition& to,
    glm::dvec3& output) noexcept {
    if (!physics::isValidWorldPosition(from)
        || !physics::isValidWorldPosition(to)) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        const int64_t sectorDelta =
            static_cast<int64_t>(to.sector[axis])
            - static_cast<int64_t>(from.sector[axis]);
        output[axis] =
            static_cast<double>(sectorDelta)
                * static_cast<double>(
                    physics::kWorldSectorSize)
            + static_cast<double>(to.local[axis])
            - static_cast<double>(from.local[axis]);
        if (!std::isfinite(output[axis])) return false;
    }
    return true;
}

[[nodiscard]] bool worldPositionsWithinDistance(
    const physics::WorldPosition& lhs,
    const physics::WorldPosition& rhs,
    float maximumDistance) noexcept {
    glm::dvec3 displacement;
    if (!finiteNonNegative(maximumDistance)
        || !worldDisplacement(lhs, rhs, displacement)) {
        return false;
    }
    const double distanceSquared = lengthSquared(displacement);
    const double maximum =
        static_cast<double>(maximumDistance);
    return std::isfinite(distanceSquared)
        && distanceSquared <= maximum * maximum;
}

[[nodiscard]] bool floatVector(
    const glm::dvec3& source, glm::vec3& output) noexcept {
    constexpr double maximum =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(source.x)
        || !std::isfinite(source.y)
        || !std::isfinite(source.z)
        || std::abs(source.x) > maximum
        || std::abs(source.y) > maximum
        || std::abs(source.z) > maximum) {
        return false;
    }
    output = glm::vec3(source);
    return finiteVector(output);
}

void criticalDampedZero(
    glm::vec3& offset, glm::vec3& velocity,
    float angularFrequency, float deltaSeconds) noexcept {
    const float decay =
        std::exp(-angularFrequency * deltaSeconds);
    const glm::vec3 junction =
        velocity + angularFrequency * offset;
    offset =
        (offset + junction * deltaSeconds) * decay;
    velocity =
        (velocity
         - angularFrequency * junction * deltaSeconds)
        * decay;
}

[[nodiscard]] bool criticalDampedWorldTarget(
    physics::WorldPosition& value,
    glm::vec3& valueVelocity,
    const physics::WorldPosition& targetAtEnd,
    const glm::vec3& targetVelocity,
    float angularFrequency,
    float deltaSeconds) noexcept {
    physics::WorldPosition targetAtStart;
    if (!translatedWorldPosition(
            targetAtEnd,
            -glm::dvec3(targetVelocity)
                * static_cast<double>(deltaSeconds),
            targetAtStart)) {
        return false;
    }
    glm::dvec3 wideOffset;
    if (!worldDisplacement(
            targetAtStart, value, wideOffset)) {
        return false;
    }
    glm::vec3 offset;
    if (!floatVector(wideOffset, offset)) return false;
    glm::vec3 relativeVelocity =
        valueVelocity - targetVelocity;
    criticalDampedZero(
        offset, relativeVelocity,
        angularFrequency, deltaSeconds);
    if (!finiteVector(offset)
        || !finiteVector(relativeVelocity)
        || !translatedWorldPosition(
            targetAtEnd, glm::dvec3(offset), value)) {
        return false;
    }
    valueVelocity = targetVelocity + relativeVelocity;
    return finiteVector(valueVelocity);
}

void criticalDampedMoving(
    float& value, float& velocity,
    float targetAtEnd, float targetVelocity,
    float angularFrequency, float deltaSeconds) noexcept {
    const float targetAtStart =
        targetAtEnd - targetVelocity * deltaSeconds;
    float offset = value - targetAtStart;
    float relativeVelocity = velocity - targetVelocity;
    const float decay =
        std::exp(-angularFrequency * deltaSeconds);
    const float junction =
        relativeVelocity + angularFrequency * offset;
    offset =
        (offset + junction * deltaSeconds) * decay;
    relativeVelocity =
        (relativeVelocity
         - angularFrequency * junction * deltaSeconds)
        * decay;
    value = targetAtEnd + offset;
    velocity = targetVelocity + relativeVelocity;
}

void criticalDampedFixed(
    float& value, float& velocity, float target,
    float angularFrequency, float deltaSeconds) noexcept {
    criticalDampedMoving(
        value, velocity, target, 0.0f,
        angularFrequency, deltaSeconds);
}

[[nodiscard]] float canonicalAngle(float angle) noexcept {
    return std::remainder(angle, kTwoPi);
}

[[nodiscard]] float angularFrequency(float hertz) noexcept {
    return kTwoPi * hertz;
}

[[nodiscard]] float shoulderSign(
    WreckwaterCameraShoulder shoulder) noexcept {
    return shoulder == WreckwaterCameraShoulder::Right
        ? 1.0f : -1.0f;
}

[[nodiscard]] float desiredFov(
    const WreckwaterThirdPersonCameraRig::Config& config,
    const glm::vec3& velocity) noexcept {
    const double speedSquared = lengthSquared(velocity);
    const double speed = std::sqrt(
        std::max(0.0, speedSquared));
    const float ratio = static_cast<float>(std::clamp(
        speed
            / static_cast<double>(
                config.speedForMaximumFovBoost),
        0.0, 1.0));
    return std::clamp(
        config.baseFovYRadians
            + config.maximumSpeedFovBoostRadians * ratio,
        config.minimumFovYRadians,
        config.maximumFovYRadians);
}

[[nodiscard]] bool sameProbeIdentity(
    const WreckwaterCameraObstructionProbeIdentity& lhs,
    const WreckwaterCameraObstructionProbeIdentity& rhs) noexcept {
    return lhs == rhs;
}

} // namespace

const char* wreckwaterThirdPersonCameraStatusName(
    WreckwaterThirdPersonCameraStatus status) noexcept {
    switch (status) {
        case WreckwaterThirdPersonCameraStatus::Accepted:
            return "accepted";
        case WreckwaterThirdPersonCameraStatus::NotInitialized:
            return "not initialized";
        case WreckwaterThirdPersonCameraStatus::
                InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterThirdPersonCameraStatus::InvalidDeltaTime:
            return "invalid delta time";
        case WreckwaterThirdPersonCameraStatus::InvalidInput:
            return "invalid input";
        case WreckwaterThirdPersonCameraStatus::InvalidTarget:
            return "invalid target";
        case WreckwaterThirdPersonCameraStatus::PositionOverflow:
            return "position overflow";
        case WreckwaterThirdPersonCameraStatus::ProbeUnavailable:
            return "probe unavailable";
        case WreckwaterThirdPersonCameraStatus::ProbeOutstanding:
            return "probe outstanding";
        case WreckwaterThirdPersonCameraStatus::
                ProbeSequenceExhausted:
            return "probe sequence exhausted";
        case WreckwaterThirdPersonCameraStatus::StaleProbeResult:
            return "stale probe result";
        case WreckwaterThirdPersonCameraStatus::
                ProbeGeometryExpired:
            return "probe geometry expired";
        case WreckwaterThirdPersonCameraStatus::
                ProbeIdentityMismatch:
            return "probe identity mismatch";
        case WreckwaterThirdPersonCameraStatus::InvalidProbeResult:
            return "invalid probe result";
        case WreckwaterThirdPersonCameraStatus::
                ProbeResultOverflow:
            return "probe result overflow";
    }
    return "unknown";
}

bool WreckwaterThirdPersonCameraRig::initialize(
    const Config& config) noexcept {
    if (initialized_ || !validConfig(config)) return false;
    config_ = config;
    lastProbeSequence_ = config.initialProbeSequence;
    initialized_ = true;
    reset();
    return true;
}

void WreckwaterThirdPersonCameraRig::invalidateProbe() noexcept {
    outstandingProbe_ = {};
    probeOutstanding_ = false;
    outstandingProbeGeometryExpired_ = false;
}

void WreckwaterThirdPersonCameraRig::
invalidateObstructionCertificate() noexcept {
    certifiedProbeStart_ = {};
    certifiedProbeEnd_ = {};
    obstructionCertificateValid_ = false;
}

bool WreckwaterThirdPersonCameraRig::probeGeometryCovered(
    const physics::WorldPosition& requestStart,
    const physics::WorldPosition& requestEnd) const noexcept {
    return worldPositionsWithinDistance(
               requestStart, smoothedTarget_,
               config_.maximumProbeEndpointDrift)
        && worldPositionsWithinDistance(
               requestEnd, unobstructedCamera_,
               config_.maximumProbeEndpointDrift);
}

void WreckwaterThirdPersonCameraRig::
clampObstructionFailClosed() noexcept {
    invalidateObstructionCertificate();
    obstructionTargetScale_ = 0.0f;
    obstructionScale_ = 0.0f;
    if (hasTarget_ && pose_.valid
        && rebuildPose()
            != WreckwaterThirdPersonCameraStatus::Accepted) {
        pose_.valid = false;
    }
}

void WreckwaterThirdPersonCameraRig::reset() noexcept {
    pose_ = {};
    smoothedTarget_ = {};
    lastRawTarget_ = {};
    unobstructedCamera_ = {};
    smoothedTargetVelocity_ = {};
    desiredYaw_ = 0.0f;
    smoothedYaw_ = 0.0f;
    yawVelocity_ = 0.0f;
    desiredPitch_ = config_.initialPitchRadians;
    smoothedPitch_ = desiredPitch_;
    pitchVelocity_ = 0.0f;
    desiredDistance_ = config_.initialDistance;
    smoothedDistance_ = desiredDistance_;
    distanceVelocity_ = 0.0f;
    shoulder_ = config_.initialShoulder;
    desiredShoulderOffset_ =
        shoulderSign(shoulder_) * config_.shoulderOffset;
    smoothedShoulderOffset_ = desiredShoulderOffset_;
    shoulderVelocity_ = 0.0f;
    desiredFovY_ = config_.baseFovYRadians;
    smoothedFovY_ = desiredFovY_;
    fovVelocity_ = 0.0f;
    obstructionTargetScale_ = 0.0f;
    obstructionScale_ = 0.0f;
    targetPlayerId_ = 0u;
    targetCharacterHandle_ = 0u;
    targetConnectionGeneration_ = 0u;
    previousShoulderSwapDown_ = false;
    hasTarget_ = false;
    invalidateProbe();
    invalidateObstructionCertificate();
    telemetry_ = {};
}

WreckwaterThirdPersonCameraFrameResult
WreckwaterThirdPersonCameraRig::update(
    const WreckwaterThirdPersonCameraTarget& target,
    const WreckwaterThirdPersonCameraInput& input,
    float deltaSeconds) noexcept {
    if (!initialized_) {
        return {
            .status =
                WreckwaterThirdPersonCameraStatus::NotInitialized,
        };
    }
    if (!finitePositive(deltaSeconds)) {
        incrementSaturated(telemetry_.framesRejected);
        return {
            .status =
                WreckwaterThirdPersonCameraStatus::InvalidDeltaTime,
        };
    }
    if (!validInput(input)) {
        incrementSaturated(telemetry_.framesRejected);
        return {
            .status = WreckwaterThirdPersonCameraStatus::InvalidInput,
        };
    }
    if (!validTarget(target, config_.maximumTargetSpeed)) {
        incrementSaturated(telemetry_.framesRejected);
        return {
            .status = WreckwaterThirdPersonCameraStatus::InvalidTarget,
        };
    }

    WreckwaterThirdPersonCameraRig candidate = *this;
    WreckwaterThirdPersonCameraFrameResult result =
        candidate.updateImpl(target, input, deltaSeconds);
    if (!result) {
        incrementSaturated(telemetry_.framesRejected);
        return result;
    }
    incrementSaturated(candidate.telemetry_.framesAccepted);
    *this = candidate;
    return result;
}

WreckwaterThirdPersonCameraFrameResult
WreckwaterThirdPersonCameraRig::updateImpl(
    const WreckwaterThirdPersonCameraTarget& target,
    const WreckwaterThirdPersonCameraInput& input,
    float deltaSeconds) noexcept {
    WreckwaterThirdPersonCameraFrameResult result;
    result.status = WreckwaterThirdPersonCameraStatus::Accepted;
    result.appliedDeltaSeconds =
        std::min(deltaSeconds, config_.maximumDeltaSeconds);
    result.deltaTimeCapped =
        deltaSeconds > config_.maximumDeltaSeconds;
    if (result.deltaTimeCapped) {
        incrementSaturated(telemetry_.hitchFrames);
    }
    const float dt = result.appliedDeltaSeconds;

    const bool identityChanged =
        !hasTarget_
        || target.playerId != targetPlayerId_
        || target.characterHandle != targetCharacterHandle_
        || target.connectionGeneration
            != targetConnectionGeneration_;

    const float facingYaw = std::atan2(
        target.facing.x, target.facing.z);
    if (!finiteFloat(facingYaw)) {
        result.status =
            WreckwaterThirdPersonCameraStatus::InvalidTarget;
        return result;
    }

    if (identityChanged) {
        desiredYaw_ = canonicalAngle(facingYaw);
        desiredPitch_ = config_.initialPitchRadians;
        desiredDistance_ = config_.initialDistance;
        shoulder_ = config_.initialShoulder;
        desiredShoulderOffset_ =
            shoulderSign(shoulder_) * config_.shoulderOffset;
        previousShoulderSwapDown_ =
            input.shoulderSwapDown;
    } else {
        const bool shoulderRising =
            input.shoulderSwapDown
            && !previousShoulderSwapDown_;
        previousShoulderSwapDown_ =
            input.shoulderSwapDown;
        if (shoulderRising) {
            shoulder_ =
                shoulder_ == WreckwaterCameraShoulder::Right
                ? WreckwaterCameraShoulder::Left
                : WreckwaterCameraShoulder::Right;
            desiredShoulderOffset_ =
                shoulderSign(shoulder_)
                * config_.shoulderOffset;
            incrementSaturated(telemetry_.shoulderSwaps);
        }
    }

    const float oldDesiredYaw = desiredYaw_;
    const float yawDelta =
        input.orbit.x
        * config_.yawSpeedRadiansPerSecond * dt;
    desiredYaw_ =
        canonicalAngle(desiredYaw_ + yawDelta);
    const float yawTargetEnd =
        oldDesiredYaw + yawDelta;
    const float yawTargetVelocity =
        yawDelta / dt;

    const float oldDesiredPitch = desiredPitch_;
    desiredPitch_ = std::clamp(
        desiredPitch_
            + input.orbit.y
                * config_.pitchSpeedRadiansPerSecond * dt,
        config_.minimumPitchRadians,
        config_.maximumPitchRadians);
    const float pitchTargetVelocity =
        (desiredPitch_ - oldDesiredPitch) / dt;

    const float oldDesiredDistance = desiredDistance_;
    desiredDistance_ = std::clamp(
        desiredDistance_
            + input.zoom
                * config_.zoomSpeedMetersPerSecond * dt,
        config_.minimumDistance,
        config_.maximumDistance);
    const float distanceTargetVelocity =
        (desiredDistance_ - oldDesiredDistance) / dt;

    glm::dvec3 lookAhead =
        glm::dvec3(target.worldVelocity)
        * static_cast<double>(
            config_.velocityLookAheadSeconds);
    const double lookAheadSquared = lengthSquared(lookAhead);
    const double maximumLookAhead =
        static_cast<double>(
            config_.maximumLookAheadDistance);
    if (!std::isfinite(lookAheadSquared)) {
        result.status =
            WreckwaterThirdPersonCameraStatus::InvalidTarget;
        return result;
    }
    if (lookAheadSquared
            > maximumLookAhead * maximumLookAhead
        && lookAheadSquared > 0.0) {
        lookAhead *= maximumLookAhead
            / std::sqrt(lookAheadSquared);
    }

    physics::WorldPosition desiredTarget;
    if (!translatedWorldPosition(
            target.worldTargetPosition,
            lookAhead, desiredTarget)) {
        result.status =
            WreckwaterThirdPersonCameraStatus::PositionOverflow;
        return result;
    }

    bool automaticDiscontinuity = false;
    if (hasTarget_ && !identityChanged) {
        glm::dvec3 rawMotion;
        if (!worldDisplacement(
                lastRawTarget_,
                target.worldTargetPosition,
                rawMotion)) {
            result.status =
                WreckwaterThirdPersonCameraStatus::PositionOverflow;
            return result;
        }
        const double rawDistanceSquared =
            lengthSquared(rawMotion);
        const double teleportDistance =
            static_cast<double>(config_.teleportDistance);
        automaticDiscontinuity =
            !std::isfinite(rawDistanceSquared)
            || rawDistanceSquared
                > teleportDistance * teleportDistance;
    }

    result.snapped = identityChanged
        || target.teleported
        || target.discontinuity
        || automaticDiscontinuity;

    desiredFovY_ = desiredFov(config_, target.worldVelocity);
    if (result.snapped) {
        smoothedTarget_ = desiredTarget;
        smoothedTargetVelocity_ = target.worldVelocity;
        smoothedYaw_ = desiredYaw_;
        yawVelocity_ = 0.0f;
        smoothedPitch_ = desiredPitch_;
        pitchVelocity_ = 0.0f;
        smoothedDistance_ = desiredDistance_;
        distanceVelocity_ = 0.0f;
        smoothedShoulderOffset_ = desiredShoulderOffset_;
        shoulderVelocity_ = 0.0f;
        smoothedFovY_ = desiredFovY_;
        fovVelocity_ = 0.0f;
        obstructionTargetScale_ = 0.0f;
        obstructionScale_ = 0.0f;
        invalidateProbe();
        invalidateObstructionCertificate();
        incrementSaturated(telemetry_.trackingSnaps);
    } else {
        if (!criticalDampedWorldTarget(
                smoothedTarget_,
                smoothedTargetVelocity_,
                desiredTarget,
                target.worldVelocity,
                angularFrequency(
                    config_.targetSmoothingFrequencyHz),
                dt)) {
            result.status =
                WreckwaterThirdPersonCameraStatus::PositionOverflow;
            return result;
        }

        float unwrappedYaw =
            oldDesiredYaw
            + std::remainder(
                smoothedYaw_ - oldDesiredYaw, kTwoPi);
        criticalDampedMoving(
            unwrappedYaw, yawVelocity_,
            yawTargetEnd, yawTargetVelocity,
            angularFrequency(
                config_.boomSmoothingFrequencyHz),
            dt);
        smoothedYaw_ = canonicalAngle(unwrappedYaw);

        criticalDampedMoving(
            smoothedPitch_, pitchVelocity_,
            desiredPitch_, pitchTargetVelocity,
            angularFrequency(
                config_.boomSmoothingFrequencyHz),
            dt);
        criticalDampedMoving(
            smoothedDistance_, distanceVelocity_,
            desiredDistance_, distanceTargetVelocity,
            angularFrequency(
                config_.boomSmoothingFrequencyHz),
            dt);
        criticalDampedFixed(
            smoothedShoulderOffset_, shoulderVelocity_,
            desiredShoulderOffset_,
            angularFrequency(
                config_.boomSmoothingFrequencyHz),
            dt);
        criticalDampedFixed(
            smoothedFovY_, fovVelocity_, desiredFovY_,
            angularFrequency(
                config_.fovSmoothingFrequencyHz),
            dt);

        smoothedPitch_ = std::clamp(
            smoothedPitch_,
            config_.minimumPitchRadians,
            config_.maximumPitchRadians);
        smoothedDistance_ = std::clamp(
            smoothedDistance_,
            config_.minimumDistance,
            config_.maximumDistance);
        smoothedShoulderOffset_ = std::clamp(
            smoothedShoulderOffset_,
            -config_.shoulderOffset,
            config_.shoulderOffset);
        smoothedFovY_ = std::clamp(
            smoothedFovY_,
            config_.minimumFovYRadians,
            config_.maximumFovYRadians);
        if ((smoothedPitch_ == config_.minimumPitchRadians
             && pitchVelocity_ < 0.0f)
            || (smoothedPitch_ == config_.maximumPitchRadians
                && pitchVelocity_ > 0.0f)) {
            pitchVelocity_ = 0.0f;
        }
        if ((smoothedDistance_ == config_.minimumDistance
             && distanceVelocity_ < 0.0f)
            || (smoothedDistance_ == config_.maximumDistance
                && distanceVelocity_ > 0.0f)) {
            distanceVelocity_ = 0.0f;
        }
        if ((smoothedShoulderOffset_ == -config_.shoulderOffset
             && shoulderVelocity_ < 0.0f)
            || (smoothedShoulderOffset_ == config_.shoulderOffset
                && shoulderVelocity_ > 0.0f)) {
            shoulderVelocity_ = 0.0f;
        }
        if ((smoothedFovY_ == config_.minimumFovYRadians
             && fovVelocity_ < 0.0f)
            || (smoothedFovY_ == config_.maximumFovYRadians
                && fovVelocity_ > 0.0f)) {
            fovVelocity_ = 0.0f;
        }

        if (obstructionTargetScale_ < obstructionScale_) {
            // A certified obstruction is a hard safety bound. Never ease
            // through geometry while waiting for the presentation spring.
            obstructionScale_ = obstructionTargetScale_;
        } else {
            const float blend =
                1.0f
                - std::exp(
                    -config_.obstructionReleaseRate * dt);
            obstructionScale_ +=
                (obstructionTargetScale_ - obstructionScale_)
                * blend;
        }
        obstructionScale_ =
            std::clamp(obstructionScale_, 0.0f, 1.0f);
    }

    if (!finiteFloat(smoothedYaw_)
        || !finiteFloat(yawVelocity_)
        || !finiteFloat(smoothedPitch_)
        || !finiteFloat(pitchVelocity_)
        || !finiteFloat(smoothedDistance_)
        || !finiteFloat(distanceVelocity_)
        || !finiteFloat(smoothedShoulderOffset_)
        || !finiteFloat(shoulderVelocity_)
        || !finiteFloat(smoothedFovY_)
        || !finiteFloat(fovVelocity_)
        || !finiteFloat(obstructionScale_)) {
        result.status =
            WreckwaterThirdPersonCameraStatus::PositionOverflow;
        return result;
    }

    targetPlayerId_ = target.playerId;
    targetCharacterHandle_ = target.characterHandle;
    targetConnectionGeneration_ =
        target.connectionGeneration;
    lastRawTarget_ = target.worldTargetPosition;
    hasTarget_ = true;

    result.status = rebuildPose();
    return result;
}

WreckwaterThirdPersonCameraStatus
WreckwaterThirdPersonCameraRig::rebuildPose() noexcept {
    const float cosinePitch = std::cos(smoothedPitch_);
    const float sinePitch = std::sin(smoothedPitch_);
    const float sineYaw = std::sin(smoothedYaw_);
    const float cosineYaw = std::cos(smoothedYaw_);
    const glm::vec3 radial{
        -sineYaw * cosinePitch,
        sinePitch,
        -cosineYaw * cosinePitch,
    };
    const glm::vec3 orbitRight{
        cosineYaw, 0.0f, -sineYaw};
    const glm::vec3 unobstructedOffset =
        radial * smoothedDistance_
        + orbitRight * smoothedShoulderOffset_;
    if (!finiteVector(unobstructedOffset)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    const double unobstructedSquared =
        lengthSquared(unobstructedOffset);
    if (!std::isfinite(unobstructedSquared)
        || unobstructedSquared <= 1.0e-12) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    const float unobstructedDistance =
        static_cast<float>(std::sqrt(unobstructedSquared));
    if (!finitePositive(unobstructedDistance)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    if (!translatedWorldPosition(
            smoothedTarget_, glm::dvec3(unobstructedOffset),
            unobstructedCamera_)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }

    bool geometryExpired = false;
    if (obstructionCertificateValid_
        && !probeGeometryCovered(
            certifiedProbeStart_, certifiedProbeEnd_)) {
        geometryExpired = true;
    }
    if (probeOutstanding_
        && !outstandingProbeGeometryExpired_
        && !probeGeometryCovered(
            outstandingProbe_.worldStart,
            outstandingProbe_.worldEnd)) {
        outstandingProbeGeometryExpired_ = true;
        geometryExpired = true;
    }
    if (geometryExpired) {
        invalidateObstructionCertificate();
        obstructionTargetScale_ = 0.0f;
        obstructionScale_ = 0.0f;
        incrementSaturated(
            telemetry_.probeGeometryExpirations);
    }
    if (!obstructionCertificateValid_) {
        obstructionTargetScale_ = 0.0f;
        obstructionScale_ = 0.0f;
    }

    const float minimumScale = std::min(
        1.0f,
        config_.minimumObstructedDistance
            / unobstructedDistance);
    const float appliedScale = std::clamp(
        std::max(obstructionScale_, minimumScale),
        minimumScale, 1.0f);
    const glm::vec3 cameraOffset =
        unobstructedOffset * appliedScale;

    physics::WorldPosition cameraPosition;
    if (!translatedWorldPosition(
            smoothedTarget_, glm::dvec3(cameraOffset),
            cameraPosition)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }

    glm::vec3 cameraSectorLookTarget;
    if (!physics::worldPositionRelativeToSector(
            smoothedTarget_, cameraPosition.sector,
            cameraSectorLookTarget, 2)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    glm::vec3 forward =
        cameraSectorLookTarget - cameraPosition.local;
    const double forwardSquared = lengthSquared(forward);
    if (!std::isfinite(forwardSquared)
        || forwardSquared <= 1.0e-12) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    forward *= static_cast<float>(
        1.0 / std::sqrt(forwardSquared));
    constexpr glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    glm::vec3 right = glm::cross(worldUp, forward);
    const double rightSquared = lengthSquared(right);
    if (!std::isfinite(rightSquared)
        || rightSquared <= 1.0e-12) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    right *= static_cast<float>(
        1.0 / std::sqrt(rightSquared));
    glm::vec3 up = glm::cross(forward, right);
    const double upSquared = lengthSquared(up);
    if (!std::isfinite(upSquared)
        || upSquared <= 1.0e-12) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    up *= static_cast<float>(1.0 / std::sqrt(upSquared));
    if (!finiteVector(forward)
        || !finiteVector(right)
        || !finiteVector(up)
        || !finiteVector(cameraSectorLookTarget)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }

    pose_ = {
        .cameraPosition = cameraPosition,
        .lookTargetPosition = smoothedTarget_,
        .cameraSectorLookTarget = cameraSectorLookTarget,
        .forward = forward,
        .right = right,
        .up = up,
        .fovYRadians = smoothedFovY_,
        .boomDistance =
            unobstructedDistance * appliedScale,
        .unobstructedBoomDistance =
            unobstructedDistance,
        .obstructionScale = appliedScale,
        .shoulder = shoulder_,
        .obstructionLimited = appliedScale < 0.9999f,
        .valid = true,
    };
    return WreckwaterThirdPersonCameraStatus::Accepted;
}

WreckwaterThirdPersonCameraStatus
WreckwaterThirdPersonCameraRig::takeObstructionProbeRequest(
    WreckwaterCameraObstructionProbeRequest& output) noexcept {
    output = {};
    if (!initialized_) {
        return WreckwaterThirdPersonCameraStatus::NotInitialized;
    }
    if (!hasTarget_ || !pose_.valid) {
        return WreckwaterThirdPersonCameraStatus::ProbeUnavailable;
    }
    if (probeOutstanding_) {
        return WreckwaterThirdPersonCameraStatus::ProbeOutstanding;
    }
    if (lastProbeSequence_
        == std::numeric_limits<uint64_t>::max()) {
        return WreckwaterThirdPersonCameraStatus::
            ProbeSequenceExhausted;
    }

    glm::dvec3 widePath;
    if (!worldDisplacement(
            pose_.lookTargetPosition,
            unobstructedCamera_, widePath)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    const double pathSquared = lengthSquared(widePath);
    if (!std::isfinite(pathSquared)
        || pathSquared <= 1.0e-12) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    const double pathLength = std::sqrt(pathSquared);
    if (!std::isfinite(pathLength)
        || pathLength
            > static_cast<double>(
                std::numeric_limits<float>::max())) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    glm::vec3 direction;
    if (!floatVector(widePath / pathLength, direction)) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    const double directionSquared = lengthSquared(direction);
    if (!std::isfinite(directionSquared)
        || directionSquared <= 1.0e-12) {
        return WreckwaterThirdPersonCameraStatus::PositionOverflow;
    }
    direction *= static_cast<float>(
        1.0 / std::sqrt(directionSquared));

    const uint64_t sequence = lastProbeSequence_ + 1u;
    WreckwaterCameraObstructionProbeRequest request;
    request.identity = {
        .sequence = sequence,
        .playerId = targetPlayerId_,
        .characterHandle = targetCharacterHandle_,
        .connectionGeneration =
            targetConnectionGeneration_,
    };
    request.physicsQuery = {
        .requestId = static_cast<uint32_t>(sequence),
        .type = physics::PhysicsQueryType::SphereCast,
        .maximumHits = 1u,
        .flags = config_.obstructionQueryFlags,
        .origin = pose_.lookTargetPosition.local,
        .radius =
            config_.obstructionProbeRadius
            + config_.maximumProbeEndpointDrift,
        .direction = direction,
        .maximumDistance = static_cast<float>(pathLength),
        .capsuleAxis = {0.0f, 1.0f, 0.0f},
        .capsuleHalfHeight = 0.0f,
        .sector = pose_.lookTargetPosition.sector,
    };
    request.worldStart = pose_.lookTargetPosition;
    request.worldEnd = unobstructedCamera_;
    request.pathDistance = static_cast<float>(pathLength);

    lastProbeSequence_ = sequence;
    outstandingProbe_ = request;
    probeOutstanding_ = true;
    output = request;
    incrementSaturated(telemetry_.probesIssued);
    return WreckwaterThirdPersonCameraStatus::Accepted;
}

WreckwaterThirdPersonCameraStatus
WreckwaterThirdPersonCameraRig::resolveObstructionProbe(
    const WreckwaterCameraObstructionProbeResult& result)
    noexcept {
    if (!initialized_) {
        return WreckwaterThirdPersonCameraStatus::NotInitialized;
    }
    if (!probeOutstanding_) {
        if (result.identity.sequence != 0u
            && result.identity.sequence <= lastProbeSequence_) {
            incrementSaturated(telemetry_.staleProbeResults);
            return WreckwaterThirdPersonCameraStatus::
                StaleProbeResult;
        }
        incrementSaturated(telemetry_.mismatchedProbeResults);
        return WreckwaterThirdPersonCameraStatus::
            ProbeIdentityMismatch;
    }
    if (result.identity.sequence
        < outstandingProbe_.identity.sequence) {
        incrementSaturated(telemetry_.staleProbeResults);
        return WreckwaterThirdPersonCameraStatus::
            StaleProbeResult;
    }
    if (!sameProbeIdentity(
            result.identity, outstandingProbe_.identity)) {
        incrementSaturated(telemetry_.mismatchedProbeResults);
        return WreckwaterThirdPersonCameraStatus::
            ProbeIdentityMismatch;
    }

    const float pathDistance = outstandingProbe_.pathDistance;
    if (!finitePositive(pathDistance)) {
        incrementSaturated(telemetry_.invalidProbeResults);
        clampObstructionFailClosed();
        return WreckwaterThirdPersonCameraStatus::
            InvalidProbeResult;
    }

    // Overflow is authoritative uncertainty for this exact request lifetime.
    // Consume it before inspecting optional hit fields: an incomplete backend
    // result must close the camera even when those fields are garbage.
    if (result.overflow) {
        invalidateProbe();
        incrementSaturated(telemetry_.probeOverflows);
        clampObstructionFailClosed();
        return WreckwaterThirdPersonCameraStatus::
            ProbeResultOverflow;
    }

    const bool geometryExpired =
        outstandingProbeGeometryExpired_
        || !probeGeometryCovered(
            outstandingProbe_.worldStart,
            outstandingProbe_.worldEnd);
    if (geometryExpired) {
        const bool newlyExpired =
            !outstandingProbeGeometryExpired_;
        invalidateProbe();
        if (newlyExpired) {
            incrementSaturated(
                telemetry_.probeGeometryExpirations);
        }
        clampObstructionFailClosed();
        return WreckwaterThirdPersonCameraStatus::
            ProbeGeometryExpired;
    }

    if (!result.completeScene
        || !finiteFloat(result.hitDistance)
        || result.hitDistance < 0.0f
        || (!result.hit && result.hitDistance != 0.0f)
        || (result.hit
            && result.hitDistance
                > pathDistance
                    + std::max(0.001f, pathDistance * 0.001f))) {
        incrementSaturated(telemetry_.invalidProbeResults);
        clampObstructionFailClosed();
        return WreckwaterThirdPersonCameraStatus::
            InvalidProbeResult;
    }

    const float minimumScale = std::min(
        1.0f,
        config_.minimumObstructedDistance / pathDistance);
    certifiedProbeStart_ = outstandingProbe_.worldStart;
    certifiedProbeEnd_ = outstandingProbe_.worldEnd;
    obstructionCertificateValid_ = true;

    if (result.hit) {
        const float safeDistance = std::clamp(
            result.hitDistance - config_.obstructionPadding,
            config_.minimumObstructedDistance,
            pathDistance);
        obstructionTargetScale_ = std::clamp(
            safeDistance / pathDistance,
            minimumScale, 1.0f);
        obstructionScale_ = std::min(
            obstructionScale_, obstructionTargetScale_);
        incrementSaturated(telemetry_.probeHits);
    } else {
        obstructionTargetScale_ = 1.0f;
        incrementSaturated(telemetry_.probeMisses);
    }
    invalidateProbe();
    if (result.hit
        && rebuildPose()
            != WreckwaterThirdPersonCameraStatus::Accepted) {
        pose_.valid = false;
        return WreckwaterThirdPersonCameraStatus::
            PositionOverflow;
    }
    return WreckwaterThirdPersonCameraStatus::Accepted;
}

WreckwaterThirdPersonCameraStatus
WreckwaterThirdPersonCameraRig::cancelObstructionProbe(
    const WreckwaterCameraObstructionProbeIdentity& identity)
    noexcept {
    if (!initialized_) {
        return WreckwaterThirdPersonCameraStatus::NotInitialized;
    }
    if (!probeOutstanding_) {
        if (identity.sequence != 0u
            && identity.sequence <= lastProbeSequence_) {
            return WreckwaterThirdPersonCameraStatus::
                StaleProbeResult;
        }
        return WreckwaterThirdPersonCameraStatus::
            ProbeIdentityMismatch;
    }
    if (identity.sequence
        < outstandingProbe_.identity.sequence) {
        return WreckwaterThirdPersonCameraStatus::
            StaleProbeResult;
    }
    if (!sameProbeIdentity(
            identity, outstandingProbe_.identity)) {
        return WreckwaterThirdPersonCameraStatus::
            ProbeIdentityMismatch;
    }
    invalidateProbe();
    return WreckwaterThirdPersonCameraStatus::Accepted;
}

} // namespace voxy::client
