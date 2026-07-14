#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_dynamic_solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
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

using Clock = std::chrono::steady_clock;
constexpr uint32_t kBodyCount = 100'000;
constexpr uint32_t kContactCount = kBodyCount / 2u;
constexpr uint32_t kWarmupFrames = 4;
constexpr uint32_t kMeasuredFrames = 20;

struct alignas(16) BenchmarkPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) BenchmarkMotion {
    glm::vec4 linearVelocitySleep{0.0f};
    glm::vec4 angularVelocityFlags{0.0f};
};

struct alignas(16) BenchmarkShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 inverseInertiaMaterial{0.0f};
};

using BenchmarkMetadata = std::array<uint32_t, 4>;

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

double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    const size_t rank = static_cast<size_t>(std::ceil(
        fraction * static_cast<double>(samples.size())));
    return samples[std::min(std::max<size_t>(rank, 1) - 1,
                            samples.size() - 1)];
}

GpuContactManifold makeSphereContact(uint32_t bodyA, uint32_t bodyB,
                                     uint32_t ordinal) {
    GpuContactManifold result;
    result.pair = {bodyB, bodyA, ordinal, ordinal};
    result.state = {1u, 0u, 0u, 0u};
    result.normal = {1.0f, 0.0f, 0.0f, 0.0f};
    result.tangent1 = {0.0f, 1.0f, 0.0f, 0.0f};
    result.tangent2 = {0.0f, 0.0f, 1.0f, 0.0f};
    result.frictionAnchorA = {0.5f, 0.0f, 0.0f, 0.0f};
    result.frictionAnchorB = {-0.5f, 0.0f, 0.0f, 0.0f};
    result.points[0].localAnchorASeparation = {0.5f, 0.0f, 0.0f, 0.0f};
    result.points[0].localAnchorBNormalImpulse = {-0.5f, 0.0f, 0.0f, 0.0f};
    result.points[0].features = {1u, 1u, 0u, 0u};
    return result;
}

TEST(GpuDynamicBenchmark,
     SolvesOneHundredThousandSparseContactSpheresAtSixtyHertz) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<BenchmarkPose> poses(kBodyCount);
    std::vector<BenchmarkMotion> motions(kBodyCount);
    std::vector<BenchmarkShape> shapes(kBodyCount);
    std::vector<BenchmarkMetadata> metadata(kBodyCount);
    std::vector<GpuContactManifold> manifolds(kContactCount);
    for (uint32_t contact = 0; contact < kContactCount; ++contact) {
        const uint32_t bodyA = contact * 2u;
        const uint32_t bodyB = bodyA + 1u;
        const float x = static_cast<float>(contact % 500u) * 3.0f;
        const float z = static_cast<float>(contact / 500u) * 3.0f;
        poses[bodyA].positionInvMass = {x - 0.5f, 0.0f, z, 1.0f};
        poses[bodyB].positionInvMass = {x + 0.5f, 0.0f, z, 1.0f};
        motions[bodyA].linearVelocitySleep = {0.05f, 0.02f, 0.0f, 0.0f};
        motions[bodyB].linearVelocitySleep = {-0.05f, -0.02f, 0.0f, 0.0f};
        for (uint32_t body : {bodyA, bodyB}) {
            shapes[body].dimensionsType = {0.5f, 0.0f, 0.0f, 0.0f};
            shapes[body].inverseInertiaMaterial = {10.0f, 10.0f, 10.0f, 0.0f};
            metadata[body] = {3u, 1u, 0u, 0u};
        }
        manifolds[contact] = makeSphereContact(bodyA, bodyB, contact);
    }
    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[10] = kContactCount;
    narrowTelemetry[11] = kContactCount;

    WGPUBuffer poseBuffer = makeStorage<BenchmarkPose>(
        context, poses, "dynamic_benchmark_poses");
    WGPUBuffer motionBuffer = makeStorage<BenchmarkMotion>(
        context, motions, "dynamic_benchmark_motions");
    WGPUBuffer shapeBuffer = makeStorage<BenchmarkShape>(
        context, shapes, "dynamic_benchmark_shapes");
    WGPUBuffer metadataBuffer = makeStorage<BenchmarkMetadata>(
        context, metadata, "dynamic_benchmark_metadata");
    WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "dynamic_benchmark_manifolds");
    WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
        context, narrowTelemetry, "dynamic_benchmark_narrow_telemetry");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(motionBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);
    ASSERT_NE(manifoldBuffer, nullptr);
    ASSERT_NE(narrowTelemetryBuffer, nullptr);

    GpuDynamicSolver solver;
    GpuDynamicSolver::Config config;
    config.bodyCapacity = kBodyCount;
    config.contactCapacity = kContactCount;
    config.colorCount = 32;
    config.workgroupSize = 128;
    config.substeps = 4;
    config.gravity = {0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(solver.initialize(
        context.getDevice(), context.getQueue(), config));
    solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                     manifoldBuffer, narrowTelemetryBuffer,
                     kBodyCount, kContactCount});

    std::vector<double> retiredFrameMs;
    retiredFrameMs.reserve(kMeasuredFrames);
    for (uint32_t frame = 0; frame < kWarmupFrames + kMeasuredFrames; ++frame) {
        const auto start = Clock::now();
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDesc);
        ASSERT_TRUE(solver.encode(encoder));
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
            context.getQueue(), 1, &command);
        const WGPUWrappedSubmissionIndex submission{
            context.getQueue(), submissionIndex};
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        if (frame >= kWarmupFrames) {
            retiredFrameMs.push_back(
                std::chrono::duration<double, std::milli>(
                    Clock::now() - start).count());
        }
    }

    constexpr size_t telemetryBytes = 64u * sizeof(uint32_t);
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "dynamic_benchmark_telemetry_readback",
            .size = telemetryBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, solver.telemetryBuffer(), 0, readback, 0, telemetryBytes);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{context.getQueue(), submissionIndex};
    struct MapState { bool done = false; bool success = false; } mapState;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& state = *static_cast<MapState*>(userdata);
        state.success = status == WGPUBufferMapAsyncStatus_Success;
        state.done = true;
    };
    wgpuBufferMapAsync(
        readback, WGPUMapMode_Read, 0, telemetryBytes, callback, &mapState);
    while (!mapState.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(mapState.success);
    std::array<uint32_t, 64> telemetryWords{};
    std::memcpy(telemetryWords.data(),
                wgpuBufferGetConstMappedRange(readback, 0, telemetryBytes),
                telemetryBytes);
    const GpuDynamicSolverTelemetry telemetry =
        GpuDynamicSolver::decodeTelemetry(telemetryWords);
    wgpuBufferUnmap(readback);
    releaseBuffer(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    const double p50 = percentile(retiredFrameMs, 0.50);
    const double p95 = percentile(retiredFrameMs, 0.95);
    EXPECT_EQ(telemetry.contactCount, kContactCount);
    EXPECT_EQ(telemetry.smallIslandContacts, kContactCount);
    EXPECT_EQ(telemetry.smallIslandBodies, kBodyCount);
    EXPECT_EQ(telemetry.coloredContacts, 0u);
    EXPECT_EQ(telemetry.overflowContacts, 0u);
    EXPECT_LT(p95, 16.667);
    std::cout << std::fixed << std::setprecision(3)
              << "gpu_dynamic bodies=" << kBodyCount
              << " contacts=" << kContactCount
              << " substeps=" << config.substeps
              << " retired_p50_ms=" << p50
              << " retired_p95_ms=" << p95
              << " scratch_mib="
              << static_cast<double>(solver.scratchBytes()) / (1024.0 * 1024.0)
              << '\n';

    releaseBuffer(narrowTelemetryBuffer);
    releaseBuffer(manifoldBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(motionBuffer);
    releaseBuffer(poseBuffer);
}

} // namespace
} // namespace voxy::physics
