#include "client/wreckwater_character_platform_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::client {
namespace {

constexpr float kFixedTickSeconds = 1.0f / 60.0f;

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

[[nodiscard]] bool finiteQuaternion(
    const glm::quat& value) noexcept {
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
        + static_cast<double>(value.y)
            * static_cast<double>(value.y)
        + static_cast<double>(value.z)
            * static_cast<double>(value.z);
    return std::isfinite(squared)
        && squared <= static_cast<double>(maximum)
            * static_cast<double>(maximum);
}

[[nodiscard]] bool worldPositionFromAbsoluteChecked(
    const glm::dvec3& absolute,
    physics::WorldPosition& output) noexcept {
    if (!std::isfinite(absolute.x)
        || !std::isfinite(absolute.y)
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

[[nodiscard]] bool platformLess(
    const game::WreckwaterCharacterPlatformSample& lhs,
    const game::WreckwaterCharacterPlatformSample& rhs) noexcept {
    return std::tuple{
        lhs.skiffId, lhs.skiffGeneration,
        lhs.body.index, lhs.body.generation}
        < std::tuple{
        rhs.skiffId, rhs.skiffGeneration,
        rhs.body.index, rhs.body.generation};
}

[[nodiscard]] bool validCanonicalPlatform(
    const game::WreckwaterCharacterMovementAuthority::Config&
        config,
    game::WreckwaterCharacterPlatformSample& platform) noexcept {
    if (platform.skiffId == 0u
        || platform.skiffGeneration == 0u
        || !platform.body.valid()
        || !physics::isValidWorldPosition(platform.position)
        || !canonicalizeOrientation(platform.orientation)
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
    canonicalizeZero(platform.position.local);
    canonicalizeZero(platform.linearVelocity);
    canonicalizeZero(platform.angularVelocity);
    canonicalizeZero(platform.deckHalfExtents);
    canonicalizeZero(platform.deckLocalHeight);
    return true;
}

[[nodiscard]] bool sameLifetimeAndDeck(
    const game::WreckwaterCharacterPlatformSample& lhs,
    const game::WreckwaterCharacterPlatformSample& rhs) noexcept {
    return lhs.skiffId == rhs.skiffId
        && lhs.skiffGeneration == rhs.skiffGeneration
        && lhs.body == rhs.body
        && lhs.deckHalfExtents == rhs.deckHalfExtents
        && lhs.deckLocalHeight == rhs.deckLocalHeight;
}

[[nodiscard]] double orientationDifference(
    const glm::quat& lhs, const glm::quat& rhs) noexcept {
    glm::quat relative = lhs * glm::conjugate(rhs);
    if (!canonicalizeOrientation(relative)) {
        return std::numeric_limits<double>::infinity();
    }
    const double clampedW = std::clamp(
        static_cast<double>(relative.w), 0.0, 1.0);
    return 2.0 * std::acos(clampedW);
}

} // namespace

const char* wreckwaterCharacterPlatformTimelineStatusName(
    WreckwaterCharacterPlatformTimelineStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterPlatformTimelineStatus::Accepted:
            return "accepted";
        case WreckwaterCharacterPlatformTimelineStatus::
                NotInitialized:
            return "not initialized";
        case WreckwaterCharacterPlatformTimelineStatus::
                InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame:
            return "invalid certified frame";
        case WreckwaterCharacterPlatformTimelineStatus::
                StaleCertifiedSnapshot:
            return "stale certified snapshot";
        case WreckwaterCharacterPlatformTimelineStatus::
                SnapshotSequenceExhausted:
            return "snapshot sequence exhausted";
        case WreckwaterCharacterPlatformTimelineStatus::
                TickExhausted:
            return "tick exhausted";
        case WreckwaterCharacterPlatformTimelineStatus::
                PlatformCapacityExceeded:
            return "platform capacity exceeded";
        case WreckwaterCharacterPlatformTimelineStatus::
                PlatformLifetimeMutation:
            return "platform lifetime mutation";
        case WreckwaterCharacterPlatformTimelineStatus::
                ImpossibleCorrection:
            return "impossible correction";
        case WreckwaterCharacterPlatformTimelineStatus::
                PredictionHorizonExceeded:
            return "prediction horizon exceeded";
        case WreckwaterCharacterPlatformTimelineStatus::
                FrameUnavailable:
            return "frame unavailable";
        case WreckwaterCharacterPlatformTimelineStatus::
                PositionOverflow:
            return "position overflow";
    }
    return "unknown";
}

bool WreckwaterCharacterPlatformTimeline::initialize(
    const Config& config) noexcept {
    initialized_ = false;
    if (config.predictionHorizonTicks == 0u
        || config.predictionHorizonTicks
            > kWreckwaterCharacterPlatformTimelineMaximumPredictionTicks
        || !game::WreckwaterCharacterMovementAuthority::validConfig(
            config.movement)) {
        return false;
    }
    config_ = config;
    initialized_ = true;
    reset();
    return true;
}

void WreckwaterCharacterPlatformTimeline::reset() noexcept {
    frames_ = {};
    frameCount_ = 0u;
    telemetry_ = {};
}

WreckwaterCharacterPlatformTimelineStatus
WreckwaterCharacterPlatformTimeline::canonicalCertifiedFrame(
    uint64_t snapshotSequence,
    uint64_t certifiedTick,
    std::span<const game::WreckwaterCharacterPlatformSample>
        certifiedPlatforms,
    WreckwaterCharacterPlatformPredictionFrame& output)
    const noexcept {
    if (snapshotSequence == 0u || certifiedTick == 0u) {
        return WreckwaterCharacterPlatformTimelineStatus::
            InvalidCertifiedFrame;
    }
    if (snapshotSequence
        == std::numeric_limits<uint64_t>::max()) {
        return WreckwaterCharacterPlatformTimelineStatus::
            SnapshotSequenceExhausted;
    }
    if (certifiedTick == std::numeric_limits<uint64_t>::max()) {
        return WreckwaterCharacterPlatformTimelineStatus::
            TickExhausted;
    }
    if (certifiedPlatforms.size()
        > game::kWreckwaterMaximumCharacterPlatforms) {
        return WreckwaterCharacterPlatformTimelineStatus::
            PlatformCapacityExceeded;
    }

    WreckwaterCharacterPlatformPredictionFrame candidate;
    candidate.tick = certifiedTick;
    candidate.certifiedSnapshotSequence = snapshotSequence;
    candidate.certifiedBaseTick = certifiedTick;
    candidate.count =
        static_cast<uint32_t>(certifiedPlatforms.size());
    for (size_t index = 0u;
         index < certifiedPlatforms.size(); ++index) {
        candidate.platforms[index] = certifiedPlatforms[index];
        if (!validCanonicalPlatform(
                config_.movement,
                candidate.platforms[index])) {
            return WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame;
        }
    }
    if (candidate.count == 2u
        && platformLess(
            candidate.platforms[1],
            candidate.platforms[0])) {
        std::swap(
            candidate.platforms[0],
            candidate.platforms[1]);
    }
    for (uint32_t index = 1u;
         index < candidate.count; ++index) {
        if (candidate.platforms[index - 1u].skiffId
                == candidate.platforms[index].skiffId
            || candidate.platforms[index - 1u].body.index
                == candidate.platforms[index].body.index) {
            return WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame;
        }
    }
    output = candidate;
    return WreckwaterCharacterPlatformTimelineStatus::Accepted;
}

WreckwaterCharacterPlatformTimelineStatus
WreckwaterCharacterPlatformTimeline::validateCorrection(
    const WreckwaterCharacterPlatformPredictionFrame& predicted,
    const WreckwaterCharacterPlatformPredictionFrame& certified)
    const noexcept {
    if (predicted.tick != certified.tick
        || predicted.count != certified.count) {
        return WreckwaterCharacterPlatformTimelineStatus::
            PlatformLifetimeMutation;
    }
    for (uint32_t index = 0u;
         index < certified.count; ++index) {
        if (!sameLifetimeAndDeck(
                predicted.platforms[index],
                certified.platforms[index])) {
            return WreckwaterCharacterPlatformTimelineStatus::
                PlatformLifetimeMutation;
        }
    }

    const uint64_t elapsedTicks =
        certified.tick - predicted.certifiedBaseTick;
    const double elapsedSeconds =
        static_cast<double>(elapsedTicks) / 60.0;
    const double linearPositionLimit =
        0.5
            * static_cast<double>(
                config_.movement
                    .maximumPlatformLinearAcceleration)
            * elapsedSeconds * elapsedSeconds
        + static_cast<double>(
            config_.movement.platformPositionTolerance);
    const double linearVelocityLimit =
        static_cast<double>(
            config_.movement.maximumPlatformLinearAcceleration)
            * elapsedSeconds
        + static_cast<double>(
            config_.movement.platformPositionTolerance)
            / static_cast<double>(kFixedTickSeconds);
    const double angularPositionLimit =
        0.5
            * static_cast<double>(
                config_.movement
                    .maximumPlatformAngularAcceleration)
            * elapsedSeconds * elapsedSeconds
        + static_cast<double>(
            config_.movement.platformAngleTolerance);
    const double angularVelocityLimit =
        static_cast<double>(
            config_.movement.maximumPlatformAngularAcceleration)
            * elapsedSeconds
        + static_cast<double>(
            config_.movement.platformAngleTolerance)
            / static_cast<double>(kFixedTickSeconds);

    for (uint32_t index = 0u;
         index < certified.count; ++index) {
        const auto& expected = predicted.platforms[index];
        const auto& actual = certified.platforms[index];
        const double positionError = glm::length(
            physics::worldPositionToAbsolute(actual.position)
            - physics::worldPositionToAbsolute(expected.position));
        const double velocityError = glm::length(
            glm::dvec3(actual.linearVelocity)
            - glm::dvec3(expected.linearVelocity));
        const double angleError = orientationDifference(
            actual.orientation, expected.orientation);
        const double angularVelocityError = glm::length(
            glm::dvec3(actual.angularVelocity)
            - glm::dvec3(expected.angularVelocity));
        if (!std::isfinite(positionError)
            || !std::isfinite(velocityError)
            || !std::isfinite(angleError)
            || !std::isfinite(angularVelocityError)
            || positionError > linearPositionLimit
            || velocityError > linearVelocityLimit
            || angleError > angularPositionLimit
            || angularVelocityError > angularVelocityLimit) {
            return WreckwaterCharacterPlatformTimelineStatus::
                ImpossibleCorrection;
        }
    }
    return WreckwaterCharacterPlatformTimelineStatus::Accepted;
}

WreckwaterCharacterPlatformTimelineStatus
WreckwaterCharacterPlatformTimeline::integrateFrame(
    const WreckwaterCharacterPlatformPredictionFrame& previous,
    WreckwaterCharacterPlatformPredictionFrame& current)
    const noexcept {
    if (previous.tick == std::numeric_limits<uint64_t>::max()) {
        return WreckwaterCharacterPlatformTimelineStatus::
            TickExhausted;
    }
    current = previous;
    current.tick = previous.tick + 1u;
    for (uint32_t index = 0u;
         index < current.count; ++index) {
        const auto& prior = previous.platforms[index];
        auto& next = current.platforms[index];
        const glm::dvec3 absolute =
            physics::worldPositionToAbsolute(prior.position)
            + glm::dvec3(prior.linearVelocity)
                * static_cast<double>(kFixedTickSeconds);
        if (!worldPositionFromAbsoluteChecked(
                absolute, next.position)) {
            return WreckwaterCharacterPlatformTimelineStatus::
                PositionOverflow;
        }

        const float angularSpeed =
            glm::length(prior.angularVelocity);
        if (!finiteFloat(angularSpeed)) {
            return WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame;
        }
        if (angularSpeed > 0.0f) {
            const glm::vec3 axis =
                prior.angularVelocity / angularSpeed;
            const glm::quat delta = glm::angleAxis(
                angularSpeed * kFixedTickSeconds, axis);
            next.orientation = delta * prior.orientation;
        }
        if (!canonicalizeOrientation(next.orientation)) {
            return WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame;
        }
    }
    return WreckwaterCharacterPlatformTimelineStatus::Accepted;
}

WreckwaterCharacterPlatformTimelineStatus
WreckwaterCharacterPlatformTimeline::appendThrough(
    uint64_t targetTick) noexcept {
    if (frameCount_ == 0u) {
        return WreckwaterCharacterPlatformTimelineStatus::
            FrameUnavailable;
    }
    const uint64_t baseTick = frames_[0].tick;
    if (targetTick < baseTick) {
        return WreckwaterCharacterPlatformTimelineStatus::
            FrameUnavailable;
    }
    if (targetTick == std::numeric_limits<uint64_t>::max()) {
        return WreckwaterCharacterPlatformTimelineStatus::
            TickExhausted;
    }
    if (targetTick - baseTick
        > config_.predictionHorizonTicks) {
        return WreckwaterCharacterPlatformTimelineStatus::
            PredictionHorizonExceeded;
    }
    while (frames_[frameCount_ - 1u].tick < targetTick) {
        if (frameCount_ >= capacity()) {
            return WreckwaterCharacterPlatformTimelineStatus::
                PredictionHorizonExceeded;
        }
        WreckwaterCharacterPlatformPredictionFrame next;
        const WreckwaterCharacterPlatformTimelineStatus status =
            integrateFrame(frames_[frameCount_ - 1u], next);
        if (status
            != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
            return status;
        }
        frames_[frameCount_++] = next;
    }
    return WreckwaterCharacterPlatformTimelineStatus::Accepted;
}

WreckwaterCharacterPlatformTimelineStatus
WreckwaterCharacterPlatformTimeline::replaceCertifiedBase(
    uint64_t snapshotSequence,
    uint64_t certifiedTick,
    std::span<const game::WreckwaterCharacterPlatformSample>
        certifiedPlatforms,
    uint64_t requiredThroughTick) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterPlatformTimelineStatus::
            NotInitialized;
    }
    const auto reject =
        [this](WreckwaterCharacterPlatformTimelineStatus status) {
            incrementSaturated(telemetry_.certifiedBasesRejected);
            if (status
                == WreckwaterCharacterPlatformTimelineStatus::
                    PlatformLifetimeMutation) {
                incrementSaturated(telemetry_.lifetimeMutations);
            } else if (
                status
                == WreckwaterCharacterPlatformTimelineStatus::
                    ImpossibleCorrection) {
                incrementSaturated(
                    telemetry_.impossibleCorrections);
            } else if (
                status
                == WreckwaterCharacterPlatformTimelineStatus::
                    PredictionHorizonExceeded) {
                incrementSaturated(telemetry_.horizonOverflows);
            }
            return status;
        };

    WreckwaterCharacterPlatformPredictionFrame certified;
    WreckwaterCharacterPlatformTimelineStatus status =
        canonicalCertifiedFrame(
            snapshotSequence, certifiedTick,
            certifiedPlatforms, certified);
    if (status
        != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
        return reject(status);
    }
    if (requiredThroughTick < certifiedTick) {
        return reject(
            WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame);
    }
    if (requiredThroughTick - certifiedTick
        > config_.predictionHorizonTicks) {
        return reject(
            WreckwaterCharacterPlatformTimelineStatus::
                PredictionHorizonExceeded);
    }
    if (frameCount_ != 0u) {
        if (snapshotSequence
                <= frames_[0].certifiedSnapshotSequence
            || certifiedTick < frames_[0].tick) {
            return reject(
                WreckwaterCharacterPlatformTimelineStatus::
                    StaleCertifiedSnapshot);
        }
        WreckwaterCharacterPlatformTimeline comparison = *this;
        status = comparison.appendThrough(certifiedTick);
        if (status
            != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
            return reject(status);
        }
        const auto predicted = comparison.frame(certifiedTick);
        if (!predicted) {
            return reject(predicted.status);
        }
        WreckwaterCharacterPlatformPredictionFrame predictedFrame;
        predictedFrame.tick = predicted.tick;
        predictedFrame.certifiedSnapshotSequence =
            predicted.certifiedSnapshotSequence;
        predictedFrame.certifiedBaseTick =
            predicted.certifiedBaseTick;
        predictedFrame.count =
            static_cast<uint32_t>(predicted.platforms.size());
        std::copy(
            predicted.platforms.begin(),
            predicted.platforms.end(),
            predictedFrame.platforms.begin());
        status = validateCorrection(predictedFrame, certified);
        if (status
            != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
            return reject(status);
        }
    }

    WreckwaterCharacterPlatformTimeline replacement;
    replacement.config_ = config_;
    replacement.initialized_ = true;
    replacement.frames_[0] = certified;
    replacement.frameCount_ = 1u;
    status = replacement.appendThrough(requiredThroughTick);
    if (status
        != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
        return reject(status);
    }

    const bool correction = frameCount_ != 0u;
    const uint32_t generated =
        replacement.frameCount_ - 1u;
    frames_ = replacement.frames_;
    frameCount_ = replacement.frameCount_;
    incrementSaturated(telemetry_.certifiedBasesAccepted);
    if (correction) {
        incrementSaturated(telemetry_.correctionsAccepted);
    }
    for (uint32_t index = 0u; index < generated; ++index) {
        incrementSaturated(telemetry_.predictedFramesGenerated);
    }
    telemetry_.frameHighWater =
        std::max(telemetry_.frameHighWater, frameCount_);
    return WreckwaterCharacterPlatformTimelineStatus::Accepted;
}

WreckwaterCharacterPlatformTimelineStatus
WreckwaterCharacterPlatformTimeline::generateThrough(
    uint64_t targetTick) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterPlatformTimelineStatus::
            NotInitialized;
    }
    WreckwaterCharacterPlatformTimeline candidate = *this;
    const uint32_t oldCount = candidate.frameCount_;
    const WreckwaterCharacterPlatformTimelineStatus status =
        candidate.appendThrough(targetTick);
    if (status
        != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
        if (status
            == WreckwaterCharacterPlatformTimelineStatus::
                PredictionHorizonExceeded) {
            incrementSaturated(telemetry_.horizonOverflows);
        }
        return status;
    }
    frames_ = candidate.frames_;
    frameCount_ = candidate.frameCount_;
    for (uint32_t index = oldCount;
         index < frameCount_; ++index) {
        incrementSaturated(telemetry_.predictedFramesGenerated);
    }
    telemetry_.frameHighWater =
        std::max(telemetry_.frameHighWater, frameCount_);
    return WreckwaterCharacterPlatformTimelineStatus::Accepted;
}

WreckwaterCharacterPlatformPredictionFrameResult
WreckwaterCharacterPlatformTimeline::frame(
    uint64_t tick) const noexcept {
    if (!initialized_) {
        return {
            .status =
                WreckwaterCharacterPlatformTimelineStatus::
                    NotInitialized,
        };
    }
    if (frameCount_ == 0u || tick < frames_[0].tick) return {};
    const uint64_t offset = tick - frames_[0].tick;
    if (offset >= frameCount_) return {};
    const auto& selected =
        frames_[static_cast<uint32_t>(offset)];
    return {
        .platforms = selected.samples(),
        .tick = selected.tick,
        .certifiedSnapshotSequence =
            selected.certifiedSnapshotSequence,
        .certifiedBaseTick = selected.certifiedBaseTick,
        .status =
            WreckwaterCharacterPlatformTimelineStatus::Accepted,
    };
}

} // namespace voxy::client
