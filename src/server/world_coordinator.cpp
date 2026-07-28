#include "server/world_coordinator.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>

namespace voxy::server {
namespace {

uint64_t endTick(const SweptBoundaryProxy& proxy) noexcept {
    return proxy.startTick > std::numeric_limits<uint64_t>::max()
            - proxy.horizonTicks
        ? std::numeric_limits<uint64_t>::max()
        : proxy.startTick + proxy.horizonTicks;
}

bool overlaps(const SweptBoundaryProxy& lhs,
              const SweptBoundaryProxy& rhs) noexcept {
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        if (lhs.maximumQ12[axis] < rhs.minimumQ12[axis]
            || rhs.maximumQ12[axis] < lhs.minimumQ12[axis]) return false;
    }
    return true;
}

bool activeMigration(MigrationStatus status) noexcept {
    return status == MigrationStatus::Scheduled
        || status == MigrationStatus::Shadowing
        || status == MigrationStatus::Verified;
}

} // namespace

WorldCoordinator::WorldCoordinator() : WorldCoordinator(Config{}) {}

WorldCoordinator::WorldCoordinator(Config config) : config_(config) {
    config_.maximumWorkers = std::max(config_.maximumWorkers, 1u);
    config_.maximumIslands = std::max(config_.maximumIslands, 1u);
    config_.maximumBoundaryProxies = std::max(
        config_.maximumBoundaryProxies, 1u);
    config_.maximumCrossWorkerPairs = std::max(
        config_.maximumCrossWorkerPairs, 1u);
    config_.migrationLeadTicks = std::max(config_.migrationLeadTicks, 1u);
    config_.rollbackRetentionTicks = std::max(
        config_.rollbackRetentionTicks, 1u);
}

bool WorldCoordinator::registerWorker(const WorkerDescriptor& source) {
    if (source.workerId == kAutomaticWorker || source.capacityUnits == 0u)
        return false;
    auto iterator = std::lower_bound(
        workers_.begin(), workers_.end(), source.workerId,
        [](const WorkerDescriptor& worker, uint32_t id) {
            return worker.workerId < id;
        });
    if (iterator != workers_.end() && iterator->workerId == source.workerId) {
        if (source.online
            && effectiveLoad(source.workerId) > source.capacityUnits) {
            return false;
        }
        const uint32_t load = iterator->loadUnits;
        *iterator = source;
        iterator->loadUnits = load;
        return true;
    }
    if (workers_.size() >= config_.maximumWorkers) return false;
    WorkerDescriptor worker = source;
    worker.loadUnits = 0u;
    workers_.insert(iterator, worker);
    return true;
}

bool WorldCoordinator::registerIsland(IslandDescriptor descriptor) {
    if (descriptor.islandId == 0u || descriptor.authorityEpoch == 0u
        || descriptor.loadUnits == 0u
        || islands_.size() >= config_.maximumIslands) return false;
    auto existing = std::lower_bound(
        islands_.begin(), islands_.end(), descriptor.islandId,
        [](const IslandDescriptor& island, uint64_t id) {
            return island.islandId < id;
        });
    if (existing != islands_.end()
        && existing->islandId == descriptor.islandId) return false;

    if (descriptor.workerId == kAutomaticWorker) {
        uint64_t bestScore = std::numeric_limits<uint64_t>::max();
        uint32_t selected = kAutomaticWorker;
        for (const auto& worker : workers_) {
            const uint64_t projected = effectiveLoad(worker.workerId)
                + descriptor.loadUnits;
            if (!worker.online
                || projected
                    > worker.capacityUnits) continue;
            const uint64_t score = (projected << 32u) / worker.capacityUnits;
            if (score < bestScore
                || (score == bestScore && worker.workerId < selected)) {
                bestScore = score;
                selected = worker.workerId;
            }
        }
        if (selected == kAutomaticWorker) return false;
        descriptor.workerId = selected;
    }
    const auto assignedWorker = worker(descriptor.workerId);
    if (!assignedWorker.has_value() || !assignedWorker->online
        || effectiveLoad(descriptor.workerId) + descriptor.loadUnits
            > assignedWorker->capacityUnits) return false;
    descriptor.checkpoint.islandId = descriptor.islandId;
    descriptor.checkpoint.authorityEpoch = descriptor.authorityEpoch;
    descriptor.checkpoint.tick = descriptor.authorityStartTick;
    descriptor.checkpoint.baselineTick = 0u;
    descriptor.checkpoint.full = true;
    descriptor.checkpoint.removedBodyIds.clear();
    descriptor.checkpoint.stateHash = network::snapshotStateHash(
        descriptor.checkpoint);
    if (!network::isCanonicalAuthoritativeSnapshot(
            descriptor.checkpoint)) {
        return false;
    }
    islands_.insert(existing, std::move(descriptor));
    refreshLoads();
    return true;
}

bool WorldCoordinator::updateCheckpoint(
    uint64_t islandId, network::AuthoritativeSnapshot checkpoint) {
    auto iterator = std::lower_bound(
        islands_.begin(), islands_.end(), islandId,
        [](const IslandDescriptor& island, uint64_t id) {
            return island.islandId < id;
        });
    if (iterator == islands_.end() || iterator->islandId != islandId
        || !checkpoint.full
        || !network::isCanonicalAuthoritativeSnapshot(checkpoint)
        || checkpoint.islandId != islandId
        || checkpoint.authorityEpoch != iterator->authorityEpoch
        || checkpoint.tick < iterator->authorityStartTick
        || checkpoint.tick < iterator->checkpoint.tick
        || (checkpoint.tick == iterator->checkpoint.tick
            && checkpoint.stateHash != iterator->checkpoint.stateHash)) {
        return false;
    }
    iterator->checkpoint = std::move(checkpoint);
    return true;
}

bool WorldCoordinator::publishBoundaryProxy(
    const SweptBoundaryProxy& proxy) {
    ++telemetry_.proxyPublications;
    if (nextFault_ == CoordinatorFault::DropNextProxy) {
        nextFault_ = CoordinatorFault::None;
        ++telemetry_.proxyDrops;
        return false;
    }
    const auto descriptor = island(proxy.islandId);
    const auto owner = descriptor.has_value()
        ? worker(descriptor->workerId) : std::nullopt;
    if (!descriptor.has_value() || proxy.horizonTicks == 0u
        || !owner.has_value() || !owner->online
        || descriptor->workerId != proxy.workerId
        || descriptor->authorityEpoch != proxy.authorityEpoch
        || proxy.startTick < descriptor->authorityStartTick) return false;
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        if (proxy.minimumQ12[axis] > proxy.maximumQ12[axis]) return false;
    }
    auto iterator = std::lower_bound(
        proxies_.begin(), proxies_.end(), proxy.islandId,
        [](const SweptBoundaryProxy& value, uint64_t id) {
            return value.islandId < id;
        });
    if (iterator != proxies_.end() && iterator->islandId == proxy.islandId) {
        if (proxy.startTick < iterator->startTick) return false;
        *iterator = proxy;
    } else {
        if (proxies_.size() >= config_.maximumBoundaryProxies) {
            ++telemetry_.proxyOverflow;
            return false;
        }
        proxies_.insert(iterator, proxy);
    }
    telemetry_.proxyHighWater = std::max(
        telemetry_.proxyHighWater, static_cast<uint32_t>(proxies_.size()));
    return true;
}

std::vector<IslandPair> WorldCoordinator::crossWorkerPairs(
    uint64_t tick) const {
    std::vector<IslandPair> pairs;
    pairs.reserve(std::min<size_t>(config_.maximumCrossWorkerPairs,
                                   proxies_.size()));
    for (size_t first = 0; first < proxies_.size(); ++first) {
        const auto& lhs = proxies_[first];
        const auto lhsIsland = island(lhs.islandId);
        const auto lhsWorker = worker(lhs.workerId);
        if (!lhsIsland.has_value() || !lhsWorker.has_value()
            || !lhsWorker->online
            || lhsIsland->workerId != lhs.workerId
            || lhsIsland->authorityEpoch != lhs.authorityEpoch
            || lhs.startTick < lhsIsland->authorityStartTick
            || tick < lhs.startTick || tick > endTick(lhs)) continue;
        for (size_t second = first + 1u; second < proxies_.size(); ++second) {
            const auto& rhs = proxies_[second];
            const auto rhsIsland = island(rhs.islandId);
            const auto rhsWorker = worker(rhs.workerId);
            if (!rhsIsland.has_value() || !rhsWorker.has_value()
                || !rhsWorker->online
                || rhsIsland->workerId != rhs.workerId
                || rhsIsland->authorityEpoch != rhs.authorityEpoch
                || rhs.startTick < rhsIsland->authorityStartTick
                || tick < rhs.startTick || tick > endTick(rhs)
                || lhs.workerId == rhs.workerId || !overlaps(lhs, rhs)) continue;
            if (pairs.size() >= config_.maximumCrossWorkerPairs) return pairs;
            pairs.push_back({lhs.islandId, rhs.islandId});
        }
    }
    return pairs;
}

uint64_t WorldCoordinator::effectiveLoad(uint32_t workerId) const noexcept {
    if (!worker(workerId).has_value())
        return std::numeric_limits<uint64_t>::max();
    uint64_t load = 0u;
    for (const auto& descriptor : islands_) {
        if (descriptor.workerId == workerId)
            load += descriptor.loadUnits;
    }
    for (const auto& migration : migrations_) {
        if (!activeMigration(migration.status)
            || migration.destinationWorker != workerId) continue;
        const auto moving = island(migration.islandId);
        if (moving.has_value() && moving->workerId != workerId)
            load += moving->loadUnits;
    }
    return load;
}

std::optional<uint32_t> WorldCoordinator::chooseWorker(
    std::span<const uint64_t> component,
    std::optional<uint32_t> excludedWorker) const {
    uint64_t bestScore = std::numeric_limits<uint64_t>::max();
    uint32_t selected = kAutomaticWorker;
    for (const auto& candidate : workers_) {
        if (!candidate.online
            || (excludedWorker.has_value()
                && candidate.workerId == *excludedWorker)) continue;
        uint64_t projected = effectiveLoad(candidate.workerId);
        for (uint64_t islandId : component) {
            const auto descriptor = island(islandId);
            if (!descriptor.has_value()) {
                projected = std::numeric_limits<uint64_t>::max();
                break;
            }
            if (descriptor->workerId != candidate.workerId)
                projected += descriptor->loadUnits;
        }
        if (projected > candidate.capacityUnits) continue;
        const uint64_t score = (projected << 32u) / candidate.capacityUnits;
        if (score < bestScore
            || (score == bestScore && candidate.workerId < selected)) {
            bestScore = score;
            selected = candidate.workerId;
        }
    }
    return selected == kAutomaticWorker
        ? std::nullopt : std::optional<uint32_t>(selected);
}

bool WorldCoordinator::schedule(
    uint64_t islandId, uint32_t destinationWorker,
    uint64_t scheduledTick, uint64_t switchTick) {
    const auto descriptor = island(islandId);
    const auto destination = worker(destinationWorker);
    if (!descriptor.has_value() || !destination.has_value()
        || !destination->online || descriptor->workerId == destinationWorker
        || descriptor->authorityEpoch == std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    if (std::any_of(migrations_.begin(), migrations_.end(),
        [islandId](const IslandMigration& migration) {
            return migration.islandId == islandId
                && activeMigration(migration.status);
        })) return false;
    IslandMigration migration;
    migration.islandId = islandId;
    migration.sourceWorker = descriptor->workerId;
    migration.destinationWorker = destinationWorker;
    migration.sourceEpoch = descriptor->authorityEpoch;
    migration.destinationEpoch = descriptor->authorityEpoch + 1u;
    migration.scheduledTick = scheduledTick;
    migration.switchTick = switchTick;
    migration.status = MigrationStatus::Scheduled;
    migration.checkpoint = descriptor->checkpoint;
    migrations_.push_back(std::move(migration));
    std::stable_sort(migrations_.begin(), migrations_.end(),
        [](const IslandMigration& lhs, const IslandMigration& rhs) {
            return std::tie(lhs.switchTick, lhs.islandId,
                            lhs.destinationWorker)
                 < std::tie(rhs.switchTick, rhs.islandId,
                            rhs.destinationWorker);
        });
    ++telemetry_.migrationsScheduled;
    return true;
}

std::vector<IslandMigration> WorldCoordinator::planMigrations(uint64_t tick) {
    const auto pairs = crossWorkerPairs(tick);
    telemetry_.crossWorkerPairs += pairs.size();
    telemetry_.pairHighWater = std::max(
        telemetry_.pairHighWater, static_cast<uint32_t>(pairs.size()));
    if (pairs.empty()) return {};

    std::vector<uint32_t> roots(islands_.size());
    std::iota(roots.begin(), roots.end(), 0u);
    const auto indexFor = [this](uint64_t islandId) -> std::optional<uint32_t> {
        auto iterator = std::lower_bound(
            islands_.begin(), islands_.end(), islandId,
            [](const IslandDescriptor& value, uint64_t id) {
                return value.islandId < id;
            });
        if (iterator == islands_.end() || iterator->islandId != islandId)
            return std::nullopt;
        return static_cast<uint32_t>(iterator - islands_.begin());
    };
    const auto findRoot = [&roots](uint32_t value) {
        while (roots[value] != value) value = roots[value];
        return value;
    };
    for (const auto& pair : pairs) {
        const auto first = indexFor(pair.first);
        const auto second = indexFor(pair.second);
        if (!first.has_value() || !second.has_value()) continue;
        const uint32_t rootA = findRoot(*first);
        const uint32_t rootB = findRoot(*second);
        roots[std::max(rootA, rootB)] = std::min(rootA, rootB);
    }
    for (uint32_t index = 0; index < roots.size(); ++index)
        roots[index] = findRoot(index);

    std::vector<uint64_t> newlyScheduled;
    for (uint32_t root = 0; root < roots.size(); ++root) {
        if (roots[root] != root) continue;
        std::vector<uint64_t> component;
        for (uint32_t index = 0; index < roots.size(); ++index) {
            if (roots[index] == root) component.push_back(islands_[index].islandId);
        }
        if (component.size() < 2u) continue;
        bool spansWorkers = false;
        for (size_t index = 1; index < component.size(); ++index) {
            spansWorkers = spansWorkers
                || island(component[index])->workerId
                    != island(component[0])->workerId;
        }
        if (!spansWorkers) continue;
        const bool alreadyMigrating = std::any_of(
            migrations_.begin(), migrations_.end(),
            [&component](const IslandMigration& migration) {
                return activeMigration(migration.status)
                    && std::binary_search(
                        component.begin(), component.end(),
                        migration.islandId);
            });
        if (alreadyMigrating) continue;
        const auto destination = chooseWorker(component);
        if (!destination.has_value()) continue;
        uint64_t switchTick = tick > std::numeric_limits<uint64_t>::max()
                - config_.migrationLeadTicks
            ? std::numeric_limits<uint64_t>::max()
            : tick + config_.migrationLeadTicks;
        uint64_t latestSwitch = std::numeric_limits<uint64_t>::max();
        for (uint64_t islandId : component) {
            auto proxy = std::lower_bound(
                proxies_.begin(), proxies_.end(), islandId,
                [](const SweptBoundaryProxy& value, uint64_t id) {
                    return value.islandId < id;
                });
            if (proxy != proxies_.end() && proxy->islandId == islandId)
                latestSwitch = std::min(latestSwitch, endTick(*proxy));
        }
        switchTick = std::min(switchTick, latestSwitch);
        if (switchTick <= tick) continue;
        for (uint64_t islandId : component) {
            if (island(islandId)->workerId != *destination
                && schedule(islandId, *destination, tick, switchTick)) {
                newlyScheduled.push_back(islandId);
            }
        }
    }
    std::vector<IslandMigration> result;
    for (uint64_t islandId : newlyScheduled) {
        auto migration = std::find_if(
            migrations_.begin(), migrations_.end(),
            [=](const IslandMigration& value) {
                return value.islandId == islandId
                    && value.scheduledTick == tick
                    && activeMigration(value.status);
            });
        if (migration != migrations_.end()) result.push_back(*migration);
    }
    std::stable_sort(result.begin(), result.end(),
        [](const IslandMigration& lhs, const IslandMigration& rhs) {
            return std::tie(lhs.switchTick, lhs.islandId)
                 < std::tie(rhs.switchTick, rhs.islandId);
        });
    return result;
}

bool WorldCoordinator::beginShadow(uint64_t islandId) {
    auto iterator = std::find_if(migrations_.begin(), migrations_.end(),
        [islandId](const IslandMigration& migration) {
            return migration.islandId == islandId
                && migration.status == MigrationStatus::Scheduled;
        });
    if (iterator == migrations_.end()) return false;
    iterator->status = MigrationStatus::Shadowing;
    return true;
}

bool WorldCoordinator::submitShadowHashes(
    uint64_t islandId, uint64_t sourceHash, uint64_t destinationHash) {
    auto iterator = std::find_if(migrations_.begin(), migrations_.end(),
        [islandId](const IslandMigration& migration) {
            return migration.islandId == islandId
                && (migration.status == MigrationStatus::Scheduled
                    || migration.status == MigrationStatus::Shadowing);
        });
    if (iterator == migrations_.end()) return false;
    if (nextFault_ == CoordinatorFault::CorruptNextShadowHash) {
        destinationHash ^= 1u;
        nextFault_ = CoordinatorFault::None;
    }
    iterator->sourceHash = sourceHash;
    iterator->destinationHash = destinationHash;
    iterator->status = sourceHash == destinationHash
        ? MigrationStatus::Verified : MigrationStatus::HashMismatch;
    if (iterator->status == MigrationStatus::HashMismatch)
        ++telemetry_.migrationHashMismatches;
    return iterator->status == MigrationStatus::Verified;
}

uint32_t WorldCoordinator::commitMigrations(uint64_t tick) {
    uint32_t committed = 0;
    for (auto& migration : migrations_) {
        if (migration.status != MigrationStatus::Verified
            || tick < migration.switchTick) continue;
        auto source = std::lower_bound(
            islands_.begin(), islands_.end(), migration.islandId,
            [](const IslandDescriptor& value, uint64_t id) {
                return value.islandId < id;
            });
        const auto destination = worker(migration.destinationWorker);
        if (source == islands_.end() || source->islandId != migration.islandId
            || !destination.has_value() || !destination->online
            || source->workerId != migration.sourceWorker
            || source->authorityEpoch != migration.sourceEpoch
            || effectiveLoad(migration.destinationWorker)
                > destination->capacityUnits) {
            migration.status = MigrationStatus::Aborted;
            ++telemetry_.migrationAborts;
            continue;
        }
        source->workerId = migration.destinationWorker;
        source->authorityEpoch = migration.destinationEpoch;
        source->authorityStartTick = migration.switchTick;
        source->checkpoint.authorityEpoch = migration.destinationEpoch;
        source->checkpoint.tick = migration.switchTick;
        source->checkpoint.stateHash = network::snapshotStateHash(
            source->checkpoint);
        eraseBoundaryProxy(migration.islandId);
        migration.rollbackRetainUntilTick = migration.switchTick
            > std::numeric_limits<uint64_t>::max()
                    - config_.rollbackRetentionTicks
            ? std::numeric_limits<uint64_t>::max()
            : migration.switchTick + config_.rollbackRetentionTicks;
        migration.status = MigrationStatus::Committed;
        ++telemetry_.migrationsCommitted;
        ++telemetry_.rollbackCopiesRetained;
        ++committed;
    }
    refreshLoads();
    return committed;
}

bool WorldCoordinator::loseWorker(uint32_t workerId, uint64_t tick) {
    auto failed = std::lower_bound(
        workers_.begin(), workers_.end(), workerId,
        [](const WorkerDescriptor& worker, uint32_t id) {
            return worker.workerId < id;
        });
    if (failed == workers_.end() || failed->workerId != workerId
        || !failed->online) return false;
    failed->online = false;
    ++telemetry_.workerLosses;
    for (auto& migration : migrations_) {
        if (activeMigration(migration.status)
            && (migration.sourceWorker == workerId
                || migration.destinationWorker == workerId)) {
            migration.status = MigrationStatus::Aborted;
            ++telemetry_.migrationAborts;
        }
    }
    std::vector<uint64_t> affected;
    for (const auto& descriptor : islands_) {
        if (descriptor.workerId == workerId)
            affected.push_back(descriptor.islandId);
    }
    if (affected.empty()) return true;
    const auto recoverableEnd = std::remove_if(
        affected.begin(), affected.end(),
        [this](uint64_t islandId) {
            const auto descriptor = island(islandId);
            if (!descriptor.has_value()
                || descriptor->authorityEpoch
                    == std::numeric_limits<uint32_t>::max()) {
                ++telemetry_.unrecoveredIslands;
                return true;
            }
            return false;
        });
    const bool unrecovered = recoverableEnd != affected.end();
    affected.erase(recoverableEnd, affected.end());
    if (affected.empty()) {
        refreshLoads();
        return false;
    }
    const auto destination = chooseWorker(affected, workerId);
    if (!destination.has_value()) {
        telemetry_.unrecoveredIslands += affected.size();
        refreshLoads();
        return false;
    }
    for (uint64_t islandId : affected) {
        auto descriptor = std::lower_bound(
            islands_.begin(), islands_.end(), islandId,
            [](const IslandDescriptor& value, uint64_t id) {
                return value.islandId < id;
            });
        IslandMigration recovery;
        recovery.islandId = islandId;
        recovery.sourceWorker = workerId;
        recovery.destinationWorker = *destination;
        recovery.sourceEpoch = descriptor->authorityEpoch;
        recovery.destinationEpoch = descriptor->authorityEpoch + 1u;
        recovery.scheduledTick = tick;
        recovery.switchTick = tick;
        recovery.sourceHash = descriptor->checkpoint.stateHash;
        recovery.destinationHash = descriptor->checkpoint.stateHash;
        recovery.status = MigrationStatus::Recovered;
        recovery.checkpoint = descriptor->checkpoint;
        descriptor->workerId = *destination;
        ++descriptor->authorityEpoch;
        descriptor->authorityStartTick = tick;
        descriptor->checkpoint.authorityEpoch = descriptor->authorityEpoch;
        descriptor->checkpoint.tick = tick;
        descriptor->checkpoint.stateHash = network::snapshotStateHash(
            descriptor->checkpoint);
        eraseBoundaryProxy(islandId);
        migrations_.push_back(std::move(recovery));
        ++telemetry_.recoveredIslands;
    }
    refreshLoads();
    return !unrecovered;
}

std::optional<WorkerDescriptor> WorldCoordinator::worker(
    uint32_t workerId) const {
    auto iterator = std::lower_bound(
        workers_.begin(), workers_.end(), workerId,
        [](const WorkerDescriptor& value, uint32_t id) {
            return value.workerId < id;
        });
    return iterator != workers_.end() && iterator->workerId == workerId
        ? std::optional<WorkerDescriptor>(*iterator) : std::nullopt;
}

std::optional<IslandDescriptor> WorldCoordinator::island(
    uint64_t islandId) const {
    auto iterator = std::lower_bound(
        islands_.begin(), islands_.end(), islandId,
        [](const IslandDescriptor& value, uint64_t id) {
            return value.islandId < id;
        });
    return iterator != islands_.end() && iterator->islandId == islandId
        ? std::optional<IslandDescriptor>(*iterator) : std::nullopt;
}

bool WorldCoordinator::ownershipValid(
    std::span<const IslandPair> connectedPairs, uint64_t tick) const {
    for (const auto& pair : connectedPairs) {
        const auto first = island(pair.first);
        const auto second = island(pair.second);
        if (!first.has_value() || !second.has_value()
            || tick < first->authorityStartTick
            || tick < second->authorityStartTick
            || first->workerId != second->workerId) return false;
        const auto owner = worker(first->workerId);
        if (!owner.has_value() || !owner->online) return false;
    }
    return true;
}

void WorldCoordinator::eraseBoundaryProxy(uint64_t islandId) noexcept {
    const auto iterator = std::lower_bound(
        proxies_.begin(), proxies_.end(), islandId,
        [](const SweptBoundaryProxy& proxy, uint64_t id) {
            return proxy.islandId < id;
        });
    if (iterator != proxies_.end() && iterator->islandId == islandId)
        proxies_.erase(iterator);
}

void WorldCoordinator::refreshLoads() {
    for (auto& worker : workers_) worker.loadUnits = 0u;
    for (const auto& island : islands_) {
        auto owner = std::lower_bound(
            workers_.begin(), workers_.end(), island.workerId,
            [](const WorkerDescriptor& value, uint32_t id) {
                return value.workerId < id;
            });
        if (owner == workers_.end() || owner->workerId != island.workerId)
            continue;
        const uint64_t load = uint64_t{owner->loadUnits} + island.loadUnits;
        owner->loadUnits = static_cast<uint32_t>(std::min<uint64_t>(
            load, std::numeric_limits<uint32_t>::max()));
        telemetry_.workerLoadHighWater = std::max(
            telemetry_.workerLoadHighWater, owner->loadUnits);
    }
}

} // namespace voxy::server
