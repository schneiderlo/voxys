#include "game/wreckwater_replay.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

namespace voxy::game {
namespace {

constexpr std::array<std::byte, 8> kReplayMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'W'}, std::byte{'R'}, std::byte{'P'}, std::byte{'L'}};
constexpr std::array<std::byte, 8> kSnapshotMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'W'}, std::byte{'S'}, std::byte{'N'}, std::byte{'P'}};
constexpr std::array<std::byte, 8> kInitialHashTag{
    std::byte{'W'}, std::byte{'R'}, std::byte{'E'}, std::byte{'P'},
    std::byte{'L'}, std::byte{'A'}, std::byte{'Y'}, std::byte{'1'}};
constexpr std::array<std::byte, 8> kFinalHashTag{
    std::byte{'W'}, std::byte{'R'}, std::byte{'F'}, std::byte{'I'},
    std::byte{'N'}, std::byte{'A'}, std::byte{'L'}, std::byte{'1'}};
constexpr uint64_t kFnv64Offset = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnv64Prime = 1'099'511'628'211ull;
constexpr size_t kArchiveHashOffset = 120u;

template <typename Enum>
[[nodiscard]] constexpr uint32_t enumWord(Enum value) noexcept {
    static_assert(std::is_enum_v<Enum>);
    return static_cast<uint32_t>(value);
}

class HashWriter {
public:
    void raw(std::span<const std::byte> bytes) noexcept {
        for (const std::byte value : bytes) {
            hash_ =
                (hash_ ^ std::to_integer<uint64_t>(value)) * kFnv64Prime;
        }
    }

    void u32(uint32_t value) noexcept {
        for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
            const std::byte byte{
                static_cast<uint8_t>(value >> shift)};
            raw(std::span<const std::byte>(&byte, 1u));
        }
    }

    void u64(uint64_t value) noexcept {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }

    [[nodiscard]] uint64_t value() const noexcept { return hash_; }

private:
    uint64_t hash_ = kFnv64Offset;
};

class VectorWriter {
public:
    explicit VectorWriter(size_t capacity) { bytes_.reserve(capacity); }

    void raw(std::span<const std::byte> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    void u32(uint32_t value) {
        for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
            bytes_.push_back(
                std::byte{static_cast<uint8_t>(value >> shift)});
        }
    }

    void u64(uint64_t value) {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }

    [[nodiscard]] std::vector<std::byte>& bytes() noexcept {
        return bytes_;
    }

private:
    std::vector<std::byte> bytes_;
};

class FixedWriter {
public:
    explicit FixedWriter(std::span<std::byte> bytes) : bytes_(bytes) {}

    void u32(uint32_t value) noexcept {
        if (remaining() < sizeof(uint32_t)) {
            valid_ = false;
            return;
        }
        for (uint32_t byte = 0u; byte < 4u; ++byte) {
            bytes_[offset_ + byte] =
                std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
        }
        offset_ += sizeof(uint32_t);
    }

    void u64(uint64_t value) noexcept {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }

    [[nodiscard]] bool complete() const noexcept {
        return valid_ && offset_ == bytes_.size();
    }

private:
    [[nodiscard]] size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    std::span<std::byte> bytes_;
    size_t offset_ = 0u;
    bool valid_ = true;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool skip(size_t count) noexcept {
        if (count > remaining()) return false;
        offset_ += count;
        return true;
    }

    [[nodiscard]] bool u32(uint32_t& value) noexcept {
        if (remaining() < sizeof(uint32_t)) return false;
        value = 0u;
        for (uint32_t byte = 0u; byte < 4u; ++byte) {
            value |= std::to_integer<uint32_t>(
                         bytes_[offset_ + byte])
                << (byte * 8u);
        }
        offset_ += sizeof(uint32_t);
        return true;
    }

    [[nodiscard]] bool u64(uint64_t& value) noexcept {
        uint32_t low = 0u;
        uint32_t high = 0u;
        if (!u32(low) || !u32(high)) return false;
        value = uint64_t{low} | (uint64_t{high} << 32u);
        return true;
    }

    [[nodiscard]] bool bytes(
        size_t count, std::span<const std::byte>& value) noexcept {
        if (count > remaining()) return false;
        value = bytes_.subspan(offset_, count);
        offset_ += count;
        return true;
    }

    [[nodiscard]] size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] size_t offset() const noexcept { return offset_; }

private:
    std::span<const std::byte> bytes_;
    size_t offset_ = 0u;
};

[[nodiscard]] bool checkedAdd(
    size_t lhs, size_t rhs, size_t& result) noexcept {
    if (lhs > std::numeric_limits<size_t>::max() - rhs) return false;
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checkedMultiply(
    size_t lhs, size_t rhs, size_t& result) noexcept {
    if (lhs != 0u
        && rhs > std::numeric_limits<size_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] uint64_t byteHash(
    std::span<const std::byte> bytes) noexcept {
    HashWriter writer;
    writer.raw(bytes);
    return writer.value();
}

[[nodiscard]] uint64_t archiveHash(
    std::span<const std::byte> bytes) noexcept {
    HashWriter writer;
    for (size_t index = 0u; index < bytes.size(); ++index) {
        const std::byte value =
            index >= kArchiveHashOffset
                && index < kArchiveHashOffset + sizeof(uint64_t)
            ? std::byte{0u}
            : bytes[index];
        writer.raw(std::span<const std::byte>(&value, 1u));
    }
    return writer.value();
}

void patchU64(
    std::vector<std::byte>& bytes, size_t offset,
    uint64_t value) noexcept {
    if (offset > bytes.size()
        || bytes.size() - offset < sizeof(uint64_t)) {
        return;
    }
    for (uint32_t byte = 0u; byte < 8u; ++byte) {
        bytes[offset + byte] =
            std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
    }
}

[[nodiscard]] bool magicMatches(
    std::span<const std::byte> bytes,
    std::span<const std::byte> magic) noexcept {
    return bytes.size() >= magic.size()
        && std::equal(magic.begin(), magic.end(), bytes.begin());
}

[[nodiscard]] bool validConfig(
    const WreckwaterReplayConfig& config) noexcept {
    return config.sessionId != 0u
        && config.matchId != 0u
        && config.worldId != 0u
        && config.worldEpoch != 0u
        && config.authorityEpoch != 0u
        && config.tickRateHz == kWreckwaterReplayTickRateHz
        && config.contentHash != 0u
        && config.matchConfigurationHash != 0u
        && config.maximumRecords != 0u
        && config.maximumRecords <= kWreckwaterReplayMaximumRecords
        && config.maximumPayloadBytes != 0u
        && config.maximumPayloadBytes
            <= kWreckwaterReplayMaximumPayloadBytes;
}

[[nodiscard]] bool validRecordType(
    WreckwaterReplayRecordType type) noexcept {
    return type == WreckwaterReplayRecordType::MatchEvent
        || type == WreckwaterReplayRecordType::CertifiedSnapshot;
}

[[nodiscard]] bool validMatchEventSource(
    MatchEventSource source) noexcept {
    switch (source) {
        case MatchEventSource::MatchSystem:
        case MatchEventSource::PlayerCommand:
        case MatchEventSource::AuthoritativeWorldEvent:
            return true;
    }
    return false;
}

[[nodiscard]] bool validMatchCommandType(
    MatchCommandType type) noexcept {
    switch (type) {
        case MatchCommandType::TowCargo:
        case MatchCommandType::CutTow:
        case MatchCommandType::StealCargo:
        case MatchCommandType::BankCargo:
            return true;
    }
    return false;
}

[[nodiscard]] bool validMatchEventType(
    MatchEventType type) noexcept {
    return enumWord(type) >= enumWord(MatchEventType::PlayerRegistered)
        && enumWord(type) <= enumWord(MatchEventType::AuthorityFault);
}

[[nodiscard]] bool validWorldEventType(
    AuthoritativeWorldEventType type) noexcept {
    return enumWord(type)
            >= enumWord(AuthoritativeWorldEventType::AttachmentBroken)
        && enumWord(type)
            <= enumWord(AuthoritativeWorldEventType::SkiffRespawned);
}

[[nodiscard]] bool validPhase(MatchPhase phase) noexcept {
    return enumWord(phase) >= enumWord(MatchPhase::Warmup)
        && enumWord(phase) <= enumWord(MatchPhase::Finished);
}

[[nodiscard]] bool validOutcome(MatchOutcomeType outcome) noexcept {
    return enumWord(outcome)
            >= enumWord(MatchOutcomeType::Undecided)
        && enumWord(outcome) <= enumWord(MatchOutcomeType::Tie);
}

[[nodiscard]] bool validFault(MatchFaultReason reason) noexcept {
    return enumWord(reason) >= enumWord(MatchFaultReason::None)
        && enumWord(reason)
            <= enumWord(MatchFaultReason::CommittedInvariantViolation);
}

[[nodiscard]] bool validCommandRejection(
    CommandRejectReason reason) noexcept {
    return enumWord(reason) >= enumWord(CommandRejectReason::None)
        && enumWord(reason)
            <= enumWord(CommandRejectReason::AuthorityFaulted);
}

[[nodiscard]] bool validWorldEventRejection(
    WorldEventRejectReason reason) noexcept {
    return enumWord(reason) >= enumWord(WorldEventRejectReason::None)
        && enumWord(reason)
            <= enumWord(WorldEventRejectReason::AuthorityFaulted);
}

[[nodiscard]] bool validCrew(CrewId crew) noexcept {
    return crew == CrewId::None
        || crew == CrewId::CrewOne
        || crew == CrewId::CrewTwo;
}

[[nodiscard]] bool validEvent(
    const MatchEvent& event,
    const WreckwaterReplayConfig& config) noexcept {
    return event.schemaVersion == kWreckwaterMatchSchemaVersion
        && event.eventSequence != 0u
        && event.matchId == config.matchId
        && event.worldId == config.worldId
        && event.worldEpoch == config.worldEpoch
        && event.authorityEpoch == config.authorityEpoch
        && event.tick <= network::kWreckwaterMaximumApplicationTick
        && event.sourcePhysicsTick
            <= network::kWreckwaterMaximumApplicationTick
        && event.commandTick
            <= network::kWreckwaterMaximumApplicationTick
        && event.sourcePhysicsTick <= event.tick
        && validMatchEventSource(event.source)
        && validMatchCommandType(event.commandType)
        && validMatchEventType(event.type)
        && validWorldEventType(event.worldEventType)
        && validPhase(event.phase)
        && validOutcome(event.outcome)
        && validFault(event.faultReason)
        && validCommandRejection(event.commandRejection)
        && validWorldEventRejection(event.worldEventRejection)
        && validCrew(event.crew)
        && event.seat < kWreckwaterPlayersPerCrew
        && event.crewOneScore <= network::kWreckwaterMaximumScore
        && event.crewTwoScore <= network::kWreckwaterMaximumScore;
}

[[nodiscard]] bool encodeEvent(
    const MatchEvent& event,
    std::span<std::byte> destination) noexcept {
    if (destination.size() != kWreckwaterReplayMatchEventBytes) {
        return false;
    }
    FixedWriter writer(destination);
    writer.u32(event.schemaVersion);
    writer.u64(event.eventSequence);
    writer.u64(event.matchId);
    writer.u64(event.worldId);
    writer.u32(event.worldEpoch);
    writer.u32(event.authorityEpoch);
    writer.u64(event.tick);
    writer.u64(event.sourcePhysicsTick);
    writer.u64(event.commandTick);
    writer.u64(event.sourceSequence);
    writer.u32(enumWord(event.source));
    writer.u32(event.sourceStreamId);
    writer.u32(enumWord(event.commandType));
    writer.u32(enumWord(event.type));
    writer.u32(enumWord(event.worldEventType));
    writer.u32(enumWord(event.phase));
    writer.u32(enumWord(event.outcome));
    writer.u32(enumWord(event.faultReason));
    writer.u32(enumWord(event.commandRejection));
    writer.u32(enumWord(event.worldEventRejection));
    writer.u64(event.playerId);
    writer.u32(event.seat);
    writer.u64(event.connectionId);
    writer.u32(event.connectionGeneration);
    writer.u32(enumWord(event.crew));
    writer.u32(event.cargoId);
    writer.u32(event.cargoGeneration);
    writer.u32(event.observedCargoRevision);
    writer.u32(event.cargoRevision);
    writer.u32(event.sourceSkiff);
    writer.u32(event.sourceSkiffGeneration);
    writer.u32(event.targetSkiff);
    writer.u32(event.targetSkiffGeneration);
    writer.u64(event.attachmentId);
    writer.u32(event.attachmentGeneration);
    writer.u64(event.sourceAttachmentId);
    writer.u32(event.sourceAttachmentGeneration);
    writer.u64(event.resultingAttachmentId);
    writer.u32(event.resultingAttachmentGeneration);
    writer.u32(event.crewOneScore);
    writer.u32(event.crewTwoScore);
    writer.u32(event.stateHash);
    return writer.complete();
}

[[nodiscard]] bool decodeEvent(
    std::span<const std::byte> payload,
    MatchEvent& event) noexcept {
    if (payload.size() != kWreckwaterReplayMatchEventBytes) return false;
    Reader reader(payload);
    uint32_t source = 0u;
    uint32_t commandType = 0u;
    uint32_t type = 0u;
    uint32_t worldEventType = 0u;
    uint32_t phase = 0u;
    uint32_t outcome = 0u;
    uint32_t fault = 0u;
    uint32_t commandRejection = 0u;
    uint32_t worldEventRejection = 0u;
    uint32_t crew = 0u;
    if (!reader.u32(event.schemaVersion)
        || !reader.u64(event.eventSequence)
        || !reader.u64(event.matchId)
        || !reader.u64(event.worldId)
        || !reader.u32(event.worldEpoch)
        || !reader.u32(event.authorityEpoch)
        || !reader.u64(event.tick)
        || !reader.u64(event.sourcePhysicsTick)
        || !reader.u64(event.commandTick)
        || !reader.u64(event.sourceSequence)
        || !reader.u32(source)
        || !reader.u32(event.sourceStreamId)
        || !reader.u32(commandType)
        || !reader.u32(type)
        || !reader.u32(worldEventType)
        || !reader.u32(phase)
        || !reader.u32(outcome)
        || !reader.u32(fault)
        || !reader.u32(commandRejection)
        || !reader.u32(worldEventRejection)
        || !reader.u64(event.playerId)
        || !reader.u32(event.seat)
        || !reader.u64(event.connectionId)
        || !reader.u32(event.connectionGeneration)
        || !reader.u32(crew)
        || !reader.u32(event.cargoId)
        || !reader.u32(event.cargoGeneration)
        || !reader.u32(event.observedCargoRevision)
        || !reader.u32(event.cargoRevision)
        || !reader.u32(event.sourceSkiff)
        || !reader.u32(event.sourceSkiffGeneration)
        || !reader.u32(event.targetSkiff)
        || !reader.u32(event.targetSkiffGeneration)
        || !reader.u64(event.attachmentId)
        || !reader.u32(event.attachmentGeneration)
        || !reader.u64(event.sourceAttachmentId)
        || !reader.u32(event.sourceAttachmentGeneration)
        || !reader.u64(event.resultingAttachmentId)
        || !reader.u32(event.resultingAttachmentGeneration)
        || !reader.u32(event.crewOneScore)
        || !reader.u32(event.crewTwoScore)
        || !reader.u32(event.stateHash)
        || reader.remaining() != 0u) {
        return false;
    }
    event.source = static_cast<MatchEventSource>(source);
    event.commandType = static_cast<MatchCommandType>(commandType);
    event.type = static_cast<MatchEventType>(type);
    event.worldEventType =
        static_cast<AuthoritativeWorldEventType>(worldEventType);
    event.phase = static_cast<MatchPhase>(phase);
    event.outcome = static_cast<MatchOutcomeType>(outcome);
    event.faultReason = static_cast<MatchFaultReason>(fault);
    event.commandRejection =
        static_cast<CommandRejectReason>(commandRejection);
    event.worldEventRejection =
        static_cast<WorldEventRejectReason>(worldEventRejection);
    event.crew = static_cast<CrewId>(crew);
    return true;
}

struct SnapshotEnvelope {
    uint32_t schemaVersion = 0u;
    uint32_t payloadType = 0u;
    uint32_t payloadBytes = 0u;
    uint32_t flags = 0u;
    uint64_t sessionId = 0u;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint64_t snapshotSequence = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    uint32_t phase = 0u;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    uint32_t outcome = 0u;
    uint32_t winner = 0u;
    uint32_t matchStateHash = 0u;
    uint32_t eventStreamHash = 0u;
    uint32_t entityCount = 0u;
    uint32_t characterCount = 0u;
    uint32_t reserved = 0u;
    uint64_t serializedByteHash = 0u;
};

[[nodiscard]] bool readSnapshotEnvelope(
    std::span<const std::byte> payload,
    SnapshotEnvelope& envelope) noexcept {
    if (payload.size() < network::kWreckwaterSnapshotHeaderBytes
        || !magicMatches(payload, kSnapshotMagic)) {
        return false;
    }
    Reader reader(payload);
    if (!reader.skip(kSnapshotMagic.size())
        || !reader.u32(envelope.schemaVersion)
        || !reader.u32(envelope.payloadType)
        || !reader.u32(envelope.payloadBytes)
        || !reader.u32(envelope.flags)
        || !reader.u64(envelope.sessionId)
        || !reader.u64(envelope.matchId)
        || !reader.u64(envelope.worldId)
        || !reader.u32(envelope.worldEpoch)
        || !reader.u32(envelope.authorityEpoch)
        || !reader.u64(envelope.snapshotSequence)
        || !reader.u64(envelope.applicationTick)
        || !reader.u64(envelope.physicsEvidenceTick)
        || !reader.u32(envelope.phase)
        || !reader.u32(envelope.crewOneScore)
        || !reader.u32(envelope.crewTwoScore)
        || !reader.u32(envelope.outcome)
        || !reader.u32(envelope.winner)
        || !reader.u32(envelope.matchStateHash)
        || !reader.u32(envelope.eventStreamHash)
        || !reader.u32(envelope.entityCount)
        || !reader.u32(envelope.characterCount)
        || !reader.u32(envelope.reserved)
        || !reader.u64(envelope.serializedByteHash)) {
        return false;
    }
    size_t expectedBytes = 0u;
    return envelope.schemaVersion
            == network::kWreckwaterWireSchemaVersion
        && envelope.payloadType
            == enumWord(network::WreckwaterPayloadType::CertifiedSnapshot)
        && envelope.payloadBytes == payload.size()
        && envelope.flags
            == network::kWreckwaterCertifiedFullSnapshotFlag
        && envelope.reserved == 0u
        && network::wreckwaterSnapshotPayloadBytes(
            envelope.entityCount, envelope.characterCount,
            expectedBytes)
        && expectedBytes == payload.size()
        && envelope.serializedByteHash
            == network::wreckwaterSnapshotSerializedByteHash(payload);
}

[[nodiscard]] bool envelopeMatchesConfig(
    const SnapshotEnvelope& envelope,
    const WreckwaterReplayConfig& config) noexcept {
    return envelope.sessionId == config.sessionId
        && envelope.matchId == config.matchId
        && envelope.worldId == config.worldId
        && envelope.worldEpoch == config.worldEpoch
        && envelope.authorityEpoch == config.authorityEpoch;
}

[[nodiscard]] uint64_t initialReplayHash(
    const WreckwaterReplayConfig& config) noexcept {
    HashWriter writer;
    writer.raw(kInitialHashTag);
    writer.u32(kWreckwaterReplaySchemaVersion);
    writer.u64(config.sessionId);
    writer.u64(config.matchId);
    writer.u64(config.worldId);
    writer.u32(config.worldEpoch);
    writer.u32(config.authorityEpoch);
    writer.u32(config.tickRateHz);
    writer.u64(config.contentHash);
    writer.u32(config.matchConfigurationHash);
    return writer.value();
}

[[nodiscard]] uint64_t recordRollingHash(
    uint64_t prior,
    WreckwaterReplayRecordType type,
    uint32_t payloadSchemaVersion,
    uint64_t applicationTick,
    uint64_t physicsEvidenceTick,
    uint64_t sequence,
    uint32_t flags,
    uint32_t payloadBytes,
    uint64_t payloadHash,
    std::span<const std::byte> payload) noexcept {
    HashWriter writer;
    writer.u64(prior);
    writer.u32(enumWord(type));
    writer.u32(payloadSchemaVersion);
    writer.u64(applicationTick);
    writer.u64(physicsEvidenceTick);
    writer.u64(sequence);
    writer.u32(payloadBytes);
    writer.u32(flags);
    writer.u64(payloadHash);
    writer.raw(payload);
    return writer.value();
}

[[nodiscard]] uint64_t finalReplayHash(
    uint64_t rollingHash,
    const WreckwaterReplaySummary& summary) noexcept {
    HashWriter writer;
    writer.raw(kFinalHashTag);
    writer.u64(rollingHash);
    writer.u64(summary.firstApplicationTick);
    writer.u64(summary.lastApplicationTick);
    writer.u64(summary.recordCount);
    writer.u64(summary.payloadBytes);
    writer.u32(summary.finalMatchStateHash);
    writer.u32(summary.finalEventStreamHash);
    return writer.value();
}

struct DecodeOrdering {
    uint64_t nextEventSequence = 1u;
    uint64_t nextSnapshotSequence = 1u;
    uint64_t lastTick = 0u;
    uint64_t firstTick = 0u;
    uint32_t lastSnapshotMatchStateHash = 0u;
    uint32_t lastSnapshotEventStreamHash = 0u;
    uint64_t lastSnapshotApplicationTick = 0u;
    uint64_t lastSnapshotEvidenceTick = 0u;
    uint32_t lastSnapshotPhase = 0u;
    bool seenEvent = false;
    bool hasRecord = false;
    bool seenSnapshot = false;
};

struct ReplayPreflightResult {
    WreckwaterReplayError error = WreckwaterReplayError::None;
    size_t failingRecord = 0u;
};

[[nodiscard]] WreckwaterReplayError validateRecordOrder(
    const WreckwaterReplayRecord& record,
    DecodeOrdering& order) noexcept {
    if (!validRecordType(record.type)
        || record.flags != 0u
        || record.sequence == 0u
        || record.applicationTick
            > network::kWreckwaterMaximumApplicationTick
        || record.physicsEvidenceTick
            > network::kWreckwaterMaximumApplicationTick
        || (order.hasRecord
            && record.applicationTick < order.lastTick)) {
        return WreckwaterReplayError::OutOfOrder;
    }
    uint64_t* expected =
        record.type == WreckwaterReplayRecordType::MatchEvent
        ? &order.nextEventSequence
        : &order.nextSnapshotSequence;
    if (*expected == 0u || record.sequence != *expected) {
        return WreckwaterReplayError::OutOfOrder;
    }
    *expected =
        record.sequence == std::numeric_limits<uint64_t>::max()
        ? 0u
        : record.sequence + 1u;
    if (!order.hasRecord) order.firstTick = record.applicationTick;
    order.lastTick = record.applicationTick;
    order.hasRecord = true;
    return WreckwaterReplayError::None;
}

[[nodiscard]] ReplayPreflightResult preflightReplayRecords(
    std::span<const std::byte> bytes,
    uint32_t recordCount,
    const WreckwaterReplayConfig& config,
    const WreckwaterReplaySummary& summary) noexcept {
    ReplayPreflightResult result;
    Reader reader(bytes);
    if (!reader.skip(kWreckwaterReplayArchiveHeaderBytes)) {
        result.error = WreckwaterReplayError::Truncated;
        return result;
    }

    DecodeOrdering order;
    uint64_t rollingHash = initialReplayHash(config);
    uint32_t eventStreamRollingHash =
        wreckwaterInitialMatchEventStreamRollingHash(
            config.matchConfigurationHash);
    WreckwaterReplayRecordType lastType =
        WreckwaterReplayRecordType::MatchEvent;
    for (uint32_t index = 0u; index < recordCount; ++index) {
        result.failingRecord = index;
        uint32_t type = 0u;
        WreckwaterReplayRecord record;
        if (!reader.u32(type)
            || !reader.u32(record.payloadSchemaVersion)
            || !reader.u64(record.applicationTick)
            || !reader.u64(record.physicsEvidenceTick)
            || !reader.u64(record.sequence)
            || !reader.u32(record.payloadBytes)
            || !reader.u32(record.flags)
            || !reader.u64(record.payloadHash)
            || !reader.u64(record.rollingHash)) {
            result.error = WreckwaterReplayError::Truncated;
            return result;
        }
        record.type = static_cast<WreckwaterReplayRecordType>(type);
        if (record.payloadSchemaVersion == 0u
            || record.payloadBytes == 0u
            || record.payloadBytes > reader.remaining()) {
            result.error = WreckwaterReplayError::InvalidRecord;
            return result;
        }
        if (record.type == WreckwaterReplayRecordType::MatchEvent
            && (record.payloadSchemaVersion
                    != kWreckwaterMatchSchemaVersion
                || record.payloadBytes
                    != kWreckwaterReplayMatchEventBytes)) {
            result.error = WreckwaterReplayError::InvalidEvent;
            return result;
        }
        if (record.type
                == WreckwaterReplayRecordType::CertifiedSnapshot
            && record.payloadSchemaVersion
                != network::kWreckwaterWireSchemaVersion) {
            result.error = WreckwaterReplayError::InvalidSnapshot;
            return result;
        }

        std::span<const std::byte> payload;
        if (!reader.bytes(record.payloadBytes, payload)) {
            result.error = WreckwaterReplayError::Truncated;
            return result;
        }
        if (record.payloadHash != byteHash(payload)) {
            result.error = WreckwaterReplayError::PayloadHashMismatch;
            return result;
        }
        const uint64_t expectedRollingHash = recordRollingHash(
            rollingHash, record.type, record.payloadSchemaVersion,
            record.applicationTick, record.physicsEvidenceTick,
            record.sequence, record.flags, record.payloadBytes,
            record.payloadHash, payload);
        if (record.rollingHash != expectedRollingHash) {
            result.error = WreckwaterReplayError::ReplayHashMismatch;
            return result;
        }
        const WreckwaterReplayError orderError =
            validateRecordOrder(record, order);
        if (orderError != WreckwaterReplayError::None) {
            result.error = orderError;
            return result;
        }

        if (record.type == WreckwaterReplayRecordType::MatchEvent) {
            MatchEvent event;
            if (!decodeEvent(payload, event)
                || !validEvent(event, config)
                || event.eventSequence != record.sequence
                || event.tick != record.applicationTick
                || event.sourcePhysicsTick
                    != record.physicsEvidenceTick) {
                result.error = WreckwaterReplayError::InvalidEvent;
                return result;
            }
            order.seenEvent = true;
            eventStreamRollingHash =
                wreckwaterAppendMatchEventStreamRollingHash(
                    eventStreamRollingHash, event);
        } else if (
            record.type
            == WreckwaterReplayRecordType::CertifiedSnapshot) {
            SnapshotEnvelope envelope;
            if (!readSnapshotEnvelope(payload, envelope)
                || !envelopeMatchesConfig(envelope, config)
                || envelope.snapshotSequence != record.sequence
                || envelope.applicationTick
                    != record.applicationTick
                || envelope.physicsEvidenceTick
                    != record.physicsEvidenceTick) {
                result.error = WreckwaterReplayError::InvalidSnapshot;
                return result;
            }
            if (network::validateWreckwaterSnapshotBytes(payload)
                != network::WreckwaterCodecError::None) {
                result.error =
                    WreckwaterReplayError::SnapshotCodecFailure;
                return result;
            }
            if (order.nextEventSequence == 0u
                || order.nextEventSequence - 1u
                    > std::numeric_limits<uint32_t>::max()
                || envelope.eventStreamHash
                    != wreckwaterFinalizeMatchEventStreamHash(
                        eventStreamRollingHash,
                        config.matchConfigurationHash,
                        envelope.matchStateHash,
                        order.nextEventSequence,
                        static_cast<uint32_t>(
                            order.nextEventSequence - 1u))) {
                result.error = WreckwaterReplayError::InvalidSnapshot;
                return result;
            }
            if (order.seenSnapshot
                && envelope.physicsEvidenceTick
                    <= order.lastSnapshotEvidenceTick) {
                result.error = WreckwaterReplayError::OutOfOrder;
                return result;
            }
            order.lastSnapshotMatchStateHash =
                envelope.matchStateHash;
            order.lastSnapshotEventStreamHash =
                envelope.eventStreamHash;
            order.lastSnapshotApplicationTick =
                envelope.applicationTick;
            order.lastSnapshotEvidenceTick =
                envelope.physicsEvidenceTick;
            order.lastSnapshotPhase = envelope.phase;
            order.seenSnapshot = true;
        } else {
            result.error = WreckwaterReplayError::InvalidRecord;
            return result;
        }
        rollingHash = expectedRollingHash;
        lastType = record.type;
    }

    if (reader.remaining() != 0u) {
        result.error = WreckwaterReplayError::TrailingBytes;
        return result;
    }
    if (!order.hasRecord || !order.seenEvent || !order.seenSnapshot
        || lastType
            != WreckwaterReplayRecordType::CertifiedSnapshot
        || order.lastSnapshotPhase
            != enumWord(network::WreckwaterPhase::Finished)
        || order.lastSnapshotEvidenceTick
            != order.lastSnapshotApplicationTick
        || summary.firstApplicationTick != order.firstTick
        || summary.lastApplicationTick != order.lastTick
        || summary.finalMatchStateHash
            != order.lastSnapshotMatchStateHash
        || summary.finalEventStreamHash
            != order.lastSnapshotEventStreamHash
        || summary.replayHash
            != finalReplayHash(rollingHash, summary)) {
        result.error = WreckwaterReplayError::ReplayHashMismatch;
        return result;
    }
    result.failingRecord = recordCount;
    return result;
}

void writeRecordHeader(
    VectorWriter& writer,
    const WreckwaterReplayRecord& record) {
    writer.u32(enumWord(record.type));
    writer.u32(record.payloadSchemaVersion);
    writer.u64(record.applicationTick);
    writer.u64(record.physicsEvidenceTick);
    writer.u64(record.sequence);
    writer.u32(record.payloadBytes);
    writer.u32(record.flags);
    writer.u64(record.payloadHash);
    writer.u64(record.rollingHash);
}

} // namespace

const char* wreckwaterReplayErrorName(
    WreckwaterReplayError error) noexcept {
    switch (error) {
        case WreckwaterReplayError::None:
            return "none";
        case WreckwaterReplayError::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterReplayError::NotInitialized:
            return "not initialized";
        case WreckwaterReplayError::AlreadyInitialized:
            return "already initialized";
        case WreckwaterReplayError::AlreadyFinalized:
            return "already finalized";
        case WreckwaterReplayError::NotFinalized:
            return "not finalized";
        case WreckwaterReplayError::InvalidIdentity:
            return "invalid identity";
        case WreckwaterReplayError::InvalidEvent:
            return "invalid event";
        case WreckwaterReplayError::InvalidSnapshot:
            return "invalid snapshot";
        case WreckwaterReplayError::InvalidRecord:
            return "invalid record";
        case WreckwaterReplayError::OutOfOrder:
            return "out of order";
        case WreckwaterReplayError::RecordCapacity:
            return "record capacity";
        case WreckwaterReplayError::PayloadCapacity:
            return "payload capacity";
        case WreckwaterReplayError::SizeOverflow:
            return "size overflow";
        case WreckwaterReplayError::AllocationFailed:
            return "allocation failed";
        case WreckwaterReplayError::InvalidMagic:
            return "invalid magic";
        case WreckwaterReplayError::UnsupportedVersion:
            return "unsupported version";
        case WreckwaterReplayError::Truncated:
            return "truncated";
        case WreckwaterReplayError::TrailingBytes:
            return "trailing bytes";
        case WreckwaterReplayError::ReservedNonZero:
            return "reserved field is nonzero";
        case WreckwaterReplayError::PayloadHashMismatch:
            return "payload hash mismatch";
        case WreckwaterReplayError::ReplayHashMismatch:
            return "replay hash mismatch";
        case WreckwaterReplayError::ArchiveHashMismatch:
            return "archive hash mismatch";
        case WreckwaterReplayError::SnapshotCodecFailure:
            return "snapshot codec failure";
    }
    return "unknown";
}

WreckwaterReplayError WreckwaterReplayRecorder::initialize(
    const WreckwaterReplayConfig& config) {
    if (initialized_) return WreckwaterReplayError::AlreadyInitialized;
    if (!validConfig(config)) {
        return WreckwaterReplayError::InvalidConfiguration;
    }

    std::vector<WreckwaterReplayRecord> replacementRecords;
    std::vector<std::byte> replacementPayload;
#if defined(__cpp_exceptions)
    try {
        replacementRecords.reserve(config.maximumRecords);
        replacementPayload.reserve(config.maximumPayloadBytes);
    } catch (const std::bad_alloc&) {
        return WreckwaterReplayError::AllocationFailed;
    }
#else
    replacementRecords.reserve(config.maximumRecords);
    replacementPayload.reserve(config.maximumPayloadBytes);
#endif

    config_ = config;
    records_ = std::move(replacementRecords);
    payload_ = std::move(replacementPayload);
    summary_ = {};
    rollingHash_ = initialReplayHash(config_);
    eventStreamRollingHash_ =
        wreckwaterInitialMatchEventStreamRollingHash(
            config_.matchConfigurationHash);
    nextMatchEventSequence_ = 1u;
    nextSnapshotSequence_ = 1u;
    lastSnapshotMatchStateHash_ = 0u;
    lastSnapshotEventStreamHash_ = 0u;
    lastSnapshotEvidenceTick_ = 0u;
    seenSnapshot_ = false;
    finalized_ = false;
    initialized_ = true;
    return WreckwaterReplayError::None;
}

WreckwaterReplayError WreckwaterReplayRecorder::appendRecord(
    WreckwaterReplayRecordType type,
    uint32_t payloadSchemaVersion,
    uint64_t applicationTick,
    uint64_t physicsEvidenceTick,
    uint64_t sequence,
    std::span<const std::byte> source) noexcept {
    if (!initialized_) return WreckwaterReplayError::NotInitialized;
    if (finalized_) return WreckwaterReplayError::AlreadyFinalized;
    if (!validRecordType(type)
        || payloadSchemaVersion == 0u
        || source.empty()
        || source.size() > std::numeric_limits<uint32_t>::max()
        || sequence == 0u
        || applicationTick
            > network::kWreckwaterMaximumApplicationTick
        || physicsEvidenceTick
            > network::kWreckwaterMaximumApplicationTick) {
        return WreckwaterReplayError::InvalidRecord;
    }
    if (!records_.empty()
        && applicationTick < records_.back().applicationTick) {
        return WreckwaterReplayError::OutOfOrder;
    }
    if (records_.size() >= config_.maximumRecords) {
        return WreckwaterReplayError::RecordCapacity;
    }
    if (source.size() > config_.maximumPayloadBytes - payload_.size()) {
        return WreckwaterReplayError::PayloadCapacity;
    }

    const uint64_t sourceHash = byteHash(source);
    const uint32_t sourceBytes = static_cast<uint32_t>(source.size());
    const uint64_t nextRollingHash = recordRollingHash(
        rollingHash_, type, payloadSchemaVersion, applicationTick,
        physicsEvidenceTick, sequence, 0u, sourceBytes, sourceHash,
        source);
    const size_t offset = payload_.size();
    payload_.resize(offset + source.size());
    std::memmove(
        payload_.data() + offset, source.data(), source.size());
    records_.push_back({
        .type = type,
        .payloadSchemaVersion = payloadSchemaVersion,
        .applicationTick = applicationTick,
        .physicsEvidenceTick = physicsEvidenceTick,
        .sequence = sequence,
        .flags = 0u,
        .payloadOffset = offset,
        .payloadBytes = sourceBytes,
        .payloadHash = sourceHash,
        .rollingHash = nextRollingHash,
    });

    if (summary_.recordCount == 0u) {
        summary_.firstApplicationTick = applicationTick;
    }
    summary_.lastApplicationTick = applicationTick;
    ++summary_.recordCount;
    summary_.payloadBytes += source.size();
    rollingHash_ = nextRollingHash;
    return WreckwaterReplayError::None;
}

WreckwaterReplayError WreckwaterReplayRecorder::appendMatchEvent(
    const MatchEvent& event) noexcept {
    if (!initialized_) return WreckwaterReplayError::NotInitialized;
    if (finalized_) return WreckwaterReplayError::AlreadyFinalized;
    if (!validEvent(event, config_)) {
        return WreckwaterReplayError::InvalidEvent;
    }
    if (nextMatchEventSequence_ == 0u
        || event.eventSequence != nextMatchEventSequence_) {
        return WreckwaterReplayError::OutOfOrder;
    }
    std::array<std::byte, kWreckwaterReplayMatchEventBytes> encoded{};
    if (!encodeEvent(event, encoded)) {
        return WreckwaterReplayError::InvalidEvent;
    }
    const WreckwaterReplayError result = appendRecord(
        WreckwaterReplayRecordType::MatchEvent,
        kWreckwaterMatchSchemaVersion,
        event.tick,
        event.sourcePhysicsTick,
        event.eventSequence,
        encoded);
    if (result == WreckwaterReplayError::None) {
        eventStreamRollingHash_ =
            wreckwaterAppendMatchEventStreamRollingHash(
                eventStreamRollingHash_, event);
        nextMatchEventSequence_ =
            event.eventSequence == std::numeric_limits<uint64_t>::max()
            ? 0u
            : event.eventSequence + 1u;
    }
    return result;
}

WreckwaterReplayError
WreckwaterReplayRecorder::appendCertifiedSnapshot(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    std::span<const std::byte> encodedPayload) noexcept {
    if (!initialized_) return WreckwaterReplayError::NotInitialized;
    if (finalized_) return WreckwaterReplayError::AlreadyFinalized;
    if (!network::isCanonicalWreckwaterSnapshot(snapshot)) {
        return WreckwaterReplayError::InvalidSnapshot;
    }
    SnapshotEnvelope envelope;
    if (!readSnapshotEnvelope(encodedPayload, envelope)
        || !network::wreckwaterSnapshotMatchesCanonicalBytes(
            snapshot, encodedPayload)) {
        return WreckwaterReplayError::InvalidSnapshot;
    }
    if (!envelopeMatchesConfig(envelope, config_)) {
        return WreckwaterReplayError::InvalidIdentity;
    }
    if (snapshot.eventStreamHash
        != expectedEventStreamHash(snapshot.matchStateHash)) {
        return WreckwaterReplayError::InvalidSnapshot;
    }
    if (nextSnapshotSequence_ == 0u
        || snapshot.snapshotSequence != nextSnapshotSequence_) {
        return WreckwaterReplayError::OutOfOrder;
    }
    if (seenSnapshot_
        && snapshot.physicsEvidenceTick
            <= lastSnapshotEvidenceTick_) {
        return WreckwaterReplayError::OutOfOrder;
    }
    const WreckwaterReplayError result = appendRecord(
        WreckwaterReplayRecordType::CertifiedSnapshot,
        network::kWreckwaterWireSchemaVersion,
        snapshot.applicationTick,
        snapshot.physicsEvidenceTick,
        snapshot.snapshotSequence,
        encodedPayload);
    if (result == WreckwaterReplayError::None) {
        nextSnapshotSequence_ =
            snapshot.snapshotSequence
                == std::numeric_limits<uint64_t>::max()
            ? 0u
            : snapshot.snapshotSequence + 1u;
        lastSnapshotMatchStateHash_ = snapshot.matchStateHash;
        lastSnapshotEventStreamHash_ = snapshot.eventStreamHash;
        lastSnapshotEvidenceTick_ = snapshot.physicsEvidenceTick;
        seenSnapshot_ = true;
    }
    return result;
}

WreckwaterReplayError WreckwaterReplayRecorder::finalize(
    uint32_t finalMatchStateHash,
    uint32_t finalEventStreamHash) noexcept {
    if (!initialized_) return WreckwaterReplayError::NotInitialized;
    if (finalized_) return WreckwaterReplayError::AlreadyFinalized;
    SnapshotEnvelope terminal;
    const bool terminalSnapshot =
        !records_.empty()
        && records_.back().type
            == WreckwaterReplayRecordType::CertifiedSnapshot
        && readSnapshotEnvelope(
            recordPayload(records_.size() - 1u), terminal)
        && terminal.phase
            == enumWord(network::WreckwaterPhase::Finished)
        && terminal.physicsEvidenceTick
            == terminal.applicationTick;
    if (!terminalSnapshot || !seenSnapshot_
        || nextMatchEventSequence_ == 1u
        || finalMatchStateHash != lastSnapshotMatchStateHash_
        || finalEventStreamHash != lastSnapshotEventStreamHash_) {
        return WreckwaterReplayError::InvalidRecord;
    }
    summary_.finalMatchStateHash = finalMatchStateHash;
    summary_.finalEventStreamHash = finalEventStreamHash;
    summary_.replayHash = finalReplayHash(rollingHash_, summary_);
    finalized_ = true;
    return WreckwaterReplayError::None;
}

std::span<const std::byte> WreckwaterReplayRecorder::recordPayload(
    size_t index) const noexcept {
    if (index >= records_.size()) return {};
    const WreckwaterReplayRecord& record = records_[index];
    if (record.payloadOffset > payload_.size()
        || record.payloadBytes
            > payload_.size() - record.payloadOffset) {
        return {};
    }
    return std::span<const std::byte>(
        payload_.data() + record.payloadOffset,
        record.payloadBytes);
}

WreckwaterReplayStorageState
WreckwaterReplayRecorder::storageState() const noexcept {
    return {
        .records = records_.data(),
        .payload = payload_.data(),
        .recordCapacity = records_.capacity(),
        .payloadCapacity = payload_.capacity(),
    };
}

uint32_t WreckwaterReplayRecorder::expectedEventStreamHash(
    uint32_t matchStateHash) const noexcept {
    if (!initialized_
        || nextMatchEventSequence_ == 0u
        || nextMatchEventSequence_
            > uint64_t{std::numeric_limits<uint32_t>::max()} + 1u) {
        return 0u;
    }
    const uint32_t eventCount = static_cast<uint32_t>(
        nextMatchEventSequence_ - 1u);
    return wreckwaterFinalizeMatchEventStreamHash(
        eventStreamRollingHash_, config_.matchConfigurationHash,
        matchStateHash, nextMatchEventSequence_, eventCount);
}

std::span<const std::byte> WreckwaterReplayArchive::recordPayload(
    size_t index) const noexcept {
    if (index >= records.size()) return {};
    const WreckwaterReplayRecord& record = records[index];
    if (record.payloadOffset > payload.size()
        || record.payloadBytes > payload.size() - record.payloadOffset) {
        return {};
    }
    return std::span<const std::byte>(
        payload.data() + record.payloadOffset, record.payloadBytes);
}

WreckwaterReplayWriteResult WreckwaterReplayCodec::encode(
    const WreckwaterReplayRecorder& recorder) {
    WreckwaterReplayWriteResult result;
    if (!recorder.initialized()) {
        result.error = WreckwaterReplayError::NotInitialized;
        return result;
    }
    if (!recorder.finalized()) {
        result.error = WreckwaterReplayError::NotFinalized;
        return result;
    }
    const auto records = recorder.records();
    const auto payload = recorder.payload();
    if (records.size() > std::numeric_limits<uint32_t>::max()) {
        result.error = WreckwaterReplayError::SizeOverflow;
        return result;
    }
    size_t recordHeadersBytes = 0u;
    size_t archiveBytes = 0u;
    if (!checkedMultiply(
            records.size(), kWreckwaterReplayRecordHeaderBytes,
            recordHeadersBytes)
        || !checkedAdd(
            kWreckwaterReplayArchiveHeaderBytes,
            recordHeadersBytes, archiveBytes)
        || !checkedAdd(archiveBytes, payload.size(), archiveBytes)) {
        result.error = WreckwaterReplayError::SizeOverflow;
        return result;
    }

    const WreckwaterReplayConfig& config = recorder.config();
    const WreckwaterReplaySummary& summary = recorder.summary();
    VectorWriter writer(archiveBytes);
    writer.raw(kReplayMagic);
    writer.u32(kWreckwaterReplaySchemaVersion);
    writer.u32(static_cast<uint32_t>(
        kWreckwaterReplayArchiveHeaderBytes));
    writer.u32(0u);
    writer.u32(static_cast<uint32_t>(
        kWreckwaterReplayRecordHeaderBytes));
    writer.u64(config.sessionId);
    writer.u64(config.matchId);
    writer.u64(config.worldId);
    writer.u32(config.worldEpoch);
    writer.u32(config.authorityEpoch);
    writer.u32(config.tickRateHz);
    writer.u32(static_cast<uint32_t>(records.size()));
    writer.u64(payload.size());
    writer.u64(summary.firstApplicationTick);
    writer.u64(summary.lastApplicationTick);
    writer.u64(config.contentHash);
    writer.u32(summary.finalMatchStateHash);
    writer.u32(summary.finalEventStreamHash);
    writer.u64(summary.replayHash);
    writer.u64(archiveBytes);
    writer.u64(0u);
    writer.u32(config.matchConfigurationHash);
    for (uint32_t index = 0u; index < 7u; ++index) writer.u32(0u);

    for (size_t index = 0u; index < records.size(); ++index) {
        const WreckwaterReplayRecord& record = records[index];
        const std::span<const std::byte> recordBytes =
            recorder.recordPayload(index);
        if (recordBytes.size() != record.payloadBytes) {
            result.error = WreckwaterReplayError::InvalidRecord;
            return result;
        }
        writeRecordHeader(writer, record);
        writer.raw(recordBytes);
    }

    if (writer.bytes().size() != archiveBytes) {
        result.error = WreckwaterReplayError::SizeOverflow;
        return result;
    }
    result.bytes = std::move(writer.bytes());
    result.archiveHash = archiveHash(result.bytes);
    patchU64(result.bytes, kArchiveHashOffset, result.archiveHash);
    return result;
}

WreckwaterReplayReadResult WreckwaterReplayCodec::decode(
    std::span<const std::byte> bytes,
    const WreckwaterReplayReadLimits& limits) {
    WreckwaterReplayReadResult result;
    if (limits.maximumRecords == 0u
        || limits.maximumRecords > kWreckwaterReplayMaximumRecords
        || limits.maximumPayloadBytes == 0u
        || limits.maximumPayloadBytes
            > kWreckwaterReplayMaximumPayloadBytes
        || limits.maximumArchiveBytes
            < kWreckwaterReplayArchiveHeaderBytes
        || limits.maximumArchiveBytes
            > kWreckwaterReplayMaximumArchiveBytes) {
        result.error = WreckwaterReplayError::InvalidConfiguration;
        return result;
    }
    if (bytes.size() < kReplayMagic.size()) {
        result.error = WreckwaterReplayError::Truncated;
        return result;
    }
    if (!magicMatches(bytes, kReplayMagic)) {
        result.error = WreckwaterReplayError::InvalidMagic;
        return result;
    }
    if (bytes.size() < kWreckwaterReplayArchiveHeaderBytes) {
        result.error = WreckwaterReplayError::Truncated;
        return result;
    }
    if (bytes.size() > limits.maximumArchiveBytes) {
        result.error = WreckwaterReplayError::PayloadCapacity;
        return result;
    }

    Reader reader(bytes);
    uint32_t schemaVersion = 0u;
    uint32_t headerBytes = 0u;
    uint32_t flags = 0u;
    uint32_t recordHeaderBytes = 0u;
    uint32_t recordCount = 0u;
    uint64_t payloadBytes = 0u;
    uint64_t archiveBytes = 0u;
    uint64_t storedArchiveHash = 0u;
    std::array<uint32_t, 7> reserved{};
    WreckwaterReplayArchive archive;
    if (!reader.skip(kReplayMagic.size())
        || !reader.u32(schemaVersion)
        || !reader.u32(headerBytes)
        || !reader.u32(flags)
        || !reader.u32(recordHeaderBytes)
        || !reader.u64(archive.config.sessionId)
        || !reader.u64(archive.config.matchId)
        || !reader.u64(archive.config.worldId)
        || !reader.u32(archive.config.worldEpoch)
        || !reader.u32(archive.config.authorityEpoch)
        || !reader.u32(archive.config.tickRateHz)
        || !reader.u32(recordCount)
        || !reader.u64(payloadBytes)
        || !reader.u64(archive.summary.firstApplicationTick)
        || !reader.u64(archive.summary.lastApplicationTick)
        || !reader.u64(archive.config.contentHash)
        || !reader.u32(archive.summary.finalMatchStateHash)
        || !reader.u32(archive.summary.finalEventStreamHash)
        || !reader.u64(archive.summary.replayHash)
        || !reader.u64(archiveBytes)
        || !reader.u64(storedArchiveHash)
        || !reader.u32(archive.config.matchConfigurationHash)) {
        result.error = WreckwaterReplayError::Truncated;
        return result;
    }
    for (uint32_t& word : reserved) {
        if (!reader.u32(word)) {
            result.error = WreckwaterReplayError::Truncated;
            return result;
        }
    }
    if (schemaVersion != kWreckwaterReplaySchemaVersion
        || headerBytes != kWreckwaterReplayArchiveHeaderBytes
        || recordHeaderBytes != kWreckwaterReplayRecordHeaderBytes) {
        result.error = WreckwaterReplayError::UnsupportedVersion;
        return result;
    }
    if (flags != 0u
        || std::any_of(
            reserved.begin(), reserved.end(),
            [](uint32_t value) { return value != 0u; })) {
        result.error = WreckwaterReplayError::ReservedNonZero;
        return result;
    }
    if (archiveBytes < bytes.size()) {
        result.error = WreckwaterReplayError::TrailingBytes;
        return result;
    }
    if (archiveBytes > bytes.size()) {
        result.error = WreckwaterReplayError::Truncated;
        return result;
    }
    if (recordCount == 0u || recordCount > limits.maximumRecords) {
        result.error = WreckwaterReplayError::RecordCapacity;
        return result;
    }
    if (payloadBytes == 0u
        || payloadBytes > limits.maximumPayloadBytes
        || payloadBytes > std::numeric_limits<size_t>::max()) {
        result.error = WreckwaterReplayError::PayloadCapacity;
        return result;
    }
    archive.config.maximumRecords = recordCount;
    archive.config.maximumPayloadBytes =
        static_cast<size_t>(payloadBytes);
    if (!validConfig(archive.config)) {
        result.error = WreckwaterReplayError::InvalidConfiguration;
        return result;
    }
    size_t recordHeadersTotal = 0u;
    size_t exactArchiveBytes = 0u;
    if (!checkedMultiply(
            recordCount, kWreckwaterReplayRecordHeaderBytes,
            recordHeadersTotal)
        || !checkedAdd(
            kWreckwaterReplayArchiveHeaderBytes,
            recordHeadersTotal, exactArchiveBytes)
        || !checkedAdd(
            exactArchiveBytes, static_cast<size_t>(payloadBytes),
            exactArchiveBytes)) {
        result.error = WreckwaterReplayError::SizeOverflow;
        return result;
    }
    if (exactArchiveBytes != bytes.size()) {
        result.error = exactArchiveBytes < bytes.size()
            ? WreckwaterReplayError::TrailingBytes
            : WreckwaterReplayError::Truncated;
        return result;
    }
    if (storedArchiveHash != archiveHash(bytes)) {
        result.error = WreckwaterReplayError::ArchiveHashMismatch;
        return result;
    }

    archive.summary.recordCount = recordCount;
    archive.summary.payloadBytes = payloadBytes;
    const ReplayPreflightResult preflight =
        preflightReplayRecords(
            bytes, recordCount, archive.config, archive.summary);
    if (preflight.error != WreckwaterReplayError::None) {
        result.error = preflight.error;
        result.failingRecord = preflight.failingRecord;
        return result;
    }

#if defined(__cpp_exceptions)
    try {
        archive.records.reserve(recordCount);
        archive.payload.reserve(static_cast<size_t>(payloadBytes));
    } catch (const std::bad_alloc&) {
        result.error = WreckwaterReplayError::AllocationFailed;
        return result;
    }
#else
    archive.records.reserve(recordCount);
    archive.payload.reserve(static_cast<size_t>(payloadBytes));
#endif
    DecodeOrdering order;
    uint64_t rollingHash = initialReplayHash(archive.config);
    uint32_t eventStreamRollingHash =
        wreckwaterInitialMatchEventStreamRollingHash(
            archive.config.matchConfigurationHash);
    for (uint32_t index = 0u; index < recordCount; ++index) {
        result.failingRecord = index;
        uint32_t type = 0u;
        WreckwaterReplayRecord record;
        if (!reader.u32(type)
            || !reader.u32(record.payloadSchemaVersion)
            || !reader.u64(record.applicationTick)
            || !reader.u64(record.physicsEvidenceTick)
            || !reader.u64(record.sequence)
            || !reader.u32(record.payloadBytes)
            || !reader.u32(record.flags)
            || !reader.u64(record.payloadHash)
            || !reader.u64(record.rollingHash)) {
            result.error = WreckwaterReplayError::Truncated;
            return result;
        }
        record.type = static_cast<WreckwaterReplayRecordType>(type);
        if (record.payloadSchemaVersion == 0u
            || record.payloadBytes == 0u
            || record.payloadBytes > reader.remaining()) {
            result.error = WreckwaterReplayError::InvalidRecord;
            return result;
        }
        std::span<const std::byte> recordPayload;
        if (!reader.bytes(record.payloadBytes, recordPayload)) {
            result.error = WreckwaterReplayError::Truncated;
            return result;
        }
        if (record.payloadHash != byteHash(recordPayload)) {
            result.error = WreckwaterReplayError::PayloadHashMismatch;
            return result;
        }
        const uint64_t expectedRollingHash = recordRollingHash(
            rollingHash, record.type, record.payloadSchemaVersion,
            record.applicationTick, record.physicsEvidenceTick,
            record.sequence, record.flags, record.payloadBytes,
            record.payloadHash, recordPayload);
        if (record.rollingHash != expectedRollingHash) {
            result.error = WreckwaterReplayError::ReplayHashMismatch;
            return result;
        }
        const WreckwaterReplayError orderError =
            validateRecordOrder(record, order);
        if (orderError != WreckwaterReplayError::None) {
            result.error = orderError;
            return result;
        }

        if (record.type == WreckwaterReplayRecordType::MatchEvent) {
            if (record.payloadSchemaVersion
                    != kWreckwaterMatchSchemaVersion
                || record.payloadBytes
                    != kWreckwaterReplayMatchEventBytes) {
                result.error = WreckwaterReplayError::InvalidEvent;
                return result;
            }
            MatchEvent event;
            if (!decodeEvent(recordPayload, event)
                || !validEvent(event, archive.config)
                || event.eventSequence != record.sequence
                || event.tick != record.applicationTick
                || event.sourcePhysicsTick
                    != record.physicsEvidenceTick) {
                result.error = WreckwaterReplayError::InvalidEvent;
                return result;
            }
            order.seenEvent = true;
            eventStreamRollingHash =
                wreckwaterAppendMatchEventStreamRollingHash(
                    eventStreamRollingHash, event);
        } else if (
            record.type
            == WreckwaterReplayRecordType::CertifiedSnapshot) {
            if (record.payloadSchemaVersion
                    != network::kWreckwaterWireSchemaVersion) {
                result.error = WreckwaterReplayError::InvalidSnapshot;
                return result;
            }
            SnapshotEnvelope envelope;
            if (!readSnapshotEnvelope(recordPayload, envelope)
                || !envelopeMatchesConfig(envelope, archive.config)
                || envelope.snapshotSequence != record.sequence
                || envelope.applicationTick != record.applicationTick
                || envelope.physicsEvidenceTick
                    != record.physicsEvidenceTick) {
                result.error = WreckwaterReplayError::InvalidSnapshot;
                return result;
            }
            if (network::validateWreckwaterSnapshotBytes(
                    recordPayload)
                != network::WreckwaterCodecError::None) {
                result.error =
                    WreckwaterReplayError::SnapshotCodecFailure;
                return result;
            }
            if (order.nextEventSequence == 0u
                || order.nextEventSequence - 1u
                    > std::numeric_limits<uint32_t>::max()
                || envelope.eventStreamHash
                    != wreckwaterFinalizeMatchEventStreamHash(
                        eventStreamRollingHash,
                        archive.config.matchConfigurationHash,
                        envelope.matchStateHash,
                        order.nextEventSequence,
                        static_cast<uint32_t>(
                            order.nextEventSequence - 1u))) {
                result.error = WreckwaterReplayError::InvalidSnapshot;
                return result;
            }
            if (order.seenSnapshot
                && envelope.physicsEvidenceTick
                    <= order.lastSnapshotEvidenceTick) {
                result.error = WreckwaterReplayError::OutOfOrder;
                return result;
            }
            order.lastSnapshotMatchStateHash =
                envelope.matchStateHash;
            order.lastSnapshotEventStreamHash =
                envelope.eventStreamHash;
            order.lastSnapshotApplicationTick =
                envelope.applicationTick;
            order.lastSnapshotEvidenceTick =
                envelope.physicsEvidenceTick;
            order.lastSnapshotPhase = envelope.phase;
            order.seenSnapshot = true;
        } else {
            result.error = WreckwaterReplayError::InvalidRecord;
            return result;
        }

        record.payloadOffset = archive.payload.size();
        archive.payload.insert(
            archive.payload.end(),
            recordPayload.begin(), recordPayload.end());
        archive.records.push_back(record);
        rollingHash = expectedRollingHash;
    }
    if (reader.remaining() != 0u) {
        result.error = WreckwaterReplayError::TrailingBytes;
        return result;
    }
    archive.summary.recordCount = archive.records.size();
    archive.summary.payloadBytes = archive.payload.size();
    if (!order.hasRecord
        || !order.seenEvent
        || !order.seenSnapshot
        || archive.records.back().type
            != WreckwaterReplayRecordType::CertifiedSnapshot
        || order.lastSnapshotPhase
            != enumWord(network::WreckwaterPhase::Finished)
        || order.lastSnapshotEvidenceTick
            != order.lastSnapshotApplicationTick
        || archive.summary.firstApplicationTick != order.firstTick
        || archive.summary.lastApplicationTick != order.lastTick
        || archive.summary.payloadBytes != payloadBytes
        || archive.summary.finalMatchStateHash
            != order.lastSnapshotMatchStateHash
        || archive.summary.finalEventStreamHash
            != order.lastSnapshotEventStreamHash
        || archive.summary.replayHash
            != finalReplayHash(rollingHash, archive.summary)) {
        result.error = WreckwaterReplayError::ReplayHashMismatch;
        return result;
    }

    result.failingRecord = archive.records.size();
    result.archive = std::move(archive);
    return result;
}

} // namespace voxy::game
