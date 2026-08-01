#pragma once

#include "client/wreckwater_character_controller.hpp"
#include "client/wreckwater_character_presentation.hpp"
#include "client/wreckwater_client_visual_clock.hpp"
#include "network/wreckwater_client_runtime.hpp"

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxy::client {

inline constexpr uint32_t
    kWreckwaterGraphicalClientMaximumCatchUpTicks = 16u;
inline constexpr uint32_t
    kWreckwaterGraphicalClientMaximumInputLeadTicks = 64u;
inline constexpr uint64_t
    kWreckwaterGraphicalClientNanosecondsPerSecond =
        1'000'000'000u;
inline constexpr uint32_t
    kWreckwaterGraphicalClientSimulationRateHz = 60u;

enum class WreckwaterGraphicalClientFrameStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidInput,
    Disconnected,
    AwaitingCertifiedCharacter,
    RuntimeFrameRejected,
    ExactSampleUnavailable,
    ControllerSampleRejected,
    SendRetryPending,
    SendRejected,
    ControllerInputRejected,
    PredictionRejected,
    VisualClockRejected,
    RenderSampleUnavailable,
    PresentationRejected,
    TickExhausted,
    SchedulerBlocked,
};

[[nodiscard]] const char* wreckwaterGraphicalClientFrameStatusName(
    WreckwaterGraphicalClientFrameStatus status) noexcept;

struct WreckwaterGraphicalClientControls {
    // Product axes before deterministic unit-circle clamp and Q15
    // quantization.
    glm::vec2 movement{0.0f};
    // Button levels. The loop detects rising edges and retains them until one
    // logical movement sample is sent successfully.
    bool jumpDown = false;
    bool boardDown = false;

    [[nodiscard]] bool operator==(
        const WreckwaterGraphicalClientControls&) const = default;
};

struct WreckwaterGraphicalClientFrameInput {
    // Integer wall-clock input for the fixed-rate phase accumulator.
    uint64_t elapsedNanoseconds = 0u;
    network::WreckwaterPhysicsRenderTick renderTick{};
    glm::ivec3 cameraSector{0};
    WreckwaterGraphicalClientControls controls{};

    [[nodiscard]] bool operator==(
        const WreckwaterGraphicalClientFrameInput&) const = default;
};

struct WreckwaterGraphicalClientTelemetry {
    uint64_t frames = 0u;
    uint64_t runtimePumps = 0u;
    uint64_t runtimeRejectedFrames = 0u;
    uint64_t exactSamplesAccepted = 0u;
    uint64_t exactCharacterUnavailableSamples = 0u;
    uint64_t exactSamplesRejected = 0u;
    uint64_t connectionFences = 0u;
    uint64_t generationFences = 0u;
    uint64_t characterAvailabilityFences = 0u;
    uint64_t schedulerResynchronizations = 0u;
    uint64_t invalidControlFrames = 0u;
    uint64_t quantizedSamples = 0u;
    uint64_t sendAttempts = 0u;
    uint64_t sendsSucceeded = 0u;
    uint64_t sendRetries = 0u;
    uint64_t sendFailures = 0u;
    uint64_t controllerInputsRecorded = 0u;
    uint64_t simulationTicks = 0u;
    uint64_t predictionFailures = 0u;
    uint64_t visualClockUpdates = 0u;
    uint64_t visualClockRejections = 0u;
    uint64_t visualClockLineageResets = 0u;
    uint64_t hitchFrames = 0u;
    uint64_t droppedWallClockTicks = 0u;
    uint64_t jumpEdgesLatched = 0u;
    uint64_t boardEdgesLatched = 0u;
    uint64_t jumpEdgesCoalesced = 0u;
    uint64_t boardEdgesCoalesced = 0u;
    uint64_t jumpOneShotsSent = 0u;
    uint64_t boardOneShotsSent = 0u;
    uint64_t oneShotsPurgedByFence = 0u;
    uint64_t pendingSamplesPurgedByFence = 0u;
    uint64_t renderSamplesAccepted = 0u;
    uint64_t renderSamplesRejected = 0u;
    uint64_t presentationUpdates = 0u;
    uint64_t presentationFailures = 0u;
    uint64_t schedulerBlockedFrames = 0u;
    uint32_t maximumSimulationTicksPerFrame = 0u;
    uint32_t maximumQueuedSimulationTicks = 0u;
    uint32_t maximumSendAttemptsForOneSample = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterGraphicalClientTelemetry&) const = default;
};

struct WreckwaterGraphicalClientFrameResult {
    WreckwaterGraphicalClientFrameStatus status =
        WreckwaterGraphicalClientFrameStatus::NotInitialized;
    network::WreckwaterClientPumpResult runtime{};
    network::WreckwaterClientActionSendResult lastSend{};
    network::WreckwaterClientReplicationError renderSampleError =
        network::WreckwaterClientReplicationError::None;
    WreckwaterCharacterControllerStatus controllerSampleStatus =
        WreckwaterCharacterControllerStatus::Accepted;
    WreckwaterCharacterControllerStatus controllerInputStatus =
        WreckwaterCharacterControllerStatus::Accepted;
    WreckwaterCharacterControllerStatus predictionStatus =
        WreckwaterCharacterControllerStatus::Accepted;
    WreckwaterClientVisualClockStatus visualClockStatus =
        WreckwaterClientVisualClockStatus::NotInitialized;
    WreckwaterCharacterPresentationStatus presentationStatus =
        WreckwaterCharacterPresentationStatus::Accepted;
    network::WreckwaterPhysicsRenderTick renderTickUsed{};
    uint32_t simulationTicks = 0u;
    uint32_t wallClockTicksDropped = 0u;
    bool certifiedSampleConsumed = false;
    bool presentationUpdated = false;
    bool sendPending = false;
};

// Fixed-storage orchestration around an injected network runtime.
//
// The owner retains the runtime and its transport. Tests inject a runtime
// built with a fake transport; the product injects the native/browser
// transport runtime. frame() is the sole runtime pump site and calls it
// exactly once.
class WreckwaterGraphicalClientLoop {
public:
    struct Config {
        uint32_t maximumCatchUpTicks = 4u;
        // API hard limit: 64. The checked-in product authority currently
        // admits at most 16; deployment wiring must keep this value within
        // the server's maximumRequestedTickLead contract.
        uint32_t inputLeadTicks = 8u;
        // Disabled preserves the explicit renderTick API used by existing
        // clients and capture tests. The native application enables this.
        bool useCertifiedVisualClock = false;
        WreckwaterClientVisualClock::Config visualClock{};
        WreckwaterCharacterController::Config controller{};
        WreckwaterCharacterPresentation::Config presentation{};
    };

    [[nodiscard]] bool initialize(
        const Config& config,
        network::WreckwaterClientRuntime& runtime) noexcept;
    void reset() noexcept;

    [[nodiscard]] WreckwaterGraphicalClientFrameResult frame(
        const WreckwaterGraphicalClientFrameInput& input);

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool sendPending() const noexcept {
        return pending_.valid;
    }
    [[nodiscard]] bool schedulerBlocked() const noexcept {
        return schedulerBlocked_;
    }
    [[nodiscard]] uint32_t queuedSimulationTicks() const noexcept {
        return queuedSimulationTicks_;
    }
    [[nodiscard]] uint64_t latestConsumedSnapshotSequence()
        const noexcept {
        return latestConsumedSnapshotSequence_;
    }
    [[nodiscard]] WreckwaterGraphicalClientFrameStatus lastStatus()
        const noexcept {
        return lastStatus_;
    }
    [[nodiscard]] const WreckwaterCharacterController& controller()
        const noexcept {
        return controller_;
    }
    [[nodiscard]] WreckwaterCharacterController& controller()
        noexcept {
        return controller_;
    }
    [[nodiscard]] const WreckwaterCharacterPresentation&
    presentation() const noexcept {
        return presentation_;
    }
    [[nodiscard]] WreckwaterCharacterPresentation& presentation()
        noexcept {
        return presentation_;
    }
    [[nodiscard]] const WreckwaterClientVisualClock& visualClock()
        const noexcept {
        return visualClock_;
    }
    [[nodiscard]] const WreckwaterGraphicalClientTelemetry& telemetry()
        const noexcept {
        return telemetry_;
    }

private:
    struct PendingSample {
        uint64_t simulationTick = 0u;
        uint64_t targetTick = 0u;
        uint32_t characterHandle = 0u;
        uint32_t connectionGeneration = 0u;
        int16_t moveXQ15 = 0;
        int16_t moveZQ15 = 0;
        uint32_t sendAttempts = 0u;
        bool jump = false;
        bool board = false;
        bool valid = false;
    };

    [[nodiscard]] bool validConfig(const Config& config) const noexcept;
    void fenceLineage(
        const WreckwaterGraphicalClientControls& controls,
        bool resetComponents) noexcept;
    void synchronizeButtons(
        const WreckwaterGraphicalClientControls& controls) noexcept;
    void observeButtonEdges(
        const WreckwaterGraphicalClientControls& controls) noexcept;
    void requeuePendingOneShots() noexcept;
    void discardPending(bool countFencePurge) noexcept;
    void reconcilePendingAfterCertifiedSample() noexcept;
    void accumulateWallClock(
        uint64_t elapsedNanoseconds,
        WreckwaterGraphicalClientFrameResult& result) noexcept;
    [[nodiscard]] bool quantizeControls(
        const WreckwaterGraphicalClientControls& controls,
        int16_t& moveXQ15,
        int16_t& moveZQ15) const noexcept;
    [[nodiscard]] bool createPendingSample(
        int16_t moveXQ15,
        int16_t moveZQ15,
        bool includeOneShots) noexcept;
    void noteStatus(
        WreckwaterGraphicalClientFrameResult& result,
        WreckwaterGraphicalClientFrameStatus status) noexcept;

    Config config_{};
    network::WreckwaterClientRuntime* runtime_ = nullptr;
    WreckwaterCharacterController controller_{};
    WreckwaterCharacterPresentation presentation_{};
    WreckwaterClientVisualClock visualClock_{};
    PendingSample pending_{};
    uint64_t latestConsumedSnapshotSequence_ = 0u;
    uint64_t observedConnectionSerial_ = 0u;
    uint32_t observedCharacterHandle_ = 0u;
    uint32_t observedCharacterGeneration_ = 0u;
    uint64_t subTickPhase_ = 0u;
    uint32_t queuedSimulationTicks_ = 0u;
    bool observedConnectionActive_ = false;
    bool observedCharacterReady_ = false;
    bool lineageHasFreshSnapshot_ = false;
    bool buttonsObserved_ = false;
    bool previousJumpDown_ = false;
    bool previousBoardDown_ = false;
    bool jumpEdgePending_ = false;
    bool boardEdgePending_ = false;
    bool schedulerBlocked_ = false;
    bool initialized_ = false;
    WreckwaterGraphicalClientFrameStatus lastStatus_ =
        WreckwaterGraphicalClientFrameStatus::NotInitialized;
    WreckwaterGraphicalClientTelemetry telemetry_{};
};

} // namespace voxy::client
