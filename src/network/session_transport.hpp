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

    [[nodiscard]] virtual std::optional<MultiplayerTransportFrame> poll() = 0;
    virtual void close() = 0;
};

} // namespace voxy::network
