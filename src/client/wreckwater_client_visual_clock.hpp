#pragma once

#include "network/wreckwater_client_replication.hpp"

#include <cstdint>

namespace voxy::client {

inline constexpr uint32_t
    kWreckwaterClientVisualClockMaximumInterpolationDelayTicks = 32u;

enum class WreckwaterClientVisualClockStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    RegressingEvidenceTick,
    TickExhausted,
};

[[nodiscard]] const char* wreckwaterClientVisualClockStatusName(
    WreckwaterClientVisualClockStatus status) noexcept;

struct WreckwaterClientVisualClockResult {
    WreckwaterClientVisualClockStatus status =
        WreckwaterClientVisualClockStatus::NotInitialized;
    network::WreckwaterPhysicsRenderTick renderTick{};
    bool initializedFromEvidence = false;
    bool advancedToDelayFloor = false;
    bool clampedToExtrapolationLimit = false;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterClientVisualClockStatus::Accepted;
    }
};

// Monotonic presentation time derived only from certified physics evidence.
//
// Wall time advances the clock at the authority's fixed physics rate. The
// clock never moves behind (latest evidence - interpolation delay), never
// moves beyond the configured certified extrapolation window, and never
// accepts a regressing evidence tick without an explicit reset.
class WreckwaterClientVisualClock {
public:
    struct Config {
        uint32_t interpolationDelayTicks = 2u;
        uint32_t maximumExtrapolationTicks =
            network::kWreckwaterClientMaximumExtrapolationTicks;
    };

    [[nodiscard]] bool initialize(const Config& config) noexcept;
    void reset() noexcept;

    [[nodiscard]] WreckwaterClientVisualClockResult advance(
        uint64_t elapsedNanoseconds,
        uint64_t certifiedPhysicsEvidenceTick) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool hasEvidence() const noexcept {
        return hasEvidence_;
    }
    [[nodiscard]] uint64_t latestEvidenceTick() const noexcept {
        return latestEvidenceTick_;
    }
    [[nodiscard]] network::WreckwaterPhysicsRenderTick
    renderTick() const noexcept;

private:
    Config config_{};
    uint64_t latestEvidenceTick_ = 0u;
    uint64_t renderWhole_ = 0u;
    uint64_t subTickPhase_ = 0u;
    bool initialized_ = false;
    bool hasEvidence_ = false;
};

} // namespace voxy::client
