#include "client/wreckwater_graphical_client_loop.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace voxy::client {
namespace {

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

void addSaturated(uint64_t& value, uint64_t amount) noexcept {
    if (amount > std::numeric_limits<uint64_t>::max() - value) {
        value = std::numeric_limits<uint64_t>::max();
    } else {
        value += amount;
    }
}

void incrementSaturated(uint32_t& value) noexcept {
    if (value != std::numeric_limits<uint32_t>::max()) ++value;
}

[[nodiscard]] uint32_t statusPriority(
    WreckwaterGraphicalClientFrameStatus status) noexcept {
    switch (status) {
        case WreckwaterGraphicalClientFrameStatus::Accepted:
            return 0u;
        case WreckwaterGraphicalClientFrameStatus::
                RuntimeFrameRejected:
            return 10u;
        case WreckwaterGraphicalClientFrameStatus::
                AwaitingCertifiedCharacter:
            return 20u;
        case WreckwaterGraphicalClientFrameStatus::Disconnected:
        case WreckwaterGraphicalClientFrameStatus::
                RenderSampleUnavailable:
            return 30u;
        case WreckwaterGraphicalClientFrameStatus::SendRetryPending:
            return 40u;
        case WreckwaterGraphicalClientFrameStatus::SchedulerBlocked:
            return 50u;
        case WreckwaterGraphicalClientFrameStatus::InvalidInput:
        case WreckwaterGraphicalClientFrameStatus::
                ExactSampleUnavailable:
            return 60u;
        case WreckwaterGraphicalClientFrameStatus::
                ControllerSampleRejected:
        case WreckwaterGraphicalClientFrameStatus::SendRejected:
        case WreckwaterGraphicalClientFrameStatus::
                ControllerInputRejected:
        case WreckwaterGraphicalClientFrameStatus::
                PredictionRejected:
        case WreckwaterGraphicalClientFrameStatus::
                VisualClockRejected:
        case WreckwaterGraphicalClientFrameStatus::
                PresentationRejected:
            return 70u;
        case WreckwaterGraphicalClientFrameStatus::TickExhausted:
            return 80u;
        case WreckwaterGraphicalClientFrameStatus::NotInitialized:
        case WreckwaterGraphicalClientFrameStatus::
                InvalidConfiguration:
            return 90u;
    }
    return 90u;
}

[[nodiscard]] bool acceptedControllerSample(
    WreckwaterCharacterControllerStatus status) noexcept {
    return status == WreckwaterCharacterControllerStatus::Accepted
        || status
            == WreckwaterCharacterControllerStatus::
                CharacterUnavailable;
}

} // namespace

const char* wreckwaterGraphicalClientFrameStatusName(
    WreckwaterGraphicalClientFrameStatus status) noexcept {
    switch (status) {
        case WreckwaterGraphicalClientFrameStatus::Accepted:
            return "accepted";
        case WreckwaterGraphicalClientFrameStatus::NotInitialized:
            return "not initialized";
        case WreckwaterGraphicalClientFrameStatus::
                InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterGraphicalClientFrameStatus::InvalidInput:
            return "invalid input";
        case WreckwaterGraphicalClientFrameStatus::Disconnected:
            return "disconnected";
        case WreckwaterGraphicalClientFrameStatus::
                AwaitingCertifiedCharacter:
            return "awaiting certified character";
        case WreckwaterGraphicalClientFrameStatus::
                RuntimeFrameRejected:
            return "runtime frame rejected";
        case WreckwaterGraphicalClientFrameStatus::
                ExactSampleUnavailable:
            return "exact sample unavailable";
        case WreckwaterGraphicalClientFrameStatus::
                ControllerSampleRejected:
            return "controller sample rejected";
        case WreckwaterGraphicalClientFrameStatus::SendRetryPending:
            return "send retry pending";
        case WreckwaterGraphicalClientFrameStatus::SendRejected:
            return "send rejected";
        case WreckwaterGraphicalClientFrameStatus::
                ControllerInputRejected:
            return "controller input rejected";
        case WreckwaterGraphicalClientFrameStatus::
                PredictionRejected:
            return "prediction rejected";
        case WreckwaterGraphicalClientFrameStatus::
                VisualClockRejected:
            return "visual clock rejected";
        case WreckwaterGraphicalClientFrameStatus::
                RenderSampleUnavailable:
            return "render sample unavailable";
        case WreckwaterGraphicalClientFrameStatus::
                PresentationRejected:
            return "presentation rejected";
        case WreckwaterGraphicalClientFrameStatus::TickExhausted:
            return "tick exhausted";
        case WreckwaterGraphicalClientFrameStatus::SchedulerBlocked:
            return "scheduler blocked";
    }
    return "unknown";
}

bool WreckwaterGraphicalClientLoop::validConfig(
    const Config& config) const noexcept {
    return config.maximumCatchUpTicks != 0u
        && config.maximumCatchUpTicks
            <= kWreckwaterGraphicalClientMaximumCatchUpTicks
        && config.inputLeadTicks != 0u
        && config.inputLeadTicks
            <= kWreckwaterGraphicalClientMaximumInputLeadTicks
        && config.controller.localPlayerId != 0u
        && config.presentation.localPlayerId
            == config.controller.localPlayerId
        && config.controller.inputHistoryTicks
            > config.inputLeadTicks
        && config.controller.stateHistoryTicks
            >= config.maximumCatchUpTicks;
}

bool WreckwaterGraphicalClientLoop::initialize(
    const Config& config,
    network::WreckwaterClientRuntime& runtime) noexcept {
    initialized_ = false;
    runtime_ = nullptr;
    if (!runtime.initialized() || !validConfig(config)) return false;

    WreckwaterCharacterController controller;
    WreckwaterCharacterPresentation presentation;
    WreckwaterClientVisualClock visualClock;
    if (!controller.initialize(config.controller)
        || !presentation.initialize(config.presentation)
        || (config.useCertifiedVisualClock
            && !visualClock.initialize(config.visualClock))) {
        return false;
    }

    config_ = config;
    runtime_ = &runtime;
    controller_ = controller;
    presentation_ = presentation;
    visualClock_ = visualClock;
    initialized_ = true;
    reset();
    return true;
}

void WreckwaterGraphicalClientLoop::reset() noexcept {
    if (controller_.initialized()) controller_.reset();
    if (presentation_.initialized()) presentation_.reset();
    if (visualClock_.initialized()) visualClock_.reset();
    pending_ = {};
    latestConsumedSnapshotSequence_ = 0u;
    observedConnectionSerial_ = 0u;
    observedCharacterHandle_ = 0u;
    observedCharacterGeneration_ = 0u;
    subTickPhase_ = 0u;
    queuedSimulationTicks_ = 0u;
    observedConnectionActive_ = false;
    observedCharacterReady_ = false;
    lineageHasFreshSnapshot_ = false;
    buttonsObserved_ = false;
    previousJumpDown_ = false;
    previousBoardDown_ = false;
    jumpEdgePending_ = false;
    boardEdgePending_ = false;
    schedulerBlocked_ = false;
    lastStatus_ = initialized_
        ? WreckwaterGraphicalClientFrameStatus::Accepted
        : WreckwaterGraphicalClientFrameStatus::NotInitialized;
    telemetry_ = {};
}

void WreckwaterGraphicalClientLoop::synchronizeButtons(
    const WreckwaterGraphicalClientControls& controls) noexcept {
    previousJumpDown_ = controls.jumpDown;
    previousBoardDown_ = controls.boardDown;
    buttonsObserved_ = true;
}

void WreckwaterGraphicalClientLoop::discardPending(
    bool countFencePurge) noexcept {
    if (pending_.valid && countFencePurge) {
        incrementSaturated(
            telemetry_.pendingSamplesPurgedByFence);
    }
    pending_ = {};
}

void WreckwaterGraphicalClientLoop::fenceLineage(
    const WreckwaterGraphicalClientControls& controls,
    bool resetComponents) noexcept {
    uint64_t oneShots = 0u;
    oneShots += jumpEdgePending_ ? 1u : 0u;
    oneShots += boardEdgePending_ ? 1u : 0u;
    if (pending_.valid) {
        oneShots += pending_.jump ? 1u : 0u;
        oneShots += pending_.board ? 1u : 0u;
    }
    addSaturated(
        telemetry_.oneShotsPurgedByFence, oneShots);
    discardPending(true);
    jumpEdgePending_ = false;
    boardEdgePending_ = false;
    queuedSimulationTicks_ = 0u;
    subTickPhase_ = 0u;
    schedulerBlocked_ = false;
    lineageHasFreshSnapshot_ = false;
    synchronizeButtons(controls);
    if (resetComponents) {
        controller_.reset();
        presentation_.reset();
        if (visualClock_.initialized()) {
            visualClock_.reset();
            incrementSaturated(
                telemetry_.visualClockLineageResets);
        }
        latestConsumedSnapshotSequence_ = 0u;
    }
}

void WreckwaterGraphicalClientLoop::observeButtonEdges(
    const WreckwaterGraphicalClientControls& controls) noexcept {
    if (!buttonsObserved_) {
        synchronizeButtons(controls);
        return;
    }
    if (controls.jumpDown && !previousJumpDown_) {
        if (jumpEdgePending_) {
            incrementSaturated(telemetry_.jumpEdgesCoalesced);
        } else {
            jumpEdgePending_ = true;
            incrementSaturated(telemetry_.jumpEdgesLatched);
        }
    }
    if (controls.boardDown && !previousBoardDown_) {
        if (boardEdgePending_) {
            incrementSaturated(telemetry_.boardEdgesCoalesced);
        } else {
            boardEdgePending_ = true;
            incrementSaturated(telemetry_.boardEdgesLatched);
        }
    }
    synchronizeButtons(controls);
}

void WreckwaterGraphicalClientLoop::requeuePendingOneShots()
    noexcept {
    if (!pending_.valid) return;
    if (pending_.jump) {
        if (jumpEdgePending_) {
            incrementSaturated(telemetry_.jumpEdgesCoalesced);
        } else {
            jumpEdgePending_ = true;
        }
    }
    if (pending_.board) {
        if (boardEdgePending_) {
            incrementSaturated(telemetry_.boardEdgesCoalesced);
        } else {
            boardEdgePending_ = true;
        }
    }
}

void WreckwaterGraphicalClientLoop::
reconcilePendingAfterCertifiedSample() noexcept {
    if (!pending_.valid) return;
    const bool sameIdentity =
        controller_.binding()
            == WreckwaterCharacterControllerBinding::Ready
        && pending_.characterHandle == controller_.characterHandle()
        && pending_.connectionGeneration
            == controller_.connectionGeneration();
    if (!sameIdentity) {
        discardPending(true);
        return;
    }
    const auto pose = controller_.predictedPose();
    if (!pose
        || pose.pose.tick == std::numeric_limits<uint64_t>::max()
        || pending_.simulationTick != pose.pose.tick + 1u
        || pending_.targetTick <= controller_.authoritativeTick()) {
        requeuePendingOneShots();
        discardPending(false);
        incrementSaturated(
            telemetry_.schedulerResynchronizations);
    }
}

void WreckwaterGraphicalClientLoop::accumulateWallClock(
    uint64_t elapsedNanoseconds,
    WreckwaterGraphicalClientFrameResult& result) noexcept {
    const uint64_t wholeSeconds =
        elapsedNanoseconds
        / kWreckwaterGraphicalClientNanosecondsPerSecond;
    const uint64_t remainderNanoseconds =
        elapsedNanoseconds
        % kWreckwaterGraphicalClientNanosecondsPerSecond;
    uint64_t elapsedTicks =
        wholeSeconds
        * kWreckwaterGraphicalClientSimulationRateHz;
    const uint64_t phase =
        subTickPhase_
        + remainderNanoseconds
            * kWreckwaterGraphicalClientSimulationRateHz;
    elapsedTicks +=
        phase / kWreckwaterGraphicalClientNanosecondsPerSecond;
    subTickPhase_ =
        phase % kWreckwaterGraphicalClientNanosecondsPerSecond;

    const uint64_t totalTicks =
        static_cast<uint64_t>(queuedSimulationTicks_)
        + elapsedTicks;
    uint64_t dropped = 0u;
    if (totalTicks > config_.maximumCatchUpTicks) {
        dropped = totalTicks - config_.maximumCatchUpTicks;
        queuedSimulationTicks_ = config_.maximumCatchUpTicks;
        incrementSaturated(telemetry_.hitchFrames);
        addSaturated(
            telemetry_.droppedWallClockTicks, dropped);
    } else {
        queuedSimulationTicks_ =
            static_cast<uint32_t>(totalTicks);
    }
    result.wallClockTicksDropped =
        dropped > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(dropped);
    telemetry_.maximumQueuedSimulationTicks = std::max(
        telemetry_.maximumQueuedSimulationTicks,
        queuedSimulationTicks_);
}

bool WreckwaterGraphicalClientLoop::quantizeControls(
    const WreckwaterGraphicalClientControls& controls,
    int16_t& moveXQ15,
    int16_t& moveZQ15) const noexcept {
    if (!std::isfinite(controls.movement.x)
        || !std::isfinite(controls.movement.y)) {
        return false;
    }
    glm::vec2 movement = controls.movement;
    const float lengthSquared =
        glm::dot(movement, movement);
    if (!std::isfinite(lengthSquared)) return false;
    if (lengthSquared > 1.0f) {
        movement *= 1.0f / std::sqrt(lengthSquared);
    }
    const auto quantize = [](float value) {
        const float bounded = std::clamp(value, -1.0f, 1.0f);
        const float scaled = bounded * 32'767.0f;
        const float rounded = scaled >= 0.0f
            ? std::floor(scaled + 0.5f)
            : std::ceil(scaled - 0.5f);
        return static_cast<int16_t>(std::clamp(
            rounded, -32'767.0f, 32'767.0f));
    };
    moveXQ15 = quantize(movement.x);
    moveZQ15 = quantize(movement.y);
    return true;
}

bool WreckwaterGraphicalClientLoop::createPendingSample(
    int16_t moveXQ15,
    int16_t moveZQ15,
    bool includeOneShots) noexcept {
    const auto pose = controller_.predictedPose();
    if (!pose
        || pose.pose.tick == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    const uint64_t simulationTick = pose.pose.tick + 1u;
    if (config_.inputLeadTicks
        > std::numeric_limits<uint64_t>::max()
            - simulationTick) {
        return false;
    }
    pending_ = {
        .simulationTick = simulationTick,
        .targetTick =
            simulationTick + config_.inputLeadTicks,
        .characterHandle = controller_.characterHandle(),
        .connectionGeneration =
            controller_.connectionGeneration(),
        .moveXQ15 = moveXQ15,
        .moveZQ15 = moveZQ15,
        .jump = includeOneShots && jumpEdgePending_,
        .board = includeOneShots && boardEdgePending_,
        .valid = true,
    };
    if (includeOneShots) {
        jumpEdgePending_ = false;
        boardEdgePending_ = false;
    }
    incrementSaturated(telemetry_.quantizedSamples);
    return true;
}

void WreckwaterGraphicalClientLoop::noteStatus(
    WreckwaterGraphicalClientFrameResult& result,
    WreckwaterGraphicalClientFrameStatus status) noexcept {
    if (statusPriority(status) > statusPriority(result.status)) {
        result.status = status;
    }
}

WreckwaterGraphicalClientFrameResult
WreckwaterGraphicalClientLoop::frame(
    const WreckwaterGraphicalClientFrameInput& input) {
    WreckwaterGraphicalClientFrameResult result;
    if (!initialized_ || runtime_ == nullptr) {
        result.status =
            WreckwaterGraphicalClientFrameStatus::NotInitialized;
        lastStatus_ = result.status;
        return result;
    }
    result.status = WreckwaterGraphicalClientFrameStatus::Accepted;
    incrementSaturated(telemetry_.frames);

    result.runtime = runtime_->pump();
    incrementSaturated(telemetry_.runtimePumps);
    if (result.runtime.framesRejected != 0u) {
        addSaturated(
            telemetry_.runtimeRejectedFrames,
            result.runtime.framesRejected);
        noteStatus(
            result,
            WreckwaterGraphicalClientFrameStatus::
                RuntimeFrameRejected);
    }

    const bool connectionActive = runtime_->connectionActive();
    const uint64_t connectionSerial =
        connectionActive ? runtime_->serverConnectionSerial() : 0u;
    const bool characterReady =
        connectionActive
        && runtime_->localCharacterBindingState()
            == network::WreckwaterLocalCharacterBindingState::Ready;
    const uint32_t characterHandle =
        characterReady ? runtime_->localCharacterHandle() : 0u;
    const uint32_t connectionGeneration =
        characterReady
        ? runtime_->localCharacterConnectionGeneration()
        : 0u;

    const bool connectionChanged =
        connectionActive != observedConnectionActive_
        || (connectionActive
            && connectionSerial != observedConnectionSerial_);
    const bool generationChanged =
        characterReady
        && observedCharacterGeneration_ != 0u
        && (characterHandle != observedCharacterHandle_
            || connectionGeneration
                != observedCharacterGeneration_);
    const bool availabilityChanged =
        characterReady != observedCharacterReady_;
    const bool lineageChanged =
        connectionChanged || generationChanged
        || availabilityChanged;
    if (lineageChanged) {
        fenceLineage(input.controls, true);
        if (connectionChanged) {
            incrementSaturated(telemetry_.connectionFences);
        }
        if (generationChanged) {
            incrementSaturated(telemetry_.generationFences);
        }
        if (!connectionChanged && !generationChanged
            && availabilityChanged) {
            incrementSaturated(
                telemetry_.characterAvailabilityFences);
        }
    }
    observedConnectionActive_ = connectionActive;
    observedConnectionSerial_ = connectionSerial;
    observedCharacterReady_ = characterReady;
    if (characterReady) {
        observedCharacterHandle_ = characterHandle;
        observedCharacterGeneration_ = connectionGeneration;
    }

    const bool shouldConsumeExactSample =
        connectionActive && runtime_->hasSnapshot()
        && (result.runtime.snapshotsAccepted != 0u
            || (characterReady
                && controller_.binding()
                    != WreckwaterCharacterControllerBinding::Ready));
    if (shouldConsumeExactSample) {
        const network::WreckwaterClientSampleResult latest =
            runtime_->latestSample();
        if (!latest) {
            incrementSaturated(telemetry_.exactSamplesRejected);
            schedulerBlocked_ = true;
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    ExactSampleUnavailable);
        } else {
            result.controllerSampleStatus =
                controller_.receiveLatestCertifiedSample(
                    latest.sample);
            if (acceptedControllerSample(
                    result.controllerSampleStatus)) {
                result.certifiedSampleConsumed = true;
                latestConsumedSnapshotSequence_ =
                    latest.sample.authoritative.snapshotSequence;
                lineageHasFreshSnapshot_ = true;
                schedulerBlocked_ = false;
                if (result.controllerSampleStatus
                    == WreckwaterCharacterControllerStatus::
                        CharacterUnavailable) {
                    incrementSaturated(
                        telemetry_.
                            exactCharacterUnavailableSamples);
                } else {
                    incrementSaturated(
                        telemetry_.exactSamplesAccepted);
                }
                reconcilePendingAfterCertifiedSample();
            } else {
                incrementSaturated(
                    telemetry_.exactSamplesRejected);
                schedulerBlocked_ = true;
                noteStatus(
                    result,
                    WreckwaterGraphicalClientFrameStatus::
                        ControllerSampleRejected);
            }
        }
    }

    const bool exactIdentityReady =
        characterReady
        && controller_.binding()
            == WreckwaterCharacterControllerBinding::Ready
        && controller_.characterHandle() == characterHandle
        && controller_.connectionGeneration()
            == connectionGeneration
        && lineageHasFreshSnapshot_;
    if (exactIdentityReady) {
        observeButtonEdges(input.controls);
    } else {
        jumpEdgePending_ = false;
        boardEdgePending_ = false;
        synchronizeButtons(input.controls);
    }

    int16_t moveXQ15 = 0;
    int16_t moveZQ15 = 0;
    const bool validControls = quantizeControls(
        input.controls, moveXQ15, moveZQ15);
    if (!validControls) {
        incrementSaturated(telemetry_.invalidControlFrames);
        noteStatus(
            result,
            WreckwaterGraphicalClientFrameStatus::InvalidInput);
    }

    const bool canSchedule =
        exactIdentityReady && validControls
        && !schedulerBlocked_;
    const bool runtimeFrameRejected =
        result.runtime.framesRejected != 0u;
    if (canSchedule && !lineageChanged) {
        accumulateWallClock(input.elapsedNanoseconds, result);
    }
    if (!connectionActive) {
        noteStatus(
            result,
            WreckwaterGraphicalClientFrameStatus::Disconnected);
    } else if (!exactIdentityReady) {
        noteStatus(
            result,
            WreckwaterGraphicalClientFrameStatus::
                AwaitingCertifiedCharacter);
    } else if (schedulerBlocked_) {
        incrementSaturated(telemetry_.schedulerBlockedFrames);
        noteStatus(
            result,
            WreckwaterGraphicalClientFrameStatus::SchedulerBlocked);
    }

    const bool rejectedFrameBlocksPendingOneShot =
        runtimeFrameRejected && pending_.valid
        && (pending_.jump || pending_.board);
    while (canSchedule
           && !rejectedFrameBlocksPendingOneShot
           && queuedSimulationTicks_ != 0u
           && result.simulationTicks
                < config_.maximumCatchUpTicks) {
        if (!pending_.valid
            && !createPendingSample(
                moveXQ15, moveZQ15,
                !runtimeFrameRejected)) {
            schedulerBlocked_ = true;
            queuedSimulationTicks_ = 0u;
            subTickPhase_ = 0u;
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    TickExhausted);
            break;
        }

        incrementSaturated(telemetry_.sendAttempts);
        incrementSaturated(pending_.sendAttempts);
        telemetry_.maximumSendAttemptsForOneSample = std::max(
            telemetry_.maximumSendAttemptsForOneSample,
            pending_.sendAttempts);
        result.lastSend = runtime_->sendLocalCharacterInput(
            pending_.targetTick,
            pending_.moveXQ15,
            pending_.moveZQ15,
            pending_.jump,
            pending_.board);
        if (!result.lastSend) {
            incrementSaturated(telemetry_.sendFailures);
            if (result.lastSend.error
                == network::WreckwaterClientRuntimeError::
                    TransportSendFailed) {
                incrementSaturated(telemetry_.sendRetries);
                noteStatus(
                    result,
                    WreckwaterGraphicalClientFrameStatus::
                        SendRetryPending);
                break;
            }
            if (result.lastSend.error
                    == network::WreckwaterClientRuntimeError::
                        NoActiveConnection
                || result.lastSend.error
                    == network::WreckwaterClientRuntimeError::
                        CharacterIdentityUnavailable) {
                fenceLineage(input.controls, true);
                noteStatus(
                    result,
                    result.lastSend.error
                            == network::
                                WreckwaterClientRuntimeError::
                                    NoActiveConnection
                        ? WreckwaterGraphicalClientFrameStatus::
                            Disconnected
                        : WreckwaterGraphicalClientFrameStatus::
                            AwaitingCertifiedCharacter);
                break;
            }
            discardPending(false);
            queuedSimulationTicks_ = 0u;
            subTickPhase_ = 0u;
            schedulerBlocked_ = true;
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    SendRejected);
            break;
        }

        const PendingSample sent = pending_;
        discardPending(false);
        incrementSaturated(telemetry_.sendsSucceeded);
        if (sent.jump) {
            incrementSaturated(telemetry_.jumpOneShotsSent);
        }
        if (sent.board) {
            incrementSaturated(telemetry_.boardOneShotsSent);
        }

        result.controllerInputStatus =
            controller_.recordSuccessfullySentInput(
                sent.targetTick,
                result.lastSend.characterInputSequence,
                sent.moveXQ15,
                sent.moveZQ15,
                sent.jump,
                sent.board);
        if (result.controllerInputStatus
            != WreckwaterCharacterControllerStatus::Accepted) {
            queuedSimulationTicks_ = 0u;
            subTickPhase_ = 0u;
            schedulerBlocked_ = true;
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    ControllerInputRejected);
            break;
        }
        incrementSaturated(telemetry_.controllerInputsRecorded);

        result.predictionStatus =
            controller_.advancePrediction(sent.simulationTick);
        if (result.predictionStatus
            != WreckwaterCharacterControllerStatus::Accepted) {
            incrementSaturated(telemetry_.predictionFailures);
            queuedSimulationTicks_ = 0u;
            subTickPhase_ = 0u;
            schedulerBlocked_ = true;
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    PredictionRejected);
            break;
        }
        --queuedSimulationTicks_;
        ++result.simulationTicks;
        incrementSaturated(telemetry_.simulationTicks);
    }
    telemetry_.maximumSimulationTicksPerFrame = std::max(
        telemetry_.maximumSimulationTicksPerFrame,
        result.simulationTicks);

    if (controller_.binding()
        == WreckwaterCharacterControllerBinding::Ready) {
        const float elapsedSeconds = static_cast<float>(
            static_cast<double>(input.elapsedNanoseconds)
            / static_cast<double>(
                kWreckwaterGraphicalClientNanosecondsPerSecond));
        const WreckwaterCharacterControllerStatus correction =
            controller_.advanceRenderCorrection(elapsedSeconds);
        if (correction
            != WreckwaterCharacterControllerStatus::Accepted) {
            result.predictionStatus = correction;
            incrementSaturated(telemetry_.predictionFailures);
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    PredictionRejected);
        }
    }

    if (connectionActive && lineageHasFreshSnapshot_
        && runtime_->hasSnapshot()) {
        bool renderTickReady = true;
        network::WreckwaterPhysicsRenderTick renderTick =
            input.renderTick;
        if (config_.useCertifiedVisualClock) {
            const WreckwaterClientVisualClockResult clock =
                visualClock_.advance(
                    input.elapsedNanoseconds,
                    runtime_->latestPhysicsEvidenceTick());
            result.visualClockStatus = clock.status;
            result.renderTickUsed = clock.renderTick;
            renderTick = clock.renderTick;
            if (clock) {
                incrementSaturated(
                    telemetry_.visualClockUpdates);
            } else {
                renderTickReady = false;
                incrementSaturated(
                    telemetry_.visualClockRejections);
                noteStatus(
                    result,
                    WreckwaterGraphicalClientFrameStatus::
                        VisualClockRejected);
            }
        } else {
            result.renderTickUsed = renderTick;
        }
        if (!renderTickReady) {
            result.sendPending = pending_.valid;
            lastStatus_ = result.status;
            return result;
        }
        const network::WreckwaterClientSampleResult renderSample =
            runtime_->sampleVisual(renderTick);
        result.renderSampleError = renderSample.error;
        if (!renderSample) {
            incrementSaturated(telemetry_.renderSamplesRejected);
            noteStatus(
                result,
                WreckwaterGraphicalClientFrameStatus::
                    RenderSampleUnavailable);
        } else {
            incrementSaturated(telemetry_.renderSamplesAccepted);
            result.presentationStatus = presentation_.update(
                controller_, renderSample.sample,
                input.cameraSector);
            if (result.presentationStatus
                == WreckwaterCharacterPresentationStatus::Accepted) {
                result.presentationUpdated = true;
                incrementSaturated(telemetry_.presentationUpdates);
            } else {
                incrementSaturated(telemetry_.presentationFailures);
                noteStatus(
                    result,
                    WreckwaterGraphicalClientFrameStatus::
                        PresentationRejected);
            }
        }
    }

    result.sendPending = pending_.valid;
    lastStatus_ = result.status;
    return result;
}

} // namespace voxy::client
