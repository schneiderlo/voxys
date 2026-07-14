#pragma once

#include "physics/deterministic/lockstep_types.hpp"
#include "physics/deterministic/lockstep_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace voxy::physics::deterministic {

enum class ArithmeticMode : uint32_t {
    DeterministicFloat = 1,
    Lockstep = 2,
};

enum class ReplayBackend : uint32_t {
    Box3DReference = 1,
    WebGpu = 2,
    NativeServer = 3,
    CpuLockstepReference = 4,
};

struct ReplayCapacityProfile {
    uint32_t residentBodies = 0;
    uint32_t activeBodies = 0;
    uint32_t commandsPerTick = 0;
    uint32_t gridEntries = 0;
    uint32_t candidatePairs = 0;
    uint32_t uniquePairs = 0;
    uint32_t contacts = 0;
    uint32_t manifolds = 0;
    uint32_t terrainContacts = 0;
    uint32_t overflowConstraints = 0;
    uint32_t events = 0;
    uint32_t visibleBodies = 0;
};

struct ReplaySimulationConfig {
    uint32_t tickRateHz = kLockstepDefaultTickRateHz;
    uint32_t substeps = kLockstepDefaultSubsteps;
    uint32_t solverIterations = 4;
    int32_t gravityPerSubstepQ16 =
        kLockstepDefaultGravityPerSubstepQ16;
};

struct ReplayHeader {
    uint32_t schemaVersion = kLockstepSchemaVersion;
    ArithmeticMode arithmeticMode = ArithmeticMode::Lockstep;
    ReplayBackend backend = ReplayBackend::CpuLockstepReference;
    uint32_t flags = 0;
    uint64_t backendVersionHash = 0;
    uint64_t shaderVersionHash = 0;
    uint64_t terrainHash = 0;
    uint64_t generatorHash = 0;
    uint64_t contentHash = 0;
    ReplaySimulationConfig simulation{};
    ReplayCapacityProfile capacity{};
    std::string buildFingerprint;
};

enum class ReplayCommandType : uint32_t {
    DestroyBody = 0,
    SpawnBody = 1,
    Correction = 2,
    SetVelocity = 3,
    ApplyImpulse = 4,
    SetAwake = 5,
};

struct CanonicalReplayCommand {
    uint64_t tick = 0;
    uint64_t sequence = 0;
    uint32_t producer = 0;
    ReplayCommandType type = ReplayCommandType::SetAwake;
    uint32_t body = 0;
    uint32_t generation = 0;
    std::array<int32_t, 12> payload{};
};

enum class ReplayHashStage : uint32_t {
    Body = 1,
    Contact = 2,
    Island = 3,
    World = 4,
};

struct ReplayHashRecord {
    uint64_t tick = 0;
    ReplayHashStage stage = ReplayHashStage::World;
    uint32_t objectId = 0;
    uint32_t hash = 0;
};

struct ReplayCheckpoint {
    uint64_t tick = 0;
    uint64_t randomState = 0;
    std::array<int32_t, 4> deterministicWaterState{};
    std::vector<LockstepBody> bodies;
    std::vector<LockstepContact> contacts;
    std::vector<uint32_t> islandRoots;
    std::vector<uint32_t> freeBodyIds;
    std::vector<uint32_t> manifoldWords;
    std::vector<uint32_t> graphColors;
    std::vector<uint32_t> sleepCounters;
};

struct ReplayRecording {
    ReplayHeader header{};
    ReplayCheckpoint checkpoint{};
    std::vector<CanonicalReplayCommand> commands;
    std::vector<ReplayHashRecord> hashes;
};

struct ReplayReadResult {
    std::optional<ReplayRecording> recording;
    std::string error;
};

struct ReplayDivergence {
    uint64_t tick = 0;
    ReplayHashStage stage = ReplayHashStage::World;
    uint32_t objectId = 0;
    uint32_t expected = 0;
    uint32_t actual = 0;
    std::string message;
};

[[nodiscard]] bool canonicalReplayCommandLess(
    const CanonicalReplayCommand& lhs,
    const CanonicalReplayCommand& rhs) noexcept;

// Applies one already-canonical command. False means the body handle was stale
// or the lifecycle transition was invalid; callers still consume the command.
[[nodiscard]] bool applyCanonicalReplayCommand(
    LockstepWorld& world,
    const CanonicalReplayCommand& command) noexcept;

class ReplayCodec {
public:
    [[nodiscard]] static std::vector<std::byte> encode(
        const ReplayRecording& recording);
    [[nodiscard]] static ReplayReadResult decode(
        std::span<const std::byte> bytes);
};

class ReplayRecorder {
public:
    explicit ReplayRecorder(ReplayRecording recording = {});
    void record(const CanonicalReplayCommand& command);
    void recordHash(const ReplayHashRecord& hash);
    [[nodiscard]] ReplayRecording finish() &&;

private:
    ReplayRecording recording_{};
};

class ReplayPlayer {
public:
    struct Result {
        bool completed = false;
        std::optional<ReplayDivergence> divergence;
        LockstepTelemetry finalTelemetry{};
    };

    [[nodiscard]] Result play(const ReplayRecording& recording) const;
};

} // namespace voxy::physics::deterministic
