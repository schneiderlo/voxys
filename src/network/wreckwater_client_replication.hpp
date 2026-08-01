#pragma once

#include "network/wreckwater_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace voxy::network {

inline constexpr uint32_t kWreckwaterClientDefaultHistorySnapshots = 8u;
inline constexpr uint32_t kWreckwaterClientMaximumHistorySnapshots = 32u;
inline constexpr uint32_t kWreckwaterClientMaximumExtrapolationTicks = 3u;
inline constexpr uint32_t
    kWreckwaterClientDefaultCharacterInterpolationGapTicks = 6u;
inline constexpr uint32_t
    kWreckwaterClientMaximumCharacterInterpolationGapTicks = 32u;
inline constexpr uint32_t kWreckwaterPhysicsTickRateHz = 60u;
inline constexpr double kWreckwaterSectorSizeMeters = 256.0;

enum class WreckwaterAuthoritativeSamplePolicy : uint32_t {
    OlderSnapshot = 1u,
    NewerSnapshot = 2u,
};

enum class WreckwaterClientReplicationError : uint32_t {
    None = 0u,
    InvalidConfiguration,
    EmptyHistory,
    MalformedSnapshot,
    EntityCapacityExceeded,
    IdentityMismatch,
    NonMonotonicSequence,
    RegressingApplicationTick,
    RegressingEvidenceTick,
    InvalidEntityGeneration,
    InvalidCharacterGeneration,
    IdentityRegistryCapacityExceeded,
    InvalidRenderTick,
    PositionOverflow,
};

[[nodiscard]] const char* wreckwaterClientReplicationErrorName(
    WreckwaterClientReplicationError error) noexcept;

struct WreckwaterClientReplicationIdentity {
    uint64_t sessionId = 0u;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterClientReplicationIdentity&) const = default;
};

// Visual pose time is the certified GPU physics-evidence timeline. It is not
// the later application tick that carried the pose.
struct WreckwaterPhysicsRenderTick {
    uint64_t whole = 0u;
    float fraction = 0.0f;

    [[nodiscard]] bool operator==(
        const WreckwaterPhysicsRenderTick&) const = default;
};

enum class WreckwaterVisualMotionMode : uint32_t {
    Snapped = 0u,
    Interpolated = 1u,
    Extrapolated = 2u,
    FrozenStale = 3u,
};

struct WreckwaterVisualPose {
    std::array<int32_t, 3> sector{};
    WreckwaterVec3 localPosition{};
    WreckwaterQuaternion orientation{};
    WreckwaterVec3 linearVelocity{};
    WreckwaterVec3 angularVelocity{};

    [[nodiscard]] bool operator==(const WreckwaterVisualPose&) const = default;
};

// Motion is presentation-only. Every gameplay and collision field remains an
// exact copy of one complete certified snapshot in authoritativeState.
struct WreckwaterSampledEntity {
    NetEntityId netEntityId = 0u;
    uint32_t netGeneration = 0u;
    WreckwaterVisualMotionMode motionMode =
        WreckwaterVisualMotionMode::Snapped;
    WreckwaterVisualPose visualPose{};
    WreckwaterEntityState authoritativeState{};

    [[nodiscard]] bool operator==(
        const WreckwaterSampledEntity&) const = default;
};

struct WreckwaterVisualCharacterPose {
    std::array<int32_t, 3> sector{};
    WreckwaterVec3 localFeetPosition{};
    WreckwaterVec3 worldVelocity{};

    [[nodiscard]] bool operator==(
        const WreckwaterVisualCharacterPose&) const = default;
};

// Character presentation follows the same certified physics-evidence
// timeline as rigid entities. Lifecycle, connection, mode, platform, and
// input-ack fields remain an exact indivisible authoritative record.
struct WreckwaterSampledCharacter {
    uint32_t characterHandle = 0u;
    WreckwaterVisualMotionMode motionMode =
        WreckwaterVisualMotionMode::Snapped;
    WreckwaterVisualCharacterPose visualPose{};
    WreckwaterCharacterState authoritativeState{};

    [[nodiscard]] bool operator==(
        const WreckwaterSampledCharacter&) const = default;
};

// These fields are copied as one indivisible unit from the policy-selected
// certified snapshot. They are never interpolated or predicted.
struct WreckwaterAuthoritativeSampleState {
    uint32_t schemaVersion = 0u;
    uint32_t flags = 0u;
    WreckwaterClientReplicationIdentity identity{};
    uint64_t snapshotSequence = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    WreckwaterPhase phase = WreckwaterPhase::Warmup;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    WreckwaterOutcomeType outcome = WreckwaterOutcomeType::Undecided;
    WreckwaterCrew winner = WreckwaterCrew::None;
    uint32_t matchStateHash = 0u;
    uint32_t eventStreamHash = 0u;
    uint64_t serializedByteHash = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterAuthoritativeSampleState&) const = default;
};

struct WreckwaterClientSample {
    WreckwaterPhysicsRenderTick requestedPhysicsTick{};
    WreckwaterPhysicsRenderTick evaluatedPhysicsTick{};
    WreckwaterAuthoritativeSampleState authoritative{};
    uint32_t entityCount = 0u;
    std::array<
        WreckwaterSampledEntity, kWreckwaterMaximumSnapshotEntities>
        entities{};
    uint32_t characterCount = 0u;
    std::array<
        WreckwaterSampledCharacter,
        kWreckwaterMaximumSnapshotCharacters>
        characters{};
    bool stale = false;
    bool clampedToOldest = false;

    [[nodiscard]] bool operator==(const WreckwaterClientSample&) const =
        default;
};

struct WreckwaterClientSampleResult {
    WreckwaterClientSample sample{};
    WreckwaterClientReplicationError error =
        WreckwaterClientReplicationError::None;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterClientReplicationError::None;
    }
};

struct WreckwaterClientIngestResult {
    WreckwaterClientReplicationError error =
        WreckwaterClientReplicationError::None;
    bool replacedSameEvidenceTick = false;
    bool evictedOldest = false;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterClientReplicationError::None;
    }
};

struct WreckwaterClientReplicationTelemetry {
    uint64_t acceptedSnapshots = 0u;
    uint64_t rejectedSnapshots = 0u;
    uint64_t replacedSameEvidenceTickSnapshots = 0u;
    uint64_t evictedSnapshots = 0u;
    uint64_t staleSamples = 0u;
    uint32_t historyHighWater = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterClientReplicationTelemetry&) const = default;
};

class WreckwaterClientSnapshotBuffer {
public:
    struct Config {
        uint32_t historySnapshots =
            kWreckwaterClientDefaultHistorySnapshots;
        uint32_t maximumEntities =
            kWreckwaterMaximumSnapshotEntities;
        uint32_t maximumExtrapolationTicks =
            kWreckwaterClientMaximumExtrapolationTicks;
        // Never sweep a remote character through a long outage. Short gaps
        // interpolate; longer gaps hold the older certified pose until the
        // newer evidence tick.
        uint32_t maximumCharacterInterpolationGapTicks =
            kWreckwaterClientDefaultCharacterInterpolationGapTicks;
        WreckwaterAuthoritativeSamplePolicy authoritativePolicy =
            WreckwaterAuthoritativeSamplePolicy::NewerSnapshot;
    };

    WreckwaterClientSnapshotBuffer();
    explicit WreckwaterClientSnapshotBuffer(Config config);

    // Reset is the only way to admit a different session, match, world, or
    // authority epoch. Preallocated history/entity storage is retained.
    void reset() noexcept;

    [[nodiscard]] WreckwaterClientIngestResult ingest(
        const WreckwaterCertifiedSnapshot& snapshot);
    [[nodiscard]] WreckwaterClientSampleResult sample(
        WreckwaterPhysicsRenderTick physicsRenderTick) const noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool hasIdentity() const noexcept {
        return identityPinned_;
    }
    [[nodiscard]] const WreckwaterClientReplicationIdentity& identity()
        const noexcept {
        return identity_;
    }
    [[nodiscard]] uint32_t size() const noexcept { return count_; }
    [[nodiscard]] uint32_t capacity() const noexcept {
        return config_.historySnapshots;
    }
    [[nodiscard]] const WreckwaterClientReplicationTelemetry& telemetry()
        const noexcept {
        return telemetry_;
    }

private:
    struct Slot {
        WreckwaterCertifiedSnapshot snapshot{};
    };

    struct NetworkLifetime {
        NetEntityId id = 0u;
        uint32_t generation = 0u;
        WreckwaterEntityKind kind = WreckwaterEntityKind::Skiff;
        uint32_t logicalId = 0u;
        uint32_t logicalGeneration = 0u;
    };

    struct LogicalLifetime {
        WreckwaterEntityKind kind = WreckwaterEntityKind::Skiff;
        uint32_t id = 0u;
        uint32_t generation = 0u;
    };

    struct AttachmentLifetime {
        LogicalAttachmentId id = 0u;
        uint32_t generation = 0u;
        NetEntityId ownerNetEntityId = 0u;
        uint32_t ownerNetGeneration = 0u;
        uint32_t cargoId = 0u;
        uint32_t cargoGeneration = 0u;
    };

    struct CharacterLifetime {
        uint32_t handle = 0u;
        uint64_t playerId = 0u;
        uint32_t connectionGeneration = 0u;
        uint64_t lastAppliedCharacterInputSequence = 0u;
        bool connected = false;
    };

    [[nodiscard]] const WreckwaterCertifiedSnapshot& at(
        uint32_t chronologicalIndex) const noexcept;
    [[nodiscard]] WreckwaterCertifiedSnapshot& slotForAppend(
        bool& evicted) noexcept;
    [[nodiscard]] WreckwaterClientReplicationError validateGenerations(
        const WreckwaterCertifiedSnapshot& snapshot) const noexcept;
    void updateGenerationRegistries(
        const WreckwaterCertifiedSnapshot& snapshot) noexcept;
    void copyInto(
        WreckwaterCertifiedSnapshot& destination,
        const WreckwaterCertifiedSnapshot& source);

    Config config_{};
    std::vector<Slot> slots_;
    uint32_t oldest_ = 0u;
    uint32_t count_ = 0u;
    bool initialized_ = false;
    bool identityPinned_ = false;
    WreckwaterClientReplicationIdentity identity_{};
    uint64_t latestSequence_ = 0u;
    uint64_t latestApplicationTick_ = 0u;
    uint64_t latestEvidenceTick_ = 0u;
    std::array<
        NetworkLifetime, kWreckwaterMaximumSnapshotEntities>
        networkLifetimes_{};
    std::array<
        LogicalLifetime, kWreckwaterMaximumSnapshotEntities>
        logicalLifetimes_{};
    std::array<
        AttachmentLifetime, kWreckwaterMaximumSnapshotEntities>
        attachmentLifetimes_{};
    std::array<
        CharacterLifetime, kWreckwaterMaximumSnapshotCharacters>
        characterLifetimes_{};
    uint32_t networkLifetimeCount_ = 0u;
    uint32_t logicalLifetimeCount_ = 0u;
    uint32_t attachmentLifetimeCount_ = 0u;
    uint32_t characterLifetimeCount_ = 0u;
    mutable WreckwaterClientReplicationTelemetry telemetry_{};
};

} // namespace voxy::network
