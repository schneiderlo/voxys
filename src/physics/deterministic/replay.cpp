#include "physics/deterministic/replay.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

namespace voxy::physics::deterministic {
namespace {

constexpr std::array<std::byte, 8> kMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'R'}, std::byte{'P'}, std::byte{'L'}, std::byte{0}};
constexpr uint32_t kMaximumReplayElements = 16u * 1024u * 1024u;
constexpr uint32_t kMaximumBuildFingerprintBytes = 4'096u;
constexpr uint32_t kFnvOffset = 2'166'136'261u;
constexpr uint32_t kFnvPrime = 16'777'619u;

uint32_t checksum(std::span<const std::byte> bytes) noexcept {
    uint32_t hash = kFnvOffset;
    for (std::byte value : bytes)
        hash = (hash ^ std::to_integer<uint32_t>(value)) * kFnvPrime;
    return hash;
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
    void string(std::string_view value) {
        u32(static_cast<uint32_t>(value.size()));
        raw(std::as_bytes(std::span(value.data(), value.size())));
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
    bool string(std::string& value) {
        uint32_t count = 0;
        if (!u32(count)) return false;
        if (count > kMaximumBuildFingerprintBytes || remaining() < count)
            return fail("invalid string length");
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), count);
        offset_ += count;
        return true;
    }
    bool count(uint32_t& value, std::string_view label) {
        if (!u32(value)) return false;
        if (value > kMaximumReplayElements) {
            return fail(std::string("excessive ") + std::string(label));
        }
        return true;
    }
    [[nodiscard]] size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] size_t offset() const noexcept { return offset_; }

private:
    bool fail(std::string message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }
    std::span<const std::byte> bytes_;
    size_t offset_ = 0;
    std::string error_;
};

void writeBody(Writer& writer, const LockstepBody& body) {
    for (uint32_t value : body.identity) writer.u32(value);
    for (int32_t value : body.sectorRadius) writer.i32(value);
    for (int32_t value : body.positionInvMass) writer.i32(value);
    for (int32_t value : body.linearVelocity) writer.i32(value);
}

bool readBody(Reader& reader, LockstepBody& body) {
    for (uint32_t& value : body.identity) if (!reader.u32(value)) return false;
    for (int32_t& value : body.sectorRadius) if (!reader.i32(value)) return false;
    for (int32_t& value : body.positionInvMass) if (!reader.i32(value)) return false;
    for (int32_t& value : body.linearVelocity) if (!reader.i32(value)) return false;
    return true;
}

void writeContact(Writer& writer, const LockstepContact& contact) {
    for (uint32_t value : contact.ids) writer.u32(value);
    for (int32_t value : contact.normalPenetration) writer.i32(value);
}

bool readContact(Reader& reader, LockstepContact& contact) {
    for (uint32_t& value : contact.ids) if (!reader.u32(value)) return false;
    for (int32_t& value : contact.normalPenetration)
        if (!reader.i32(value)) return false;
    return true;
}

uint32_t commandPriority(ReplayCommandType type) noexcept {
    return static_cast<uint32_t>(type);
}

uint32_t actualHash(const LockstepTelemetry& telemetry,
                    const ReplayHashRecord& expected) {
    switch (expected.stage) {
        case ReplayHashStage::Body:
            return expected.objectId < telemetry.hashes.bodies.size()
                ? telemetry.hashes.bodies[expected.objectId] : 0u;
        case ReplayHashStage::Contact:
            return expected.objectId < telemetry.hashes.contacts.size()
                ? telemetry.hashes.contacts[expected.objectId] : 0u;
        case ReplayHashStage::Island:
            return expected.objectId < telemetry.hashes.islands.size()
                ? telemetry.hashes.islands[expected.objectId] : 0u;
        case ReplayHashStage::World:
            return telemetry.hashes.world;
    }
    return 0u;
}

} // namespace

bool canonicalReplayCommandLess(
    const CanonicalReplayCommand& lhs,
    const CanonicalReplayCommand& rhs) noexcept {
    return std::tuple{lhs.tick, commandPriority(lhs.type), lhs.sequence,
                      lhs.producer, lhs.body, lhs.generation, lhs.payload}
         < std::tuple{rhs.tick, commandPriority(rhs.type), rhs.sequence,
                      rhs.producer, rhs.body, rhs.generation, rhs.payload};
}

std::vector<std::byte> ReplayCodec::encode(const ReplayRecording& source) {
    ReplayRecording recording = source;
    std::stable_sort(recording.commands.begin(), recording.commands.end(),
                     canonicalReplayCommandLess);
    std::sort(recording.hashes.begin(), recording.hashes.end(),
        [](const ReplayHashRecord& lhs, const ReplayHashRecord& rhs) {
            return std::tie(lhs.tick, lhs.stage, lhs.objectId)
                 < std::tie(rhs.tick, rhs.stage, rhs.objectId);
        });
    Writer writer;
    writer.raw(kMagic);
    writer.u32(recording.header.schemaVersion);
    writer.u32(static_cast<uint32_t>(recording.header.arithmeticMode));
    writer.u32(static_cast<uint32_t>(recording.header.backend));
    writer.u32(recording.header.flags);
    writer.u64(recording.header.backendVersionHash);
    writer.u64(recording.header.shaderVersionHash);
    writer.u64(recording.header.terrainHash);
    writer.u64(recording.header.generatorHash);
    writer.u64(recording.header.contentHash);
    writer.u32(recording.header.simulation.tickRateHz);
    writer.u32(recording.header.simulation.substeps);
    writer.u32(recording.header.simulation.solverIterations);
    writer.i32(recording.header.simulation.gravityPerSubstepQ16);
    const auto& capacity = recording.header.capacity;
    for (uint32_t value : {
             capacity.residentBodies, capacity.activeBodies,
             capacity.commandsPerTick, capacity.gridEntries,
             capacity.candidatePairs, capacity.uniquePairs,
             capacity.contacts, capacity.manifolds,
             capacity.terrainContacts, capacity.overflowConstraints,
             capacity.events, capacity.visibleBodies}) writer.u32(value);
    writer.string(recording.header.buildFingerprint);
    writer.u64(recording.checkpoint.tick);
    writer.u64(recording.checkpoint.randomState);
    for (int32_t value : recording.checkpoint.deterministicWaterState)
        writer.i32(value);
    writer.u32(static_cast<uint32_t>(recording.checkpoint.bodies.size()));
    writer.u32(static_cast<uint32_t>(recording.checkpoint.contacts.size()));
    writer.u32(static_cast<uint32_t>(recording.checkpoint.islandRoots.size()));
    writer.u32(static_cast<uint32_t>(recording.checkpoint.freeBodyIds.size()));
    writer.u32(static_cast<uint32_t>(recording.checkpoint.manifoldWords.size()));
    writer.u32(static_cast<uint32_t>(recording.checkpoint.graphColors.size()));
    writer.u32(static_cast<uint32_t>(recording.checkpoint.sleepCounters.size()));
    writer.u32(static_cast<uint32_t>(recording.commands.size()));
    writer.u32(static_cast<uint32_t>(recording.hashes.size()));
    for (const auto& body : recording.checkpoint.bodies) writeBody(writer, body);
    for (const auto& contact : recording.checkpoint.contacts)
        writeContact(writer, contact);
    for (uint32_t value : recording.checkpoint.islandRoots) writer.u32(value);
    for (uint32_t value : recording.checkpoint.freeBodyIds) writer.u32(value);
    for (uint32_t value : recording.checkpoint.manifoldWords) writer.u32(value);
    for (uint32_t value : recording.checkpoint.graphColors) writer.u32(value);
    for (uint32_t value : recording.checkpoint.sleepCounters) writer.u32(value);
    for (const auto& command : recording.commands) {
        writer.u64(command.tick);
        writer.u64(command.sequence);
        writer.u32(command.producer);
        writer.u32(static_cast<uint32_t>(command.type));
        writer.u32(command.body);
        writer.u32(command.generation);
        for (int32_t value : command.payload) writer.i32(value);
    }
    for (const auto& hash : recording.hashes) {
        writer.u64(hash.tick);
        writer.u32(static_cast<uint32_t>(hash.stage));
        writer.u32(hash.objectId);
        writer.u32(hash.hash);
        writer.u32(0u);
    }
    const uint32_t digest = checksum(writer.bytes());
    writer.u32(digest);
    return std::move(writer.bytes());
}

ReplayReadResult ReplayCodec::decode(std::span<const std::byte> bytes) {
    ReplayReadResult result;
    if (bytes.size() < kMagic.size() + 4u
        || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        result.error = "invalid replay magic";
        return result;
    }
    uint32_t storedChecksum = 0;
    const size_t checksumOffset = bytes.size() - 4u;
    for (uint32_t byte = 0; byte < 4u; ++byte) {
        storedChecksum |= std::to_integer<uint32_t>(
            bytes[checksumOffset + byte]) << (byte * 8u);
    }
    if (checksum(bytes.first(checksumOffset)) != storedChecksum) {
        result.error = "replay checksum mismatch";
        return result;
    }
    Reader reader(bytes.subspan(kMagic.size(), checksumOffset - kMagic.size()));
    ReplayRecording recording;
    uint32_t arithmetic = 0;
    uint32_t backend = 0;
    if (!reader.u32(recording.header.schemaVersion)
        || !reader.u32(arithmetic) || !reader.u32(backend)
        || !reader.u32(recording.header.flags)
        || !reader.u64(recording.header.backendVersionHash)
        || !reader.u64(recording.header.shaderVersionHash)
        || !reader.u64(recording.header.terrainHash)
        || !reader.u64(recording.header.generatorHash)
        || !reader.u64(recording.header.contentHash)
        || !reader.u32(recording.header.simulation.tickRateHz)
        || !reader.u32(recording.header.simulation.substeps)
        || !reader.u32(recording.header.simulation.solverIterations)
        || !reader.i32(recording.header.simulation.gravityPerSubstepQ16)) {
        result.error = reader.error();
        return result;
    }
    if (recording.header.schemaVersion != kLockstepSchemaVersion) {
        result.error = "unsupported replay schema version";
        return result;
    }
    if (arithmetic < static_cast<uint32_t>(ArithmeticMode::DeterministicFloat)
        || arithmetic > static_cast<uint32_t>(ArithmeticMode::Lockstep)) {
        result.error = "invalid arithmetic mode";
        return result;
    }
    recording.header.arithmeticMode = static_cast<ArithmeticMode>(arithmetic);
    if (backend < static_cast<uint32_t>(ReplayBackend::Box3DReference)
        || backend > static_cast<uint32_t>(ReplayBackend::CpuLockstepReference)) {
        result.error = "invalid replay backend";
        return result;
    }
    recording.header.backend = static_cast<ReplayBackend>(backend);
    auto& capacity = recording.header.capacity;
    for (uint32_t* value : {
             &capacity.residentBodies, &capacity.activeBodies,
             &capacity.commandsPerTick, &capacity.gridEntries,
             &capacity.candidatePairs, &capacity.uniquePairs,
             &capacity.contacts, &capacity.manifolds,
             &capacity.terrainContacts, &capacity.overflowConstraints,
             &capacity.events, &capacity.visibleBodies}) {
        if (!reader.u32(*value)) {
            result.error = reader.error();
            return result;
        }
    }
    if (!reader.string(recording.header.buildFingerprint)
        || !reader.u64(recording.checkpoint.tick)
        || !reader.u64(recording.checkpoint.randomState)) {
        result.error = reader.error();
        return result;
    }
    for (int32_t& value : recording.checkpoint.deterministicWaterState) {
        if (!reader.i32(value)) {
            result.error = reader.error();
            return result;
        }
    }
    std::array<uint32_t, 9> counts{};
    constexpr std::array<std::string_view, 9> labels{
        "bodies", "contacts", "roots", "free IDs", "manifold words",
        "graph colors", "sleep counters", "commands", "hashes"};
    for (uint32_t index = 0; index < counts.size(); ++index) {
        if (!reader.count(counts[index], labels[index])) {
            result.error = reader.error();
            return result;
        }
    }
    const uint64_t requiredBytes =
          uint64_t{counts[0]} * sizeof(LockstepBody)
        + uint64_t{counts[1]} * sizeof(LockstepContact)
        + uint64_t{counts[2] + counts[3] + counts[4] + counts[5] + counts[6]}
            * sizeof(uint32_t)
        + uint64_t{counts[7]} * 80u
        + uint64_t{counts[8]} * 24u;
    if (requiredBytes > reader.remaining()) {
        result.error = "replay element counts exceed payload";
        return result;
    }
    recording.checkpoint.bodies.resize(counts[0]);
    recording.checkpoint.contacts.resize(counts[1]);
    recording.checkpoint.islandRoots.resize(counts[2]);
    recording.checkpoint.freeBodyIds.resize(counts[3]);
    recording.checkpoint.manifoldWords.resize(counts[4]);
    recording.checkpoint.graphColors.resize(counts[5]);
    recording.checkpoint.sleepCounters.resize(counts[6]);
    recording.commands.resize(counts[7]);
    recording.hashes.resize(counts[8]);
    for (auto& body : recording.checkpoint.bodies)
        if (!readBody(reader, body)) { result.error = reader.error(); return result; }
    for (auto& contact : recording.checkpoint.contacts)
        if (!readContact(reader, contact)) { result.error = reader.error(); return result; }
    for (auto* values : {
             &recording.checkpoint.islandRoots,
             &recording.checkpoint.freeBodyIds,
             &recording.checkpoint.manifoldWords,
             &recording.checkpoint.graphColors,
             &recording.checkpoint.sleepCounters}) {
        for (uint32_t& value : *values) {
            if (!reader.u32(value)) { result.error = reader.error(); return result; }
        }
    }
    for (auto& command : recording.commands) {
        uint32_t type = 0;
        if (!reader.u64(command.tick) || !reader.u64(command.sequence)
            || !reader.u32(command.producer) || !reader.u32(type)
            || !reader.u32(command.body) || !reader.u32(command.generation)) {
            result.error = reader.error();
            return result;
        }
        command.type = static_cast<ReplayCommandType>(type);
        if (type > static_cast<uint32_t>(ReplayCommandType::SetAwake)) {
            result.error = "invalid replay command type";
            return result;
        }
        for (int32_t& value : command.payload) {
            if (!reader.i32(value)) { result.error = reader.error(); return result; }
        }
    }
    for (auto& hash : recording.hashes) {
        uint32_t stage = 0;
        uint32_t reserved = 0;
        if (!reader.u64(hash.tick) || !reader.u32(stage)
            || !reader.u32(hash.objectId) || !reader.u32(hash.hash)
            || !reader.u32(reserved)) {
            result.error = reader.error();
            return result;
        }
        hash.stage = static_cast<ReplayHashStage>(stage);
        if (stage < static_cast<uint32_t>(ReplayHashStage::Body)
            || stage > static_cast<uint32_t>(ReplayHashStage::World)) {
            result.error = "invalid replay hash stage";
            return result;
        }
        if (reserved != 0u) {
            result.error = "invalid replay hash reserved field";
            return result;
        }
    }
    if (reader.remaining() != 0u) {
        result.error = "unexpected replay trailing bytes";
        return result;
    }
    std::stable_sort(recording.commands.begin(), recording.commands.end(),
                     canonicalReplayCommandLess);
    std::sort(recording.hashes.begin(), recording.hashes.end(),
        [](const ReplayHashRecord& lhs, const ReplayHashRecord& rhs) {
            return std::tie(lhs.tick, lhs.stage, lhs.objectId)
                 < std::tie(rhs.tick, rhs.stage, rhs.objectId);
        });
    result.recording = std::move(recording);
    return result;
}

ReplayRecorder::ReplayRecorder(ReplayRecording recording)
    : recording_(std::move(recording)) {}

void ReplayRecorder::record(const CanonicalReplayCommand& command) {
    recording_.commands.push_back(command);
}

void ReplayRecorder::recordHash(const ReplayHashRecord& hash) {
    recording_.hashes.push_back(hash);
}

ReplayRecording ReplayRecorder::finish() && {
    std::stable_sort(recording_.commands.begin(), recording_.commands.end(),
                     canonicalReplayCommandLess);
    std::sort(recording_.hashes.begin(), recording_.hashes.end(),
        [](const ReplayHashRecord& lhs, const ReplayHashRecord& rhs) {
            return std::tie(lhs.tick, lhs.stage, lhs.objectId)
                 < std::tie(rhs.tick, rhs.stage, rhs.objectId);
        });
    return std::move(recording_);
}

bool applyCanonicalReplayCommand(
    LockstepWorld& world,
    const CanonicalReplayCommand& command) noexcept {
    if (command.body >= world.bodies().size()) return false;
    auto& body = world.bodies()[command.body];
    const bool isAlive = (body.identity[2] & LockstepBodyAlive) != 0u;
    if (command.type == ReplayCommandType::SpawnBody) {
        const bool firstGeneration = body.identity[1]
            != std::numeric_limits<uint32_t>::max()
            && body.identity[1] + 1u == command.generation;
        if (isAlive || (body.identity[1] != command.generation
                        && !firstGeneration)) return false;
    } else if (!isAlive || body.identity[1] != command.generation) {
        return false;
    }
    switch (command.type) {
        case ReplayCommandType::DestroyBody:
            body = {};
            body.identity[1] = command.generation + 1u;
            return true;
        case ReplayCommandType::SpawnBody:
            body.identity = {command.body, command.generation,
                             static_cast<uint32_t>(command.payload[11]), 0u};
            body.sectorRadius = {command.payload[0], command.payload[1],
                                 command.payload[2], command.payload[3]};
            body.positionInvMass = {command.payload[4], command.payload[5],
                                    command.payload[6], command.payload[7]};
            body.linearVelocity = {command.payload[8], command.payload[9],
                                   command.payload[10], 0};
            return true;
        case ReplayCommandType::Correction:
            body.sectorRadius[0] = command.payload[0];
            body.sectorRadius[1] = command.payload[1];
            body.sectorRadius[2] = command.payload[2];
            body.positionInvMass[0] = command.payload[4];
            body.positionInvMass[1] = command.payload[5];
            body.positionInvMass[2] = command.payload[6];
            body.linearVelocity[0] = command.payload[8];
            body.linearVelocity[1] = command.payload[9];
            body.linearVelocity[2] = command.payload[10];
            return true;
        case ReplayCommandType::SetVelocity:
            for (uint32_t axis = 0; axis < 3u; ++axis)
                body.linearVelocity[axis] = command.payload[axis];
            return true;
        case ReplayCommandType::ApplyImpulse:
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                body.linearVelocity[axis] = lockstepSaturatingAdd(
                    body.linearVelocity[axis], command.payload[axis]);
            }
            return true;
        case ReplayCommandType::SetAwake:
            if (command.payload[0] != 0)
                body.identity[2] |= LockstepBodyAwake;
            else
                body.identity[2] &= ~LockstepBodyAwake;
            return true;
    }
    return false;
}

ReplayPlayer::Result ReplayPlayer::play(
    const ReplayRecording& recording) const {
    Result result;
    if (recording.header.arithmeticMode != ArithmeticMode::Lockstep
        || recording.checkpoint.bodies.empty()) return result;
    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = std::max(
        recording.header.capacity.residentBodies,
        static_cast<uint32_t>(recording.checkpoint.bodies.size()));
    config.contactCapacity = std::max(
        {recording.header.capacity.contacts,
         static_cast<uint32_t>(recording.checkpoint.contacts.size()), 1u});
    config.tickRateHz = recording.header.simulation.tickRateHz;
    config.substeps = recording.header.simulation.substeps;
    config.solverIterations = recording.header.simulation.solverIterations;
    config.gravityPerSubstepQ16 =
        recording.header.simulation.gravityPerSubstepQ16;
    if (!world.initialize(config)
        || !world.setBodies(recording.checkpoint.bodies)) return result;

    std::vector<CanonicalReplayCommand> commands = recording.commands;
    std::stable_sort(commands.begin(), commands.end(), canonicalReplayCommandLess);
    std::vector<ReplayHashRecord> hashes = recording.hashes;
    std::sort(hashes.begin(), hashes.end(),
        [](const ReplayHashRecord& lhs, const ReplayHashRecord& rhs) {
            return std::tie(lhs.tick, lhs.stage, lhs.objectId)
                 < std::tie(rhs.tick, rhs.stage, rhs.objectId);
        });
    if (recording.checkpoint.tick > std::numeric_limits<uint32_t>::max())
        return result;
    uint64_t finalTick = recording.checkpoint.tick;
    for (const auto& command : commands) finalTick = std::max(finalTick, command.tick);
    for (const auto& hash : hashes) finalTick = std::max(finalTick, hash.tick);
    size_t commandIndex = 0;
    while (commandIndex < commands.size()
           && commands[commandIndex].tick <= recording.checkpoint.tick) {
        ++commandIndex;
    }
    size_t hashIndex = 0;
    if (!hashes.empty() && hashes.front().tick < recording.checkpoint.tick)
        return result;
    std::vector<uint32_t> checkpointRoots(world.bodies().size(),
                                          std::numeric_limits<uint32_t>::max());
    std::copy_n(recording.checkpoint.islandRoots.begin(),
                std::min(recording.checkpoint.islandRoots.size(),
                         checkpointRoots.size()),
                checkpointRoots.begin());
    result.finalTelemetry.tick = static_cast<uint32_t>(
        recording.checkpoint.tick);
    result.finalTelemetry.contacts = static_cast<uint32_t>(
        recording.checkpoint.contacts.size());
    result.finalTelemetry.hashes = LockstepWorld::computeHashes(
        world.bodies(), recording.checkpoint.contacts, checkpointRoots,
        result.finalTelemetry.tick, config.contactCapacity);
    for (const auto& body : world.bodies()) {
        result.finalTelemetry.liveBodies +=
            (body.identity[2] & LockstepBodyAlive) != 0u ? 1u : 0u;
    }
    while (hashIndex < hashes.size()
           && hashes[hashIndex].tick == recording.checkpoint.tick) {
        const auto& expected = hashes[hashIndex];
        const uint32_t actual = actualHash(result.finalTelemetry, expected);
        if (actual != expected.hash) {
            result.divergence = ReplayDivergence{
                .tick = recording.checkpoint.tick,
                .stage = expected.stage,
                .objectId = expected.objectId,
                .expected = expected.hash,
                .actual = actual,
                .message = "first replay divergence in checkpoint",
            };
            return result;
        }
        ++hashIndex;
    }
    if (finalTick > std::numeric_limits<uint32_t>::max()) return result;
    for (uint64_t tick = recording.checkpoint.tick + 1u;
         tick <= finalTick; ++tick) {
        while (commandIndex < commands.size()
               && commands[commandIndex].tick == tick) {
            static_cast<void>(applyCanonicalReplayCommand(
                world, commands[commandIndex]));
            ++commandIndex;
        }
        result.finalTelemetry = world.step(static_cast<uint32_t>(tick));
        while (hashIndex < hashes.size() && hashes[hashIndex].tick == tick) {
            const auto& expected = hashes[hashIndex];
            const uint32_t actual = actualHash(result.finalTelemetry, expected);
            if (actual != expected.hash) {
                std::ostringstream message;
                message << "first replay divergence at tick " << tick
                        << ", stage " << static_cast<uint32_t>(expected.stage)
                        << ", object " << expected.objectId;
                result.divergence = ReplayDivergence{
                    .tick = tick,
                    .stage = expected.stage,
                    .objectId = expected.objectId,
                    .expected = expected.hash,
                    .actual = actual,
                    .message = message.str(),
                };
                return result;
            }
            ++hashIndex;
        }
    }
    result.completed = hashIndex == hashes.size();
    return result;
}

} // namespace voxy::physics::deterministic
