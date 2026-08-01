#include "game/wreckwater_character_step.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace voxy::game {
namespace {

constexpr float kFixedTickSeconds = 1.0f / 60.0f;

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

[[nodiscard]] bool canonicalizeOrientation(
    glm::quat& orientation) noexcept {
    if (!finiteQuaternion(orientation)) return false;
    const float lengthSquared = glm::dot(orientation, orientation);
    if (!finiteFloat(lengthSquared)
        || lengthSquared < 0.25f || lengthSquared > 4.0f) {
        return false;
    }
    orientation *= glm::inversesqrt(lengthSquared);
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

void clampMagnitude(glm::vec3& value, float maximum) noexcept {
    const float squared = glm::dot(value, value);
    if (squared > maximum * maximum) {
        value *= maximum * glm::inversesqrt(squared);
    }
    canonicalizeZero(value);
}

[[nodiscard]] glm::vec2 approach(
    glm::vec2 current, const glm::vec2& target,
    float maximumDelta) noexcept {
    glm::vec2 delta = target - current;
    const float squared = glm::dot(delta, delta);
    if (squared > maximumDelta * maximumDelta && squared > 0.0f) {
        delta *= maximumDelta * glm::inversesqrt(squared);
    }
    current += delta;
    canonicalizeZero(current);
    return current;
}

[[nodiscard]] double absoluteY(
    const physics::WorldPosition& position) noexcept {
    return static_cast<double>(position.sector.y)
             * static_cast<double>(physics::kWorldSectorSize)
        + static_cast<double>(position.local.y);
}

[[nodiscard]] bool worldPositionFromAbsoluteChecked(
    const glm::dvec3& absolute,
    physics::WorldPosition& output) noexcept {
    if (!std::isfinite(absolute.x) || !std::isfinite(absolute.y)
        || !std::isfinite(absolute.z)) {
        return false;
    }
    constexpr double minimum =
        static_cast<double>(std::numeric_limits<int32_t>::min())
            * static_cast<double>(physics::kWorldSectorSize)
        - static_cast<double>(physics::kWorldSectorHalf);
    constexpr double maximum =
        static_cast<double>(std::numeric_limits<int32_t>::max())
            * static_cast<double>(physics::kWorldSectorSize)
        + static_cast<double>(physics::kWorldSectorHalf);
    if (absolute.x < minimum || absolute.x >= maximum
        || absolute.y < minimum || absolute.y >= maximum
        || absolute.z < minimum || absolute.z >= maximum) {
        return false;
    }
    output = physics::worldPositionFromAbsolute(absolute);
    return physics::isValidWorldPosition(output);
}

[[nodiscard]] bool translatedWorldPosition(
    const physics::WorldPosition& position,
    const glm::vec3& displacement,
    physics::WorldPosition& output) noexcept {
    if (!physics::isValidWorldPosition(position)
        || !finiteVector(displacement)) {
        return false;
    }
    return worldPositionFromAbsoluteChecked(
        physics::worldPositionToAbsolute(position)
            + glm::dvec3(displacement),
        output);
}

[[nodiscard]] bool platformPointWorld(
    const WreckwaterCharacterPlatformSample& platform,
    const glm::vec3& localPoint,
    physics::WorldPosition& output) noexcept {
    if (!finiteVector(localPoint)) return false;
    const glm::vec3 rotated = platform.orientation * localPoint;
    return worldPositionFromAbsoluteChecked(
        physics::worldPositionToAbsolute(platform.position)
            + glm::dvec3(rotated),
        output);
}

[[nodiscard]] bool platformLocalPoint(
    const WreckwaterCharacterPlatformSample& platform,
    const physics::WorldPosition& worldPoint,
    glm::vec3& output) noexcept {
    if (!physics::isValidWorldPosition(worldPoint)) return false;
    const glm::dvec3 wide =
        physics::worldPositionToAbsolute(worldPoint)
        - physics::worldPositionToAbsolute(platform.position);
    constexpr double maximumLocalDistance = 1.0e6;
    if (!std::isfinite(wide.x) || !std::isfinite(wide.y)
        || !std::isfinite(wide.z)
        || std::abs(wide.x) > maximumLocalDistance
        || std::abs(wide.y) > maximumLocalDistance
        || std::abs(wide.z) > maximumLocalDistance) {
        return false;
    }
    output = glm::conjugate(platform.orientation) * glm::vec3(wide);
    canonicalizeZero(output);
    return finiteVector(output);
}

[[nodiscard]] glm::vec3 platformPointVelocity(
    const WreckwaterCharacterPlatformSample& platform,
    const glm::vec3& localPoint) noexcept {
    const glm::vec3 worldLever = platform.orientation * localPoint;
    glm::vec3 result = platform.linearVelocity
        + glm::cross(platform.angularVelocity, worldLever);
    canonicalizeZero(result);
    return result;
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

struct RigidDeckSweepSample {
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 localPoint{0.0f};
    float signedDistance = 0.0f;
};

[[nodiscard]] bool sampleRigidDeckSweep(
    const WreckwaterCharacterPlatformSample& previous,
    const WreckwaterCharacterPlatformSample& current,
    const physics::WorldPosition& characterStart,
    const physics::WorldPosition& characterEnd,
    float fraction,
    RigidDeckSweepSample& output) noexcept {
    if (!finiteFloat(fraction)
        || fraction < 0.0f || fraction > 1.0f) {
        return false;
    }
    const double wideFraction = static_cast<double>(fraction);
    const glm::dvec3 previousPlatform =
        physics::worldPositionToAbsolute(previous.position);
    const glm::dvec3 currentPlatform =
        physics::worldPositionToAbsolute(current.position);
    const glm::dvec3 start =
        physics::worldPositionToAbsolute(characterStart);
    const glm::dvec3 end =
        physics::worldPositionToAbsolute(characterEnd);
    const glm::dvec3 platformPosition =
        previousPlatform
        + (currentPlatform - previousPlatform) * wideFraction;
    const glm::dvec3 characterPosition =
        start + (end - start) * wideFraction;
    const glm::dvec3 wideLocal =
        characterPosition - platformPosition;
    constexpr double maximumLocalDistance = 1.0e6;
    if (!std::isfinite(wideLocal.x)
        || !std::isfinite(wideLocal.y)
        || !std::isfinite(wideLocal.z)
        || std::abs(wideLocal.x) > maximumLocalDistance
        || std::abs(wideLocal.y) > maximumLocalDistance
        || std::abs(wideLocal.z) > maximumLocalDistance) {
        return false;
    }

    output.orientation = glm::slerp(
        previous.orientation, current.orientation, fraction);
    if (!canonicalizeOrientation(output.orientation)) return false;
    output.linearVelocity = glm::mix(
        previous.linearVelocity, current.linearVelocity, fraction);
    output.angularVelocity = glm::mix(
        previous.angularVelocity, current.angularVelocity, fraction);
    output.localPoint =
        glm::conjugate(output.orientation) * glm::vec3(wideLocal);
    output.signedDistance =
        output.localPoint.y - current.deckLocalHeight;
    canonicalizeZero(output.linearVelocity);
    canonicalizeZero(output.angularVelocity);
    canonicalizeZero(output.localPoint);
    canonicalizeZero(output.signedDistance);
    return finiteVector(output.linearVelocity)
        && finiteVector(output.angularVelocity)
        && finiteVector(output.localPoint)
        && finiteFloat(output.signedDistance);
}

void clearPlatformLink(WreckwaterCharacterState& state) noexcept {
    state.skiffId = 0u;
    state.skiffGeneration = 0u;
    state.skiffBody = {};
    state.skiffLocalFeetPosition = glm::vec3(0.0f);
    state.skiffLocalVelocity = glm::vec3(0.0f);
}

[[nodiscard]] bool validMode(WreckwaterCharacterMode mode) noexcept {
    return mode == WreckwaterCharacterMode::Airborne
        || mode == WreckwaterCharacterMode::OnSkiff
        || mode == WreckwaterCharacterMode::Swimming;
}

[[nodiscard]] bool canonicalOrientationSign(
    const glm::quat& orientation) noexcept {
    return orientation.w > 0.0f
        || (orientation.w == 0.0f
            && (orientation.x > 0.0f
                || (orientation.x == 0.0f
                    && (orientation.y > 0.0f
                        || (orientation.y == 0.0f
                            && orientation.z >= 0.0f)))));
}

[[nodiscard]] bool validExactPlatform(
    const WreckwaterCharacterMovementAuthority::Config& config,
    const WreckwaterCharacterPlatformSample& platform) noexcept {
    if (platform.skiffId == 0u
        || platform.skiffGeneration == 0u
        || !platform.body.valid()
        || !physics::isValidWorldPosition(platform.position)
        || !finiteQuaternion(platform.orientation)
        || !canonicalOrientationSign(platform.orientation)
        || !finiteVector(platform.linearVelocity)
        || !finiteVector(platform.angularVelocity)
        || !finiteVector(platform.deckHalfExtents)
        || !finiteFloat(platform.deckLocalHeight)
        || platform.deckHalfExtents.x <= config.capsule.radius
        || platform.deckHalfExtents.y <= config.capsule.radius
        || platform.deckHalfExtents.x
            > config.maximumDeckHalfExtent
        || platform.deckHalfExtents.y
            > config.maximumDeckHalfExtent
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
    const float orientationLengthSquared =
        glm::dot(platform.orientation, platform.orientation);
    constexpr float kOrientationUnitTolerance = 2.0e-5f;
    return finiteFloat(orientationLengthSquared)
        && std::abs(orientationLengthSquared - 1.0f)
            <= kOrientationUnitTolerance;
}

[[nodiscard]] bool validExactPlatforms(
    const WreckwaterCharacterMovementAuthority::Config& config,
    std::span<const WreckwaterCharacterPlatformSample>
        platforms) noexcept {
    for (size_t index = 0u; index < platforms.size(); ++index) {
        if (!validExactPlatform(config, platforms[index]))
            return false;
        for (size_t prior = 0u; prior < index; ++prior) {
            if (platforms[prior].skiffId
                    == platforms[index].skiffId
                || platforms[prior].body.index
                    == platforms[index].body.index) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool validStepInput(
    const WreckwaterCharacterStepInput& step) noexcept {
    if (step.tick == 0u
        || !WreckwaterCharacterMovementAuthority::validConfig(
            step.config)
        || step.previousPlatforms.size()
            > kWreckwaterMaximumCharacterPlatforms
        || step.currentPlatforms.size()
            > kWreckwaterMaximumCharacterPlatforms
        || !step.priorState.active
        || !validMode(step.priorState.mode)
        || step.input.targetTick != step.tick
        || step.input.character != step.priorState.handle
        || step.input.playerId != step.priorState.playerId
        || step.input.connectionGeneration
            != step.priorState.connectionGeneration
        || !finiteVector(step.input.move)) {
        return false;
    }
    if (!validExactPlatforms(
            step.config, step.previousPlatforms)
        || !validExactPlatforms(
            step.config, step.currentPlatforms)) {
        return false;
    }
    const float moveLengthSquared =
        glm::dot(step.input.move, step.input.move);
    if (!finiteFloat(moveLengthSquared)
        || moveLengthSquared > 1.0002f) {
        return false;
    }
    const bool neutral =
        step.input.characterInputSequence == 0u;
    if (neutral
        && (step.input.move != glm::vec2(0.0f)
            || step.input.jump || step.input.board)) {
        return false;
    }
    return neutral || step.priorState.connected;
}

} // namespace

WreckwaterCharacterStepResult wreckwaterCharacterStep(
    const WreckwaterCharacterStepInput& step) noexcept {
    WreckwaterCharacterStepResult result;
    result.nextState = step.priorState;
    if (!validStepInput(step)) return result;

    const auto& config = step.config;
    WreckwaterCharacterInput input = step.input;
    clampMagnitude(input.move, 1.0f);
    WreckwaterCharacterState state = step.priorState;
    if (input.characterInputSequence != 0u) {
        state.lastAppliedCharacterInputSequence =
            input.characterInputSequence;
        state.lastInputTick = step.tick;
    }

    const WreckwaterCharacterMode originalMode = state.mode;
    const SkiffId originalSkiffId = state.skiffId;
    const uint32_t originalSkiffGeneration = state.skiffGeneration;
    const physics::BodyHandle originalSkiffBody = state.skiffBody;
    bool advanceAirborne =
        state.mode == WreckwaterCharacterMode::Airborne;
    const bool advanceSwimming =
        state.mode == WreckwaterCharacterMode::Swimming;

    const auto findExact = [](
        std::span<const WreckwaterCharacterPlatformSample> platforms,
        SkiffId skiffId, uint32_t generation,
        physics::BodyHandle body)
        -> const WreckwaterCharacterPlatformSample* {
        for (const auto& platform : platforms) {
            if (platform.skiffId == skiffId
                && platform.skiffGeneration == generation
                && platform.body == body) {
                return &platform;
            }
        }
        return nullptr;
    };
    const auto walkable = [&config](
        const WreckwaterCharacterPlatformSample& platform) {
        const glm::vec3 up =
            platform.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        return finiteVector(up)
            && up.y >= config.minimumWalkableDeckUp;
    };
    const auto setAboard = [&config](
        WreckwaterCharacterState& character,
        const WreckwaterCharacterPlatformSample& platform,
        glm::vec3 localFeet, glm::vec3 inheritedWorldVelocity) {
        const float safeX =
            platform.deckHalfExtents.x - config.capsule.radius;
        const float safeZ =
            platform.deckHalfExtents.y - config.capsule.radius;
        localFeet.x = std::clamp(localFeet.x, -safeX, safeX);
        localFeet.y = platform.deckLocalHeight;
        localFeet.z = std::clamp(localFeet.z, -safeZ, safeZ);
        const glm::vec3 pointVelocity =
            platformPointVelocity(platform, localFeet);
        glm::vec3 localVelocity =
            glm::conjugate(platform.orientation)
            * (inheritedWorldVelocity - pointVelocity);
        glm::vec2 planar(localVelocity.x, localVelocity.z);
        clampMagnitude(planar, config.deckMaximumSpeed);
        localVelocity = {planar.x, 0.0f, planar.y};

        character.mode = WreckwaterCharacterMode::OnSkiff;
        character.skiffId = platform.skiffId;
        character.skiffGeneration = platform.skiffGeneration;
        character.skiffBody = platform.body;
        character.skiffLocalFeetPosition = localFeet;
        character.skiffLocalVelocity = localVelocity;
        if (!platformPointWorld(
                platform, localFeet, character.feetPosition)) {
            return false;
        }
        character.worldVelocity =
            platformPointVelocity(platform, localFeet)
            + platform.orientation * localVelocity;
        canonicalizeZero(character.worldVelocity);
        return boundedMagnitude(
            character.worldVelocity,
            config.maximumCharacterWorldSpeed);
    };
    const auto enterWater = [&config](
        const physics::WorldPosition& position) {
        return absoluteY(position)
            <= static_cast<double>(config.waterHeight)
             - static_cast<double>(config.waterEnterDepth);
    };

    if (state.mode == WreckwaterCharacterMode::OnSkiff) {
        const auto* platform = findExact(
            step.currentPlatforms, state.skiffId,
            state.skiffGeneration, state.skiffBody);
        if (platform == nullptr) {
            state.mode = WreckwaterCharacterMode::Airborne;
            clearPlatformLink(state);
            advanceAirborne = true;
        } else {
            state.skiffLocalFeetPosition.y =
                platform->deckLocalHeight;
            if (!platformPointWorld(
                    *platform, state.skiffLocalFeetPosition,
                    state.feetPosition)) {
                result.status =
                    WreckwaterCharacterStatus::StateOutOfRange;
                return result;
            }
            state.worldVelocity =
                platformPointVelocity(
                    *platform, state.skiffLocalFeetPosition)
                + platform->orientation * state.skiffLocalVelocity;
            if (!boundedMagnitude(
                    state.worldVelocity,
                    config.maximumCharacterWorldSpeed)) {
                result.status =
                    WreckwaterCharacterStatus::StateOutOfRange;
                return result;
            }
            if (!walkable(*platform)
                || enterWater(state.feetPosition)) {
                state.mode = enterWater(state.feetPosition)
                    ? WreckwaterCharacterMode::Swimming
                    : WreckwaterCharacterMode::Airborne;
                clearPlatformLink(state);
            } else {
                glm::vec2 localPlanar(
                    state.skiffLocalVelocity.x,
                    state.skiffLocalVelocity.z);
                const glm::vec2 target =
                    input.move * config.deckMaximumSpeed;
                const float rate =
                    glm::dot(input.move, input.move) > 0.0f
                    ? config.deckAcceleration
                    : config.deckBraking;
                localPlanar = approach(
                    localPlanar, target,
                    rate * kFixedTickSeconds);
                clampMagnitude(localPlanar, config.deckMaximumSpeed);
                state.skiffLocalVelocity =
                    {localPlanar.x, 0.0f, localPlanar.y};

                if (input.jump) {
                    state.worldVelocity =
                        platformPointVelocity(
                            *platform,
                            state.skiffLocalFeetPosition)
                        + platform->orientation
                            * state.skiffLocalVelocity
                        + platform->orientation
                            * glm::vec3(
                                0.0f, config.jumpSpeed, 0.0f);
                    state.mode = WreckwaterCharacterMode::Airborne;
                    clearPlatformLink(state);
                } else {
                    state.skiffLocalFeetPosition.x +=
                        state.skiffLocalVelocity.x
                            * kFixedTickSeconds;
                    state.skiffLocalFeetPosition.z +=
                        state.skiffLocalVelocity.z
                            * kFixedTickSeconds;
                    canonicalizeZero(state.skiffLocalFeetPosition);
                    const float safeX =
                        platform->deckHalfExtents.x
                        - config.capsule.radius;
                    const float safeZ =
                        platform->deckHalfExtents.y
                        - config.capsule.radius;
                    const bool leftDeck =
                        std::abs(state.skiffLocalFeetPosition.x)
                            > safeX
                        || std::abs(state.skiffLocalFeetPosition.z)
                            > safeZ;
                    if (!platformPointWorld(
                            *platform,
                            state.skiffLocalFeetPosition,
                            state.feetPosition)) {
                        result.status =
                            WreckwaterCharacterStatus::
                                StateOutOfRange;
                        return result;
                    }
                    state.worldVelocity =
                        platformPointVelocity(
                            *platform,
                            state.skiffLocalFeetPosition)
                        + platform->orientation
                            * state.skiffLocalVelocity;
                    if (leftDeck) {
                        state.mode =
                            WreckwaterCharacterMode::Airborne;
                        clearPlatformLink(state);
                    }
                }
            }
        }
    }
    if (advanceAirborne) {
        glm::vec2 horizontal(
            state.worldVelocity.x, state.worldVelocity.z);
        if (glm::dot(input.move, input.move) > 0.0f) {
            const glm::vec2 target =
                input.move * config.airMaximumSpeed;
            horizontal = approach(
                horizontal, target,
                config.airAcceleration * kFixedTickSeconds);
        }
        state.worldVelocity.x = horizontal.x;
        state.worldVelocity.z = horizontal.y;
        state.worldVelocity.y = std::max(
            state.worldVelocity.y
                - config.gravity * kFixedTickSeconds,
            -config.terminalVelocity);
        clampMagnitude(
            state.worldVelocity,
            config.maximumCharacterWorldSpeed);

        const physics::WorldPosition start = state.feetPosition;
        physics::WorldPosition end;
        if (!translatedWorldPosition(
                start,
                state.worldVelocity * kFixedTickSeconds,
                end)) {
            result.status =
                WreckwaterCharacterStatus::StateOutOfRange;
            return result;
        }

        const WreckwaterCharacterPlatformSample* best = nullptr;
        glm::vec3 bestLocal(0.0f);
        float bestFraction = std::numeric_limits<float>::max();
        for (const auto& platform : step.currentPlatforms) {
            if (!walkable(platform)) continue;
            const auto* previous = findExact(
                step.previousPlatforms, platform.skiffId,
                platform.skiffGeneration, platform.body);
            if (previous == nullptr) continue;
            constexpr uint32_t kSweepSubsteps = 16u;
            constexpr uint32_t kSweepBisections = 12u;
            RigidDeckSweepSample left;
            if (!sampleRigidDeckSweep(
                    *previous, platform, start, end, 0.0f, left)) {
                continue;
            }
            bool impactFound = false;
            float fraction = 0.0f;
            RigidDeckSweepSample impact;
            for (uint32_t sweepStep = 1u;
                 sweepStep <= kSweepSubsteps; ++sweepStep) {
                const float rightFraction =
                    static_cast<float>(sweepStep)
                    / static_cast<float>(kSweepSubsteps);
                RigidDeckSweepSample right;
                if (!sampleRigidDeckSweep(
                        *previous, platform, start, end,
                        rightFraction, right)) {
                    break;
                }
                if (left.signedDistance >= 0.0f
                    && right.signedDistance <= 0.0f) {
                    float lower =
                        static_cast<float>(sweepStep - 1u)
                        / static_cast<float>(kSweepSubsteps);
                    float upper = rightFraction;
                    RigidDeckSweepSample upperSample = right;
                    for (uint32_t iteration = 0u;
                         iteration < kSweepBisections;
                         ++iteration) {
                        const float middle =
                            0.5f * (lower + upper);
                        RigidDeckSweepSample middleSample;
                        if (!sampleRigidDeckSweep(
                                *previous, platform, start, end,
                                middle, middleSample)) {
                            break;
                        }
                        if (middleSample.signedDistance > 0.0f) {
                            lower = middle;
                        } else {
                            upper = middle;
                            upperSample = middleSample;
                        }
                    }
                    fraction = upper;
                    impact = upperSample;
                    impactFound = true;
                    break;
                }
                left = right;
            }
            if (!impactFound
                || std::abs(impact.signedDistance)
                    > config.landingSkin) {
                continue;
            }
            const glm::vec3 worldLever =
                impact.orientation * impact.localPoint;
            const glm::vec3 pointVelocity =
                impact.linearVelocity
                + glm::cross(
                    impact.angularVelocity, worldLever);
            const glm::vec3 relativeVelocity =
                state.worldVelocity - pointVelocity;
            const glm::vec3 deckUp =
                impact.orientation
                * glm::vec3(0.0f, 1.0f, 0.0f);
            if (deckUp.y < config.minimumWalkableDeckUp
                || glm::dot(relativeVelocity, deckUp) > 0.0f) {
                continue;
            }
            glm::vec3 local = impact.localPoint;
            local.y = platform.deckLocalHeight;
            const float safeX =
                platform.deckHalfExtents.x
                - config.capsule.radius;
            const float safeZ =
                platform.deckHalfExtents.y
                - config.capsule.radius;
            if (std::abs(local.x) > safeX
                || std::abs(local.z) > safeZ) {
                continue;
            }
            if (best == nullptr || fraction < bestFraction
                || (fraction == bestFraction
                    && platformIdentityLess(platform, *best))) {
                best = &platform;
                bestLocal = local;
                bestFraction = fraction;
            }
        }
        state.feetPosition = end;
        if (best != nullptr) {
            if (!setAboard(
                    state, *best, bestLocal,
                    state.worldVelocity)) {
                result.status =
                    WreckwaterCharacterStatus::StateOutOfRange;
                return result;
            }
        } else if (enterWater(state.feetPosition)) {
            state.mode = WreckwaterCharacterMode::Swimming;
            clearPlatformLink(state);
        }
    } else if (advanceSwimming) {
        glm::vec2 horizontal(
            state.worldVelocity.x, state.worldVelocity.z);
        const glm::vec2 target =
            input.move * config.swimMaximumSpeed;
        horizontal = approach(
            horizontal, target,
            config.swimAcceleration * kFixedTickSeconds);
        state.worldVelocity.x = horizontal.x;
        state.worldVelocity.z = horizontal.y;

        const double depth = static_cast<double>(config.waterHeight)
            - absoluteY(state.feetPosition);
        const float immersed = std::clamp(
            static_cast<float>(
                depth
                / static_cast<double>(
                    config.swimFullSubmersionDepth)),
            0.0f, 1.0f);
        float verticalAcceleration =
            -config.gravity
            + config.gravity
                * config.swimBuoyancyRatio * immersed;
        if (input.jump) {
            verticalAcceleration += config.swimStrokeAcceleration;
        }
        state.worldVelocity.y +=
            verticalAcceleration * kFixedTickSeconds;
        const float dragFactor = 1.0f
            / (1.0f
               + config.waterLinearDrag * kFixedTickSeconds);
        state.worldVelocity *= dragFactor;
        clampMagnitude(
            state.worldVelocity,
            config.maximumCharacterWorldSpeed);
        if (!translatedWorldPosition(
                state.feetPosition,
                state.worldVelocity * kFixedTickSeconds,
                state.feetPosition)) {
            result.status =
                WreckwaterCharacterStatus::StateOutOfRange;
            return result;
        }

        const WreckwaterCharacterPlatformSample* best = nullptr;
        glm::vec3 bestLocal(0.0f);
        float bestDistanceSquared =
            std::numeric_limits<float>::max();
        if (input.board) {
            for (const auto& platform : step.currentPlatforms) {
                if (!walkable(platform)) continue;
                glm::vec3 local;
                if (!platformLocalPoint(
                        platform, state.feetPosition, local)) {
                    continue;
                }
                const float deckDelta =
                    platform.deckLocalHeight - local.y;
                if (deckDelta > config.boardAssistMaximumClimb
                    || deckDelta
                        < -config.boardAssistMaximumDrop) {
                    continue;
                }
                const float safeX =
                    platform.deckHalfExtents.x
                    - config.capsule.radius;
                const float safeZ =
                    platform.deckHalfExtents.y
                    - config.capsule.radius;
                const float outsideX =
                    std::max(std::abs(local.x) - safeX, 0.0f);
                const float outsideZ =
                    std::max(std::abs(local.z) - safeZ, 0.0f);
                const float horizontalDistanceSquared =
                    outsideX * outsideX + outsideZ * outsideZ;
                const bool inside =
                    outsideX == 0.0f && outsideZ == 0.0f;
                if (inside
                    || horizontalDistanceSquared
                        > config.boardAssistHorizontalReach
                            * config.boardAssistHorizontalReach) {
                    continue;
                }
                glm::vec3 deckLocal(
                    std::clamp(local.x, -safeX, safeX),
                    platform.deckLocalHeight,
                    std::clamp(local.z, -safeZ, safeZ));
                const glm::vec2 toDeck(
                    deckLocal.x - local.x,
                    deckLocal.z - local.z);
                const glm::vec3 localRequest3 =
                    glm::conjugate(platform.orientation)
                    * glm::vec3(
                        input.move.x, 0.0f, input.move.y);
                const glm::vec2 localRequest(
                    localRequest3.x, localRequest3.z);
                if (glm::dot(localRequest, toDeck) <= 0.0f) {
                    continue;
                }
                const glm::vec3 relativeVelocity =
                    state.worldVelocity
                    - platformPointVelocity(platform, deckLocal);
                if (!boundedMagnitude(
                        relativeVelocity,
                        config.boardAssistMaximumRelativeSpeed)) {
                    continue;
                }
                physics::WorldPosition deckWorld;
                if (!platformPointWorld(
                        platform, deckLocal, deckWorld)
                    || absoluteY(deckWorld)
                        <= static_cast<double>(config.waterHeight)
                         - static_cast<double>(
                             config.waterEnterDepth)) {
                    continue;
                }
                if (best == nullptr
                    || horizontalDistanceSquared
                        < bestDistanceSquared
                    || (horizontalDistanceSquared
                            == bestDistanceSquared
                        && platformIdentityLess(
                            platform, *best))) {
                    best = &platform;
                    bestLocal = deckLocal;
                    bestDistanceSquared =
                        horizontalDistanceSquared;
                }
            }
        }
        if (best != nullptr) {
            if (!setAboard(
                    state, *best, bestLocal,
                    state.worldVelocity)) {
                result.status =
                    WreckwaterCharacterStatus::StateOutOfRange;
                return result;
            }
        } else if (absoluteY(state.feetPosition)
                   > static_cast<double>(config.waterHeight)
                     + static_cast<double>(
                         config.waterExitHeight)) {
            state.mode = WreckwaterCharacterMode::Airborne;
            clearPlatformLink(state);
        }
    }

    canonicalizeZero(state.feetPosition.local);
    canonicalizeZero(state.worldVelocity);
    canonicalizeZero(state.skiffLocalFeetPosition);
    canonicalizeZero(state.skiffLocalVelocity);
    if (!physics::isValidWorldPosition(state.feetPosition)
        || !boundedMagnitude(
            state.worldVelocity,
            config.maximumCharacterWorldSpeed)
        || !finiteVector(state.skiffLocalFeetPosition)
        || !finiteVector(state.skiffLocalVelocity)) {
        result.status = WreckwaterCharacterStatus::StateOutOfRange;
        return result;
    }

    const bool platformChanged =
        originalSkiffId != state.skiffId
        || originalSkiffGeneration != state.skiffGeneration
        || originalSkiffBody != state.skiffBody;
    if (originalMode != state.mode || platformChanged) {
        state.lastTransitionTick = step.tick;
        WreckwaterCharacterTransition transition;
        transition.tick = step.tick;
        transition.character = state.handle;
        transition.playerId = state.playerId;
        transition.from = originalMode;
        transition.to = state.mode;
        if (state.mode == WreckwaterCharacterMode::OnSkiff) {
            transition.skiffId = state.skiffId;
            transition.skiffGeneration = state.skiffGeneration;
            transition.skiffBody = state.skiffBody;
        } else {
            transition.skiffId = originalSkiffId;
            transition.skiffGeneration = originalSkiffGeneration;
            transition.skiffBody = originalSkiffBody;
        }
        result.transition = transition;
    }

    result.nextState = state;
    result.status = WreckwaterCharacterStatus::Accepted;
    return result;
}

} // namespace voxy::game
