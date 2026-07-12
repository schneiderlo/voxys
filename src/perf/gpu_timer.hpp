// ═══════════════════════════════════════════════════════════════════════════════
// gpu_timer.hpp - GPU Timing with Timestamp Queries (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides GPU-side timing using WebGPU timestamp queries.
// Features:
//   - Timestamp query insertion at key points
//   - Async result readback
//   - Graceful fallback when not supported
// Note: Timestamp queries are optional in WebGPU and may not be available.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

// WebGPU header
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::perf {

// ─────────────────────────────────────────────────────────────────────────────
// GPU Timestamp
// ─────────────────────────────────────────────────────────────────────────────

/// A labeled GPU timestamp measurement
struct GPUTimestamp {
    std::string label;
    double timeMs = 0.0;
};

/// GPU timing results for a frame
struct GPUTimingResult {
    std::vector<GPUTimestamp> timestamps;
    double totalMs = 0.0;
    bool valid = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU Timer
// ─────────────────────────────────────────────────────────────────────────────

/// GPU timer using WebGPU timestamp queries.
/// Gracefully handles cases where timestamp queries are not supported.
class GPUTimer {
public:
    /// Maximum number of timestamp queries per frame
    static constexpr uint32_t MAX_QUERIES = 16;
    
    GPUTimer() = default;
    ~GPUTimer();
    
    // Non-copyable
    GPUTimer(const GPUTimer&) = delete;
    GPUTimer& operator=(const GPUTimer&) = delete;
    
    // Movable
    GPUTimer(GPUTimer&& other) noexcept;
    GPUTimer& operator=(GPUTimer&& other) noexcept;
    
    /// Initialize the GPU timer
    /// @param device WebGPU device
    /// @return true if timestamp queries are supported and initialized
    [[nodiscard]] bool init(WGPUDevice device);
    
    /// Check if GPU timing is supported and initialized
    [[nodiscard]] bool isSupported() const noexcept { return supported_; }
    
    /// Release resources
    void shutdown();
    
    /// Begin a new frame's timing (resets query index)
    void beginFrame();
    
    /// Write a timestamp at the current point in the command encoder
    /// @param encoder Command encoder to write timestamp to
    /// @param label Label for this timestamp
    void writeTimestamp(WGPUCommandEncoder encoder, const char* label);
    
    /// Resolve timestamp queries after all commands are recorded
    /// @param encoder Command encoder to use for resolve
    void resolve(WGPUCommandEncoder encoder);
    
    /// Read back timing results (call after queue submission completes)
    /// @return Timing results, or invalid result if not ready
    [[nodiscard]] GPUTimingResult readResults();
    
    /// Get the most recent valid results (may be from previous frame)
    [[nodiscard]] const GPUTimingResult& getLastResults() const noexcept { return lastResults_; }

private:
    WGPUDevice device_ = nullptr;
    WGPUQuerySet querySet_ = nullptr;
    WGPUBuffer resolveBuffer_ = nullptr;
    WGPUBuffer readbackBuffer_ = nullptr;
    
    std::vector<std::string> labels_;
    uint32_t queryIndex_ = 0;
    uint64_t timestampPeriod_ = 1;  // Nanoseconds per tick
    bool supported_ = false;
    bool pendingReadback_ = false;
    
    GPUTimingResult lastResults_;
};

} // namespace voxy::perf



