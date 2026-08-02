#pragma once

#include "network/protocol.hpp"
#include "network/ridgebreak_protocol.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace voxy::network {

enum class RidgebreakSnapshotWindowError : uint32_t {
    None = 0u,
    NotInitialized,
    ConnectionSerialMismatch,
    WrongDeliveryClass,
    OuterDecodeFailed,
    OuterIdentityMismatch,
    SnapshotDecodeFailed,
    SnapshotTickMismatch,
    PlayerIdentityMismatch,
    SequenceJump,
    DuplicateOrStaleSequence,
    StaleSnapshotTick,
};

struct RidgebreakSnapshotWindowResult {
    std::optional<RidgebreakSnapshot> snapshot;
    RidgebreakSnapshotWindowError error =
        RidgebreakSnapshotWindowError::None;
    uint64_t packetSequence = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return snapshot.has_value();
    }
};

// One instance belongs to one authenticated transport connection lifetime.
// reset() deliberately clears its sequence window across reconnects.
class RidgebreakClientSnapshotWindow {
public:
    struct Config {
        uint64_t sessionId = 0u;
        uint64_t worldId = 0u;
        uint32_t worldEpoch = 0u;
        uint32_t authorityEpoch = 0u;
        uint32_t playerId = 0u;
        uint64_t connectionSerial = 0u;
        uint32_t connectionGeneration = 0u;
        uint64_t maximumSequenceAdvance = 256u;
    };

    [[nodiscard]] bool reset(const Config& config) noexcept;
    [[nodiscard]] RidgebreakSnapshotWindowResult accept(
        uint64_t transportConnectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> framedPacket) noexcept;

    [[nodiscard]] uint64_t latestSequence() const noexcept {
        return received_.latest();
    }
    [[nodiscard]] uint64_t acknowledgementBits() const noexcept {
        return received_.bits();
    }
    [[nodiscard]] uint64_t latestAuthoritativeTick() const noexcept {
        return latestAuthoritativeTick_;
    }
    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    Config config_{};
    AckWindow received_{};
    uint64_t latestAuthoritativeTick_ = 0u;
    bool initialized_ = false;
};

} // namespace voxy::network
