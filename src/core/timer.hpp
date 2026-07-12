// ═══════════════════════════════════════════════════════════════════════════════
// timer.hpp - High-Resolution Timer & Profiling (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides timing utilities for performance measurement and profiling.
// Updated for C++20 with constexpr improvements and std::chrono enhancements.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <chrono>
#include <cstdint>
#include <concepts>
#include <string_view>

namespace voxy::perf {

// ─────────────────────────────────────────────────────────────────────────────
// C++20 Concepts for Time-related Types
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
concept Duration = requires(T t) {
    { std::chrono::duration_cast<std::chrono::milliseconds>(t) };
};

// ─────────────────────────────────────────────────────────────────────────────
// High-Resolution Timer
// ─────────────────────────────────────────────────────────────────────────────

class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::duration<double, std::milli>;
    
    constexpr Timer() noexcept = default;
    
    // Start/restart the timer
    void start() noexcept {
        start_ = Clock::now();
        running_ = true;
    }
    
    // Stop the timer and accumulate elapsed time
    void stop() noexcept {
        if (running_) {
            auto end = Clock::now();
            elapsed_ += std::chrono::duration_cast<Duration>(end - start_).count();
            running_ = false;
        }
    }
    
    // Reset elapsed time to zero
    constexpr void reset() noexcept {
        elapsed_ = 0.0;
        running_ = false;
    }
    
    // Restart (reset + start)
    void restart() noexcept {
        reset();
        start();
    }
    
    // Get elapsed time in milliseconds
    [[nodiscard]] double elapsedMs() const noexcept {
        double total = elapsed_;
        if (running_) {
            auto now = Clock::now();
            total += std::chrono::duration_cast<Duration>(now - start_).count();
        }
        return total;
    }
    
    // Get elapsed time in seconds
    [[nodiscard]] double elapsedSec() const noexcept {
        return elapsedMs() / 1000.0;
    }
    
    // Get elapsed time as a duration (C++20 style)
    [[nodiscard]] Duration elapsed() const noexcept {
        return Duration{elapsedMs()};
    }
    
    // Check if timer is currently running
    [[nodiscard]] constexpr bool isRunning() const noexcept { return running_; }
    
private:
    TimePoint start_{};
    double elapsed_ = 0.0;
    bool running_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Scoped Timer (RAII profiling)
// ─────────────────────────────────────────────────────────────────────────────

class ScopedTimer {
public:
    // Constructor starts timing
    // If outMs is provided, elapsed time is written there on destruction
    explicit ScopedTimer(std::string_view name, double* outMs = nullptr) noexcept
        : name_(name), outMs_(outMs) {
        timer_.start();
    }
    
    ~ScopedTimer() {
        timer_.stop();
        if (outMs_) {
            *outMs_ = timer_.elapsedMs();
        }
    }
    
    // Non-copyable, non-movable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;
    
    // Get current elapsed time (before destruction)
    [[nodiscard]] double elapsedMs() const noexcept {
        return timer_.elapsedMs();
    }
    
    // Get the name of this timing scope
    [[nodiscard]] constexpr std::string_view name() const noexcept { return name_; }
    
private:
    std::string_view name_;
    double* outMs_;
    Timer timer_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Frame Statistics
// ─────────────────────────────────────────────────────────────────────────────

struct FrameStats {
    double totalMs = 0.0;      // Total frame time
    double updateMs = 0.0;     // Time spent in update logic
    double renderMs = 0.0;     // Time spent in render commands
    double presentMs = 0.0;    // Time spent waiting for present
    uint32_t frameNumber = 0;  // Current frame count
    
    // Computed values
    [[nodiscard]] constexpr double fps() const noexcept {
        return totalMs > 0.0 ? 1000.0 / totalMs : 0.0;
    }
    
    // C++20: Default three-way comparison
    [[nodiscard]] constexpr auto operator<=>(const FrameStats&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Frame Timer (for consistent frame timing)
// ─────────────────────────────────────────────────────────────────────────────

class FrameTimer {
public:
    constexpr FrameTimer() noexcept = default;
    
    // Call at the start of each frame
    void beginFrame() noexcept {
        frameStart_ = Timer::Clock::now();
        stats_.frameNumber++;
    }
    
    // Call at the end of each frame
    void endFrame() noexcept {
        auto now = Timer::Clock::now();
        stats_.totalMs = std::chrono::duration_cast<Timer::Duration>(now - frameStart_).count();
        
        // Update average
        avgAccum_ += stats_.totalMs;
        avgCount_++;
    }
    
    // Mark timing sections within a frame
    void markUpdate() noexcept {
        auto now = Timer::Clock::now();
        stats_.updateMs = std::chrono::duration_cast<Timer::Duration>(now - frameStart_).count();
    }
    
    void markRender() noexcept {
        auto now = Timer::Clock::now();
        stats_.renderMs = std::chrono::duration_cast<Timer::Duration>(now - frameStart_).count() 
                        - stats_.updateMs;
    }
    
    void markPresent() noexcept {
        auto now = Timer::Clock::now();
        stats_.presentMs = std::chrono::duration_cast<Timer::Duration>(now - frameStart_).count()
                         - stats_.updateMs - stats_.renderMs;
    }
    
    // Get last frame statistics
    [[nodiscard]] constexpr const FrameStats& getLastFrameStats() const noexcept {
        return stats_;
    }
    
    // Get average frame time over recent frames
    [[nodiscard]] constexpr double getAverageFrameTimeMs() const noexcept {
        return avgCount_ > 0 ? avgAccum_ / avgCount_ : 0.0;
    }
    
    // Get average FPS
    [[nodiscard]] constexpr double getAverageFps() const noexcept {
        double avgMs = getAverageFrameTimeMs();
        return avgMs > 0.0 ? 1000.0 / avgMs : 0.0;
    }
    
    // Reset average accumulator
    constexpr void resetAverage() noexcept {
        avgAccum_ = 0.0;
        avgCount_ = 0;
    }
    
    // Get frame number
    [[nodiscard]] constexpr uint32_t getFrameNumber() const noexcept {
        return stats_.frameNumber;
    }
    
private:
    Timer::TimePoint frameStart_{};
    FrameStats stats_{};
    double avgAccum_ = 0.0;
    uint32_t avgCount_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

// Get current time in milliseconds since epoch (for absolute timestamps)
[[nodiscard]] inline double currentTimeMs() noexcept {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(duration).count();
}

// Sleep for specified milliseconds (use sparingly)
// C++20: Can use std::this_thread::sleep_for, but keeping busy-wait option
inline void sleepMs(double ms) noexcept {
    if (ms > 0) [[likely]] {
        auto start = Timer::Clock::now();
        while (std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                   Timer::Clock::now() - start).count() < ms) {
            // Busy wait for small durations
        }
    }
}

// C++20: Helper to convert any duration to milliseconds
template<Duration D>
[[nodiscard]] constexpr double toMs(D duration) noexcept {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(duration).count();
}

} // namespace voxy::perf

// ═══════════════════════════════════════════════════════════════════════════════
// Convenience Macros
// ═══════════════════════════════════════════════════════════════════════════════

#define PERF_SCOPE(name) voxy::perf::ScopedTimer _perf_##__LINE__(name)
#define PERF_SCOPE_MS(name, outMs) voxy::perf::ScopedTimer _perf_##__LINE__(name, outMs)

