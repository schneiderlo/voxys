#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_queries.hpp"
#include "physics/physics_types.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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

TEST(DebugReadbackRing, RejectsOverflowingTotalAllocation) {
    DebugReadbackRing ring;
    const auto device = reinterpret_cast<WGPUDevice>(uintptr_t{1});
    EXPECT_FALSE(ring.initialize(
        device, 2u, std::numeric_limits<size_t>::max()));
    EXPECT_EQ(ring.allocatedBytes(), 0u);
}

struct alignas(16) TestPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) TestShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 properties{0.0f};
    glm::vec4 material{-1.0f, -1.0f, -1.0f, 1.0f};
};

using TestMetadata = std::array<uint32_t, 4>;

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

std::optional<GpuQueryBatchResult> executeBatch(
    gpu::Context& context, GpuAsyncQuerySystem& queries,
    std::span<const GpuQueryRequest> requests, uint64_t tick) {
    if (!queries.submit(requests, tick)) return std::nullopt;
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    if (!queries.encode(encoder)) {
        wgpuCommandEncoderRelease(encoder);
        return std::nullopt;
    }
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(
        encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    auto result = queries.poll();
    for (uint32_t attempt = 0; !result && attempt < 64u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
        result = queries.poll();
    }
    return result;
}

GpuQueryRequest makeRequest(uint32_t id, GpuQueryType type,
                            uint32_t maximumHits) {
    GpuQueryRequest request;
    request.ids = {id, static_cast<uint32_t>(type), maximumHits, 0u};
    request.directionDistance = {1.0f, 0.0f, 0.0f, 50.0f};
    request.dimensions = {0.0f, 1.0f, 0.0f, 0.5f};
    return request;
}

TEST(GpuAsyncQueries, RejectsRequestsBeyondDeviceCapacity) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    GpuAsyncQuerySystem queries;
    GpuAsyncQuerySystem::Config config;
    config.bodyCapacity = 2u;
    config.requestCapacity = std::numeric_limits<uint32_t>::max();
    config.readbackSlots = DebugReadbackRing::kMaximumSlots;
    EXPECT_FALSE(queries.initialize(context.getDevice(), context.getQueue(), config));
    EXPECT_EQ(queries.allocatedBytes(), 0u);
}

TEST(GpuAsyncQueries, EncodedBatchesKeepTheirOwnRequests) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    std::array<TestPose, 2> poses{};
    std::array<TestShape, 2> shapes{};
    std::array<TestMetadata, 2> metadata{};
    poses[1].positionInvMass = {5.0f, 0.0f, 0.0f, 1.0f};
    shapes[1].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
    metadata[1][3] = packGpuBodyMetadata(1u, kGpuBodyAliveFlag);
    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "batch_query_poses");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(context, shapes, "batch_query_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(context, metadata, "batch_query_metadata");
    GpuAsyncQuerySystem queries;
    GpuAsyncQuerySystem::Config config;
    config.bodyCapacity = 2u;
    config.requestCapacity = 3u;
    config.readbackSlots = 3u;
    ASSERT_TRUE(queries.initialize(context.getDevice(), context.getQueue(), config));
    queries.setBodyView({poseBuffer, shapeBuffer, metadataBuffer, 2u});
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    for (uint32_t batch = 1u; batch <= 3u; ++batch) {
        std::array<GpuQueryRequest, 3> requests{};
        for (uint32_t index = 0u; index < batch; ++index) {
            requests[index] = makeRequest(batch * 100u + index, GpuQueryType::RayCast, 1u);
        }
        ASSERT_TRUE(queries.submit(std::span(requests.data(), batch), batch));
        ASSERT_TRUE(queries.encode(encoder));
    }
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1u, &command);
    const WGPUWrappedSubmissionIndex submission{context.getQueue(), submissionIndex};
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    for (uint32_t batch = 1u; batch <= 3u; ++batch) {
        auto result = queries.poll();
        for (uint32_t attempt = 0u; !result && attempt < 64u; ++attempt) {
            static_cast<void>(wgpuDevicePoll(context.getDevice(), true, &submission));
            result = queries.poll();
        }
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->tick, batch);
        ASSERT_EQ(result->outputs.size(), batch);
        for (uint32_t index = 0u; index < batch; ++index) {
            EXPECT_EQ(result->outputs[index].header[0], batch * 100u + index);
            EXPECT_EQ(result->outputs[index].header[1], 1u);
        }
    }
    const std::array reuse{makeRequest(400u, GpuQueryType::RayCast, 1u)};
    const auto result = executeBatch(context, queries, reuse, 4u);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->outputs.size(), 1u);
    EXPECT_EQ(result->outputs[0].header[0], 400u);
    queries.shutdown();
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST(GpuAsyncQueries, SortsBatchesAndReportsOverflowWithoutBlockingPhysics) {
    constexpr uint32_t bodyCapacity = 24;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    for (uint32_t body = 1; body <= 20; ++body) {
        poses[body].positionInvMass = {
            2.0f * float(body), 0.0f, 0.0f, 1.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
    }
    shapes[2].dimensionsType.w = 2.0f; // box
    shapes[3].dimensionsType = {1.0f, 2.0f, 1.0f, 3.0f}; // capsule
    metadata[1][3] = packGpuBodyMetadata(1u, kGpuBodyAliveFlag);
    metadata[2][3] = packGpuBodyMetadata(1u, kGpuBodyAliveFlag);
    metadata[3][3] = packGpuBodyMetadata(1u, kGpuBodyAliveFlag);

    WGPUBuffer poseBuffer = makeStorage<TestPose>(
        context, poses, "query_test_poses");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(
        context, shapes, "query_test_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "query_test_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuAsyncQuerySystem queries;
    GpuAsyncQuerySystem::Config config;
    config.bodyCapacity = bodyCapacity;
    config.requestCapacity = 8;
    config.readbackSlots = 3;
    ASSERT_TRUE(queries.initialize(
        context.getDevice(), context.getQueue(), config));
    const WGPUBuffer workingOutput = queries.outputBuffer();
    auto invalidConfig = config;
    invalidConfig.requestCapacity = 0u;
    EXPECT_FALSE(queries.initialize(
        context.getDevice(), context.getQueue(), invalidConfig));
    EXPECT_EQ(queries.outputBuffer(), workingOutput);
    queries.setBodyView({poseBuffer, shapeBuffer, metadataBuffer,
                         bodyCapacity});

    std::array<GpuQueryRequest, 4> requests = {
        makeRequest(100u, GpuQueryType::RayCast, 16u),
        makeRequest(101u, GpuQueryType::OverlapSphere, 16u),
        makeRequest(102u, GpuQueryType::SphereCast, 16u),
        makeRequest(103u, GpuQueryType::CapsuleCast, 16u),
    };
    requests[1].originRadius = {2.0f, 0.0f, 0.0f, 0.1f};
    requests[2].originRadius[3] = 0.25f;
    requests[3].originRadius[3] = 0.25f;

    const auto first = executeBatch(context, queries, requests, 77u);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->tick, 77u);
    ASSERT_EQ(first->outputs.size(), requests.size());
    for (size_t query = 0; query < requests.size(); ++query) {
        EXPECT_EQ(first->outputs[query].header[0], requests[query].ids[0]);
        EXPECT_EQ(first->outputs[query].header[3], requests[query].ids[1]);
        EXPECT_EQ(first->outputs[query].header[2], 0u);
    }
    ASSERT_EQ(first->outputs[0].header[1], 3u);
    EXPECT_EQ(first->outputs[0].hits[0].ids[1], 1u);
    EXPECT_EQ(first->outputs[0].hits[1].ids[1], 2u);
    EXPECT_EQ(first->outputs[0].hits[2].ids[1], 3u);
    EXPECT_NEAR(first->outputs[0].hits[0].metricDistance[0], 0.03f, 1e-5f);
    ASSERT_EQ(first->outputs[1].header[1], 1u);
    EXPECT_EQ(first->outputs[1].hits[0].ids[1], 1u);
    for (const auto& output : first->outputs) {
        for (uint32_t hit = 0; hit < output.header[1]; ++hit) {
            EXPECT_TRUE(std::isfinite(output.hits[hit].metricDistance[0]));
            if (hit != 0u) {
                EXPECT_LE(output.hits[hit - 1u].metricDistance[0],
                          output.hits[hit].metricDistance[0]);
            }
        }
    }

    for (uint32_t body = 1; body <= 20; ++body) {
        metadata[body][3] = packGpuBodyMetadata(1u, kGpuBodyAliveFlag);
    }
    gpu::writeBuffer(context.getQueue(), metadataBuffer, 0,
                     std::span<const TestMetadata>(metadata));
    GpuQueryRequest limited = makeRequest(
        200u, GpuQueryType::RayCast, 2u);
    const auto second = executeBatch(
        context, queries, std::span<const GpuQueryRequest>(&limited, 1), 78u);
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->outputs.size(), 1u);
    EXPECT_EQ(second->tick, 78u);
    EXPECT_EQ(second->outputs[0].header[1], 2u);
    EXPECT_EQ(second->outputs[0].header[2], 1u);
    EXPECT_EQ(second->outputs[0].hits[0].ids[1], 1u);
    EXPECT_EQ(second->outputs[0].hits[1].ids[1], 2u);
    EXPECT_GT(queries.allocatedBytes(), 0u);

    queries.shutdown();
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST(GpuAsyncQueries, CastsAcrossMultipleLargeWorldSectors) {
    constexpr uint32_t bodyCapacity = 3;
    constexpr int32_t querySectorX = 1'500'000;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::array<TestPose, bodyCapacity> poses{};
    std::array<TestShape, bodyCapacity> shapes{};
    std::array<TestMetadata, bodyCapacity> metadata{};
    poses[1].positionInvMass = {0.0f, 0.0f, 0.0f, 1.0f};
    poses[2].positionInvMass = poses[1].positionInvMass;
    shapes[1].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
    shapes[2].dimensionsType = shapes[1].dimensionsType;
    metadata[1] = {
        static_cast<uint32_t>(querySectorX + 2), 0u, 0u,
        packGpuBodyMetadata(1u, kGpuBodyAliveFlag),
    };
    metadata[2] = {
        0u, 0u, 0u, packGpuBodyMetadata(1u, kGpuBodyAliveFlag),
    };

    WGPUBuffer poseBuffer = makeStorage<TestPose>(
        context, poses, "long_query_poses");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(
        context, shapes, "long_query_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "long_query_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuAsyncQuerySystem queries;
    GpuAsyncQuerySystem::Config config;
    config.bodyCapacity = bodyCapacity;
    config.requestCapacity = 1;
    ASSERT_TRUE(queries.initialize(
        context.getDevice(), context.getQueue(), config));
    queries.setBodyView({poseBuffer, shapeBuffer, metadataBuffer,
                         bodyCapacity});
    GpuQueryRequest request = makeRequest(
        300u, GpuQueryType::RayCast, 4u);
    request.directionDistance[3] = 600.0f;
    request.sector = {querySectorX, 0, 0, 4};

    const auto result = executeBatch(
        context, queries, std::span<const GpuQueryRequest>(&request, 1), 90u);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->outputs.size(), 1u);
    ASSERT_EQ(result->outputs[0].header[1], 1u);
    EXPECT_EQ(result->outputs[0].hits[0].ids[1], 1u);
    EXPECT_NEAR(result->outputs[0].hits[0].metricDistance[1],
                511.5f, 1e-4f);
    EXPECT_NEAR(result->outputs[0].hits[0].point[0], 511.5f, 1e-4f);

    queries.shutdown();
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST(GpuAsyncQueries, UsesExactShapeGeometryAxisFiltersAndGenerations) {
    constexpr uint32_t bodyCapacity = 6;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::array<TestPose, bodyCapacity> poses{};
    std::array<TestShape, bodyCapacity> shapes{};
    std::array<TestMetadata, bodyCapacity> metadata{};
    const auto initializeBody = [&](uint32_t body, const glm::vec4& pose,
                                    const glm::vec4& shape,
                                    uint32_t generation, uint32_t flags) {
        poses[body].positionInvMass = pose;
        shapes[body].dimensionsType = shape;
        metadata[body][3] = packGpuBodyMetadata(
            generation, kGpuBodyAliveFlag | flags);
    };
    initializeBody(1u, {5.0f, 0.0f, 0.0f, 1.0f},
                   {1.0f, 4.0f, 1.0f, 3.0f}, 11u,
                   kGpuBodyAwakeFlag);
    initializeBody(2u, {5.0f, 10.0f, 0.0f, 1.0f},
                   {2.0f, 4.0f, 2.0f, 4.0f}, 12u,
                   kGpuBodyAwakeFlag);
    initializeBody(3u, {5.0f, 20.0f, 0.0f, 1.0f},
                   {2.0f, 2.0f, 2.0f, 2.0f}, 13u,
                   kGpuBodyAwakeFlag);
    initializeBody(4u, {5.0f, 30.0f, 0.0f, 1.0f},
                   {1.0f, 1.0f, 1.0f, 0.0f}, 14u,
                   kGpuBodyAwakeFlag | kGpuBodyBulletFlag);
    initializeBody(5u, {10.0f, 40.0f, 0.0f, 0.0f},
                   {10.0f, 0.2f, 0.2f, 2.0f}, 15u, 0u);

    WGPUBuffer poseBuffer = makeStorage<TestPose>(
        context, poses, "exact_query_poses");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(
        context, shapes, "exact_query_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "exact_query_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuAsyncQuerySystem queries;
    GpuAsyncQuerySystem::Config config;
    config.bodyCapacity = bodyCapacity;
    config.requestCapacity = 16u;
    ASSERT_TRUE(queries.initialize(
        context.getDevice(), context.getQueue(), config));
    queries.setBodyView({poseBuffer, shapeBuffer, metadataBuffer,
                         bodyCapacity});

    std::array<GpuQueryRequest, 12> requests{};
    requests[0] = makeRequest(400u, GpuQueryType::RayCast, 4u);
    requests[0].originRadius = {0.0f, 0.0f, 1.0f, 0.0f};
    requests[1] = makeRequest(401u, GpuQueryType::RayCast, 4u);
    requests[1].originRadius = {0.0f, 0.0f, 0.0f, 0.0f};
    requests[2] = makeRequest(402u, GpuQueryType::RayCast, 4u);
    requests[2].originRadius = {0.0f, 12.3f, 0.0f, 0.0f};
    requests[3] = makeRequest(403u, GpuQueryType::RayCast, 4u);
    requests[3].originRadius = {0.0f, 10.0f, 0.0f, 0.0f};
    requests[4] = makeRequest(404u, GpuQueryType::SphereCast, 4u);
    requests[4].originRadius = {0.0f, 21.4f, 1.4f, 0.5f};
    requests[5] = makeRequest(405u, GpuQueryType::SphereCast, 4u);
    requests[5].originRadius = {0.0f, 20.0f, 0.0f, 0.5f};
    requests[6] = makeRequest(406u, GpuQueryType::CapsuleCast, 4u);
    requests[6].originRadius = {0.0f, 28.0f, 0.0f, 0.25f};
    requests[6].dimensions = {0.0f, 1.0f, 0.0f, 2.0f};
    requests[7] = requests[6];
    requests[7].ids[0] = 407u;
    requests[7].dimensions = {0.0f, 0.0f, 1.0f, 2.0f};
    requests[8] = makeRequest(408u, GpuQueryType::OverlapSphere, 4u);
    requests[8].originRadius = {10.0f, 41.0f, 0.0f, 0.2f};
    requests[9] = makeRequest(409u, GpuQueryType::OverlapSphere, 4u);
    requests[9].originRadius = {10.0f, 40.2f, 0.0f, 0.2f};
    requests[10] = makeRequest(410u, GpuQueryType::RayCast, 4u);
    requests[10].originRadius = {0.0f, 30.0f, 0.0f, 0.0f};
    requests[10].ids[3] = PhysicsQueryExcludeBullets;
    requests[11] = makeRequest(411u, GpuQueryType::RayCast, 4u);
    requests[11].originRadius = {0.0f, 40.0f, 0.0f, 0.0f};
    requests[11].ids[3] = PhysicsQueryExcludeStatic;

    const auto result = executeBatch(context, queries, requests, 400u);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->outputs.size(), requests.size());
    EXPECT_EQ(result->outputs[0].header[1], 0u)
        << "capsule bounding sphere produced a false ray hit";
    ASSERT_EQ(result->outputs[1].header[1], 1u);
    EXPECT_EQ(result->outputs[1].hits[0].ids[1], 1u);
    EXPECT_EQ(result->outputs[1].hits[0].ids[2], 0x102u);
    EXPECT_EQ(result->outputs[1].hits[0].sector[3], 11);
    EXPECT_NEAR(result->outputs[1].hits[0].metricDistance[1], 4.5f, 1e-4f);
    EXPECT_EQ(result->outputs[2].header[1], 0u)
        << "cylinder bounding sphere produced a false ray hit";
    ASSERT_EQ(result->outputs[3].header[1], 1u);
    EXPECT_EQ(result->outputs[3].hits[0].ids[1], 2u);
    EXPECT_EQ(result->outputs[3].hits[0].ids[2], 0x310u);
    EXPECT_NEAR(result->outputs[3].hits[0].metricDistance[1], 4.0f, 1e-4f);
    EXPECT_EQ(result->outputs[4].header[1], 0u)
        << "expanded box AABB produced a corner false positive";
    ASSERT_EQ(result->outputs[5].header[1], 1u);
    EXPECT_EQ(result->outputs[5].hits[0].ids[1], 3u);
    EXPECT_NEAR(result->outputs[5].hits[0].metricDistance[1], 3.5f, 2e-3f);
    ASSERT_EQ(result->outputs[6].header[1], 1u);
    EXPECT_EQ(result->outputs[6].hits[0].ids[1], 4u);
    EXPECT_NEAR(result->outputs[6].hits[0].metricDistance[1], 4.25f, 2e-3f);
    EXPECT_EQ(result->outputs[7].header[1], 0u)
        << "capsule cast axis was ignored";
    EXPECT_EQ(result->outputs[8].header[1], 0u)
        << "overlap used the box bounding sphere";
    ASSERT_EQ(result->outputs[9].header[1], 1u);
    EXPECT_EQ(result->outputs[9].hits[0].ids[1], 5u);
    EXPECT_NEAR(result->outputs[9].hits[0].metricDistance[1], 0.1f, 1e-4f);
    EXPECT_EQ(result->outputs[10].header[1], 0u);
    EXPECT_EQ(result->outputs[11].header[1], 0u);

    queries.shutdown();
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST(DebugReadbackRing, RetiresWrappedSlotsInSubmissionOrder) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    uint32_t sourceValue = 0u;
    WGPUBuffer source = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(sizeof(sourceValue), false,
                                 "ordered_readback_source"),
        std::span<const uint32_t>(&sourceValue, 1u));
    ASSERT_NE(source, nullptr);
    DebugReadbackRing ring;
    ASSERT_TRUE(ring.initialize(context.getDevice(), 3u, sizeof(uint32_t)));
    const size_t workingBytes = ring.allocatedBytes();
    EXPECT_FALSE(ring.initialize(
        context.getDevice(), 65u, sizeof(uint32_t)));
    EXPECT_EQ(ring.allocatedBytes(), workingBytes);
    WGPUCommandEncoderDescriptor invalidEncoderDesc{};
    WGPUCommandEncoder invalidEncoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &invalidEncoderDesc);
    ASSERT_NE(invalidEncoder, nullptr);
    EXPECT_FALSE(ring.encodeCopy(
        invalidEncoder, source, sizeof(sourceValue), sizeof(sourceValue),
        0u, 0u, 1u));
    wgpuCommandEncoderRelease(invalidEncoder);

    const auto submit = [&](uint64_t tick) {
        sourceValue = static_cast<uint32_t>(tick);
        gpu::writeBuffer(context.getQueue(), source, 0u, sourceValue);
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDesc);
        EXPECT_TRUE(ring.encodeCopy(
            encoder, source, 0u, sizeof(sourceValue), tick, 0u, 1u));
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(
            encoder, &commandDesc);
        const WGPUSubmissionIndex submission = wgpuQueueSubmitForIndex(
            context.getQueue(), 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        return submission;
    };

    const WGPUSubmissionIndex firstSubmission = submit(1u);
    static_cast<void>(submit(2u));
    static_cast<void>(submit(3u));
    std::vector<uint64_t> retired;
    if (auto packet = ring.poll()) retired.push_back(packet->tick);
    const WGPUWrappedSubmissionIndex firstWrapped{
        context.getQueue(), firstSubmission};
    static_cast<void>(wgpuDevicePoll(
        context.getDevice(), true, &firstWrapped));

    for (uint32_t attempt = 0u; retired.empty() && attempt < 32u; ++attempt) {
        if (auto packet = ring.poll()) retired.push_back(packet->tick);
        if (retired.empty()) {
            static_cast<void>(wgpuDevicePoll(
                context.getDevice(), false, nullptr));
        }
    }
    ASSERT_EQ(retired, std::vector<uint64_t>{1u});

    const WGPUSubmissionIndex fourthSubmission = submit(4u);
    if (auto packet = ring.poll()) retired.push_back(packet->tick);
    const WGPUWrappedSubmissionIndex fourthWrapped{
        context.getQueue(), fourthSubmission};
    static_cast<void>(wgpuDevicePoll(
        context.getDevice(), true, &fourthWrapped));
    for (uint32_t attempt = 0u; retired.size() < 4u && attempt < 32u;
         ++attempt) {
        if (auto packet = ring.poll()) retired.push_back(packet->tick);
        if (retired.size() < 4u) {
            static_cast<void>(wgpuDevicePoll(
                context.getDevice(), false, nullptr));
        }
    }
    EXPECT_EQ(retired, (std::vector<uint64_t>{1u, 2u, 3u, 4u}));

    ring.shutdown();
    releaseBuffer(source);
}

TEST(DebugReadbackRing, ShutdownCancelsOutstandingMapWithoutSlotLifetimeRace) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    const uint32_t value = 42u;
    WGPUBuffer source = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(
            sizeof(value), false, "shutdown_readback_source"),
        std::span<const uint32_t>(&value, 1u));
    ASSERT_NE(source, nullptr);
    DebugReadbackRing ring;
    ASSERT_TRUE(ring.initialize(context.getDevice(), 1u, sizeof(value)));

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    ASSERT_TRUE(ring.encodeCopy(
        encoder, source, 0u, sizeof(value), 1u, 0u, 1u));
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(
        encoder, &commandDesc);
    wgpuQueueSubmit(context.getQueue(), 1u, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    static_cast<void>(ring.poll());
    ring.shutdown();
    static_cast<void>(wgpuDevicePoll(
        context.getDevice(), true, nullptr));
    releaseBuffer(source);
}

} // namespace
} // namespace voxy::physics
