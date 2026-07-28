#pragma once

#include "network/protocol.hpp"
#include "physics/deterministic/lockstep_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace voxy::network {

inline constexpr uint32_t kInputRedundancyFrames = 4;
inline constexpr uint32_t kMaximumInputFramesPerBundle = 8;

struct InputFrame {
    uint64_t tick = 0;
    uint64_t sequence = 0;
    uint32_t buttons = 0;
    int32_t moveXQ16 = 0;
    int32_t moveZQ16 = 0;
    int32_t lookXQ16 = 0;
    int32_t lookYQ16 = 0;
};

struct InputBundleReadResult {
    std::optional<std::vector<InputFrame>> frames;
    std::string error;
};

class InputRedundancyBuffer {
public:
    explicit InputRedundancyBuffer(uint32_t historyCapacity = 64);
    [[nodiscard]] bool push(const InputFrame& frame);
    [[nodiscard]] std::vector<InputFrame> bundle(
        uint32_t frameCount = kInputRedundancyFrames) const;
    [[nodiscard]] std::vector<InputFrame> ingest(
        std::span<const InputFrame> redundantFrames);
    void acknowledge(uint64_t tick);
    [[nodiscard]] uint64_t acknowledgedTick() const noexcept {
        return acknowledgedTick_;
    }

    [[nodiscard]] static std::vector<std::byte> encode(
        std::span<const InputFrame> frames);
    [[nodiscard]] static InputBundleReadResult decode(
        std::span<const std::byte> bytes);

private:
    uint32_t historyCapacity_ = 64;
    uint64_t acknowledgedTick_ = 0;
    std::vector<InputFrame> frames_;
};

struct TickSyncSample {
    uint64_t clientSendMicros = 0;
    uint64_t clientReceiveMicros = 0;
    uint64_t localReceiveTick = 0;
    uint64_t serverTick = 0;
};

class TickSynchronizer {
public:
    struct Config {
        uint32_t tickRateHz = 60;
        uint32_t inputLeadTicks = 2;
        uint32_t maximumSamples = 16;
    };

    TickSynchronizer();
    explicit TickSynchronizer(Config config);
    [[nodiscard]] bool observe(const TickSyncSample& sample);
    [[nodiscard]] uint64_t estimatedServerTick(uint64_t localTick) const;
    [[nodiscard]] uint64_t recommendedInputTick(uint64_t localTick) const;
    [[nodiscard]] uint64_t minimumRttMicros() const noexcept {
        return minimumRttMicros_;
    }
    [[nodiscard]] int64_t offsetTicks() const noexcept { return offsetTicks_; }

private:
    Config config_{};
    uint64_t minimumRttMicros_ = 0;
    int64_t offsetTicks_ = 0;
    uint32_t samples_ = 0;
};

struct AuthoritativeSnapshot {
    uint64_t tick = 0;
    uint64_t islandId = 0;
    uint32_t authorityEpoch = 0;
    uint64_t baselineTick = 0;
    bool full = true;
    uint32_t stateHash = 0;
    std::vector<physics::deterministic::LockstepBody> bodies;
    std::vector<uint32_t> removedBodyIds;
};

struct SnapshotReadResult {
    std::optional<AuthoritativeSnapshot> snapshot;
    std::string error;
};

[[nodiscard]] uint32_t snapshotStateHash(
    const AuthoritativeSnapshot& snapshot);
[[nodiscard]] bool isCanonicalAuthoritativeSnapshot(
    const AuthoritativeSnapshot& snapshot);

class SnapshotCodec {
public:
    [[nodiscard]] static std::vector<std::byte> encode(
        const AuthoritativeSnapshot& snapshot);
    [[nodiscard]] static SnapshotReadResult decode(
        std::span<const std::byte> bytes);
};

class SnapshotHistory {
public:
    explicit SnapshotHistory(uint32_t capacity = 64);
    [[nodiscard]] bool store(AuthoritativeSnapshot snapshot);
    [[nodiscard]] std::optional<AuthoritativeSnapshot> find(
        uint64_t islandId, uint32_t epoch, uint64_t tick) const;
    [[nodiscard]] std::optional<AuthoritativeSnapshot> deltaFrom(
        const AuthoritativeSnapshot& current, uint64_t acknowledgedTick) const;
    [[nodiscard]] std::optional<AuthoritativeSnapshot> applyDelta(
        const AuthoritativeSnapshot& delta) const;
    [[nodiscard]] size_t size() const noexcept { return snapshots_.size(); }

private:
    uint32_t capacity_ = 64;
    std::vector<AuthoritativeSnapshot> snapshots_;
};

class SnapshotAckTracker {
public:
    void acknowledge(uint32_t clientId, uint64_t tick);
    [[nodiscard]] std::optional<uint64_t> acknowledgedTick(
        uint32_t clientId) const noexcept;

private:
    struct Entry { uint32_t clientId = 0; uint64_t tick = 0; };
    std::vector<Entry> entries_;
};

struct StaticChunkKey {
    uint64_t worldSeed = 0;
    uint64_t generatorVersion = 0;
    std::array<int32_t, 3> coordinate{};
    uint64_t contentHash = 0;
};

struct InterestCell {
    std::array<int32_t, 3> coordinate{};
    [[nodiscard]] auto operator<=>(const InterestCell&) const = default;
};

struct InterestQueryResult {
    std::vector<uint32_t> bodyIds;
    bool overflow = false;
};

class InterestGrid {
public:
    struct Config {
        int32_t cellSizeQ12 = 32 * physics::deterministic::kLockstepPositionOne;
        uint32_t maximumEntries = 65'536;
        uint32_t maximumQueryBodies = 4'096;
    };

    InterestGrid();
    explicit InterestGrid(Config config);
    [[nodiscard]] bool rebuild(
        std::span<const physics::deterministic::LockstepBody> bodies);
    [[nodiscard]] InterestCell cellFor(
        const physics::deterministic::LockstepBody& body) const noexcept;
    [[nodiscard]] InterestQueryResult query(
        InterestCell center, uint32_t radiusCells) const;
    [[nodiscard]] uint32_t highWater() const noexcept { return highWater_; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

private:
    struct Entry {
        InterestCell cell{};
        uint32_t bodyId = 0;
    };
    Config config_{};
    std::vector<Entry> entries_;
    uint32_t highWater_ = 0;
    bool overflowed_ = false;
};

struct IslandAuthority {
    uint64_t islandId = 0;
    uint32_t epoch = 0;
    uint32_t workerId = 0;
    uint64_t startTick = 0;
    std::array<uint64_t, 2> checkpointHash{};
};

class AuthorityTable {
public:
    [[nodiscard]] bool assign(const IslandAuthority& authority);
    [[nodiscard]] std::optional<IslandAuthority> find(uint64_t islandId) const;
    [[nodiscard]] bool accepts(
        uint64_t islandId, uint32_t epoch, uint64_t tick) const noexcept;
    [[nodiscard]] std::optional<IslandAuthority> advanceEpoch(
        uint64_t islandId, uint32_t destinationWorker, uint64_t startTick,
        std::array<uint64_t, 2> checkpointHash);

private:
    std::vector<IslandAuthority> entries_;
};

struct PredictionTelemetry {
    uint64_t confirmedTicks = 0;
    uint64_t rollbacks = 0;
    uint64_t replayedTicks = 0;
    uint64_t staleSnapshots = 0;
    uint64_t correctionEvents = 0;
    uint32_t historyHighWater = 0;
};

class PredictionBubble {
public:
    struct Config {
        physics::deterministic::LockstepWorld::Config world{};
        uint32_t historyTicks = 64;
        uint32_t maximumPredictedBodies = 256;
    };

    struct ReconcileResult {
        bool accepted = false;
        bool matched = false;
        bool rolledBack = false;
        uint64_t replayedTicks = 0;
        uint32_t finalHash = 0;
    };

    [[nodiscard]] bool initialize(
        const Config& config, uint64_t islandId, uint32_t authorityEpoch,
        uint32_t controlledBody,
        std::span<const physics::deterministic::LockstepBody> bodies);
    [[nodiscard]] bool setMembership(
        std::span<const uint32_t> predictedBodies,
        std::span<const uint32_t> boundaryGhosts);
    [[nodiscard]] bool predict(
        uint64_t tick,
        std::span<const physics::deterministic::CanonicalReplayCommand> commands);
    [[nodiscard]] ReconcileResult reconcile(
        const AuthoritativeSnapshot& authoritative);

    [[nodiscard]] AuthoritativeSnapshot snapshot() const;
    [[nodiscard]] std::span<const physics::deterministic::LockstepBody>
    bodies() const noexcept { return world_.bodies(); }
    [[nodiscard]] uint64_t currentTick() const noexcept { return currentTick_; }
    [[nodiscard]] const PredictionTelemetry& telemetry() const noexcept {
        return telemetry_;
    }
    [[nodiscard]] std::span<const physics::deterministic::CanonicalReplayCommand>
    recordedCommands() const noexcept { return recordedCommands_; }
    [[nodiscard]] std::span<const physics::deterministic::CanonicalReplayCommand>
    correctionEvents() const noexcept { return correctionEvents_; }

private:
    struct HistoryEntry {
        uint64_t tick = 0;
        uint32_t hash = 0;
        std::vector<physics::deterministic::LockstepBody> bodies;
    };

    void storeHistory(uint64_t tick);
    void recordCommand(
        const physics::deterministic::CanonicalReplayCommand& command);
    [[nodiscard]] std::vector<physics::deterministic::LockstepBody>
    expandSnapshot(const AuthoritativeSnapshot& snapshot) const;

    Config config_{};
    physics::deterministic::LockstepWorld world_{};
    uint64_t islandId_ = 0;
    uint32_t authorityEpoch_ = 0;
    uint32_t controlledBody_ = 0;
    uint64_t currentTick_ = 0;
    std::vector<uint32_t> predictedBodies_;
    std::vector<uint32_t> boundaryGhosts_;
    std::vector<HistoryEntry> history_;
    std::vector<physics::deterministic::CanonicalReplayCommand> inputs_;
    std::vector<physics::deterministic::CanonicalReplayCommand>
        recordedCommands_;
    std::vector<physics::deterministic::CanonicalReplayCommand>
        correctionEvents_;
    PredictionTelemetry telemetry_{};
};

} // namespace voxy::network
