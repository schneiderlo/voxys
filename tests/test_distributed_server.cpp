#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "network/replication.hpp"
#include "server/native_gpu_backend.hpp"
#include "server/world_coordinator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#ifndef WGPUWrappedSubmissionIndex
using WGPUSubmissionIndex = uint64_t;
struct WGPUWrappedSubmissionIndex {
    WGPUQueue queue;
    WGPUSubmissionIndex submissionIndex;
};
#endif

extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(
    WGPUQueue queue, size_t commandCount,
    const WGPUCommandBuffer* commands);
extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);

namespace voxy::server {
namespace {

using physics::deterministic::LockstepBody;
using physics::deterministic::LockstepBodyAlive;
using physics::deterministic::LockstepBodyAwake;

LockstepBody body(uint32_t id, int32_t xQ12, int32_t yQ12) {
    LockstepBody result;
    result.identity = {id, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
    result.sectorRadius = {
        0, 0, 0, physics::deterministic::kLockstepPositionOne / 2};
    result.positionInvMass = {
        xQ12, yQ12, 0, physics::deterministic::kLockstepVelocityOne};
    result.linearVelocity[0] = 4'096;
    return result;
}

network::AuthoritativeSnapshot checkpoint(
    uint64_t islandId, uint32_t epoch, uint64_t tick) {
    network::AuthoritativeSnapshot result;
    result.tick = tick;
    result.islandId = islandId;
    result.authorityEpoch = epoch;
    result.full = true;
    result.bodies = {body(1u, static_cast<int32_t>(islandId * 32), 8'192)};
    result.stateHash = network::snapshotStateHash(result);
    return result;
}

SweptBoundaryProxy proxy(
    uint64_t islandId, uint32_t worker, uint32_t epoch,
    uint64_t tick, int64_t minimumX, int64_t maximumX,
    uint32_t horizonTicks = 5u) {
    return {
        .islandId = islandId,
        .workerId = worker,
        .authorityEpoch = epoch,
        .startTick = tick,
        .horizonTicks = horizonTicks,
        .minimumQ12 = {minimumX, 0, 0},
        .maximumQ12 = {maximumX, 100, 100},
    };
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

TEST(NativeServerGpu, MovedFromBackendFailsSafely) {
    NativeServerGpuBackend source;
    NativeServerGpuBackend destination(std::move(source));
    source.shutdown();
    EXPECT_FALSE(source.createWorld({
        .worldId = 1u,
        .islandId = 1u,
    }).has_value());
    EXPECT_FALSE(source.initialize(nullptr, nullptr, {.maximumWorlds = 1u}));
    EXPECT_EQ(source.telemetry().activeWorlds, 0u);
    destination.shutdown();
}

TEST(WorldCoordinator, MigratesWholeConnectivityAndRecoversWorkerLoss) {
    WorldCoordinator assignment;
    ASSERT_TRUE(assignment.registerWorker({10u, 100u, 0u, true}));
    ASSERT_TRUE(assignment.registerWorker({20u, 100u, 0u, true}));
    ASSERT_TRUE(assignment.registerIsland({
        .islandId = 99u,
        .workerId = kAutomaticWorker,
        .loadUnits = 20u,
        .checkpoint = checkpoint(99u, 1u, 0u),
    }));
    EXPECT_EQ(assignment.island(99u)->workerId, 10u);

    WorldCoordinator coordinator;
    ASSERT_TRUE(coordinator.registerWorker({1u, 500u, 0u, true}));
    ASSERT_TRUE(coordinator.registerWorker({2u, 500u, 0u, true}));
    ASSERT_TRUE(coordinator.registerWorker({3u, 500u, 0u, true}));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 1,
        .workerId = 1,
        .loadUnits = 100,
        .checkpoint = checkpoint(1, 1, 0),
    }));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 2,
        .workerId = 2,
        .loadUnits = 100,
        .checkpoint = checkpoint(2, 1, 0),
    }));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 3,
        .workerId = 3,
        .loadUnits = 400,
        .checkpoint = checkpoint(3, 1, 0),
    }));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 4,
        .workerId = 2,
        .loadUnits = 50,
        .checkpoint = checkpoint(4, 1, 0),
    }));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 5,
        .workerId = 1,
        .loadUnits = 50,
        .checkpoint = checkpoint(5, 1, 0),
    }));

    ASSERT_TRUE(coordinator.publishBoundaryProxy(proxy(1, 1, 1, 10, 0, 10)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(proxy(2, 2, 1, 10, 5, 15)));
    const auto cross = coordinator.crossWorkerPairs(10);
    ASSERT_EQ(cross, (std::vector<IslandPair>{{1u, 2u}}));
    const auto planned = coordinator.planMigrations(10);
    ASSERT_EQ(planned.size(), 1u);
    EXPECT_EQ(planned[0].islandId, 2u);
    EXPECT_EQ(planned[0].destinationWorker, 1u);
    EXPECT_EQ(planned[0].switchTick, 12u);
    ASSERT_TRUE(coordinator.beginShadow(2u));
    const uint64_t handoffHash = 0x123456789ABCDEF0ull;
    ASSERT_TRUE(coordinator.submitShadowHashes(
        2u, handoffHash, handoffHash));
    EXPECT_EQ(coordinator.commitMigrations(11u), 0u);
    EXPECT_EQ(coordinator.commitMigrations(12u), 1u);
    ASSERT_TRUE(coordinator.island(2u).has_value());
    EXPECT_EQ(coordinator.island(2u)->workerId, 1u);
    EXPECT_EQ(coordinator.island(2u)->authorityEpoch, 2u);
    EXPECT_TRUE(coordinator.ownershipValid(cross, 12u));
    EXPECT_TRUE(coordinator.crossWorkerPairs(12u).empty());
    EXPECT_FALSE(coordinator.publishBoundaryProxy(
        proxy(2u, 1u, 2u, 11u, 5, 15)));
    const auto committed = std::find_if(
        coordinator.migrations().begin(), coordinator.migrations().end(),
        [](const IslandMigration& migration) {
            return migration.islandId == 2u
                && migration.status == MigrationStatus::Committed;
        });
    ASSERT_NE(committed, coordinator.migrations().end());
    EXPECT_EQ(committed->rollbackRetainUntilTick, 76u);

    ASSERT_TRUE(coordinator.publishBoundaryProxy(proxy(4, 2, 1, 20, 20, 30)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(proxy(5, 1, 1, 20, 25, 35)));
    const auto secondPlan = coordinator.planMigrations(20u);
    ASSERT_EQ(secondPlan.size(), 1u);
    ASSERT_EQ(secondPlan[0].islandId, 5u);
    coordinator.injectFault(CoordinatorFault::CorruptNextShadowHash);
    EXPECT_FALSE(coordinator.submitShadowHashes(5u, 99u, 99u));
    EXPECT_EQ(coordinator.commitMigrations(22u), 0u);
    EXPECT_EQ(coordinator.island(5u)->workerId, 1u);
    EXPECT_EQ(coordinator.telemetry().migrationHashMismatches, 1u);

    coordinator.injectFault(CoordinatorFault::DropNextProxy);
    EXPECT_FALSE(coordinator.publishBoundaryProxy(proxy(3, 3, 1, 25, 50, 60)));
    EXPECT_EQ(coordinator.telemetry().proxyDrops, 1u);

    ASSERT_TRUE(coordinator.loseWorker(1u, 30u));
    EXPECT_FALSE(coordinator.worker(1u)->online);
    EXPECT_EQ(coordinator.island(1u)->workerId, 2u);
    EXPECT_EQ(coordinator.island(2u)->workerId, 2u);
    EXPECT_EQ(coordinator.island(5u)->workerId, 2u);
    EXPECT_TRUE(coordinator.ownershipValid(
        std::array<IslandPair, 2>{{{1u, 2u}, {4u, 5u}}}, 30u));
    EXPECT_EQ(coordinator.telemetry().workerLosses, 1u);
    EXPECT_EQ(coordinator.telemetry().recoveredIslands, 3u);
    EXPECT_EQ(coordinator.telemetry().unrecoveredIslands, 0u);
    EXPECT_GE(coordinator.telemetry().rollbackCopiesRetained, 1u);
}

TEST(WorldCoordinator, ReservesCapacityAndKeepsCheckpointsMonotonic) {
    WorldCoordinator coordinator;
    ASSERT_TRUE(coordinator.registerWorker({1u, 100u, 0u, true}));
    ASSERT_TRUE(coordinator.registerWorker({2u, 100u, 0u, true}));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 1u,
        .workerId = 1u,
        .loadUnits = 80u,
        .checkpoint = checkpoint(1u, 1u, 0u),
    }));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 2u,
        .workerId = 2u,
        .loadUnits = 20u,
        .checkpoint = checkpoint(2u, 1u, 0u),
    }));

    const auto newest = checkpoint(1u, 1u, 5u);
    ASSERT_TRUE(coordinator.updateCheckpoint(1u, newest));
    EXPECT_FALSE(coordinator.updateCheckpoint(
        1u, checkpoint(1u, 1u, 4u)));
    auto conflicting = newest;
    conflicting.bodies.front().positionInvMass[0] += 1;
    conflicting.stateHash = network::snapshotStateHash(conflicting);
    EXPECT_FALSE(coordinator.updateCheckpoint(1u, conflicting));
    EXPECT_EQ(coordinator.island(1u)->checkpoint.tick, 5u);
    EXPECT_EQ(coordinator.island(1u)->checkpoint.stateHash, newest.stateHash);

    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(1u, 1u, 1u, 10u, 0, 10)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(2u, 2u, 1u, 10u, 5, 15)));
    const auto planned = coordinator.planMigrations(10u);
    ASSERT_EQ(planned.size(), 1u);
    ASSERT_EQ(planned.front().islandId, 2u);
    ASSERT_EQ(planned.front().destinationWorker, 1u);

    EXPECT_FALSE(coordinator.registerIsland({
        .islandId = 3u,
        .workerId = 1u,
        .loadUnits = 1u,
        .checkpoint = checkpoint(3u, 1u, 0u),
    }));
    EXPECT_FALSE(coordinator.registerWorker({1u, 99u, 0u, true}));
    ASSERT_TRUE(coordinator.beginShadow(2u));
    ASSERT_TRUE(coordinator.submitShadowHashes(2u, 123u, 123u));
    EXPECT_EQ(coordinator.commitMigrations(12u), 1u);
    EXPECT_EQ(coordinator.worker(1u)->loadUnits, 100u);
}

TEST(WorldCoordinator, RejectsExpiredHandoffsAndEpochWrap) {
    WorldCoordinator coordinator;
    EXPECT_FALSE(coordinator.registerWorker(
        {kAutomaticWorker, 100u, 0u, true}));
    ASSERT_TRUE(coordinator.registerWorker({1u, 100u, 0u, true}));
    ASSERT_TRUE(coordinator.registerWorker({2u, 100u, 0u, true}));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 1u,
        .workerId = 1u,
        .loadUnits = 50u,
        .checkpoint = checkpoint(1u, 1u, 0u),
    }));
    ASSERT_TRUE(coordinator.registerIsland({
        .islandId = 2u,
        .workerId = 2u,
        .loadUnits = 50u,
        .checkpoint = checkpoint(2u, 1u, 0u),
    }));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(1u, 1u, 1u, 10u, 0, 10, 1u)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(2u, 2u, 1u, 10u, 5, 15, 1u)));
    ASSERT_EQ(coordinator.crossWorkerPairs(11u).size(), 1u);
    EXPECT_TRUE(coordinator.planMigrations(11u).empty());
    EXPECT_TRUE(coordinator.migrations().empty());

    WorldCoordinator exhausted;
    ASSERT_TRUE(exhausted.registerWorker({1u, 100u, 0u, true}));
    ASSERT_TRUE(exhausted.registerWorker({2u, 100u, 0u, true}));
    constexpr uint32_t finalEpoch = std::numeric_limits<uint32_t>::max();
    ASSERT_TRUE(exhausted.registerIsland({
        .islandId = 9u,
        .workerId = 1u,
        .authorityEpoch = finalEpoch,
        .loadUnits = 10u,
        .checkpoint = checkpoint(9u, finalEpoch, 0u),
    }));
    EXPECT_FALSE(exhausted.loseWorker(1u, 20u));
    ASSERT_TRUE(exhausted.island(9u).has_value());
    EXPECT_EQ(exhausted.island(9u)->workerId, 1u);
    EXPECT_EQ(exhausted.island(9u)->authorityEpoch, finalEpoch);
    EXPECT_EQ(exhausted.telemetry().unrecoveredIslands, 1u);
}

TEST(WorldCoordinator, DoesNotSplitComponentAroundActiveMigration) {
    WorldCoordinator coordinator;
    for (uint32_t workerId = 1u; workerId <= 3u; ++workerId)
        ASSERT_TRUE(coordinator.registerWorker(
            {workerId, 300u, 0u, true}));
    for (uint64_t islandId = 1u; islandId <= 3u; ++islandId) {
        ASSERT_TRUE(coordinator.registerIsland({
            .islandId = islandId,
            .workerId = static_cast<uint32_t>(islandId),
            .loadUnits = 50u,
            .checkpoint = checkpoint(islandId, 1u, 0u),
        }));
    }
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(1u, 1u, 1u, 10u, 0, 10)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(2u, 2u, 1u, 10u, 5, 15)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(3u, 3u, 1u, 10u, 100, 110)));
    const auto initial = coordinator.planMigrations(10u);
    ASSERT_EQ(initial.size(), 1u);
    ASSERT_EQ(initial.front().islandId, 2u);

    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(1u, 1u, 1u, 11u, 0, 10)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(2u, 2u, 1u, 11u, 5, 15)));
    ASSERT_TRUE(coordinator.publishBoundaryProxy(
        proxy(3u, 3u, 1u, 11u, 10, 20)));
    EXPECT_TRUE(coordinator.planMigrations(11u).empty());
    ASSERT_EQ(coordinator.migrations().size(), 1u);
    EXPECT_EQ(coordinator.migrations().front().islandId, 2u);
}

// Keep the pre-optimization enumeration as an independent differential oracle.
std::vector<IslandPair> referenceBoundaryPairs(
    const WorldCoordinator& coordinator,
    std::span<const SweptBoundaryProxy> proxies,
    uint64_t tick, size_t capacity) {
    const auto eligible = [&](const SweptBoundaryProxy& value) {
        const auto descriptor = coordinator.island(value.islandId);
        const auto owner = coordinator.worker(value.workerId);
        const uint64_t end = value.startTick
                > std::numeric_limits<uint64_t>::max() - value.horizonTicks
            ? std::numeric_limits<uint64_t>::max()
            : value.startTick + value.horizonTicks;
        return descriptor && owner && owner->online
            && descriptor->workerId == value.workerId
            && descriptor->authorityEpoch == value.authorityEpoch
            && value.startTick >= descriptor->authorityStartTick
            && tick >= value.startTick && tick <= end;
    };
    std::vector<IslandPair> result;
    for (size_t first = 0; first < proxies.size(); ++first) {
        const auto& lhs = proxies[first];
        if (!eligible(lhs)) continue;
        for (size_t second = first + 1; second < proxies.size(); ++second) {
            const auto& rhs = proxies[second];
            if (!eligible(rhs) || lhs.workerId == rhs.workerId) continue;
            bool overlap = true;
            for (size_t axis = 0; axis < 3; ++axis) {
                overlap &= lhs.maximumQ12[axis] >= rhs.minimumQ12[axis]
                    && rhs.maximumQ12[axis] >= lhs.minimumQ12[axis];
            }
            if (!overlap) continue;
            if (result.size() == capacity) return result;
            result.push_back({lhs.islandId, rhs.islandId});
        }
    }
    return result;
}

TEST(WorldCoordinator, BoundaryPairsMatchOriginalEnumerationAndTruncation) {
    for (const uint32_t capacity : {1u, 7u, 65536u}) {
        WorldCoordinator coordinator({.maximumCrossWorkerPairs = capacity});
        for (uint32_t id = 1; id <= 4; ++id)
            ASSERT_TRUE(coordinator.registerWorker({id, 1000u, 0u, true}));
        std::vector<SweptBoundaryProxy> proxies;
        uint32_t random = 0x12345678u;
        // Publish backwards to exercise canonical insertion order.
        for (uint64_t id = 64; id > 0; --id) {
            const uint32_t workerId = static_cast<uint32_t>(id % 4) + 1;
            ASSERT_TRUE(coordinator.registerIsland({
                .islandId = id, .workerId = workerId, .loadUnits = 1u,
                .checkpoint = checkpoint(id, 1u, 0u),
            }));
            random ^= random << 13u;
            random ^= random >> 17u;
            random ^= random << 5u;
            const int64_t x = static_cast<int64_t>(random % 16u) * 10;
            const uint64_t start = id % 5 == 0
                ? std::numeric_limits<uint64_t>::max() - 2 : 10u;
            auto value = proxy(id, workerId, 1u, start, x, x + 10, 5u);
            value.minimumQ12[1] = static_cast<int64_t>(id % 3) * 100;
            value.maximumQ12[1] = value.minimumQ12[1] + 100;
            ASSERT_TRUE(coordinator.publishBoundaryProxy(value));
            proxies.push_back(value);
        }
        std::reverse(proxies.begin(), proxies.end());
        for (const bool online : {true, false}) {
            ASSERT_TRUE(coordinator.registerWorker({2u, 1000u, 0u, online}));
            for (const uint64_t tick : std::array<uint64_t, 7>{
                     9u, 10u, 12u, 15u, 16u,
                     std::numeric_limits<uint64_t>::max() - 1,
                     std::numeric_limits<uint64_t>::max()}) {
                const auto expected = referenceBoundaryPairs(
                    coordinator, proxies, tick, capacity);
                EXPECT_EQ(coordinator.crossWorkerPairs(tick), expected);
                EXPECT_EQ(coordinator.crossWorkerPairs(tick), expected);
            }
        }
        // Planning/committing changes authority epochs and removes migrated
        // proxies. Stale entries in the reference list are rejected by the
        // same metadata fence, exercising invalidation between queries.
        ASSERT_TRUE(coordinator.registerWorker({2u, 1000u, 0u, true}));
        const auto migrations = coordinator.planMigrations(10u);
        for (const auto& migration : migrations) {
            ASSERT_TRUE(coordinator.beginShadow(migration.islandId));
            ASSERT_TRUE(coordinator.submitShadowHashes(
                migration.islandId, migration.sourceHash, migration.sourceHash));
        }
        static_cast<void>(coordinator.commitMigrations(12u));
        EXPECT_EQ(coordinator.crossWorkerPairs(12u),
                  referenceBoundaryPairs(coordinator, proxies, 12u, capacity));
    }
}

TEST(NativeServerGpu, BatchesWorldsInOneSubmissionWithSharedHashes) {
    constexpr uint32_t worldCount = 3;
    constexpr uint32_t bodyCapacity = 8;
    constexpr uint32_t contactCapacity = 16;
    constexpr uint32_t ticks = 5;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    NativeServerGpuBackend backend;
    ASSERT_TRUE(backend.initialize(
        context.getDevice(), context.getQueue(), {.maximumWorlds = 8u}));
    std::array<ServerWorldHandle, worldCount> handles{};
    std::array<physics::deterministic::LockstepWorld, worldCount> cpuWorlds{};
    std::array<std::vector<LockstepBody>, worldCount> initial{};
    physics::deterministic::LockstepWorld::Config cpuConfig;
    cpuConfig.bodyCapacity = bodyCapacity;
    cpuConfig.contactCapacity = contactCapacity;
    for (uint32_t world = 0; world < worldCount; ++world) {
        physics::deterministic::GpuLockstepWorld::Config gpuConfig;
        gpuConfig.bodyCapacity = bodyCapacity;
        gpuConfig.contactCapacity = contactCapacity;
        const auto handle = backend.createWorld({
            .worldId = world + 1u,
            .islandId = 100u + world,
            .physics = gpuConfig,
        });
        ASSERT_TRUE(handle.has_value());
        handles[world] = *handle;
        initial[world].resize(bodyCapacity);
        initial[world][1] = body(
            1u, static_cast<int32_t>(world) * 8'192, 20'480);
        ASSERT_TRUE(cpuWorlds[world].initialize(cpuConfig));
        ASSERT_TRUE(cpuWorlds[world].setBodies(initial[world]));
        ASSERT_TRUE(backend.uploadBodies(handles[world], initial[world]));
    }
    EXPECT_FALSE(backend.initialize(
        context.getDevice(), context.getQueue(),
        {.maximumWorlds = 65'537u}));
    for (const auto handle : handles) {
        EXPECT_TRUE(backend.descriptor(handle).has_value());
        EXPECT_NE(backend.bodyBuffer(handle), nullptr);
    }

    WGPUSubmissionIndex finalSubmission = 0;
    const std::array reversed{handles[2], handles[1], handles[0]};
    std::array<physics::deterministic::LockstepTelemetry, worldCount>
        cpuTelemetry{};
    for (uint32_t tick = 1; tick <= ticks; ++tick) {
        for (uint32_t world = 0; world < worldCount; ++world)
            cpuTelemetry[world] = cpuWorlds[world].step(tick);
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDescriptor);
        ASSERT_TRUE(backend.encodeBatch(encoder, tick, reversed));
        WGPUCommandBufferDescriptor commandDescriptor{};
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(
            encoder, &commandDescriptor);
        finalSubmission = wgpuQueueSubmitForIndex(
            context.getQueue(), 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
    }

    constexpr size_t bodyBytes = bodyCapacity * sizeof(LockstepBody);
    constexpr size_t telemetryBytes = 32u * sizeof(uint32_t);
    constexpr size_t stride = bodyBytes + telemetryBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "native_server_batch_readback",
            .size = stride * worldCount,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    WGPUCommandEncoderDescriptor encoderDescriptor{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDescriptor);
    for (uint32_t world = 0; world < worldCount; ++world) {
        const size_t offset = stride * world;
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, backend.bodyBuffer(handles[world]), 0u,
            readback, offset, bodyBytes);
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, backend.telemetryBuffer(handles[world]), 0u,
            readback, offset + bodyBytes, telemetryBytes);
    }
    WGPUCommandBufferDescriptor commandDescriptor{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(
        encoder, &commandDescriptor);
    finalSubmission = wgpuQueueSubmitForIndex(context.getQueue(), 1u, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), finalSubmission};
    struct MapState { bool done = false; bool success = false; } map;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& state = *static_cast<MapState*>(userdata);
        state.success = status == WGPUBufferMapAsyncStatus_Success;
        state.done = true;
    };
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0u,
                       stride * worldCount, callback, &map);
    while (!map.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(map.success);
    const auto* bytes = static_cast<const std::byte*>(
        wgpuBufferGetConstMappedRange(readback, 0u, stride * worldCount));
    for (uint32_t world = 0; world < worldCount; ++world) {
        const size_t offset = stride * world;
        EXPECT_EQ(std::memcmp(bytes + offset,
                              cpuWorlds[world].bodies().data(), bodyBytes), 0);
        std::array<uint32_t, 32> telemetryWords{};
        std::memcpy(telemetryWords.data(), bytes + offset + bodyBytes,
                    telemetryBytes);
        const auto gpuTelemetry =
            physics::deterministic::GpuLockstepWorld::decodeTelemetry(
                telemetryWords);
        EXPECT_EQ(gpuTelemetry.tick, ticks);
        EXPECT_EQ(gpuTelemetry.hashes.world,
                  cpuTelemetry[world].hashes.world);
        EXPECT_EQ(gpuTelemetry.hashes.bodyAggregate,
                  cpuTelemetry[world].hashes.bodyAggregate);
        EXPECT_EQ(gpuTelemetry.hashes.contactAggregate,
                  cpuTelemetry[world].hashes.contactAggregate);
        EXPECT_EQ(gpuTelemetry.hashes.islandAggregate,
                  cpuTelemetry[world].hashes.islandAggregate);
    }
    EXPECT_EQ(backend.telemetry().encodedBatches, ticks);
    EXPECT_EQ(backend.telemetry().worldDispatches, ticks * worldCount);
    EXPECT_EQ(backend.telemetry().isolatedSubmissionEquivalent,
              ticks * worldCount);
    EXPECT_EQ(backend.telemetry().avoidedSubmissionIntents,
              ticks * (worldCount - 1u));
    EXPECT_EQ(backend.telemetry().activeWorlds, worldCount);
    EXPECT_EQ(NativeServerGpuBackend::arithmeticSchemaVersion(), 1u);

    wgpuBufferUnmap(readback);
    releaseBuffer(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    const ServerWorldHandle stale = handles[0];
    EXPECT_TRUE(backend.destroyWorld(stale));
    EXPECT_FALSE(backend.destroyWorld(stale));
    backend.shutdown();
}

} // namespace
} // namespace voxy::server
