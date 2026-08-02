#include "network/ridgebreak_protocol.hpp"

#include <bit>
#include <algorithm>
#include <limits>
#include <utility>

namespace voxy::network {
namespace {

class Writer {
public:
    explicit Writer(size_t capacity) { bytes_.reserve(capacity); }
    void u8(uint8_t value) { bytes_.push_back(std::byte{value}); }
    void u16(uint16_t value) {
        u8(static_cast<uint8_t>(value));
        u8(static_cast<uint8_t>(value >> 8u));
    }
    void i16(int16_t value) { u16(std::bit_cast<uint16_t>(value)); }
    void u32(uint32_t value) {
        u16(static_cast<uint16_t>(value));
        u16(static_cast<uint16_t>(value >> 16u));
    }
    void i32(int32_t value) { u32(std::bit_cast<uint32_t>(value)); }
    void u64(uint64_t value) {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }
    [[nodiscard]] std::vector<std::byte> take() { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool u8(uint8_t& value) {
        if (offset_ >= bytes_.size()) return false;
        value = std::to_integer<uint8_t>(bytes_[offset_++]);
        return true;
    }
    bool u16(uint16_t& value) {
        uint8_t low = 0u;
        uint8_t high = 0u;
        if (!u8(low) || !u8(high)) return false;
        value = static_cast<uint16_t>(low | (uint16_t{high} << 8u));
        return true;
    }
    bool i16(int16_t& value) {
        uint16_t bits = 0u;
        if (!u16(bits)) return false;
        value = std::bit_cast<int16_t>(bits);
        return true;
    }
    bool u32(uint32_t& value) {
        uint16_t low = 0u;
        uint16_t high = 0u;
        if (!u16(low) || !u16(high)) return false;
        value = uint32_t{low} | (uint32_t{high} << 16u);
        return true;
    }
    bool i32(int32_t& value) {
        uint32_t bits = 0u;
        if (!u32(bits)) return false;
        value = std::bit_cast<int32_t>(bits);
        return true;
    }
    bool u64(uint64_t& value) {
        uint32_t low = 0u;
        uint32_t high = 0u;
        if (!u32(low) || !u32(high)) return false;
        value = uint64_t{low} | (uint64_t{high} << 32u);
        return true;
    }

private:
    std::span<const std::byte> bytes_;
    size_t offset_ = 0u;
};

bool validSample(const RidgebreakInputSample& sample) noexcept {
    return sample.requestedTick != 0u
        && sample.inputSequence != 0u
        && sample.throttleQ15 <= 32'767u
        && sample.brakeQ15 <= 32'767u
        && sample.steerQ15 >= -32'767
        && sample.leanQ15 >= -32'767
        && (sample.flags & ~kRidgebreakKnownInputFlags) == 0u
        && sample.reserved == 0u
        && !((sample.flags & RidgebreakInputGearUp) != 0u
             && (sample.flags & RidgebreakInputGearDown) != 0u)
        && !((sample.flags & RidgebreakInputStanding) != 0u
             && (sample.flags & RidgebreakInputDucking) != 0u);
}

bool validLifecycle(RidgebreakPlayerLifecycle lifecycle) noexcept {
    return lifecycle == RidgebreakPlayerLifecycle::Disconnected
        || lifecycle == RidgebreakPlayerLifecycle::Riding
        || lifecycle == RidgebreakPlayerLifecycle::Crashed;
}

bool validPlayer(const RidgebreakPlayerState& player, bool populated) noexcept {
    constexpr uint32_t knownStateFlags = RidgebreakInputStanding
        | RidgebreakInputDucking;
    if (!populated) return player == RidgebreakPlayerState{};
    if (player.playerId == 0u || player.connectionGeneration == 0u
        || player.connectionSerial == 0u
        || !validLifecycle(player.lifecycle)
        || player.gear == 0u || player.gear > 5u
        || player.headingTurnsQ16 < -32'768
        || player.headingTurnsQ16 > 32'767
        || player.forwardSpeedMillimetersPerSecond < 0
        || player.forwardSpeedMillimetersPerSecond > 100'000
        || player.velocityXMillimetersPerSecond < -100'000
        || player.velocityXMillimetersPerSecond > 100'000
        || player.velocityZMillimetersPerSecond < -100'000
        || player.velocityZMillimetersPerSecond > 100'000
        || player.leanQ15 < -32'767
        || (player.stateFlags & ~knownStateFlags) != 0u) {
        return false;
    }
    if ((player.stateFlags & RidgebreakInputStanding) != 0u
        && (player.stateFlags & RidgebreakInputDucking) != 0u) {
        return false;
    }
    if (player.lifecycle == RidgebreakPlayerLifecycle::Disconnected
        && (player.velocityXMillimetersPerSecond != 0
            || player.velocityZMillimetersPerSecond != 0
            || player.forwardSpeedMillimetersPerSecond != 0
            || player.leanQ15 != 0 || player.stateFlags != 0u)) {
        return false;
    }
    return true;
}

void writeSample(Writer& writer, const RidgebreakInputSample& sample) {
    writer.u64(sample.requestedTick);
    writer.u64(sample.inputSequence);
    writer.u16(sample.throttleQ15);
    writer.u16(sample.brakeQ15);
    writer.i16(sample.steerQ15);
    writer.i16(sample.leanQ15);
    writer.u16(sample.flags);
    writer.u16(sample.reserved);
}

bool readSample(Reader& reader, RidgebreakInputSample& sample) {
    return reader.u64(sample.requestedTick)
        && reader.u64(sample.inputSequence)
        && reader.u16(sample.throttleQ15)
        && reader.u16(sample.brakeQ15)
        && reader.i16(sample.steerQ15)
        && reader.i16(sample.leanQ15)
        && reader.u16(sample.flags)
        && reader.u16(sample.reserved);
}

void writePlayer(Writer& writer, const RidgebreakPlayerState& player) {
    writer.u32(player.playerId);
    writer.u32(player.connectionGeneration);
    writer.u64(player.connectionSerial);
    writer.u64(player.lastProcessedInputSequence);
    writer.i32(player.positionXMillimeters);
    writer.i32(player.positionZMillimeters);
    writer.i32(player.velocityXMillimetersPerSecond);
    writer.i32(player.velocityZMillimetersPerSecond);
    writer.i32(player.headingTurnsQ16);
    writer.i32(player.forwardSpeedMillimetersPerSecond);
    writer.i16(player.leanQ15);
    writer.u8(player.gear);
    writer.u8(static_cast<uint8_t>(player.lifecycle));
    writer.u32(player.stateFlags);
}

bool readPlayer(Reader& reader, RidgebreakPlayerState& player) {
    uint8_t lifecycle = 0u;
    if (!reader.u32(player.playerId)
        || !reader.u32(player.connectionGeneration)
        || !reader.u64(player.connectionSerial)
        || !reader.u64(player.lastProcessedInputSequence)
        || !reader.i32(player.positionXMillimeters)
        || !reader.i32(player.positionZMillimeters)
        || !reader.i32(player.velocityXMillimetersPerSecond)
        || !reader.i32(player.velocityZMillimetersPerSecond)
        || !reader.i32(player.headingTurnsQ16)
        || !reader.i32(player.forwardSpeedMillimetersPerSecond)
        || !reader.i16(player.leanQ15)
        || !reader.u8(player.gear) || !reader.u8(lifecycle)
        || !reader.u32(player.stateFlags)) {
        return false;
    }
    player.lifecycle = static_cast<RidgebreakPlayerLifecycle>(lifecycle);
    return true;
}

} // namespace

std::vector<std::byte> encodeRidgebreakInputBundle(
    const RidgebreakInputBundle& bundle) {
    if (bundle.schemaVersion != kRidgebreakWireSchemaVersion
        || bundle.sampleCount == 0u
        || bundle.sampleCount > kRidgebreakMaximumRedundantInputs
        || bundle.connectionSerial == 0u
        || bundle.connectionGeneration == 0u || bundle.reserved != 0u) {
        return {};
    }
    for (uint32_t i = 0u; i < bundle.sampleCount; ++i) {
        if (!validSample(bundle.samples[i])) return {};
        if (i != 0u
            && (bundle.samples[i - 1u].requestedTick
                    >= bundle.samples[i].requestedTick
                || bundle.samples[i - 1u].inputSequence
                    >= bundle.samples[i].inputSequence)) {
            return {};
        }
    }
    Writer writer(kRidgebreakInputBundleBytes);
    writer.u32(bundle.schemaVersion);
    writer.u32(bundle.sampleCount);
    writer.u64(bundle.connectionSerial);
    writer.u32(bundle.connectionGeneration);
    writer.u32(bundle.reserved);
    for (const auto& sample : bundle.samples) writeSample(writer, sample);
    return writer.take();
}

RidgebreakInputDecodeResult decodeRidgebreakInputBundle(
    std::span<const std::byte> bytes) {
    RidgebreakInputDecodeResult result;
    if (bytes.size() != kRidgebreakInputBundleBytes) {
        result.error = RidgebreakCodecError::WrongSize;
        return result;
    }
    Reader reader(bytes);
    RidgebreakInputBundle bundle;
    if (!reader.u32(bundle.schemaVersion)
        || !reader.u32(bundle.sampleCount)
        || !reader.u64(bundle.connectionSerial)
        || !reader.u32(bundle.connectionGeneration)
        || !reader.u32(bundle.reserved)) {
        result.error = RidgebreakCodecError::WrongSize;
        return result;
    }
    if (bundle.schemaVersion != kRidgebreakWireSchemaVersion) {
        result.error = RidgebreakCodecError::WrongSchema;
        return result;
    }
    if (bundle.sampleCount == 0u
        || bundle.sampleCount > kRidgebreakMaximumRedundantInputs) {
        result.error = RidgebreakCodecError::InvalidCount;
        return result;
    }
    if (bundle.connectionSerial == 0u
        || bundle.connectionGeneration == 0u || bundle.reserved != 0u) {
        result.error = RidgebreakCodecError::InvalidInput;
        return result;
    }
    for (auto& sample : bundle.samples) {
        if (!readSample(reader, sample)) {
            result.error = RidgebreakCodecError::WrongSize;
            return result;
        }
    }
    for (uint32_t i = 0u; i < bundle.sampleCount; ++i) {
        if (!validSample(bundle.samples[i])) {
            result.error = RidgebreakCodecError::InvalidInput;
            return result;
        }
        if (i != 0u
            && (bundle.samples[i - 1u].requestedTick
                    >= bundle.samples[i].requestedTick
                || bundle.samples[i - 1u].inputSequence
                    >= bundle.samples[i].inputSequence)) {
            result.error = RidgebreakCodecError::InvalidOrdering;
            return result;
        }
    }
    for (uint32_t i = bundle.sampleCount;
         i < kRidgebreakMaximumRedundantInputs; ++i) {
        if (bundle.samples[i] != RidgebreakInputSample{}) {
            result.error = RidgebreakCodecError::InvalidInput;
            return result;
        }
    }
    result.bundle = bundle;
    return result;
}

std::vector<std::byte> encodeRidgebreakSnapshot(
    const RidgebreakSnapshot& snapshot) {
    if (snapshot.schemaVersion != kRidgebreakWireSchemaVersion
        || snapshot.playerCount > kRidgebreakMaximumPlayers) {
        return {};
    }
    for (uint32_t i = 0u; i < kRidgebreakMaximumPlayers; ++i) {
        const auto& player = snapshot.players[i];
        if (!validPlayer(player, i < snapshot.playerCount)) return {};
        if (i != 0u && i < snapshot.playerCount
            && snapshot.players[i - 1u].playerId >= player.playerId)
            return {};
        for (uint32_t previous = 0u; previous < i
             && i < snapshot.playerCount; ++previous) {
            if (snapshot.players[previous].connectionSerial
                == player.connectionSerial) return {};
        }
    }
    Writer writer(kRidgebreakSnapshotBytes);
    writer.u32(snapshot.schemaVersion);
    writer.u32(snapshot.playerCount);
    writer.u64(snapshot.authoritativeTick);
    writer.u64(snapshot.canonicalStateHash);
    for (const auto& player : snapshot.players) writePlayer(writer, player);
    return writer.take();
}

RidgebreakSnapshotDecodeResult decodeRidgebreakSnapshot(
    std::span<const std::byte> bytes) {
    RidgebreakSnapshotDecodeResult result;
    if (bytes.size() != kRidgebreakSnapshotBytes) {
        result.error = RidgebreakCodecError::WrongSize;
        return result;
    }
    Reader reader(bytes);
    RidgebreakSnapshot snapshot;
    if (!reader.u32(snapshot.schemaVersion)
        || !reader.u32(snapshot.playerCount)
        || !reader.u64(snapshot.authoritativeTick)
        || !reader.u64(snapshot.canonicalStateHash)) {
        result.error = RidgebreakCodecError::WrongSize;
        return result;
    }
    if (snapshot.schemaVersion != kRidgebreakWireSchemaVersion) {
        result.error = RidgebreakCodecError::WrongSchema;
        return result;
    }
    if (snapshot.playerCount > kRidgebreakMaximumPlayers) {
        result.error = RidgebreakCodecError::InvalidCount;
        return result;
    }
    for (auto& player : snapshot.players) {
        if (!readPlayer(reader, player)) {
            result.error = RidgebreakCodecError::WrongSize;
            return result;
        }
    }
    for (uint32_t i = 0u; i < kRidgebreakMaximumPlayers; ++i) {
        const auto& player = snapshot.players[i];
        if (!validPlayer(player, i < snapshot.playerCount)
            || (i != 0u && i < snapshot.playerCount
                && snapshot.players[i - 1u].playerId >= player.playerId)) {
            result.error = RidgebreakCodecError::InvalidPlayer;
            return result;
        }
        for (uint32_t previous = 0u; previous < i
             && i < snapshot.playerCount; ++previous) {
            if (snapshot.players[previous].connectionSerial
                == player.connectionSerial) {
                result.error = RidgebreakCodecError::InvalidPlayer;
                return result;
            }
        }
    }
    result.snapshot = snapshot;
    return result;
}

RidgebreakReconciliation reconcileRidgebreakPrediction(
    uint64_t authoritativeTick,
    const RidgebreakPlayerState& predicted,
    const RidgebreakPlayerState& authoritative,
    uint32_t hardCorrectionDistanceMillimeters) noexcept {
    const auto difference = [](int32_t expected, int32_t actual) {
        return static_cast<int32_t>(std::clamp<int64_t>(
            int64_t{expected} - actual,
            std::numeric_limits<int32_t>::min(),
            std::numeric_limits<int32_t>::max()));
    };
    RidgebreakReconciliation result;
    result.authoritativeTick = authoritativeTick;
    result.acknowledgedInputSequence =
        authoritative.lastProcessedInputSequence;
    result.positionErrorXMillimeters = difference(
        authoritative.positionXMillimeters,
        predicted.positionXMillimeters);
    result.positionErrorZMillimeters = difference(
        authoritative.positionZMillimeters,
        predicted.positionZMillimeters);
    result.speedErrorMillimetersPerSecond = difference(
        authoritative.forwardSpeedMillimetersPerSecond,
        predicted.forwardSpeedMillimetersPerSecond);
    const int64_t x = result.positionErrorXMillimeters;
    const int64_t z = result.positionErrorZMillimeters;
    const uint64_t absoluteX = static_cast<uint64_t>(x < 0 ? -x : x);
    const uint64_t absoluteZ = static_cast<uint64_t>(z < 0 ? -z : z);
    const uint64_t tolerance = hardCorrectionDistanceMillimeters;
    result.hardCorrectionRequired =
        predicted.playerId != authoritative.playerId
        || predicted.connectionGeneration
            != authoritative.connectionGeneration
        || predicted.connectionSerial != authoritative.connectionSerial
        || predicted.lifecycle != authoritative.lifecycle
        || absoluteX * absoluteX + absoluteZ * absoluteZ
            > tolerance * tolerance;
    return result;
}

static_assert(kRidgebreakInputBundleBytes == 108u);
static_assert(kRidgebreakSnapshotBytes == 248u);

} // namespace voxy::network
