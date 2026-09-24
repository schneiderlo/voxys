#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/gpu/gpu_broad_phase.hpp"
#include "physics/gpu/gpu_event_readback.hpp"
#include "physics/gpu/gpu_islands.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <optional>
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

namespace voxy::physics {
namespace {

static_assert(sizeof(GpuPhysicsEvent) == 96u);

template <typename T>
WGPUBuffer makeStorage(gpu::Context& context, std::span<const T> values,
                       const char* label) {
    return gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(values.size_bytes(), false, label), values);
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

std::optional<GpuEventBatch> encodeAndPoll(
    gpu::Context& context, GpuEventReadbackRing& ring, uint64_t tick) {
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    if (!ring.encodeReadback(encoder, tick)) {
        wgpuCommandEncoderRelease(encoder);
        return std::nullopt;
    }
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    auto result = ring.poll();
    for (uint32_t attempt = 0; !result && attempt < 64u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
        result = ring.poll();
    }
    return result;
}

TEST(GpuEventReadback, PacksPriorityAndStableKeysThroughAsyncRing) {
    constexpr uint32_t contactCapacity = 4;
    constexpr uint32_t islandCapacity = 4;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::array<GpuContactEvent, contactCapacity * 2u> contacts{};
    contacts[0] = {2u, 5u,
        static_cast<uint32_t>(ContactEventType::Begin), 7u};
    contacts[0].identity = {102u, 105u, 0u, 0u};
    contacts[1] = {4u, 6u,
        static_cast<uint32_t>(ContactEventType::Begin), 8u};
    contacts[1].identity = {104u, 106u, 0u, 0u};
    contacts[contactCapacity] = {
        1u, 3u, static_cast<uint32_t>(ContactEventType::End), 9u};
    contacts[contactCapacity].identity = {71u, 73u, 0u, 0u};
    std::array<uint32_t, 32> contactTelemetry{};
    contactTelemetry[7] = 2u;
    contactTelemetry[8] = 1u;

    std::array<GpuIslandEvent, islandCapacity> islands{};
    islands[0] = {4u, GpuIslandEventType::Sleep, 41u, 3u};
    islands[1] = {9u, GpuIslandEventType::Wake, 41u, 2u};
    std::array<uint32_t, 32> islandTelemetry{};
    islandTelemetry[8] = 2u;

    std::array<GpuContactManifold, 1> manifolds{};
    // Source manifold A is body 12 and B is body 10. Event packing must
    // canonicalize to 10/12, swap both local anchors/features, and invert the
    // A->B normal while retaining summed multi-point evidence.
    manifolds[0].pair = {10u, 12u, 0u, 77u};
    manifolds[0].state[0] = 2u;
    manifolds[0].normal = {1.0f, 0.0f, 0.0f, 0.0f};
    manifolds[0].points[0].localAnchorASeparation =
        {1.0f, 0.0f, 0.0f, -0.10f};
    manifolds[0].points[0].localAnchorBNormalImpulse =
        {10.0f, 0.0f, 0.0f, 2.0f};
    manifolds[0].points[0].features = {42u, 52u, 0u, 0u};
    manifolds[0].points[0].impulses[1] = 3.0f;
    manifolds[0].points[1].localAnchorASeparation =
        {3.0f, 0.0f, 0.0f, -0.20f};
    manifolds[0].points[1].localAnchorBNormalImpulse =
        {14.0f, 0.0f, 0.0f, 3.0f};
    manifolds[0].points[1].features = {43u, 53u, 0u, 0u};
    manifolds[0].points[1].impulses[1] = 5.0f;
    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[11] = 1u;
    std::array<std::array<uint32_t, 4>, 13> metadata{};
    for (uint32_t body = 1u; body < metadata.size(); ++body) {
        metadata[body][3] = packGpuBodyMetadata(
            100u + body, kGpuBodyAliveFlag);
    }

    WGPUBuffer contactBuffer = makeStorage<GpuContactEvent>(
        context, contacts, "event_test_contacts");
    WGPUBuffer contactTelemetryBuffer = makeStorage<uint32_t>(
        context, contactTelemetry, "event_test_contact_telemetry");
    WGPUBuffer islandBuffer = makeStorage<GpuIslandEvent>(
        context, islands, "event_test_islands");
    WGPUBuffer islandTelemetryBuffer = makeStorage<uint32_t>(
        context, islandTelemetry, "event_test_island_telemetry");
    WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "event_test_manifolds");
    WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
        context, narrowTelemetry, "event_test_narrow_telemetry");
    WGPUBuffer metadataBuffer = makeStorage<std::array<uint32_t, 4>>(
        context, metadata, "event_test_metadata");

    const GpuEventSources sources{
        .contactEvents = contactBuffer,
        .contactTelemetry = contactTelemetryBuffer,
        .contactCapacity = contactCapacity,
        .islandEvents = islandBuffer,
        .islandTelemetry = islandTelemetryBuffer,
        .islandEventCapacity = islandCapacity,
        .manifolds = manifoldBuffer,
        .narrowPhaseTelemetry = narrowTelemetryBuffer,
        .manifoldCapacity = static_cast<uint32_t>(manifolds.size()),
        .metadata = metadataBuffer,
        .bodyCapacity = static_cast<uint32_t>(metadata.size()),
    };
    GpuEventReadbackRing ring;
    GpuEventReadbackRing::Config config;
    config.eventCapacity = 8;
    ASSERT_TRUE(ring.initialize(
        context.getDevice(), context.getQueue(), config));
    const WGPUBuffer workingEvents = ring.packedEventBuffer();
    auto invalidConfig = config;
    invalidConfig.eventCapacity = 0u;
    EXPECT_FALSE(ring.initialize(
        context.getDevice(), context.getQueue(), invalidConfig));
    EXPECT_EQ(ring.packedEventBuffer(), workingEvents);
    ring.setSources(sources);
    constexpr uint64_t tick = (uint64_t{3} << 32u) | 41u;
    const auto batch = encodeAndPoll(context, ring, tick);
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->tick, tick);
    EXPECT_FALSE(batch->overflow);
    ASSERT_EQ(batch->events.size(), 6u);
    const std::array<GpuPhysicsEventType, 6> expectedTypes = {
        GpuPhysicsEventType::ContactBegin,
        GpuPhysicsEventType::ContactBegin,
        GpuPhysicsEventType::ContactEnd,
        GpuPhysicsEventType::ContactHit,
        GpuPhysicsEventType::IslandSleep,
        GpuPhysicsEventType::IslandWake,
    };
    for (size_t index = 0; index < expectedTypes.size(); ++index) {
        EXPECT_EQ(batch->events[index].header[0], 41u);
        EXPECT_EQ(batch->events[index].header[1],
                  static_cast<uint32_t>(expectedTypes[index]));
    }
    EXPECT_EQ(batch->events[0].header[2], 2u);
    EXPECT_EQ(batch->events[1].header[2], 4u);
    EXPECT_EQ(batch->events[2].header[2], 1u);
    EXPECT_EQ(batch->events[3].header[2], 10u);
    EXPECT_EQ(batch->events[3].header[3], 12u);
    EXPECT_EQ(batch->events[3].detail[0], 53u);
    EXPECT_EQ(batch->events[3].detail[1], 77u);
    EXPECT_EQ(batch->events[3].detail[2], 2u);
    EXPECT_EQ(batch->events[3].detail[3], 43u);
    EXPECT_EQ(batch->events[0].identity[0], 102u);
    EXPECT_EQ(batch->events[0].identity[1], 105u);
    EXPECT_EQ(batch->events[2].identity[0], 71u);
    EXPECT_EQ(batch->events[2].identity[1], 73u);
    EXPECT_EQ(batch->events[3].identity[0], 110u);
    EXPECT_EQ(batch->events[3].identity[1], 112u);
    EXPECT_FLOAT_EQ(
        batch->events[3].localAnchorASeparation[0], 12.4f);
    EXPECT_FLOAT_EQ(
        batch->events[3].localAnchorASeparation[3], -0.16f);
    EXPECT_FLOAT_EQ(
        batch->events[3].localAnchorBImpulse[0], 2.2f);
    EXPECT_FLOAT_EQ(
        batch->events[3].localAnchorBImpulse[3], 5.0f);
    EXPECT_EQ(
        glm::vec3(
            batch->events[3].normalSpeed[0],
            batch->events[3].normalSpeed[1],
            batch->events[3].normalSpeed[2]),
        glm::vec3(-1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(batch->events[3].normalSpeed[3], 5.0f);
    EXPECT_EQ(batch->events[4].header[2], 4u);
    EXPECT_EQ(batch->events[4].detail[2], 3u);
    EXPECT_EQ(batch->events[5].header[2], 9u);
    EXPECT_EQ(batch->events[4].identity[0], 104u);
    EXPECT_EQ(batch->events[5].identity[0], 109u);
    EXPECT_GE(
        ring.allocatedBytes(),
        16u + 8u * sizeof(GpuPhysicsEvent));
    ring.shutdown();

    GpuEventReadbackRing limited;
    config.eventCapacity = 3;
    ASSERT_TRUE(limited.initialize(
        context.getDevice(), context.getQueue(), config));
    limited.setSources(sources);
    const auto truncated = encodeAndPoll(context, limited, 42u);
    ASSERT_TRUE(truncated.has_value());
    EXPECT_TRUE(truncated->overflow);
    ASSERT_EQ(truncated->events.size(), 3u);
    EXPECT_EQ(truncated->events[0].header[1],
              static_cast<uint32_t>(GpuPhysicsEventType::ContactBegin));
    EXPECT_EQ(truncated->events[2].header[1],
              static_cast<uint32_t>(GpuPhysicsEventType::ContactEnd));
    limited.shutdown();

    releaseBuffer(metadataBuffer);
    releaseBuffer(narrowTelemetryBuffer);
    releaseBuffer(manifoldBuffer);
    releaseBuffer(islandTelemetryBuffer);
    releaseBuffer(islandBuffer);
    releaseBuffer(contactTelemetryBuffer);
    releaseBuffer(contactBuffer);
}

TEST(GpuEventReadback, CountedPacketsCopyOnlyLiveRecordsAndPreserveMalformedHeaders) {
    gpu::Context context;
    gpu::ContextConfig config;
    config.enableValidation = true;
    if (!context.initHeadless(config)) GTEST_SKIP() << "Headless WebGPU is unavailable";
    context.setErrorCallback([](WGPUErrorType, const char* message) { ADD_FAILURE() << message; });
    constexpr size_t headerBytes = 16u, recordBytes = 96u, capacity = 5u;
    std::array<uint32_t, (headerBytes + recordBytes * capacity) / 4u> packet{};
    for (size_t i = 0; i < packet.size(); ++i) packet[i] = static_cast<uint32_t>(i + 17u);
    auto source = makeStorage<uint32_t>(context, packet, "counted_packet");
    ASSERT_NE(source, nullptr);
    DebugReadbackRing ring;
    ASSERT_TRUE(ring.initialize(context.getDevice(), 1u, sizeof(packet)));
    const auto read = [&](std::optional<CountedReadbackLayout> layout) {
        EXPECT_TRUE(gpu::writeBuffer(context.getQueue(), source, 0, packet));
        WGPUCommandEncoderDescriptor encoderDesc{};
        auto encoder = wgpuDeviceCreateCommandEncoder(context.getDevice(), &encoderDesc);
        EXPECT_TRUE(ring.encodeCopy(encoder, source, 0, sizeof(packet), 42u, 3u, 5u,
                                   std::nullopt, 91u));
        WGPUCommandBufferDescriptor commandDesc{};
        auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        const auto index = wgpuQueueSubmitForIndex(context.getQueue(), 1, &command);
        const WGPUWrappedSubmissionIndex submission{context.getQueue(), index};
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        auto result = ring.poll(layout);
        for (uint32_t attempt = 0; !result && ring.availableSlots() == 0u && attempt < 64u; ++attempt) {
            static_cast<void>(wgpuDevicePoll(context.getDevice(), true, &submission));
            result = ring.poll(layout);
        }
        return result;
    };
    for (const uint32_t count : {0u, 1u, 5u, std::numeric_limits<uint32_t>::max(), 0u}) {
        packet[0] = count;
        const auto result = read(CountedReadbackLayout{headerBytes, recordBytes});
        ASSERT_TRUE(result);
        EXPECT_EQ(result->tick, 42u);
        EXPECT_EQ(result->submissionSerial, 91u);
        EXPECT_EQ(result->firstBody, 3u);
        EXPECT_EQ(result->bodyCount, 5u);
        EXPECT_EQ(result->bytes.size(), headerBytes + std::min(size_t{count}, capacity) * recordBytes);
        EXPECT_EQ(std::memcmp(result->bytes.data(), packet.data(), result->bytes.size()), 0);
        EXPECT_EQ(ring.availableSlots(), 1u);
    }
    // A bad caller layout fails safely and releases the slot. Full snapshots
    // still use their complete range, even with a zero first word.
    EXPECT_FALSE(read(CountedReadbackLayout{12u, recordBytes}));
    EXPECT_EQ(ring.failedReadbacks(), 1u);
    const auto full = read(std::nullopt);
    ASSERT_TRUE(full);
    EXPECT_EQ(full->bytes.size(), sizeof(packet));
    EXPECT_EQ(std::memcmp(full->bytes.data(), packet.data(), sizeof(packet)), 0);
    ring.shutdown();
    releaseBuffer(source);
}

TEST(GpuEventReadback, BatchedCopiesKeepTheirOwnTickParameters) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    std::array<GpuContactEvent, 2> contacts{};
    contacts[0] = {2u, 5u,
        static_cast<uint32_t>(ContactEventType::Begin), 7u};
    std::array<uint32_t, 32> telemetry{};
    telemetry[7] = 1u;
    WGPUBuffer contactBuffer = makeStorage<GpuContactEvent>(
        context, contacts, "batched_event_contacts");
    WGPUBuffer telemetryBuffer = makeStorage<uint32_t>(
        context, telemetry, "batched_event_telemetry");
    GpuEventReadbackRing ring;
    GpuEventReadbackRing::Config config;
    config.eventCapacity = 1u;
    config.readbackSlots = 3u;
    ASSERT_TRUE(ring.initialize(context.getDevice(), context.getQueue(), config));
    ring.setSources({
        .contactEvents = contactBuffer,
        .contactTelemetry = telemetryBuffer,
        .contactCapacity = 1u,
    });

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    for (uint64_t tick = 41u; tick < 44u; ++tick) {
        ASSERT_TRUE(ring.encodeReadback(encoder, tick));
    }
    // A full-ring attempt must not overwrite any already encoded parameters.
    EXPECT_FALSE(ring.encodeReadback(encoder, 99u));
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{context.getQueue(), submissionIndex};
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    for (uint64_t tick = 41u; tick < 44u; ++tick) {
        auto batch = ring.poll();
        for (uint32_t attempt = 0u; !batch && attempt < 64u; ++attempt) {
            static_cast<void>(wgpuDevicePoll(context.getDevice(), true, &submission));
            batch = ring.poll();
        }
        ASSERT_TRUE(batch.has_value());
        EXPECT_EQ(batch->tick, tick);
        ASSERT_EQ(batch->events.size(), 1u);
        EXPECT_EQ(batch->events[0].header[0], tick);
    }
    const auto reused = encodeAndPoll(context, ring, 44u);
    ASSERT_TRUE(reused.has_value());
    ASSERT_EQ(reused->events.size(), 1u);
    EXPECT_EQ(reused->events[0].header[0], 44u);
    ring.shutdown();
    releaseBuffer(telemetryBuffer);
    releaseBuffer(contactBuffer);
}

} // namespace
} // namespace voxy::physics
