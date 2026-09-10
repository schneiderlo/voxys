#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace voxy::game::expedition {

// The starter cove encodes at most one 60 Hz tick per submission. Prepare a
// value without publishing it; assign it only after the matching GPU submit.
// Paused maintenance ticks (restore, rescue, neutral controls) keep wave phase.
class CoveWaterClock {
public:
    [[nodiscard]] bool reset(uint64_t tick, double seconds) noexcept {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > maximumSeconds) return false;
        tick_ = tick;
        seconds_ = seconds;
        subsecondTicks_ = 0;
        initialized_ = true;
        return true;
    }
    [[nodiscard]] std::optional<CoveWaterClock> prepare(
        uint64_t tick, bool advancing) const noexcept {
        if (!initialized_ || tick < tick_ || tick - tick_ > 1) return {};
        auto next = *this;
        // Subtract integers first: absolute ticks may exceed f64 precision.
        if (advancing && tick != tick_) {
            if (++next.subsecondTicks_ == 60) {
                next.seconds_ += 1;
                next.subsecondTicks_ = 0;
            }
            if (next.seconds() > maximumSeconds) return {};
        }
        next.tick_ = tick;
        return next;
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] uint64_t tick() const noexcept { return tick_; }
    [[nodiscard]] double seconds() const noexcept {
        return seconds_ + static_cast<double>(subsecondTicks_) / 60.0;
    }
    [[nodiscard]] float phase() const noexcept {
        // f32 can round a remainder just below 4096 up to 4096.
        const auto phase = static_cast<float>(std::fmod(seconds(), 4096.0));
        return phase < 4096.0f ? phase : 0.0f;
    }
private:
    static constexpr double maximumSeconds = 1e12; // Existing SVCE water bound.
    uint64_t tick_ = 0;
    // Keep fractional ticks separate from a large saved time: repeatedly
    // adding 1/60 to 1e12 seconds would accumulate ~3.4 ms drift per second.
    double seconds_ = 0;
    uint32_t subsecondTicks_ = 0;
    bool initialized_ = false;
};

} // namespace voxy::game::expedition
