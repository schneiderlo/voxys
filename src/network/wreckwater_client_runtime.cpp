#include "network/wreckwater_client_runtime.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace voxy::network {
namespace {

inline constexpr uint32_t kMaximumRuntimeFramesPerPump = 256u;

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

[[nodiscard]] bool cargoAction(WreckwaterAction action) noexcept {
    switch (action) {
        case WreckwaterAction::Tow:
        case WreckwaterAction::Cut:
        case WreckwaterAction::Steal:
        case WreckwaterAction::Bank:
            return true;
        case WreckwaterAction::Helm:
            return false;
    }
    return false;
}

} // namespace

const char* wreckwaterClientRuntimeErrorName(
    WreckwaterClientRuntimeError error) noexcept {
    switch (error) {
        case WreckwaterClientRuntimeError::None: return "none";
        case WreckwaterClientRuntimeError::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterClientRuntimeError::NoActiveConnection:
            return "no active connection";
        case WreckwaterClientRuntimeError::CharacterIdentityUnavailable:
            return "character identity unavailable";
        case WreckwaterClientRuntimeError::InvalidAction:
            return "invalid action";
        case WreckwaterClientRuntimeError::NonMonotonicRequestTick:
            return "nonmonotonic request tick";
        case WreckwaterClientRuntimeError::RequestSequenceExhausted:
            return "request sequence exhausted";
        case WreckwaterClientRuntimeError::ActionEncodeFailed:
            return "action encode failed";
        case WreckwaterClientRuntimeError::CharacterInputEncodeFailed:
            return "character input encode failed";
        case WreckwaterClientRuntimeError::OuterPacketEncodeFailed:
            return "outer packet encode failed";
        case WreckwaterClientRuntimeError::TransportSendFailed:
            return "transport send failed";
        case WreckwaterClientRuntimeError::StaleConnectionFrame:
            return "stale connection frame";
        case WreckwaterClientRuntimeError::InvalidLifecycleFrame:
            return "invalid lifecycle frame";
        case WreckwaterClientRuntimeError::WrongDeliveryClass:
            return "wrong delivery class";
        case WreckwaterClientRuntimeError::OversizedFrame:
            return "oversized frame";
        case WreckwaterClientRuntimeError::OuterPacketDecodeFailed:
            return "outer packet decode failed";
        case WreckwaterClientRuntimeError::WrongPayloadType:
            return "wrong payload type";
        case WreckwaterClientRuntimeError::OuterIdentityMismatch:
            return "outer identity mismatch";
        case WreckwaterClientRuntimeError::SnapshotDecodeFailed:
            return "snapshot decode failed";
        case WreckwaterClientRuntimeError::SnapshotIdentityMismatch:
            return "snapshot identity mismatch";
        case WreckwaterClientRuntimeError::SnapshotRejected:
            return "snapshot rejected";
    }
    return "unknown";
}

WreckwaterClientRuntime::WreckwaterClientRuntime(
    Config config, std::unique_ptr<IMultiplayerTransport> transport)
    : config_(config),
      transport_(std::move(transport)),
      snapshots_(config_.snapshotBuffer),
      nextClientRequestSequence_(config_.firstClientRequestSequence),
      currentConnectionSerial_(config_.serverConnectionSerial),
      connectionSerialHighWater_(config_.serverConnectionSerial) {
    if (transport_ == nullptr || !snapshots_.initialized()
        || config_.sessionId == 0u || config_.matchId == 0u
        || config_.worldId == 0u || config_.worldEpoch == 0u
        || config_.authorityEpoch == 0u
        || config_.maximumFramesPerPump == 0u
        || config_.maximumFramesPerPump > kMaximumRuntimeFramesPerPump
        || config_.firstClientRequestSequence == 0u) {
        return;
    }
    initialized_ = true;
    connectionActive_ = currentConnectionSerial_ != 0u;
    requestSequenceAvailable_ = true;
    localCharacterBindingState_ =
        config_.localPlayerId == 0u
        ? WreckwaterLocalCharacterBindingState::Disabled
        : WreckwaterLocalCharacterBindingState::
            AwaitingAuthoritativeGeneration;
}

void WreckwaterClientRuntime::purgeCharacterInputHistory() noexcept {
    if (characterInputHistoryCount_ == 0u) return;
    incrementSaturated(telemetry_.characterInputHistoryPurges);
    addSaturated(
        telemetry_.characterInputHistoryPurgedSamples,
        characterInputHistoryCount_);
    characterInputHistory_ = {};
    characterInputHistoryCount_ = 0u;
}

void WreckwaterClientRuntime::acknowledgeCharacterInputs(
    uint64_t sequence) noexcept {
    if (sequence == 0u) return;
    if (sequence >= nextCharacterInputSequence_) {
        if (sequence
            >= std::numeric_limits<uint64_t>::max() - 1u) {
            characterInputSequenceAvailable_ = false;
        } else {
            nextCharacterInputSequence_ = sequence + 1u;
            characterInputSequenceAvailable_ = true;
        }
    }
    if (characterInputHistoryCount_ == 0u) return;
    uint32_t acknowledged = 0u;
    while (acknowledged < characterInputHistoryCount_
           && characterInputHistory_[acknowledged]
                  .characterInputSequence <= sequence) {
        ++acknowledged;
    }
    if (acknowledged == 0u) return;
    for (uint32_t index = acknowledged;
         index < characterInputHistoryCount_; ++index) {
        characterInputHistory_[index - acknowledged] =
            characterInputHistory_[index];
    }
    const uint32_t retained =
        characterInputHistoryCount_ - acknowledged;
    for (uint32_t index = retained;
         index < characterInputHistoryCount_; ++index) {
        characterInputHistory_[index] = {};
    }
    characterInputHistoryCount_ = retained;
    addSaturated(
        telemetry_.characterInputSamplesAcknowledged,
        acknowledged);
}

void WreckwaterClientRuntime::invalidateLocalCharacterBinding() noexcept {
    if (localCharacterBindingState_
        != WreckwaterLocalCharacterBindingState::Disabled) {
        localCharacterBindingState_ =
            WreckwaterLocalCharacterBindingState::
                AwaitingAuthoritativeGeneration;
    }
    purgeCharacterInputHistory();
}

void WreckwaterClientRuntime::observeLocalCharacterBinding(
    const WreckwaterCertifiedSnapshot& snapshot) noexcept {
    if (localCharacterBindingState_
        == WreckwaterLocalCharacterBindingState::Disabled) {
        return;
    }
    const auto found = std::lower_bound(
        snapshot.characters.begin(), snapshot.characters.end(),
        config_.localPlayerId,
        [](const WreckwaterCharacterState& character,
           uint64_t playerId) {
            return character.playerId < playerId;
        });
    if (found == snapshot.characters.end()
        || found->playerId != config_.localPlayerId
        || (found->stateFlags
            & kWreckwaterCharacterStateConnectedFlag)
            == 0u) {
        invalidateLocalCharacterBinding();
        return;
    }
    if (localCharacterHandle_ != 0u
        && found->characterHandle != localCharacterHandle_) {
        invalidateLocalCharacterBinding();
        return;
    }
    if (localCharacterBindingState_
            == WreckwaterLocalCharacterBindingState::Ready
        && found->connectionGeneration
            == localCharacterConnectionGenerationHighWater_) {
        if (characterInputIdentityBound_
            && characterInputHandle_ == found->characterHandle
            && characterInputConnectionGeneration_
                == found->connectionGeneration) {
            acknowledgeCharacterInputs(
                found->lastAppliedCharacterInputSequence);
        }
        return;
    }
    if (localCharacterConnectionGenerationHighWater_ != 0u
        && found->connectionGeneration
            <= localCharacterConnectionGenerationHighWater_) {
        invalidateLocalCharacterBinding();
        return;
    }
    purgeCharacterInputHistory();
    localCharacterHandle_ = found->characterHandle;
    localCharacterConnectionGenerationHighWater_ =
        found->connectionGeneration;
    characterInputHandle_ = found->characterHandle;
    characterInputConnectionGeneration_ =
        found->connectionGeneration;
    characterInputIdentityBound_ = true;
    if (found->lastAppliedCharacterInputSequence
        >= std::numeric_limits<uint64_t>::max() - 1u) {
        characterInputSequenceAvailable_ = false;
    } else {
        nextCharacterInputSequence_ =
            found->lastAppliedCharacterInputSequence + 1u;
        characterInputSequenceAvailable_ = true;
    }
    localCharacterBindingState_ =
        WreckwaterLocalCharacterBindingState::Ready;
}

WreckwaterClientRuntime::FrameProcessResult
WreckwaterClientRuntime::processFrame(
    const MultiplayerTransportFrame& frame) {
    FrameProcessResult result;
    if (frame.peerId != config_.serverPeerId) {
        result.error =
            WreckwaterClientRuntimeError::StaleConnectionFrame;
        incrementSaturated(telemetry_.staleConnectionFrames);
        return result;
    }

    if (frame.type != MultiplayerTransportFrameType::Data) {
        if (!frame.bytes.empty()) {
            result.error =
                WreckwaterClientRuntimeError::InvalidLifecycleFrame;
            return result;
        }
        if (frame.type == MultiplayerTransportFrameType::Connected) {
            if (frame.connectionSerial == 0u) {
                result.error =
                    WreckwaterClientRuntimeError::StaleConnectionFrame;
                incrementSaturated(
                    telemetry_.staleConnectionFrames);
                return result;
            }
            if (connectionActive_) {
                if (frame.connectionSerial
                    != currentConnectionSerial_) {
                    result.error =
                        WreckwaterClientRuntimeError::
                            StaleConnectionFrame;
                    incrementSaturated(
                        telemetry_.staleConnectionFrames);
                    return result;
                }
                incrementSaturated(telemetry_.lifecycleFrames);
                return result;
            }
            if (connectionEnded_
                || currentConnectionSerial_ != 0u
                || frame.connectionSerial
                    <= connectionSerialHighWater_) {
                result.error =
                    WreckwaterClientRuntimeError::
                        StaleConnectionFrame;
                incrementSaturated(
                    telemetry_.staleConnectionFrames);
                return result;
            }
            currentConnectionSerial_ = frame.connectionSerial;
            connectionSerialHighWater_ = frame.connectionSerial;
            connectionActive_ = true;
            incrementSaturated(
                telemetry_.connectionSerialBindings);
            incrementSaturated(telemetry_.lifecycleFrames);
            return result;
        }
        if (frame.type
            == MultiplayerTransportFrameType::Disconnected) {
            if (!connectionActive_
                || frame.connectionSerial
                    != currentConnectionSerial_) {
                result.error =
                    WreckwaterClientRuntimeError::
                        StaleConnectionFrame;
                incrementSaturated(
                    telemetry_.staleConnectionFrames);
                return result;
            }
            connectionActive_ = false;
            connectionEnded_ = true;
            invalidateLocalCharacterBinding();
            incrementSaturated(telemetry_.lifecycleFrames);
            return result;
        }
        result.error =
            WreckwaterClientRuntimeError::InvalidLifecycleFrame;
        return result;
    }

    if (!connectionActive_ || connectionEnded_ || closed_
        || currentConnectionSerial_ == 0u
        || frame.connectionSerial
            != currentConnectionSerial_) {
        result.error =
            WreckwaterClientRuntimeError::StaleConnectionFrame;
        incrementSaturated(telemetry_.staleConnectionFrames);
        return result;
    }
    if (frame.delivery != DeliveryClass::Realtime) {
        result.error =
            WreckwaterClientRuntimeError::WrongDeliveryClass;
        return result;
    }
    if (frame.bytes.size() > kConservativeRealtimeMtu) {
        result.error = WreckwaterClientRuntimeError::OversizedFrame;
        return result;
    }

    const PacketReadResult outer =
        PacketCodec::decode(frame.bytes, DeliveryClass::Realtime);
    if (!outer.packet.has_value()) {
        result.error =
            WreckwaterClientRuntimeError::OuterPacketDecodeFailed;
        incrementSaturated(telemetry_.outerDecodeFailures);
        return result;
    }
    const Packet& packet = *outer.packet;
    if (packet.header.payloadType != PacketPayloadType::Snapshot) {
        result.error = WreckwaterClientRuntimeError::WrongPayloadType;
        return result;
    }
    if (packet.header.flags != 0u
        || packet.header.sessionId != config_.sessionId
        || packet.header.worldId != config_.worldId
        || packet.header.worldEpoch != config_.worldEpoch
        || packet.header.authorityEpoch != config_.authorityEpoch
        || packet.header.sequence == 0u) {
        result.error =
            WreckwaterClientRuntimeError::OuterIdentityMismatch;
        incrementSaturated(telemetry_.snapshotIdentityFailures);
        return result;
    }
    AckWindow candidateAckWindow = snapshotAckWindow_;
    if (!candidateAckWindow.observe(packet.header.sequence)) {
        result.error = WreckwaterClientRuntimeError::SnapshotRejected;
        result.replicationError =
            WreckwaterClientReplicationError::NonMonotonicSequence;
        incrementSaturated(telemetry_.snapshotBufferFailures);
        return result;
    }

    WreckwaterSnapshotReadResult decoded =
        WreckwaterSnapshotCodec::decode(packet.payload);
    if (!decoded.snapshot.has_value()) {
        result.error =
            WreckwaterClientRuntimeError::SnapshotDecodeFailed;
        result.snapshotCodecError = decoded.error;
        incrementSaturated(telemetry_.snapshotDecodeFailures);
        return result;
    }
    WreckwaterCertifiedSnapshot& snapshot = *decoded.snapshot;
    if (snapshot.sessionId != config_.sessionId
        || snapshot.matchId != config_.matchId
        || snapshot.worldId != config_.worldId
        || snapshot.worldEpoch != config_.worldEpoch
        || snapshot.authorityEpoch != config_.authorityEpoch
        || packet.header.sessionId != snapshot.sessionId
        || packet.header.worldId != snapshot.worldId
        || packet.header.worldEpoch != snapshot.worldEpoch
        || packet.header.authorityEpoch != snapshot.authorityEpoch
        || packet.header.sequence != snapshot.snapshotSequence
        || packet.header.tick != snapshot.applicationTick) {
        result.error =
            WreckwaterClientRuntimeError::SnapshotIdentityMismatch;
        incrementSaturated(telemetry_.snapshotIdentityFailures);
        return result;
    }

    const WreckwaterClientIngestResult ingested =
        snapshots_.ingest(snapshot);
    if (!ingested) {
        result.error = WreckwaterClientRuntimeError::SnapshotRejected;
        result.replicationError = ingested.error;
        incrementSaturated(telemetry_.snapshotBufferFailures);
        return result;
    }

    snapshotAckWindow_ = candidateAckWindow;
    latestPhysicsEvidenceTick_ = snapshot.physicsEvidenceTick;
    hasSnapshot_ = true;
    observeLocalCharacterBinding(snapshot);
    result.snapshotAccepted = true;
    incrementSaturated(telemetry_.acceptedSnapshots);
    return result;
}

WreckwaterClientPumpResult WreckwaterClientRuntime::pump() {
    WreckwaterClientPumpResult result;
    if (!initialized_) {
        result.lastError =
            WreckwaterClientRuntimeError::InvalidConfiguration;
        return result;
    }

    transport_->service();
    incrementSaturated(telemetry_.serviceCalls);
    for (uint32_t drained = 0u;
         drained < config_.maximumFramesPerPump; ++drained) {
        std::optional<MultiplayerTransportFrame> frame =
            transport_->poll();
        if (!frame.has_value()) break;
        ++result.framesPolled;
        incrementSaturated(telemetry_.framesPolled);

        const FrameProcessResult processed = processFrame(*frame);
        if (processed.snapshotAccepted) ++result.snapshotsAccepted;
        if (processed.error != WreckwaterClientRuntimeError::None) {
            ++result.framesRejected;
            result.lastError = processed.error;
            result.lastSnapshotCodecError =
                processed.snapshotCodecError;
            result.lastReplicationError =
                processed.replicationError;
            incrementSaturated(telemetry_.rejectedFrames);
        }
    }
    telemetry_.maximumFramesDrainedInPump = std::max(
        telemetry_.maximumFramesDrainedInPump, result.framesPolled);
    return result;
}

WreckwaterClientActionSendResult WreckwaterClientRuntime::sendAction(
    WreckwaterActionRequest request, PacketPayloadType payloadType,
    DeliveryClass delivery) {
    WreckwaterClientActionSendResult result;
    if (!initialized_) {
        result.error =
            WreckwaterClientRuntimeError::InvalidConfiguration;
        return result;
    }
    if (!connectionActive() || transport_ == nullptr) {
        result.error =
            WreckwaterClientRuntimeError::NoActiveConnection;
        return result;
    }
    if (!requestSequenceAvailable_) {
        result.error =
            WreckwaterClientRuntimeError::RequestSequenceExhausted;
        return result;
    }
    if (hasActionRequestedApplicationTick_
        && request.requestedApplicationTick
            < lastActionRequestedApplicationTick_) {
        result.error =
            WreckwaterClientRuntimeError::NonMonotonicRequestTick;
        return result;
    }

    request.schemaVersion = kWreckwaterWireSchemaVersion;
    request.clientRequestSequence = nextClientRequestSequence_;
    WreckwaterWriteResult action =
        WreckwaterActionRequestCodec::encode(request);
    if (!action) {
        result.error =
            WreckwaterClientRuntimeError::ActionEncodeFailed;
        result.codecError = action.error;
        incrementSaturated(telemetry_.actionEncodeFailures);
        return result;
    }

    Packet packet;
    packet.header.payloadType = payloadType;
    packet.header.flags = 0u;
    packet.header.sessionId = config_.sessionId;
    packet.header.worldId = config_.worldId;
    packet.header.worldEpoch = config_.worldEpoch;
    packet.header.authorityEpoch = config_.authorityEpoch;
    packet.header.sequence = request.clientRequestSequence;
    packet.header.ackSequence = snapshotAckWindow_.latest();
    packet.header.ackBits = snapshotAckWindow_.bits();
    packet.header.tick = request.requestedApplicationTick;
    packet.payload = std::move(action.bytes);
    const PacketWriteResult encoded = PacketCodec::encode(packet, delivery);
    if (!encoded.error.empty()) {
        result.error =
            WreckwaterClientRuntimeError::OuterPacketEncodeFailed;
        return result;
    }
    if (delivery == DeliveryClass::Realtime
        && encoded.bytes.size() > kConservativeRealtimeMtu) {
        result.error =
            WreckwaterClientRuntimeError::OuterPacketEncodeFailed;
        return result;
    }

    result.clientRequestSequence = request.clientRequestSequence;
    if (!transport_->send(
            config_.serverPeerId, currentConnectionSerial_,
            delivery, encoded.bytes)) {
        result.error =
            WreckwaterClientRuntimeError::TransportSendFailed;
        incrementSaturated(telemetry_.sendFailures);
        return result;
    }
    lastActionRequestedApplicationTick_ =
        request.requestedApplicationTick;
    hasActionRequestedApplicationTick_ = true;
    if (nextClientRequestSequence_
        == std::numeric_limits<uint64_t>::max()) {
        requestSequenceAvailable_ = false;
    } else {
        ++nextClientRequestSequence_;
    }
    addSaturated(
        telemetry_.sentPacketBytes,
        encoded.bytes.size());
    if (request.action == WreckwaterAction::Helm) {
        incrementSaturated(telemetry_.helmRequestsSent);
    } else {
        incrementSaturated(telemetry_.cargoRequestsSent);
    }
    return result;
}

WreckwaterClientActionSendResult WreckwaterClientRuntime::sendHelm(
    uint64_t requestedApplicationTick,
    int16_t throttleQ15, int16_t steeringQ15) {
    WreckwaterActionRequest request;
    request.requestedApplicationTick = requestedApplicationTick;
    request.action = WreckwaterAction::Helm;
    request.helmThrottleQ15 = throttleQ15;
    request.helmSteeringQ15 = steeringQ15;
    return sendAction(
        request, PacketPayloadType::Input, DeliveryClass::Realtime);
}

WreckwaterClientActionSendResult
WreckwaterClientRuntime::sendCargoAction(
    uint64_t requestedApplicationTick, WreckwaterAction action,
    uint32_t cargoId, uint32_t cargoGeneration,
    uint32_t observedCargoRevision) {
    if (!cargoAction(action)) {
        return {
            .error = WreckwaterClientRuntimeError::InvalidAction,
        };
    }
    WreckwaterActionRequest request;
    request.requestedApplicationTick = requestedApplicationTick;
    request.action = action;
    request.cargoId = cargoId;
    request.cargoGeneration = cargoGeneration;
    request.observedCargoRevision = observedCargoRevision;
    return sendAction(
        request, PacketPayloadType::Command,
        DeliveryClass::ReliableEvent);
}

WreckwaterClientActionSendResult
WreckwaterClientRuntime::sendCharacterInput(
    uint64_t requestedApplicationTick,
    uint32_t characterHandle, uint32_t connectionGeneration,
    int16_t moveXQ15, int16_t moveZQ15,
    bool jump, bool board) {
    WreckwaterClientActionSendResult result;
    if (!initialized_) {
        result.error =
            WreckwaterClientRuntimeError::InvalidConfiguration;
        return result;
    }
    if (!connectionActive() || transport_ == nullptr) {
        result.error =
            WreckwaterClientRuntimeError::NoActiveConnection;
        return result;
    }
    if (!requestSequenceAvailable_) {
        result.error =
            WreckwaterClientRuntimeError::RequestSequenceExhausted;
        return result;
    }
    uint64_t characterInputSequence = 1u;
    bool sameCharacterIdentity = false;
    if (characterInputIdentityBound_) {
        if (connectionGeneration
                < characterInputConnectionGeneration_
            || (connectionGeneration
                    == characterInputConnectionGeneration_
                && characterHandle != characterInputHandle_)) {
            result.error =
                WreckwaterClientRuntimeError::
                    CharacterInputEncodeFailed;
            result.codecError = WreckwaterCodecError::InvalidIdentity;
            incrementSaturated(
                telemetry_.characterInputEncodeFailures);
            return result;
        }
        if (connectionGeneration
            == characterInputConnectionGeneration_) {
            sameCharacterIdentity = true;
            if (!characterInputSequenceAvailable_) {
                result.error =
                    WreckwaterClientRuntimeError::
                        RequestSequenceExhausted;
                return result;
            }
            characterInputSequence = nextCharacterInputSequence_;
        }
    }
    if (hasCharacterRequestedApplicationTick_
        && requestedApplicationTick
            < lastCharacterRequestedApplicationTick_) {
        result.error =
            WreckwaterClientRuntimeError::NonMonotonicRequestTick;
        return result;
    }

    WreckwaterCharacterInputRequest request;
    request.requestedApplicationTick = requestedApplicationTick;
    request.clientRequestSequence = nextClientRequestSequence_;
    request.characterHandle = characterHandle;
    request.connectionGeneration = connectionGeneration;
    request.moveXQ15 = moveXQ15;
    request.moveZQ15 = moveZQ15;
    request.inputFlags =
        (jump ? kWreckwaterCharacterInputJumpFlag : 0u)
        | (board ? kWreckwaterCharacterInputBoardFlag : 0u);
    request.characterInputSequence = characterInputSequence;
    if (sameCharacterIdentity) {
        request.redundantInputCount =
            characterInputHistoryCount_;
        std::copy_n(
            characterInputHistory_.begin(),
            characterInputHistoryCount_,
            request.redundantInputs.begin());
    }
    WreckwaterWriteResult input =
        WreckwaterCharacterInputRequestCodec::encode(request);
    if (!input) {
        result.error =
            WreckwaterClientRuntimeError::CharacterInputEncodeFailed;
        result.codecError = input.error;
        incrementSaturated(
            telemetry_.characterInputEncodeFailures);
        return result;
    }

    Packet packet;
    packet.header.payloadType = PacketPayloadType::Input;
    packet.header.sessionId = config_.sessionId;
    packet.header.worldId = config_.worldId;
    packet.header.worldEpoch = config_.worldEpoch;
    packet.header.authorityEpoch = config_.authorityEpoch;
    packet.header.sequence = request.clientRequestSequence;
    packet.header.ackSequence = snapshotAckWindow_.latest();
    packet.header.ackBits = snapshotAckWindow_.bits();
    packet.header.tick = requestedApplicationTick;
    packet.payload = std::move(input.bytes);
    const PacketWriteResult encoded =
        PacketCodec::encode(packet, DeliveryClass::Realtime);
    if (!encoded.error.empty()
        || encoded.bytes.size() > kConservativeRealtimeMtu) {
        result.error =
            WreckwaterClientRuntimeError::OuterPacketEncodeFailed;
        return result;
    }

    result.clientRequestSequence = request.clientRequestSequence;
    result.characterInputSequence =
        request.characterInputSequence;
    if (!transport_->send(
            config_.serverPeerId, currentConnectionSerial_,
            DeliveryClass::Realtime, encoded.bytes)) {
        result.error =
            WreckwaterClientRuntimeError::TransportSendFailed;
        incrementSaturated(telemetry_.sendFailures);
        return result;
    }
    lastCharacterRequestedApplicationTick_ =
        requestedApplicationTick;
    hasCharacterRequestedApplicationTick_ = true;
    if (!sameCharacterIdentity) {
        purgeCharacterInputHistory();
    }
    if (characterInputHistoryCount_
        == kWreckwaterCharacterInputMaximumRedundantSamples) {
        for (uint32_t index = 1u;
             index < characterInputHistoryCount_; ++index) {
            characterInputHistory_[index - 1u] =
                characterInputHistory_[index];
        }
        --characterInputHistoryCount_;
        incrementSaturated(
            telemetry_.
                characterInputSamplesRetiredUnacknowledged);
    }
    characterInputHistory_[characterInputHistoryCount_++] = {
        .requestedApplicationTick =
            request.requestedApplicationTick,
        .characterInputSequence =
            request.characterInputSequence,
        .moveXQ15 = request.moveXQ15,
        .moveZQ15 = request.moveZQ15,
        .inputFlags = request.inputFlags,
    };
    if (nextClientRequestSequence_
        == std::numeric_limits<uint64_t>::max()) {
        requestSequenceAvailable_ = false;
    } else {
        ++nextClientRequestSequence_;
    }
    characterInputHandle_ = characterHandle;
    characterInputConnectionGeneration_ = connectionGeneration;
    characterInputIdentityBound_ = true;
    if (characterInputSequence
        >= std::numeric_limits<uint64_t>::max() - 1u) {
        characterInputSequenceAvailable_ = false;
    } else {
        nextCharacterInputSequence_ = characterInputSequence + 1u;
        characterInputSequenceAvailable_ = true;
    }
    addSaturated(
        telemetry_.sentPacketBytes, encoded.bytes.size());
    incrementSaturated(telemetry_.characterInputsSent);
    addSaturated(
        telemetry_.characterInputSamplesRetransmitted,
        request.redundantInputCount);
    telemetry_.maximumCharacterInputRedundancy = std::max(
        telemetry_.maximumCharacterInputRedundancy,
        request.redundantInputCount);
    return result;
}

WreckwaterClientActionSendResult
WreckwaterClientRuntime::sendLocalCharacterInput(
    uint64_t requestedApplicationTick,
    int16_t moveXQ15, int16_t moveZQ15,
    bool jump, bool board) {
    if (!initialized_) {
        return {
            .error =
                WreckwaterClientRuntimeError::InvalidConfiguration,
        };
    }
    if (localCharacterBindingState_
            != WreckwaterLocalCharacterBindingState::Ready
        || localCharacterHandle_ == 0u
        || localCharacterConnectionGenerationHighWater_ == 0u) {
        return {
            .error = WreckwaterClientRuntimeError::
                CharacterIdentityUnavailable,
        };
    }
    return sendCharacterInput(
        requestedApplicationTick,
        localCharacterHandle_,
        localCharacterConnectionGenerationHighWater_,
        moveXQ15, moveZQ15, jump, board);
}

WreckwaterClientSampleResult WreckwaterClientRuntime::latestSample()
    const noexcept {
    if (!hasSnapshot_) {
        return snapshots_.sample({});
    }
    return snapshots_.sample({
        .whole = latestPhysicsEvidenceTick_,
        .fraction = 0.0f,
    });
}

bool WreckwaterClientRuntime::disconnectForTransportReplacement() {
    if (!initialized_ || closed_ || transport_ == nullptr
        || !connectionActive_ || connectionEnded_
        || currentConnectionSerial_ == 0u) {
        return false;
    }
    transport_->close();
    connectionActive_ = false;
    connectionEnded_ = true;
    invalidateLocalCharacterBinding();
    incrementSaturated(telemetry_.localTransportDisconnects);
    return true;
}

bool WreckwaterClientRuntime::replaceTransport(
    std::unique_ptr<IMultiplayerTransport> transport) {
    if (!initialized_ || closed_ || transport == nullptr
        || connectionActive_ || !connectionEnded_
        || connectionSerialHighWater_
            == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    if (transport_ != nullptr) transport_->close();
    transport_ = std::move(transport);
    currentConnectionSerial_ = 0u;
    connectionEnded_ = false;
    incrementSaturated(telemetry_.transportReplacements);
    return true;
}

void WreckwaterClientRuntime::close() {
    if (closed_) return;
    closed_ = true;
    connectionActive_ = false;
    connectionEnded_ = true;
    invalidateLocalCharacterBinding();
    if (transport_ != nullptr) transport_->close();
}

} // namespace voxy::network
