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

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
    std::string physicsBackend = "unknown";
    uint32_t frameCount = 0;
    double totalTimeMs = 0.0;
    double avgFrameMs = 0.0;
    double p50FrameMs = 0.0;
    double p95FrameMs = 0.0;
    double p99FrameMs = 0.0;
    double minFrameMs = 0.0;
    double maxFrameMs = 0.0;
    double fps = 0.0;
    
    // Breakdown (if available)
    double avgUpdateMs = 0.0;
    double avgRenderMs = 0.0;
    double avgPresentMs = 0.0;
    double avgPhysicsSimulationMs = 0.0;
    double avgPhysicsWaterMs = 0.0;
    double avgPhysicsSnapshotMs = 0.0;
    double avgPrimitiveCullMs = 0.0;
    double avgPrimitivePackingMs = 0.0;
    double avgPrimitiveUploadMs = 0.0;
    double avgPrimitiveRenderMs = 0.0;
    uint32_t expectedBodyCount = 0;
    uint32_t minResidentBodies = 0;
    uint32_t maxResidentBodies = 0;
    uint32_t minActiveBodies = 0;
    uint32_t activeBodySamples = 0;
    bool bodyCountInvariantPassed = true;
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

    /// Attach the selected physics backend to every result record.
    void setPhysicsBackend(std::string backend);

    /// Require an exact resident and active body count throughout each scenario.
    void setExpectedBodyCount(uint32_t bodyCount) noexcept;

    /// Require aggregate measured frame throughput at or above this value.
    void setMinimumThroughputFps(double minimumFps) noexcept;
    
    /// Start running benchmark scenarios
    /// @param scenarios List of scenarios to run (empty = use defaults)
    void start(const std::vector<BenchmarkScenario>& scenarios = {});
    
    /// Stop benchmarking
    void stop();
    
    /// Check if benchmarking is active
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    /// True when the frame currently being rendered closes its scenario.
    /// The application uses this to drain bounded GPU work at clean scenario
    /// boundaries before recording the frame's elapsed time.
    [[nodiscard]] bool willCompleteScenarioAfterCurrentFrame() const noexcept;
    
    /// Called each frame during benchmarking
    /// @param frameStats Frame timing statistics from FrameTimer
    /// @return true if benchmark continues, false if complete
    bool onFrame(const FrameStats& frameStats);
    
    /// Get results from completed benchmark
    [[nodiscard]] const std::vector<BenchmarkResult>& getResults() const noexcept { return results_; }

    /// Aggregate measured frames divided by their total measured frame time.
    [[nodiscard]] double overallThroughputFps() const noexcept;

    /// True only after a complete run satisfies every configured guardrail.
    [[nodiscard]] bool passed() const noexcept;
    
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
    std::string physicsBackend_ = "unknown";
    uint32_t expectedBodyCount_ = 0;
    double minimumThroughputFps_ = 0.0;
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
    double scenarioSumPhysicsSimulation_ = 0.0;
    double scenarioSumPhysicsWater_ = 0.0;
    double scenarioSumPhysicsSnapshot_ = 0.0;
    double scenarioSumPrimitiveCull_ = 0.0;
    double scenarioSumPrimitivePacking_ = 0.0;
    double scenarioSumPrimitiveUpload_ = 0.0;
    double scenarioSumPrimitiveRender_ = 0.0;
    uint32_t scenarioMinResidentBodies_ = 0;
    uint32_t scenarioMaxResidentBodies_ = 0;
    uint32_t scenarioMinActiveBodies_ = 0;
    uint32_t scenarioActiveBodySamples_ = 0;
    std::vector<double> scenarioFrameTimes_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Production Browser Journey Benchmark
// ─────────────────────────────────────────────────────────────────────────────

/// State values are part of the WASM automation ABI. Do not renumber them.
enum class BrowserJourneyStatus : int32_t {
    Failed = -1,
    Idle = 0,
    Warming = 1,
    Throwing = 2,
    Impact = 3,
    Settling = 4,
    Complete = 5,
};

[[nodiscard]] const char*
browserJourneyStatusName(BrowserJourneyStatus status) noexcept;

enum class BrowserJourneyLayout : uint32_t {
    FixedPile = 0,
    TerrainSweep = 1,
};

[[nodiscard]] const char*
browserJourneyLayoutName(BrowserJourneyLayout layout) noexcept;

/// Fixed-tick production workload with deterministic volley pacing.
struct BrowserJourneyConfig {
    uint32_t targetBodies = 0;
    uint32_t warmupTicks = 120;
    uint32_t impactTicks = 300;
    uint32_t settleTicks = 180;
    uint32_t bodiesPerVolley = 128;
    uint32_t ticksPerVolley = 4;
    BrowserJourneyLayout layout = BrowserJourneyLayout::FixedPile;
};

/// One submitted browser frame. Wall time includes missed RAF opportunities;
/// CPU time is command generation and submission, not asynchronous GPU work.
struct BrowserJourneyFrameSample {
    uint64_t frame = 0;
    uint64_t physicsTick = 0;
    BrowserJourneyStatus phase = BrowserJourneyStatus::Idle;
    float wallMilliseconds = 0.0f;
    float cpuMilliseconds = 0.0f;
    uint32_t residentBodies = 0;
    uint32_t activeBodies = 0;
    uint32_t candidatePairs = 0;
    uint32_t contacts = 0;
    uint32_t terrainContactBodies = 0;
    uint32_t submittedPrimitives = 0;
    uint32_t capacityOverflowMask = 0;
    uint32_t physicsErrorMask = 0;
};

/// Drives and records the real-browser throwing journey without allocating in
/// the measured frame loop. Application supplies the actual throwing callback.
class BrowserJourneyBenchmark {
public:
    static constexpr size_t kSampleCapacity = 65'536u;
    static constexpr uint32_t kMaximumBodiesPerVolley = 128u;

    /// Reset and arm a new journey.
    [[nodiscard]] bool start(const BrowserJourneyConfig& config,
                             uint32_t baselineBodies,
                             uint64_t startFrame,
                             uint64_t startPhysicsTick) noexcept;

    /// Advance phase timing and return the exact body count for this tick's
    /// real throwing volley. Returns zero when no volley is due.
    [[nodiscard]] uint32_t advance(uint64_t physicsTick,
                                   uint32_t residentBodies) noexcept;

    /// Complete the pending volley. Partial spawns fail the workload rather
    /// than silently changing its collision density.
    [[nodiscard]] bool reportVolley(uint64_t physicsTick,
                                    uint32_t requestedBodies,
                                    uint32_t spawnedBodies) noexcept;

    /// Record one submitted application frame after presentation.
    void recordFrame(const BrowserJourneyFrameSample& sample) noexcept;

    [[nodiscard]] BrowserJourneyStatus status() const noexcept {
        return status_;
    }
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool passed() const noexcept;
    [[nodiscard]] uint32_t spawnedBodies() const noexcept {
        return spawnedBodies_;
    }
    [[nodiscard]] uint32_t volleyCount() const noexcept {
        return volleyCount_;
    }
    [[nodiscard]] size_t sampleCount() const noexcept {
        return sampleCount_;
    }
    [[nodiscard]] BrowserJourneyLayout layout() const noexcept {
        return config_.layout;
    }
    [[nodiscard]] const char* failureReason() const noexcept;

    /// Full summary plus compact raw samples, generated only when requested.
    [[nodiscard]] std::string resultJson() const;

private:
    enum class Failure : uint8_t {
        NoFailure,
        InvalidConfiguration,
        ClockRegression,
        PendingVolley,
        SpawnFailure,
        BodyCountMismatch,
        InvalidFrameSample,
        SampleCapacity,
        NoFrameSamples,
    };

    void fail(Failure failure) noexcept;
    void transition(BrowserJourneyStatus status,
                    uint64_t physicsTick) noexcept;

    BrowserJourneyConfig config_{};
    BrowserJourneyStatus status_ = BrowserJourneyStatus::Idle;
    BrowserJourneyStatus framePhase_ = BrowserJourneyStatus::Idle;
    Failure failure_ = Failure::NoFailure;
    uint32_t baselineBodies_ = 0;
    uint32_t spawnedBodies_ = 0;
    uint32_t volleyCount_ = 0;
    uint32_t pendingVolleyBodies_ = 0;
    uint32_t finalResidentBodies_ = 0;
    uint32_t finalActiveBodies_ = 0;
    uint64_t startFrame_ = 0;
    uint64_t startPhysicsTick_ = 0;
    uint64_t phaseStartTick_ = 0;
    uint64_t lastObservedTick_ = 0;
    uint64_t pendingVolleyTick_ = 0;
    uint64_t lastVolleyTick_ = 0;
    bool hasVolleyTick_ = false;
    std::array<BrowserJourneyFrameSample, kSampleCapacity> samples_{};
    size_t sampleCount_ = 0;
};

} // namespace voxy::perf
