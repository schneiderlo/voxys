#pragma once

#include "game/wreckwater_replay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace voxy::game {

inline constexpr uint32_t kWreckwaterReplaySubticksPerTick = 65'536u;
inline constexpr size_t kWreckwaterReplayNoSnapshot =
    static_cast<size_t>(-1);
inline constexpr uint32_t kWreckwaterWreckCamDefaultInterestTicks =
    5u * kWreckwaterReplayTickRateHz;

enum class WreckwaterReplayPlaybackError : uint32_t {
    None = 0u,
    InvalidConfiguration,
    AlreadyInitialized,
    NotInitialized,
    NotFinalized,
    ArchiveDecodeFailure,
    AllocationFailed,
    RecordCapacity,
    PayloadCapacity,
    SnapshotCapacity,
    EventCapacity,
    EntityCapacity,
    LifetimeCapacity,
    InvalidRecord,
    InvalidEvent,
    InvalidSnapshot,
    InvalidIdentity,
    ContentMismatch,
    NonMonotonicRecord,
    NonMonotonicSnapshot,
    GenerationAlias,
    PayloadHashMismatch,
    ReplayHashMismatch,
    InvalidTime,
    PositionOverflow,
};

[[nodiscard]] const char* wreckwaterReplayPlaybackErrorName(
    WreckwaterReplayPlaybackError error) noexcept;

struct WreckwaterReplayTime {
    uint64_t wholeTick = 0u;
    uint16_t subTick = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayTime&) const = default;
};

struct WreckwaterReplayPlaybackConfig {
    WreckwaterReplayReadLimits readLimits{};
    // Zero accepts any internally valid archive and treats contentHash as
    // provenance. A nonzero value is an exact local-compatibility gate checked
    // before record or payload allocation.
    uint64_t expectedContentHash = 0u;
    size_t maximumSnapshots = 262'144u;
    size_t maximumEvents = 262'144u;
    uint32_t maximumEntities =
        network::kWreckwaterMaximumSnapshotEntities;
    // One aggregate cap across roster, connection, character, source,
    // network, logical-entity, and attachment lifetime registries.
    size_t maximumTrackedLifetimes = 262'144u;
    uint32_t cameraInterestTicks =
        kWreckwaterWreckCamDefaultInterestTicks;
    uint32_t maximumEventsPerTick = 64u;
    uint32_t maximumCameraEvents = 4'096u;
};

struct WreckwaterReplayPlaybackLoadResult {
    WreckwaterReplayPlaybackError error =
        WreckwaterReplayPlaybackError::None;
    WreckwaterReplayError archiveError = WreckwaterReplayError::None;
    network::WreckwaterCodecError snapshotError =
        network::WreckwaterCodecError::None;
    size_t failingRecord = 0u;
    size_t failingSnapshot = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterReplayPlaybackError::None;
    }
};

enum class WreckwaterReplayVisualMotion : uint32_t {
    Exact = 0u,
    Interpolated = 1u,
    HeldTopology = 2u,
};

struct WreckwaterReplayVisualPose {
    std::array<int32_t, 3> sector{};
    network::WreckwaterVec3 localPosition{};
    network::WreckwaterQuaternion orientation{};
    network::WreckwaterVec3 linearVelocity{};
    network::WreckwaterVec3 angularVelocity{};

    [[nodiscard]] bool operator==(
        const WreckwaterReplayVisualPose&) const = default;
};

struct WreckwaterReplayPlaybackEntity {
    network::NetEntityId netEntityId = 0u;
    uint32_t netGeneration = 0u;
    WreckwaterReplayVisualMotion motion =
        WreckwaterReplayVisualMotion::Exact;
    WreckwaterReplayVisualPose visualPose{};
    // Exact state from one certified snapshot. Only visualPose may blend.
    network::WreckwaterEntityState authoritativeState{};

    [[nodiscard]] bool operator==(
        const WreckwaterReplayPlaybackEntity&) const = default;
};

struct WreckwaterReplayAuthoritativeFrame {
    uint32_t schemaVersion = 0u;
    uint32_t flags = 0u;
    uint64_t sessionId = 0u;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint64_t snapshotSequence = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    network::WreckwaterPhase phase =
        network::WreckwaterPhase::Warmup;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    network::WreckwaterOutcomeType outcome =
        network::WreckwaterOutcomeType::Undecided;
    network::WreckwaterCrew winner = network::WreckwaterCrew::None;
    uint32_t matchStateHash = 0u;
    uint32_t eventStreamHash = 0u;
    uint64_t serializedByteHash = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayAuthoritativeFrame&) const = default;
};

struct WreckwaterReplayPlaybackFrame {
    WreckwaterReplayTime requestedTime{};
    WreckwaterReplayTime evaluatedTime{};
    WreckwaterReplayAuthoritativeFrame authoritative{};
    size_t exactSnapshotIndex = kWreckwaterReplayNoSnapshot;
    size_t olderSnapshotIndex = kWreckwaterReplayNoSnapshot;
    size_t newerSnapshotIndex = kWreckwaterReplayNoSnapshot;
    uint32_t entityCount = 0u;
    std::array<
        WreckwaterReplayPlaybackEntity,
        network::kWreckwaterMaximumSnapshotEntities>
        entities{};
    bool exactSnapshot = false;
    bool clampedToStart = false;
    bool clampedToEnd = false;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayPlaybackFrame&) const = default;
};

enum class WreckwaterReplayStepDirection : uint32_t {
    None = 0u,
    Forward = 1u,
    Reverse = 2u,
};

// For Forward, consume [eventBegin, eventEnd) in ascending order.
// For Reverse, undo the same half-open range in descending order.
struct WreckwaterReplayStepResult {
    WreckwaterReplayPlaybackError error =
        WreckwaterReplayPlaybackError::None;
    WreckwaterReplayStepDirection direction =
        WreckwaterReplayStepDirection::None;
    size_t eventBegin = 0u;
    size_t eventEnd = 0u;
    bool clamped = false;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterReplayPlaybackError::None;
    }
};

enum class WreckwaterWreckCamShot : uint32_t {
    Establishing = 0u,
    Follow = 1u,
    Objective = 2u,
    Duel = 3u,
    Impact = 4u,
    Finish = 5u,
};

struct WreckwaterWreckCamCandidate {
    network::NetEntityId netEntityId = 0u;
    uint32_t netGeneration = 0u;
    uint32_t score = 0u;
    MatchEventType reason = MatchEventType::MatchStarted;
    uint64_t reasonEventSequence = 0u;
    uint32_t reasonContribution = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterWreckCamCandidate&) const = default;
};

struct WreckwaterWreckCamSelection {
    WreckwaterWreckCamShot shot = WreckwaterWreckCamShot::Establishing;
    WreckwaterReplayTime time{};
    uint32_t candidateCount = 0u;
    std::array<
        WreckwaterWreckCamCandidate,
        network::kWreckwaterMaximumSnapshotEntities>
        candidates{};
    network::NetEntityId primaryNetEntityId = 0u;
    uint32_t primaryNetGeneration = 0u;
    uint32_t primaryScore = 0u;
    network::NetEntityId secondaryNetEntityId = 0u;
    uint32_t secondaryNetGeneration = 0u;
    uint32_t secondaryScore = 0u;
    MatchEventType reason = MatchEventType::MatchStarted;
    uint64_t reasonEventSequence = 0u;
    bool valid = false;
    bool hasSecondary = false;

    [[nodiscard]] bool operator==(
        const WreckwaterWreckCamSelection&) const = default;
};

struct WreckwaterReplayPlaybackStorageState {
    const void* snapshots = nullptr;
    const void* events = nullptr;
    const void* snapshotEventPrefixes = nullptr;
    size_t snapshotCapacity = 0u;
    size_t eventCapacity = 0u;
    size_t snapshotEventPrefixCapacity = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterReplayPlaybackStorageState&) const = default;
};

class WreckwaterReplayPlayback {
public:
    WreckwaterReplayPlayback() = default;

    WreckwaterReplayPlayback(const WreckwaterReplayPlayback&) = delete;
    WreckwaterReplayPlayback& operator=(
        const WreckwaterReplayPlayback&) = delete;
    WreckwaterReplayPlayback(WreckwaterReplayPlayback&&) = delete;
    WreckwaterReplayPlayback& operator=(
        WreckwaterReplayPlayback&&) = delete;

    // Archive parsing and all storage allocation happen here, never in
    // seek(), stepSubticks(), or wreckCamSelection().
    [[nodiscard]] WreckwaterReplayPlaybackLoadResult initialize(
        std::span<const std::byte> archiveBytes,
        const WreckwaterReplayPlaybackConfig& config = {});
    [[nodiscard]] WreckwaterReplayPlaybackLoadResult initialize(
        const WreckwaterReplayArchive& archive,
        const WreckwaterReplayPlaybackConfig& config = {});
    [[nodiscard]] WreckwaterReplayPlaybackLoadResult initialize(
        const WreckwaterReplayRecorder& recorder,
        const WreckwaterReplayPlaybackConfig& config = {});

    [[nodiscard]] WreckwaterReplayPlaybackError seek(
        WreckwaterReplayTime time) noexcept;
    [[nodiscard]] WreckwaterReplayStepResult stepSubticks(
        int64_t deltaSubticks) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] const WreckwaterReplayPlaybackFrame& frame()
        const noexcept {
        return frame_;
    }
    [[nodiscard]] WreckwaterReplayTime time() const noexcept {
        return frame_.evaluatedTime;
    }
    [[nodiscard]] WreckwaterReplayTime firstTime() const noexcept;
    [[nodiscard]] WreckwaterReplayTime lastTime() const noexcept;
    [[nodiscard]] size_t visibleEventCount() const noexcept {
        return visibleEventCount_;
    }
    [[nodiscard]] std::span<const MatchEvent> events() const noexcept {
        return events_;
    }
    [[nodiscard]] std::span<
        const network::WreckwaterCertifiedSnapshot>
    snapshots() const noexcept {
        return snapshots_;
    }
    [[nodiscard]] const network::WreckwaterCertifiedSnapshot*
    exactSnapshot() const noexcept;
    [[nodiscard]] const WreckwaterReplayConfig& replayConfig()
        const noexcept {
        return replayConfig_;
    }
    [[nodiscard]] const WreckwaterReplaySummary& replaySummary()
        const noexcept {
        return replaySummary_;
    }
    [[nodiscard]] WreckwaterReplayPlaybackStorageState storageState()
        const noexcept;
    [[nodiscard]] WreckwaterWreckCamSelection wreckCamSelection()
        const noexcept;

private:
    [[nodiscard]] WreckwaterReplayPlaybackLoadResult initializeDecoded(
        const WreckwaterReplayArchive& archive,
        const WreckwaterReplayPlaybackConfig& config);
    [[nodiscard]] WreckwaterReplayPlaybackError buildFrame(
        WreckwaterReplayTime requested,
        WreckwaterReplayPlaybackFrame& frame,
        size_t& visibleEventCount) const noexcept;

    WreckwaterReplayPlaybackConfig config_{};
    WreckwaterReplayConfig replayConfig_{};
    WreckwaterReplaySummary replaySummary_{};
    std::vector<network::WreckwaterCertifiedSnapshot> snapshots_;
    std::vector<MatchEvent> events_;
    std::vector<size_t> snapshotEventPrefixes_;
    WreckwaterReplayPlaybackFrame frame_{};
    size_t visibleEventCount_ = 0u;
    bool initialized_ = false;
};

} // namespace voxy::game
