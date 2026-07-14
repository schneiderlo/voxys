// ═══════════════════════════════════════════════════════════════════════════════
// benchmark.cpp - Benchmark Mode Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "perf/benchmark.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {

double observedPercentile(const std::vector<double>& sortedSamples,
                          double percentile) {
    if (sortedSamples.empty()) return 0.0;
    const auto rank = static_cast<size_t>(
        std::ceil(percentile * static_cast<double>(sortedSamples.size())));
    return sortedSamples[std::min(std::max<size_t>(rank, 1u) - 1u,
                                  sortedSamples.size() - 1u)];
}

} // namespace

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

void BenchmarkRunner::setPhysicsBackend(std::string backend) {
    physicsBackend_ = std::move(backend);
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
    LOG_INFO("Physics backend: {}", physicsBackend_);
    
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
    
    // Accumulate statistics
    scenarioSumFrame_ += frameStats.totalMs;
    scenarioSumUpdate_ += frameStats.updateMs;
    scenarioSumRender_ += frameStats.renderMs;
    scenarioSumPresent_ += frameStats.presentMs;
    scenarioSumPhysicsSimulation_ += frameStats.physicsSimulationMs;
    scenarioSumPhysicsWater_ += frameStats.physicsWaterMs;
    scenarioSumPhysicsSnapshot_ += frameStats.physicsSnapshotMs;
    scenarioSumPrimitiveCull_ += frameStats.primitiveCullMs;
    scenarioSumPrimitivePacking_ += frameStats.primitivePackingMs;
    scenarioSumPrimitiveUpload_ += frameStats.primitiveUploadMs;
    scenarioSumPrimitiveRender_ += frameStats.primitiveRenderMs;
    scenarioFrameTimes_.push_back(frameStats.totalMs);
    
    if (currentFrame_ > 0) {  // Skip first frame for min/max (warmup)
        scenarioMinFrame_ = std::min(scenarioMinFrame_, frameStats.totalMs);
        scenarioMaxFrame_ = std::max(scenarioMaxFrame_, frameStats.totalMs);
    }
    
    currentFrame_++;
    
    // Check if scenario is complete
    if (currentFrame_ >= scenario.frameCount) {
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
    LOG_INFO("  Frames: {}", scenario.frameCount);
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
    scenarioSumPhysicsSimulation_ = 0.0;
    scenarioSumPhysicsWater_ = 0.0;
    scenarioSumPhysicsSnapshot_ = 0.0;
    scenarioSumPrimitiveCull_ = 0.0;
    scenarioSumPrimitivePacking_ = 0.0;
    scenarioSumPrimitiveUpload_ = 0.0;
    scenarioSumPrimitiveRender_ = 0.0;
    scenarioFrameTimes_.clear();
    scenarioFrameTimes_.reserve(scenario.frameCount);
}

void BenchmarkRunner::endScenario() {
    const auto& scenario = scenarios_[currentScenario_];
    
    auto now = std::chrono::high_resolution_clock::now();
    double endTime = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
    double totalTime = endTime - scenarioStartTime_;
    
    BenchmarkResult result;
    result.scenarioName = scenario.name;
    result.physicsBackend = physicsBackend_;
    result.frameCount = scenario.frameCount;
    result.totalTimeMs = totalTime;
    result.avgFrameMs = scenarioSumFrame_ / static_cast<double>(scenario.frameCount);
    // The first frame warms newly selected camera state and is excluded from
    // latency distributions, matching the existing min/max convention.
    if (scenarioFrameTimes_.size() > 1u) {
        scenarioFrameTimes_.erase(scenarioFrameTimes_.begin());
    }
    std::sort(scenarioFrameTimes_.begin(), scenarioFrameTimes_.end());
    result.p50FrameMs = observedPercentile(scenarioFrameTimes_, 0.50);
    result.p95FrameMs = observedPercentile(scenarioFrameTimes_, 0.95);
    result.p99FrameMs = observedPercentile(scenarioFrameTimes_, 0.99);
    result.minFrameMs = scenarioMinFrame_;
    result.maxFrameMs = scenarioMaxFrame_;
    result.fps = (result.avgFrameMs > 0.0) ? (1000.0 / result.avgFrameMs) : 0.0;
    result.avgUpdateMs = scenarioSumUpdate_ / static_cast<double>(scenario.frameCount);
    result.avgRenderMs = scenarioSumRender_ / static_cast<double>(scenario.frameCount);
    result.avgPresentMs = scenarioSumPresent_ / static_cast<double>(scenario.frameCount);
    result.avgPhysicsSimulationMs = scenarioSumPhysicsSimulation_ /
        static_cast<double>(scenario.frameCount);
    result.avgPhysicsWaterMs = scenarioSumPhysicsWater_ /
        static_cast<double>(scenario.frameCount);
    result.avgPhysicsSnapshotMs = scenarioSumPhysicsSnapshot_ /
        static_cast<double>(scenario.frameCount);
    result.avgPrimitiveCullMs = scenarioSumPrimitiveCull_ /
        static_cast<double>(scenario.frameCount);
    result.avgPrimitivePackingMs = scenarioSumPrimitivePacking_ /
        static_cast<double>(scenario.frameCount);
    result.avgPrimitiveUploadMs = scenarioSumPrimitiveUpload_ /
        static_cast<double>(scenario.frameCount);
    result.avgPrimitiveRenderMs = scenarioSumPrimitiveRender_ /
        static_cast<double>(scenario.frameCount);
    
    results_.push_back(result);
    
    LOG_INFO("  Complete: {:.1f} FPS (p50 {:.2f} ms, p95 {:.2f} ms, p99 {:.2f} ms)",
             result.fps, result.p50FrameMs, result.p95FrameMs, result.p99FrameMs);
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
        LOG_INFO("  Physics backend: {}", result.physicsBackend);
        LOG_INFO("  Frames: {}", result.frameCount);
        LOG_INFO("  Total Time: {:.0f} ms", result.totalTimeMs);
        LOG_INFO("  Avg Frame: {:.2f} ms ({:.1f} FPS)", result.avgFrameMs, result.fps);
        LOG_INFO("  Latency: p50 {:.2f} ms, p95 {:.2f} ms, p99 {:.2f} ms",
                 result.p50FrameMs, result.p95FrameMs, result.p99FrameMs);
        LOG_INFO("  Min Frame: {:.2f} ms", result.minFrameMs);
        LOG_INFO("  Max Frame: {:.2f} ms", result.maxFrameMs);
        LOG_INFO("  Breakdown: Update {:.2f} ms, Render {:.2f} ms, Present {:.2f} ms",
                 result.avgUpdateMs, result.avgRenderMs, result.avgPresentMs);
        LOG_INFO("  Physics: simulation {:.2f} ms, water {:.2f} ms, snapshot {:.2f} ms",
                 result.avgPhysicsSimulationMs, result.avgPhysicsWaterMs,
                 result.avgPhysicsSnapshotMs);
        LOG_INFO("  Primitives: cull {:.2f} ms, pack {:.2f} ms, upload {:.2f} ms, render {:.2f} ms",
                 result.avgPrimitiveCullMs, result.avgPrimitivePackingMs,
                 result.avgPrimitiveUploadMs, result.avgPrimitiveRenderMs);
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
