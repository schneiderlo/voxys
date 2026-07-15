#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_islands.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include <glm/vec4.hpp>

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

namespace voxy::physics {
namespace {

struct alignas(16) TestPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};
struct alignas(16) TestMotion {
    glm::vec4 linearVelocitySleep{0.0f};
    glm::vec4 angularVelocityFlags{0.0f};
};
using TestMetadata = std::array<uint32_t, 4>;

TestMetadata awakeMetadata(int32_t sectorX = 0) {
    return {static_cast<uint32_t>(sectorX), 0u, 0u,
            packGpuBodyMetadata(
                1u, kGpuBodyAliveFlag | kGpuBodyAwakeFlag)};
}

struct IslandSnapshot {
    GpuIslandTelemetry telemetry;
    std::vector<uint32_t> roots;
    std::vector<TestMetadata> metadata;
    std::vector<GpuIslandRecord> islands;
    std::vector<GpuKeyValue> sleepingGrid;
    std::vector<GpuSleepingCellRange> sleepingRanges;
    std::vector<GpuIslandEvent> events;
};

template <typename T>
WGPUBuffer makeStorage(gpu::Context& context, std::span<const T> values,
                       const char* label) {
    return gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(values.size_bytes(), false, label), values);
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (buffer) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }
}

IslandSnapshot runAndRead(gpu::Context& context, GpuIslandManager& manager,
                          WGPUBuffer metadataBuffer, uint32_t bodyCapacity,
                          uint32_t eventCapacity,
                          bool compactSmallWorld = false) {
    constexpr size_t telemetryBytes = 32u * sizeof(uint32_t);
    const size_t rootBytes = size_t{bodyCapacity} * sizeof(uint32_t);
    const size_t metadataBytes = size_t{bodyCapacity} * sizeof(TestMetadata);
    const size_t islandBytes = size_t{bodyCapacity} * sizeof(GpuIslandRecord);
    const size_t gridBytes = size_t{bodyCapacity} * sizeof(GpuKeyValue);
    const size_t rangeBytes = size_t{bodyCapacity} * sizeof(GpuSleepingCellRange);
    const size_t eventBytes = size_t{eventCapacity} * sizeof(GpuIslandEvent);
    const size_t totalBytes = telemetryBytes + rootBytes + metadataBytes
        + islandBytes + gridBytes + rangeBytes + eventBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "island_test_readback",
            .size = totalBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    EXPECT_TRUE(manager.encode(encoder, compactSmallWorld));
    size_t offset = 0;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manager.telemetryBuffer(), 0, readback, offset, telemetryBytes);
    offset += telemetryBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manager.bodyRoots(), 0, readback, offset, rootBytes);
    offset += rootBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, metadataBuffer, 0, readback, offset, metadataBytes);
    offset += metadataBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manager.islands(), 0, readback, offset, islandBytes);
    offset += islandBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manager.sleepingGridEntries(), 0, readback, offset, gridBytes);
    offset += gridBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manager.sleepingCellRanges(), 0, readback, offset, rangeBytes);
    offset += rangeBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manager.events(), 0, readback, offset, eventBytes);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    struct MapState { bool done = false; bool success = false; } state;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& map = *static_cast<MapState*>(userdata);
        map.success = status == WGPUBufferMapAsyncStatus_Success;
        map.done = true;
    };
    wgpuBufferMapAsync(
        readback, WGPUMapMode_Read, 0, totalBytes, callback, &state);
    while (!state.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    EXPECT_TRUE(state.success);
    IslandSnapshot result;
    if (state.success) {
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
        offset = 0;
        std::array<uint32_t, 32> telemetry{};
        std::memcpy(telemetry.data(), bytes + offset, telemetryBytes);
        result.telemetry = GpuIslandManager::decodeTelemetry(telemetry);
        offset += telemetryBytes;
        result.roots.resize(bodyCapacity);
        std::memcpy(result.roots.data(), bytes + offset, rootBytes);
        offset += rootBytes;
        result.metadata.resize(bodyCapacity);
        std::memcpy(result.metadata.data(), bytes + offset, metadataBytes);
        offset += metadataBytes;
        result.islands.resize(bodyCapacity);
        std::memcpy(result.islands.data(), bytes + offset, islandBytes);
        offset += islandBytes;
        result.sleepingGrid.resize(bodyCapacity);
        std::memcpy(result.sleepingGrid.data(), bytes + offset, gridBytes);
        offset += gridBytes;
        result.sleepingRanges.resize(bodyCapacity);
        std::memcpy(result.sleepingRanges.data(), bytes + offset, rangeBytes);
        offset += rangeBytes;
        result.events.resize(eventCapacity);
        std::memcpy(result.events.data(), bytes + offset, eventBytes);
    }
    wgpuBufferUnmap(readback);
    releaseBuffer(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    return result;
}

class GpuIslandTest : public ::testing::TestWithParam<uint32_t> {};

TEST(GpuIslandSectorTest, RejectsCellSizeThatDoesNotDivideSector) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    GpuIslandManager islands;
    GpuIslandManager::Config config;
    config.bodyCapacity = 8;
    config.contactCapacity = 8;
    config.eventCapacity = 8;
    config.sleepingCellSize = 3.0f;
    EXPECT_FALSE(islands.initialize(
        context.getDevice(), context.getQueue(), config));
}

TEST_P(GpuIslandTest, CompactsSleepsWakesAndMaintainsSleepingGrid) {
    constexpr uint32_t bodyCapacity = 32;
    constexpr uint32_t contactCapacity = 32;
    constexpr uint32_t eventCapacity = 32;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestMotion> motions(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    std::vector<GpuContactManifold> manifolds(contactCapacity);
    const auto addBody = [&](uint32_t body, float x, int32_t sectorX) {
        poses[body].positionInvMass = {x, 0.0f, 0.0f, 1.0f};
        metadata[body] = awakeMetadata(sectorX);
    };
    for (uint32_t body = 1u; body <= 8u; ++body)
        addBody(body, float(body), 1'500'000);
    for (uint32_t body = 10u; body <= 12u; ++body)
        addBody(body, float(body), -1'500'000);
    addBody(20u, 20.0f, std::numeric_limits<int32_t>::min());
    motions[11].linearVelocitySleep = {1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t contactCount = 0;
    const auto addContact = [&](uint32_t bodyA, uint32_t bodyB) {
        auto& manifold = manifolds[contactCount];
        manifold.pair = {bodyB, bodyA, contactCount, contactCount};
        manifold.state = {1u, 0u, 0u, 0u};
        ++contactCount;
    };
    for (uint32_t body = 1u; body < 8u; ++body)
        addContact(body, body + 1u);
    addContact(10u, 11u);
    addContact(11u, 12u);
    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[10] = contactCount;
    narrowTelemetry[11] = contactCount;

    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "island_poses");
    WGPUBuffer motionBuffer = makeStorage<TestMotion>(
        context, motions, "island_motions");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "island_metadata");
    WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "island_manifolds");
    WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
        context, narrowTelemetry, "island_narrow_telemetry");

    GpuIslandManager manager;
    GpuIslandManager::Config config;
    config.bodyCapacity = bodyCapacity;
    config.contactCapacity = contactCapacity;
    config.eventCapacity = eventCapacity;
    config.workgroupSize = GetParam();
    config.unionRounds = 16;
    config.sleepTicks = 3;
    config.linearSleepThreshold = 0.05f;
    config.angularSleepThreshold = 0.05f;
    ASSERT_TRUE(manager.initialize(
        context.getDevice(), context.getQueue(), config));
    manager.setInput({poseBuffer, motionBuffer, metadataBuffer,
                      manifoldBuffer, narrowTelemetryBuffer,
                      bodyCapacity, contactCapacity});

    IslandSnapshot snapshot;
    for (uint32_t tick = 0; tick < 4u; ++tick) {
        snapshot = runAndRead(
            context, manager, metadataBuffer, bodyCapacity, eventCapacity, true);
    }
    EXPECT_EQ(manager.inputBindGroupCacheMisses(), 3u);
    EXPECT_EQ(snapshot.telemetry.tick, 4u);
    EXPECT_EQ(snapshot.telemetry.islandCount, 3u);
    EXPECT_EQ(snapshot.telemetry.maximumIslandBodies, 8u);
    EXPECT_EQ(snapshot.telemetry.rootErrors, 0u);
    EXPECT_EQ(snapshot.telemetry.sleepingIslands, 2u);
    EXPECT_EQ(snapshot.telemetry.sleepingBodies, 9u);
    EXPECT_EQ(snapshot.telemetry.awakeBodies, 3u);
    EXPECT_EQ(snapshot.telemetry.sleepTransitions, 2u);
    EXPECT_EQ(snapshot.telemetry.events, 2u);
    EXPECT_EQ(snapshot.telemetry.sleepingGridEntries, 9u);
    EXPECT_FALSE(snapshot.telemetry.eventOverflow);
    EXPECT_FALSE(snapshot.telemetry.gridOverflow);
    for (uint32_t body = 1u; body <= 8u; ++body) {
        EXPECT_EQ(snapshot.roots[body], 1u);
        EXPECT_EQ(snapshot.metadata[body][3] & kGpuBodyAwakeFlag, 0u);
    }
    for (uint32_t body = 10u; body <= 12u; ++body) {
        EXPECT_EQ(snapshot.roots[body], 10u);
        EXPECT_NE(snapshot.metadata[body][3] & kGpuBodyAwakeFlag, 0u);
    }
    EXPECT_EQ(snapshot.roots[20], 20u);
    ASSERT_EQ(snapshot.events[0].rootBody, 1u);
    ASSERT_EQ(snapshot.events[0].type, GpuIslandEventType::Sleep);
    ASSERT_EQ(snapshot.events[1].rootBody, 20u);
    ASSERT_EQ(snapshot.events[1].type, GpuIslandEventType::Sleep);

    const TestMetadata wakeBody = awakeMetadata(1'500'000);
    gpu::writeBuffer(context.getQueue(), metadataBuffer,
                     uint64_t{4u} * sizeof(TestMetadata), wakeBody);
    snapshot = runAndRead(
        context, manager, metadataBuffer, bodyCapacity, eventCapacity, true);
    EXPECT_EQ(snapshot.telemetry.wakeTransitions, 1u);
    EXPECT_EQ(snapshot.telemetry.events, 1u);
    EXPECT_EQ(snapshot.events[0].rootBody, 1u);
    EXPECT_EQ(snapshot.events[0].type, GpuIslandEventType::Wake);
    EXPECT_EQ(snapshot.telemetry.sleepingBodies, 1u);
    for (uint32_t body = 1u; body <= 8u; ++body)
        EXPECT_NE(snapshot.metadata[body][3] & kGpuBodyAwakeFlag, 0u);

    for (uint32_t tick = 0; tick < 3u; ++tick) {
        snapshot = runAndRead(
            context, manager, metadataBuffer, bodyCapacity, eventCapacity, true);
    }
    EXPECT_EQ(snapshot.telemetry.sleepingBodies, 9u);
    manifolds[3].state[0] = 0u;
    gpu::writeBuffer(context.getQueue(), manifoldBuffer, 0,
        std::as_bytes(std::span<const GpuContactManifold>(manifolds)));
    snapshot = runAndRead(
        context, manager, metadataBuffer, bodyCapacity, eventCapacity, true);
    EXPECT_EQ(snapshot.telemetry.islandCount, 4u);
    EXPECT_EQ(snapshot.telemetry.wakeTransitions, 2u);
    EXPECT_EQ(snapshot.telemetry.sleepingBodies, 1u);
    EXPECT_EQ(snapshot.telemetry.sleepingGridEntries, 1u);
    EXPECT_EQ(snapshot.events[0].rootBody, 1u);
    EXPECT_EQ(snapshot.events[1].rootBody, 5u);

    WGPUBuffer alternateManifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "island_alternate_manifolds");
    ASSERT_NE(alternateManifoldBuffer, nullptr);
    manager.setInput({poseBuffer, motionBuffer, metadataBuffer,
                      alternateManifoldBuffer, narrowTelemetryBuffer,
                      bodyCapacity, contactCapacity});
    static_cast<void>(runAndRead(
        context, manager, metadataBuffer, bodyCapacity, eventCapacity, true));
    EXPECT_EQ(manager.inputBindGroupCacheMisses(), 6u);
    manager.setInput({poseBuffer, motionBuffer, metadataBuffer,
                      manifoldBuffer, narrowTelemetryBuffer,
                      bodyCapacity, contactCapacity});
    static_cast<void>(runAndRead(
        context, manager, metadataBuffer, bodyCapacity, eventCapacity, true));
    EXPECT_EQ(manager.inputBindGroupCacheMisses(), 6u);

    manager.setInput({});
    releaseBuffer(alternateManifoldBuffer);
    releaseBuffer(narrowTelemetryBuffer);
    releaseBuffer(manifoldBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(motionBuffer);
    releaseBuffer(poseBuffer);
}

INSTANTIATE_TEST_SUITE_P(
    WorkgroupProfiles, GpuIslandTest,
    ::testing::Values(64u, 128u, 256u));

} // namespace
} // namespace voxy::physics
