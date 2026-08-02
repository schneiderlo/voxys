// RIDGEBREAK fixed-tick race and freeride rules.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace voxy::moto {

inline constexpr uint32_t kMaximumRacePlayers = 12u;
inline constexpr uint32_t kMaximumRaceCheckpoints = 64u;
inline constexpr uint16_t kInvalidRacePlayer = 0xffffu;

enum class RaceMode : uint8_t {
    Circuit = 0,
    Freeride = 1,
};

enum class RacePhase : uint8_t {
    Lobby = 0,
    Countdown = 1,
    Running = 2,
    Finished = 3,
};

enum class MotoRaceControlAction : uint8_t {
    None = 0,
    StartCircuit = 1,
    ResetPractice = 2,
};

/// Pure application-facing race policy. It keeps keyboard priority, grid
/// ownership, and HUD transitions testable without constructing a window or
/// GPU device.
struct MotoRaceApplicationPolicy {
    MotoRaceControlAction action = MotoRaceControlAction::None;
    RacePhase hudPhase = RacePhase::Lobby;
    uint32_t hudCountdownTicks = 0u;
    bool gridOwned = false;
    bool hudRaceActive = false;
};

struct RaceCheckpoint {
    glm::vec3 center{0.0f};
    // Horizontal race direction through the gate. A checkpoint is collected
    // only when a rider crosses the center plane from back to front.
    glm::vec3 forward{1.0f, 0.0f, 0.0f};
    float halfWidth = 14.0f;
    float halfHeight = 8.0f;
};

struct CircuitGridPose {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

enum class RaceTrick : uint8_t {
    Whip = 0,
    NoHander = 1,
    NacNac = 2,
    Superman = 3,
    Backflip = 4,
    Frontflip = 5,
    DoubleBackflip = 6,
    BarrelRoll = 7,
};

struct RaceTrickEvent {
    // Authority-minted, per-rider sequence. The first event is 1 and every
    // later event must be exactly previous + 1. This makes retry/replay
    // handling deterministic without an unbounded event-ID set.
    uint64_t sequence = 0u;
    // Authority-minted physical landing identity. New landings increase by
    // exactly one; all tricks from one landing share the same identity/tick.
    uint64_t landingIdentity = 0u;
    uint64_t authorityTick = 0u;
    RaceTrick trick = RaceTrick::Whip;
};

struct RaceConfig {
    RaceMode mode = RaceMode::Circuit;
    uint32_t tickRate = 60u;
    uint32_t countdownTicks = 180u;
    // Circuit budget includes real margin above the measured six-minute ideal
    // lap. Zero is supported only for untimed freeride practice.
    uint32_t durationTicks = 23'400u; // Six minutes thirty seconds at 60 Hz.
    uint32_t lapCount = 1u;
    uint32_t maximumPlayers = kMaximumRacePlayers;
    // Authoritative poses moving farther than this in one tick are treated as
    // discontinuities and cannot collect a checkpoint during that tick.
    float maximumTravelPerTick = 12.0f;
    uint32_t maximumTrickEventsPerFrame = 8u;
    // A scored jump needs airtime plus separation from the previous landing.
    // Thirty ticks is a conservative authority-side anti-spam floor.
    uint32_t minimumTrickLandingIntervalTicks = 30u;
};

[[nodiscard]] RaceConfig makeUntimedPracticeRaceConfig() noexcept;

/// True only while a circuit start owns the bike on the grid. Freeride and
/// every other race phase leave rider input untouched.
[[nodiscard]] bool isCircuitGridLocked(
    RaceMode mode, RacePhase phase) noexcept;

[[nodiscard]] MotoRaceApplicationPolicy evaluateMotoRaceApplicationPolicy(
    bool circuitPressed, bool resetPressed, RaceMode mode,
    RacePhase phase, uint32_t countdownTicksRemaining) noexcept;

struct RaceRiderFrame {
    uint16_t player = kInvalidRacePlayer;
    glm::vec3 position{0.0f};
    std::span<const RaceTrickEvent> trickEvents{};
    // Respawn/reconciliation teleports reset the checkpoint sweep origin.
    bool discontinuity = false;
};

struct RaceRiderState {
    uint16_t player = kInvalidRacePlayer;
    glm::vec3 position{0.0f};
    glm::vec3 previousPosition{0.0f};
    uint64_t score = 0u;
    uint64_t finishTick = 0u;
    uint32_t nextCheckpoint = 0u;
    uint32_t completedLaps = 0u;
    uint32_t finishPlace = 0u;
    uint32_t rejectedFrames = 0u;
    uint32_t rejectedTrickEvents = 0u;
    uint64_t lastTrickSequence = 0u;
    uint64_t lastLandingIdentity = 0u;
    uint64_t lastLandingTick = 0u;
    uint32_t acceptedLandingTrickMask = 0u;
    bool joined = false;
    bool positionValid = false;
    bool finished = false;
    bool didNotFinish = false;
};

/// Convert a closed or implicitly closed XZ route into canonical directed
/// gates. Duplicate closure points are removed. Terrain height may be filled
/// into each center by the caller before configuring RaceSession.
[[nodiscard]] bool buildDirectedRaceCheckpoints(
    std::span<const glm::vec2> route,
    float halfWidth,
    float halfHeight,
    std::vector<RaceCheckpoint>* checkpoints,
    std::string* error = nullptr);

[[nodiscard]] bool buildCircuitGridPose(
    std::span<const RaceCheckpoint> checkpoints,
    float distanceBehindGate,
    CircuitGridPose* pose,
    std::string* error = nullptr);

class RaceSession {
public:
    [[nodiscard]] bool configure(
        const RaceConfig& config,
        std::span<const RaceCheckpoint> checkpoints,
        std::string* error = nullptr);

    void reset() noexcept;
    [[nodiscard]] bool join(uint16_t player) noexcept;
    [[nodiscard]] bool leave(uint16_t player) noexcept;
    [[nodiscard]] bool start() noexcept;
    void step(std::span<const RaceRiderFrame> frames) noexcept;

    [[nodiscard]] RacePhase phase() const noexcept { return phase_; }
    [[nodiscard]] uint64_t tick() const noexcept { return tick_; }
    [[nodiscard]] uint64_t runningTick() const noexcept;
    [[nodiscard]] uint32_t countdownTicksRemaining() const noexcept;
    [[nodiscard]] const RaceConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::span<const RaceCheckpoint> checkpoints() const noexcept {
        return checkpoints_;
    }
    [[nodiscard]] const RaceRiderState* rider(uint16_t player) const noexcept;
    [[nodiscard]] std::vector<uint16_t> standings() const;

private:
    [[nodiscard]] RaceRiderState* mutableRider(uint16_t player) noexcept;
    void acceptFrame(RaceRiderState& rider,
                     const RaceRiderFrame& frame) noexcept;
    void finishExpiredSession() noexcept;

    RaceConfig config_{};
    std::vector<RaceCheckpoint> checkpoints_;
    std::array<RaceRiderState, kMaximumRacePlayers> riders_{};
    RacePhase phase_ = RacePhase::Lobby;
    uint64_t tick_ = 0u;
    uint64_t runningStartTick_ = 0u;
    uint32_t finishers_ = 0u;
    bool configured_ = false;
};

} // namespace voxy::moto
