#include "network/deterministic_adversity_transport.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

constexpr uint32_t kMaximumAdversityPeers = 4'096u;
constexpr uint32_t kMaximumAdversityFramesPerPeer = 4'096u;
constexpr uint32_t kMaximumAdversityLifecycleFramesPerPeer = 16u;
constexpr uint64_t kMaximumAdversitySlots = 65'536u;
constexpr uint64_t kMaximumAdversityReservedBytes =
    256u * 1024u * 1024u;

void incrementSaturated(uint64_t& value, uint64_t amount = 1u) noexcept {
    value = value > std::numeric_limits<uint64_t>::max() - amount
        ? std::numeric_limits<uint64_t>::max()
        : value + amount;
}

bool validPolicy(const DeterministicAdversityPolicy& policy) noexcept {
    return policy.minimumDelayServiceQuanta
            <= policy.maximumDelayServiceQuanta
        && policy.realtimeDropPermille <= 1'000u
        && policy.realtimeDuplicatePermille <= 1'000u
        && policy.reliableDuplicatePermille <= 1'000u
        && policy.reorderPermille <= 1'000u;
}

bool validConfig(
    const DeterministicAdversityTransportConfig& config,
    std::string* error) {
    if (config.maximumPeers == 0u
        || config.maximumPeers > kMaximumAdversityPeers
        || config.maximumQueuedDataFramesPerPeer == 0u
        || config.maximumQueuedDataFramesPerPeer
            > kMaximumAdversityFramesPerPeer
        || config.maximumQueuedLifecycleFramesPerPeer < 2u
        || config.maximumQueuedLifecycleFramesPerPeer
            > kMaximumAdversityLifecycleFramesPerPeer
        || config.maximumFrameBytes == 0u
        || config.maximumFrameBytes > kMaximumReliableFrameBytes
        || config.maximumUnderlyingFramesPerService == 0u
        || config.maximumOutgoingFramesPerService == 0u
        || config.firstScheduleSequence == 0u
        || !validPolicy(config.incoming)
        || !validPolicy(config.outgoing)) {
        if (error != nullptr)
            *error = "invalid deterministic adversity configuration";
        return false;
    }
    const uint64_t slotsPerPeer =
        uint64_t{config.maximumQueuedDataFramesPerPeer} * 2u
        + config.maximumQueuedLifecycleFramesPerPeer + 1u;
    if (slotsPerPeer > kMaximumAdversitySlots
        || config.maximumPeers
            > kMaximumAdversitySlots / slotsPerPeer) {
        if (error != nullptr)
            *error = "deterministic adversity slot budget is too large";
        return false;
    }
    const uint64_t totalSlots =
        uint64_t{config.maximumPeers} * slotsPerPeer;
    if (config.maximumFrameBytes
            > kMaximumAdversityReservedBytes
                / std::max<uint64_t>(totalSlots, 1u)) {
        if (error != nullptr)
            *error = "deterministic adversity byte budget is too large";
        return false;
    }
    return true;
}

struct ScheduledFrame {
    bool occupied = false;
    uint32_t peerId = 0u;
    uint64_t connectionSerial = 0u;
    DeliveryClass delivery = DeliveryClass::Realtime;
    MultiplayerTransportFrameType type =
        MultiplayerTransportFrameType::Data;
    uint64_t dueQuantum = 0u;
    uint64_t scheduleSequence = 0u;
    std::vector<std::byte> bytes;
};

struct PeerLane {
    bool assigned = false;
    uint32_t peerId = 0u;
    uint64_t connectionSerial = 0u;
    uint64_t incomingOrderedTail = 0u;
    uint64_t outgoingOrderedTail = 0u;
    uint64_t connectedDueQuantum = 0u;
    uint32_t inboundDataCount = 0u;
    uint32_t inboundLifecycleCount = 0u;
    uint32_t outboundDataCount = 0u;
    bool connectedSeen = false;
    bool connectedDelivered = false;
    bool disconnectedSeen = false;
    bool recoverHeldRealtime = false;
    std::vector<ScheduledFrame> incoming;
    std::vector<ScheduledFrame> outgoing;
    ScheduledFrame heldRealtime;
};

uint64_t occupiedCount(
    const std::vector<ScheduledFrame>& frames) noexcept {
    uint64_t count = 0u;
    for (const ScheduledFrame& frame : frames) {
        if (frame.occupied) ++count;
    }
    return count;
}

void clearFrame(ScheduledFrame& frame) noexcept {
    frame.occupied = false;
    frame.peerId = 0u;
    frame.connectionSerial = 0u;
    frame.delivery = DeliveryClass::Realtime;
    frame.type = MultiplayerTransportFrameType::Data;
    frame.dueQuantum = 0u;
    frame.scheduleSequence = 0u;
    frame.bytes.clear();
}

} // namespace

struct DeterministicAdversityTransport::State {
    std::unique_ptr<IMultiplayerTransport> underlying;
    DeterministicAdversityTransportConfig config;
    std::vector<PeerLane> peers;
    DeterministicAdversityTransportTelemetry telemetry;
    DeterministicAdversityTransportError lastError =
        DeterministicAdversityTransportError::None;
    uint64_t randomState = 0u;
    uint64_t quantum = 0u;
    uint64_t nextScheduleSequence = 1u;
    uint64_t inboundQueued = 0u;
    uint64_t outboundQueued = 0u;
    bool scheduleSequenceAvailable = true;
    bool isClosed = false;
    bool isFaulted = false;

    [[nodiscard]] uint64_t random() noexcept {
        randomState += 0x9e3779b97f4a7c15ull;
        uint64_t value = randomState;
        value = (value ^ (value >> 30u))
            * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u))
            * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    [[nodiscard]] bool chance(uint32_t permille) noexcept {
        if (permille == 0u) return false;
        if (permille == 1'000u) return true;
        return random() % 1'000u < permille;
    }

    [[nodiscard]] uint32_t randomDelay(
        const DeterministicAdversityPolicy& policy) noexcept {
        if (policy.minimumDelayServiceQuanta
                == policy.maximumDelayServiceQuanta) {
            return policy.minimumDelayServiceQuanta;
        }
        const uint64_t range =
            uint64_t{policy.maximumDelayServiceQuanta}
            - policy.minimumDelayServiceQuanta + 1u;
        return policy.minimumDelayServiceQuanta
            + static_cast<uint32_t>(random() % range);
    }

    [[nodiscard]] bool nextTicket(uint64_t& ticket) noexcept {
        if (!scheduleSequenceAvailable) {
            incrementSaturated(
                telemetry.scheduleSequenceExhaustions);
            return false;
        }
        ticket = nextScheduleSequence;
        if (nextScheduleSequence
                == std::numeric_limits<uint64_t>::max()) {
            scheduleSequenceAvailable = false;
        } else {
            ++nextScheduleSequence;
        }
        return true;
    }

    [[nodiscard]] PeerLane* lane(
        uint32_t peerId, bool create) noexcept {
        for (PeerLane& candidate : peers) {
            if (candidate.assigned
                && candidate.peerId == peerId) {
                return &candidate;
            }
        }
        if (!create) return nullptr;
        for (PeerLane& candidate : peers) {
            if (!candidate.assigned) {
                candidate.assigned = true;
                candidate.peerId = peerId;
                return &candidate;
            }
        }
        return nullptr;
    }

    void purgeLane(PeerLane& lane) noexcept {
        uint64_t purged = 0u;
        const uint64_t held =
            lane.heldRealtime.occupied ? 1u : 0u;
        for (ScheduledFrame& frame : lane.incoming) {
            if (!frame.occupied) continue;
            ++purged;
            clearFrame(frame);
        }
        for (ScheduledFrame& frame : lane.outgoing) {
            if (!frame.occupied) continue;
            ++purged;
            clearFrame(frame);
        }
        if (lane.heldRealtime.occupied) {
            ++purged;
            clearFrame(lane.heldRealtime);
        }
        inboundQueued -=
            uint64_t{lane.inboundDataCount}
            + lane.inboundLifecycleCount + held;
        outboundQueued -= lane.outboundDataCount;
        lane.inboundDataCount = 0u;
        lane.inboundLifecycleCount = 0u;
        lane.outboundDataCount = 0u;
        lane.incomingOrderedTail = quantum;
        lane.outgoingOrderedTail = quantum;
        lane.connectedDueQuantum = quantum;
        lane.connectedSeen = false;
        lane.connectedDelivered = false;
        lane.disconnectedSeen = false;
        lane.recoverHeldRealtime = false;
        incrementSaturated(telemetry.staleFramesPurged, purged);
    }

    void clearQueues(bool closing) noexcept {
        uint64_t purged = 0u;
        for (PeerLane& lane : peers) {
            purged += occupiedCount(lane.incoming);
            purged += occupiedCount(lane.outgoing);
            if (lane.heldRealtime.occupied) ++purged;
            for (ScheduledFrame& frame : lane.incoming)
                clearFrame(frame);
            for (ScheduledFrame& frame : lane.outgoing)
                clearFrame(frame);
            clearFrame(lane.heldRealtime);
            lane.inboundDataCount = 0u;
            lane.inboundLifecycleCount = 0u;
            lane.outboundDataCount = 0u;
            lane.connectedSeen = false;
            lane.connectedDelivered = false;
            lane.disconnectedSeen = false;
            lane.recoverHeldRealtime = false;
        }
        inboundQueued = 0u;
        outboundQueued = 0u;
        if (closing)
            incrementSaturated(telemetry.closePurgedFrames, purged);
    }

    void close() noexcept {
        if (isClosed) return;
        clearQueues(true);
        underlying->close();
        isClosed = true;
    }

    void fault(DeterministicAdversityTransportError error) noexcept {
        if (isFaulted || isClosed) return;
        isFaulted = true;
        lastError = error;
        incrementSaturated(telemetry.faults);
        close();
    }

    [[nodiscard]] ScheduledFrame* freeIncomingSlot(
        PeerLane& lane, MultiplayerTransportFrameType type) noexcept {
        const bool data =
            type == MultiplayerTransportFrameType::Data;
        if (data
            && lane.inboundDataCount
                >= config.maximumQueuedDataFramesPerPeer) {
            return nullptr;
        }
        if (!data
            && lane.inboundLifecycleCount
                >= config.maximumQueuedLifecycleFramesPerPeer) {
            return nullptr;
        }
        for (ScheduledFrame& frame : lane.incoming) {
            if (!frame.occupied) return &frame;
        }
        return nullptr;
    }

    [[nodiscard]] ScheduledFrame* freeOutgoingSlot(
        PeerLane& lane) noexcept {
        if (lane.outboundDataCount
            >= config.maximumQueuedDataFramesPerPeer) {
            return nullptr;
        }
        for (ScheduledFrame& frame : lane.outgoing) {
            if (!frame.occupied) return &frame;
        }
        return nullptr;
    }

    [[nodiscard]] bool computeDue(
        const DeterministicAdversityPolicy& policy,
        uint64_t orderedTail, bool allowReorder,
        uint64_t& due, bool& reordered) noexcept {
        const uint32_t delay = randomDelay(policy);
        if (quantum > std::numeric_limits<uint64_t>::max() - delay) {
            fault(
                DeterministicAdversityTransportError::
                    ServiceQuantumExhausted);
            return false;
        }
        const uint64_t raw = quantum + delay;
        const bool reorder = allowReorder
            && chance(policy.reorderPermille);
        due = reorder ? raw : std::max(raw, orderedTail);
        reordered = reorder && due < orderedTail;
        return true;
    }

    [[nodiscard]] bool fillScheduled(
        ScheduledFrame& scheduled,
        const MultiplayerTransportFrame& source,
        uint64_t due, uint64_t ticket) noexcept {
        if (source.bytes.size() > config.maximumFrameBytes)
            return false;
        scheduled.occupied = true;
        scheduled.peerId = source.peerId;
        scheduled.connectionSerial = source.connectionSerial;
        scheduled.delivery = source.delivery;
        scheduled.type = source.type;
        scheduled.dueQuantum = due;
        scheduled.scheduleSequence = ticket;
        scheduled.bytes.assign(
            source.bytes.begin(), source.bytes.end());
        return true;
    }

    [[nodiscard]] bool fillScheduled(
        ScheduledFrame& scheduled, uint32_t peerId,
        uint64_t connectionSerial, DeliveryClass delivery,
        std::span<const std::byte> bytes,
        uint64_t due, uint64_t ticket) noexcept {
        if (bytes.size() > config.maximumFrameBytes) return false;
        scheduled.occupied = true;
        scheduled.peerId = peerId;
        scheduled.connectionSerial = connectionSerial;
        scheduled.delivery = delivery;
        scheduled.type = MultiplayerTransportFrameType::Data;
        scheduled.dueQuantum = due;
        scheduled.scheduleSequence = ticket;
        scheduled.bytes.assign(bytes.begin(), bytes.end());
        return true;
    }

    void updateInboundHighWater(const PeerLane& lane) noexcept {
        telemetry.inboundDataHighWater = std::max(
            telemetry.inboundDataHighWater,
            uint64_t{lane.inboundDataCount});
        telemetry.inboundLifecycleHighWater = std::max(
            telemetry.inboundLifecycleHighWater,
            uint64_t{lane.inboundLifecycleCount});
    }

    void updateOutboundHighWater(const PeerLane& lane) noexcept {
        telemetry.outboundDataHighWater = std::max(
            telemetry.outboundDataHighWater,
            uint64_t{lane.outboundDataCount});
    }

    [[nodiscard]] bool scheduleIncomingCopy(
        PeerLane& lane, const MultiplayerTransportFrame& frame,
        uint64_t due, bool duplicate) noexcept {
        ScheduledFrame* slot =
            freeIncomingSlot(lane, frame.type);
        uint64_t ticket = 0u;
        if (slot == nullptr || !nextTicket(ticket)) {
            if (duplicate) {
                incrementSaturated(
                    telemetry.inboundDuplicateSuppressions);
                return true;
            }
            return false;
        }
        if (!fillScheduled(*slot, frame, due, ticket))
            return false;
        if (frame.isData()) {
            ++lane.inboundDataCount;
        } else {
            ++lane.inboundLifecycleCount;
        }
        ++inboundQueued;
        incrementSaturated(telemetry.inboundFramesQueued);
        if (duplicate)
            incrementSaturated(telemetry.inboundDuplicatesQueued);
        if (duplicate
            && frame.delivery == DeliveryClass::Realtime) {
            incrementSaturated(
                telemetry.inboundRealtimeDuplicatesQueued);
        } else if (duplicate) {
            incrementSaturated(
                telemetry.inboundReliableDuplicatesQueued);
        }
        updateInboundHighWater(lane);
        return true;
    }

    void discardHeldRealtime(PeerLane& lane) noexcept {
        if (!lane.heldRealtime.occupied) return;
        --inboundQueued;
        clearFrame(lane.heldRealtime);
        lane.recoverHeldRealtime = false;
    }

    [[nodiscard]] bool holdRealtimeForDisconnect(
        PeerLane& lane,
        const MultiplayerTransportFrame& frame) noexcept {
        if (!config.preserveLastRealtimeBeforeDisconnect)
            return false;
        uint64_t due = 0u;
        bool reordered = false;
        if (!computeDue(
                config.incoming, lane.incomingOrderedTail,
                false, due, reordered)) {
            return false;
        }
        if (frame.connectionSerial != 0u)
            due = std::max(due, lane.connectedDueQuantum);
        uint64_t ticket = 0u;
        if (!nextTicket(ticket)) return false;
        if (!fillScheduled(
                lane.heldRealtime, frame, due, ticket)) {
            return false;
        }
        lane.recoverHeldRealtime = false;
        lane.incomingOrderedTail =
            std::max(lane.incomingOrderedTail, due);
        ++inboundQueued;
        incrementSaturated(telemetry.inboundFramesQueued);
        return true;
    }

    void ingestConnected(
        PeerLane& lane,
        const MultiplayerTransportFrame& frame) noexcept {
        if (frame.connectionSerial == 0u
            || !frame.bytes.empty()) {
            fault(
                DeterministicAdversityTransportError::
                    InvalidLifecycleFrame);
            return;
        }
        if (lane.connectionSerial != 0u) {
            if (frame.connectionSerial <= lane.connectionSerial) {
                incrementSaturated(
                    telemetry.staleLifecycleFramesIgnored);
                return;
            }
            purgeLane(lane);
        }
        lane.connectionSerial = frame.connectionSerial;
        lane.connectedSeen = true;
        lane.connectedDelivered = false;
        lane.disconnectedSeen = false;

        uint64_t due = 0u;
        bool reordered = false;
        if (!computeDue(
                config.incoming, lane.incomingOrderedTail,
                false, due, reordered)) {
            return;
        }
        lane.incomingOrderedTail =
            std::max(lane.incomingOrderedTail, due);
        lane.connectedDueQuantum = due;
        if (!scheduleIncomingCopy(lane, frame, due, false)) {
            fault(
                scheduleSequenceAvailable
                    ? DeterministicAdversityTransportError::
                        LifecycleCapacityExceeded
                    : DeterministicAdversityTransportError::
                        ScheduleSequenceExhausted);
        }
    }

    void ingestDisconnected(
        PeerLane& lane,
        const MultiplayerTransportFrame& frame) noexcept {
        if (frame.connectionSerial == 0u
            || !frame.bytes.empty()) {
            fault(
                DeterministicAdversityTransportError::
                    InvalidLifecycleFrame);
            return;
        }
        if (!lane.connectedSeen
            || frame.connectionSerial != lane.connectionSerial
            || lane.disconnectedSeen) {
            incrementSaturated(
                telemetry.staleLifecycleFramesIgnored);
            return;
        }
        if (lane.heldRealtime.occupied) {
            lane.recoverHeldRealtime = true;
            incrementSaturated(
                telemetry.inboundRealtimeDisconnectRecoveries);
        }
        uint64_t due = 0u;
        bool reordered = false;
        if (!computeDue(
                config.incoming, lane.incomingOrderedTail,
                false, due, reordered)) {
            return;
        }
        for (const ScheduledFrame& queued : lane.incoming) {
            if (queued.occupied
                && queued.connectionSerial
                    == frame.connectionSerial) {
                due = std::max(due, queued.dueQuantum);
            }
        }
        if (lane.heldRealtime.occupied) {
            due = std::max(
                due, lane.heldRealtime.dueQuantum);
        }
        lane.incomingOrderedTail = due;
        if (!scheduleIncomingCopy(lane, frame, due, false)) {
            fault(
                scheduleSequenceAvailable
                    ? DeterministicAdversityTransportError::
                        LifecycleCapacityExceeded
                    : DeterministicAdversityTransportError::
                        ScheduleSequenceExhausted);
            return;
        }
        lane.disconnectedSeen = true;
    }

    void ingestData(
        PeerLane& lane,
        const MultiplayerTransportFrame& frame) noexcept {
        if (frame.connectionSerial != 0u) {
            if (!lane.connectedSeen) {
                fault(
                    DeterministicAdversityTransportError::
                        InvalidLifecycleOrder);
                return;
            }
            if (frame.connectionSerial != lane.connectionSerial) {
                incrementSaturated(telemetry.staleFramesPurged);
                return;
            }
            if (lane.disconnectedSeen) {
                fault(
                    DeterministicAdversityTransportError::
                        InvalidLifecycleOrder);
                return;
            }
        }
        discardHeldRealtime(lane);
        if (frame.bytes.size() > config.maximumFrameBytes) {
            if (frame.delivery == DeliveryClass::Realtime) {
                incrementSaturated(telemetry.inboundRealtimeDrops);
                return;
            }
            fault(
                DeterministicAdversityTransportError::
                    OversizedReliableInboundFrame);
            return;
        }
        if (frame.delivery == DeliveryClass::Realtime
            && chance(config.incoming.realtimeDropPermille)) {
            incrementSaturated(telemetry.inboundRealtimeDrops);
            (void)holdRealtimeForDisconnect(lane, frame);
            return;
        }
        if (lane.inboundDataCount
            >= config.maximumQueuedDataFramesPerPeer) {
            if (frame.delivery == DeliveryClass::Realtime) {
                incrementSaturated(telemetry.inboundRealtimeDrops);
                return;
            }
            fault(
                DeterministicAdversityTransportError::
                    ReliableInboundCapacityExceeded);
            return;
        }

        uint64_t due = 0u;
        bool reordered = false;
        if (!computeDue(
                config.incoming, lane.incomingOrderedTail,
                true, due, reordered)) {
            return;
        }
        if (frame.connectionSerial != 0u)
            due = std::max(due, lane.connectedDueQuantum);
        if (!scheduleIncomingCopy(lane, frame, due, false)) {
            fault(
                scheduleSequenceAvailable
                    ? DeterministicAdversityTransportError::
                        ReliableInboundCapacityExceeded
                    : DeterministicAdversityTransportError::
                        ScheduleSequenceExhausted);
            return;
        }
        if (reordered) {
            incrementSaturated(telemetry.inboundReorders);
            if (frame.delivery != DeliveryClass::Realtime) {
                incrementSaturated(
                    telemetry.inboundReliableReorders);
            }
        }
        lane.incomingOrderedTail =
            std::max(lane.incomingOrderedTail, due);

        const uint32_t duplicatePermille =
            frame.delivery == DeliveryClass::Realtime
            ? config.incoming.realtimeDuplicatePermille
            : config.incoming.reliableDuplicatePermille;
        if (!chance(duplicatePermille)) return;

        uint64_t duplicateDue = 0u;
        bool duplicateReordered = false;
        if (!computeDue(
                config.incoming, lane.incomingOrderedTail,
                true, duplicateDue, duplicateReordered)) {
            return;
        }
        if (frame.connectionSerial != 0u)
            duplicateDue = std::max(
                duplicateDue, lane.connectedDueQuantum);
        if (!scheduleIncomingCopy(
                lane, frame, duplicateDue, true)) {
            return;
        }
        if (duplicateReordered) {
            incrementSaturated(telemetry.inboundReorders);
            if (frame.delivery != DeliveryClass::Realtime) {
                incrementSaturated(
                    telemetry.inboundReliableReorders);
            }
        }
        lane.incomingOrderedTail =
            std::max(lane.incomingOrderedTail, duplicateDue);
    }

    void ingest(
        MultiplayerTransportFrame frame) noexcept {
        PeerLane* peerLane = lane(frame.peerId, true);
        if (peerLane == nullptr) {
            fault(
                DeterministicAdversityTransportError::
                    PeerCapacityExceeded);
            return;
        }
        switch (frame.type) {
            case MultiplayerTransportFrameType::Connected:
                ingestConnected(*peerLane, frame);
                return;
            case MultiplayerTransportFrameType::Disconnected:
                ingestDisconnected(*peerLane, frame);
                return;
            case MultiplayerTransportFrameType::Data:
                ingestData(*peerLane, frame);
                return;
        }
        fault(
            DeterministicAdversityTransportError::
                InvalidLifecycleFrame);
    }

    [[nodiscard]] bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) noexcept {
        if (isClosed || isFaulted
            || bytes.size() > config.maximumFrameBytes) {
            incrementSaturated(telemetry.outboundBackpressure);
            return false;
        }
        PeerLane* peerLane = lane(peerId, true);
        if (peerLane == nullptr) {
            incrementSaturated(telemetry.outboundBackpressure);
            return false;
        }
        if (connectionSerial != 0u
            && peerLane->connectionSerial != 0u
            && connectionSerial != peerLane->connectionSerial) {
            incrementSaturated(telemetry.outboundBackpressure);
            return false;
        }
        if (peerLane->outboundDataCount
            >= config.maximumQueuedDataFramesPerPeer
            || !scheduleSequenceAvailable) {
            if (!scheduleSequenceAvailable) {
                incrementSaturated(
                    telemetry.scheduleSequenceExhaustions);
            }
            incrementSaturated(telemetry.outboundBackpressure);
            return false;
        }
        if (delivery == DeliveryClass::Realtime
            && chance(config.outgoing.realtimeDropPermille)) {
            incrementSaturated(telemetry.outboundRealtimeDrops);
            return true;
        }

        ScheduledFrame* slot = freeOutgoingSlot(*peerLane);
        uint64_t ticket = 0u;
        if (slot == nullptr || !nextTicket(ticket)) {
            incrementSaturated(telemetry.outboundBackpressure);
            return false;
        }
        uint64_t due = 0u;
        bool reordered = false;
        if (!computeDue(
                config.outgoing,
                peerLane->outgoingOrderedTail,
                true, due, reordered)) {
            return false;
        }
        if (!fillScheduled(
                *slot, peerId, connectionSerial,
                delivery, bytes, due, ticket)) {
            incrementSaturated(telemetry.outboundBackpressure);
            return false;
        }
        ++peerLane->outboundDataCount;
        ++outboundQueued;
        incrementSaturated(telemetry.outboundFramesQueued);
        if (reordered) {
            incrementSaturated(telemetry.outboundReorders);
            if (delivery != DeliveryClass::Realtime) {
                incrementSaturated(
                    telemetry.outboundReliableReorders);
            }
        }
        peerLane->outgoingOrderedTail =
            std::max(peerLane->outgoingOrderedTail, due);
        updateOutboundHighWater(*peerLane);

        const uint32_t duplicatePermille =
            delivery == DeliveryClass::Realtime
            ? config.outgoing.realtimeDuplicatePermille
            : config.outgoing.reliableDuplicatePermille;
        if (!chance(duplicatePermille)) return true;

        ScheduledFrame* duplicateSlot =
            freeOutgoingSlot(*peerLane);
        uint64_t duplicateTicket = 0u;
        if (duplicateSlot == nullptr
            || !nextTicket(duplicateTicket)) {
            incrementSaturated(
                telemetry.outboundDuplicateSuppressions);
            return true;
        }
        uint64_t duplicateDue = 0u;
        bool duplicateReordered = false;
        if (!computeDue(
                config.outgoing,
                peerLane->outgoingOrderedTail,
                true, duplicateDue, duplicateReordered)) {
            return true;
        }
        if (!fillScheduled(
                *duplicateSlot, peerId, connectionSerial,
                delivery, bytes, duplicateDue,
                duplicateTicket)) {
            incrementSaturated(
                telemetry.outboundDuplicateSuppressions);
            return true;
        }
        ++peerLane->outboundDataCount;
        ++outboundQueued;
        incrementSaturated(telemetry.outboundFramesQueued);
        incrementSaturated(telemetry.outboundDuplicatesQueued);
        if (delivery == DeliveryClass::Realtime) {
            incrementSaturated(
                telemetry.outboundRealtimeDuplicatesQueued);
        } else {
            incrementSaturated(
                telemetry.outboundReliableDuplicatesQueued);
        }
        if (duplicateReordered) {
            incrementSaturated(telemetry.outboundReorders);
            if (delivery != DeliveryClass::Realtime) {
                incrementSaturated(
                    telemetry.outboundReliableReorders);
            }
        }
        peerLane->outgoingOrderedTail = std::max(
            peerLane->outgoingOrderedTail, duplicateDue);
        updateOutboundHighWater(*peerLane);
        return true;
    }

    [[nodiscard]] ScheduledFrame* nextDueOutgoing(
        PeerLane*& owner) noexcept {
        ScheduledFrame* selected = nullptr;
        owner = nullptr;
        for (PeerLane& lane : peers) {
            for (ScheduledFrame& frame : lane.outgoing) {
                if (!frame.occupied || frame.dueQuantum > quantum)
                    continue;
                if (selected == nullptr
                    || frame.dueQuantum < selected->dueQuantum
                    || (frame.dueQuantum == selected->dueQuantum
                        && frame.scheduleSequence
                            < selected->scheduleSequence)) {
                    selected = &frame;
                    owner = &lane;
                }
            }
        }
        return selected;
    }

    void flushOutgoing() noexcept {
        for (uint32_t sent = 0u;
             sent < config.maximumOutgoingFramesPerService;
             ++sent) {
            PeerLane* owner = nullptr;
            ScheduledFrame* frame = nextDueOutgoing(owner);
            if (frame == nullptr || owner == nullptr) return;
            const bool accepted =
                frame->connectionSerial == 0u
                ? underlying->send(
                    frame->peerId, frame->delivery, frame->bytes)
                : underlying->send(
                    frame->peerId, frame->connectionSerial,
                    frame->delivery, frame->bytes);
            if (!accepted
                && frame->delivery != DeliveryClass::Realtime) {
                incrementSaturated(telemetry.reliableSendRetries);
                return;
            }
            if (!accepted)
                incrementSaturated(telemetry.outboundRealtimeDrops);
            else
                incrementSaturated(
                    telemetry.outboundFramesDelivered);
            --owner->outboundDataCount;
            --outboundQueued;
            clearFrame(*frame);
        }
    }

    void service() noexcept {
        if (isClosed || isFaulted) return;
        if (quantum == std::numeric_limits<uint64_t>::max()) {
            fault(
                DeterministicAdversityTransportError::
                    ServiceQuantumExhausted);
            return;
        }
        ++quantum;
        incrementSaturated(telemetry.serviceCalls);
        underlying->service();
        for (uint32_t drained = 0u;
             drained < config.maximumUnderlyingFramesPerService;
             ++drained) {
            std::optional<MultiplayerTransportFrame> frame =
                underlying->poll();
            if (!frame.has_value()) break;
            incrementSaturated(telemetry.underlyingFramesPolled);
            ingest(std::move(*frame));
            if (isFaulted || isClosed) return;
        }
        flushOutgoing();
    }

    [[nodiscard]] ScheduledFrame* nextDueIncoming(
        PeerLane*& owner, bool& heldRealtime) noexcept {
        ScheduledFrame* selected = nullptr;
        owner = nullptr;
        heldRealtime = false;
        for (PeerLane& lane : peers) {
            for (ScheduledFrame& frame : lane.incoming) {
                if (!frame.occupied || frame.dueQuantum > quantum)
                    continue;
                if (selected == nullptr
                    || frame.dueQuantum < selected->dueQuantum
                    || (frame.dueQuantum == selected->dueQuantum
                        && frame.scheduleSequence
                            < selected->scheduleSequence)) {
                    selected = &frame;
                    owner = &lane;
                    heldRealtime = false;
                }
            }
            ScheduledFrame& held = lane.heldRealtime;
            if (lane.recoverHeldRealtime
                && held.occupied && held.dueQuantum <= quantum
                && (selected == nullptr
                    || held.dueQuantum < selected->dueQuantum
                    || (held.dueQuantum == selected->dueQuantum
                        && held.scheduleSequence
                            < selected->scheduleSequence))) {
                selected = &held;
                owner = &lane;
                heldRealtime = true;
            }
        }
        return selected;
    }

    [[nodiscard]] std::optional<MultiplayerTransportFrame>
    poll() noexcept {
        if (isClosed || isFaulted) return std::nullopt;
        PeerLane* owner = nullptr;
        bool heldRealtime = false;
        ScheduledFrame* scheduled =
            nextDueIncoming(owner, heldRealtime);
        if (scheduled == nullptr || owner == nullptr)
            return std::nullopt;
        MultiplayerTransportFrame frame;
        frame.peerId = scheduled->peerId;
        frame.connectionSerial = scheduled->connectionSerial;
        frame.delivery = scheduled->delivery;
        frame.type = scheduled->type;
        frame.bytes.assign(
            scheduled->bytes.begin(), scheduled->bytes.end());
        if (heldRealtime) {
            owner->recoverHeldRealtime = false;
        } else if (scheduled->type
                == MultiplayerTransportFrameType::Data) {
            --owner->inboundDataCount;
        } else {
            --owner->inboundLifecycleCount;
            if (scheduled->type
                    == MultiplayerTransportFrameType::Connected) {
                owner->connectedDelivered = true;
            } else {
                owner->connectedDelivered = false;
            }
        }
        --inboundQueued;
        clearFrame(*scheduled);
        incrementSaturated(telemetry.inboundFramesDelivered);
        return frame;
    }
};

const char* deterministicAdversityTransportErrorName(
    DeterministicAdversityTransportError error) noexcept {
    switch (error) {
        case DeterministicAdversityTransportError::None:
            return "none";
        case DeterministicAdversityTransportError::
                ServiceQuantumExhausted:
            return "service_quantum_exhausted";
        case DeterministicAdversityTransportError::
                ScheduleSequenceExhausted:
            return "schedule_sequence_exhausted";
        case DeterministicAdversityTransportError::
                PeerCapacityExceeded:
            return "peer_capacity_exceeded";
        case DeterministicAdversityTransportError::
                OversizedReliableInboundFrame:
            return "oversized_reliable_inbound_frame";
        case DeterministicAdversityTransportError::
                ReliableInboundCapacityExceeded:
            return "reliable_inbound_capacity_exceeded";
        case DeterministicAdversityTransportError::
                LifecycleCapacityExceeded:
            return "lifecycle_capacity_exceeded";
        case DeterministicAdversityTransportError::
                InvalidLifecycleFrame:
            return "invalid_lifecycle_frame";
        case DeterministicAdversityTransportError::
                InvalidLifecycleOrder:
            return "invalid_lifecycle_order";
    }
    return "unknown";
}

void mergeDeterministicAdversityTransportTelemetry(
    DeterministicAdversityTransportTelemetry& destination,
    const DeterministicAdversityTransportTelemetry& source) noexcept {
#define VOXY_MERGE_ADVERSITY_COUNTER(field) \
    incrementSaturated(destination.field, source.field)
    VOXY_MERGE_ADVERSITY_COUNTER(serviceCalls);
    VOXY_MERGE_ADVERSITY_COUNTER(underlyingFramesPolled);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundFramesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundFramesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundFramesDelivered);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundFramesDelivered);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundRealtimeDrops);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundRealtimeDrops);
    VOXY_MERGE_ADVERSITY_COUNTER(
        inboundRealtimeDisconnectRecoveries);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundDuplicatesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundDuplicatesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundRealtimeDuplicatesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundReliableDuplicatesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundRealtimeDuplicatesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundReliableDuplicatesQueued);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundDuplicateSuppressions);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundDuplicateSuppressions);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundReorders);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundReorders);
    VOXY_MERGE_ADVERSITY_COUNTER(inboundReliableReorders);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundReliableReorders);
    VOXY_MERGE_ADVERSITY_COUNTER(staleFramesPurged);
    VOXY_MERGE_ADVERSITY_COUNTER(staleLifecycleFramesIgnored);
    VOXY_MERGE_ADVERSITY_COUNTER(outboundBackpressure);
    VOXY_MERGE_ADVERSITY_COUNTER(reliableSendRetries);
    destination.inboundDataHighWater = std::max(
        destination.inboundDataHighWater,
        source.inboundDataHighWater);
    destination.inboundLifecycleHighWater = std::max(
        destination.inboundLifecycleHighWater,
        source.inboundLifecycleHighWater);
    destination.outboundDataHighWater = std::max(
        destination.outboundDataHighWater,
        source.outboundDataHighWater);
    VOXY_MERGE_ADVERSITY_COUNTER(closePurgedFrames);
    VOXY_MERGE_ADVERSITY_COUNTER(scheduleSequenceExhaustions);
    VOXY_MERGE_ADVERSITY_COUNTER(faults);
#undef VOXY_MERGE_ADVERSITY_COUNTER
}

DeterministicAdversityTransport::DeterministicAdversityTransport(
    std::unique_ptr<State> state)
    : state_(std::move(state)) {}

DeterministicAdversityTransport::~DeterministicAdversityTransport() {
    close();
}

std::unique_ptr<DeterministicAdversityTransport>
DeterministicAdversityTransport::create(
    std::unique_ptr<IMultiplayerTransport> underlying,
    DeterministicAdversityTransportConfig config,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (underlying == nullptr || !validConfig(config, error)) {
        if (error != nullptr && error->empty())
            *error = "deterministic adversity requires a transport";
        return nullptr;
    }
#if defined(__cpp_exceptions)
    try {
#endif
        auto state = std::make_unique<State>();
        state->underlying = std::move(underlying);
        state->config = config;
        state->randomState = config.seed;
        state->quantum = config.initialServiceQuantum;
        state->nextScheduleSequence =
            config.firstScheduleSequence;
        state->peers.resize(config.maximumPeers);
        const uint32_t incomingSlots =
            config.maximumQueuedDataFramesPerPeer
            + config.maximumQueuedLifecycleFramesPerPeer;
        for (PeerLane& lane : state->peers) {
            lane.incoming.resize(incomingSlots);
            lane.outgoing.resize(
                config.maximumQueuedDataFramesPerPeer);
            lane.heldRealtime.bytes.reserve(
                config.maximumFrameBytes);
            for (ScheduledFrame& frame : lane.incoming)
                frame.bytes.reserve(config.maximumFrameBytes);
            for (ScheduledFrame& frame : lane.outgoing)
                frame.bytes.reserve(config.maximumFrameBytes);
        }
        return std::unique_ptr<DeterministicAdversityTransport>(
            new DeterministicAdversityTransport(
                std::move(state)));
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        if (error != nullptr)
            *error =
                "deterministic adversity allocation failed";
        return nullptr;
    }
#endif
}

void DeterministicAdversityTransport::service() {
    if (state_) state_->service();
}

bool DeterministicAdversityTransport::send(
    uint32_t peerId, DeliveryClass delivery,
    std::span<const std::byte> bytes) {
    return state_
        && state_->send(peerId, 0u, delivery, bytes);
}

bool DeterministicAdversityTransport::send(
    uint32_t peerId, uint64_t connectionSerial,
    DeliveryClass delivery, std::span<const std::byte> bytes) {
    return state_
        && state_->send(
            peerId, connectionSerial, delivery, bytes);
}

std::optional<MultiplayerTransportFrame>
DeterministicAdversityTransport::poll() {
    return state_ ? state_->poll() : std::nullopt;
}

void DeterministicAdversityTransport::close() {
    if (state_) state_->close();
}

bool DeterministicAdversityTransport::faulted() const noexcept {
    return state_ && state_->isFaulted;
}

bool DeterministicAdversityTransport::closed() const noexcept {
    return !state_ || state_->isClosed;
}

DeterministicAdversityTransportError
DeterministicAdversityTransport::lastError() const noexcept {
    return state_
        ? state_->lastError
        : DeterministicAdversityTransportError::None;
}

uint64_t
DeterministicAdversityTransport::serviceQuantum() const noexcept {
    return state_ ? state_->quantum : 0u;
}

uint64_t
DeterministicAdversityTransport::queuedInboundFrames() const noexcept {
    return state_ ? state_->inboundQueued : 0u;
}

uint64_t
DeterministicAdversityTransport::queuedOutboundFrames() const noexcept {
    return state_ ? state_->outboundQueued : 0u;
}

const DeterministicAdversityTransportTelemetry&
DeterministicAdversityTransport::telemetry() const noexcept {
    static const DeterministicAdversityTransportTelemetry empty{};
    return state_ ? state_->telemetry : empty;
}

} // namespace voxy::network
