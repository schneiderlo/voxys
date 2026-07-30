// ═══════════════════════════════════════════════════════════════════════════════
// benchmark.cpp - Benchmark Mode Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "perf/benchmark.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
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

namespace {

constexpr uint32_t kMaximumBrowserJourneyBodies = 1'000'000u;
constexpr uint32_t kMaximumBrowserJourneyPhaseTicks = 36'000u;
constexpr uint32_t kMaximumBrowserJourneyVolleyIntervalTicks = 3'600u;
constexpr float kMaximumBrowserJourneyFrameMilliseconds = 60'000.0f;

struct BrowserJourneyDistribution {
    size_t count = 0;
    double total = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double lowOnePercentFps = 0.0;
    uint64_t missed60HzDeadlines = 0;
    uint64_t missed90HzDeadlines = 0;
    uint64_t missed120HzDeadlines = 0;
    uint64_t framesOver33Milliseconds = 0;
};

uint64_t missedDeadlines(double frameMilliseconds,
                         double refreshMilliseconds) noexcept {
    // RAF intervals cluster around integer refresh periods. Assign the sample
    // to the nearest period so a quantized 16.7 ms interval is one 60 Hz frame
    // and two 90/120 Hz periods, while 33.3 ms is two 60 Hz periods.
    const auto intervals = static_cast<uint64_t>(
        std::floor(frameMilliseconds / refreshMilliseconds + 0.5));
    return intervals > 1u ? intervals - 1u : 0u;
}

template <typename ReadValue>
BrowserJourneyDistribution browserJourneyDistribution(
    const std::array<voxy::perf::BrowserJourneyFrameSample,
                     voxy::perf::BrowserJourneyBenchmark::kSampleCapacity>&
        samples,
    size_t sampleCount,
    bool filterPhase,
    voxy::perf::BrowserJourneyStatus phase,
    ReadValue readValue,
    bool includeFrameDeadlines) {
    std::vector<double> values;
    values.reserve(sampleCount);
    for (size_t index = 0; index < sampleCount; ++index) {
        if (filterPhase && samples[index].phase != phase) continue;
        values.push_back(readValue(samples[index]));
    }

    BrowserJourneyDistribution result;
    result.count = values.size();
    if (values.empty()) return result;

    for (const double value : values) {
        result.total += value;
        if (includeFrameDeadlines) {
            result.missed60HzDeadlines += missedDeadlines(
                value, 1000.0 / 60.0);
            result.missed90HzDeadlines += missedDeadlines(
                value, 1000.0 / 90.0);
            result.missed120HzDeadlines += missedDeadlines(
                value, 1000.0 / 120.0);
            result.framesOver33Milliseconds += value > (1000.0 / 30.0)
                ? 1u : 0u;
        }
    }
    std::sort(values.begin(), values.end());
    result.minimum = values.front();
    result.maximum = values.back();
    result.mean = result.total / static_cast<double>(values.size());
    result.p50 = observedPercentile(values, 0.50);
    result.p95 = observedPercentile(values, 0.95);
    result.p99 = observedPercentile(values, 0.99);

    const size_t slowCount = std::max<size_t>(
        1u, static_cast<size_t>(std::ceil(
            static_cast<double>(values.size()) * 0.01)));
    double slowTotal = 0.0;
    for (size_t index = values.size() - slowCount;
         index < values.size(); ++index) {
        slowTotal += values[index];
    }
    const double slowMean = slowTotal / static_cast<double>(slowCount);
    result.lowOnePercentFps = slowMean > 0.0 ? 1000.0 / slowMean : 0.0;
    return result;
}

void appendBrowserJourneyDistribution(
    std::ostream& out,
    const BrowserJourneyDistribution& distribution,
    bool includeFrameDeadlines) {
    out << "{\"count\":" << distribution.count
        << ",\"total_ms\":" << distribution.total
        << ",\"mean_ms\":" << distribution.mean
        << ",\"min_ms\":" << distribution.minimum
        << ",\"p50_ms\":" << distribution.p50
        << ",\"p95_ms\":" << distribution.p95
        << ",\"p99_ms\":" << distribution.p99
        << ",\"max_ms\":" << distribution.maximum;
    if (includeFrameDeadlines) {
        const double fps = distribution.total > 0.0
            ? static_cast<double>(distribution.count) * 1000.0
                / distribution.total
            : 0.0;
        out << ",\"fps\":" << fps
            << ",\"low_1_percent_fps\":"
            << distribution.lowOnePercentFps
            << ",\"missed_deadlines_60hz\":"
            << distribution.missed60HzDeadlines
            << ",\"missed_deadlines_90hz\":"
            << distribution.missed90HzDeadlines
            << ",\"missed_deadlines_120hz\":"
            << distribution.missed120HzDeadlines
            << ",\"frames_over_33_333_ms\":"
            << distribution.framesOver33Milliseconds;
    }
    out << '}';
}

} // namespace

namespace voxy::perf {

const char* browserJourneyStatusName(BrowserJourneyStatus status) noexcept {
    switch (status) {
        case BrowserJourneyStatus::Failed: return "failed";
        case BrowserJourneyStatus::Idle: return "idle";
        case BrowserJourneyStatus::Warming: return "warming";
        case BrowserJourneyStatus::Throwing: return "throwing";
        case BrowserJourneyStatus::Impact: return "impact";
        case BrowserJourneyStatus::Settling: return "settling";
        case BrowserJourneyStatus::Complete: return "complete";
    }
    return "unknown";
}

const char* browserJourneyLayoutName(BrowserJourneyLayout layout) noexcept {
    switch (layout) {
        case BrowserJourneyLayout::FixedPile: return "pile";
        case BrowserJourneyLayout::TerrainSweep: return "sweep";
        case BrowserJourneyLayout::CubePyramid: return "triangle";
    }
    return "unknown";
}

const char* browserJourneyShapeName(BrowserJourneyShape shape) noexcept {
    switch (shape) {
        case BrowserJourneyShape::Sphere: return "sphere";
        case BrowserJourneyShape::Cube: return "cube";
        case BrowserJourneyShape::Box: return "box";
        case BrowserJourneyShape::Capsule: return "capsule";
        case BrowserJourneyShape::Cylinder: return "cylinder";
        case BrowserJourneyShape::Mixed: return "mixed";
    }
    return "unknown";
}

bool BrowserJourneyBenchmark::start(const BrowserJourneyConfig& config,
                                    uint32_t baselineBodies,
                                    uint64_t startFrame,
                                    uint64_t startPhysicsTick) noexcept {
    config_ = config;
    status_ = BrowserJourneyStatus::Idle;
    framePhase_ = BrowserJourneyStatus::Idle;
    failure_ = Failure::NoFailure;
    baselineBodies_ = baselineBodies;
    spawnedBodies_ = 0u;
    volleyCount_ = 0u;
    pendingVolleyBodies_ = 0u;
    finalResidentBodies_ = baselineBodies;
    finalActiveBodies_ = 0u;
    startFrame_ = startFrame;
    startPhysicsTick_ = startPhysicsTick;
    phaseStartTick_ = startPhysicsTick;
    lastObservedTick_ = startPhysicsTick;
    pendingVolleyTick_ = 0u;
    lastVolleyTick_ = 0u;
    hasVolleyTick_ = false;
    sampleCount_ = 0u;

    const uint64_t finalBodyCount = uint64_t{baselineBodies}
                                  + config.targetBodies;
    const uint64_t volleySize = std::max(config.bodiesPerVolley, 1u);
    const uint64_t volleyCount =
        (uint64_t{config.targetBodies} + volleySize - 1u) / volleySize;
    const uint64_t throwingTicks = volleyCount > 0u
        ? (volleyCount - 1u) * config.ticksPerVolley : 0u;
    const uint64_t measuredTicks = uint64_t{config.impactTicks}
                                 + config.settleTicks
                                 + throwingTicks;
    const bool observesCubePyramid =
        config.layout == BrowserJourneyLayout::CubePyramid
        && config.targetBodies == 0u
        && config.shape == BrowserJourneyShape::Cube;
    const bool throwsBodies =
        config.targetBodies != 0u;
    if ((!observesCubePyramid && !throwsBodies)
        || config.targetBodies > kMaximumBrowserJourneyBodies
        || config.bodiesPerVolley == 0u
        || config.bodiesPerVolley > kMaximumBodiesPerVolley
        || config.ticksPerVolley == 0u
        || config.ticksPerVolley
            > kMaximumBrowserJourneyVolleyIntervalTicks
        || config.warmupTicks > kMaximumBrowserJourneyPhaseTicks
        || config.impactTicks == 0u
        || config.impactTicks > kMaximumBrowserJourneyPhaseTicks
        || config.settleTicks == 0u
        || config.settleTicks > kMaximumBrowserJourneyPhaseTicks
        || (config.layout != BrowserJourneyLayout::FixedPile
            && config.layout != BrowserJourneyLayout::TerrainSweep
            && config.layout != BrowserJourneyLayout::CubePyramid)
        || config.shape > BrowserJourneyShape::Mixed
        || finalBodyCount > std::numeric_limits<uint32_t>::max()
        || measuredTicks > kMaximumBrowserJourneyPhaseTicks) {
        fail(Failure::InvalidConfiguration);
        return false;
    }

    transition(BrowserJourneyStatus::Warming, startPhysicsTick);
    return true;
}

uint32_t BrowserJourneyBenchmark::advance(
    uint64_t physicsTick, uint32_t residentBodies) noexcept {
    if (!isRunning()) return 0u;
    if (physicsTick < lastObservedTick_) {
        fail(Failure::ClockRegression);
        return 0u;
    }
    if (pendingVolleyBodies_ != 0u) {
        fail(Failure::PendingVolley);
        return 0u;
    }
    lastObservedTick_ = physicsTick;
    framePhase_ = status_;

    for (;;) {
        switch (status_) {
            case BrowserJourneyStatus::Warming:
                if (physicsTick - phaseStartTick_ < config_.warmupTicks) {
                    return 0u;
                }
                transition(BrowserJourneyStatus::Throwing, physicsTick);
                framePhase_ = BrowserJourneyStatus::Throwing;
                continue;

            case BrowserJourneyStatus::Throwing: {
                if (spawnedBodies_ >= config_.targetBodies) {
                    transition(BrowserJourneyStatus::Impact, physicsTick);
                    framePhase_ = BrowserJourneyStatus::Impact;
                    continue;
                }
                if (hasVolleyTick_
                    && physicsTick - lastVolleyTick_
                        < config_.ticksPerVolley) {
                    return 0u;
                }
                const uint32_t requested = std::min(
                    config_.bodiesPerVolley,
                    config_.targetBodies - spawnedBodies_);
                pendingVolleyBodies_ = requested;
                pendingVolleyTick_ = physicsTick;
                lastVolleyTick_ = physicsTick;
                hasVolleyTick_ = true;
                return requested;
            }

            case BrowserJourneyStatus::Impact:
                if (physicsTick - phaseStartTick_ < config_.impactTicks) {
                    return 0u;
                }
                transition(BrowserJourneyStatus::Settling, physicsTick);
                framePhase_ = BrowserJourneyStatus::Settling;
                continue;

            case BrowserJourneyStatus::Settling:
                if (physicsTick - phaseStartTick_ < config_.settleTicks) {
                    return 0u;
                }
                finalResidentBodies_ = residentBodies;
                if (sampleCount_ == 0u) {
                    fail(Failure::NoFrameSamples);
                } else if (residentBodies
                           != baselineBodies_ + config_.targetBodies) {
                    fail(Failure::BodyCountMismatch);
                } else {
                    transition(BrowserJourneyStatus::Complete, physicsTick);
                }
                return 0u;

            case BrowserJourneyStatus::Failed:
            case BrowserJourneyStatus::Idle:
            case BrowserJourneyStatus::Complete:
                return 0u;
        }
    }
}

bool BrowserJourneyBenchmark::reportVolley(
    uint64_t physicsTick, uint32_t requestedBodies,
    uint32_t spawnedBodies) noexcept {
    if (status_ != BrowserJourneyStatus::Throwing
        || pendingVolleyBodies_ == 0u
        || physicsTick != pendingVolleyTick_
        || requestedBodies != pendingVolleyBodies_) {
        fail(Failure::PendingVolley);
        return false;
    }
    pendingVolleyBodies_ = 0u;
    if (spawnedBodies != requestedBodies) {
        fail(Failure::SpawnFailure);
        return false;
    }

    spawnedBodies_ += spawnedBodies;
    ++volleyCount_;
    if (spawnedBodies_ == config_.targetBodies) {
        // Keep framePhase_ as Throwing so the frame that creates the final
        // volley is attributed to the spawn path, not the following impacts.
        transition(BrowserJourneyStatus::Impact, physicsTick);
    }
    return true;
}

void BrowserJourneyBenchmark::recordFrame(
    const BrowserJourneyFrameSample& sample) noexcept {
    if (framePhase_ != BrowserJourneyStatus::Throwing
        && framePhase_ != BrowserJourneyStatus::Impact
        && framePhase_ != BrowserJourneyStatus::Settling) {
        return;
    }
    if (!std::isfinite(sample.wallMilliseconds)
        || !std::isfinite(sample.cpuMilliseconds)
        || sample.wallMilliseconds < 0.0f
        || sample.cpuMilliseconds < 0.0f
        || sample.wallMilliseconds > kMaximumBrowserJourneyFrameMilliseconds
        || sample.cpuMilliseconds > kMaximumBrowserJourneyFrameMilliseconds
        || (sampleCount_ != 0u
            && (sample.frame <= samples_[sampleCount_ - 1u].frame
                || sample.physicsTick
                    < samples_[sampleCount_ - 1u].physicsTick))) {
        fail(Failure::InvalidFrameSample);
        return;
    }
    if (sampleCount_ == kSampleCapacity) {
        fail(Failure::SampleCapacity);
        return;
    }

    BrowserJourneyFrameSample stored = sample;
    stored.phase = framePhase_;
    samples_[sampleCount_++] = stored;
    finalResidentBodies_ = stored.residentBodies;
    finalActiveBodies_ = stored.activeBodies;
    if (status_ == BrowserJourneyStatus::Complete) {
        framePhase_ = BrowserJourneyStatus::Idle;
    }
}

bool BrowserJourneyBenchmark::isRunning() const noexcept {
    return status_ == BrowserJourneyStatus::Warming
        || status_ == BrowserJourneyStatus::Throwing
        || status_ == BrowserJourneyStatus::Impact
        || status_ == BrowserJourneyStatus::Settling;
}

bool BrowserJourneyBenchmark::passed() const noexcept {
    return status_ == BrowserJourneyStatus::Complete
        && failure_ == Failure::NoFailure
        && spawnedBodies_ == config_.targetBodies
        && finalResidentBodies_
            == baselineBodies_ + config_.targetBodies
        && sampleCount_ != 0u;
}

const char* BrowserJourneyBenchmark::failureReason() const noexcept {
    switch (failure_) {
        case Failure::NoFailure: return "";
        case Failure::InvalidConfiguration: return "invalid_configuration";
        case Failure::ClockRegression: return "physics_clock_regression";
        case Failure::PendingVolley: return "pending_volley_protocol";
        case Failure::SpawnFailure: return "partial_spawn";
        case Failure::BodyCountMismatch: return "body_count_mismatch";
        case Failure::InvalidFrameSample: return "invalid_frame_sample";
        case Failure::SampleCapacity: return "frame_sample_capacity";
        case Failure::NoFrameSamples: return "no_frame_samples";
    }
    return "unknown";
}

void BrowserJourneyBenchmark::fail(Failure failure) noexcept {
    if (failure_ == Failure::NoFailure) failure_ = failure;
    status_ = BrowserJourneyStatus::Failed;
    framePhase_ = BrowserJourneyStatus::Idle;
}

void BrowserJourneyBenchmark::transition(
    BrowserJourneyStatus status, uint64_t physicsTick) noexcept {
    status_ = status;
    phaseStartTick_ = physicsTick;
}

std::string BrowserJourneyBenchmark::resultJson() const {
    const auto wall = [&](bool filter, BrowserJourneyStatus phase) {
        return browserJourneyDistribution(
            samples_, sampleCount_, filter, phase,
            [](const BrowserJourneyFrameSample& sample) {
                return static_cast<double>(sample.wallMilliseconds);
            },
            true);
    };
    const auto cpu = [&](bool filter, BrowserJourneyStatus phase) {
        return browserJourneyDistribution(
            samples_, sampleCount_, filter, phase,
            [](const BrowserJourneyFrameSample& sample) {
                return static_cast<double>(sample.cpuMilliseconds);
            },
            false);
    };
    BrowserJourneyFrameSample peaks;
    for (size_t index = 0; index < sampleCount_; ++index) {
        const BrowserJourneyFrameSample& sample = samples_[index];
        peaks.residentBodies = std::max(
            peaks.residentBodies, sample.residentBodies);
        peaks.activeBodies = std::max(
            peaks.activeBodies, sample.activeBodies);
        peaks.candidatePairs = std::max(
            peaks.candidatePairs, sample.candidatePairs);
        peaks.contacts = std::max(peaks.contacts, sample.contacts);
        peaks.terrainContactBodies = std::max(
            peaks.terrainContactBodies, sample.terrainContactBodies);
        peaks.submittedPrimitives = std::max(
            peaks.submittedPrimitives, sample.submittedPrimitives);
        peaks.capacityOverflowMask |= sample.capacityOverflowMask;
        peaks.physicsErrorMask |= sample.physicsErrorMask;
    }

    std::ostringstream out;
    out << std::setprecision(10);
    out << "{\"schema\":\"voxys.browser_journey.v1\""
        << ",\"status\":\"" << browserJourneyStatusName(status_) << '"'
        << ",\"status_code\":" << static_cast<int32_t>(status_)
        << ",\"failure\":";
    if (failure_ == Failure::NoFailure) {
        out << "null";
    } else {
        out << '"' << failureReason() << '"';
    }
    out << ",\"passed\":" << (passed() ? "true" : "false")
        << ",\"config\":{\"target_bodies\":" << config_.targetBodies
        << ",\"warmup_ticks\":" << config_.warmupTicks
        << ",\"impact_ticks\":" << config_.impactTicks
        << ",\"settle_ticks\":" << config_.settleTicks
        << ",\"bodies_per_volley\":" << config_.bodiesPerVolley
        << ",\"ticks_per_volley\":" << config_.ticksPerVolley
        << ",\"layout\":\""
        << browserJourneyLayoutName(config_.layout) << '"'
        << ",\"shape\":\""
        << browserJourneyShapeName(config_.shape) << "\"}"
        << ",\"counts\":{\"baseline_bodies\":" << baselineBodies_
        << ",\"spawned_bodies\":" << spawnedBodies_
        << ",\"expected_final_bodies\":"
        << baselineBodies_ + config_.targetBodies
        << ",\"final_resident_bodies\":" << finalResidentBodies_
        << ",\"final_active_bodies\":" << finalActiveBodies_
        << ",\"volleys\":" << volleyCount_ << '}'
        << ",\"peaks\":{\"resident_bodies\":" << peaks.residentBodies
        << ",\"active_bodies\":" << peaks.activeBodies
        << ",\"candidate_pairs\":" << peaks.candidatePairs
        << ",\"contacts\":" << peaks.contacts
        << ",\"terrain_contact_bodies\":"
        << peaks.terrainContactBodies
        << ",\"submitted_primitives\":"
        << peaks.submittedPrimitives
        << ",\"capacity_overflow_mask\":"
        << peaks.capacityOverflowMask
        << ",\"physics_error_mask\":"
        << peaks.physicsErrorMask << '}'
        << ",\"clock\":{\"start_frame\":" << startFrame_
        << ",\"start_physics_tick\":" << startPhysicsTick_
        << ",\"final_physics_tick\":" << lastObservedTick_ << '}'
        << ",\"frame\":";
    appendBrowserJourneyDistribution(out, wall(false, {}), true);
    out << ",\"cpu\":";
    appendBrowserJourneyDistribution(out, cpu(false, {}), false);
    out << ",\"phases\":{";
    constexpr std::array phases{
        BrowserJourneyStatus::Throwing,
        BrowserJourneyStatus::Impact,
        BrowserJourneyStatus::Settling,
    };
    for (size_t index = 0; index < phases.size(); ++index) {
        if (index != 0u) out << ',';
        out << '"' << browserJourneyStatusName(phases[index]) << "\":{"
            << "\"frame\":";
        appendBrowserJourneyDistribution(
            out, wall(true, phases[index]), true);
        out << ",\"cpu\":";
        appendBrowserJourneyDistribution(
            out, cpu(true, phases[index]), false);
        out << '}';
    }
    out << "},\"sample_fields\":[\"frame\",\"physics_tick\",\"phase\","
           "\"wall_ms\",\"cpu_ms\",\"resident_bodies\",\"active_bodies\","
           "\"candidate_pairs\",\"contacts\",\"terrain_contact_bodies\","
           "\"submitted_primitives\",\"capacity_overflow_mask\","
           "\"physics_error_mask\"]"
        << ",\"samples\":[";
    for (size_t index = 0; index < sampleCount_; ++index) {
        if (index != 0u) out << ',';
        const BrowserJourneyFrameSample& sample = samples_[index];
        out << '[' << sample.frame
            << ',' << sample.physicsTick
            << ',' << static_cast<int32_t>(sample.phase)
            << ',' << sample.wallMilliseconds
            << ',' << sample.cpuMilliseconds
            << ',' << sample.residentBodies
            << ',' << sample.activeBodies
            << ',' << sample.candidatePairs
            << ',' << sample.contacts
            << ',' << sample.terrainContactBodies
            << ',' << sample.submittedPrimitives
            << ',' << sample.capacityOverflowMask
            << ',' << sample.physicsErrorMask << ']';
    }
    out << "]}";
    return out.str();
}

} // namespace voxy::perf
