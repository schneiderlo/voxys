#pragma once

#include "network/session_transport.hpp"
#include "network/wreckwater_client_replication.hpp"
#include "network/wreckwater_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace voxy::network {

inline constexpr size_t kWreckwaterFramedActionRequestBytes =
    kWreckwaterActionRequestBytes + kNetworkPacketOverheadBytes;
inline constexpr size_t kWreckwaterFramedCharacterInputRequestBytes =
    kWreckwaterCharacterInputRequestBytes +
    kNetworkPacketOverheadBytes;
static_assert(kWreckwaterFramedActionRequestBytes == 148u);
static_assert(kWreckwaterFramedCharacterInputRequestBytes == 228u);
static_assert(
    kWreckwaterFramedActionRequestBytes <= kConservativeRealtimeMtu);
static_assert(
    kWreckwaterFramedCharacterInputRequestBytes
    <= kConservativeRealtimeMtu);
static_assert(
    kWreckwaterFirstSliceSnapshotBytes + kNetworkPacketOverheadBytes
    <= kConservativeRealtimeMtu);

enum class WreckwaterClientRuntimeError : uint32_t {
    None = 0u,
    InvalidConfiguration,
    NoActiveConnection,
    CharacterIdentityUnavailable,
    InvalidAction,
    NonMonotonicRequestTick,
    RequestSequenceExhausted,
    ActionEncodeFailed,
    CharacterInputEncodeFailed,
    OuterPacketEncodeFailed,
    TransportSendFailed,
    StaleConnectionFrame,
    InvalidLifecycleFrame,
    WrongDeliveryClass,
    OversizedFrame,
    OuterPacketDecodeFailed,
    WrongPayloadType,
    OuterIdentityMismatch,
    SnapshotDecodeFailed,
    SnapshotIdentityMismatch,
    SnapshotRejected,
};

enum class WreckwaterLocalCharacterBindingState : uint32_t {
    Disabled = 0u,
    AwaitingAuthoritativeGeneration,
    Ready,
};

[[nodiscard]] const char* wreckwaterClientRuntimeErrorName(
    WreckwaterClientRuntimeError error) noexcept;

struct WreckwaterClientRuntimeTelemetry {
    uint64_t serviceCalls = 0u;
    uint64_t framesPolled = 0u;
    uint64_t lifecycleFrames = 0u;
    uint64_t acceptedSnapshots = 0u;
    uint64_t rejectedFrames = 0u;
    uint64_t staleConnectionFrames = 0u;
    uint64_t outerDecodeFailures = 0u;
    uint64_t snapshotDecodeFailures = 0u;
    uint64_t snapshotIdentityFailures = 0u;
    uint64_t snapshotBufferFailures = 0u;
    uint64_t helmRequestsSent = 0u;
    uint64_t cargoRequestsSent = 0u;
    uint64_t characterInputsSent = 0u;
    uint64_t characterInputSamplesRetransmitted = 0u;
    uint64_t characterInputSamplesAcknowledged = 0u;
    uint64_t characterInputSamplesRetiredUnacknowledged = 0u;
    uint64_t characterInputHistoryPurges = 0u;
    uint64_t characterInputHistoryPurgedSamples = 0u;
    uint64_t actionEncodeFailures = 0u;
    uint64_t characterInputEncodeFailures = 0u;
    uint64_t sendFailures = 0u;
    uint64_t sentPacketBytes = 0u;
    uint64_t connectionSerialBindings = 0u;
    uint64_t localTransportDisconnects = 0u;
    uint64_t transportReplacements = 0u;
    uint32_t maximumFramesDrainedInPump = 0u;
    uint32_t maximumCharacterInputRedundancy = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterClientRuntimeTelemetry&) const = default;
};

struct WreckwaterClientPumpResult {
    uint32_t framesPolled = 0u;
    uint32_t snapshotsAccepted = 0u;
    uint32_t framesRejected = 0u;
    WreckwaterClientRuntimeError lastError =
        WreckwaterClientRuntimeError::None;
    WreckwaterCodecError lastSnapshotCodecError =
        WreckwaterCodecError::None;
    WreckwaterClientReplicationError lastReplicationError =
        WreckwaterClientReplicationError::None;

    [[nodiscard]] explicit operator bool() const noexcept {
        return lastError == WreckwaterClientRuntimeError::None;
    }
};

struct WreckwaterClientActionSendResult {
    // Global outer request ordering. This is PacketHeader::sequence for every
    // action and movement packet.
    uint64_t clientRequestSequence = 0u;
    // Nonzero only for character input. This independently orders movement
    // within one authoritative connection generation.
    uint64_t characterInputSequence = 0u;
    WreckwaterClientRuntimeError error =
        WreckwaterClientRuntimeError::None;
    WreckwaterCodecError codecError = WreckwaterCodecError::None;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterClientRuntimeError::None;
    }
};

class WreckwaterClientRuntime {
public:
    struct Config {
        // NativeTcpClientTransport represents its remote server as peer 0.
        // Its authenticated nonzero client peer ID is server-side only.
        uint32_t serverPeerId = 0u;
        // Zero waits for the first ordered Connected frame to bind the
        // server-issued serial. Nonzero pins an already-connected transport.
        uint64_t serverConnectionSerial = 0u;
        uint64_t sessionId = 0u;
        uint64_t matchId = 0u;
        uint64_t worldId = 0u;
        uint32_t worldEpoch = 0u;
        uint32_t authorityEpoch = 0u;
        // Zero leaves the high-level local-character path disabled. A
        // nonzero player ID is bound only from an accepted certified
        // snapshot, never from caller-supplied handle/generation data.
        uint64_t localPlayerId = 0u;
        uint32_t maximumFramesPerPump = 64u;
        uint64_t firstClientRequestSequence = 1u;
        WreckwaterClientSnapshotBuffer::Config snapshotBuffer{};
    };

    WreckwaterClientRuntime(
        Config config, std::unique_ptr<IMultiplayerTransport> transport);
    ~WreckwaterClientRuntime() = default;

    WreckwaterClientRuntime(const WreckwaterClientRuntime&) = delete;
    WreckwaterClientRuntime& operator=(
        const WreckwaterClientRuntime&) = delete;
    WreckwaterClientRuntime(WreckwaterClientRuntime&&) = delete;
    WreckwaterClientRuntime& operator=(
        WreckwaterClientRuntime&&) = delete;

    // Calls transport.service() exactly once, then drains at most the
    // configured number of already-decoded frames.
    [[nodiscard]] WreckwaterClientPumpResult pump();

    // Helm is a realtime input sample. The caller invokes this once per desired
    // input cadence; the runtime does not invent a clock or repeat old input.
    [[nodiscard]] WreckwaterClientActionSendResult sendHelm(
        uint64_t requestedApplicationTick,
        int16_t throttleQ15, int16_t steeringQ15);

    // Cargo actions are reliable events. Only Tow, Cut, Steal, and Bank are
    // accepted here.
    [[nodiscard]] WreckwaterClientActionSendResult sendCargoAction(
        uint64_t requestedApplicationTick, WreckwaterAction action,
        uint32_t cargoId, uint32_t cargoGeneration,
        uint32_t observedCargoRevision);

    // Character movement shares the realtime lane with helm input. Each packet
    // carries the current logical sample plus up to three exact retained
    // samples. Multiple controls may target the same tick. The packet keeps
    // global request ordering while movement owns a separate
    // per-connection-generation sequence for authority replacement and
    // snapshot acknowledgement. This identity-taking entry point is retained
    // for protocol tests and tooling; product input should use
    // sendLocalCharacterInput().
    [[nodiscard]] WreckwaterClientActionSendResult sendCharacterInput(
        uint64_t requestedApplicationTick,
        uint32_t characterHandle, uint32_t connectionGeneration,
        int16_t moveXQ15, int16_t moveZQ15,
        bool jump, bool board);

    // Product-facing movement API. Identity comes only from an accepted
    // snapshot for Config::localPlayerId. It stays unavailable after a
    // transport replacement until a strictly newer authoritative connection
    // generation is observed.
    [[nodiscard]] WreckwaterClientActionSendResult
    sendLocalCharacterInput(
        uint64_t requestedApplicationTick,
        int16_t moveXQ15, int16_t moveZQ15,
        bool jump, bool board);

    [[nodiscard]] WreckwaterClientSampleResult sampleVisual(
        WreckwaterPhysicsRenderTick physicsRenderTick) const noexcept {
        return snapshots_.sample(physicsRenderTick);
    }
    [[nodiscard]] WreckwaterClientSampleResult latestSample()
        const noexcept;

    // Explicitly closes and fences the known active serial. This is the local
    // lifecycle transition used for a deliberate reconnect; it does not
    // synthesize a network frame.
    [[nodiscard]] bool disconnectForTransportReplacement();

    // Replaces a transport only after an exact remote Disconnected frame or
    // disconnectForTransportReplacement(). Snapshot history and request
    // sequencing remain intact. The replacement stays inactive until
    // Connected supplies a strictly newer nonzero serial.
    [[nodiscard]] bool replaceTransport(
        std::unique_ptr<IMultiplayerTransport> transport);
    void close();

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool connectionActive() const noexcept {
        return connectionActive_ && !closed_;
    }
    [[nodiscard]] bool hasSnapshot() const noexcept {
        return hasSnapshot_;
    }
    [[nodiscard]] uint64_t latestPhysicsEvidenceTick() const noexcept {
        return latestPhysicsEvidenceTick_;
    }
    [[nodiscard]] uint64_t nextClientRequestSequence() const noexcept {
        return requestSequenceAvailable_ ? nextClientRequestSequence_ : 0u;
    }
    [[nodiscard]] uint64_t nextCharacterInputSequence() const noexcept {
        return characterInputSequenceAvailable_
                && (config_.localPlayerId == 0u
                    || localCharacterBindingState_
                        == WreckwaterLocalCharacterBindingState::Ready)
            ? nextCharacterInputSequence_ : 0u;
    }
    [[nodiscard]] WreckwaterLocalCharacterBindingState
    localCharacterBindingState() const noexcept {
        return localCharacterBindingState_;
    }
    [[nodiscard]] uint32_t localCharacterHandle() const noexcept {
        return localCharacterBindingState_
                == WreckwaterLocalCharacterBindingState::Ready
            ? localCharacterHandle_ : 0u;
    }
    [[nodiscard]] uint32_t
    localCharacterConnectionGeneration() const noexcept {
        return localCharacterBindingState_
                == WreckwaterLocalCharacterBindingState::Ready
            ? localCharacterConnectionGenerationHighWater_ : 0u;
    }
    [[nodiscard]] uint64_t serverConnectionSerial() const noexcept {
        return currentConnectionSerial_;
    }
    [[nodiscard]] uint32_t pendingCharacterInputCount() const noexcept {
        return characterInputHistoryCount_;
    }
    [[nodiscard]] const WreckwaterClientRuntimeTelemetry& telemetry()
        const noexcept {
        return telemetry_;
    }
    [[nodiscard]] const WreckwaterClientSnapshotBuffer& snapshotBuffer()
        const noexcept {
        return snapshots_;
    }

private:
    struct FrameProcessResult {
        WreckwaterClientRuntimeError error =
            WreckwaterClientRuntimeError::None;
        WreckwaterCodecError snapshotCodecError =
            WreckwaterCodecError::None;
        WreckwaterClientReplicationError replicationError =
            WreckwaterClientReplicationError::None;
        bool snapshotAccepted = false;
    };

    [[nodiscard]] FrameProcessResult processFrame(
        const MultiplayerTransportFrame& frame);
    [[nodiscard]] WreckwaterClientActionSendResult sendAction(
        WreckwaterActionRequest request, PacketPayloadType payloadType,
        DeliveryClass delivery);
    void observeLocalCharacterBinding(
        const WreckwaterCertifiedSnapshot& snapshot) noexcept;
    void invalidateLocalCharacterBinding() noexcept;
    void acknowledgeCharacterInputs(uint64_t sequence) noexcept;
    void purgeCharacterInputHistory() noexcept;

    Config config_{};
    std::unique_ptr<IMultiplayerTransport> transport_;
    WreckwaterClientSnapshotBuffer snapshots_;
    AckWindow snapshotAckWindow_{};
    uint64_t nextClientRequestSequence_ = 0u;
    uint64_t nextCharacterInputSequence_ = 1u;
    uint64_t lastActionRequestedApplicationTick_ = 0u;
    uint64_t lastCharacterRequestedApplicationTick_ = 0u;
    uint64_t latestPhysicsEvidenceTick_ = 0u;
    uint64_t currentConnectionSerial_ = 0u;
    uint64_t connectionSerialHighWater_ = 0u;
    uint32_t characterInputHandle_ = 0u;
    uint32_t characterInputConnectionGeneration_ = 0u;
    uint32_t localCharacterHandle_ = 0u;
    uint32_t localCharacterConnectionGenerationHighWater_ = 0u;
    std::array<
        WreckwaterCharacterInputSample,
        kWreckwaterCharacterInputMaximumRedundantSamples>
        characterInputHistory_{};
    uint32_t characterInputHistoryCount_ = 0u;
    WreckwaterLocalCharacterBindingState localCharacterBindingState_ =
        WreckwaterLocalCharacterBindingState::Disabled;
    bool initialized_ = false;
    bool connectionActive_ = false;
    bool connectionEnded_ = false;
    bool closed_ = false;
    bool requestSequenceAvailable_ = false;
    bool characterInputSequenceAvailable_ = true;
    bool characterInputIdentityBound_ = false;
    bool hasActionRequestedApplicationTick_ = false;
    bool hasCharacterRequestedApplicationTick_ = false;
    bool hasSnapshot_ = false;
    WreckwaterClientRuntimeTelemetry telemetry_{};
};

} // namespace voxy::network
