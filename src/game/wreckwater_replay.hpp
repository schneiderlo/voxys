#pragma once

#include "game/wreckwater_match.hpp"
#include "network/wreckwater_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::game {

// WRECKWATER uses certified state playback rather than claiming that the
// production float GPU simulation is cross-platform lockstep. Match events
// preserve causal/gameplay detail between the full certified state frames.
inline constexpr uint32_t kWreckwaterReplaySchemaVersion = 2u;
inline constexpr uint32_t kWreckwaterReplayTickRateHz = 60u;
inline constexpr size_t kWreckwaterReplayArchiveHeaderBytes = 160u;
inline constexpr size_t kWreckwaterReplayRecordHeaderBytes = 56u;
inline constexpr size_t kWreckwaterReplayMatchEventBytes = 216u;
inline constexpr size_t kWreckwaterReplayMaximumRecords = 1'048'576u;
inline constexpr size_t kWreckwaterReplayMaximumPayloadBytes =
    512u * 1024u * 1024u;
inline constexpr size_t kWreckwaterReplayMaximumArchiveBytes =
    kWreckwaterReplayMaximumPayloadBytes
    + kWreckwaterReplayMaximumRecords
        * kWreckwaterReplayRecordHeaderBytes
    + kWreckwaterReplayArchiveHeaderBytes;

enum class WreckwaterReplayRecordType : uint32_t {
    MatchEvent = 1u,
    CertifiedSnapshot = 2u,
};

enum class WreckwaterReplayError : uint32_t {
    None = 0u,
    InvalidConfiguration,
    NotInitialized,
    AlreadyInitialized,
    AlreadyFinalized,
    NotFinalized,
    InvalidIdentity,
    InvalidEvent,
    InvalidSnapshot,
    InvalidRecord,
    OutOfOrder,
    RecordCapacity,
    PayloadCapacity,
    SizeOverflow,
    AllocationFailed,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    TrailingBytes,
    ReservedNonZero,
    PayloadHashMismatch,
    ReplayHashMismatch,
    ArchiveHashMismatch,
    SnapshotCodecFailure,
};

[[nodiscard]] const char* wreckwaterReplayErrorName(
    WreckwaterReplayError error) noexcept;

struct WreckwaterReplayConfig {
    uint64_t sessionId = 1u;
    uint64_t matchId = 1u;
    uint64_t worldId = 1u;
    uint32_t worldEpoch = 1u;
    uint32_t authorityEpoch = 1u;
    uint32_t tickRateHz = kWreckwaterReplayTickRateHz;
    uint64_t contentHash = 0u;
    uint32_t matchConfigurationHash = 0u;
    size_t maximumRecords = 98'304u;
    size_t maximumPayloadBytes = 64u * 1024u * 1024u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayConfig&) const = default;
};

struct WreckwaterReplayRecord {
    WreckwaterReplayRecordType type =
        WreckwaterReplayRecordType::MatchEvent;
    uint32_t payloadSchemaVersion = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    uint64_t sequence = 0u;
    uint32_t flags = 0u;
    size_t payloadOffset = 0u;
    uint32_t payloadBytes = 0u;
    uint64_t payloadHash = 0u;
    uint64_t rollingHash = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayRecord&) const = default;
};

struct WreckwaterReplaySummary {
    uint64_t firstApplicationTick = 0u;
    uint64_t lastApplicationTick = 0u;
    uint64_t recordCount = 0u;
    uint64_t payloadBytes = 0u;
    uint32_t finalMatchStateHash = 0u;
    uint32_t finalEventStreamHash = 0u;
    uint64_t replayHash = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplaySummary&) const = default;
};

struct WreckwaterReplayStorageState {
    const void* records = nullptr;
    const void* payload = nullptr;
    size_t recordCapacity = 0u;
    size_t payloadCapacity = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayStorageState&) const = default;
};

class WreckwaterReplayRecorder {
public:
    WreckwaterReplayRecorder() = default;

    WreckwaterReplayRecorder(const WreckwaterReplayRecorder&) = delete;
    WreckwaterReplayRecorder& operator=(
        const WreckwaterReplayRecorder&) = delete;
    WreckwaterReplayRecorder(WreckwaterReplayRecorder&&) = delete;
    WreckwaterReplayRecorder& operator=(
        WreckwaterReplayRecorder&&) = delete;

    [[nodiscard]] WreckwaterReplayError initialize(
        const WreckwaterReplayConfig& config);

    // Both append operations are allocation-free after initialize(). The
    // snapshot bytes must be the exact canonical bytes already produced for
    // transport, avoiding a second encode/allocation in the authority tick.
    [[nodiscard]] WreckwaterReplayError appendMatchEvent(
        const MatchEvent& event) noexcept;
    [[nodiscard]] WreckwaterReplayError appendCertifiedSnapshot(
        const network::WreckwaterCertifiedSnapshot& snapshot,
        std::span<const std::byte> encodedPayload) noexcept;
    [[nodiscard]] WreckwaterReplayError finalize(
        uint32_t finalMatchStateHash,
        uint32_t finalEventStreamHash) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool finalized() const noexcept {
        return finalized_;
    }
    [[nodiscard]] const WreckwaterReplayConfig& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const WreckwaterReplaySummary& summary() const noexcept {
        return summary_;
    }
    [[nodiscard]] uint32_t expectedEventStreamHash(
        uint32_t matchStateHash) const noexcept;
    [[nodiscard]] std::span<const WreckwaterReplayRecord> records()
        const noexcept {
        return records_;
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept {
        return payload_;
    }
    [[nodiscard]] std::span<const std::byte> recordPayload(
        size_t index) const noexcept;
    [[nodiscard]] WreckwaterReplayStorageState storageState()
        const noexcept;

private:
    [[nodiscard]] WreckwaterReplayError appendRecord(
        WreckwaterReplayRecordType type,
        uint32_t payloadSchemaVersion,
        uint64_t applicationTick,
        uint64_t physicsEvidenceTick,
        uint64_t sequence,
        std::span<const std::byte> payload) noexcept;

    WreckwaterReplayConfig config_{};
    std::vector<WreckwaterReplayRecord> records_;
    std::vector<std::byte> payload_;
    WreckwaterReplaySummary summary_{};
    uint64_t rollingHash_ = 0u;
    uint32_t eventStreamRollingHash_ = 0u;
    uint64_t nextMatchEventSequence_ = 1u;
    uint64_t nextSnapshotSequence_ = 1u;
    uint32_t lastSnapshotMatchStateHash_ = 0u;
    uint32_t lastSnapshotEventStreamHash_ = 0u;
    uint64_t lastSnapshotEvidenceTick_ = 0u;
    bool seenSnapshot_ = false;
    bool initialized_ = false;
    bool finalized_ = false;
};

struct WreckwaterReplayWriteResult {
    std::vector<std::byte> bytes;
    WreckwaterReplayError error = WreckwaterReplayError::None;
    uint64_t archiveHash = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterReplayError::None && !bytes.empty();
    }
};

struct WreckwaterReplayArchive {
    WreckwaterReplayConfig config{};
    WreckwaterReplaySummary summary{};
    std::vector<WreckwaterReplayRecord> records;
    std::vector<std::byte> payload;

    [[nodiscard]] std::span<const std::byte> recordPayload(
        size_t index) const noexcept;
};

struct WreckwaterReplayReadLimits {
    size_t maximumRecords = 262'144u;
    size_t maximumPayloadBytes = 64u * 1024u * 1024u;
    size_t maximumArchiveBytes =
        maximumPayloadBytes
        + maximumRecords
            * kWreckwaterReplayRecordHeaderBytes
        + kWreckwaterReplayArchiveHeaderBytes;
};

struct WreckwaterReplayReadResult {
    std::optional<WreckwaterReplayArchive> archive;
    WreckwaterReplayError error = WreckwaterReplayError::None;
    size_t failingRecord = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return archive.has_value();
    }
};

class WreckwaterReplayCodec {
public:
    // Export is expected off the fixed-tick path and may allocate.
    [[nodiscard]] static WreckwaterReplayWriteResult encode(
        const WreckwaterReplayRecorder& recorder);
    [[nodiscard]] static WreckwaterReplayReadResult decode(
        std::span<const std::byte> bytes,
        const WreckwaterReplayReadLimits& limits = {});
};

} // namespace voxy::game
