// ═══════════════════════════════════════════════════════════════════════════════
// benchmark.hpp - Benchmark Mode (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides automated benchmarking with predefined camera paths.
// Features:
//   - Predefined benchmark scenarios
//   - Automated camera path playback
//   - Statistics collection and reporting
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include <glm/glm.hpp>

#include "core/timer.hpp"  // For FrameStats

namespace voxy::perf {

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark Scenario
// ─────────────────────────────────────────────────────────────────────────────

/// A single benchmark scenario with camera position and frame count
struct BenchmarkScenario {
    std::string name;          ///< Human-readable scenario name
    glm::vec3 cameraPos;       ///< Camera position
    glm::vec3 cameraTarget;    ///< Camera look-at target
    uint32_t frameCount;       ///< Number of frames to render
    
    /// Default scenarios for benchmarking
    static std::vector<BenchmarkScenario> getDefaultScenarios();
};

/// Results from running a benchmark scenario
struct BenchmarkResult {
    std::string scenarioName;
    uint32_t frameCount = 0;
    double totalTimeMs = 0.0;
    double avgFrameMs = 0.0;
    double minFrameMs = 0.0;
    double maxFrameMs = 0.0;
    double fps = 0.0;
    
    // Breakdown (if available)
    double avgUpdateMs = 0.0;
    double avgRenderMs = 0.0;
    double avgPresentMs = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark Runner
// ─────────────────────────────────────────────────────────────────────────────

/// Callback for updating camera position during benchmark
using CameraUpdateCallback = std::function<void(const glm::vec3& pos, const glm::vec3& target)>;

/// Runs benchmark scenarios with automated camera control
class BenchmarkRunner {
public:
    BenchmarkRunner() = default;
    
    /// Set the callback for updating camera position
    void setCameraCallback(CameraUpdateCallback callback);
    
    /// Start running benchmark scenarios
    /// @param scenarios List of scenarios to run (empty = use defaults)
    void start(const std::vector<BenchmarkScenario>& scenarios = {});
    
    /// Stop benchmarking
    void stop();
    
    /// Check if benchmarking is active
    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    
    /// Called each frame during benchmarking
    /// @param frameStats Frame timing statistics from FrameTimer
    /// @return true if benchmark continues, false if complete
    bool onFrame(const FrameStats& frameStats);
    
    /// Get results from completed benchmark
    [[nodiscard]] const std::vector<BenchmarkResult>& getResults() const noexcept { return results_; }
    
    /// Get current scenario name (for display)
    [[nodiscard]] const std::string& getCurrentScenarioName() const;
    
    /// Get current frame within scenario
    [[nodiscard]] uint32_t getCurrentFrame() const noexcept { return currentFrame_; }
    
    /// Get current scenario index
    [[nodiscard]] uint32_t getCurrentScenarioIndex() const noexcept { return currentScenario_; }
    
    /// Get total scenario count
    [[nodiscard]] uint32_t getScenarioCount() const noexcept { 
        return static_cast<uint32_t>(scenarios_.size()); 
    }
    
    /// Print results to log
    void printResults() const;

private:
    void beginScenario();
    void endScenario();
    
    CameraUpdateCallback cameraCallback_;
    std::vector<BenchmarkScenario> scenarios_;
    std::vector<BenchmarkResult> results_;
    
    bool running_ = false;
    uint32_t currentScenario_ = 0;
    uint32_t currentFrame_ = 0;
    
    // Per-scenario timing
    double scenarioStartTime_ = 0.0;
    double scenarioMinFrame_ = 0.0;
    double scenarioMaxFrame_ = 0.0;
    double scenarioSumFrame_ = 0.0;
    double scenarioSumUpdate_ = 0.0;
    double scenarioSumRender_ = 0.0;
    double scenarioSumPresent_ = 0.0;
};

} // namespace voxy::perf


