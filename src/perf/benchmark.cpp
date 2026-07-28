// ═══════════════════════════════════════════════════════════════════════════════
// benchmark.cpp - Benchmark Mode Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "perf/benchmark.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr uint32_t kMaximumBenchmarkFrames = 1'000'000u;
constexpr uint64_t kMaximumBenchmarkTotalFrames = 1'000'000u;
constexpr size_t kMaximumBenchmarkScenarios = 1'024u;
constexpr size_t kMaximumBenchmarkScenarioNameBytes = 256u;
constexpr double kMaximumBenchmarkMetricMilliseconds =
    std::numeric_limits<double>::max()
    / static_cast<double>(kMaximumBenchmarkTotalFrames + 1u);

double observedPercentile(const std::vector<double>& sortedSamples,
                          double percentile) {
    if (sortedSamples.empty()) return 0.0;
    const auto rank = static_cast<size_t>(
        std::ceil(percentile * static_cast<double>(sortedSamples.size())));
    return sortedSamples[std::min(std::max<size_t>(rank, 1u) - 1u,
                                  sortedSamples.size() - 1u)];
}

bool finiteVec3(const glm::vec3& value) noexcept {
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool validFrameStats(const voxy::perf::FrameStats& stats) noexcept {
    for (const double value : {
             stats.totalMs, stats.updateMs, stats.renderMs, stats.presentMs,
             stats.physicsSimulationMs, stats.physicsWaterMs,
             stats.physicsSnapshotMs, stats.primitiveCullMs,
             stats.primitivePackingMs, stats.primitiveUploadMs,
             stats.primitiveRenderMs}) {
        if (!std::isfinite(value) || value < 0.0
            || value > kMaximumBenchmarkMetricMilliseconds) {
            return false;
        }
    }
    return true;
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

void BenchmarkRunner::setExpectedBodyCount(uint32_t bodyCount) noexcept {
    expectedBodyCount_ = bodyCount;
}

void BenchmarkRunner::setMinimumThroughputFps(double minimumFps) noexcept {
    if (!std::isfinite(minimumFps)) return;
    minimumThroughputFps_ = std::max(minimumFps, 0.0);
}

double BenchmarkRunner::overallThroughputFps() const noexcept {
    uint64_t totalFrames = 0u;
    double totalFrameMs = 0.0;
    for (const BenchmarkResult& result : results_) {
        totalFrames += result.frameCount;
        totalFrameMs += result.avgFrameMs
                      * static_cast<double>(result.frameCount);
    }
    return totalFrameMs > 0.0
        ? static_cast<double>(totalFrames) * 1000.0 / totalFrameMs
        : 0.0;
}

bool BenchmarkRunner::passed() const noexcept {
    if (running_ || results_.size() != scenarios_.size() || results_.empty()) {
        return false;
    }
    const bool workloadValid = std::all_of(
        results_.begin(), results_.end(), [](const BenchmarkResult& result) {
            return result.bodyCountInvariantPassed;
        });
    return workloadValid
        && (minimumThroughputFps_ <= 0.0
            || overallThroughputFps() >= minimumThroughputFps_);
}

void BenchmarkRunner::start(const std::vector<BenchmarkScenario>& scenarios) {
    if (scenarios.size() > kMaximumBenchmarkScenarios) {
        LOG_ERROR("Cannot start benchmark: invalid scenario list");
        return;
    }
    const auto validScenarios = [](const auto& candidates) {
        if (candidates.empty()) return false;
        uint64_t totalFrames = 0u;
        for (const BenchmarkScenario& scenario : candidates) {
            if (scenario.name.size() > kMaximumBenchmarkScenarioNameBytes
                || scenario.frameCount == 0u
                || scenario.frameCount > kMaximumBenchmarkFrames
                || scenario.frameCount
                    > kMaximumBenchmarkTotalFrames - totalFrames
                || !finiteVec3(scenario.cameraPos)
                || !finiteVec3(scenario.cameraTarget)) {
                return false;
            }
            totalFrames += scenario.frameCount;
        }
        return true;
    };
    if (!scenarios.empty() && !validScenarios(scenarios)) {
        LOG_ERROR("Cannot start benchmark: invalid scenario list");
        return;
    }
    std::vector<BenchmarkScenario> replacement = scenarios.empty()
        ? BenchmarkScenario::getDefaultScenarios() : scenarios;
    if (!validScenarios(replacement)) {
        LOG_ERROR("Cannot start benchmark: invalid default scenarios");
        return;
    }
    scenarios_ = std::move(replacement);
    
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

bool BenchmarkRunner::willCompleteScenarioAfterCurrentFrame() const noexcept {
    return running_ && currentScenario_ < scenarios_.size()
        && currentFrame_ + 1u >= scenarios_[currentScenario_].frameCount;
}

bool BenchmarkRunner::onFrame(const FrameStats& frameStats) {
    if (!running_ || currentScenario_ >= scenarios_.size()) {
        return false;
    }
    if (!validFrameStats(frameStats)) {
        running_ = false;
        LOG_ERROR("Benchmark stopped: frame telemetry is not finite/nonnegative");
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
    scenarioMinResidentBodies_ = std::min(
        scenarioMinResidentBodies_, frameStats.physicsResidentBodies);
    scenarioMaxResidentBodies_ = std::max(
        scenarioMaxResidentBodies_, frameStats.physicsResidentBodies);
    if (frameStats.physicsActiveBodiesObserved) {
        scenarioMinActiveBodies_ = std::min(
            scenarioMinActiveBodies_, frameStats.physicsActiveBodies);
        ++scenarioActiveBodySamples_;
    }
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
    auto now = std::chrono::steady_clock::now();
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
    scenarioMinResidentBodies_ = std::numeric_limits<uint32_t>::max();
    scenarioMaxResidentBodies_ = 0;
    scenarioMinActiveBodies_ = std::numeric_limits<uint32_t>::max();
    scenarioActiveBodySamples_ = 0;
    scenarioFrameTimes_.clear();
    scenarioFrameTimes_.reserve(scenario.frameCount);
}

void BenchmarkRunner::endScenario() {
    const auto& scenario = scenarios_[currentScenario_];
    
    auto now = std::chrono::steady_clock::now();
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
    result.minFrameMs = scenarioFrameTimes_.empty()
        ? 0.0 : scenarioFrameTimes_.front();
    result.maxFrameMs = scenarioFrameTimes_.empty()
        ? 0.0 : scenarioFrameTimes_.back();
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
    result.expectedBodyCount = expectedBodyCount_;
    result.minResidentBodies = scenarioMinResidentBodies_;
    result.maxResidentBodies = scenarioMaxResidentBodies_;
    result.minActiveBodies = scenarioActiveBodySamples_ != 0u
        ? scenarioMinActiveBodies_ : 0u;
    result.activeBodySamples = scenarioActiveBodySamples_;
    result.bodyCountInvariantPassed = expectedBodyCount_ == 0u ||
        (result.minResidentBodies == expectedBodyCount_ &&
         result.maxResidentBodies == expectedBodyCount_ &&
         result.activeBodySamples != 0u &&
         result.minActiveBodies == expectedBodyCount_);
    
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
    
    uint64_t totalFrames = 0;
    
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
        if (result.expectedBodyCount != 0u) {
            LOG_INFO("  Bodies: resident {}..{}, active min {} over {} observed samples (expected {}) [{}]",
                     result.minResidentBodies, result.maxResidentBodies,
                     result.minActiveBodies, result.activeBodySamples,
                     result.expectedBodyCount,
                     result.bodyCountInvariantPassed ? "PASS" : "INVALID");
        }
        LOG_INFO("");
        
        totalFrames += result.frameCount;
    }
    
    if (!results_.empty()) {
        const double throughputFps = overallThroughputFps();
        LOG_INFO("Overall Throughput: {:.1f} FPS ({} total frames)",
                 throughputFps, totalFrames);
        if (expectedBodyCount_ != 0u) {
            const bool workloadValid = std::all_of(
                results_.begin(), results_.end(), [](const BenchmarkResult& result) {
                    return result.bodyCountInvariantPassed;
                });
            LOG_INFO("{}-body workload invariant: {}", expectedBodyCount_,
                     workloadValid ? "PASS" : "INVALID");
        }
        if (minimumThroughputFps_ > 0.0) {
            const bool throughputPassed =
                throughputFps >= minimumThroughputFps_;
            LOG_INFO("Throughput guardrail: {} ({:.1f} measured, {:.1f} required)",
                     throughputPassed ? "PASS" : "FAIL", throughputFps,
                     minimumThroughputFps_);
        }
    }
}

} // namespace voxy::perf
