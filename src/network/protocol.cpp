#include "network/protocol.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::network {
namespace {

constexpr std::array<std::byte, 8> kPacketMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'N'}, std::byte{'E'}, std::byte{'T'}, std::byte{0}};
constexpr uint32_t kFnvOffset = 2'166'136'261u;
constexpr uint32_t kFnvPrime = 16'777'619u;
constexpr size_t kEncodedCommandBytes = 96u;

uint32_t checksum(std::span<const std::byte> bytes) noexcept {
    uint32_t result = kFnvOffset;
    for (std::byte value : bytes)
        result = (result ^ std::to_integer<uint32_t>(value)) * kFnvPrime;
    return result;
}

class Writer {
public:
    void u32(uint32_t value) {
        for (uint32_t shift = 0; shift < 32u; shift += 8u)
            bytes_.push_back(std::byte{static_cast<uint8_t>(value >> shift)});
    }
    void i32(int32_t value) { u32(std::bit_cast<uint32_t>(value)); }
    void u64(uint64_t value) {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }
    void raw(std::span<const std::byte> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }
    [[nodiscard]] std::vector<std::byte>& bytes() noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool u32(uint32_t& value) {
        if (remaining() < 4u) return fail("truncated u32");
        value = 0;
        for (uint32_t byte = 0; byte < 4u; ++byte) {
            value |= std::to_integer<uint32_t>(bytes_[offset_ + byte])
                << (byte * 8u);
        }
        offset_ += 4u;
        return true;
    }
    bool i32(int32_t& value) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        value = std::bit_cast<int32_t>(bits);
        return true;
    }
    bool u64(uint64_t& value) {
        uint32_t low = 0;
        uint32_t high = 0;
        if (!u32(low) || !u32(high)) return false;
        value = uint64_t{low} | (uint64_t{high} << 32u);
        return true;
    }
    [[nodiscard]] size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    bool fail(std::string value) {
        if (error_.empty()) error_ = std::move(value);
        return false;
    }
    std::span<const std::byte> bytes_;
    size_t offset_ = 0;
    std::string error_;
};

bool validPayloadType(uint32_t value) noexcept {
    return value >= static_cast<uint32_t>(PacketPayloadType::Input)
        && value <= static_cast<uint32_t>(PacketPayloadType::Correction);
}

bool validDeliveryClass(DeliveryClass delivery) noexcept {
    switch (delivery) {
        case DeliveryClass::Realtime:
        case DeliveryClass::ReliableEvent:
        case DeliveryClass::ReliableControl:
            return true;
    }
    return false;
}

bool validCommandType(uint32_t value) noexcept {
    switch (static_cast<NetworkCommandType>(value)) {
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::FireMeteorRequest:
        case NetworkCommandType::ApplyImpulseRequest:
        case NetworkCommandType::SetAwakeRequest:
        case NetworkCommandType::SpawnBody:
        case NetworkCommandType::DestroyBody:
        case NetworkCommandType::Correction:
        case NetworkCommandType::SetVelocity:
        case NetworkCommandType::SetAwake:
            return true;
    }
    return false;
}

uint32_t commandPriority(NetworkCommandType type) noexcept {
    switch (type) {
        case NetworkCommandType::DestroyBody: return 0u;
        case NetworkCommandType::SpawnBody: return 1u;
        case NetworkCommandType::Correction: return 2u;
        case NetworkCommandType::SetVelocity: return 3u;
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::ApplyImpulseRequest: return 4u;
        case NetworkCommandType::FireMeteorRequest: return 5u;
        case NetworkCommandType::SetAwakeRequest:
        case NetworkCommandType::SetAwake: return 6u;
    }
    return std::numeric_limits<uint32_t>::max();
}

} // namespace

PacketWriteResult PacketCodec::encode(
    const Packet& packet, DeliveryClass delivery) {
    PacketWriteResult result;
    if (!validDeliveryClass(delivery)) {
        result.error = "invalid network delivery class";
        return result;
    }
    if (packet.header.protocolVersion != kNetworkProtocolVersion
        || !validPayloadType(
            static_cast<uint32_t>(packet.header.payloadType))) {
        result.error = "invalid network packet header";
        return result;
    }
    const size_t frameLimit = delivery == DeliveryClass::Realtime
        ? kConservativeRealtimeMtu : kMaximumReliableFrameBytes;
    if (packet.payload.size() > std::numeric_limits<uint32_t>::max()
        || packet.payload.size() > frameLimit - kNetworkPacketOverheadBytes) {
        result.error = delivery == DeliveryClass::Realtime
            ? "realtime packet exceeds conservative MTU"
            : "reliable packet exceeds frame limit";
        return result;
    }
    Writer writer;
    writer.raw(kPacketMagic);
    writer.u32(packet.header.protocolVersion);
    writer.u32(static_cast<uint32_t>(packet.header.payloadType));
    writer.u32(packet.header.flags);
    writer.u32(static_cast<uint32_t>(packet.payload.size()));
    writer.u64(packet.header.sessionId);
    writer.u64(packet.header.worldId);
    writer.u32(packet.header.worldEpoch);
    writer.u32(packet.header.authorityEpoch);
    writer.u64(packet.header.sequence);
    writer.u64(packet.header.ackSequence);
    writer.u64(packet.header.ackBits);
    writer.u64(packet.header.tick);
    writer.raw(packet.payload);
    writer.u32(checksum(writer.bytes()));
    result.bytes = std::move(writer.bytes());
    return result;
}

PacketReadResult PacketCodec::decode(
    std::span<const std::byte> bytes, DeliveryClass delivery) {
    PacketReadResult result;
    if (!validDeliveryClass(delivery)) {
        result.error = "invalid network delivery class";
        return result;
    }
    if (bytes.size() < kNetworkPacketOverheadBytes
        || !std::equal(kPacketMagic.begin(), kPacketMagic.end(), bytes.begin())) {
        result.error = "invalid network packet magic or length";
        return result;
    }
    const size_t frameLimit = delivery == DeliveryClass::Realtime
        ? kConservativeRealtimeMtu : kMaximumReliableFrameBytes;
    if (bytes.size() > frameLimit) {
        result.error = delivery == DeliveryClass::Realtime
            ? "realtime packet exceeds conservative MTU"
            : "reliable packet exceeds frame limit";
        return result;
    }
    const size_t checksumOffset = bytes.size() - sizeof(uint32_t);
    uint32_t expectedChecksum = 0;
    for (uint32_t byte = 0; byte < 4u; ++byte) {
        expectedChecksum |= std::to_integer<uint32_t>(
            bytes[checksumOffset + byte]) << (byte * 8u);
    }
    if (checksum(bytes.first(checksumOffset)) != expectedChecksum) {
        result.error = "network packet checksum mismatch";
        return result;
    }
    Reader reader(bytes.subspan(kPacketMagic.size(),
                                checksumOffset - kPacketMagic.size()));
    Packet packet;
    uint32_t payloadType = 0;
    uint32_t payloadBytes = 0;
    if (!reader.u32(packet.header.protocolVersion)
        || !reader.u32(payloadType)
        || !reader.u32(packet.header.flags)
        || !reader.u32(payloadBytes)
        || !reader.u64(packet.header.sessionId)
        || !reader.u64(packet.header.worldId)
        || !reader.u32(packet.header.worldEpoch)
        || !reader.u32(packet.header.authorityEpoch)
        || !reader.u64(packet.header.sequence)
        || !reader.u64(packet.header.ackSequence)
        || !reader.u64(packet.header.ackBits)
        || !reader.u64(packet.header.tick)) {
        result.error = reader.error();
        return result;
    }
    if (packet.header.protocolVersion != kNetworkProtocolVersion) {
        result.error = "unsupported network protocol version";
        return result;
    }
    if (!validPayloadType(payloadType)) {
        result.error = "invalid network payload type";
        return result;
    }
    if (payloadBytes != reader.remaining()) {
        result.error = "network payload length mismatch";
        return result;
    }
    packet.header.payloadType = static_cast<PacketPayloadType>(payloadType);
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(
                              checksumOffset - payloadBytes),
                          bytes.begin() + static_cast<std::ptrdiff_t>(
                              checksumOffset));
    result.packet = std::move(packet);
    return result;
}

bool canonicalNetworkCommandLess(
    const CanonicalNetworkCommand& lhs,
    const CanonicalNetworkCommand& rhs) noexcept {
    return std::tuple{lhs.tick, commandPriority(lhs.type), lhs.sequence,
                      lhs.clientId, lhs.islandId, lhs.body, lhs.generation,
                      lhs.authorityEpoch, lhs.flags, lhs.payload}
         < std::tuple{rhs.tick, commandPriority(rhs.type), rhs.sequence,
                      rhs.clientId, rhs.islandId, rhs.body, rhs.generation,
                      rhs.authorityEpoch, rhs.flags, rhs.payload};
}

bool isClientCommandAllowed(NetworkCommandType type) noexcept {
    switch (type) {
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::FireMeteorRequest:
        case NetworkCommandType::ApplyImpulseRequest:
        case NetworkCommandType::SetAwakeRequest:
            return true;
        case NetworkCommandType::SpawnBody:
        case NetworkCommandType::DestroyBody:
        case NetworkCommandType::Correction:
        case NetworkCommandType::SetVelocity:
        case NetworkCommandType::SetAwake:
            return false;
    }
    return false;
}

std::optional<physics::deterministic::CanonicalReplayCommand>
toReplayCommand(const CanonicalNetworkCommand& source) noexcept {
    using physics::deterministic::CanonicalReplayCommand;
    using physics::deterministic::ReplayCommandType;
    CanonicalReplayCommand result;
    result.tick = source.tick;
    result.sequence = source.sequence;
    result.producer = source.clientId;
    result.body = source.body;
    result.generation = source.generation;
    result.payload = source.payload;
    switch (source.type) {
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::ApplyImpulseRequest:
            result.type = ReplayCommandType::ApplyImpulse;
            break;
        case NetworkCommandType::FireMeteorRequest:
        case NetworkCommandType::SpawnBody:
            result.type = ReplayCommandType::SpawnBody;
            break;
        case NetworkCommandType::DestroyBody:
            result.type = ReplayCommandType::DestroyBody;
            break;
        case NetworkCommandType::Correction:
            result.type = ReplayCommandType::Correction;
            break;
        case NetworkCommandType::SetVelocity:
            result.type = ReplayCommandType::SetVelocity;
            break;
        case NetworkCommandType::SetAwakeRequest:
        case NetworkCommandType::SetAwake:
            result.type = ReplayCommandType::SetAwake;
            break;
        default:
            return std::nullopt;
    }
    return result;
}

std::vector<std::byte> NetworkCommandCodec::encode(
    std::span<const CanonicalNetworkCommand> input) {
    if (input.size() > kMaximumCommandsPerPacket) return {};
    if (std::any_of(input.begin(), input.end(), [](const auto& command) {
            constexpr uint32_t allowedFlags = NetworkCommandServerIssued
                | NetworkCommandCorrectionEvent;
            return !validCommandType(
                       static_cast<uint32_t>(command.type))
                || (command.flags & ~allowedFlags) != 0u;
        })) {
        return {};
    }
    std::vector<CanonicalNetworkCommand> commands(input.begin(), input.end());
    std::stable_sort(commands.begin(), commands.end(), canonicalNetworkCommandLess);
    Writer writer;
    writer.u32(static_cast<uint32_t>(commands.size()));
    for (const auto& command : commands) {
        writer.u64(command.tick);
        writer.u64(command.sequence);
        writer.u64(command.islandId);
        writer.u32(command.authorityEpoch);
        writer.u32(command.clientId);
        writer.u32(static_cast<uint32_t>(command.type));
        writer.u32(command.body);
        writer.u32(command.generation);
        writer.u32(command.flags);
        for (int32_t value : command.payload) writer.i32(value);
    }
    return std::move(writer.bytes());
}

NetworkCommandReadResult NetworkCommandCodec::decode(
    std::span<const std::byte> bytes) {
    NetworkCommandReadResult result;
    Reader reader(bytes);
    uint32_t count = 0;
    if (!reader.u32(count)) {
        result.error = reader.error();
        return result;
    }
    if (count > kMaximumCommandsPerPacket
        || uint64_t{count} * kEncodedCommandBytes != reader.remaining()) {
        result.error = "invalid network command count";
        return result;
    }
    std::vector<CanonicalNetworkCommand> commands(count);
    for (auto& command : commands) {
        uint32_t type = 0;
        if (!reader.u64(command.tick) || !reader.u64(command.sequence)
            || !reader.u64(command.islandId)
            || !reader.u32(command.authorityEpoch)
            || !reader.u32(command.clientId) || !reader.u32(type)
            || !reader.u32(command.body) || !reader.u32(command.generation)
            || !reader.u32(command.flags)) {
            result.error = reader.error();
            return result;
        }
        if (!validCommandType(type)) {
            result.error = "invalid network command type";
            return result;
        }
        constexpr uint32_t knownFlags = NetworkCommandServerIssued
            | NetworkCommandCorrectionEvent;
        if ((command.flags & ~knownFlags) != 0u) {
            result.error = "invalid network command flags";
            return result;
        }
        command.type = static_cast<NetworkCommandType>(type);
        for (int32_t& value : command.payload) {
            if (!reader.i32(value)) {
                result.error = reader.error();
                return result;
            }
        }
    }
    std::stable_sort(commands.begin(), commands.end(), canonicalNetworkCommandLess);
    result.commands = std::move(commands);
    return result;
}

bool AckWindow::observe(uint64_t sequence) noexcept {
    if (!initialized_) {
        initialized_ = true;
        latest_ = sequence;
        bits_ = 1u;
        return true;
    }
    if (sequence > latest_) {
        const uint64_t shift = sequence - latest_;
        bits_ = shift >= 64u ? 1u : (bits_ << shift) | 1u;
        latest_ = sequence;
        return true;
    }
    const uint64_t distance = latest_ - sequence;
    if (distance >= 64u) return false;
    const uint64_t mask = uint64_t{1} << distance;
    if ((bits_ & mask) != 0u) return false;
    bits_ |= mask;
    return true;
}

bool AckWindow::acknowledges(uint64_t sequence) const noexcept {
    if (!initialized_ || sequence > latest_) return false;
    const uint64_t distance = latest_ - sequence;
    return distance < 64u && (bits_ & (uint64_t{1} << distance)) != 0u;
}

} // namespace voxy::network
