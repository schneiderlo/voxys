#pragma once

#include "network/session_transport.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace voxy::network {

struct DeterministicAdversityPolicy {
    uint32_t minimumDelayServiceQuanta = 0u;
    uint32_t maximumDelayServiceQuanta = 0u;
    uint32_t realtimeDropPermille = 0u;
    uint32_t realtimeDuplicatePermille = 0u;
    uint32_t reliableDuplicatePermille = 0u;
    uint32_t reorderPermille = 0u;
};

struct DeterministicAdversityTransportConfig {
    uint64_t seed = 1u;
    uint32_t maximumPeers = 1u;
    uint32_t maximumQueuedDataFramesPerPeer = 64u;
    uint32_t maximumQueuedLifecycleFramesPerPeer = 4u;
    size_t maximumFrameBytes = kConservativeRealtimeMtu;
    uint32_t maximumUnderlyingFramesPerService = 64u;
    uint32_t maximumOutgoingFramesPerService = 64u;
    uint64_t initialServiceQuantum = 0u;
    uint64_t firstScheduleSequence = 1u;
    // Holds at most one selected incoming realtime loss per peer. A later
    // data frame confirms that loss. If Disconnected arrives first, the
    // holdback is delivered before it so terminal state can converge.
    bool preserveLastRealtimeBeforeDisconnect = false;
    DeterministicAdversityPolicy incoming{};
    DeterministicAdversityPolicy outgoing{};
};

enum class DeterministicAdversityTransportError : uint32_t {
    None = 0u,
    ServiceQuantumExhausted,
    ScheduleSequenceExhausted,
    PeerCapacityExceeded,
    OversizedReliableInboundFrame,
    ReliableInboundCapacityExceeded,
    LifecycleCapacityExceeded,
    InvalidLifecycleFrame,
    InvalidLifecycleOrder,
};

[[nodiscard]] const char* deterministicAdversityTransportErrorName(
    DeterministicAdversityTransportError error) noexcept;

struct DeterministicAdversityTransportTelemetry {
    uint64_t serviceCalls = 0u;
    uint64_t underlyingFramesPolled = 0u;
    uint64_t inboundFramesQueued = 0u;
    uint64_t outboundFramesQueued = 0u;
    uint64_t inboundFramesDelivered = 0u;
    uint64_t outboundFramesDelivered = 0u;
    uint64_t inboundRealtimeDrops = 0u;
    uint64_t outboundRealtimeDrops = 0u;
    uint64_t outboundRealtimeSupersessions = 0u;
    uint64_t inboundRealtimeDisconnectRecoveries = 0u;
    uint64_t inboundDuplicatesQueued = 0u;
    uint64_t outboundDuplicatesQueued = 0u;
    uint64_t inboundRealtimeDuplicatesQueued = 0u;
    uint64_t inboundReliableDuplicatesQueued = 0u;
    uint64_t outboundRealtimeDuplicatesQueued = 0u;
    uint64_t outboundReliableDuplicatesQueued = 0u;
    uint64_t inboundDuplicateSuppressions = 0u;
    uint64_t outboundDuplicateSuppressions = 0u;
    uint64_t inboundReorders = 0u;
    uint64_t outboundReorders = 0u;
    uint64_t inboundReliableReorders = 0u;
    uint64_t outboundReliableReorders = 0u;
    uint64_t staleFramesPurged = 0u;
    uint64_t staleLifecycleFramesIgnored = 0u;
    uint64_t outboundBackpressure = 0u;
    uint64_t reliableSendRetries = 0u;
    uint64_t inboundDataHighWater = 0u;
    uint64_t inboundLifecycleHighWater = 0u;
    uint64_t outboundDataHighWater = 0u;
    uint64_t closePurgedFrames = 0u;
    uint64_t scheduleSequenceExhaustions = 0u;
    uint64_t faults = 0u;

    [[nodiscard]] bool operator==(
        const DeterministicAdversityTransportTelemetry&) const = default;
};

void mergeDeterministicAdversityTransportTelemetry(
    DeterministicAdversityTransportTelemetry& destination,
    const DeterministicAdversityTransportTelemetry& source) noexcept;

// A bounded application-frame scheduler. It intentionally models adversity
// above an existing transport; it is not a network congestion simulator.
class DeterministicAdversityTransport final
    : public IMultiplayerTransport {
public:
    ~DeterministicAdversityTransport() override;

    DeterministicAdversityTransport(
        const DeterministicAdversityTransport&) = delete;
    DeterministicAdversityTransport& operator=(
        const DeterministicAdversityTransport&) = delete;

    [[nodiscard]] static std::unique_ptr<
        DeterministicAdversityTransport>
    create(
        std::unique_ptr<IMultiplayerTransport> underlying,
        DeterministicAdversityTransportConfig config,
        std::string* error = nullptr);

    void service() override;

    [[nodiscard]] bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool sendLatestRealtime(
        uint32_t peerId, uint64_t connectionSerial,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool acceptConnection(
        uint32_t peerId, uint64_t connectionSerial) override;
    void rejectConnection(
        uint32_t peerId, uint64_t connectionSerial) override;
    [[nodiscard]] std::optional<MultiplayerTransportFrame> poll() override;
    void close() override;

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] DeterministicAdversityTransportError
    lastError() const noexcept;
    [[nodiscard]] uint64_t serviceQuantum() const noexcept;
    [[nodiscard]] uint64_t queuedInboundFrames() const noexcept;
    [[nodiscard]] uint64_t queuedOutboundFrames() const noexcept;
    [[nodiscard]] const DeterministicAdversityTransportTelemetry&
    telemetry() const noexcept;

private:
    struct State;
    explicit DeterministicAdversityTransport(
        std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace voxy::network
