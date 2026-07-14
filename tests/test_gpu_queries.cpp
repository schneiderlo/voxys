#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_queries.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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

struct alignas(16) TestShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 properties{0.0f};
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
    metadata[1].front() = 1u;
    metadata[2].front() = 1u;
    metadata[3].front() = 1u;

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

    for (uint32_t body = 1; body <= 20; ++body) metadata[body].front() = 1u;
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

} // namespace
} // namespace voxy::physics
