#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_ccd.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

struct CcdSnapshot {
    std::vector<TestPose> poses;
    std::vector<TestMotion> motions;
    std::vector<TestMetadata> metadata;
    std::vector<uint32_t> bulletIds;
    GpuCcdTelemetry telemetry;
};

CcdSnapshot runAndRead(gpu::Context& context, GpuCcd& ccd,
                       WGPUBuffer poses, WGPUBuffer motions,
                       WGPUBuffer metadata, uint32_t bodyCapacity) {
    constexpr size_t telemetryBytes = 32u * sizeof(uint32_t);
    const size_t poseBytes = size_t{bodyCapacity} * sizeof(TestPose);
    const size_t motionBytes = size_t{bodyCapacity} * sizeof(TestMotion);
    const size_t metadataBytes = size_t{bodyCapacity} * sizeof(TestMetadata);
    const size_t bulletBytes = size_t{bodyCapacity} * sizeof(uint32_t);
    const size_t totalBytes = poseBytes + motionBytes + metadataBytes
        + bulletBytes + telemetryBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "ccd_test_readback",
            .size = totalBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    EXPECT_TRUE(ccd.encode(encoder, 1.0f / 60.0f));
    size_t offset = 0;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, poses, 0, readback, offset, poseBytes);
    offset += poseBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, motions, 0, readback, offset, motionBytes);
    offset += motionBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, metadata, 0, readback, offset, metadataBytes);
    offset += metadataBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, ccd.bulletBodyIds(), 0, readback, offset, bulletBytes);
    offset += bulletBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, ccd.telemetryBuffer(), 0, readback, offset, telemetryBytes);
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
    CcdSnapshot result;
    if (state.success) {
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
        offset = 0;
        result.poses.resize(bodyCapacity);
        std::memcpy(result.poses.data(), bytes + offset, poseBytes);
        offset += poseBytes;
        result.motions.resize(bodyCapacity);
        std::memcpy(result.motions.data(), bytes + offset, motionBytes);
        offset += motionBytes;
        result.metadata.resize(bodyCapacity);
        std::memcpy(result.metadata.data(), bytes + offset, metadataBytes);
        offset += metadataBytes;
        result.bulletIds.resize(bodyCapacity);
        std::memcpy(result.bulletIds.data(), bytes + offset, bulletBytes);
        offset += bulletBytes;
        std::array<uint32_t, 32> telemetry{};
        std::memcpy(telemetry.data(), bytes + offset, telemetryBytes);
        result.telemetry = GpuCcd::decodeTelemetry(telemetry);
    }
    wgpuBufferUnmap(readback);
    releaseBuffer(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    return result;
}

class GpuCcdTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(GpuCcdTest, StopsFastSphereAndCapsuleWithBoundedBulletOverflow) {
    constexpr uint32_t bodyCapacity = 8;
    constexpr uint32_t terrainExtent = 16;
    constexpr std::array<int32_t, 3> terrainSector{
        1'500'000, -1'500'000, 900'000};
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestMotion> motions(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    const auto addBody = [&](uint32_t body, float x, bool bullet) {
        poses[body].positionInvMass = {x, 5.0f, 0.0f, 1.0f};
        motions[body].linearVelocitySleep = {0.0f, -600.0f, 0.0f, 0.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
        const uint32_t flags = kGpuBodyAliveFlag | kGpuBodyAwakeFlag
            | (bullet ? kGpuBodyBulletFlag : 0u);
        metadata[body] = {
            static_cast<uint32_t>(terrainSector[0]),
            static_cast<uint32_t>(terrainSector[1]),
            static_cast<uint32_t>(terrainSector[2]),
            packGpuBodyMetadata(1u, flags)};
    };
    addBody(1u, -2.0f, true);
    addBody(2u, 0.0f, false);
    shapes[2].dimensionsType = {1.0f, 2.0f, 1.0f, 3.0f};
    addBody(3u, 2.0f, true);
    addBody(4u, 4.0f, false);
    motions[4].linearVelocitySleep.y = -1.0f;

    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "ccd_poses");
    WGPUBuffer motionBuffer = makeStorage<TestMotion>(
        context, motions, "ccd_motions");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(
        context, shapes, "ccd_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "ccd_metadata");
    std::vector<uint16_t> terrainSamples(
        terrainExtent * terrainExtent, 32'768u);
    auto textureDesc = gpu::TextureDesc::tex2D(
        terrainExtent, terrainExtent, WGPUTextureFormat_R16Uint,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "ccd_flat_terrain");
    WGPUTexture terrainTexture = gpu::createTextureWithData(
        context.getDevice(), context.getQueue(), textureDesc,
        std::as_bytes(std::span<const uint16_t>(terrainSamples)),
        terrainExtent * sizeof(uint16_t));
    gpu::TextureViewDesc viewDesc;
    viewDesc.format = WGPUTextureFormat_R16Uint;
    WGPUTextureView terrainView = gpu::createTextureView(
        terrainTexture, viewDesc);
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(motionBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);
    ASSERT_NE(terrainView, nullptr);

    GpuCcd ccd;
    GpuCcd::Config config;
    config.bodyCapacity = bodyCapacity;
    config.bulletCapacity = 1;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(ccd.initialize(
        context.getDevice(), context.getQueue(), config));
    const WGPUBuffer workingTelemetry = ccd.telemetryBuffer();
    auto invalidConfig = config;
    invalidConfig.bodyCapacity = 0u;
    EXPECT_FALSE(ccd.initialize(
        context.getDevice(), context.getQueue(), invalidConfig));
    EXPECT_EQ(ccd.telemetryBuffer(), workingTelemetry);
    ccd.setInput({
        .poseBuffer = poseBuffer,
        .motionBuffer = motionBuffer,
        .shapeBuffer = shapeBuffer,
        .metadataBuffer = metadataBuffer,
        .terrainTexture = terrainView,
        .bodyCapacity = bodyCapacity,
        .terrainWidth = terrainExtent,
        .terrainHeight = terrainExtent,
        .terrainHeightScale = 10.0f,
        .terrainCellScale = 1.0f,
        .terrainSector = terrainSector,
    });

    const CcdSnapshot snapshot = runAndRead(
        context, ccd, poseBuffer, motionBuffer, metadataBuffer, bodyCapacity);
    ASSERT_EQ(snapshot.poses.size(), bodyCapacity);
    EXPECT_GT(snapshot.poses[1].positionInvMass.y, 0.49f);
    EXPECT_LT(snapshot.poses[1].positionInvMass.y, 0.53f);
    EXPECT_GE(snapshot.motions[1].linearVelocitySleep.y, 0.0f);
    EXPECT_NE(snapshot.metadata[1][3] & kGpuBodyCcdHitFlag, 0u);
    EXPECT_GT(snapshot.poses[2].positionInvMass.y, 0.99f);
    EXPECT_LT(snapshot.poses[2].positionInvMass.y, 1.03f);
    EXPECT_GE(snapshot.motions[2].linearVelocitySleep.y, 0.0f);
    EXPECT_NE(snapshot.metadata[2][3] & kGpuBodyCcdHitFlag, 0u);
    for (uint32_t body : {1u, 2u, 3u, 4u}) {
        EXPECT_EQ(snapshot.metadata[body][0],
                  static_cast<uint32_t>(terrainSector[0]));
        EXPECT_EQ(snapshot.metadata[body][1],
                  static_cast<uint32_t>(terrainSector[1]));
        EXPECT_EQ(snapshot.metadata[body][2],
                  static_cast<uint32_t>(terrainSector[2]));
    }
    EXPECT_FLOAT_EQ(snapshot.poses[3].positionInvMass.y, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.motions[3].linearVelocitySleep.y, -600.0f);
    EXPECT_EQ(snapshot.metadata[3][3] & kGpuBodyCcdHitFlag, 0u);
    EXPECT_FLOAT_EQ(snapshot.poses[4].positionInvMass.y, 5.0f);
    ASSERT_GE(snapshot.bulletIds.size(), 2u);
    EXPECT_EQ(snapshot.bulletIds[0], 1u);
    EXPECT_EQ(snapshot.bulletIds[1], 3u);
    EXPECT_EQ(snapshot.telemetry.fastCandidates, 1u);
    EXPECT_EQ(snapshot.telemetry.bulletRequested, 2u);
    EXPECT_EQ(snapshot.telemetry.bulletProcessed, 1u);
    EXPECT_EQ(snapshot.telemetry.bulletOverflow, 1u);
    EXPECT_EQ(snapshot.telemetry.hits, 2u);
    EXPECT_EQ(snapshot.telemetry.failures, 0u);
    EXPECT_GT(snapshot.telemetry.maximumIterations, 0u);
    EXPECT_LE(snapshot.telemetry.maximumIterations, 24u);
    EXPECT_EQ(snapshot.telemetry.tick, 1u);
    EXPECT_GT(ccd.allocatedBytes(), 0u);

    for (auto& bodyMetadata : metadata) bodyMetadata = {};
    metadata[1] = {
        static_cast<uint32_t>(terrainSector[0]),
        static_cast<uint32_t>(terrainSector[1]),
        static_cast<uint32_t>(terrainSector[2]),
        packGpuBodyMetadata(
            1u, kGpuBodyAliveFlag | kGpuBodyAwakeFlag
                | kGpuBodyBulletFlag)};
    poses[1].positionInvMass = {-2.0f, 5.0f, 0.0f, 1.0f};
    motions[1].linearVelocitySleep = {2'000.0f, 0.0f, 0.0f, 0.0f};
    gpu::writeBuffer(context.getQueue(), poseBuffer, 0,
                     std::span<const TestPose>(poses));
    gpu::writeBuffer(context.getQueue(), motionBuffer, 0,
                     std::span<const TestMotion>(motions));
    gpu::writeBuffer(context.getQueue(), metadataBuffer, 0,
                     std::span<const TestMetadata>(metadata));
    const CcdSnapshot underResolved = runAndRead(
        context, ccd, poseBuffer, motionBuffer, metadataBuffer, bodyCapacity);
    EXPECT_EQ(underResolved.telemetry.bulletRequested, 1u);
    EXPECT_EQ(underResolved.telemetry.bulletOverflow, 0u);
    EXPECT_EQ(underResolved.telemetry.stalls, 1u);
    EXPECT_EQ(underResolved.telemetry.failures, 1u);
    EXPECT_NE(underResolved.metadata[1][3] & kGpuBodyCcdFailureFlag, 0u);
    EXPECT_EQ(underResolved.telemetry.tick, 2u);

    ccd.shutdown();
    wgpuTextureViewRelease(terrainView);
    wgpuTextureDestroy(terrainTexture);
    wgpuTextureRelease(terrainTexture);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(motionBuffer);
    releaseBuffer(poseBuffer);
}

INSTANTIATE_TEST_SUITE_P(WorkgroupSizes, GpuCcdTest,
                         ::testing::Values(64u, 128u, 256u));

} // namespace
} // namespace voxy::physics
