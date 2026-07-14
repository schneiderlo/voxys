#pragma once

#include "network/replication.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::network {

struct SandboxTelemetry {
    uint64_t tick = 0;
    uint64_t acceptedCommands = 0;
    uint64_t rejectedCommands = 0;
    uint64_t duplicateInputs = 0;
    uint64_t staleEpochCommands = 0;
    uint64_t spawnedMeteors = 0;
    uint64_t correctionEvents = 0;
    uint64_t snapshots = 0;
    uint64_t deltas = 0;
    uint32_t liveBodies = 0;
    uint32_t worldHash = 0;
};

class TwoClientMeteorBoxSandbox {
public:
    struct Config {
        uint32_t bodyCapacity = 32;
        uint32_t contactCapacity = 128;
        uint32_t historyTicks = 64;
        uint32_t inputFutureWindow = 8;
        uint64_t islandId = 1;
        uint32_t authorityEpoch = 1;
    };

    TwoClientMeteorBoxSandbox();
    explicit TwoClientMeteorBoxSandbox(Config config);

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool submitInputs(
        uint32_t clientId, uint64_t islandId, uint32_t authorityEpoch,
        std::span<const InputFrame> redundantFrames);
    [[nodiscard]] bool submitCommand(const CanonicalNetworkCommand& command);
    [[nodiscard]] bool queueCorrection(
        uint32_t bodyId,
        const physics::deterministic::LockstepBody& authoritativeBody);
    [[nodiscard]] bool step();

    [[nodiscard]] AuthoritativeSnapshot currentSnapshot() const;
    [[nodiscard]] std::optional<AuthoritativeSnapshot> snapshotFor(
        uint32_t clientId);
    [[nodiscard]] bool acknowledgeSnapshot(uint32_t clientId, uint64_t tick);
    [[nodiscard]] std::optional<IslandAuthority> advanceAuthority(
        uint32_t destinationWorker, uint64_t startTick);

    [[nodiscard]] uint64_t currentTick() const noexcept { return currentTick_; }
    [[nodiscard]] uint64_t islandId() const noexcept { return config_.islandId; }
    [[nodiscard]] uint32_t authorityEpoch() const noexcept {
        return config_.authorityEpoch;
    }
    [[nodiscard]] uint32_t controlledBody(uint32_t clientId) const noexcept;
    [[nodiscard]] std::span<const physics::deterministic::LockstepBody>
    bodies() const noexcept { return world_.bodies(); }
    [[nodiscard]] std::span<const CanonicalNetworkCommand>
    recordedNetworkCommands() const noexcept { return networkRecording_; }
    [[nodiscard]] std::span<
        const physics::deterministic::CanonicalReplayCommand>
    recordedReplayCommands() const noexcept { return replayRecording_; }
    [[nodiscard]] const SandboxTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    struct ClientState {
        uint32_t clientId = 0;
        uint32_t controlledBody = 0;
        InputRedundancyBuffer receivedInputs{64};
    };

    [[nodiscard]] ClientState* client(uint32_t clientId) noexcept;
    [[nodiscard]] const ClientState* client(uint32_t clientId) const noexcept;
    [[nodiscard]] bool validateClientCommand(
        const CanonicalNetworkCommand& command) const noexcept;
    [[nodiscard]] uint32_t allocateBodyId() const noexcept;
    [[nodiscard]] bool execute(CanonicalNetworkCommand command);
    void recordNetwork(const CanonicalNetworkCommand& command);
    void recordReplay(
        const physics::deterministic::CanonicalReplayCommand& command);

    Config config_{};
    physics::deterministic::LockstepWorld world_{};
    AuthorityTable authority_{};
    SnapshotHistory snapshots_{};
    SnapshotAckTracker acknowledgements_{};
    std::array<ClientState, 2> clients_{};
    std::vector<CanonicalNetworkCommand> pending_;
    std::vector<CanonicalNetworkCommand> networkRecording_;
    std::vector<physics::deterministic::CanonicalReplayCommand>
        replayRecording_;
    uint64_t currentTick_ = 0;
    uint64_t nextServerSequence_ = 1;
    SandboxTelemetry telemetry_{};
    bool initialized_ = false;
};

} // namespace voxy::network
