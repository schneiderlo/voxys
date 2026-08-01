#include "game/wreckwater_replay.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace voxy::game {
namespace {

constexpr uint64_t kFnv64Offset = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnv64Prime = 1'099'511'628'211ull;
constexpr size_t kArchiveHashOffset = 120u;

void writeU32(
    std::vector<std::byte>& bytes, size_t offset,
    uint32_t value) {
    ASSERT_LE(offset + sizeof(uint32_t), bytes.size());
    for (uint32_t byte = 0u; byte < 4u; ++byte) {
        bytes[offset + byte] =
            std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
    }
}

void writeU64(
    std::vector<std::byte>& bytes, size_t offset,
    uint64_t value) {
    ASSERT_LE(offset + sizeof(uint64_t), bytes.size());
    for (uint32_t byte = 0u; byte < 8u; ++byte) {
        bytes[offset + byte] =
            std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
    }
}

uint32_t readU32(
    std::span<const std::byte> bytes, size_t offset) {
    EXPECT_LE(offset + sizeof(uint32_t), bytes.size());
    uint32_t value = 0u;
    for (uint32_t byte = 0u; byte < 4u; ++byte) {
        value |= std::to_integer<uint32_t>(bytes[offset + byte])
            << (byte * 8u);
    }
    return value;
}

uint64_t readU64(
    std::span<const std::byte> bytes, size_t offset) {
    EXPECT_LE(offset + sizeof(uint64_t), bytes.size());
    uint64_t value = 0u;
    for (uint32_t byte = 0u; byte < 8u; ++byte) {
        value |= std::to_integer<uint64_t>(bytes[offset + byte])
            << (byte * 8u);
    }
    return value;
}

uint64_t testArchiveHash(std::span<const std::byte> bytes) {
    uint64_t hash = kFnv64Offset;
    for (size_t index = 0u; index < bytes.size(); ++index) {
        const std::byte value =
            index >= kArchiveHashOffset
                    && index < kArchiveHashOffset + sizeof(uint64_t)
                ? std::byte{0u}
                : bytes[index];
        hash =
            (hash ^ std::to_integer<uint64_t>(value)) * kFnv64Prime;
    }
    return hash;
}

void refreshArchiveHash(std::vector<std::byte>& bytes) {
    writeU64(bytes, kArchiveHashOffset, 0u);
    writeU64(bytes, kArchiveHashOffset, testArchiveHash(bytes));
}

WreckwaterReplayConfig makeConfig(
    size_t maximumRecords = 8u,
    size_t maximumPayloadBytes = 16u * 1024u) {
    return {
        .sessionId = 0x0102'0304'0506'0708ull,
        .matchId = 0x1112'1314'1516'1718ull,
        .worldId = 0x2122'2324'2526'2728ull,
        .worldEpoch = 31u,
        .authorityEpoch = 41u,
        .tickRateHz = 60u,
        .contentHash = 0xa1b2'c3d4'e5f6'0718ull,
        .matchConfigurationHash = 0x1357'2468u,
        .maximumRecords = maximumRecords,
        .maximumPayloadBytes = maximumPayloadBytes,
    };
}

MatchEvent makeEvent(
    uint64_t sequence, uint64_t tick,
    MatchEventType type = MatchEventType::PlayerRegistered) {
    MatchEvent event;
    event.eventSequence = sequence;
    event.matchId = makeConfig().matchId;
    event.worldId = makeConfig().worldId;
    event.worldEpoch = makeConfig().worldEpoch;
    event.authorityEpoch = makeConfig().authorityEpoch;
    event.tick = tick;
    event.type = type;
    event.phase = MatchPhase::Warmup;
    event.playerId = 101u;
    event.seat = 0u;
    event.crew = CrewId::CrewOne;
    event.crewOneScore = 0u;
    event.crewTwoScore = 0u;
    event.stateHash = static_cast<uint32_t>(0x1000u + sequence);
    return event;
}

network::WreckwaterCertifiedSnapshot makeSnapshot(
    uint64_t sequence = 1u,
    uint64_t applicationTick = 1u,
    uint32_t stateHash = 0x2222'3333u,
    uint32_t eventHash = 0x4444'5555u) {
    network::WreckwaterCertifiedSnapshot snapshot;
    snapshot.sessionId = makeConfig().sessionId;
    snapshot.matchId = makeConfig().matchId;
    snapshot.worldId = makeConfig().worldId;
    snapshot.worldEpoch = makeConfig().worldEpoch;
    snapshot.authorityEpoch = makeConfig().authorityEpoch;
    snapshot.snapshotSequence = sequence;
    snapshot.applicationTick = applicationTick;
    snapshot.physicsEvidenceTick = applicationTick;
    snapshot.phase = network::WreckwaterPhase::Finished;
    snapshot.outcome = network::WreckwaterOutcomeType::Tie;
    snapshot.matchStateHash = stateHash;
    snapshot.eventStreamHash = eventHash;

    network::WreckwaterEntityState skiff;
    skiff.netEntityId = 1u;
    skiff.netGeneration = 1u;
    skiff.kind = network::WreckwaterEntityKind::Skiff;
    skiff.crew = network::WreckwaterCrew::CrewOne;
    skiff.localPosition = {-12.0f, 1.5f, 3.0f};
    skiff.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    skiff.linearVelocity = {2.0f, 0.0f, -1.0f};
    skiff.angularVelocity = {0.0f, 0.2f, 0.0f};
    skiff.shape = network::WreckwaterShape::Compound;
    skiff.dimensions = {3.5f, 1.25f, 6.0f};
    skiff.packedMaterialFlags = 0xa123'4567u;
    skiff.skiff = {
        .skiffId = 1u,
        .generation = 1u,
        .disposition =
            network::WreckwaterSkiffDisposition::Active,
    };
    snapshot.entities.push_back(skiff);
    EXPECT_TRUE(network::canonicalizeWreckwaterSnapshot(snapshot));
    return snapshot;
}

network::WreckwaterWriteResult encodeSnapshot(
    network::WreckwaterCertifiedSnapshot& snapshot) {
    network::WreckwaterWriteResult encoded =
        network::WreckwaterSnapshotCodec::encode(snapshot);
    EXPECT_TRUE(encoded)
        << network::wreckwaterCodecErrorName(encoded.error);
    snapshot.serializedByteHash = encoded.serializedByteHash;
    return encoded;
}

network::WreckwaterWriteResult encodeSnapshot(
    const WreckwaterReplayRecorder& recorder,
    network::WreckwaterCertifiedSnapshot& snapshot) {
    snapshot.eventStreamHash =
        recorder.expectedEventStreamHash(snapshot.matchStateHash);
    return encodeSnapshot(snapshot);
}

void appendCompleteRecording(
    WreckwaterReplayRecorder& recorder,
    MatchEvent first = makeEvent(1u, 0u)) {
    ASSERT_EQ(
        recorder.appendMatchEvent(first),
        WreckwaterReplayError::None);
    const MatchEvent second =
        makeEvent(2u, 1u, MatchEventType::MatchStarted);
    ASSERT_EQ(
        recorder.appendMatchEvent(second),
        WreckwaterReplayError::None);
    network::WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
    const network::WreckwaterWriteResult encoded =
        encodeSnapshot(recorder, snapshot);
    ASSERT_EQ(
        recorder.appendCertifiedSnapshot(snapshot, encoded.bytes),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        recorder.finalize(
            snapshot.matchStateHash, snapshot.eventStreamHash),
        WreckwaterReplayError::None);
}

TEST(WreckwaterReplayRecorderTest,
     RecordsWithoutPostInitializeStorageMovementAndRoundTrips) {
    WreckwaterReplayRecorder recorder;
    ASSERT_EQ(
        recorder.initialize(makeConfig()),
        WreckwaterReplayError::None);
    const WreckwaterReplayStorageState initial =
        recorder.storageState();
    ASSERT_NE(initial.records, nullptr);
    ASSERT_NE(initial.payload, nullptr);

    appendCompleteRecording(recorder);

    EXPECT_EQ(recorder.storageState(), initial);
    EXPECT_TRUE(recorder.finalized());
    ASSERT_EQ(recorder.records().size(), 3u);
    EXPECT_EQ(recorder.records()[0].payloadBytes,
              kWreckwaterReplayMatchEventBytes);
    EXPECT_EQ(recorder.records()[1].payloadBytes,
              kWreckwaterReplayMatchEventBytes);
    EXPECT_EQ(recorder.records()[2].payloadBytes,
              network::kWreckwaterSnapshotHeaderBytes
                  + network::kWreckwaterEntityStateBytes);
    EXPECT_NE(recorder.summary().replayHash, 0u);

    const WreckwaterReplayWriteResult encoded =
        WreckwaterReplayCodec::encode(recorder);
    ASSERT_TRUE(encoded)
        << wreckwaterReplayErrorName(encoded.error);
    EXPECT_EQ(encoded.archiveHash,
              readU64(encoded.bytes, kArchiveHashOffset));
    EXPECT_EQ(encoded.bytes.size(),
              kWreckwaterReplayArchiveHeaderBytes
                  + 3u * kWreckwaterReplayRecordHeaderBytes
                  + recorder.payload().size());

    const WreckwaterReplayReadResult decoded =
        WreckwaterReplayCodec::decode(encoded.bytes);
    ASSERT_TRUE(decoded)
        << wreckwaterReplayErrorName(decoded.error);
    ASSERT_TRUE(decoded.archive.has_value());
    EXPECT_EQ(decoded.archive->summary, recorder.summary());
    ASSERT_EQ(
        decoded.archive->records.size(),
        recorder.records().size());
    EXPECT_TRUE(std::equal(
        decoded.archive->records.begin(),
        decoded.archive->records.end(),
        recorder.records().begin(),
        recorder.records().end()));
    EXPECT_TRUE(std::equal(
        decoded.archive->payload.begin(),
        decoded.archive->payload.end(),
        recorder.payload().begin(),
        recorder.payload().end()));

    const auto snapshotRecord =
        decoded.archive->recordPayload(2u);
    const network::WreckwaterSnapshotReadResult snapshot =
        network::WreckwaterSnapshotCodec::decode(snapshotRecord);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot.snapshot->matchStateHash,
              decoded.archive->summary.finalMatchStateHash);
    EXPECT_EQ(snapshot.snapshot->eventStreamHash,
              decoded.archive->summary.finalEventStreamHash);
}

TEST(WreckwaterReplayRecorderTest,
     CapacityAndOrderingFailuresAreAtomic) {
    WreckwaterReplayRecorder sequenceRecorder;
    ASSERT_EQ(
        sequenceRecorder.initialize(makeConfig()),
        WreckwaterReplayError::None);
    const WreckwaterReplayStorageState initial =
        sequenceRecorder.storageState();
    EXPECT_EQ(
        sequenceRecorder.appendMatchEvent(makeEvent(2u, 0u)),
        WreckwaterReplayError::OutOfOrder);
    MatchEvent foreign = makeEvent(1u, 0u);
    foreign.matchId += 1u;
    EXPECT_EQ(
        sequenceRecorder.appendMatchEvent(foreign),
        WreckwaterReplayError::InvalidEvent);
    EXPECT_TRUE(sequenceRecorder.records().empty());
    EXPECT_TRUE(sequenceRecorder.payload().empty());
    EXPECT_EQ(sequenceRecorder.storageState(), initial);

    WreckwaterReplayRecorder recordLimited;
    ASSERT_EQ(
        recordLimited.initialize(makeConfig(
            1u, kWreckwaterReplayMatchEventBytes * 2u)),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        recordLimited.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);
    const auto beforeRecordFailure = recordLimited.summary();
    EXPECT_EQ(
        recordLimited.appendMatchEvent(makeEvent(2u, 1u)),
        WreckwaterReplayError::RecordCapacity);
    EXPECT_EQ(recordLimited.summary(), beforeRecordFailure);
    EXPECT_EQ(recordLimited.records().size(), 1u);

    WreckwaterReplayRecorder payloadLimited;
    ASSERT_EQ(
        payloadLimited.initialize(makeConfig(
            2u, kWreckwaterReplayMatchEventBytes)),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        payloadLimited.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);
    const auto beforePayloadFailure = payloadLimited.summary();
    EXPECT_EQ(
        payloadLimited.appendMatchEvent(makeEvent(2u, 1u)),
        WreckwaterReplayError::PayloadCapacity);
    EXPECT_EQ(payloadLimited.summary(), beforePayloadFailure);
    EXPECT_EQ(payloadLimited.payload().size(),
              kWreckwaterReplayMatchEventBytes);
}

TEST(WreckwaterReplayRecorderTest,
     FinalStateMustBeBackedByLastCertifiedSnapshot) {
    WreckwaterReplayRecorder recorder;
    ASSERT_EQ(
        recorder.initialize(makeConfig()),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        recorder.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        recorder.finalize(1u, 2u),
        WreckwaterReplayError::InvalidRecord);

    network::WreckwaterCertifiedSnapshot forgedHash = makeSnapshot();
    const auto forgedHashBytes = encodeSnapshot(forgedHash);
    ASSERT_NE(
        forgedHash.eventStreamHash,
        recorder.expectedEventStreamHash(
            forgedHash.matchStateHash));
    EXPECT_EQ(
        recorder.appendCertifiedSnapshot(
            forgedHash, forgedHashBytes.bytes),
        WreckwaterReplayError::InvalidSnapshot);
    EXPECT_EQ(recorder.records().size(), 1u);

    network::WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
    const auto encoded = encodeSnapshot(recorder, snapshot);
    ASSERT_EQ(
        recorder.appendCertifiedSnapshot(snapshot, encoded.bytes),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        recorder.finalize(
            snapshot.matchStateHash + 1u,
            snapshot.eventStreamHash),
        WreckwaterReplayError::InvalidRecord);
    EXPECT_EQ(
        recorder.finalize(
            snapshot.matchStateHash,
            snapshot.eventStreamHash),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        recorder.appendMatchEvent(makeEvent(2u, 2u)),
        WreckwaterReplayError::AlreadyFinalized);
    EXPECT_EQ(
        recorder.finalize(
            snapshot.matchStateHash,
            snapshot.eventStreamHash),
        WreckwaterReplayError::AlreadyFinalized);
}

TEST(
    WreckwaterReplayRecorderTest,
    FinalizeRequiresTerminalSnapshotAsLastRecordAndAtLeastOneEvent) {
    WreckwaterReplayRecorder snapshotOnly;
    ASSERT_EQ(
        snapshotOnly.initialize(makeConfig()),
        WreckwaterReplayError::None);
    network::WreckwaterCertifiedSnapshot terminal = makeSnapshot();
    const auto terminalBytes =
        encodeSnapshot(snapshotOnly, terminal);
    ASSERT_EQ(
        snapshotOnly.appendCertifiedSnapshot(
            terminal, terminalBytes.bytes),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        snapshotOnly.finalize(
            terminal.matchStateHash,
            terminal.eventStreamHash),
        WreckwaterReplayError::InvalidRecord);

    WreckwaterReplayRecorder trailingEvent;
    ASSERT_EQ(
        trailingEvent.initialize(makeConfig()),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        trailingEvent.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);
    network::WreckwaterCertifiedSnapshot trailingTerminal =
        makeSnapshot();
    const auto trailingTerminalBytes =
        encodeSnapshot(trailingEvent, trailingTerminal);
    ASSERT_EQ(
        trailingEvent.appendCertifiedSnapshot(
            trailingTerminal, trailingTerminalBytes.bytes),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        trailingEvent.appendMatchEvent(makeEvent(2u, 2u)),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        trailingEvent.finalize(
            terminal.matchStateHash,
            trailingTerminal.eventStreamHash),
        WreckwaterReplayError::InvalidRecord);

    WreckwaterReplayRecorder warmup;
    ASSERT_EQ(
        warmup.initialize(makeConfig()),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        warmup.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);
    network::WreckwaterCertifiedSnapshot warmupSnapshot =
        makeSnapshot();
    warmupSnapshot.phase = network::WreckwaterPhase::Warmup;
    warmupSnapshot.outcome =
        network::WreckwaterOutcomeType::Undecided;
    const auto warmupBytes =
        encodeSnapshot(warmup, warmupSnapshot);
    ASSERT_EQ(
        warmup.appendCertifiedSnapshot(
            warmupSnapshot, warmupBytes.bytes),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        warmup.finalize(
            warmupSnapshot.matchStateHash,
            warmupSnapshot.eventStreamHash),
        WreckwaterReplayError::InvalidRecord);

    WreckwaterReplayRecorder staleEvidence;
    ASSERT_EQ(
        staleEvidence.initialize(makeConfig()),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        staleEvidence.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);
    network::WreckwaterCertifiedSnapshot stale = makeSnapshot(
        1u, 2u);
    stale.physicsEvidenceTick = 1u;
    const auto staleBytes =
        encodeSnapshot(staleEvidence, stale);
    ASSERT_EQ(
        staleEvidence.appendCertifiedSnapshot(
            stale, staleBytes.bytes),
        WreckwaterReplayError::None);
    EXPECT_EQ(
        staleEvidence.finalize(
            stale.matchStateHash, stale.eventStreamHash),
        WreckwaterReplayError::InvalidRecord);
}

TEST(
    WreckwaterReplayRecorderTest,
    RejectsSnapshotObjectWhoseEntityBytesComeFromAnotherSnapshot) {
    WreckwaterReplayRecorder recorder;
    ASSERT_EQ(
        recorder.initialize(makeConfig()),
        WreckwaterReplayError::None);
    ASSERT_EQ(
        recorder.appendMatchEvent(makeEvent(1u, 0u)),
        WreckwaterReplayError::None);

    network::WreckwaterCertifiedSnapshot object = makeSnapshot();
    network::WreckwaterCertifiedSnapshot encodedSource = object;
    object.eventStreamHash =
        recorder.expectedEventStreamHash(object.matchStateHash);
    encodedSource.eventStreamHash = object.eventStreamHash;
    encodedSource.entities[0].localPosition.x = -11.0f;
    ASSERT_TRUE(
        network::canonicalizeWreckwaterSnapshot(encodedSource));
    const auto encoded = encodeSnapshot(encodedSource);
    object.serializedByteHash = encoded.serializedByteHash;

    EXPECT_EQ(
        recorder.appendCertifiedSnapshot(object, encoded.bytes),
        WreckwaterReplayError::InvalidSnapshot);
    EXPECT_EQ(recorder.records().size(), 1u);
}

TEST(WreckwaterReplayCodecTest,
     RejectsCorruptionTruncationTrailingBytesAndReadLimitOverflow) {
    WreckwaterReplayRecorder recorder;
    ASSERT_EQ(
        recorder.initialize(makeConfig()),
        WreckwaterReplayError::None);
    appendCompleteRecording(recorder);
    const WreckwaterReplayWriteResult encoded =
        WreckwaterReplayCodec::encode(recorder);
    ASSERT_TRUE(encoded);

    std::vector<std::byte> corrupt = encoded.bytes;
    corrupt.back() ^= std::byte{1u};
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(corrupt).error,
        WreckwaterReplayError::ArchiveHashMismatch);

    refreshArchiveHash(corrupt);
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(corrupt).error,
        WreckwaterReplayError::PayloadHashMismatch);

    std::vector<std::byte> rolling = encoded.bytes;
    constexpr size_t kFirstRecordRollingHashOffset =
        kWreckwaterReplayArchiveHeaderBytes + 48u;
    rolling[kFirstRecordRollingHashOffset] ^= std::byte{1u};
    refreshArchiveHash(rolling);
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(rolling).error,
        WreckwaterReplayError::ReplayHashMismatch);

    std::vector<std::byte> reserved = encoded.bytes;
    reserved[132u] = std::byte{1u};
    refreshArchiveHash(reserved);
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(reserved).error,
        WreckwaterReplayError::ReservedNonZero);

    std::vector<std::byte> invalidSchema = encoded.bytes;
    constexpr size_t kFirstRecordPayloadSchemaOffset =
        kWreckwaterReplayArchiveHeaderBytes + 4u;
    writeU32(
        invalidSchema, kFirstRecordPayloadSchemaOffset, 0u);
    refreshArchiveHash(invalidSchema);
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(invalidSchema).error,
        WreckwaterReplayError::InvalidRecord);

    EXPECT_EQ(
        WreckwaterReplayCodec::decode(
            std::span<const std::byte>(encoded.bytes)
                .first(encoded.bytes.size() - 1u)).error,
        WreckwaterReplayError::Truncated);

    std::vector<std::byte> trailing = encoded.bytes;
    trailing.push_back(std::byte{0u});
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(trailing).error,
        WreckwaterReplayError::TrailingBytes);

    WreckwaterReplayReadLimits recordLimit;
    recordLimit.maximumRecords = 2u;
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(
            encoded.bytes, recordLimit).error,
        WreckwaterReplayError::RecordCapacity);

    WreckwaterReplayReadLimits payloadLimit;
    payloadLimit.maximumPayloadBytes =
        recorder.payload().size() - 1u;
    EXPECT_EQ(
        WreckwaterReplayCodec::decode(
            encoded.bytes, payloadLimit).error,
        WreckwaterReplayError::PayloadCapacity);
}

TEST(WreckwaterReplayCodecTest,
     HashConvergesForExactRecordingAndDivergesForOneEventBit) {
    WreckwaterReplayRecorder first;
    WreckwaterReplayRecorder second;
    WreckwaterReplayRecorder divergent;
    ASSERT_EQ(first.initialize(makeConfig()),
              WreckwaterReplayError::None);
    ASSERT_EQ(second.initialize(makeConfig()),
              WreckwaterReplayError::None);
    ASSERT_EQ(divergent.initialize(makeConfig()),
              WreckwaterReplayError::None);

    appendCompleteRecording(first);
    appendCompleteRecording(second);
    MatchEvent changed = makeEvent(1u, 0u);
    changed.stateHash ^= 1u;
    appendCompleteRecording(divergent, changed);

    EXPECT_EQ(first.summary().replayHash,
              second.summary().replayHash);
    EXPECT_NE(first.summary().replayHash,
              divergent.summary().replayHash);
    const auto firstBytes = WreckwaterReplayCodec::encode(first);
    const auto secondBytes = WreckwaterReplayCodec::encode(second);
    const auto divergentBytes =
        WreckwaterReplayCodec::encode(divergent);
    ASSERT_TRUE(firstBytes);
    ASSERT_TRUE(secondBytes);
    ASSERT_TRUE(divergentBytes);
    EXPECT_EQ(firstBytes.bytes, secondBytes.bytes);
    EXPECT_NE(firstBytes.bytes, divergentBytes.bytes);
}

TEST(WreckwaterReplayCodecTest,
     ArchiveHeaderIsExplicitLittleEndianAndSelfDescribing) {
    WreckwaterReplayRecorder recorder;
    ASSERT_EQ(
        recorder.initialize(makeConfig()),
        WreckwaterReplayError::None);
    appendCompleteRecording(recorder);
    const auto encoded = WreckwaterReplayCodec::encode(recorder);
    ASSERT_TRUE(encoded);

    constexpr std::array<uint8_t, 8> magic{
        'V', 'O', 'X', 'Y', 'W', 'R', 'P', 'L'};
    for (size_t index = 0u; index < magic.size(); ++index) {
        EXPECT_EQ(
            std::to_integer<uint8_t>(encoded.bytes[index]),
            magic[index]);
    }
    EXPECT_EQ(readU32(encoded.bytes, 8u),
              kWreckwaterReplaySchemaVersion);
    EXPECT_EQ(readU32(encoded.bytes, 12u),
              kWreckwaterReplayArchiveHeaderBytes);
    EXPECT_EQ(readU32(encoded.bytes, 20u),
              kWreckwaterReplayRecordHeaderBytes);
    EXPECT_EQ(readU64(encoded.bytes, 24u),
              makeConfig().sessionId);
    EXPECT_EQ(readU64(encoded.bytes, 32u),
              makeConfig().matchId);
    EXPECT_EQ(readU64(encoded.bytes, 40u),
              makeConfig().worldId);
    EXPECT_EQ(readU32(encoded.bytes, 60u), 3u);
    EXPECT_EQ(readU64(encoded.bytes, 112u),
              encoded.bytes.size());
    EXPECT_EQ(readU32(encoded.bytes, 128u),
              makeConfig().matchConfigurationHash);
    EXPECT_EQ(testArchiveHash(encoded.bytes),
              encoded.archiveHash);
    EXPECT_EQ(recorder.records()[0].payloadHash, 12189712288282740756ull);
    EXPECT_EQ(recorder.records()[0].rollingHash, 8562278780200096133ull);
    EXPECT_EQ(
        recorder.summary().replayHash,
        10'667'219'235'186'252'482ull);
    EXPECT_EQ(
        encoded.archiveHash,
        11'330'521'706'123'726'950ull);
}

TEST(WreckwaterReplayRecorderTest, RejectsInvalidConfiguration) {
    WreckwaterReplayRecorder recorder;
    WreckwaterReplayConfig config = makeConfig();
    config.contentHash = 0u;
    EXPECT_EQ(
        recorder.initialize(config),
        WreckwaterReplayError::InvalidConfiguration);
    config = makeConfig();
    config.matchConfigurationHash = 0u;
    EXPECT_EQ(
        recorder.initialize(config),
        WreckwaterReplayError::InvalidConfiguration);
    config = makeConfig();
    config.maximumRecords =
        kWreckwaterReplayMaximumRecords + 1u;
    EXPECT_EQ(
        recorder.initialize(config),
        WreckwaterReplayError::InvalidConfiguration);
    EXPECT_FALSE(recorder.initialized());
}

} // namespace
} // namespace voxy::game
