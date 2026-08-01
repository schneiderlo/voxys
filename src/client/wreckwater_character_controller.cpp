#include "client/wreckwater_character_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace voxy::client {
namespace {

constexpr float kQ15Scale = 1.0f / 32'767.0f;
constexpr uint32_t kClientPlatformHandleBit = 0x8000'0000u;

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

[[nodiscard]] bool finitePositive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

[[nodiscard]] glm::vec3 gameVector(
    const network::WreckwaterVec3& value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] glm::quat gameQuaternion(
    const network::WreckwaterQuaternion& value) noexcept {
    return {value.w, value.x, value.y, value.z};
}

[[nodiscard]] physics::WorldPosition gamePosition(
    const std::array<int32_t, 3>& sector,
    const network::WreckwaterVec3& local) noexcept {
    return {
        .sector = {sector[0], sector[1], sector[2]},
        .local = gameVector(local),
    };
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

[[nodiscard]] bool validIdentity(
    const network::WreckwaterClientReplicationIdentity&
        identity) noexcept {
    return identity.sessionId != 0u
        && identity.matchId != 0u
        && identity.worldId != 0u
        && identity.worldEpoch != 0u
        && identity.authorityEpoch != 0u;
}

[[nodiscard]] bool validExactSample(
    const network::WreckwaterClientSample& sample) noexcept {
    return sample.authoritative.schemaVersion
            == network::kWreckwaterWireSchemaVersion
        && sample.authoritative.flags
            == network::kWreckwaterCertifiedFullSnapshotFlag
        && validIdentity(sample.authoritative.identity)
        && sample.authoritative.snapshotSequence != 0u
        && sample.authoritative.physicsEvidenceTick
            == sample.evaluatedPhysicsTick.whole
        && sample.evaluatedPhysicsTick.fraction == 0.0f
        && !sample.stale
        && !sample.clampedToOldest
        && sample.entityCount
            <= network::kWreckwaterMaximumSnapshotEntities
        && sample.characterCount
            <= network::kWreckwaterMaximumSnapshotCharacters;
}

[[nodiscard]] bool platformLess(
    const game::WreckwaterCharacterPlatformSample& lhs,
    const game::WreckwaterCharacterPlatformSample& rhs) noexcept {
    return std::tuple{
        lhs.skiffId, lhs.skiffGeneration}
        < std::tuple{
        rhs.skiffId, rhs.skiffGeneration};
}

[[nodiscard]] bool clientPlatformHandle(
    game::SkiffId skiffId, uint32_t skiffGeneration,
    physics::BodyHandle& handle) noexcept {
    if (skiffId == 0u
        || (skiffId & kClientPlatformHandleBit) != 0u
        || skiffGeneration == 0u) {
        return false;
    }
    handle = {
        .index = kClientPlatformHandleBit | skiffId,
        .generation = skiffGeneration,
    };
    return true;
}

[[nodiscard]] bool modeFromNetwork(
    const network::WreckwaterCharacterState& source,
    game::WreckwaterCharacterMode& mode) noexcept {
    switch (static_cast<network::WreckwaterCharacterMode>(
        source.stateFlags & 0x3u)) {
        case network::WreckwaterCharacterMode::Airborne:
            mode = game::WreckwaterCharacterMode::Airborne;
            return true;
        case network::WreckwaterCharacterMode::OnSkiff:
            mode = game::WreckwaterCharacterMode::OnSkiff;
            return true;
        case network::WreckwaterCharacterMode::Swimming:
            mode = game::WreckwaterCharacterMode::Swimming;
            return true;
    }
    return false;
}

[[nodiscard]] bool samePlatformBinding(
    const game::WreckwaterCharacterState& lhs,
    const game::WreckwaterCharacterState& rhs) noexcept {
    if (lhs.mode != rhs.mode) return false;
    return lhs.mode != game::WreckwaterCharacterMode::OnSkiff
        || (lhs.skiffId == rhs.skiffId
            && lhs.skiffGeneration == rhs.skiffGeneration);
}

} // namespace

const char* wreckwaterCharacterControllerStatusName(
    WreckwaterCharacterControllerStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterControllerStatus::Accepted:
            return "accepted";
        case WreckwaterCharacterControllerStatus::NotInitialized:
            return "not initialized";
        case WreckwaterCharacterControllerStatus::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterCharacterControllerStatus::InvalidCertifiedSample:
            return "invalid certified sample";
        case WreckwaterCharacterControllerStatus::
                SnapshotIdentityMismatch:
            return "snapshot identity mismatch";
        case WreckwaterCharacterControllerStatus::StaleSnapshot:
            return "stale snapshot";
        case WreckwaterCharacterControllerStatus::CharacterUnavailable:
            return "character unavailable";
        case WreckwaterCharacterControllerStatus::NotBound:
            return "not bound";
        case WreckwaterCharacterControllerStatus::InvalidInput:
            return "invalid input";
        case WreckwaterCharacterControllerStatus::ExpiredInput:
            return "expired input";
        case WreckwaterCharacterControllerStatus::InvalidInputSequence:
            return "invalid input sequence";
        case WreckwaterCharacterControllerStatus::ReplayedInputSequence:
            return "replayed input sequence";
        case WreckwaterCharacterControllerStatus::IgnoredLowerSequence:
            return "ignored lower sequence";
        case WreckwaterCharacterControllerStatus::InputSequenceExhausted:
            return "input sequence exhausted";
        case WreckwaterCharacterControllerStatus::
                SnapshotSequenceExhausted:
            return "snapshot sequence exhausted";
        case WreckwaterCharacterControllerStatus::InputCapacityExceeded:
            return "input capacity exceeded";
        case WreckwaterCharacterControllerStatus::
                NonMonotonicPredictionTick:
            return "non-monotonic prediction tick";
        case WreckwaterCharacterControllerStatus::
                PredictionTickExhausted:
            return "prediction tick exhausted";
        case WreckwaterCharacterControllerStatus::
                PlatformPredictionHorizonExceeded:
            return "platform prediction horizon exceeded";
        case WreckwaterCharacterControllerStatus::
                PlatformPredictionSourceMismatch:
            return "platform prediction source mismatch";
        case WreckwaterCharacterControllerStatus::
                PlatformCapacityExceeded:
            return "platform capacity exceeded";
        case WreckwaterCharacterControllerStatus::
                HistoryCapacityExceeded:
            return "history capacity exceeded";
        case WreckwaterCharacterControllerStatus::HistoryGap:
            return "history gap";
        case WreckwaterCharacterControllerStatus::
                PlatformDiscontinuity:
            return "platform discontinuity";
        case WreckwaterCharacterControllerStatus::SimulationFailed:
            return "simulation failed";
        case WreckwaterCharacterControllerStatus::PositionOverflow:
            return "position overflow";
    }
    return "unknown";
}

bool WreckwaterCharacterController::initialize(
    const Config& config) noexcept {
    initialized_ = false;
    if (config.localPlayerId == 0u
        || config.inputHistoryTicks == 0u
        || config.inputHistoryTicks
            > kWreckwaterCharacterControllerMaximumTicks
        || config.stateHistoryTicks == 0u
        || config.stateHistoryTicks
            > kWreckwaterCharacterControllerMaximumTicks
        || config.platformPredictionTicks == 0u
        || config.platformPredictionTicks
            > kWreckwaterCharacterPlatformTimelineMaximumPredictionTicks
        || !finitePositive(config.correctionHalfLifeSeconds)
        || !finitePositive(config.maximumCorrectionDeltaSeconds)
        || !finiteNonNegative(config.hardSnapDistance)
        || !game::WreckwaterCharacterMovementAuthority::validConfig(
            config.movement)) {
        return false;
    }
    WreckwaterCharacterPlatformTimeline platformTimeline;
    if (!platformTimeline.initialize({
            .predictionHorizonTicks =
                config.platformPredictionTicks,
            .movement = config.movement,
        })) {
        return false;
    }
    config_ = config;
    platformTimeline_ = platformTimeline;
    initialized_ = true;
    reset();
    return true;
}

void WreckwaterCharacterController::reset() noexcept {
    inputs_ = {};
    history_ = {};
    identity_ = {};
    authoritativeState_ = {};
    predictedState_ = {};
    authoritativePlatforms_ = {};
    platformTimeline_.reset();
    renderCorrection_ = glm::dvec3(0.0);
    latestSnapshotSequence_ = 0u;
    authoritativeTick_ = 0u;
    predictedTick_ = 0u;
    inputCount_ = 0u;
    historyCount_ = 0u;
    binding_ =
        WreckwaterCharacterControllerBinding::
            AwaitingCertifiedSnapshot;
    identityPinned_ = false;
    hasAuthoritativeState_ = false;
    hasPredictedState_ = false;
    platformPredictionSource_ =
        PlatformPredictionSource::Unselected;
    lastFailClosedStatus_ =
        WreckwaterCharacterControllerStatus::Accepted;
    telemetry_ = {};
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::platformFrameFromExactSamples(
    uint64_t tick,
    std::span<const game::WreckwaterCharacterPlatformSample> samples,
    PlatformFrame& frame) const noexcept {
    if (samples.size()
        > game::kWreckwaterMaximumCharacterPlatforms) {
        return WreckwaterCharacterControllerStatus::
            PlatformCapacityExceeded;
    }
    PlatformFrame candidate;
    candidate.tick = tick;
    candidate.count = static_cast<uint32_t>(samples.size());
    for (size_t index = 0u; index < samples.size(); ++index) {
        candidate.platforms[index] = samples[index];
        if (!clientPlatformHandle(
                candidate.platforms[index].skiffId,
                candidate.platforms[index].skiffGeneration,
                candidate.platforms[index].body)) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
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
    if (candidate.count == 2u
        && candidate.platforms[0].skiffId
            == candidate.platforms[1].skiffId) {
        return WreckwaterCharacterControllerStatus::
            InvalidCertifiedSample;
    }
    frame = candidate;
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::platformFrameFromCertifiedSample(
    const network::WreckwaterClientSample& sample,
    PlatformFrame& frame) const noexcept {
    std::array<
        game::WreckwaterCharacterPlatformSample,
        game::kWreckwaterMaximumCharacterPlatforms>
        platforms{};
    uint32_t count = 0u;
    for (uint32_t index = 0u;
         index < sample.entityCount; ++index) {
        const network::WreckwaterEntityState& source =
            sample.entities[index].authoritativeState;
        if (!network::isCanonicalWreckwaterEntityState(source)) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        }
        if (source.kind != network::WreckwaterEntityKind::Skiff)
            continue;
        if (count >= platforms.size()
            || source.shape != network::WreckwaterShape::Box
            || !finitePositive(source.dimensions.x)
            || !finitePositive(source.dimensions.y)
            || !finitePositive(source.dimensions.z)) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        }
        physics::BodyHandle body;
        if (!clientPlatformHandle(
                source.skiff.skiffId,
                source.skiff.generation, body)) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        }
        platforms[count++] = {
            .skiffId = source.skiff.skiffId,
            .skiffGeneration = source.skiff.generation,
            .body = body,
            .position = gamePosition(
                source.sector, source.localPosition),
            .orientation = gameQuaternion(source.orientation),
            .linearVelocity = gameVector(source.linearVelocity),
            .angularVelocity = gameVector(source.angularVelocity),
            .deckHalfExtents = {
                source.dimensions.x * 0.5f,
                source.dimensions.z * 0.5f,
            },
            .deckLocalHeight =
                source.dimensions.y * 0.5f,
        };
    }
    const WreckwaterCharacterControllerStatus status =
        platformFrameFromExactSamples(
        sample.authoritative.physicsEvidenceTick,
        std::span(platforms.data(), count), frame);
    if (status == WreckwaterCharacterControllerStatus::Accepted) {
        seedPlatformLifetimes(frame);
    }
    return status;
}

void WreckwaterCharacterController::seedPlatformLifetimes(
    PlatformFrame& frame) const noexcept {
    frame.lifetimes = {};
    for (uint32_t index = 0u; index < frame.count; ++index) {
        frame.lifetimes[index] = {
            .sample = frame.platforms[index],
            .known = true,
            .available = true,
        };
    }
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::controllerStatus(
    WreckwaterCharacterPlatformTimelineStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterPlatformTimelineStatus::Accepted:
            return WreckwaterCharacterControllerStatus::Accepted;
        case WreckwaterCharacterPlatformTimelineStatus::
                NotInitialized:
            return WreckwaterCharacterControllerStatus::NotInitialized;
        case WreckwaterCharacterPlatformTimelineStatus::
                InvalidConfiguration:
            return WreckwaterCharacterControllerStatus::
                InvalidConfiguration;
        case WreckwaterCharacterPlatformTimelineStatus::
                InvalidCertifiedFrame:
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        case WreckwaterCharacterPlatformTimelineStatus::
                StaleCertifiedSnapshot:
            return WreckwaterCharacterControllerStatus::StaleSnapshot;
        case WreckwaterCharacterPlatformTimelineStatus::
                SnapshotSequenceExhausted:
            return WreckwaterCharacterControllerStatus::
                SnapshotSequenceExhausted;
        case WreckwaterCharacterPlatformTimelineStatus::
                TickExhausted:
            return WreckwaterCharacterControllerStatus::
                PredictionTickExhausted;
        case WreckwaterCharacterPlatformTimelineStatus::
                PlatformCapacityExceeded:
            return WreckwaterCharacterControllerStatus::
                PlatformCapacityExceeded;
        case WreckwaterCharacterPlatformTimelineStatus::
                PlatformLifetimeMutation:
        case WreckwaterCharacterPlatformTimelineStatus::
                ImpossibleCorrection:
            return WreckwaterCharacterControllerStatus::
                PlatformDiscontinuity;
        case WreckwaterCharacterPlatformTimelineStatus::
                PredictionHorizonExceeded:
            return WreckwaterCharacterControllerStatus::
                PlatformPredictionHorizonExceeded;
        case WreckwaterCharacterPlatformTimelineStatus::
                FrameUnavailable:
            return WreckwaterCharacterControllerStatus::HistoryGap;
        case WreckwaterCharacterPlatformTimelineStatus::
                PositionOverflow:
            return WreckwaterCharacterControllerStatus::
                PositionOverflow;
    }
    return WreckwaterCharacterControllerStatus::
        InvalidCertifiedSample;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::replacePlatformTimelineBase(
    const network::WreckwaterClientSample& sample,
    const PlatformFrame& platforms,
    uint64_t requiredThroughTick,
    bool permitDiscontinuityReset,
    bool& resetForDiscontinuity) noexcept {
    resetForDiscontinuity = false;
    const auto exactPlatforms = std::span(
        platforms.platforms.data(), platforms.count);
    WreckwaterCharacterPlatformTimelineStatus status =
        platformTimeline_.replaceCertifiedBase(
            sample.authoritative.snapshotSequence,
            sample.authoritative.physicsEvidenceTick,
            exactPlatforms, requiredThroughTick);
    if (status
        == WreckwaterCharacterPlatformTimelineStatus::Accepted) {
        return WreckwaterCharacterControllerStatus::Accepted;
    }
    const bool recoverableDiscontinuity =
        status
            == WreckwaterCharacterPlatformTimelineStatus::
                PlatformLifetimeMutation
        || status
            == WreckwaterCharacterPlatformTimelineStatus::
                ImpossibleCorrection
        || status
            == WreckwaterCharacterPlatformTimelineStatus::
                PredictionHorizonExceeded;
    if (!permitDiscontinuityReset
        || !recoverableDiscontinuity) {
        return controllerStatus(status);
    }

    platformTimeline_.reset();
    status = platformTimeline_.replaceCertifiedBase(
        sample.authoritative.snapshotSequence,
        sample.authoritative.physicsEvidenceTick,
        exactPlatforms,
        sample.authoritative.physicsEvidenceTick);
    if (status
        != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
        return controllerStatus(status);
    }
    resetForDiscontinuity = true;
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::validatePlatformFrameSuccessor(
    const PlatformFrame& previous,
    PlatformFrame& current) const noexcept {
    auto nextLifetimes = previous.lifetimes;
    for (PlatformLifetime& lifetime : nextLifetimes) {
        lifetime.available = false;
    }
    for (uint32_t sampleIndex = 0u;
         sampleIndex < current.count; ++sampleIndex) {
        const auto& sample = current.platforms[sampleIndex];
        size_t lifetimeIndex = nextLifetimes.size();
        for (size_t index = 0u;
             index < nextLifetimes.size(); ++index) {
            if (nextLifetimes[index].known
                && nextLifetimes[index].sample.skiffId
                    == sample.skiffId) {
                lifetimeIndex = index;
                break;
            }
        }
        if (lifetimeIndex == nextLifetimes.size()) {
            for (size_t index = 0u;
                 index < nextLifetimes.size(); ++index) {
                if (!nextLifetimes[index].known) {
                    lifetimeIndex = index;
                    break;
                }
            }
        }
        if (lifetimeIndex == nextLifetimes.size()) {
            return WreckwaterCharacterControllerStatus::
                PlatformCapacityExceeded;
        }
        PlatformLifetime& lifetime =
            nextLifetimes[lifetimeIndex];
        if (lifetime.known) {
            const PlatformLifetime& priorLifetime =
                previous.lifetimes[lifetimeIndex];
            const game::WreckwaterCharacterStatus status =
                game::wreckwaterCharacterPlatformSuccessorStatus(
                    config_.movement, priorLifetime.sample,
                    priorLifetime.available, sample);
            if (status
                != game::WreckwaterCharacterStatus::Accepted) {
                return WreckwaterCharacterControllerStatus::
                    PlatformDiscontinuity;
            }
        }
        lifetime = {
            .sample = sample,
            .known = true,
            .available = true,
        };
    }
    current.lifetimes = nextLifetimes;
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::characterStateFromCertifiedSample(
    const network::WreckwaterClientSample& sample,
    const PlatformFrame& platforms,
    game::WreckwaterCharacterState& state) const noexcept {
    const network::WreckwaterCharacterState* source = nullptr;
    for (uint32_t index = 0u;
         index < sample.characterCount; ++index) {
        const auto& candidate =
            sample.characters[index].authoritativeState;
        if (!network::isCanonicalWreckwaterCharacterState(
                candidate)) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        }
        if (candidate.playerId != config_.localPlayerId) continue;
        if (source != nullptr) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        }
        source = &candidate;
    }
    if (source == nullptr
        || (source->stateFlags
            & network::kWreckwaterCharacterStateActiveFlag)
            == 0u
        || (source->stateFlags
            & network::kWreckwaterCharacterStateConnectedFlag)
            == 0u) {
        return WreckwaterCharacterControllerStatus::
            CharacterUnavailable;
    }

    game::WreckwaterCharacterMode mode;
    if (!modeFromNetwork(*source, mode)
        || source->characterHandle == 0u
        || source->connectionGeneration == 0u) {
        return WreckwaterCharacterControllerStatus::
            InvalidCertifiedSample;
    }

    game::WreckwaterCharacterState candidate;
    candidate.handle = source->characterHandle;
    candidate.playerId = source->playerId;
    candidate.connectionGeneration =
        source->connectionGeneration;
    candidate.mode = mode;
    candidate.feetPosition = gamePosition(
        source->sector, source->localFeetPosition);
    candidate.worldVelocity = gameVector(source->worldVelocity);
    candidate.skiffId = source->skiffId;
    candidate.skiffGeneration = source->skiffGeneration;
    candidate.skiffLocalFeetPosition =
        gameVector(source->skiffLocalFeetPosition);
    candidate.skiffLocalVelocity =
        gameVector(source->skiffLocalVelocity);
    candidate.latestCharacterInputSequence =
        source->lastAppliedCharacterInputSequence;
    candidate.lastAppliedCharacterInputSequence =
        source->lastAppliedCharacterInputSequence;
    candidate.active = true;
    candidate.connected = true;

    if (mode == game::WreckwaterCharacterMode::OnSkiff) {
        bool found = false;
        for (uint32_t index = 0u;
             index < platforms.count; ++index) {
            const auto& platform = platforms.platforms[index];
            if (platform.skiffId == candidate.skiffId
                && platform.skiffGeneration
                    == candidate.skiffGeneration) {
                candidate.skiffBody = platform.body;
                found = true;
                break;
            }
        }
        if (!found) {
            return WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample;
        }
    }

    if (hasAuthoritativeState_
        && authoritativeState_.handle == candidate.handle
        && authoritativeState_.connectionGeneration
            == candidate.connectionGeneration) {
        if (authoritativeState_
                .lastAppliedCharacterInputSequence
            == candidate.lastAppliedCharacterInputSequence) {
            candidate.lastInputTick =
                authoritativeState_.lastInputTick;
        } else {
            for (const InputSlot& slot : inputs_) {
                if (slot.occupied
                    && slot.input.characterInputSequence
                        == candidate
                            .lastAppliedCharacterInputSequence) {
                    candidate.lastInputTick = slot.tick;
                    break;
                }
            }
        }
        if (samePlatformBinding(
                authoritativeState_, candidate)) {
            candidate.lastTransitionTick =
                authoritativeState_.lastTransitionTick;
        } else {
            candidate.lastTransitionTick =
                sample.authoritative.physicsEvidenceTick;
        }
    }
    if (!physics::isValidWorldPosition(candidate.feetPosition)) {
        return WreckwaterCharacterControllerStatus::
            InvalidCertifiedSample;
    }
    state = candidate;
    return WreckwaterCharacterControllerStatus::Accepted;
}

const WreckwaterCharacterController::InputSlot*
WreckwaterCharacterController::inputAt(uint64_t tick) const noexcept {
    if (!initialized_ || config_.inputHistoryTicks == 0u)
        return nullptr;
    const InputSlot& slot =
        inputs_[tick % config_.inputHistoryTicks];
    return slot.occupied && slot.tick == tick ? &slot : nullptr;
}

WreckwaterCharacterController::InputSlot*
WreckwaterCharacterController::inputAt(uint64_t tick) noexcept {
    return const_cast<InputSlot*>(
        std::as_const(*this).inputAt(tick));
}

const WreckwaterCharacterController::HistorySlot*
WreckwaterCharacterController::historyAt(uint64_t tick) const noexcept {
    if (!initialized_ || config_.stateHistoryTicks == 0u)
        return nullptr;
    const HistorySlot& slot =
        history_[tick % config_.stateHistoryTicks];
    return slot.occupied && slot.tick == tick ? &slot : nullptr;
}

WreckwaterCharacterController::HistorySlot*
WreckwaterCharacterController::historyAt(uint64_t tick) noexcept {
    return const_cast<HistorySlot*>(
        std::as_const(*this).historyAt(tick));
}

void WreckwaterCharacterController::clearInputs() noexcept {
    inputs_ = {};
    inputCount_ = 0u;
}

void WreckwaterCharacterController::clearHistory() noexcept {
    history_ = {};
    historyCount_ = 0u;
}

void WreckwaterCharacterController::hardPurgeAndSnap(
    bool countHardSnap) noexcept {
    clearInputs();
    clearHistory();
    renderCorrection_ = glm::dvec3(0.0);
    if (hasAuthoritativeState_) {
        predictedState_ = authoritativeState_;
        predictedTick_ = authoritativeTick_;
        hasPredictedState_ = true;
    } else {
        predictedState_ = {};
        predictedTick_ = 0u;
        hasPredictedState_ = false;
    }
    platformPredictionSource_ =
        PlatformPredictionSource::Unselected;
    incrementSaturated(telemetry_.purges);
    if (countHardSnap) incrementSaturated(telemetry_.hardSnaps);
}

void WreckwaterCharacterController::failClosed(
    WreckwaterCharacterControllerStatus status) noexcept {
    lastFailClosedStatus_ = status;
    if (status
        == WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity) {
        incrementSaturated(
            telemetry_.platformDiscontinuities);
    }
    hardPurgeAndSnap(true);
}

void WreckwaterCharacterController::pruneInputs(
    uint64_t acknowledgedSequence,
    uint64_t coveredTick) noexcept {
    for (InputSlot& slot : inputs_) {
        if (!slot.occupied) continue;
        if (slot.input.characterInputSequence
            <= acknowledgedSequence) {
            slot = {};
            --inputCount_;
            incrementSaturated(
                telemetry_.acknowledgedInputsPruned);
        } else if (slot.tick <= coveredTick) {
            slot = {};
            --inputCount_;
            incrementSaturated(telemetry_.expiredInputs);
        }
    }
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::simulateTick(
    uint64_t tick,
    const PlatformFrame& previousPlatforms,
    const PlatformFrame& currentPlatforms,
    bool replaying,
    game::WreckwaterCharacterState& state) noexcept {
    game::WreckwaterCharacterInput input;
    input.targetTick = tick;
    input.character = state.handle;
    input.playerId = state.playerId;
    input.connectionGeneration = state.connectionGeneration;
    const InputSlot* recorded = inputAt(tick);
    if (recorded != nullptr) {
        input = recorded->input;
        input.character = state.handle;
        input.playerId = state.playerId;
        input.connectionGeneration = state.connectionGeneration;
        state.latestCharacterInputSequence = std::max(
            state.latestCharacterInputSequence,
            input.characterInputSequence);
    }

    const game::WreckwaterCharacterStepResult stepped =
        game::wreckwaterCharacterStep({
            .config = config_.movement,
            .priorState = state,
            .input = input,
            .previousPlatforms = {
                previousPlatforms.platforms.data(),
                previousPlatforms.count,
            },
            .currentPlatforms = {
                currentPlatforms.platforms.data(),
                currentPlatforms.count,
            },
            .tick = tick,
        });
    if (!stepped) {
        return WreckwaterCharacterControllerStatus::
            SimulationFailed;
    }
    state = stepped.nextState;
    if (replaying) {
        incrementSaturated(telemetry_.replayedTicks);
        if (recorded != nullptr) {
            incrementSaturated(telemetry_.replayedInputs);
        } else {
            incrementSaturated(telemetry_.neutralReplayTicks);
        }
    } else {
        incrementSaturated(telemetry_.predictedTicks);
        if (recorded == nullptr) {
            incrementSaturated(telemetry_.neutralPredictionTicks);
        }
    }
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::replayTo(
    uint64_t targetTick,
    bool replaceFuturePlatformFrames) noexcept {
    if (!hasAuthoritativeState_
        || targetTick < authoritativeTick_
        || targetTick - authoritativeTick_
            > config_.stateHistoryTicks) {
        return WreckwaterCharacterControllerStatus::HistoryGap;
    }
    for (HistorySlot& slot : history_) {
        if (slot.occupied && slot.tick <= authoritativeTick_) {
            slot = {};
            --historyCount_;
        }
    }

    game::WreckwaterCharacterState state = authoritativeState_;
    PlatformFrame previous = authoritativePlatforms_;
    const uint64_t replayCount =
        targetTick - authoritativeTick_;
    for (uint64_t offset = 1u;
         offset <= replayCount; ++offset) {
        const uint64_t tick = authoritativeTick_ + offset;
        HistorySlot* slot = historyAt(tick);
        if (slot == nullptr) {
            return WreckwaterCharacterControllerStatus::HistoryGap;
        }
        PlatformFrame current;
        if (replaceFuturePlatformFrames) {
            const auto predictedPlatformFrame =
                platformTimeline_.frame(tick);
            if (!predictedPlatformFrame) {
                return controllerStatus(
                    predictedPlatformFrame.status);
            }
            const WreckwaterCharacterControllerStatus converted =
                platformFrameFromExactSamples(
                    tick,
                    predictedPlatformFrame.platforms,
                    current);
            if (converted
                != WreckwaterCharacterControllerStatus::Accepted) {
                return converted;
            }
        } else {
            current = slot->platforms;
        }
        WreckwaterCharacterControllerStatus simulated =
            validatePlatformFrameSuccessor(previous, current);
        if (simulated
            != WreckwaterCharacterControllerStatus::Accepted) {
            return simulated;
        }
        simulated = simulateTick(
            tick, previous, current, true, state);
        if (simulated
            != WreckwaterCharacterControllerStatus::Accepted) {
            return simulated;
        }
        slot->state = state;
        slot->platforms = current;
        previous = current;
    }
    predictedState_ = state;
    predictedTick_ = targetTick;
    hasPredictedState_ = true;
    return WreckwaterCharacterControllerStatus::Accepted;
}

glm::dvec3 WreckwaterCharacterController::renderedAbsolute()
    const noexcept {
    if (!hasPredictedState_) return glm::dvec3(0.0);
    return physics::worldPositionToAbsolute(
        predictedState_.feetPosition) + renderCorrection_;
}

void WreckwaterCharacterController::
updateCorrectionAfterReconciliation(
    const glm::dvec3& oldRenderedAbsolute) noexcept {
    if (!hasPredictedState_) {
        renderCorrection_ = glm::dvec3(0.0);
        return;
    }
    const glm::dvec3 predictedAbsolute =
        physics::worldPositionToAbsolute(
            predictedState_.feetPosition);
    const glm::dvec3 correction =
        oldRenderedAbsolute - predictedAbsolute;
    const double distance = glm::length(correction);
    if (!std::isfinite(distance)
        || distance
            > static_cast<double>(config_.hardSnapDistance)) {
        renderCorrection_ = glm::dvec3(0.0);
        incrementSaturated(telemetry_.hardSnaps);
        return;
    }
    renderCorrection_ = correction;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::receiveLatestCertifiedSample(
    const network::WreckwaterClientSample& sample) noexcept {
    const auto reject =
        [this](WreckwaterCharacterControllerStatus status) {
            incrementSaturated(
                telemetry_.certifiedSamplesRejected);
            return status;
        };
    if (!initialized_) {
        return WreckwaterCharacterControllerStatus::NotInitialized;
    }
    if (!validExactSample(sample)) {
        return reject(
            WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample);
    }
    if (identityPinned_
        && sample.authoritative.identity != identity_) {
        return reject(
            WreckwaterCharacterControllerStatus::
                SnapshotIdentityMismatch);
    }
    if (identityPinned_
        && (sample.authoritative.snapshotSequence
                <= latestSnapshotSequence_
            || (hasAuthoritativeState_
                && sample.authoritative.physicsEvidenceTick
                    < authoritativeTick_))) {
        return reject(
            WreckwaterCharacterControllerStatus::StaleSnapshot);
    }

    PlatformFrame platforms;
    WreckwaterCharacterControllerStatus converted =
        platformFrameFromCertifiedSample(sample, platforms);
    if (converted != WreckwaterCharacterControllerStatus::Accepted) {
        return reject(converted);
    }
    game::WreckwaterCharacterState state;
    converted = characterStateFromCertifiedSample(
        sample, platforms, state);
    if (converted
        == WreckwaterCharacterControllerStatus::
            CharacterUnavailable) {
        identity_ = sample.authoritative.identity;
        identityPinned_ = true;
        latestSnapshotSequence_ =
            sample.authoritative.snapshotSequence;
        hasAuthoritativeState_ = false;
        binding_ =
            WreckwaterCharacterControllerBinding::
                AwaitingCertifiedSnapshot;
        platformTimeline_.reset();
        hardPurgeAndSnap(hasPredictedState_);
        incrementSaturated(telemetry_.certifiedSamplesAccepted);
        return converted;
    }
    if (converted != WreckwaterCharacterControllerStatus::Accepted) {
        return reject(converted);
    }

    const bool wasBound =
        binding_ == WreckwaterCharacterControllerBinding::Ready
        && hasAuthoritativeState_ && hasPredictedState_;
    const bool sameGeneration =
        wasBound
        && state.handle == authoritativeState_.handle
        && state.playerId == authoritativeState_.playerId
        && state.connectionGeneration
            == authoritativeState_.connectionGeneration;
    if (sameGeneration
        && state.lastAppliedCharacterInputSequence
            < authoritativeState_
                  .lastAppliedCharacterInputSequence) {
        return reject(
            WreckwaterCharacterControllerStatus::
                InvalidCertifiedSample);
    }
    const bool discontinuity =
        wasBound
        && (!sameGeneration
            || !samePlatformBinding(
                authoritativeState_, state));
    const glm::dvec3 oldRenderedAbsolute =
        wasBound ? renderedAbsolute() : glm::dvec3(0.0);
    const uint64_t oldPredictedTick =
        wasBound ? predictedTick_ :
        sample.authoritative.physicsEvidenceTick;
    const bool externalExactFrameRewind =
        wasBound && !discontinuity
        && platformPredictionSource_
            == PlatformPredictionSource::ExternalExactFrames
        && oldPredictedTick
            > sample.authoritative.physicsEvidenceTick;
    bool platformTimelineReset = false;
    if (!wasBound || discontinuity) {
        platformTimeline_.reset();
    }
    const bool regenerateFuturePlatforms =
        wasBound && !discontinuity
        && !externalExactFrameRewind;
    const uint64_t requiredPlatformTick =
        regenerateFuturePlatforms
            ? std::max(
                oldPredictedTick,
                sample.authoritative.physicsEvidenceTick)
            : sample.authoritative.physicsEvidenceTick;
    converted = replacePlatformTimelineBase(
        sample, platforms, requiredPlatformTick,
        wasBound && !discontinuity,
        platformTimelineReset);
    if (converted != WreckwaterCharacterControllerStatus::Accepted) {
        if (wasBound) failClosed(converted);
        return reject(converted);
    }

    identity_ = sample.authoritative.identity;
    identityPinned_ = true;
    latestSnapshotSequence_ =
        sample.authoritative.snapshotSequence;
    authoritativeState_ = state;
    authoritativePlatforms_ = platforms;
    authoritativeTick_ =
        sample.authoritative.physicsEvidenceTick;
    hasAuthoritativeState_ = true;
    binding_ = WreckwaterCharacterControllerBinding::Ready;

    if (!wasBound) {
        clearInputs();
        clearHistory();
        predictedState_ = authoritativeState_;
        predictedTick_ = authoritativeTick_;
        hasPredictedState_ = true;
        renderCorrection_ = glm::dvec3(0.0);
        incrementSaturated(telemetry_.binds);
        incrementSaturated(telemetry_.certifiedSamplesAccepted);
        return WreckwaterCharacterControllerStatus::Accepted;
    }
    if (discontinuity || platformTimelineReset
        || externalExactFrameRewind) {
        if (externalExactFrameRewind) {
            incrementSaturated(telemetry_.rewinds);
        }
        hardPurgeAndSnap(true);
        if (platformTimelineReset) {
            incrementSaturated(
                telemetry_.platformDiscontinuities);
        }
        if (!sameGeneration) {
            incrementSaturated(telemetry_.binds);
        }
        incrementSaturated(telemetry_.certifiedSamplesAccepted);
        return WreckwaterCharacterControllerStatus::Accepted;
    }

    pruneInputs(
        authoritativeState_
            .lastAppliedCharacterInputSequence,
        authoritativeTick_);
    predictedState_ = authoritativeState_;
    predictedTick_ = authoritativeTick_;
    hasPredictedState_ = true;
    if (oldPredictedTick > authoritativeTick_) {
        incrementSaturated(telemetry_.rewinds);
        converted = replayTo(oldPredictedTick, true);
        if (converted
            != WreckwaterCharacterControllerStatus::Accepted) {
            failClosed(converted);
            incrementSaturated(
                telemetry_.certifiedSamplesAccepted);
            return converted;
        }
    } else {
        clearHistory();
        platformPredictionSource_ =
            PlatformPredictionSource::Unselected;
    }
    updateCorrectionAfterReconciliation(oldRenderedAbsolute);
    incrementSaturated(telemetry_.certifiedSamplesAccepted);
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::recordSuccessfullySentInput(
    uint64_t targetTick,
    uint64_t characterInputSequence,
    int16_t moveXQ15,
    int16_t moveZQ15,
    bool jump,
    bool board) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterControllerStatus::NotInitialized;
    }
    if (binding_ != WreckwaterCharacterControllerBinding::Ready
        || !hasAuthoritativeState_ || !hasPredictedState_) {
        return WreckwaterCharacterControllerStatus::NotBound;
    }
    if (targetTick == 0u || characterInputSequence == 0u) {
        return WreckwaterCharacterControllerStatus::InvalidInput;
    }
    if (characterInputSequence
        == std::numeric_limits<uint64_t>::max()) {
        incrementSaturated(telemetry_.inputOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                InputSequenceExhausted);
        return WreckwaterCharacterControllerStatus::
            InputSequenceExhausted;
    }
    if (targetTick <= authoritativeTick_) {
        incrementSaturated(telemetry_.expiredInputs);
        return WreckwaterCharacterControllerStatus::ExpiredInput;
    }
    if (characterInputSequence
        <= authoritativeState_
               .lastAppliedCharacterInputSequence) {
        return WreckwaterCharacterControllerStatus::
            ReplayedInputSequence;
    }
    if (targetTick - authoritativeTick_
        > config_.inputHistoryTicks) {
        incrementSaturated(telemetry_.inputOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                InputCapacityExceeded);
        return WreckwaterCharacterControllerStatus::
            InputCapacityExceeded;
    }

    InputSlot& destination =
        inputs_[targetTick % config_.inputHistoryTicks];
    if (destination.occupied
        && destination.tick != targetTick) {
        incrementSaturated(telemetry_.inputOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                InputCapacityExceeded);
        return WreckwaterCharacterControllerStatus::
            InputCapacityExceeded;
    }
    if (destination.occupied) {
        if (characterInputSequence
            == destination.input.characterInputSequence) {
            return WreckwaterCharacterControllerStatus::
                ReplayedInputSequence;
        }
        if (characterInputSequence
            < destination.input.characterInputSequence) {
            return WreckwaterCharacterControllerStatus::
                IgnoredLowerSequence;
        }
    }

    uint64_t predecessorTick = authoritativeTick_;
    uint64_t predecessorSequence =
        authoritativeState_
            .lastAppliedCharacterInputSequence;
    uint64_t successorTick = 0u;
    uint64_t successorSequence = 0u;
    bool successorFound = false;
    for (const InputSlot& slot : inputs_) {
        if (!slot.occupied || slot.tick == targetTick) continue;
        if (slot.input.characterInputSequence
            == characterInputSequence) {
            return WreckwaterCharacterControllerStatus::
                ReplayedInputSequence;
        }
        if (slot.tick < targetTick
            && slot.tick > predecessorTick) {
            predecessorTick = slot.tick;
            predecessorSequence =
                slot.input.characterInputSequence;
        } else if (
            slot.tick > targetTick
            && (!successorFound || slot.tick < successorTick)) {
            successorTick = slot.tick;
            successorSequence =
                slot.input.characterInputSequence;
            successorFound = true;
        }
    }
    const uint64_t maximumAdvance =
        config_.movement.maximumInputSequenceAdvance;
    if (characterInputSequence <= predecessorSequence
        || characterInputSequence - predecessorSequence
            > maximumAdvance
        || (successorFound
            && (successorSequence <= characterInputSequence
                || successorSequence - characterInputSequence
                    > maximumAdvance))) {
        return WreckwaterCharacterControllerStatus::
            InvalidInputSequence;
    }

    glm::vec2 move(
        static_cast<float>(moveXQ15) * kQ15Scale,
        static_cast<float>(moveZQ15) * kQ15Scale);
    const float lengthSquared = glm::dot(move, move);
    if (!std::isfinite(lengthSquared)) {
        return WreckwaterCharacterControllerStatus::InvalidInput;
    }
    if (lengthSquared > 1.0f) {
        move *= 1.0f / std::sqrt(lengthSquared);
    }
    if (move.x == 0.0f) move.x = 0.0f;
    if (move.y == 0.0f) move.y = 0.0f;

    const bool replaced = destination.occupied;
    destination = {
        .input = {
            .targetTick = targetTick,
            .characterInputSequence =
                characterInputSequence,
            .character = authoritativeState_.handle,
            .playerId = authoritativeState_.playerId,
            .connectionGeneration =
                authoritativeState_.connectionGeneration,
            .move = move,
            .jump = jump,
            .board = board,
        },
        .tick = targetTick,
        .occupied = true,
    };
    if (replaced) {
        incrementSaturated(
            telemetry_.sameTickInputsSuperseded);
    } else {
        ++inputCount_;
    }
    incrementSaturated(telemetry_.inputsRecorded);
    telemetry_.inputHighWater =
        std::max(telemetry_.inputHighWater, inputCount_);

    if (targetTick <= predictedTick_) {
        const glm::dvec3 oldRenderedAbsolute =
            renderedAbsolute();
        incrementSaturated(telemetry_.rewinds);
        const WreckwaterCharacterControllerStatus replayed =
            replayTo(predictedTick_);
        if (replayed
            != WreckwaterCharacterControllerStatus::Accepted) {
            failClosed(replayed);
            return replayed;
        }
        updateCorrectionAfterReconciliation(
            oldRenderedAbsolute);
    }
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::advancePredictionWithPlatforms(
    uint64_t tick,
    std::span<const game::WreckwaterCharacterPlatformSample>
        exactPlatforms) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterControllerStatus::NotInitialized;
    }
    if (binding_ != WreckwaterCharacterControllerBinding::Ready
        || !hasPredictedState_ || !hasAuthoritativeState_) {
        return WreckwaterCharacterControllerStatus::NotBound;
    }
    if (predictedTick_ == std::numeric_limits<uint64_t>::max()) {
        incrementSaturated(telemetry_.historyOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                PredictionTickExhausted);
        return WreckwaterCharacterControllerStatus::
            PredictionTickExhausted;
    }
    if (tick != predictedTick_ + 1u) {
        return WreckwaterCharacterControllerStatus::
            NonMonotonicPredictionTick;
    }
    if (tick - authoritativeTick_
        > config_.stateHistoryTicks) {
        incrementSaturated(telemetry_.historyOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                HistoryCapacityExceeded);
        return WreckwaterCharacterControllerStatus::
            HistoryCapacityExceeded;
    }

    PlatformFrame current;
    WreckwaterCharacterControllerStatus status =
        platformFrameFromExactSamples(
            tick, exactPlatforms, current);
    if (status != WreckwaterCharacterControllerStatus::Accepted) {
        return status;
    }
    HistorySlot& destination =
        history_[tick % config_.stateHistoryTicks];
    if (destination.occupied
        && destination.tick != tick) {
        incrementSaturated(telemetry_.historyOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                HistoryCapacityExceeded);
        return WreckwaterCharacterControllerStatus::
            HistoryCapacityExceeded;
    }

    const PlatformFrame* previous = nullptr;
    if (predictedTick_ == authoritativeTick_) {
        previous = &authoritativePlatforms_;
    } else {
        const HistorySlot* prior = historyAt(predictedTick_);
        if (prior == nullptr) {
            failClosed(
                WreckwaterCharacterControllerStatus::HistoryGap);
            return WreckwaterCharacterControllerStatus::HistoryGap;
        }
        previous = &prior->platforms;
    }
    status = validatePlatformFrameSuccessor(*previous, current);
    if (status != WreckwaterCharacterControllerStatus::Accepted) {
        failClosed(status);
        return status;
    }
    game::WreckwaterCharacterState next = predictedState_;
    status = simulateTick(
        tick, *previous, current, false, next);
    if (status != WreckwaterCharacterControllerStatus::Accepted) {
        failClosed(status);
        return status;
    }

    const bool newSlot = !destination.occupied;
    destination = {
        .state = next,
        .platforms = current,
        .tick = tick,
        .occupied = true,
    };
    if (newSlot) ++historyCount_;
    telemetry_.historyHighWater =
        std::max(telemetry_.historyHighWater, historyCount_);
    predictedState_ = next;
    predictedTick_ = tick;
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::advancePrediction(
    uint64_t tick,
    std::span<const game::WreckwaterCharacterPlatformSample>
        exactPlatforms) noexcept {
    if (platformPredictionSource_
        == PlatformPredictionSource::CertifiedTimeline) {
        failClosed(
            WreckwaterCharacterControllerStatus::
                PlatformPredictionSourceMismatch);
        return WreckwaterCharacterControllerStatus::
            PlatformPredictionSourceMismatch;
    }
    const WreckwaterCharacterControllerStatus status =
        advancePredictionWithPlatforms(tick, exactPlatforms);
    if (status == WreckwaterCharacterControllerStatus::Accepted) {
        platformPredictionSource_ =
            PlatformPredictionSource::ExternalExactFrames;
    }
    return status;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::advancePrediction(
    uint64_t tick) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterControllerStatus::NotInitialized;
    }
    if (binding_ != WreckwaterCharacterControllerBinding::Ready
        || !hasPredictedState_ || !hasAuthoritativeState_) {
        return WreckwaterCharacterControllerStatus::NotBound;
    }
    if (platformPredictionSource_
        == PlatformPredictionSource::ExternalExactFrames) {
        failClosed(
            WreckwaterCharacterControllerStatus::
                PlatformPredictionSourceMismatch);
        return WreckwaterCharacterControllerStatus::
            PlatformPredictionSourceMismatch;
    }
    if (predictedTick_ == std::numeric_limits<uint64_t>::max()) {
        incrementSaturated(telemetry_.historyOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                PredictionTickExhausted);
        return WreckwaterCharacterControllerStatus::
            PredictionTickExhausted;
    }
    if (tick != predictedTick_ + 1u) {
        return WreckwaterCharacterControllerStatus::
            NonMonotonicPredictionTick;
    }
    if (tick - authoritativeTick_
        > config_.stateHistoryTicks) {
        incrementSaturated(telemetry_.historyOverflows);
        failClosed(
            WreckwaterCharacterControllerStatus::
                HistoryCapacityExceeded);
        return WreckwaterCharacterControllerStatus::
            HistoryCapacityExceeded;
    }

    const WreckwaterCharacterPlatformTimelineStatus generated =
        platformTimeline_.generateThrough(tick);
    if (generated
        != WreckwaterCharacterPlatformTimelineStatus::Accepted) {
        const WreckwaterCharacterControllerStatus status =
            controllerStatus(generated);
        if (status
            == WreckwaterCharacterControllerStatus::
                PlatformPredictionHorizonExceeded) {
            incrementSaturated(telemetry_.historyOverflows);
        }
        failClosed(status);
        return status;
    }
    const auto frame = platformTimeline_.frame(tick);
    if (!frame) {
        const WreckwaterCharacterControllerStatus status =
            controllerStatus(frame.status);
        failClosed(status);
        return status;
    }
    const WreckwaterCharacterControllerStatus status =
        advancePredictionWithPlatforms(tick, frame.platforms);
    if (status == WreckwaterCharacterControllerStatus::Accepted) {
        platformPredictionSource_ =
            PlatformPredictionSource::CertifiedTimeline;
    }
    return status;
}

WreckwaterCharacterControllerStatus
WreckwaterCharacterController::advanceRenderCorrection(
    float deltaSeconds) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterControllerStatus::NotInitialized;
    }
    if (!hasPredictedState_) {
        return WreckwaterCharacterControllerStatus::NotBound;
    }
    if (!finiteNonNegative(deltaSeconds)) {
        return WreckwaterCharacterControllerStatus::InvalidInput;
    }
    const float boundedDelta = std::min(
        deltaSeconds, config_.maximumCorrectionDeltaSeconds);
    const double decay = std::exp2(
        -static_cast<double>(boundedDelta)
        / static_cast<double>(
            config_.correctionHalfLifeSeconds));
    renderCorrection_ *= decay;
    if (glm::dot(renderCorrection_, renderCorrection_)
        < 1.0e-12) {
        renderCorrection_ = glm::dvec3(0.0);
    }
    return WreckwaterCharacterControllerStatus::Accepted;
}

WreckwaterCharacterControllerPose
WreckwaterCharacterController::poseOf(
    const game::WreckwaterCharacterState& state,
    uint64_t tick) const noexcept {
    return {
        .tick = tick,
        .feetPosition = state.feetPosition,
        .worldVelocity = state.worldVelocity,
        .mode = state.mode,
        .skiffId = state.skiffId,
        .skiffGeneration = state.skiffGeneration,
    };
}

WreckwaterCharacterControllerPoseResult
WreckwaterCharacterController::authoritativePose() const noexcept {
    if (!initialized_) {
        return {
            .status =
                WreckwaterCharacterControllerStatus::NotInitialized,
        };
    }
    if (!hasAuthoritativeState_) return {};
    return {
        .pose = poseOf(
            authoritativeState_, authoritativeTick_),
        .status = WreckwaterCharacterControllerStatus::Accepted,
    };
}

WreckwaterCharacterControllerPoseResult
WreckwaterCharacterController::predictedPose() const noexcept {
    if (!initialized_) {
        return {
            .status =
                WreckwaterCharacterControllerStatus::NotInitialized,
        };
    }
    if (!hasPredictedState_) return {};
    return {
        .pose = poseOf(predictedState_, predictedTick_),
        .status = WreckwaterCharacterControllerStatus::Accepted,
    };
}

WreckwaterCharacterControllerPoseResult
WreckwaterCharacterController::renderPose() const noexcept {
    if (!initialized_) {
        return {
            .status =
                WreckwaterCharacterControllerStatus::NotInitialized,
        };
    }
    if (!hasPredictedState_) return {};
    WreckwaterCharacterControllerPose result =
        poseOf(predictedState_, predictedTick_);
    if (!worldPositionFromAbsoluteChecked(
            renderedAbsolute(), result.feetPosition)) {
        return {
            .status =
                WreckwaterCharacterControllerStatus::
                    PositionOverflow,
        };
    }
    return {
        .pose = result,
        .status = WreckwaterCharacterControllerStatus::Accepted,
    };
}

} // namespace voxy::client
