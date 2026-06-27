// ═══════════════════════════════════════════════════════════════════════════════
// benchmark.cpp - Benchmark Mode Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "perf/benchmark.hpp"
#include "core/log.hpp"

#include <limits>
#include <chrono>

namespace voxy::perf {

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkScenario Implementation
// ─────────────────────────────────────────────────────────────────────────────

std::vector<BenchmarkScenario> BenchmarkScenario::getDefaultScenarios() {
    return {
        {"Overhead View",   {0.0f, 1000.0f, 0.0f},    {0.0f, 0.0f, 0.0f},       300},
        {"Ground Level",    {100.0f, 10.0f, 100.0f},  {200.0f, 10.0f, 200.0f},  300},
        {"Horizon View",    {0.0f, 500.0f, -2000.0f}, {0.0f, 0.0f, 2000.0f},    300},
        {"Close Detail",    {50.0f, 20.0f, 50.0f},    {60.0f, 15.0f, 60.0f},    300},
        {"High Altitude",   {0.0f, 2000.0f, 0.0f},    {1000.0f, 0.0f, 1000.0f}, 300},
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkRunner Implementation
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkRunner::setCameraCallback(CameraUpdateCallback callback) {
    cameraCallback_ = std::move(callback);
}

void BenchmarkRunner::start(const std::vector<BenchmarkScenario>& scenarios) {
    if (scenarios.empty()) {
        scenarios_ = BenchmarkScenario::getDefaultScenarios();
    } else {
        scenarios_ = scenarios;
    }
    
    results_.clear();
    results_.reserve(scenarios_.size());
    
    currentScenario_ = 0;
    currentFrame_ = 0;
    running_ = true;
    
    LOG_INFO("=== Starting Benchmark ===");
    LOG_INFO("Scenarios: {}", scenarios_.size());
    
    beginScenario();
}

void BenchmarkRunner::stop() {
    if (running_) {
        running_ = false;
        LOG_INFO("Benchmark stopped by user");
    }
}

bool BenchmarkRunner::onFrame(const FrameStats& frameStats) {
    if (!running_ || currentScenario_ >= scenarios_.size()) {
        return false;
    }
    
    const auto& scenario = scenarios_[currentScenario_];
    if (currentFrame_ < kWarmupFrames) {
        currentFrame_++;
        return true;
    }

    const uint32_t measuredFrame = currentFrame_ - kWarmupFrames;
    
    // Accumulate statistics
    scenarioSumFrame_ += frameStats.totalMs;
    scenarioSumUpdate_ += frameStats.updateMs;
    scenarioSumRender_ += frameStats.renderMs;
    scenarioSumPresent_ += frameStats.presentMs;
    scenarioMinFrame_ = std::min(scenarioMinFrame_, frameStats.totalMs);
    scenarioMaxFrame_ = std::max(scenarioMaxFrame_, frameStats.totalMs);
    
    currentFrame_++;
    
    // Check if scenario is complete
    if (measuredFrame + 1u >= scenario.frameCount) {
        endScenario();
        
        currentScenario_++;
        currentFrame_ = 0;
        
        if (currentScenario_ < scenarios_.size()) {
            beginScenario();
        } else {
            // All scenarios complete
            running_ = false;
            LOG_INFO("=== Benchmark Complete ===");
            printResults();
            return false;
        }
    }
    
    return true;
}

void BenchmarkRunner::beginScenario() {
    const auto& scenario = scenarios_[currentScenario_];
    
    LOG_INFO("");
    LOG_INFO("Scenario {}/{}: {}", 
             currentScenario_ + 1, scenarios_.size(), scenario.name);
    LOG_INFO("  Frames: {} measured (+{} warmup)", scenario.frameCount, kWarmupFrames);
    LOG_INFO("  Camera: ({:.1f}, {:.1f}, {:.1f}) -> ({:.1f}, {:.1f}, {:.1f})",
             scenario.cameraPos.x, scenario.cameraPos.y, scenario.cameraPos.z,
             scenario.cameraTarget.x, scenario.cameraTarget.y, scenario.cameraTarget.z);
    
    // Set camera position
    if (cameraCallback_) {
        cameraCallback_(scenario.cameraPos, scenario.cameraTarget);
    }
    
    // Reset accumulators
    auto now = std::chrono::high_resolution_clock::now();
    scenarioStartTime_ = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
    scenarioMinFrame_ = std::numeric_limits<double>::max();
    scenarioMaxFrame_ = 0.0;
    scenarioSumFrame_ = 0.0;
    scenarioSumUpdate_ = 0.0;
    scenarioSumRender_ = 0.0;
    scenarioSumPresent_ = 0.0;
}

void BenchmarkRunner::endScenario() {
    const auto& scenario = scenarios_[currentScenario_];
    
    BenchmarkResult result;
    result.scenarioName = scenario.name;
    result.frameCount = scenario.frameCount;
    result.totalTimeMs = scenarioSumFrame_;
    result.avgFrameMs = scenarioSumFrame_ / static_cast<double>(scenario.frameCount);
    result.minFrameMs = scenarioMinFrame_;
    result.maxFrameMs = scenarioMaxFrame_;
    result.fps = (result.avgFrameMs > 0.0) ? (1000.0 / result.avgFrameMs) : 0.0;
    result.avgUpdateMs = scenarioSumUpdate_ / static_cast<double>(scenario.frameCount);
    result.avgRenderMs = scenarioSumRender_ / static_cast<double>(scenario.frameCount);
    result.avgPresentMs = scenarioSumPresent_ / static_cast<double>(scenario.frameCount);
    
    results_.push_back(result);
    
    LOG_INFO("  Complete: {:.1f} FPS (avg {:.2f} ms, min {:.2f} ms, max {:.2f} ms)",
             result.fps, result.avgFrameMs, result.minFrameMs, result.maxFrameMs);
}

const std::string& BenchmarkRunner::getCurrentScenarioName() const {
    static std::string empty;
    if (currentScenario_ < scenarios_.size()) {
        return scenarios_[currentScenario_].name;
    }
    return empty;
}

void BenchmarkRunner::printResults() const {
    LOG_INFO("");
    LOG_INFO("=== Benchmark Results ===");
    LOG_INFO("");
    
    double totalFps = 0.0;
    uint32_t totalFrames = 0;
    
    for (const auto& result : results_) {
        LOG_INFO("Scenario: {}", result.scenarioName);
        LOG_INFO("  Frames: {}", result.frameCount);
        LOG_INFO("  Total Time: {:.0f} ms", result.totalTimeMs);
        LOG_INFO("  Avg Frame: {:.2f} ms ({:.1f} FPS)", result.avgFrameMs, result.fps);
        LOG_INFO("  Min Frame: {:.2f} ms", result.minFrameMs);
        LOG_INFO("  Max Frame: {:.2f} ms", result.maxFrameMs);
        LOG_INFO("  Breakdown: Update {:.2f} ms, Render {:.2f} ms, Present {:.2f} ms",
                 result.avgUpdateMs, result.avgRenderMs, result.avgPresentMs);
        LOG_INFO("");
        
        totalFps += result.fps;
        totalFrames += result.frameCount;
    }
    
    if (!results_.empty()) {
        double avgFps = totalFps / static_cast<double>(results_.size());
        LOG_INFO("Overall Average: {:.1f} FPS ({} total frames)", avgFps, totalFrames);
    }
}

} // namespace voxy::perf

