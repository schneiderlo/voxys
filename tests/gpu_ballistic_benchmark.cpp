#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/physics_world.hpp"
#include "render/primitive_path.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
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

namespace voxy::physics {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kDefaultBodyCount = 100'000;
constexpr uint32_t kWarmupFrames = 4;
constexpr uint32_t kMeasuredFrames = 20;

uint32_t benchmarkBodyCount() {
    const char* text = std::getenv("VOXY_GPU_BALLISTIC_BODIES");
    if (!text || *text == '\0') return kDefaultBodyCount;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    return end != text && *end == '\0' && value > 0u
            && value <= 131'071u
        ? static_cast<uint32_t>(value) : kDefaultBodyCount;
}

bool benchmarkDenseOverlap() {
    const char* text = std::getenv("VOXY_GPU_BALLISTIC_DENSE_OVERLAP");
    return text && std::string_view(text) == "1";
}

double timestampPeriodNanoseconds() {
    const char* text = std::getenv("VOXY_GPU_TIMESTAMP_PERIOD_NS");
    if (!text || *text == '\0') return 1.0;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    return end != text && *end == '\0' && std::isfinite(value) && value > 0.0
        ? value : 1.0;
}

double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    const size_t rank = static_cast<size_t>(std::ceil(
        fraction * static_cast<double>(samples.size())));
    return samples[std::min(std::max<size_t>(rank, 1) - 1,
                            samples.size() - 1)];
}

TEST(GpuBallisticBenchmark, UpdatesAndRendersConfiguredBodiesDirectly) {
    const uint32_t bodyCount = benchmarkBodyCount();
    const bool denseOverlap = benchmarkDenseOverlap();
    const double timestampPeriod = timestampPeriodNanoseconds();
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    contextConfig.enableTimestamps = true;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    WGPUQuerySet renderQuerySet = nullptr;
    WGPUBuffer renderResolveBuffer = nullptr;
    WGPUBuffer renderReadbackBuffer = nullptr;
    if (wgpuDeviceHasFeature(
            context.getDevice(), WGPUFeatureName_TimestampQuery)) {
        WGPUQuerySetDescriptor queryDesc{};
        queryDesc.label = "gpu_ballistic_render_timestamps";
        queryDesc.type = WGPUQueryType_Timestamp;
        queryDesc.count = 2u;
        renderQuerySet = wgpuDeviceCreateQuerySet(
            context.getDevice(), &queryDesc);
        renderResolveBuffer = gpu::createBuffer(
            context.getDevice(), gpu::BufferDesc{
                .label = "gpu_ballistic_render_resolve",
                .size = 2u * sizeof(uint64_t),
                .usage = WGPUBufferUsage_QueryResolve
                       | WGPUBufferUsage_CopySrc,
            });
        renderReadbackBuffer = gpu::createBuffer(
            context.getDevice(), gpu::BufferDesc{
                .label = "gpu_ballistic_render_readback",
                .size = 2u * sizeof(uint64_t),
                .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
            });
        ASSERT_NE(renderQuerySet, nullptr);
        ASSERT_NE(renderResolveBuffer, nullptr);
        ASSERT_NE(renderReadbackBuffer, nullptr);
    }

    PhysicsWorld world;
    PhysicsInitContext physicsConfig;
    physicsConfig.requestedBackend = BackendType::WebGpuSoft;
    physicsConfig.device = context.getDevice();
    physicsConfig.queue = context.getQueue();
    physicsConfig.maxBodies = 131'072;
    physicsConfig.maxActiveBodies = 131'072;
    physicsConfig.maxPairs = denseOverlap ? 65'536u : 8'192u;
    physicsConfig.maxContacts = denseOverlap ? 16'384u : 4'096u;
    physicsConfig.maxManifolds = denseOverlap ? 65'536u : 8'192u;
    physicsConfig.gpu.enableStageProfiling = true;
    physicsConfig.gpu.stageProfilingTimestampPeriodNanoseconds =
        timestampPeriod;
    ASSERT_TRUE(world.initialize(physicsConfig));

    render::PrimitivePathConfig renderConfig;
    renderConfig.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    render::PrimitivePath renderer;
    ASSERT_TRUE(renderer.init(
        context.getDevice(), context.getQueue(), renderConfig));

    WGPUTexture colorTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::renderTarget(
            128, 128, WGPUTextureFormat_RGBA8Unorm,
            "gpu_ballistic_benchmark_color"));
    WGPUTexture depthTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::depth(
            128, 128, WGPUTextureFormat_Depth32Float,
            "gpu_ballistic_benchmark_depth"));
    const float clearRayDepth = -1.0f;
    WGPUTexture rayTexture = gpu::createTextureWithData(
        context.getDevice(), context.getQueue(),
        gpu::TextureDesc::tex2D(
            1, 1, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "gpu_ballistic_benchmark_ray_depth"),
        std::as_bytes(std::span<const float>(&clearRayDepth, 1)),
        sizeof(float));
    ASSERT_NE(colorTexture, nullptr);
    ASSERT_NE(depthTexture, nullptr);
    ASSERT_NE(rayTexture, nullptr);
    WGPUTextureView colorView = gpu::createTextureView(colorTexture);
    WGPUTextureView depthView = gpu::createTextureView(depthTexture);
    WGPUTextureView rayView = gpu::createTextureView(rayTexture);
    ASSERT_NE(colorView, nullptr);
    ASSERT_NE(depthView, nullptr);
    ASSERT_NE(rayView, nullptr);
    renderer.setRayDepthTexture(rayView);

    BodySpawnDesc body;
    for (uint32_t index = 0; index < bodyCount; ++index) {
        const uint32_t x = index % 400u;
        const uint32_t z = index / 400u;
        body.shape = static_cast<ThrowableShape>(
            index % static_cast<uint32_t>(ThrowableShape::Count));
        body.position = denseOverlap
            ? glm::vec3(0.0f, 20.0f, 0.0f)
            : glm::vec3(
                (static_cast<float>(x) - 199.5f) * 4.0f,
                20.0f + static_cast<float>(index % 17u) * 0.15f,
                static_cast<float>(z) * 4.0f);
        body.linearVelocity = {
            static_cast<float>(int32_t(index % 7u) - 3) * 0.03f,
            0.0f,
            static_cast<float>(int32_t(index % 5u) - 2) * 0.02f};
        body.angularVelocity = {0.1f, 0.2f, 0.05f};
        body.dimensions = throwableShapeDimensions(body.shape) * 0.7f;
        ASSERT_TRUE(world.spawnBody(body).valid());
    }
    ASSERT_EQ(world.stats().residentBodies, bodyCount);
    ASSERT_TRUE(world.dynamicBodies().empty());
    renderer.setPhysicsRenderView(world.renderView());

    const glm::vec3 cameraPosition(0.0f, 600.0f, -1'800.0f);
    const glm::mat4 view = glm::lookAt(
        cameraPosition, glm::vec3(0.0f, 20.0f, 500.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(
        glm::radians(70.0f), 1.0f, 0.1f, 5'000.0f);
    const glm::vec3 light = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));

    std::vector<double> retiredFrameMs;
    retiredFrameMs.reserve(kMeasuredFrames);
    std::vector<double> updateMs;
    std::vector<double> physicsEncodeMs;
    std::vector<double> renderEncodeMs;
    std::vector<double> commandFinishMs;
    std::vector<double> queueSubmitMs;
    std::vector<double> gpuWaitMs;
    std::vector<double> renderGpuMs;
    std::array<std::vector<double>, kPhysicsGpuStageCount> stageMs;
    for (auto& samples : stageMs) samples.reserve(kMeasuredFrames);
    for (uint32_t frame = 0; frame < kWarmupFrames + kMeasuredFrames; ++frame) {
        const auto start = Clock::now();
        world.update(1.0f / 60.0f);
        const auto afterUpdate = Clock::now();
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDesc);
        world.encodeGpuStep(encoder);
        const auto afterPhysicsEncode = Clock::now();
        const auto writeRenderBoundary = [&](uint32_t query) {
            if (!renderQuerySet) return;
            gpu::CompatPassTimestampWrites writes{};
            writes.querySet = renderQuerySet;
            writes.beginningOfPassWriteIndex = query;
            writes.endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
            WGPUComputePassDescriptor boundaryDesc{};
            boundaryDesc.timestampWrites = &writes;
            WGPUComputePassEncoder boundary =
                wgpuCommandEncoderBeginComputePass(encoder, &boundaryDesc);
            wgpuComputePassEncoderEnd(boundary);
            wgpuComputePassEncoderRelease(boundary);
        };
        writeRenderBoundary(0u);
        renderer.render(
            encoder, colorView, depthView, view, projection, cameraPosition,
            light, 128, 128, false);
        writeRenderBoundary(1u);
        if (renderQuerySet) {
            wgpuCommandEncoderResolveQuerySet(
                encoder, renderQuerySet, 0u, 2u, renderResolveBuffer, 0u);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, renderResolveBuffer, 0u, renderReadbackBuffer, 0u,
                2u * sizeof(uint64_t));
        }
        const auto afterRenderEncode = Clock::now();
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        const auto afterCommandFinish = Clock::now();
        const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
            context.getQueue(), 1, &command);
        const auto afterQueueSubmit = Clock::now();
        const WGPUWrappedSubmissionIndex submission{
            context.getQueue(), submissionIndex};
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
        const auto afterRetire = Clock::now();
        double frameRenderGpuMs = 0.0;
        bool hasFrameRenderGpuTime = false;
        if (renderReadbackBuffer) {
            struct MapState { bool done = false; bool success = false; } state;
            const auto callback = [](
                WGPUBufferMapAsyncStatus status, void* userdata) {
                auto& value = *static_cast<MapState*>(userdata);
                value.success = status == WGPUBufferMapAsyncStatus_Success;
                value.done = true;
            };
            wgpuBufferMapAsync(
                renderReadbackBuffer, WGPUMapMode_Read, 0u,
                2u * sizeof(uint64_t), callback, &state);
            while (!state.done) {
                static_cast<void>(wgpuDevicePoll(
                    context.getDevice(), true, &submission));
            }
            if (state.success) {
                std::array<uint64_t, 2> timestamps{};
                const void* mapped = wgpuBufferGetConstMappedRange(
                    renderReadbackBuffer, 0u, sizeof(timestamps));
                if (mapped) {
                    std::memcpy(timestamps.data(), mapped, sizeof(timestamps));
                    frameRenderGpuMs = static_cast<double>(
                        timestamps[1] - timestamps[0])
                        * timestampPeriod * 1.0e-6;
                    hasFrameRenderGpuTime = true;
                }
                wgpuBufferUnmap(renderReadbackBuffer);
            }
        }
        auto stageTiming = world.pollGpuStageTimings();
        for (uint32_t attempt = 0u; !stageTiming && attempt < 8u; ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                context.getDevice(), true, &submission));
            stageTiming = world.pollGpuStageTimings();
        }
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        if (frame >= kWarmupFrames) {
            const auto elapsedMs = [](auto first, auto last) {
                return std::chrono::duration<double, std::milli>(
                    last - first).count();
            };
            retiredFrameMs.push_back(
                elapsedMs(start, Clock::now()));
            updateMs.push_back(elapsedMs(start, afterUpdate));
            physicsEncodeMs.push_back(
                elapsedMs(afterUpdate, afterPhysicsEncode));
            renderEncodeMs.push_back(
                elapsedMs(afterPhysicsEncode, afterRenderEncode));
            commandFinishMs.push_back(
                elapsedMs(afterRenderEncode, afterCommandFinish));
            queueSubmitMs.push_back(
                elapsedMs(afterCommandFinish, afterQueueSubmit));
            gpuWaitMs.push_back(elapsedMs(afterQueueSubmit, afterRetire));
            if (hasFrameRenderGpuTime) renderGpuMs.push_back(frameRenderGpuMs);
            if (stageTiming) {
                for (size_t stage = 0; stage < kPhysicsGpuStageCount; ++stage) {
                    stageMs[stage].push_back(stageTiming->milliseconds[stage]);
                }
            }
        }
    }

    EXPECT_EQ(renderer.lastCompactUploadStats().bytesUploaded, 0u);
    EXPECT_EQ(renderer.lastUploadStats().bytesUploaded, 0u);
    EXPECT_TRUE(world.dynamicBodies().empty());
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 0u);
    const PhysicsStats finalStats = world.stats();
    EXPECT_GT(finalStats.telemetryTick, 0u);
    std::cout << std::fixed << std::setprecision(3)
              << "gpu_ballistic bodies=" << bodyCount
              << " pairs=" << finalStats.highPairs
              << " contacts=" << finalStats.highContacts
              << " manifolds=" << finalStats.highManifolds
              << " retired_p50_ms=" << percentile(retiredFrameMs, 0.50)
              << " retired_p95_ms=" << percentile(retiredFrameMs, 0.95)
              << " persistent_mib="
              << static_cast<double>(finalStats.estimatedPersistentBytes)
                    / (1024.0 * 1024.0)
              << " scratch_mib="
              << static_cast<double>(finalStats.scratchBytes)
                    / (1024.0 * 1024.0)
              << " timestamp_period_ns=" << timestampPeriod
              << '\n';
    std::cout << "cpu_stage name=update p50_ms="
              << percentile(updateMs, 0.50)
              << " p95_ms=" << percentile(updateMs, 0.95) << '\n'
              << "cpu_stage name=physics_encode p50_ms="
              << percentile(physicsEncodeMs, 0.50)
              << " p95_ms=" << percentile(physicsEncodeMs, 0.95) << '\n'
              << "cpu_stage name=render_encode p50_ms="
              << percentile(renderEncodeMs, 0.50)
              << " p95_ms=" << percentile(renderEncodeMs, 0.95) << '\n'
              << "cpu_stage name=command_finish p50_ms="
              << percentile(commandFinishMs, 0.50)
              << " p95_ms=" << percentile(commandFinishMs, 0.95) << '\n'
              << "cpu_stage name=queue_submit p50_ms="
              << percentile(queueSubmitMs, 0.50)
              << " p95_ms=" << percentile(queueSubmitMs, 0.95) << '\n'
              << "cpu_stage name=gpu_wait p50_ms="
              << percentile(gpuWaitMs, 0.50)
              << " p95_ms=" << percentile(gpuWaitMs, 0.95) << '\n';
    if (renderGpuMs.size() == kMeasuredFrames) {
        std::cout << "gpu_stage name=culling_render p50_ms="
                  << percentile(renderGpuMs, 0.50)
                  << " p95_ms=" << percentile(renderGpuMs, 0.95) << '\n';
    }
    if (stageMs[0].size() == kMeasuredFrames) {
        for (size_t stage = 0; stage < kPhysicsGpuStageCount; ++stage) {
            std::cout << "gpu_stage name="
                      << physicsGpuStageName(
                          static_cast<PhysicsGpuStage>(stage))
                      << " p50_ms=" << percentile(stageMs[stage], 0.50)
                      << " p95_ms=" << percentile(stageMs[stage], 0.95)
                      << '\n';
        }
    } else {
        std::cout << "gpu_stage unavailable=1\n";
    }

    wgpuTextureViewRelease(rayView);
    if (renderReadbackBuffer) {
        wgpuBufferDestroy(renderReadbackBuffer);
        wgpuBufferRelease(renderReadbackBuffer);
    }
    if (renderResolveBuffer) {
        wgpuBufferDestroy(renderResolveBuffer);
        wgpuBufferRelease(renderResolveBuffer);
    }
    if (renderQuerySet) wgpuQuerySetRelease(renderQuerySet);
    wgpuTextureViewRelease(depthView);
    wgpuTextureViewRelease(colorView);
    wgpuTextureDestroy(rayTexture);
    wgpuTextureRelease(rayTexture);
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);
    wgpuTextureDestroy(colorTexture);
    wgpuTextureRelease(colorTexture);
}

} // namespace
} // namespace voxy::physics
