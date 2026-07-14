#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "server/native_gpu_backend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
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

using Clock = std::chrono::steady_clock;
using physics::deterministic::LockstepBody;
using physics::deterministic::LockstepBodyAlive;
using physics::deterministic::LockstepBodyAwake;

double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(
        fraction * static_cast<double>(values.size() - 1u));
    return values[index];
}

TEST(NativeServerBatchBenchmark, OneSubmissionBeatsPerWorldSubmissions) {
    constexpr uint32_t worldCount = 16;
    constexpr uint32_t bodyCapacity = 16;
    constexpr uint32_t warmups = 4;
    constexpr uint32_t samples = 20;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    NativeServerGpuBackend backend;
    ASSERT_TRUE(backend.initialize(
        context.getDevice(), context.getQueue(),
        {.maximumWorlds = worldCount}));
    std::array<ServerWorldHandle, worldCount> handles{};
    for (uint32_t world = 0; world < worldCount; ++world) {
        physics::deterministic::GpuLockstepWorld::Config config;
        config.bodyCapacity = bodyCapacity;
        config.contactCapacity = 32;
        const auto handle = backend.createWorld({
            .worldId = world + 1u,
            .islandId = 1'000u + world,
            .physics = config,
        });
        ASSERT_TRUE(handle.has_value());
        handles[world] = *handle;
        std::vector<LockstepBody> bodies(bodyCapacity);
        bodies[1].identity = {
            1u, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
        bodies[1].sectorRadius = {
            0, 0, 0, physics::deterministic::kLockstepPositionOne / 2};
        bodies[1].positionInvMass = {
            static_cast<int32_t>(world) * 4'096, 40'960, 0,
            physics::deterministic::kLockstepVelocityOne};
        ASSERT_TRUE(backend.uploadBodies(*handle, bodies));
    }

    uint32_t tick = 1;
    const auto run = [&](bool batched) {
        const auto start = Clock::now();
        WGPUSubmissionIndex finalSubmission = 0;
        if (batched) {
            WGPUCommandEncoderDescriptor encoderDescriptor{};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
                context.getDevice(), &encoderDescriptor);
            EXPECT_TRUE(backend.encodeBatch(encoder, tick, handles));
            WGPUCommandBufferDescriptor commandDescriptor{};
            WGPUCommandBuffer command = wgpuCommandEncoderFinish(
                encoder, &commandDescriptor);
            finalSubmission = wgpuQueueSubmitForIndex(
                context.getQueue(), 1u, &command);
            wgpuCommandBufferRelease(command);
            wgpuCommandEncoderRelease(encoder);
        } else {
            for (const auto handle : handles) {
                WGPUCommandEncoderDescriptor encoderDescriptor{};
                WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
                    context.getDevice(), &encoderDescriptor);
                EXPECT_TRUE(backend.encodeBatch(
                    encoder, tick,
                    std::span<const ServerWorldHandle>(&handle, 1u)));
                WGPUCommandBufferDescriptor commandDescriptor{};
                WGPUCommandBuffer command = wgpuCommandEncoderFinish(
                    encoder, &commandDescriptor);
                finalSubmission = wgpuQueueSubmitForIndex(
                    context.getQueue(), 1u, &command);
                wgpuCommandBufferRelease(command);
                wgpuCommandEncoderRelease(encoder);
            }
        }
        const WGPUWrappedSubmissionIndex submission{
            context.getQueue(), finalSubmission};
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
        ++tick;
        return std::chrono::duration<double, std::milli>(
            Clock::now() - start).count();
    };

    for (uint32_t warmup = 0; warmup < warmups; ++warmup) {
        static_cast<void>(run(true));
        static_cast<void>(run(false));
    }
    std::vector<double> batched;
    std::vector<double> isolated;
    for (uint32_t sample = 0; sample < samples; ++sample) {
        if ((sample & 1u) == 0u) {
            batched.push_back(run(true));
            isolated.push_back(run(false));
        } else {
            isolated.push_back(run(false));
            batched.push_back(run(true));
        }
    }
    const double batchedP50 = percentile(batched, 0.50);
    const double batchedP95 = percentile(batched, 0.95);
    const double isolatedP50 = percentile(isolated, 0.50);
    const double isolatedP95 = percentile(isolated, 0.95);
    EXPECT_LT(batchedP50, isolatedP50);
    std::cout << std::fixed << std::setprecision(3)
              << "native_server_batch worlds=" << worldCount
              << " bodies_per_world=" << bodyCapacity
              << " batched_p50_ms=" << batchedP50
              << " batched_p95_ms=" << batchedP95
              << " isolated_p50_ms=" << isolatedP50
              << " isolated_p95_ms=" << isolatedP95
              << " submission_reduction=" << worldCount << "x\n";
    backend.shutdown();
}

} // namespace
} // namespace voxy::server
