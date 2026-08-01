#include "client/wreckwater_client_visual_clock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::client {
namespace {

constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000u;

[[nodiscard]] bool tickBefore(
    uint64_t lhsWhole,
    uint64_t lhsPhase,
    uint64_t rhsWhole,
    uint64_t rhsPhase = 0u) noexcept {
    return lhsWhole < rhsWhole
        || (lhsWhole == rhsWhole && lhsPhase < rhsPhase);
}

} // namespace

const char* wreckwaterClientVisualClockStatusName(
    WreckwaterClientVisualClockStatus status) noexcept {
    switch (status) {
        case WreckwaterClientVisualClockStatus::Accepted:
            return "accepted";
        case WreckwaterClientVisualClockStatus::NotInitialized:
            return "not initialized";
        case WreckwaterClientVisualClockStatus::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterClientVisualClockStatus::RegressingEvidenceTick:
            return "regressing evidence tick";
        case WreckwaterClientVisualClockStatus::TickExhausted:
            return "tick exhausted";
    }
    return "unknown";
}

bool WreckwaterClientVisualClock::initialize(
    const Config& config) noexcept {
    initialized_ = false;
    if (config.interpolationDelayTicks
            > kWreckwaterClientVisualClockMaximumInterpolationDelayTicks
        || config.maximumExtrapolationTicks
            > network::kWreckwaterClientMaximumExtrapolationTicks) {
        return false;
    }
    config_ = config;
    initialized_ = true;
    reset();
    return true;
}

void WreckwaterClientVisualClock::reset() noexcept {
    latestEvidenceTick_ = 0u;
    renderWhole_ = 0u;
    subTickPhase_ = 0u;
    hasEvidence_ = false;
}

network::WreckwaterPhysicsRenderTick
WreckwaterClientVisualClock::renderTick() const noexcept {
    const float fraction = static_cast<float>(
        static_cast<double>(subTickPhase_)
        / static_cast<double>(kNanosecondsPerSecond));
    return {
        .whole = renderWhole_,
        .fraction = std::min(
            fraction,
            std::nextafter(1.0f, 0.0f)),
    };
}

WreckwaterClientVisualClockResult
WreckwaterClientVisualClock::advance(
    uint64_t elapsedNanoseconds,
    uint64_t certifiedPhysicsEvidenceTick) noexcept {
    WreckwaterClientVisualClockResult result;
    if (!initialized_) {
        result.status =
            WreckwaterClientVisualClockStatus::NotInitialized;
        return result;
    }

    if (hasEvidence_
        && certifiedPhysicsEvidenceTick < latestEvidenceTick_) {
        result.status =
            WreckwaterClientVisualClockStatus::
                RegressingEvidenceTick;
        result.renderTick = renderTick();
        return result;
    }

    const uint64_t delayFloor =
        certifiedPhysicsEvidenceTick
            > config_.interpolationDelayTicks
        ? certifiedPhysicsEvidenceTick
            - config_.interpolationDelayTicks
        : 0u;

    if (!hasEvidence_) {
        latestEvidenceTick_ = certifiedPhysicsEvidenceTick;
        renderWhole_ = delayFloor;
        subTickPhase_ = 0u;
        hasEvidence_ = true;
        result.status = WreckwaterClientVisualClockStatus::Accepted;
        result.renderTick = renderTick();
        result.initializedFromEvidence = true;
        return result;
    }

    const uint64_t wholeSeconds =
        elapsedNanoseconds / kNanosecondsPerSecond;
    const uint64_t remainderNanoseconds =
        elapsedNanoseconds % kNanosecondsPerSecond;
    constexpr uint64_t rate =
        network::kWreckwaterPhysicsTickRateHz;
    uint64_t candidateWhole = renderWhole_;
    uint64_t candidateSubTickPhase = subTickPhase_;
    bool arithmeticSaturated = false;
    if (wholeSeconds
        > std::numeric_limits<uint64_t>::max() / rate) {
        candidateWhole = std::numeric_limits<uint64_t>::max();
        candidateSubTickPhase = 0u;
        arithmeticSaturated = true;
    } else {
        const uint64_t wholeTicks = wholeSeconds * rate;
        if (wholeTicks
            > std::numeric_limits<uint64_t>::max()
                - candidateWhole) {
            candidateWhole =
                std::numeric_limits<uint64_t>::max();
            candidateSubTickPhase = 0u;
            arithmeticSaturated = true;
        } else {
            candidateWhole += wholeTicks;
        }
    }
    const uint64_t candidatePhase =
        subTickPhase_ + remainderNanoseconds * rate;
    const uint64_t phaseTicks =
        candidatePhase / kNanosecondsPerSecond;
    if (!arithmeticSaturated) {
        if (phaseTicks
            > std::numeric_limits<uint64_t>::max()
                - candidateWhole) {
            candidateWhole =
                std::numeric_limits<uint64_t>::max();
            candidateSubTickPhase = 0u;
            arithmeticSaturated = true;
        } else {
            candidateWhole += phaseTicks;
            candidateSubTickPhase =
                candidatePhase % kNanosecondsPerSecond;
        }
    }

    if (tickBefore(
            candidateWhole, candidateSubTickPhase,
            delayFloor)) {
        candidateWhole = delayFloor;
        candidateSubTickPhase = 0u;
        result.advancedToDelayFloor = true;
    }

    uint64_t maximumWhole = certifiedPhysicsEvidenceTick;
    if (config_.maximumExtrapolationTicks
        > std::numeric_limits<uint64_t>::max() - maximumWhole) {
        maximumWhole = std::numeric_limits<uint64_t>::max();
    } else {
        maximumWhole += config_.maximumExtrapolationTicks;
    }
    if (tickBefore(
            maximumWhole, 0u,
            candidateWhole, candidateSubTickPhase)
        || arithmeticSaturated) {
        candidateWhole = maximumWhole;
        candidateSubTickPhase = 0u;
        result.clampedToExtrapolationLimit = true;
    }

    latestEvidenceTick_ = certifiedPhysicsEvidenceTick;
    renderWhole_ = candidateWhole;
    subTickPhase_ = candidateSubTickPhase;
    result.status = WreckwaterClientVisualClockStatus::Accepted;
    result.renderTick = renderTick();
    return result;
}

} // namespace voxy::client
