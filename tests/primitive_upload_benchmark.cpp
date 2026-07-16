#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "render/primitive_instance_packing.hpp"
#include "render/primitive_path.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
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

namespace voxy::render::detail {
namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kIterations = 240;
constexpr size_t kCompactIterations = 60;
constexpr size_t kDirtyInstance = 17;

struct Summary {
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double framesPerSecond = 0.0;
};

struct Result {
    Summary enqueue;
    Summary retired;
    uint64_t outputHash = 0;
    size_t bytesPerFrame = 0;
};

Summary summarize(std::vector<double> samples, double elapsedSeconds) {
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double fraction) {
        const size_t rank = static_cast<size_t>(std::ceil(
            fraction * static_cast<double>(samples.size())));
        return samples[std::min(std::max<size_t>(rank, 1) - 1,
                                samples.size() - 1)];
    };
    return {percentile(0.50), percentile(0.95), percentile(0.99),
            static_cast<double>(samples.size()) / elapsedSeconds};
}

uint64_t hashBytes(std::span<const std::byte> bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (std::byte value : bytes) {
        hash ^= std::to_integer<uint8_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<GpuInstance> makeInstances(size_t count) {
    std::vector<GpuInstance> instances(count);
    for (size_t index = 0; index < count; ++index) {
        auto& instance = instances[index];
        instance.model[0][0] =
            1.0f + static_cast<float>(index % 13) * 0.01f;
        instance.model[1][1] =
            1.0f + static_cast<float>(index % 7) * 0.02f;
        instance.model[2][2] =
            1.0f + static_cast<float>(index % 5) * 0.03f;
        instance.model[3] = glm::vec4(
            static_cast<float>(index % 127),
            static_cast<float>(index / 127),
            static_cast<float>(index % 31), 1.0f);
        instance.color = glm::vec4(
            static_cast<float>(index % 3) * 0.25f,
            0.5f, 0.75f, 1.0f);
    }
    return instances;
}

void retireQueue(WGPUDevice device, WGPUQueue queue) {
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex index =
        wgpuQueueSubmitForIndex(queue, 1, &command);
    const WGPUWrappedSubmissionIndex wrapped{queue, index};
    static_cast<void>(wgpuDevicePoll(device, true, &wrapped));
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
}

uint64_t readbackHash(WGPUDevice device, WGPUQueue queue, WGPUBuffer source,
                      size_t byteCount) {
    const gpu::BufferDesc stagingDesc{
        .label = "primitive_upload_readback",
        .size = byteCount,
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
    };
    WGPUBuffer staging = gpu::createBuffer(device, stagingDesc);
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, source, 0, staging, 0, byteCount);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex index =
        wgpuQueueSubmitForIndex(queue, 1, &command);
    const WGPUWrappedSubmissionIndex wrapped{queue, index};

    struct MapState {
        bool done = false;
        bool success = false;
    } state;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& result = *static_cast<MapState*>(userdata);
        result.success = status == WGPUBufferMapAsyncStatus_Success;
        result.done = true;
    };
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, byteCount,
                       callback, &state);
    while (!state.done) {
        static_cast<void>(wgpuDevicePoll(device, true, &wrapped));
    }
    EXPECT_TRUE(state.success);

    uint64_t hash = 0;
    if (state.success) {
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(staging, 0, byteCount));
        hash = hashBytes({bytes, byteCount});
    }
    wgpuBufferUnmap(staging);
    wgpuBufferDestroy(staging);
    wgpuBufferRelease(staging);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    return hash;
}

Result run(WGPUDevice device, WGPUQueue queue, size_t count,
           bool dirtyRanges) {
    auto current = makeInstances(count);
    std::vector<uint64_t> currentTokens(count);
    for (size_t index = 0; index < count; ++index) {
        currentTokens[index] = index + 1;
    }
    auto previousTokens = currentTokens;
    const size_t byteCount = current.size() * sizeof(GpuInstance);
    const gpu::BufferDesc bufferDesc{
        .label = "primitive_upload_benchmark",
        .size = byteCount,
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
               | WGPUBufferUsage_CopySrc,
    };
    WGPUBuffer buffer = gpu::createBuffer(device, bufferDesc);
    gpu::writeBuffer(queue, buffer, 0,
                     std::as_bytes(std::span<const GpuInstance>(current)));
    retireQueue(device, queue);

    bool planValid = true;
    std::vector<double> enqueueSamples;
    std::vector<double> retiredSamples;
    enqueueSamples.reserve(kIterations);
    retiredSamples.reserve(kIterations);
    const auto totalStart = Clock::now();
    for (size_t iteration = 0; iteration < kIterations; ++iteration) {
        current[kDirtyInstance].model[3][0] =
            static_cast<float>(iteration) * 0.125f;
        const auto frameStart = Clock::now();
        if (dirtyRanges) {
            currentTokens[kDirtyInstance] = 0;
            const auto plan = planPrimitiveInstanceUpload(
                current.size(), currentTokens, previousTokens, true);
            planValid &= !plan.fullUpload && plan.rangeCount == 1
                      && plan.byteCount == sizeof(GpuInstance);
            for (size_t rangeIndex = 0;
                 rangeIndex < plan.rangeCount; ++rangeIndex) {
                const auto& range = plan.ranges[rangeIndex];
                const auto values = std::span<const GpuInstance>(current)
                                        .subspan(range.firstInstance,
                                                 range.instanceCount);
                gpu::writeBuffer(queue, buffer,
                                 range.firstInstance * sizeof(GpuInstance),
                                 std::as_bytes(values));
            }
        } else {
            gpu::writeBuffer(
                queue, buffer, 0,
                std::as_bytes(std::span<const GpuInstance>(current)));
        }
        const auto enqueueEnd = Clock::now();
        retireQueue(device, queue);
        const auto retiredEnd = Clock::now();
        enqueueSamples.push_back(std::chrono::duration<double, std::milli>(
            enqueueEnd - frameStart).count());
        retiredSamples.push_back(std::chrono::duration<double, std::milli>(
            retiredEnd - frameStart).count());
        previousTokens = currentTokens;
    }
    EXPECT_TRUE(planValid);
    const double elapsedSeconds =
        std::chrono::duration<double>(Clock::now() - totalStart).count();
    const uint64_t hash = readbackHash(device, queue, buffer, byteCount);
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    return {summarize(std::move(enqueueSamples), elapsedSeconds),
            summarize(std::move(retiredSamples), elapsedSeconds), hash,
            dirtyRanges ? sizeof(GpuInstance) : byteCount};
}

Result runCompactFallback(
    PrimitivePath& path,
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& bodies,
    WGPUDevice device, WGPUQueue queue, bool dirtyShapes) {
    std::vector<double> enqueueSamples;
    std::vector<double> retiredSamples;
    enqueueSamples.reserve(kCompactIterations);
    retiredSamples.reserve(kCompactIterations);
    PrimitiveUploadStats stats;
    const auto totalStart = Clock::now();
    for (size_t iteration = 0; iteration < kCompactIterations; ++iteration) {
        if (dirtyShapes) {
            const float delta = (iteration & 1u) == 0u ? 0.001f : -0.001f;
            bodies.front().dimensions.x += delta;
            bodies.back().dimensions.z += delta;
        }
        bodies[iteration % bodies.size()].position.x += 0.001f;
        const auto frameStart = Clock::now();
        path.setCompactPhysicsInstances(bodies);
        const auto enqueueEnd = Clock::now();
        stats = path.lastCompactUploadStats();
        retireQueue(device, queue);
        const auto retiredEnd = Clock::now();
        enqueueSamples.push_back(std::chrono::duration<double, std::milli>(
            enqueueEnd - frameStart).count());
        retiredSamples.push_back(std::chrono::duration<double, std::milli>(
            retiredEnd - frameStart).count());
    }
    const double elapsedSeconds =
        std::chrono::duration<double>(Clock::now() - totalStart).count();
    return {summarize(std::move(enqueueSamples), elapsedSeconds),
            summarize(std::move(retiredSamples), elapsedSeconds), 0u,
            stats.bytesUploaded};
}

TEST(PrimitiveUploadBenchmark, OneDirtyInstanceAtLargeBodyCounts) {
    gpu::Context context;
    gpu::ContextConfig config;
    config.enableValidation = false;
    if (!context.initHeadless(config)) {
        GTEST_SKIP() << "Headless GPU context not available";
    }

    for (const auto [count, goldenHash] : {
             std::pair{size_t{10'000}, 0xae450de0df3ff3acull},
             std::pair{size_t{16'384}, 0x4b66a55048231b2bull}}) {
        const Result baseline = run(
            context.getDevice(), context.getQueue(), count, false);
        const Result candidate = run(
            context.getDevice(), context.getQueue(), count, true);
        EXPECT_EQ(baseline.outputHash, goldenHash);
        EXPECT_EQ(candidate.outputHash, goldenHash);
        EXPECT_EQ(candidate.bytesPerFrame, sizeof(GpuInstance));
        EXPECT_LE(candidate.retired.p50Ms,
                  baseline.retired.p50Ms * 0.75);

        for (const auto& [mode, result] : {
                 std::pair{"full", &baseline},
                 std::pair{"dirty_range", &candidate}}) {
            std::cout << std::fixed << std::setprecision(6)
                      << "primitive_upload mode=" << mode
                      << " bodies=" << count
                      << " bytes=" << result->bytesPerFrame
                      << " enqueue_p50_ms=" << result->enqueue.p50Ms
                      << " enqueue_p95_ms=" << result->enqueue.p95Ms
                      << " enqueue_p99_ms=" << result->enqueue.p99Ms
                      << " retired_p50_ms=" << result->retired.p50Ms
                      << " retired_p95_ms=" << result->retired.p95Ms
                      << " retired_p99_ms=" << result->retired.p99Ms
                      << " frames_per_s="
                      << result->retired.framesPerSecond
                      << " fnv64=0x" << std::hex << result->outputHash
                      << std::dec << '\n';
        }
    }

    constexpr size_t compactBodyCount = 100'000;
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot> bodies(
        compactBodyCount);
    for (size_t index = 0; index < bodies.size(); ++index) {
        bodies[index].shape = static_cast<physics::ThrowableShape>(
            index % static_cast<size_t>(physics::ThrowableShape::Count));
        bodies[index].position = glm::vec3(
            static_cast<float>(index % 400u), 20.0f,
            static_cast<float>(index / 400u));
        bodies[index].dimensions = glm::vec3(0.7f, 0.8f, 0.9f);
    }
    PrimitivePath path;
    ASSERT_TRUE(path.init(context.getDevice(), context.getQueue()));
    path.setCompactPhysicsInstances(bodies);
    retireQueue(context.getDevice(), context.getQueue());
    const Result fullShapes = runCompactFallback(
        path, bodies, context.getDevice(), context.getQueue(), true);
    path.setCompactPhysicsInstances(bodies);
    retireQueue(context.getDevice(), context.getQueue());
    const Result cachedShapes = runCompactFallback(
        path, bodies, context.getDevice(), context.getQueue(), false);
    EXPECT_EQ(fullShapes.bytesPerFrame, compactBodyCount * 64u);
    EXPECT_EQ(cachedShapes.bytesPerFrame, compactBodyCount * 32u);
    for (const auto& [mode, result] : {
             std::pair{"full_shape", &fullShapes},
             std::pair{"cached_shape", &cachedShapes}}) {
        std::cout << std::fixed << std::setprecision(6)
                  << "primitive_compact_upload mode=" << mode
                  << " bodies=" << compactBodyCount
                  << " bytes=" << result->bytesPerFrame
                  << " enqueue_p50_ms=" << result->enqueue.p50Ms
                  << " retired_p50_ms=" << result->retired.p50Ms
                  << " frames_per_s=" << result->retired.framesPerSecond
                  << '\n';
    }
    path.shutdown();
    context.shutdown();
}

} // namespace
} // namespace voxy::render::detail
