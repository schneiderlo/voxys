#include "game/wreckwater_character_movement.hpp"
#include "game/wreckwater_character_step.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace voxy::game {
namespace {

constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
constexpr float kFixedTickSeconds = 1.0f / 60.0f;
constexpr uint64_t kFnvOffset64 = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnvPrime64 = 1'099'511'628'211ull;

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

[[nodiscard]] bool finiteQuaternion(const glm::quat& value) noexcept {
    return finiteFloat(value.w) && finiteFloat(value.x)
        && finiteFloat(value.y) && finiteFloat(value.z);
}

void canonicalizeZero(float& value) noexcept {
    if (value == 0.0f) value = 0.0f;
}

void canonicalizeZero(glm::vec2& value) noexcept {
    canonicalizeZero(value.x);
    canonicalizeZero(value.y);
}

void canonicalizeZero(glm::vec3& value) noexcept {
    canonicalizeZero(value.x);
    canonicalizeZero(value.y);
    canonicalizeZero(value.z);
}

void canonicalizeZero(glm::quat& value) noexcept {
    canonicalizeZero(value.w);
    canonicalizeZero(value.x);
    canonicalizeZero(value.y);
    canonicalizeZero(value.z);
}

[[nodiscard]] bool canonicalizeOrientation(glm::quat& orientation) noexcept {
    if (!finiteQuaternion(orientation)) return false;
    const float lengthSquared = glm::dot(orientation, orientation);
    if (!finiteFloat(lengthSquared)
        || lengthSquared < 0.25f || lengthSquared > 4.0f) {
        return false;
    }
    orientation *= glm::inversesqrt(lengthSquared);
    // q and -q encode one rotation. Choose one representation before the
    // sample enters state or hash order.
    if (orientation.w < 0.0f
        || (orientation.w == 0.0f && orientation.x < 0.0f)
        || (orientation.w == 0.0f && orientation.x == 0.0f
            && orientation.y < 0.0f)
        || (orientation.w == 0.0f && orientation.x == 0.0f
            && orientation.y == 0.0f && orientation.z < 0.0f)) {
        orientation = -orientation;
    }
    canonicalizeZero(orientation);
    return true;
}

[[nodiscard]] bool boundedMagnitude(
    const glm::vec3& value, float maximum) noexcept {
    if (!finiteVector(value)) return false;
    const double squared =
        static_cast<double>(value.x) * static_cast<double>(value.x)
        + static_cast<double>(value.y) * static_cast<double>(value.y)
        + static_cast<double>(value.z) * static_cast<double>(value.z);
    return std::isfinite(squared)
        && squared <= static_cast<double>(maximum)
            * static_cast<double>(maximum);
}

void clampMagnitude(glm::vec2& value, float maximum) noexcept {
    const float squared = glm::dot(value, value);
    if (squared > maximum * maximum) {
        value *= maximum * glm::inversesqrt(squared);
    }
    canonicalizeZero(value);
}

[[nodiscard]] bool platformIdentityLess(
    const WreckwaterCharacterPlatformSample& lhs,
    const WreckwaterCharacterPlatformSample& rhs) noexcept {
    return std::tuple{
        lhs.skiffId, lhs.skiffGeneration,
        lhs.body.index, lhs.body.generation}
        < std::tuple{
        rhs.skiffId, rhs.skiffGeneration,
        rhs.body.index, rhs.body.generation};
}

[[nodiscard]] bool continuousPlatformMotion(
    const WreckwaterCharacterMovementAuthority::Config& config,
    const WreckwaterCharacterPlatformSample& previous,
    const WreckwaterCharacterPlatformSample& current) noexcept {
    const glm::dvec3 displacement =
        physics::worldPositionToAbsolute(current.position)
        - physics::worldPositionToAbsolute(previous.position);
    const glm::dvec3 expectedDisplacement =
        0.5 * static_cast<double>(kFixedTickSeconds)
        * (glm::dvec3(previous.linearVelocity)
           + glm::dvec3(current.linearVelocity));
    const double linearError =
        glm::length(displacement - expectedDisplacement);
    const double maximumLinearError =
        0.5
            * static_cast<double>(
                config.maximumPlatformLinearAcceleration)
            * static_cast<double>(kFixedTickSeconds)
            * static_cast<double>(kFixedTickSeconds)
        + static_cast<double>(config.platformPositionTolerance);
    const double linearVelocityDelta =
        glm::length(
            glm::dvec3(current.linearVelocity)
            - glm::dvec3(previous.linearVelocity));
    const double maximumLinearVelocityDelta =
        static_cast<double>(
            config.maximumPlatformLinearAcceleration)
            * static_cast<double>(kFixedTickSeconds)
        + static_cast<double>(config.platformPositionTolerance)
            / static_cast<double>(kFixedTickSeconds);
    if (!std::isfinite(linearError)
        || !std::isfinite(linearVelocityDelta)
        || linearError > maximumLinearError
        || linearVelocityDelta > maximumLinearVelocityDelta) {
        return false;
    }

    glm::quat relative =
        current.orientation * glm::conjugate(previous.orientation);
    if (!canonicalizeOrientation(relative)) return false;
    const float clampedW = std::clamp(relative.w, 0.0f, 1.0f);
    const float angle = 2.0f * std::acos(clampedW);
    const float sinHalfSquared =
        std::max(1.0f - clampedW * clampedW, 0.0f);
    glm::vec3 rotationVector(0.0f);
    if (sinHalfSquared > 1.0e-12f) {
        rotationVector =
            glm::vec3(relative.x, relative.y, relative.z)
            * (angle * glm::inversesqrt(sinHalfSquared));
    }
    const glm::vec3 expectedRotation =
        0.5f * kFixedTickSeconds
        * (previous.angularVelocity + current.angularVelocity);
    const float angularError =
        glm::length(rotationVector - expectedRotation);
    const float maximumAngularError =
        0.5f * config.maximumPlatformAngularAcceleration
            * kFixedTickSeconds * kFixedTickSeconds
        + config.platformAngleTolerance;
    const float angularVelocityDelta =
        glm::length(
            current.angularVelocity - previous.angularVelocity);
    const float maximumAngularVelocityDelta =
        config.maximumPlatformAngularAcceleration
            * kFixedTickSeconds
        + config.platformAngleTolerance / kFixedTickSeconds;
    return finiteFloat(angle)
        && finiteVector(rotationVector)
        && finiteFloat(angularError)
        && finiteFloat(angularVelocityDelta)
        && angularError <= maximumAngularError
        && angularVelocityDelta <= maximumAngularVelocityDelta;
}

void hashU32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kFnvPrime64;
    }
}

void hashU64(uint64_t& hash, uint64_t value) noexcept {
    hashU32(hash, static_cast<uint32_t>(value));
    hashU32(hash, static_cast<uint32_t>(value >> 32u));
}

void hashFloat(uint64_t& hash, float value) noexcept {
    hashU32(hash, std::bit_cast<uint32_t>(value));
}

void hashVec2(uint64_t& hash, const glm::vec2& value) noexcept {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
}

void hashVec3(uint64_t& hash, const glm::vec3& value) noexcept {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashWorldPosition(
    uint64_t& hash, const physics::WorldPosition& position) noexcept {
    hashU32(hash, static_cast<uint32_t>(position.sector.x));
    hashU32(hash, static_cast<uint32_t>(position.sector.y));
    hashU32(hash, static_cast<uint32_t>(position.sector.z));
    hashVec3(hash, position.local);
}

void hashBodyHandle(
    uint64_t& hash, physics::BodyHandle handle) noexcept {
    hashU32(hash, handle.index);
    hashU32(hash, handle.generation);
}

void hashPlatform(
    uint64_t& hash,
    const WreckwaterCharacterPlatformSample& platform) noexcept {
    hashU32(hash, platform.skiffId);
    hashU32(hash, platform.skiffGeneration);
    hashBodyHandle(hash, platform.body);
    hashWorldPosition(hash, platform.position);
    hashFloat(hash, platform.orientation.w);
    hashFloat(hash, platform.orientation.x);
    hashFloat(hash, platform.orientation.y);
    hashFloat(hash, platform.orientation.z);
    hashVec3(hash, platform.linearVelocity);
    hashVec3(hash, platform.angularVelocity);
    hashVec2(hash, platform.deckHalfExtents);
    hashFloat(hash, platform.deckLocalHeight);
}

void canonicalizeConfiguration(
    WreckwaterCharacterMovementAuthority::Config& config) noexcept {
    canonicalizeZero(config.capsule.radius);
    canonicalizeZero(config.capsule.height);
    canonicalizeZero(config.capsule.maxSlopeAngleDegrees);
    canonicalizeZero(config.capsule.stepUp);
    canonicalizeZero(config.capsule.stepDown);
    canonicalizeZero(config.deckMaximumSpeed);
    canonicalizeZero(config.deckAcceleration);
    canonicalizeZero(config.deckBraking);
    canonicalizeZero(config.airMaximumSpeed);
    canonicalizeZero(config.airAcceleration);
    canonicalizeZero(config.jumpSpeed);
    canonicalizeZero(config.gravity);
    canonicalizeZero(config.terminalVelocity);
    canonicalizeZero(config.waterHeight);
    canonicalizeZero(config.waterEnterDepth);
    canonicalizeZero(config.waterExitHeight);
    canonicalizeZero(config.swimFullSubmersionDepth);
    canonicalizeZero(config.swimBuoyancyRatio);
    canonicalizeZero(config.waterLinearDrag);
    canonicalizeZero(config.swimMaximumSpeed);
    canonicalizeZero(config.swimAcceleration);
    canonicalizeZero(config.swimStrokeAcceleration);
    canonicalizeZero(config.boardAssistHorizontalReach);
    canonicalizeZero(config.boardAssistMaximumClimb);
    canonicalizeZero(config.boardAssistMaximumDrop);
    canonicalizeZero(config.boardAssistMaximumRelativeSpeed);
    canonicalizeZero(config.landingSkin);
    canonicalizeZero(config.minimumWalkableDeckUp);
    canonicalizeZero(config.maximumDeckHalfExtent);
    canonicalizeZero(config.maximumPlatformLinearSpeed);
    canonicalizeZero(config.maximumPlatformAngularSpeed);
    canonicalizeZero(config.maximumPlatformLinearAcceleration);
    canonicalizeZero(config.maximumPlatformAngularAcceleration);
    canonicalizeZero(config.platformPositionTolerance);
    canonicalizeZero(config.platformAngleTolerance);
    canonicalizeZero(config.maximumCharacterWorldSpeed);
}

[[nodiscard]] uint64_t hashConfiguration(
    const WreckwaterCharacterMovementAuthority::Config& config) noexcept {
    uint64_t hash = kFnvOffset64;
    hashU32(hash, kWreckwaterCharacterMovementSchemaVersion);
    hashU32(hash, config.tickRateHz);
    hashU32(hash, config.maximumCharacters);
    hashFloat(hash, config.capsule.radius);
    hashFloat(hash, config.capsule.height);
    hashFloat(hash, config.capsule.maxSlopeAngleDegrees);
    hashFloat(hash, config.capsule.stepUp);
    hashFloat(hash, config.capsule.stepDown);
    hashFloat(hash, config.deckMaximumSpeed);
    hashFloat(hash, config.deckAcceleration);
    hashFloat(hash, config.deckBraking);
    hashFloat(hash, config.airMaximumSpeed);
    hashFloat(hash, config.airAcceleration);
    hashFloat(hash, config.jumpSpeed);
    hashFloat(hash, config.gravity);
    hashFloat(hash, config.terminalVelocity);
    hashFloat(hash, config.waterHeight);
    hashFloat(hash, config.waterEnterDepth);
    hashFloat(hash, config.waterExitHeight);
    hashFloat(hash, config.swimFullSubmersionDepth);
    hashFloat(hash, config.swimBuoyancyRatio);
    hashFloat(hash, config.waterLinearDrag);
    hashFloat(hash, config.swimMaximumSpeed);
    hashFloat(hash, config.swimAcceleration);
    hashFloat(hash, config.swimStrokeAcceleration);
    hashFloat(hash, config.boardAssistHorizontalReach);
    hashFloat(hash, config.boardAssistMaximumClimb);
    hashFloat(hash, config.boardAssistMaximumDrop);
    hashFloat(hash, config.boardAssistMaximumRelativeSpeed);
    hashFloat(hash, config.landingSkin);
    hashFloat(hash, config.minimumWalkableDeckUp);
    hashFloat(hash, config.maximumDeckHalfExtent);
    hashFloat(hash, config.maximumPlatformLinearSpeed);
    hashFloat(hash, config.maximumPlatformAngularSpeed);
    hashFloat(hash, config.maximumPlatformLinearAcceleration);
    hashFloat(hash, config.maximumPlatformAngularAcceleration);
    hashFloat(hash, config.platformPositionTolerance);
    hashFloat(hash, config.platformAngleTolerance);
    hashFloat(hash, config.maximumCharacterWorldSpeed);
    hashU32(hash, config.maximumInputSequenceAdvance);
    return hash;
}

} // namespace

WreckwaterCharacterStatus
wreckwaterCharacterPlatformSuccessorStatus(
    const WreckwaterCharacterMovementAuthority::Config& config,
    const WreckwaterCharacterPlatformSample& previous,
    bool previousAvailable,
    const WreckwaterCharacterPlatformSample& current) noexcept {
    if (previous.skiffId != current.skiffId) {
        return WreckwaterCharacterStatus::InvalidInput;
    }
    const bool sameGeneration =
        current.skiffGeneration == previous.skiffGeneration;
    const bool exactSuccessor =
        previous.skiffGeneration
                != std::numeric_limits<uint32_t>::max()
        && current.skiffGeneration
            == previous.skiffGeneration + 1u;
    if ((!sameGeneration && !exactSuccessor)
        || (sameGeneration && current.body != previous.body)
        || (exactSuccessor && current.body == previous.body)) {
        return WreckwaterCharacterStatus::StalePlatformIdentity;
    }
    if (sameGeneration
        && (!previousAvailable
            || current.deckHalfExtents
                != previous.deckHalfExtents
            || current.deckLocalHeight
                != previous.deckLocalHeight
            || !continuousPlatformMotion(
                config, previous, current))) {
        return WreckwaterCharacterStatus::
            PlatformMotionDiscontinuity;
    }
    return WreckwaterCharacterStatus::Accepted;
}

const char* wreckwaterCharacterStatusName(
    WreckwaterCharacterStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterStatus::Accepted:
            return "accepted";
        case WreckwaterCharacterStatus::NotInitialized:
            return "not initialized";
        case WreckwaterCharacterStatus::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterCharacterStatus::InvalidInput:
            return "invalid input";
        case WreckwaterCharacterStatus::WrongInputTick:
            return "wrong input tick";
        case WreckwaterCharacterStatus::NonMonotonicTick:
            return "non-monotonic tick";
        case WreckwaterCharacterStatus::CharacterCapacityExceeded:
            return "character capacity exceeded";
        case WreckwaterCharacterStatus::PlatformCapacityExceeded:
            return "platform capacity exceeded";
        case WreckwaterCharacterStatus::DuplicatePlayer:
            return "duplicate player";
        case WreckwaterCharacterStatus::DuplicatePlatformIdentity:
            return "duplicate platform identity";
        case WreckwaterCharacterStatus::StalePlatformIdentity:
            return "stale platform identity";
        case WreckwaterCharacterStatus::PlatformMotionDiscontinuity:
            return "platform motion discontinuity";
        case WreckwaterCharacterStatus::UnknownCharacter:
            return "unknown character";
        case WreckwaterCharacterStatus::StaleCharacterIdentity:
            return "stale character identity";
        case WreckwaterCharacterStatus::PlayerMismatch:
            return "player mismatch";
        case WreckwaterCharacterStatus::Disconnected:
            return "disconnected";
        case WreckwaterCharacterStatus::AlreadyDisconnected:
            return "already disconnected";
        case WreckwaterCharacterStatus::AlreadyConnected:
            return "already connected";
        case WreckwaterCharacterStatus::StaleConnectionGeneration:
            return "stale connection generation";
        case WreckwaterCharacterStatus::ConnectionGenerationExhausted:
            return "connection generation exhausted";
        case WreckwaterCharacterStatus::ReplayedInputSequence:
            return "replayed input sequence";
        case WreckwaterCharacterStatus::InputSequenceExhausted:
            return "input sequence exhausted";
        case WreckwaterCharacterStatus::InputSequenceJumpTooLarge:
            return "input sequence jump too large";
        case WreckwaterCharacterStatus::StateOutOfRange:
            return "state out of range";
        case WreckwaterCharacterStatus::IdentityExhausted:
            return "identity exhausted";
        case WreckwaterCharacterStatus::TransitionCapacityExceeded:
            return "transition capacity exceeded";
    }
    return "unknown";
}

bool WreckwaterCharacterMovementAuthority::validMode(
    WreckwaterCharacterMode mode) noexcept {
    return mode == WreckwaterCharacterMode::Airborne
        || mode == WreckwaterCharacterMode::OnSkiff
        || mode == WreckwaterCharacterMode::Swimming;
}

bool WreckwaterCharacterMovementAuthority::validConfig(
    const Config& config) noexcept {
    const auto finitePositive = [](float value) noexcept {
        return std::isfinite(value) && value > 0.0f;
    };
    const auto finiteNonNegative = [](float value) noexcept {
        return std::isfinite(value) && value >= 0.0f;
    };
    if (config.tickRateHz != kWreckwaterTickRateHz
        || config.maximumCharacters == 0u
        || config.maximumCharacters > kWreckwaterMaximumCharacters
        || !finitePositive(config.capsule.radius)
        || !finitePositive(config.capsule.height)
        || config.capsule.radius < 0.05f
        || config.capsule.height
            < 2.0f * config.capsule.radius + 0.02f
        || config.capsule.height > physics::kWorldSectorHalf
        || !finiteNonNegative(config.capsule.maxSlopeAngleDegrees)
        || config.capsule.maxSlopeAngleDegrees >= 90.0f
        || !finiteNonNegative(config.capsule.stepUp)
        || !finiteNonNegative(config.capsule.stepDown)
        || !finitePositive(config.deckMaximumSpeed)
        || !finitePositive(config.deckAcceleration)
        || !finitePositive(config.deckBraking)
        || !finiteNonNegative(config.airMaximumSpeed)
        || !finiteNonNegative(config.airAcceleration)
        || !finitePositive(config.jumpSpeed)
        || !finitePositive(config.gravity)
        || !finitePositive(config.terminalVelocity)
        || !std::isfinite(config.waterHeight)
        || !finiteNonNegative(config.waterEnterDepth)
        || !finitePositive(config.waterExitHeight)
        || !finitePositive(config.swimFullSubmersionDepth)
        || config.swimFullSubmersionDepth <= config.waterEnterDepth
        || !std::isfinite(config.swimBuoyancyRatio)
        || config.swimBuoyancyRatio <= 1.0f
        || !finiteNonNegative(config.waterLinearDrag)
        || !finitePositive(config.swimMaximumSpeed)
        || !finitePositive(config.swimAcceleration)
        || !finiteNonNegative(config.swimStrokeAcceleration)
        || !finiteNonNegative(config.boardAssistHorizontalReach)
        || !finiteNonNegative(config.boardAssistMaximumClimb)
        || !finiteNonNegative(config.boardAssistMaximumDrop)
        || !finitePositive(
            config.boardAssistMaximumRelativeSpeed)
        || !finiteNonNegative(config.landingSkin)
        || !std::isfinite(config.minimumWalkableDeckUp)
        || config.minimumWalkableDeckUp < 0.0f
        || config.minimumWalkableDeckUp > 1.0f
        || !finitePositive(config.maximumDeckHalfExtent)
        || config.maximumDeckHalfExtent > physics::kWorldSectorHalf * 0.5f
        || !finitePositive(config.maximumPlatformLinearSpeed)
        || !finitePositive(config.maximumPlatformAngularSpeed)
        || !finitePositive(
            config.maximumPlatformLinearAcceleration)
        || !finitePositive(
            config.maximumPlatformAngularAcceleration)
        || !finiteNonNegative(config.platformPositionTolerance)
        || !finiteNonNegative(config.platformAngleTolerance)
        || !finitePositive(config.maximumCharacterWorldSpeed)
        || config.maximumInputSequenceAdvance == 0u) {
        return false;
    }
    constexpr float maximumRate = 100'000.0f;
    if (config.deckMaximumSpeed > maximumRate
        || config.deckAcceleration > maximumRate
        || config.deckBraking > maximumRate
        || config.airMaximumSpeed > maximumRate
        || config.airAcceleration > maximumRate
        || config.jumpSpeed > maximumRate
        || config.gravity > maximumRate
        || config.terminalVelocity > maximumRate
        || config.waterLinearDrag > maximumRate
        || config.swimMaximumSpeed > maximumRate
        || config.swimAcceleration > maximumRate
        || config.swimStrokeAcceleration > maximumRate
        || config.boardAssistMaximumRelativeSpeed > maximumRate
        || config.maximumPlatformLinearSpeed > maximumRate
        || config.maximumPlatformAngularSpeed > maximumRate
        || config.maximumPlatformLinearAcceleration > maximumRate
        || config.maximumPlatformAngularAcceleration > maximumRate
        || config.maximumCharacterWorldSpeed > maximumRate) {
        return false;
    }
    constexpr float maximumSwimBuoyancyRatio = 8.0f;
    constexpr float maximumBoardReach = 4.0f;
    constexpr float maximumBoardClimb = 3.0f;
    constexpr float maximumBoardDrop = 2.0f;
    constexpr float maximumLandingSkin = 0.5f;
    if (config.swimBuoyancyRatio > maximumSwimBuoyancyRatio
        || config.boardAssistHorizontalReach > maximumBoardReach
        || config.boardAssistMaximumClimb > maximumBoardClimb
        || config.boardAssistMaximumDrop > maximumBoardDrop
        || config.landingSkin > maximumLandingSkin
        || std::abs(config.waterHeight) > 1.0e6f
        || config.waterEnterDepth > config.capsule.height
        || config.waterExitHeight > config.capsule.height
        || config.swimFullSubmersionDepth
            > config.maximumDeckHalfExtent
        || config.capsule.stepUp > config.capsule.height
        || config.capsule.stepDown > config.capsule.height
        || config.minimumWalkableDeckUp
            < std::cos(
                glm::radians(
                    config.capsule.maxSlopeAngleDegrees))) {
        return false;
    }
    if (config.airMaximumSpeed > config.maximumCharacterWorldSpeed
        || config.jumpSpeed > config.maximumCharacterWorldSpeed
        || config.terminalVelocity
            > config.maximumCharacterWorldSpeed
        || config.swimMaximumSpeed
            > config.maximumCharacterWorldSpeed
        || config.boardAssistMaximumRelativeSpeed
            > config.maximumCharacterWorldSpeed) {
        return false;
    }
    const float maximumRotationPerTick =
        config.maximumPlatformAngularSpeed * kFixedTickSeconds
        + 0.5f * config.maximumPlatformAngularAcceleration
            * kFixedTickSeconds * kFixedTickSeconds
        + config.platformAngleTolerance;
    if (!finiteFloat(maximumRotationPerTick)
        || maximumRotationPerTick
            >= glm::pi<float>()) {
        return false;
    }
    const double worstPlatformPointSpeed =
        static_cast<double>(config.maximumPlatformLinearSpeed)
        + std::sqrt(3.0)
            * static_cast<double>(config.maximumDeckHalfExtent)
            * static_cast<double>(config.maximumPlatformAngularSpeed)
        + static_cast<double>(config.deckMaximumSpeed)
        + static_cast<double>(config.jumpSpeed);
    return worstPlatformPointSpeed
        <= static_cast<double>(config.maximumCharacterWorldSpeed);
}

bool WreckwaterCharacterMovementAuthority::validPlatform(
    const Config& config,
    WreckwaterCharacterPlatformSample& platform) noexcept {
    if (platform.skiffId == 0u || platform.skiffGeneration == 0u
        || !platform.body.valid()
        || !physics::isValidWorldPosition(platform.position)
        || !canonicalizeOrientation(platform.orientation)
        || !finiteVector(platform.linearVelocity)
        || !finiteVector(platform.angularVelocity)
        || !finiteVector(platform.deckHalfExtents)
        || !std::isfinite(platform.deckLocalHeight)
        || platform.deckHalfExtents.x <= config.capsule.radius
        || platform.deckHalfExtents.y <= config.capsule.radius
        || platform.deckHalfExtents.x > config.maximumDeckHalfExtent
        || platform.deckHalfExtents.y > config.maximumDeckHalfExtent
        || std::abs(platform.deckLocalHeight)
            > config.maximumDeckHalfExtent
        || !boundedMagnitude(
            platform.linearVelocity,
            config.maximumPlatformLinearSpeed)
        || !boundedMagnitude(
            platform.angularVelocity,
            config.maximumPlatformAngularSpeed)) {
        return false;
    }
    canonicalizeZero(platform.position.local);
    canonicalizeZero(platform.linearVelocity);
    canonicalizeZero(platform.angularVelocity);
    canonicalizeZero(platform.deckHalfExtents);
    canonicalizeZero(platform.deckLocalHeight);
    return true;
}

bool WreckwaterCharacterMovementAuthority::initialize() noexcept {
    return initialize(Config{});
}

bool WreckwaterCharacterMovementAuthority::initialize(
    const Config& config) noexcept {
    Config canonicalConfig = config;
    canonicalizeConfiguration(canonicalConfig);
    if (!validConfig(canonicalConfig)) return false;
    config_ = canonicalConfig;
    characters_ = {};
    generations_ = {};
    generations_.fill(1u);
    pendingInputs_ = {};
    platformHistory_ = {};
    transitionOutput_ = {};
    transitionOutputCount_ = 0u;
    lastClosedTick_ = 0u;
    configurationHash_ = hashConfiguration(config_);
    initialized_ = true;
    updateStateHash();
    return true;
}

WreckwaterCharacterStatus
WreckwaterCharacterMovementAuthority::findCharacterIndex(
    WreckwaterCharacterHandle character, size_t& index) const noexcept {
    index = physics::characterHandleSlot(character);
    if (character == kInvalidWreckwaterCharacter
        || index >= config_.maximumCharacters
        || index >= characters_.size()) {
        return WreckwaterCharacterStatus::UnknownCharacter;
    }
    if (!characters_[index].active) {
        return generations_[index]
                != physics::characterHandleGeneration(character)
            ? WreckwaterCharacterStatus::StaleCharacterIdentity
            : WreckwaterCharacterStatus::UnknownCharacter;
    }
    return characters_[index].handle == character
        ? WreckwaterCharacterStatus::Accepted
        : WreckwaterCharacterStatus::StaleCharacterIdentity;
}

WreckwaterCharacterSpawnResult
WreckwaterCharacterMovementAuthority::spawnCharacter(
    const WreckwaterCharacterSpawn& spawn) noexcept {
    WreckwaterCharacterSpawnResult result;
    if (!initialized_) return result;
    if (spawn.playerId == 0u || spawn.connectionGeneration == 0u
        || !validMode(spawn.mode)
        || !physics::isValidWorldPosition(spawn.feetPosition)
        || !boundedMagnitude(
            spawn.worldVelocity, config_.maximumCharacterWorldSpeed)
        || !finiteVector(spawn.skiffLocalFeetPosition)
        || !finiteVector(spawn.skiffLocalVelocity)) {
        result.status = WreckwaterCharacterStatus::InvalidInput;
        return result;
    }
    const bool aboard = spawn.mode == WreckwaterCharacterMode::OnSkiff;
    if (aboard) {
        if (spawn.skiffId == 0u || spawn.skiffGeneration == 0u
            || !spawn.skiffBody.valid()
            || std::abs(spawn.skiffLocalFeetPosition.x)
                > config_.maximumDeckHalfExtent
            || std::abs(spawn.skiffLocalFeetPosition.y)
                > config_.maximumDeckHalfExtent
            || std::abs(spawn.skiffLocalFeetPosition.z)
                > config_.maximumDeckHalfExtent
            || std::abs(spawn.skiffLocalVelocity.y) > 1.0e-4f
            || glm::length(glm::vec2(
                spawn.skiffLocalVelocity.x,
                spawn.skiffLocalVelocity.z))
                > config_.deckMaximumSpeed) {
            result.status = WreckwaterCharacterStatus::InvalidInput;
            return result;
        }
    } else if (spawn.skiffId != 0u || spawn.skiffGeneration != 0u
               || spawn.skiffBody.valid()
               || spawn.skiffLocalFeetPosition != glm::vec3(0.0f)
               || spawn.skiffLocalVelocity != glm::vec3(0.0f)) {
        result.status = WreckwaterCharacterStatus::InvalidInput;
        return result;
    }
    for (const auto& state : characters_) {
        if (state.active && state.playerId == spawn.playerId) {
            result.status = WreckwaterCharacterStatus::DuplicatePlayer;
            return result;
        }
    }

    size_t selected = kInvalidIndex;
    bool exhaustedSlotExists = false;
    for (size_t index = 0u; index < config_.maximumCharacters; ++index) {
        if (!characters_[index].active) {
            if (generations_[index]
                == std::numeric_limits<uint16_t>::max()) {
                exhaustedSlotExists = true;
                continue;
            }
            selected = index;
            break;
        }
    }
    if (selected == kInvalidIndex) {
        result.status = exhaustedSlotExists
            ? WreckwaterCharacterStatus::IdentityExhausted
            : WreckwaterCharacterStatus::CharacterCapacityExceeded;
        return result;
    }

    WreckwaterCharacterState state;
    state.handle = physics::makeCharacterHandle(
        static_cast<uint32_t>(selected), generations_[selected]);
    state.playerId = spawn.playerId;
    state.connectionGeneration = spawn.connectionGeneration;
    state.mode = spawn.mode;
    state.feetPosition = spawn.feetPosition;
    state.worldVelocity = spawn.worldVelocity;
    state.skiffId = spawn.skiffId;
    state.skiffGeneration = spawn.skiffGeneration;
    state.skiffBody = spawn.skiffBody;
    state.skiffLocalFeetPosition = spawn.skiffLocalFeetPosition;
    state.skiffLocalVelocity = spawn.skiffLocalVelocity;
    state.active = true;
    state.connected = true;
    canonicalizeZero(state.feetPosition.local);
    canonicalizeZero(state.worldVelocity);
    canonicalizeZero(state.skiffLocalFeetPosition);
    canonicalizeZero(state.skiffLocalVelocity);
    characters_[selected] = state;
    pendingInputs_[selected] = {};
    updateStateHash();
    result.status = WreckwaterCharacterStatus::Accepted;
    result.handle = state.handle;
    return result;
}

WreckwaterCharacterStatus
WreckwaterCharacterMovementAuthority::destroyCharacter(
    WreckwaterCharacterHandle character, PlayerId playerId) noexcept {
    if (!initialized_) return WreckwaterCharacterStatus::NotInitialized;
    size_t index = kInvalidIndex;
    const WreckwaterCharacterStatus found =
        findCharacterIndex(character, index);
    if (found != WreckwaterCharacterStatus::Accepted) return found;
    if (characters_[index].playerId != playerId)
        return WreckwaterCharacterStatus::PlayerMismatch;

    characters_[index] = {};
    pendingInputs_[index] = {};
    if (generations_[index] != std::numeric_limits<uint16_t>::max())
        ++generations_[index];
    updateStateHash();
    return WreckwaterCharacterStatus::Accepted;
}

WreckwaterCharacterStatus
WreckwaterCharacterMovementAuthority::disconnectCharacter(
    WreckwaterCharacterHandle character, PlayerId playerId,
    uint32_t connectionGeneration) noexcept {
    if (!initialized_) return WreckwaterCharacterStatus::NotInitialized;
    size_t index = kInvalidIndex;
    const WreckwaterCharacterStatus found =
        findCharacterIndex(character, index);
    if (found != WreckwaterCharacterStatus::Accepted) return found;
    auto& state = characters_[index];
    if (state.playerId != playerId)
        return WreckwaterCharacterStatus::PlayerMismatch;
    if (state.connectionGeneration != connectionGeneration)
        return WreckwaterCharacterStatus::StaleConnectionGeneration;
    if (!state.connected)
        return WreckwaterCharacterStatus::AlreadyDisconnected;
    state.connected = false;
    pendingInputs_[index] = {};
    updateStateHash();
    return WreckwaterCharacterStatus::Accepted;
}

WreckwaterCharacterStatus
WreckwaterCharacterMovementAuthority::reconnectCharacter(
    WreckwaterCharacterHandle character, PlayerId playerId,
    uint32_t priorConnectionGeneration,
    uint32_t nextConnectionGeneration) noexcept {
    if (!initialized_) return WreckwaterCharacterStatus::NotInitialized;
    size_t index = kInvalidIndex;
    const WreckwaterCharacterStatus found =
        findCharacterIndex(character, index);
    if (found != WreckwaterCharacterStatus::Accepted) return found;
    auto& state = characters_[index];
    if (state.playerId != playerId)
        return WreckwaterCharacterStatus::PlayerMismatch;
    if (state.connectionGeneration != priorConnectionGeneration)
        return WreckwaterCharacterStatus::StaleConnectionGeneration;
    if (state.connected)
        return WreckwaterCharacterStatus::AlreadyConnected;
    if (priorConnectionGeneration
        == std::numeric_limits<uint32_t>::max()) {
        return WreckwaterCharacterStatus::ConnectionGenerationExhausted;
    }
    if (nextConnectionGeneration != priorConnectionGeneration + 1u)
        return WreckwaterCharacterStatus::InvalidInput;

    state.connectionGeneration = nextConnectionGeneration;
    state.latestCharacterInputSequence = 0u;
    state.lastAppliedCharacterInputSequence = 0u;
    state.lastInputTick = 0u;
    state.connected = true;
    pendingInputs_[index] = {};
    updateStateHash();
    return WreckwaterCharacterStatus::Accepted;
}

WreckwaterCharacterStatus
WreckwaterCharacterMovementAuthority::submitInput(
    const WreckwaterCharacterInput& requested) noexcept {
    if (!initialized_) return WreckwaterCharacterStatus::NotInitialized;
    if (requested.targetTick != lastClosedTick_ + 1u)
        return WreckwaterCharacterStatus::WrongInputTick;
    if (requested.characterInputSequence == 0u
        || requested.playerId == 0u
        || !finiteVector(requested.move)) {
        return WreckwaterCharacterStatus::InvalidInput;
    }
    if (requested.characterInputSequence
        == std::numeric_limits<uint64_t>::max()) {
        return WreckwaterCharacterStatus::InputSequenceExhausted;
    }
    const float moveLengthSquared = glm::dot(
        requested.move, requested.move);
    if (!std::isfinite(moveLengthSquared)
        || moveLengthSquared > 1.0002f) {
        return WreckwaterCharacterStatus::InvalidInput;
    }

    size_t index = kInvalidIndex;
    const WreckwaterCharacterStatus found =
        findCharacterIndex(requested.character, index);
    if (found != WreckwaterCharacterStatus::Accepted) return found;
    auto& state = characters_[index];
    if (state.playerId != requested.playerId)
        return WreckwaterCharacterStatus::PlayerMismatch;
    if (!state.connected)
        return WreckwaterCharacterStatus::Disconnected;
    if (state.connectionGeneration != requested.connectionGeneration)
        return WreckwaterCharacterStatus::StaleConnectionGeneration;
    if (requested.characterInputSequence
        <= state.latestCharacterInputSequence)
        return WreckwaterCharacterStatus::ReplayedInputSequence;
    if (requested.characterInputSequence
            - state.latestCharacterInputSequence
        > config_.maximumInputSequenceAdvance) {
        return WreckwaterCharacterStatus::InputSequenceJumpTooLarge;
    }

    WreckwaterCharacterInput input = requested;
    clampMagnitude(input.move, 1.0f);
    pendingInputs_[index] = {input, true};
    state.latestCharacterInputSequence =
        input.characterInputSequence;
    updateStateHash();
    return WreckwaterCharacterStatus::Accepted;
}

WreckwaterCharacterTickResult
WreckwaterCharacterMovementAuthority::closeExactTick(
    uint64_t tick,
    std::span<const WreckwaterCharacterPlatformSample> platforms)
    noexcept {
    WreckwaterCharacterTickResult result;
    result.tick = tick;
    result.stateHash = stateHash_;
    if (!initialized_) return result;
    if (tick == 0u || tick != lastClosedTick_ + 1u) {
        result.status = WreckwaterCharacterStatus::NonMonotonicTick;
        return result;
    }
    if (platforms.size() > kWreckwaterMaximumCharacterPlatforms) {
        result.status =
            WreckwaterCharacterStatus::PlatformCapacityExceeded;
        return result;
    }

    std::array<WreckwaterCharacterPlatformSample,
               kWreckwaterMaximumCharacterPlatforms>
        canonicalPlatforms{};
    for (size_t index = 0u; index < platforms.size(); ++index) {
        canonicalPlatforms[index] = platforms[index];
        if (!validPlatform(config_, canonicalPlatforms[index])) {
            result.status = WreckwaterCharacterStatus::InvalidInput;
            return result;
        }
    }
    // The product slice has exactly two skiffs. Keep canonicalization visibly
    // allocation-free instead of routing this two-element case through a
    // general sorting implementation.
    if (platforms.size() == 2u
        && platformIdentityLess(
            canonicalPlatforms[1], canonicalPlatforms[0])) {
        std::swap(canonicalPlatforms[0], canonicalPlatforms[1]);
    }
    for (size_t index = 1u; index < platforms.size(); ++index) {
        const auto& previous = canonicalPlatforms[index - 1u];
        const auto& current = canonicalPlatforms[index];
        if (previous.skiffId == current.skiffId
            || previous.body.index == current.body.index) {
            result.status =
                WreckwaterCharacterStatus::DuplicatePlatformIdentity;
            return result;
        }
    }

    auto nextPlatformHistory = platformHistory_;
    for (auto& history : nextPlatformHistory)
        history.available = false;
    for (size_t sampleIndex = 0u;
         sampleIndex < platforms.size(); ++sampleIndex) {
        const auto& sample = canonicalPlatforms[sampleIndex];
        size_t historyIndex = kInvalidIndex;
        for (size_t index = 0u;
             index < nextPlatformHistory.size(); ++index) {
            if (nextPlatformHistory[index].known
                && nextPlatformHistory[index].sample.skiffId
                    == sample.skiffId) {
                historyIndex = index;
                break;
            }
        }
        if (historyIndex == kInvalidIndex) {
            for (size_t index = 0u;
                 index < nextPlatformHistory.size(); ++index) {
                if (!nextPlatformHistory[index].known) {
                    historyIndex = index;
                    break;
                }
            }
        }
        if (historyIndex == kInvalidIndex) {
            result.status =
                WreckwaterCharacterStatus::PlatformCapacityExceeded;
            return result;
        }
        const auto& old = platformHistory_[historyIndex];
        if (old.known) {
            result.status =
                wreckwaterCharacterPlatformSuccessorStatus(
                    config_, old.sample, old.available, sample);
            if (result.status
                != WreckwaterCharacterStatus::Accepted) {
                return result;
            }
        }
        nextPlatformHistory[historyIndex].sample = sample;
        nextPlatformHistory[historyIndex].known = true;
        nextPlatformHistory[historyIndex].available = true;
    }

    std::array<WreckwaterCharacterPlatformSample,
               kWreckwaterMaximumCharacterPlatforms>
        previousStepPlatforms{};
    size_t previousStepPlatformCount = 0u;
    for (const auto& history : platformHistory_) {
        if (history.available) {
            previousStepPlatforms[previousStepPlatformCount++] =
                history.sample;
        }
    }
    std::array<WreckwaterCharacterPlatformSample,
               kWreckwaterMaximumCharacterPlatforms>
        currentStepPlatforms{};
    size_t currentStepPlatformCount = 0u;
    for (const auto& history : nextPlatformHistory) {
        if (history.available) {
            currentStepPlatforms[currentStepPlatformCount++] =
                history.sample;
        }
    }
    const std::span<const WreckwaterCharacterPlatformSample>
        previousStepPlatformSpan(
            previousStepPlatforms.data(),
            previousStepPlatformCount);
    const std::span<const WreckwaterCharacterPlatformSample>
        currentStepPlatformSpan(
            currentStepPlatforms.data(),
            currentStepPlatformCount);

    auto nextCharacters = characters_;
    std::array<WreckwaterCharacterTransition,
               kWreckwaterMaximumCharacterTransitionsPerTick>
        nextTransitions{};
    size_t nextTransitionCount = 0u;
    uint32_t appliedInputCount = 0u;
    uint32_t neutralInputCount = 0u;
    for (size_t index = 0u;
         index < config_.maximumCharacters; ++index) {
        auto& character = nextCharacters[index];
        if (!character.active) continue;

        WreckwaterCharacterInput input;
        input.targetTick = tick;
        input.character = character.handle;
        input.playerId = character.playerId;
        input.connectionGeneration =
            character.connectionGeneration;
        const bool applied =
            pendingInputs_[index].occupied
            && pendingInputs_[index].input.targetTick == tick
            && character.connected;
        if (applied) {
            input = pendingInputs_[index].input;
            ++appliedInputCount;
        } else {
            ++neutralInputCount;
        }

        const WreckwaterCharacterStepResult stepped =
            wreckwaterCharacterStep({
                .config = config_,
                .priorState = character,
                .input = input,
                .previousPlatforms = previousStepPlatformSpan,
                .currentPlatforms = currentStepPlatformSpan,
                .tick = tick,
            });
        if (!stepped) {
            result.status =
                WreckwaterCharacterStatus::StateOutOfRange;
            return result;
        }
        character = stepped.nextState;
        if (stepped.transition.has_value()) {
            if (nextTransitionCount >= nextTransitions.size()) {
                result.status = WreckwaterCharacterStatus::
                    TransitionCapacityExceeded;
                return result;
            }
            nextTransitions[nextTransitionCount++] =
                *stepped.transition;
        }
    }

    characters_ = nextCharacters;
    platformHistory_ = nextPlatformHistory;
    for (size_t index = 0u;
         index < config_.maximumCharacters; ++index) {
        if (pendingInputs_[index].occupied
            && pendingInputs_[index].input.targetTick == tick) {
            pendingInputs_[index] = {};
        }
    }
    transitionOutput_ = nextTransitions;
    transitionOutputCount_ = nextTransitionCount;
    lastClosedTick_ = tick;
    updateStateHash();

    result.status = WreckwaterCharacterStatus::Accepted;
    result.appliedInputCount = appliedInputCount;
    result.neutralInputCount = neutralInputCount;
    result.transitions =
        std::span<const WreckwaterCharacterTransition>(
            transitionOutput_.data(), transitionOutputCount_);
    result.stateHash = stateHash_;
    return result;
}

const WreckwaterCharacterState*
WreckwaterCharacterMovementAuthority::character(
    WreckwaterCharacterHandle handle) const noexcept {
    if (!initialized_) return nullptr;
    size_t index = kInvalidIndex;
    return findCharacterIndex(handle, index)
            == WreckwaterCharacterStatus::Accepted
        ? &characters_[index]
        : nullptr;
}

WreckwaterCharacterStorageState
WreckwaterCharacterMovementAuthority::storageState() const noexcept {
    return {
        .characters = characters_.data(),
        .platformHistory = platformHistory_.data(),
        .pendingInputs = pendingInputs_.data(),
        .transitions = transitionOutput_.data(),
        .characterCapacity = characters_.size(),
        .platformCapacity = platformHistory_.size(),
        .pendingInputCapacity = pendingInputs_.size(),
        .transitionCapacity = transitionOutput_.size(),
    };
}

void WreckwaterCharacterMovementAuthority::updateStateHash() noexcept {
    uint64_t hash = kFnvOffset64;
    hashU64(hash, configurationHash_);
    hashU64(hash, lastClosedTick_);
    for (size_t index = 0u; index < characters_.size(); ++index) {
        hashU32(hash, generations_[index]);
        const auto& state = characters_[index];
        hashU32(hash, state.handle);
        hashU64(hash, state.playerId);
        hashU32(hash, state.connectionGeneration);
        hashU32(hash, static_cast<uint32_t>(state.mode));
        hashWorldPosition(hash, state.feetPosition);
        hashVec3(hash, state.worldVelocity);
        hashU32(hash, state.skiffId);
        hashU32(hash, state.skiffGeneration);
        hashBodyHandle(hash, state.skiffBody);
        hashVec3(hash, state.skiffLocalFeetPosition);
        hashVec3(hash, state.skiffLocalVelocity);
        hashU64(hash, state.latestCharacterInputSequence);
        hashU64(hash, state.lastAppliedCharacterInputSequence);
        hashU64(hash, state.lastInputTick);
        hashU64(hash, state.lastTransitionTick);
        hashU32(hash, state.active ? 1u : 0u);
        hashU32(hash, state.connected ? 1u : 0u);

        const auto& pending = pendingInputs_[index];
        hashU32(hash, pending.occupied ? 1u : 0u);
        hashU64(hash, pending.input.targetTick);
        hashU64(hash, pending.input.characterInputSequence);
        hashU32(hash, pending.input.character);
        hashU64(hash, pending.input.playerId);
        hashU32(hash, pending.input.connectionGeneration);
        hashVec2(hash, pending.input.move);
        hashU32(hash, pending.input.jump ? 1u : 0u);
        hashU32(hash, pending.input.board ? 1u : 0u);
    }
    for (const auto& history : platformHistory_) {
        hashU32(hash, history.known ? 1u : 0u);
        hashU32(hash, history.available ? 1u : 0u);
        hashPlatform(hash, history.sample);
    }
    stateHash_ = hash;
}

} // namespace voxy::game
