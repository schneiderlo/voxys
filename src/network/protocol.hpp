#pragma once

#include "physics/deterministic/replay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace voxy::network {

inline constexpr uint32_t kNetworkProtocolVersion = 1;
inline constexpr size_t kConservativeRealtimeMtu = 1'200;
inline constexpr size_t kMaximumReliableFrameBytes = 16u * 1024u * 1024u;
inline constexpr size_t kNetworkPacketOverheadBytes = 84u;
inline constexpr uint32_t kMaximumCommandsPerPacket = 256;

enum class PacketPayloadType : uint32_t {
    Input = 1,
    Command = 2,
    Snapshot = 3,
    SnapshotAck = 4,
    TickSync = 5,
    StateHash = 6,
    Event = 7,
    Control = 8,
    Checkpoint = 9,
    Correction = 10,
};

enum class DeliveryClass : uint32_t {
    Realtime = 1,
    ReliableEvent = 2,
    ReliableControl = 3,
};

struct PacketHeader {
    uint32_t protocolVersion = kNetworkProtocolVersion;
    PacketPayloadType payloadType = PacketPayloadType::Control;
    uint32_t flags = 0;
    uint64_t sessionId = 0;
    uint64_t worldId = 0;
    uint32_t worldEpoch = 0;
    uint32_t authorityEpoch = 0;
    uint64_t sequence = 0;
    uint64_t ackSequence = 0;
    uint64_t ackBits = 0;
    uint64_t tick = 0;
};

struct Packet {
    PacketHeader header{};
    std::vector<std::byte> payload;
};

struct PacketWriteResult {
    std::vector<std::byte> bytes;
    std::string error;
};

struct PacketReadResult {
    std::optional<Packet> packet;
    std::string error;
};

class PacketCodec {
public:
    [[nodiscard]] static PacketWriteResult encode(
        const Packet& packet, DeliveryClass delivery);
    [[nodiscard]] static PacketReadResult decode(
        std::span<const std::byte> bytes, DeliveryClass delivery);
};

enum class NetworkCommandType : uint32_t {
    MoveInput = 0,
    FireMeteorRequest = 1,
    ApplyImpulseRequest = 2,
    SetAwakeRequest = 3,
    SpawnBody = 16,
    DestroyBody = 17,
    Correction = 18,
    SetVelocity = 19,
    SetAwake = 20,
};

enum NetworkCommandFlag : uint32_t {
    NetworkCommandServerIssued = 1u << 0u,
    NetworkCommandCorrectionEvent = 1u << 1u,
};

struct CanonicalNetworkCommand {
    uint64_t tick = 0;
    uint64_t sequence = 0;
    uint64_t islandId = 0;
    uint32_t authorityEpoch = 0;
    uint32_t clientId = 0;
    NetworkCommandType type = NetworkCommandType::MoveInput;
    uint32_t body = 0;
    uint32_t generation = 0;
    uint32_t flags = 0;
    std::array<int32_t, 12> payload{};
};

struct NetworkCommandReadResult {
    std::optional<std::vector<CanonicalNetworkCommand>> commands;
    std::string error;
};

[[nodiscard]] bool canonicalNetworkCommandLess(
    const CanonicalNetworkCommand& lhs,
    const CanonicalNetworkCommand& rhs) noexcept;
[[nodiscard]] bool isClientCommandAllowed(NetworkCommandType type) noexcept;
[[nodiscard]] std::optional<physics::deterministic::CanonicalReplayCommand>
toReplayCommand(const CanonicalNetworkCommand& command) noexcept;

class NetworkCommandCodec {
public:
    [[nodiscard]] static std::vector<std::byte> encode(
        std::span<const CanonicalNetworkCommand> commands);
    [[nodiscard]] static NetworkCommandReadResult decode(
        std::span<const std::byte> bytes);
};

class AckWindow {
public:
    // Returns false for a duplicate or a packet older than the 64-bit window.
    [[nodiscard]] bool observe(uint64_t sequence) noexcept;
    [[nodiscard]] uint64_t latest() const noexcept { return latest_; }
    [[nodiscard]] uint64_t bits() const noexcept { return bits_; }
    [[nodiscard]] bool acknowledges(uint64_t sequence) const noexcept;

private:
    uint64_t latest_ = 0;
    uint64_t bits_ = 0;
    bool initialized_ = false;
};

} // namespace voxy::network
