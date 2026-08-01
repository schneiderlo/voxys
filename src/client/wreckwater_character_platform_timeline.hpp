#pragma once

#include "game/wreckwater_character_movement.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace voxy::client {

inline constexpr uint32_t
    kWreckwaterCharacterPlatformTimelineMaximumPredictionTicks = 128u;
inline constexpr uint32_t
    kWreckwaterCharacterPlatformTimelineDefaultPredictionTicks = 32u;

enum class WreckwaterCharacterPlatformTimelineStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidCertifiedFrame,
    StaleCertifiedSnapshot,
    SnapshotSequenceExhausted,
    TickExhausted,
    PlatformCapacityExceeded,
    PlatformLifetimeMutation,
    ImpossibleCorrection,
    PredictionHorizonExceeded,
    FrameUnavailable,
    PositionOverflow,
};

[[nodiscard]] const char*
wreckwaterCharacterPlatformTimelineStatusName(
    WreckwaterCharacterPlatformTimelineStatus status) noexcept;

struct WreckwaterCharacterPlatformPredictionFrame {
    uint64_t tick = 0u;
    uint64_t certifiedSnapshotSequence = 0u;
    uint64_t certifiedBaseTick = 0u;
    uint32_t count = 0u;
    std::array<
        game::WreckwaterCharacterPlatformSample,
        game::kWreckwaterMaximumCharacterPlatforms>
        platforms{};

    [[nodiscard]] std::span<
        const game::WreckwaterCharacterPlatformSample>
    samples() const noexcept {
        return {platforms.data(), count};
    }

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterPlatformPredictionFrame&) const =
        default;
};

struct WreckwaterCharacterPlatformPredictionFrameResult {
    std::span<const game::WreckwaterCharacterPlatformSample>
        platforms{};
    uint64_t tick = 0u;
    uint64_t certifiedSnapshotSequence = 0u;
    uint64_t certifiedBaseTick = 0u;
    WreckwaterCharacterPlatformTimelineStatus status =
        WreckwaterCharacterPlatformTimelineStatus::FrameUnavailable;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status
            == WreckwaterCharacterPlatformTimelineStatus::Accepted;
    }
};

struct WreckwaterCharacterPlatformTimelineTelemetry {
    uint64_t certifiedBasesAccepted = 0u;
    uint64_t certifiedBasesRejected = 0u;
    uint64_t correctionsAccepted = 0u;
    uint64_t predictedFramesGenerated = 0u;
    uint64_t lifetimeMutations = 0u;
    uint64_t impossibleCorrections = 0u;
    uint64_t horizonOverflows = 0u;
    uint32_t frameHighWater = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterPlatformTimelineTelemetry&) const =
        default;
};

// A bounded 60 Hz platform-motion model for local character prediction.
//
// Inputs are exact skiff states already selected from accepted certified
// snapshots. Visual interpolation is never accepted here. Between certified
// bases, position uses constant linear velocity and orientation uses constant
// world-space angular velocity. This is deliberately a short-horizon model;
// collisions, waves, and helm changes remain unknown until the next certified
// base arrives.
class WreckwaterCharacterPlatformTimeline {
public:
    struct Config {
        uint32_t predictionHorizonTicks =
            kWreckwaterCharacterPlatformTimelineDefaultPredictionTicks;
        game::WreckwaterCharacterMovementAuthority::Config movement{};
    };

    [[nodiscard]] bool initialize(const Config& config) noexcept;
    void reset() noexcept;

    // Atomically replaces the certified base and regenerates every requested
    // future 60 Hz frame. A newer base is checked against the prior prediction
    // before any live frame is replaced.
    [[nodiscard]] WreckwaterCharacterPlatformTimelineStatus
    replaceCertifiedBase(
        uint64_t snapshotSequence,
        uint64_t certifiedTick,
        std::span<
            const game::WreckwaterCharacterPlatformSample>
            certifiedPlatforms,
        uint64_t requiredThroughTick) noexcept;

    // Lazily extends the current certified lineage through targetTick.
    [[nodiscard]] WreckwaterCharacterPlatformTimelineStatus
    generateThrough(uint64_t targetTick) noexcept;

    [[nodiscard]] WreckwaterCharacterPlatformPredictionFrameResult
    frame(uint64_t tick) const noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool hasCertifiedBase() const noexcept {
        return frameCount_ != 0u;
    }
    [[nodiscard]] uint64_t certifiedSnapshotSequence()
        const noexcept {
        return frameCount_ != 0u
            ? frames_[0].certifiedSnapshotSequence
            : 0u;
    }
    [[nodiscard]] uint64_t certifiedBaseTick() const noexcept {
        return frameCount_ != 0u ? frames_[0].tick : 0u;
    }
    [[nodiscard]] uint64_t generatedThroughTick() const noexcept {
        return frameCount_ != 0u
            ? frames_[frameCount_ - 1u].tick
            : 0u;
    }
    [[nodiscard]] uint32_t frameCount() const noexcept {
        return frameCount_;
    }
    [[nodiscard]] uint32_t capacity() const noexcept {
        return config_.predictionHorizonTicks + 1u;
    }
    [[nodiscard]] const
        WreckwaterCharacterPlatformTimelineTelemetry&
    telemetry() const noexcept {
        return telemetry_;
    }

private:
    [[nodiscard]] WreckwaterCharacterPlatformTimelineStatus
    canonicalCertifiedFrame(
        uint64_t snapshotSequence,
        uint64_t certifiedTick,
        std::span<
            const game::WreckwaterCharacterPlatformSample>
            certifiedPlatforms,
        WreckwaterCharacterPlatformPredictionFrame&
            output) const noexcept;
    [[nodiscard]] WreckwaterCharacterPlatformTimelineStatus
    validateCorrection(
        const WreckwaterCharacterPlatformPredictionFrame&
            predicted,
        const WreckwaterCharacterPlatformPredictionFrame&
            certified) const noexcept;
    [[nodiscard]] WreckwaterCharacterPlatformTimelineStatus
    appendThrough(uint64_t targetTick) noexcept;
    [[nodiscard]] WreckwaterCharacterPlatformTimelineStatus
    integrateFrame(
        const WreckwaterCharacterPlatformPredictionFrame& previous,
        WreckwaterCharacterPlatformPredictionFrame&
            current) const noexcept;

    Config config_{};
    std::array<
        WreckwaterCharacterPlatformPredictionFrame,
        kWreckwaterCharacterPlatformTimelineMaximumPredictionTicks
            + 1u>
        frames_{};
    uint32_t frameCount_ = 0u;
    bool initialized_ = false;
    WreckwaterCharacterPlatformTimelineTelemetry telemetry_{};
};

} // namespace voxy::client
