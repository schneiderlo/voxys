#pragma once

#include "gameplay/structural_assembly.hpp"
#include "gameplay/water_dynamics.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::gameplay {

struct DemolitionFragmentState {
    uint32_t rootNode = 0;
    std::vector<uint32_t> nodeIds;
    int32_t verticalOffsetQ12 = 0;
    int32_t verticalVelocityQ16 = 0;
    bool grounded = false;
};

struct DemolitionTelemetry {
    uint64_t acceptedImpacts = 0;
    uint64_t rejectedImpacts = 0;
    uint64_t fractureEvents = 0;
    uint32_t detachedBodies = 0;
    uint32_t stateHash = 0;
};

class DemolitionLeagueSlice {
public:
    struct Config {
        uint64_t roundTicks = 20u * kGameplayTickRateHz;
        int32_t significantMassQ16 = 2 * kScalarOne;
    };

    DemolitionLeagueSlice();
    explicit DemolitionLeagueSlice(Config config);

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool submitImpact(const AssemblyDamageCommand& impact);
    [[nodiscard]] bool step();

    [[nodiscard]] uint64_t currentTick() const noexcept { return currentTick_; }
    [[nodiscard]] bool roundComplete() const noexcept {
        return initialized_ && currentTick_ >= config_.roundTicks;
    }
    [[nodiscard]] int32_t scoreQ16() const noexcept { return scoreQ16_; }
    [[nodiscard]] uint32_t stateHash() const noexcept { return stateHash_; }
    [[nodiscard]] const StructuralAssembly& building() const noexcept {
        return building_;
    }
    [[nodiscard]] std::span<const DemolitionFragmentState>
    fragments() const noexcept { return fragments_; }
    [[nodiscard]] std::span<const AssemblyDamageCommand>
    replayCommands() const noexcept { return building_.commandRecording(); }
    [[nodiscard]] const DemolitionTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    void synchronizeFragments();
    void awardNewDetachments();
    void integrateFragments();
    void updateHash() noexcept;

    Config config_{};
    StructuralAssembly building_{};
    std::vector<DemolitionFragmentState> fragments_;
    std::vector<uint32_t> scoredNodes_;
    uint64_t currentTick_ = 0;
    int32_t scoreQ16_ = 0;
    uint32_t stateHash_ = 0;
    DemolitionTelemetry telemetry_{};
    bool initialized_ = false;
};

enum class TransportOutcome : uint32_t {
    Running = 0,
    Delivered = 1,
    Lost = 2,
    TimedOut = 3,
};

enum TransportInputFlag : uint32_t {
    TransportInputBrake = 1u << 0u,
};

struct DeadweightInput {
    uint64_t tick = 0;
    uint64_t sequence = 0;
    uint32_t clientId = 0;
    int32_t pullXQ16 = 0;
    int32_t pullZQ16 = 0;
    uint32_t flags = 0;

    [[nodiscard]] bool operator==(const DeadweightInput&) const = default;
};

[[nodiscard]] bool deadweightInputLess(
    const DeadweightInput& lhs, const DeadweightInput& rhs) noexcept;

struct DeadweightState {
    uint64_t tick = 0;
    std::array<int32_t, 2> cargoPositionQ12{};
    std::array<int32_t, 2> cargoVelocityQ16{};
    int32_t cargoYawQ16 = 0;
    int32_t cargoYawVelocityQ16 = 0;
    TransportOutcome outcome = TransportOutcome::Running;
    uint32_t stateHash = 0;
};

struct DeadweightTelemetry {
    uint64_t acceptedInputs = 0;
    uint64_t rejectedInputs = 0;
    uint64_t duplicateInputs = 0;
    uint64_t missingClientTicks = 0;
    uint64_t hazardTicks = 0;
    uint64_t historyEvictions = 0;
};

class DeadweightSlice {
public:
    struct Config {
        uint64_t roundTicks = 30u * kGameplayTickRateHz;
        uint32_t inputFutureWindow = 8;
        uint32_t historyTicks = 64;
        int32_t cargoMassQ16 = 8 * kScalarOne;
        int32_t winchForceQ16 = 5 * kScalarOne;
        int32_t objectiveZQ12 = 10 * kPositionOne;
        int32_t deliveryHalfWidthQ12 = 2 * kPositionOne;
        int32_t lossHalfWidthQ12 = 5 * kPositionOne;
    };

    DeadweightSlice();
    explicit DeadweightSlice(Config config);

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool submitInput(const DeadweightInput& input);
    [[nodiscard]] bool submitRedundantInputs(
        std::span<const DeadweightInput> inputs);
    [[nodiscard]] bool step();

    [[nodiscard]] const DeadweightState& state() const noexcept {
        return state_;
    }
    [[nodiscard]] std::span<const DeadweightState> history() const noexcept {
        return history_;
    }
    [[nodiscard]] std::span<const DeadweightInput>
    replayCommands() const noexcept { return recording_; }
    [[nodiscard]] const DeadweightTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    [[nodiscard]] bool validate(const DeadweightInput& input) const noexcept;
    void updateHash() noexcept;
    void storeHistory();

    Config config_{};
    std::vector<DeadweightInput> pending_;
    std::vector<DeadweightInput> recording_;
    std::vector<DeadweightState> history_;
    DeadweightState state_{};
    DeadweightTelemetry telemetry_{};
    bool initialized_ = false;
};

struct HullFragmentState {
    uint32_t rootNode = 0;
    std::vector<uint32_t> nodeIds;
    int32_t verticalOffsetQ12 = 0;
    int32_t verticalVelocityQ16 = 0;
    BuoyancyResultQ buoyancy{};
};

struct FloodedCompartment {
    uint32_t nodeId = 0;
    int32_t floodedFractionQ16 = 0;
};

struct WreckwaterTelemetry {
    uint64_t acceptedImpacts = 0;
    uint64_t rejectedImpacts = 0;
    uint64_t fractureEvents = 0;
    uint64_t floodedNodeTicks = 0;
    uint32_t significantFragments = 0;
    uint32_t tinyDebrisFragments = 0;
    uint32_t stateHash = 0;
};

class WreckwaterSlice {
public:
    struct Config {
        int32_t significantMassQ16 = 2 * kScalarOne;
        int32_t floodRatePerTickQ16 = kScalarOne / 240;
        int32_t dragCoefficientQ16 = kScalarOne / 2;
        WaterFieldConfig water{};
    };

    WreckwaterSlice();
    explicit WreckwaterSlice(Config config);

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool submitImpact(const AssemblyDamageCommand& impact);
    [[nodiscard]] bool step();

    [[nodiscard]] uint64_t currentTick() const noexcept { return currentTick_; }
    [[nodiscard]] uint32_t stateHash() const noexcept { return stateHash_; }
    [[nodiscard]] const StructuralAssembly& assembly() const noexcept {
        return assembly_;
    }
    [[nodiscard]] std::span<const HullFragmentState> fragments() const noexcept {
        return fragments_;
    }
    [[nodiscard]] std::span<const FloodedCompartment>
    flooding() const noexcept { return flooding_; }
    [[nodiscard]] std::span<const AssemblyDamageCommand>
    replayCommands() const noexcept { return assembly_.commandRecording(); }
    [[nodiscard]] const WreckwaterTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    void addBreach(uint32_t nodeId);
    void advanceFlooding();
    void synchronizeFragments();
    void integrateFragments();
    void updateHash() noexcept;
    [[nodiscard]] int32_t floodedFraction(uint32_t nodeId) const noexcept;

    Config config_{};
    DeterministicWaterField water_{};
    StructuralAssembly assembly_{};
    std::vector<HullFragmentState> fragments_;
    std::vector<FloodedCompartment> flooding_;
    uint64_t currentTick_ = 0;
    uint32_t stateHash_ = 0;
    WreckwaterTelemetry telemetry_{};
    bool initialized_ = false;
};

enum class ProductCandidate : uint32_t {
    DemolitionLeague = 0,
    Deadweight = 1,
    Wreckwater = 2,
};

struct PlaytestObservation {
    uint64_t sessionId = 0;
    ProductCandidate candidate = ProductCandidate::DemolitionLeague;
    uint32_t participants = 0;
    uint32_t durationMinutes = 0;
    uint32_t funRating = 0;
    uint32_t blockerCount = 0;
    uint32_t supportMinutes = 0;
    bool completed = false;
    bool crashed = false;
    bool humanVerified = false;
};

enum class ProductDecisionStatus : uint32_t {
    InsufficientEvidence = 0,
    OperationalFailure = 1,
    Tie = 2,
    Ready = 3,
};

struct ProductDecision {
    ProductDecisionStatus status = ProductDecisionStatus::InsufficientEvidence;
    std::optional<ProductCandidate> candidate;
    std::array<int32_t, 3> evidenceScores{};
    std::array<uint32_t, 3> qualifyingSessions{};
};

class PlaytestLedger {
public:
    struct Config {
        uint32_t minimumSessionsPerCandidate = 3;
        uint32_t minimumDurationMinutes = 10;
        uint32_t maximumCrashRatePercent = 20;
        int32_t minimumWinningMargin = 25;
    };

    PlaytestLedger();
    explicit PlaytestLedger(Config config);

    [[nodiscard]] bool record(const PlaytestObservation& observation);
    [[nodiscard]] ProductDecision decision() const noexcept;
    [[nodiscard]] std::span<const PlaytestObservation>
    observations() const noexcept { return observations_; }

private:
    Config config_{};
    std::vector<PlaytestObservation> observations_;
};

} // namespace voxy::gameplay
