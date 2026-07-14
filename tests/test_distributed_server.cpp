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
    uint64_t tick, int64_t minimumX, int64_t maximumX) {
    return {
        .islandId = islandId,
        .workerId = worker,
        .authorityEpoch = epoch,
        .startTick = tick,
        .horizonTicks = 5,
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
