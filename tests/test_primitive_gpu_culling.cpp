#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "render/primitive_gpu_culling.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

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

namespace voxy::render {
namespace {

struct alignas(16) TestPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) TestShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 properties{0.0f};
};

struct alignas(16) TestMetadata {
    glm::ivec4 sectorGenerationFlags{0};
};

struct IndirectDrawArgs {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t baseVertex;
    uint32_t firstInstance;
};

static_assert(sizeof(TestPose) == 32);
static_assert(sizeof(TestShape) == 32);
static_assert(sizeof(TestMetadata) == 16);
static_assert(sizeof(IndirectDrawArgs) == 20);

TEST(PrimitiveGpuCullingTest, StablyBucketsVisibleBodiesByShape) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    constexpr uint32_t bodyCount = 7;
    std::array<TestPose, bodyCount> poses{};
    std::array<TestShape, bodyCount> shapes{};
    for (uint32_t shape = 0; shape < PrimitiveGpuCulling::kShapeCount;
         ++shape) {
        const uint32_t body = shape + 1u;
        poses[body].positionInvMass = glm::vec4(
            -0.5f + 0.25f * static_cast<float>(shape), 0.0f, 0.5f, 1.0f);
        shapes[body].dimensionsType = glm::vec4(
            0.1f, 0.1f, 0.1f, static_cast<float>(shape));
    }
    poses[6].positionInvMass = glm::vec4(20.0f, 0.0f, 0.5f, 1.0f);
    shapes[6].dimensionsType = glm::vec4(0.1f, 0.1f, 0.1f, 0.0f);

    WGPUBuffer poseBuffer = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(sizeof(poses), true, "cull_test_poses"),
        std::span<const TestPose>(poses));
    WGPUBuffer shapeBuffer = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(sizeof(shapes), true, "cull_test_shapes"),
        std::span<const TestShape>(shapes));
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);

    std::array<PrimitiveDrawGeometry, PrimitiveGpuCulling::kShapeCount> geometry{};
    for (uint32_t shape = 0; shape < geometry.size(); ++shape) {
        geometry[shape] = {100u + shape, 200u + shape * 10u};
    }
    PrimitiveGpuCulling culling;
    ASSERT_TRUE(culling.initialize(
        context.getDevice(), context.getQueue(),
        "shaders/physics_primitive_cull.wgsl", geometry));
    culling.setBodyView({
        .poseBuffer = poseBuffer,
        .shapeBuffer = shapeBuffer,
        .residentBodyCapacity = bodyCount,
        .shapeCount = PrimitiveGpuCulling::kShapeCount,
    });

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    ASSERT_TRUE(culling.encode(encoder, glm::mat4(1.0f)));
    const size_t indirectBytes =
        PrimitiveGpuCulling::kShapeCount * sizeof(IndirectDrawArgs);
    const size_t visibleBytes = size_t{culling.segmentCapacity()}
                              * PrimitiveGpuCulling::kShapeCount
                              * sizeof(uint32_t);
    const gpu::BufferDesc readbackDesc{
        .label = "primitive_cull_test_readback",
        .size = indirectBytes + visibleBytes,
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
    };
    WGPUBuffer readback = gpu::createBuffer(context.getDevice(), readbackDesc);
    ASSERT_NE(readback, nullptr);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, culling.indirectDrawArgs(), 0, readback, 0, indirectBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, culling.visibleBodyIds(), 0, readback, indirectBytes,
        visibleBytes);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};

    struct MapState {
        bool done = false;
        bool success = false;
    } state;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& map = *static_cast<MapState*>(userdata);
        map.success = status == WGPUBufferMapAsyncStatus_Success;
        map.done = true;
    };
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0,
                       indirectBytes + visibleBytes, callback, &state);
    while (!state.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(state.success);
    const auto* bytes = static_cast<const std::byte*>(
        wgpuBufferGetConstMappedRange(
            readback, 0, indirectBytes + visibleBytes));
    ASSERT_NE(bytes, nullptr);
    std::array<IndirectDrawArgs, PrimitiveGpuCulling::kShapeCount> indirect{};
    std::memcpy(indirect.data(), bytes, indirectBytes);
    for (uint32_t shape = 0; shape < indirect.size(); ++shape) {
        EXPECT_EQ(indirect[shape].indexCount, geometry[shape].indexCount);
        EXPECT_EQ(indirect[shape].firstIndex, geometry[shape].firstIndex);
        EXPECT_EQ(indirect[shape].instanceCount, 1u);
        uint32_t body = 0;
        const size_t bodyOffset = indirectBytes
            + size_t{shape} * culling.segmentCapacity() * sizeof(uint32_t);
        std::memcpy(&body, bytes + bodyOffset, sizeof(body));
        EXPECT_EQ(body, shape + 1u);
    }

    wgpuBufferUnmap(readback);
    wgpuBufferDestroy(readback);
    wgpuBufferRelease(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferDestroy(shapeBuffer);
    wgpuBufferRelease(shapeBuffer);
    wgpuBufferDestroy(poseBuffer);
    wgpuBufferRelease(poseBuffer);
}

TEST(PrimitiveGpuCullingTest,
     RebasesAdjacentLargeWorldSectorsAndRejectsDistantBodies) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    constexpr uint32_t bodyCount = 4;
    constexpr int32_t cameraSectorX = 1'500'000;
    std::array<TestPose, bodyCount> poses{};
    std::array<TestShape, bodyCount> shapes{};
    std::array<TestMetadata, bodyCount> metadata{};
    const int32_t alive = static_cast<int32_t>(physics::packGpuBodyMetadata(
        1u, physics::kGpuBodyAliveFlag));

    poses[1].positionInvMass = glm::vec4(127.25f, 0.0f, 0.5f, 1.0f);
    poses[2].positionInvMass = glm::vec4(-127.75f, 0.0f, 0.5f, 1.0f);
    poses[3].positionInvMass = poses[1].positionInvMass;
    for (uint32_t body = 1; body < bodyCount; ++body) {
        shapes[body].dimensionsType = glm::vec4(0.1f, 0.1f, 0.1f, 0.0f);
    }
    metadata[1].sectorGenerationFlags =
        glm::ivec4(cameraSectorX, 0, 0, alive);
    metadata[2].sectorGenerationFlags =
        glm::ivec4(cameraSectorX + 1, 0, 0, alive);
    metadata[3].sectorGenerationFlags = glm::ivec4(0, 0, 0, alive);

    WGPUBuffer poseBuffer = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(sizeof(poses), true, "sector_cull_poses"),
        std::span<const TestPose>(poses));
    WGPUBuffer shapeBuffer = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(sizeof(shapes), true, "sector_cull_shapes"),
        std::span<const TestShape>(shapes));
    WGPUBuffer metadataBuffer = gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(
            sizeof(metadata), true, "sector_cull_metadata"),
        std::span<const TestMetadata>(metadata));
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    std::array<PrimitiveDrawGeometry,
               PrimitiveGpuCulling::kShapeCount> geometry{};
    for (uint32_t shape = 0; shape < geometry.size(); ++shape) {
        geometry[shape] = {100u + shape, 200u + shape * 10u};
    }
    PrimitiveGpuCulling culling;
    ASSERT_TRUE(culling.initialize(
        context.getDevice(), context.getQueue(),
        "shaders/physics_primitive_cull.wgsl", geometry));
    culling.setBodyView({
        .poseBuffer = poseBuffer,
        .shapeBuffer = shapeBuffer,
        .metadataBuffer = metadataBuffer,
        .residentBodyCapacity = bodyCount,
        .shapeCount = PrimitiveGpuCulling::kShapeCount,
    });

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    const glm::mat4 viewProjection = glm::translate(
        glm::mat4(1.0f), glm::vec3(-127.5f, 0.0f, 0.0f));
    ASSERT_TRUE(culling.encode(
        encoder, viewProjection, glm::ivec3(cameraSectorX, 0, 0)));

    const size_t indirectBytes =
        PrimitiveGpuCulling::kShapeCount * sizeof(IndirectDrawArgs);
    const size_t visibleBytes = size_t{culling.segmentCapacity()}
                              * PrimitiveGpuCulling::kShapeCount
                              * sizeof(uint32_t);
    const size_t poseBytes = sizeof(poses);
    const gpu::BufferDesc readbackDesc{
        .label = "primitive_sector_cull_readback",
        .size = indirectBytes + visibleBytes + poseBytes,
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
    };
    WGPUBuffer readback = gpu::createBuffer(context.getDevice(), readbackDesc);
    ASSERT_NE(readback, nullptr);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, culling.indirectDrawArgs(), 0, readback, 0, indirectBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, culling.visibleBodyIds(), 0, readback, indirectBytes,
        visibleBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, culling.renderPoseBuffer(), 0, readback,
        indirectBytes + visibleBytes, poseBytes);

    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    struct MapState {
        bool done = false;
        bool success = false;
    } state;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& map = *static_cast<MapState*>(userdata);
        map.success = status == WGPUBufferMapAsyncStatus_Success;
        map.done = true;
    };
    const size_t totalBytes = indirectBytes + visibleBytes + poseBytes;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, totalBytes,
                       callback, &state);
    while (!state.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(state.success);
    const auto* bytes = static_cast<const std::byte*>(
        wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
    ASSERT_NE(bytes, nullptr);

    std::array<IndirectDrawArgs,
               PrimitiveGpuCulling::kShapeCount> indirect{};
    std::memcpy(indirect.data(), bytes, indirectBytes);
    EXPECT_EQ(indirect[0].instanceCount, 2u);
    for (uint32_t shape = 1; shape < indirect.size(); ++shape) {
        EXPECT_EQ(indirect[shape].instanceCount, 0u);
    }
    std::array<uint32_t, 2> visible{};
    std::memcpy(visible.data(), bytes + indirectBytes, sizeof(visible));
    EXPECT_EQ(visible[0], 1u);
    EXPECT_EQ(visible[1], 2u);

    std::array<TestPose, bodyCount> rebased{};
    std::memcpy(rebased.data(), bytes + indirectBytes + visibleBytes,
                poseBytes);
    EXPECT_FLOAT_EQ(rebased[1].positionInvMass.x, 127.25f);
    EXPECT_FLOAT_EQ(rebased[2].positionInvMass.x, 128.25f);
    EXPECT_LT(rebased[3].positionInvMass.w, 0.0f);

    wgpuBufferUnmap(readback);
    wgpuBufferDestroy(readback);
    wgpuBufferRelease(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferDestroy(metadataBuffer);
    wgpuBufferRelease(metadataBuffer);
    wgpuBufferDestroy(shapeBuffer);
    wgpuBufferRelease(shapeBuffer);
    wgpuBufferDestroy(poseBuffer);
    wgpuBufferRelease(poseBuffer);
}

} // namespace
} // namespace voxy::render
