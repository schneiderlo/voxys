#pragma once

#include "physics/physics_types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

namespace voxy::game::expedition {

// Ephemeral presentation state. Like CoveWaterClock, prepare one value for the
// owned 60 Hz frame and publish only after that command buffer is submitted.
// No wall-clock drift, physical RPM claim, inventory, or saved-state mutation.
class CoveMechanisms {
public:
    struct RopeSample {
        physics::AttachmentHandle handle{};
        uint64_t tick = 0;
        double length = 0;
    };
    static constexpr double tau = 2.0 * std::numbers::pi;
    static constexpr double fullRotorRadiansPerSecond = 2.0 * tau;
    // Authored cable-wrap centerline, not the outer radius of its 18 mm tube.
    static constexpr double drumRadiusMetres = .28;

    [[nodiscard]] bool reset(uint64_t incarnation, uint64_t tick) noexcept {
        if (!incarnation) return false;
        *this = {};
        incarnation_ = incarnation;
        tick_ = tick;
        return true;
    }
    [[nodiscard]] std::optional<CoveMechanisms> prepare(
        uint64_t tick, bool running, double effectiveDrive,
        std::optional<RopeSample> sample = {}) const noexcept {
        if (!incarnation_ || tick < tick_ || tick - tick_ > 1
            || !std::isfinite(effectiveDrive) || std::abs(effectiveDrive) > 1.0)
            return {};
        if (sample && (!sample->handle.valid() || !sample->handle.generation
            || sample->tick > tick || !std::isfinite(sample->length) || sample->length < 0))
            return {};
        auto next = *this;
        next.tick_ = tick;
        next.drive_ = running ? effectiveDrive : 0;
        if (running && tick != tick_)
            next.rotor_ = wrapped(rotor_ + effectiveDrive * fullRotorRadiansPerSecond / 60.0);
        if (!running || !sample) {
            // Resume/new cable establishes a baseline; queued observations
            // must not spin a stopped drum or produce a catch-up jump.
            next.rope_.reset();
            next.ropeFloor_ = std::max(ropeFloor_, tick);
            return next;
        }
        if (sample->tick <= ropeFloor_ || sample->tick < observedThrough_
            || (rope_ && sample->handle != rope_->handle && sample->tick == observedThrough_))
            return next;
        if (!rope_ || sample->handle != rope_->handle) {
            next.rope_ = sample;
            next.observedThrough_ = sample->tick;
        } else if (sample->tick > rope_->tick) {
            const double delta = sample->length - rope_->length;
            const double angle = delta / drumRadiusMetres;
            if (!std::isfinite(angle)) return {};
            next.drum_ = wrapped(drum_ - angle);
            next.rope_ = sample;
            next.observedThrough_ = sample->tick;
        } else if (sample->tick == rope_->tick && sample->length != rope_->length) {
            return {}; // One accepted observation cannot have two lengths.
        }
        // Older same-handle observations are ignored, never integrated twice.
        return next;
    }
    [[nodiscard]] uint64_t incarnation() const noexcept { return incarnation_; }
    [[nodiscard]] uint64_t tick() const noexcept { return tick_; }
    [[nodiscard]] double rotorRadians() const noexcept { return rotor_; }
    [[nodiscard]] double drumRadians() const noexcept { return drum_; }
    [[nodiscard]] double effectiveDrive() const noexcept { return drive_; }
    [[nodiscard]] const std::optional<RopeSample>& ropeSample() const noexcept { return rope_; }
private:
    [[nodiscard]] static double wrapped(double radians) noexcept {
        const auto value = std::fmod(radians, tau);
        return value < 0 ? value + tau : value;
    }
    uint64_t incarnation_ = 0, tick_ = 0;
    uint64_t observedThrough_ = 0, ropeFloor_ = 0;
    double rotor_ = 0, drum_ = 0, drive_ = 0;
    std::optional<RopeSample> rope_;
};

} // namespace voxy::game::expedition
