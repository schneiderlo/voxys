#pragma once

#include "network/replication.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace voxy::server {

inline constexpr uint32_t kAutomaticWorker =
    std::numeric_limits<uint32_t>::max();

struct WorkerDescriptor {
    uint32_t workerId = 0;
    uint32_t capacityUnits = 0;
    uint32_t loadUnits = 0;
    bool online = true;
};

struct IslandDescriptor {
    uint64_t islandId = 0;
    uint32_t workerId = kAutomaticWorker;
    uint32_t authorityEpoch = 1;
    uint64_t authorityStartTick = 0;
    uint32_t loadUnits = 0;
    network::AuthoritativeSnapshot checkpoint{};
};

struct SweptBoundaryProxy {
    uint64_t islandId = 0;
    uint32_t workerId = 0;
    uint32_t authorityEpoch = 0;
    uint64_t startTick = 0;
    uint32_t horizonTicks = 0;
    std::array<int64_t, 3> minimumQ12{};
    std::array<int64_t, 3> maximumQ12{};
};

struct IslandPair {
    uint64_t first = 0;
    uint64_t second = 0;
    [[nodiscard]] auto operator<=>(const IslandPair&) const = default;
};

enum class MigrationStatus : uint32_t {
    Scheduled = 1,
    Shadowing = 2,
    Verified = 3,
    HashMismatch = 4,
    Committed = 5,
    Aborted = 6,
    Recovered = 7,
};

struct IslandMigration {
    uint64_t islandId = 0;
    uint32_t sourceWorker = 0;
    uint32_t destinationWorker = 0;
    uint32_t sourceEpoch = 0;
    uint32_t destinationEpoch = 0;
    uint64_t scheduledTick = 0;
    uint64_t switchTick = 0;
    uint64_t sourceHash = 0;
    uint64_t destinationHash = 0;
    uint64_t rollbackRetainUntilTick = 0;
    MigrationStatus status = MigrationStatus::Scheduled;
    network::AuthoritativeSnapshot checkpoint{};
};

enum class CoordinatorFault : uint32_t {
    None = 0,
    DropNextProxy = 1,
    CorruptNextShadowHash = 2,
};

struct CoordinatorTelemetry {
    uint64_t proxyPublications = 0;
    uint64_t proxyDrops = 0;
    uint64_t proxyOverflow = 0;
    uint64_t crossWorkerPairs = 0;
    uint64_t migrationsScheduled = 0;
    uint64_t migrationsCommitted = 0;
    uint64_t migrationHashMismatches = 0;
    uint64_t migrationAborts = 0;
    uint64_t rollbackCopiesRetained = 0;
    uint64_t workerLosses = 0;
    uint64_t recoveredIslands = 0;
    uint64_t unrecoveredIslands = 0;
    uint32_t workerLoadHighWater = 0;
    uint32_t proxyHighWater = 0;
    uint32_t pairHighWater = 0;
};

class WorldCoordinator {
public:
    struct Config {
        uint32_t maximumWorkers = 64;
        uint32_t maximumIslands = 65'536;
        uint32_t maximumBoundaryProxies = 65'536;
        uint32_t maximumCrossWorkerPairs = 65'536;
        uint32_t migrationLeadTicks = 2;
        uint32_t rollbackRetentionTicks = 64;
    };

    WorldCoordinator();
    explicit WorldCoordinator(Config config);

    [[nodiscard]] bool registerWorker(const WorkerDescriptor& worker);
    [[nodiscard]] bool registerIsland(IslandDescriptor island);
    [[nodiscard]] bool updateCheckpoint(
        uint64_t islandId, network::AuthoritativeSnapshot checkpoint);
    [[nodiscard]] bool publishBoundaryProxy(const SweptBoundaryProxy& proxy);
    [[nodiscard]] std::vector<IslandPair> crossWorkerPairs(
        uint64_t tick) const;
    [[nodiscard]] std::vector<IslandMigration> planMigrations(uint64_t tick);
    [[nodiscard]] bool beginShadow(uint64_t islandId);
    [[nodiscard]] bool submitShadowHashes(
        uint64_t islandId, uint64_t sourceHash, uint64_t destinationHash);
    [[nodiscard]] uint32_t commitMigrations(uint64_t tick);
    [[nodiscard]] bool loseWorker(uint32_t workerId, uint64_t tick);
    void injectFault(CoordinatorFault fault) noexcept { nextFault_ = fault; }

    [[nodiscard]] std::optional<WorkerDescriptor> worker(
        uint32_t workerId) const;
    [[nodiscard]] std::optional<IslandDescriptor> island(
        uint64_t islandId) const;
    [[nodiscard]] std::span<const IslandMigration> migrations() const noexcept {
        return migrations_;
    }
    [[nodiscard]] bool ownershipValid(
        std::span<const IslandPair> connectedPairs, uint64_t tick) const;
    [[nodiscard]] const CoordinatorTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    [[nodiscard]] std::optional<uint32_t> chooseWorker(
        std::span<const uint64_t> component,
        std::optional<uint32_t> excludedWorker = std::nullopt) const;
    [[nodiscard]] uint32_t effectiveLoad(uint32_t workerId) const noexcept;
    [[nodiscard]] bool schedule(
        uint64_t islandId, uint32_t destinationWorker,
        uint64_t scheduledTick, uint64_t switchTick);
    void refreshLoads();

    Config config_{};
    std::vector<WorkerDescriptor> workers_;
    std::vector<IslandDescriptor> islands_;
    std::vector<SweptBoundaryProxy> proxies_;
    std::vector<IslandPair> latestConnectedPairs_;
    std::vector<IslandMigration> migrations_;
    CoordinatorFault nextFault_ = CoordinatorFault::None;
    CoordinatorTelemetry telemetry_{};
};

} // namespace voxy::server
