#pragma once

#include "client/wreckwater_character_platform_timeline.hpp"
#include "game/wreckwater_character_step.hpp"
#include "network/wreckwater_client_replication.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/vec3.hpp>

namespace voxy::client {

inline constexpr uint32_t kWreckwaterCharacterControllerMaximumTicks =
    128u;

enum class WreckwaterCharacterControllerStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidCertifiedSample,
    SnapshotIdentityMismatch,
    StaleSnapshot,
    CharacterUnavailable,
    NotBound,
    InvalidInput,
    ExpiredInput,
    InvalidInputSequence,
    ReplayedInputSequence,
    IgnoredLowerSequence,
    InputSequenceExhausted,
    InputCapacityExceeded,
    NonMonotonicPredictionTick,
    PredictionTickExhausted,
    PlatformCapacityExceeded,
    HistoryCapacityExceeded,
    HistoryGap,
    PlatformDiscontinuity,
    SimulationFailed,
    PositionOverflow,
    SnapshotSequenceExhausted,
    PlatformPredictionHorizonExceeded,
    PlatformPredictionSourceMismatch,
};

[[nodiscard]] const char* wreckwaterCharacterControllerStatusName(
    WreckwaterCharacterControllerStatus status) noexcept;

enum class WreckwaterCharacterControllerBinding : uint32_t {
    AwaitingCertifiedSnapshot = 0u,
    Ready,
};

struct WreckwaterCharacterControllerPose {
    uint64_t tick = 0u;
    physics::WorldPosition feetPosition{};
    glm::vec3 worldVelocity{0.0f};
    game::WreckwaterCharacterMode mode =
        game::WreckwaterCharacterMode::Airborne;
    game::SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterControllerPose&) const = default;
};

struct WreckwaterCharacterControllerPoseResult {
    WreckwaterCharacterControllerPose pose{};
    WreckwaterCharacterControllerStatus status =
        WreckwaterCharacterControllerStatus::NotBound;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterCharacterControllerStatus::Accepted;
    }
};

struct WreckwaterCharacterControllerTelemetry {
    uint64_t certifiedSamplesAccepted = 0u;
    uint64_t certifiedSamplesRejected = 0u;
    uint64_t binds = 0u;
    uint64_t purges = 0u;
    uint64_t rewinds = 0u;
    uint64_t replayedTicks = 0u;
    uint64_t replayedInputs = 0u;
    uint64_t neutralReplayTicks = 0u;
    uint64_t predictedTicks = 0u;
    uint64_t neutralPredictionTicks = 0u;
    uint64_t inputsRecorded = 0u;
    uint64_t sameTickInputsSuperseded = 0u;
    uint64_t acknowledgedInputsPruned = 0u;
    uint64_t expiredInputs = 0u;
    uint64_t hardSnaps = 0u;
    uint64_t inputOverflows = 0u;
    uint64_t historyOverflows = 0u;
    uint64_t platformDiscontinuities = 0u;
    uint32_t inputHighWater = 0u;
    uint32_t historyHighWater = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterControllerTelemetry&) const = default;
};

// Client-only prediction/reconciliation. Platform body handles inside this
// class are synthetic high-bit-tagged identities derived from logical skiff
// identity. They must never be serialized or described as authority handles.
class WreckwaterCharacterController {
public:
    struct Config {
        uint64_t localPlayerId = 0u;
        uint32_t inputHistoryTicks =
            kWreckwaterCharacterControllerMaximumTicks;
        uint32_t stateHistoryTicks =
            kWreckwaterCharacterControllerMaximumTicks;
        uint32_t platformPredictionTicks =
            kWreckwaterCharacterPlatformTimelineDefaultPredictionTicks;
        float correctionHalfLifeSeconds = 0.10f;
        float maximumCorrectionDeltaSeconds = 0.25f;
        float hardSnapDistance = 2.0f;
        game::WreckwaterCharacterMovementAuthority::Config movement{};
    };

    [[nodiscard]] bool initialize(const Config& config) noexcept;
    void reset() noexcept;

    // Pass only the latest exact sample returned by an already-accepting
    // WreckwaterClientSnapshotBuffer. Interpolated or stale samples fail.
    [[nodiscard]] WreckwaterCharacterControllerStatus
    receiveLatestCertifiedSample(
        const network::WreckwaterClientSample& sample) noexcept;

    // Call only after sendLocalCharacterInput() succeeds. The sequence is the
    // returned characterInputSequence, not the outer packet sequence.
    [[nodiscard]] WreckwaterCharacterControllerStatus
    recordSuccessfullySentInput(
        uint64_t targetTick,
        uint64_t characterInputSequence,
        int16_t moveXQ15,
        int16_t moveZQ15,
        bool jump,
        bool board) noexcept;

    // Legacy/tooling path. Samples must describe the exact current-tick skiff
    // frame. Incoming body handles are ignored and replaced by stable
    // client-only handles. Do not mix this with the certified-timeline
    // overload inside one speculative run; mixing fails closed.
    [[nodiscard]] WreckwaterCharacterControllerStatus
    advancePrediction(
        uint64_t tick,
        std::span<const game::WreckwaterCharacterPlatformSample>
            exactPlatforms) noexcept;

    // Normal product path. The exact 60 Hz frame is generated from the latest
    // certified skiff base. It never consumes an interpolated render pose.
    [[nodiscard]] WreckwaterCharacterControllerStatus
    advancePrediction(uint64_t tick) noexcept;

    // Presentation correction is independent from prediction/gameplay state.
    [[nodiscard]] WreckwaterCharacterControllerStatus
    advanceRenderCorrection(float deltaSeconds) noexcept;

    [[nodiscard]] WreckwaterCharacterControllerPoseResult
    authoritativePose() const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerPoseResult
    predictedPose() const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerPoseResult
    renderPose() const noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] WreckwaterCharacterControllerBinding binding()
        const noexcept {
        return binding_;
    }
    [[nodiscard]] uint32_t characterHandle() const noexcept {
        return binding_
                == WreckwaterCharacterControllerBinding::Ready
            ? authoritativeState_.handle
            : 0u;
    }
    [[nodiscard]] uint64_t playerId() const noexcept {
        return binding_
                == WreckwaterCharacterControllerBinding::Ready
            ? authoritativeState_.playerId
            : 0u;
    }
    [[nodiscard]] uint32_t connectionGeneration() const noexcept {
        return binding_
                == WreckwaterCharacterControllerBinding::Ready
            ? authoritativeState_.connectionGeneration
            : 0u;
    }
    [[nodiscard]] const
        network::WreckwaterClientReplicationIdentity&
    replicationIdentity() const noexcept {
        return identity_;
    }
    [[nodiscard]] uint64_t authoritativeTick() const noexcept {
        return hasAuthoritativeState_ ? authoritativeTick_ : 0u;
    }
    [[nodiscard]] uint64_t predictedTick() const noexcept {
        return hasPredictedState_ ? predictedTick_ : 0u;
    }
    [[nodiscard]] uint64_t lastAppliedCharacterInputSequence()
        const noexcept {
        return hasPredictedState_
            ? predictedState_.lastAppliedCharacterInputSequence
            : 0u;
    }
    [[nodiscard]] uint32_t bufferedInputCount() const noexcept {
        return inputCount_;
    }
    [[nodiscard]] uint32_t historyCount() const noexcept {
        return historyCount_;
    }
    [[nodiscard]] const WreckwaterCharacterControllerTelemetry&
    telemetry() const noexcept {
        return telemetry_;
    }
    // Diagnostic view of the certified timeline only. Legacy exact frames are
    // intentionally not mirrored into it.
    [[nodiscard]] WreckwaterCharacterPlatformPredictionFrameResult
    platformPredictionFrame(uint64_t tick) const noexcept {
        return platformTimeline_.frame(tick);
    }
    [[nodiscard]] const
        WreckwaterCharacterPlatformTimelineTelemetry&
    platformTimelineTelemetry() const noexcept {
        return platformTimeline_.telemetry();
    }

private:
    enum class PlatformPredictionSource : uint32_t {
        Unselected = 0u,
        CertifiedTimeline,
        ExternalExactFrames,
    };

    struct PlatformLifetime {
        game::WreckwaterCharacterPlatformSample sample{};
        bool known = false;
        bool available = false;
    };

    struct PlatformFrame {
        uint64_t tick = 0u;
        uint32_t count = 0u;
        std::array<
            game::WreckwaterCharacterPlatformSample,
            game::kWreckwaterMaximumCharacterPlatforms>
            platforms{};
        std::array<
            PlatformLifetime,
            game::kWreckwaterMaximumCharacterPlatforms>
            lifetimes{};
    };

    struct InputSlot {
        game::WreckwaterCharacterInput input{};
        uint64_t tick = 0u;
        bool occupied = false;
    };

    struct HistorySlot {
        game::WreckwaterCharacterState state{};
        PlatformFrame platforms{};
        uint64_t tick = 0u;
        bool occupied = false;
    };

    [[nodiscard]] WreckwaterCharacterControllerStatus
    platformFrameFromCertifiedSample(
        const network::WreckwaterClientSample& sample,
        PlatformFrame& frame) const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus
    platformFrameFromExactSamples(
        uint64_t tick,
        std::span<const game::WreckwaterCharacterPlatformSample>
            samples,
        PlatformFrame& frame) const noexcept;
    void seedPlatformLifetimes(PlatformFrame& frame) const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus
    validatePlatformFrameSuccessor(
        const PlatformFrame& previous,
        PlatformFrame& current) const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus
    characterStateFromCertifiedSample(
        const network::WreckwaterClientSample& sample,
        const PlatformFrame& platforms,
        game::WreckwaterCharacterState& state) const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus
    replacePlatformTimelineBase(
        const network::WreckwaterClientSample& sample,
        const PlatformFrame& platforms,
        uint64_t requiredThroughTick,
        bool permitDiscontinuityReset,
        bool& resetForDiscontinuity) noexcept;
    [[nodiscard]] static WreckwaterCharacterControllerStatus
    controllerStatus(
        WreckwaterCharacterPlatformTimelineStatus status) noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus replayTo(
        uint64_t targetTick,
        bool replaceFuturePlatformFrames = false) noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus simulateTick(
        uint64_t tick,
        const PlatformFrame& previousPlatforms,
        const PlatformFrame& currentPlatforms,
        bool replaying,
        game::WreckwaterCharacterState& state) noexcept;
    [[nodiscard]] WreckwaterCharacterControllerStatus
    advancePredictionWithPlatforms(
        uint64_t tick,
        std::span<
            const game::WreckwaterCharacterPlatformSample>
            exactPlatforms) noexcept;
    [[nodiscard]] const InputSlot* inputAt(uint64_t tick) const noexcept;
    [[nodiscard]] InputSlot* inputAt(uint64_t tick) noexcept;
    [[nodiscard]] HistorySlot* historyAt(uint64_t tick) noexcept;
    [[nodiscard]] const HistorySlot* historyAt(
        uint64_t tick) const noexcept;
    void pruneInputs(
        uint64_t acknowledgedSequence,
        uint64_t coveredTick) noexcept;
    void clearInputs() noexcept;
    void clearHistory() noexcept;
    void hardPurgeAndSnap(bool countHardSnap) noexcept;
    void failClosed(
        WreckwaterCharacterControllerStatus status) noexcept;
    void updateCorrectionAfterReconciliation(
        const glm::dvec3& oldRenderedAbsolute) noexcept;
    [[nodiscard]] glm::dvec3 renderedAbsolute() const noexcept;
    [[nodiscard]] WreckwaterCharacterControllerPose poseOf(
        const game::WreckwaterCharacterState& state,
        uint64_t tick) const noexcept;

    Config config_{};
    std::array<
        InputSlot, kWreckwaterCharacterControllerMaximumTicks>
        inputs_{};
    std::array<
        HistorySlot, kWreckwaterCharacterControllerMaximumTicks>
        history_{};
    network::WreckwaterClientReplicationIdentity identity_{};
    game::WreckwaterCharacterState authoritativeState_{};
    game::WreckwaterCharacterState predictedState_{};
    PlatformFrame authoritativePlatforms_{};
    WreckwaterCharacterPlatformTimeline platformTimeline_{};
    glm::dvec3 renderCorrection_{0.0};
    uint64_t latestSnapshotSequence_ = 0u;
    uint64_t authoritativeTick_ = 0u;
    uint64_t predictedTick_ = 0u;
    uint32_t inputCount_ = 0u;
    uint32_t historyCount_ = 0u;
    WreckwaterCharacterControllerBinding binding_ =
        WreckwaterCharacterControllerBinding::
            AwaitingCertifiedSnapshot;
    bool initialized_ = false;
    bool identityPinned_ = false;
    bool hasAuthoritativeState_ = false;
    bool hasPredictedState_ = false;
    PlatformPredictionSource platformPredictionSource_ =
        PlatformPredictionSource::Unselected;
    WreckwaterCharacterControllerStatus lastFailClosedStatus_ =
        WreckwaterCharacterControllerStatus::Accepted;
    WreckwaterCharacterControllerTelemetry telemetry_{};
};

} // namespace voxy::client
