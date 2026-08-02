#pragma once

#include "network/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::network {

// Data is the source-compatible default. Lifecycle-aware transports publish a
// nonzero connection serial with every frame. A Connected frame is ordered
// before that connection's first Data frame; Disconnected is ordered after its
// last retained Data frame.
enum class MultiplayerTransportFrameType : uint32_t {
    Data = 0,
    Connected = 1,
    Disconnected = 2,
    // Authentication succeeded, but the transport has not replaced an
    // existing same-peer connection. The session must accept or reject this
    // exact serial before the candidate can exchange data.
    ConnectionRequested = 3,
};

struct MultiplayerTransportFrame {
    uint32_t peerId = 0;
    DeliveryClass delivery = DeliveryClass::Realtime;
    std::vector<std::byte> bytes;
    MultiplayerTransportFrameType type =
        MultiplayerTransportFrameType::Data;
    uint64_t connectionSerial = 0;

    [[nodiscard]] bool isData() const noexcept {
        return type == MultiplayerTransportFrameType::Data;
    }

    [[nodiscard]] bool operator==(
        const MultiplayerTransportFrame&) const = default;
};

class IMultiplayerTransport {
public:
    virtual ~IMultiplayerTransport() = default;

    // Performs one bounded, nonblocking progress quantum. Session code calls
    // this exactly once before draining a bounded number of already-decoded
    // frames. poll() itself must not perform hidden I/O work.
    virtual void service() {}

    // Sends to the transport's currently active connection for peerId.
    [[nodiscard]] virtual bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) = 0;

    // Fails closed by default for a nonzero serial. Lifecycle-aware transports
    // override this so a response derived from an old inbound frame can never
    // cross a same-ID reconnect boundary.
    [[nodiscard]] virtual bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery, std::span<const std::byte> bytes) {
        if (connectionSerial != 0u) return false;
        return send(peerId, delivery, bytes);
    }

    // Realtime state supersedes older, wholly-unsent realtime state for the
    // same connection. Transports without an internal queue safely fall back
    // to send(); queued transports override this to prevent stale backlog.
    [[nodiscard]] virtual bool sendLatestRealtime(
        uint32_t peerId, uint64_t connectionSerial,
        std::span<const std::byte> bytes) {
        return send(peerId, connectionSerial, DeliveryClass::Realtime, bytes);
    }

    // Two-phase admission for transports that can authenticate a replacement
    // while retaining the currently active same-peer socket. Connected
    // transports need not implement these; ConnectionRequested transports do.
    [[nodiscard]] virtual bool acceptConnection(
        uint32_t, uint64_t) { return false; }
    virtual void rejectConnection(uint32_t, uint64_t) {}

    [[nodiscard]] virtual std::optional<MultiplayerTransportFrame> poll() = 0;
    virtual void close() = 0;
};

} // namespace voxy::network
