#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/physics_world.hpp"
#include "render/primitive_path.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
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

namespace voxy::physics {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kBodyCount = 100'000;
constexpr uint32_t kWarmupFrames = 60;
constexpr uint32_t kMeasuredFrames = 20;

double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    const size_t rank = static_cast<size_t>(std::ceil(
        fraction * static_cast<double>(samples.size())));
    return samples[std::min(std::max<size_t>(rank, 1) - 1,
                            samples.size() - 1)];
}

TEST(GpuStaticBenchmark,
     SimulatesTerrainWaterAndDirectRenderingForOneHundredThousandBodies) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    PhysicsWorld world;
    PhysicsInitContext physicsConfig;
    physicsConfig.requestedBackend = BackendType::WebGpuSoft;
    physicsConfig.device = context.getDevice();
    physicsConfig.queue = context.getQueue();
    physicsConfig.maxBodies = 131'072;
    physicsConfig.maxActiveBodies = 131'072;
    physicsConfig.gpu.maximumLinearSpeed = 50.0f;
    ASSERT_TRUE(world.initialize(physicsConfig));

    constexpr uint32_t terrainExtent = 512;
    std::vector<uint16_t> terrain(terrainExtent * terrainExtent);
    for (uint32_t z = 0; z < terrainExtent; ++z) {
        for (uint32_t x = 0; x < terrainExtent; ++x) {
            const uint32_t ripples = ((x / 16u + z / 16u) & 1u) * 24u;
            terrain[z * terrainExtent + x] = static_cast<uint16_t>(
                30'000u + x * 12u + ripples);
        }
    }
    ASSERT_TRUE(world.setTerrain(
        terrain, terrainExtent, terrainExtent, 10.0f, 1.0f));
    world.setWaterPlane(0.0f, true);

    render::PrimitivePathConfig renderConfig;
    renderConfig.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    render::PrimitivePath renderer;
    ASSERT_TRUE(renderer.init(
        context.getDevice(), context.getQueue(), renderConfig));

    WGPUTexture colorTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::renderTarget(
            128, 128, WGPUTextureFormat_RGBA8Unorm,
            "gpu_static_benchmark_color"));
    WGPUTexture depthTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::depth(
            128, 128, WGPUTextureFormat_Depth32Float,
            "gpu_static_benchmark_depth"));
    const float clearRayDepth = -1.0f;
    WGPUTexture rayTexture = gpu::createTextureWithData(
        context.getDevice(), context.getQueue(),
        gpu::TextureDesc::tex2D(
            1, 1, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "gpu_static_benchmark_ray_depth"),
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
    for (uint32_t index = 0; index < kBodyCount; ++index) {
        const uint32_t x = index % 400u;
        const uint32_t z = index / 400u;
        body.shape = static_cast<ThrowableShape>(
            index % static_cast<uint32_t>(ThrowableShape::Count));
        body.position = {
            (static_cast<float>(x) - 199.5f) * 0.9f,
            0.8f + static_cast<float>(index % 13u) * 0.10f,
            (static_cast<float>(z) - 124.5f) * 1.5f};
        body.linearVelocity = {
            -0.25f - static_cast<float>(index % 5u) * 0.02f,
            0.0f,
            static_cast<float>(int32_t(index % 7u) - 3) * 0.03f};
        body.angularVelocity = {0.3f, 0.5f, 0.2f};
        body.dimensions = throwableShapeDimensions(body.shape) * 0.6f;
        ASSERT_TRUE(world.spawnBody(body).valid());
    }
    ASSERT_EQ(world.stats().residentBodies, kBodyCount);
    renderer.setPhysicsRenderView(world.renderView());

    const glm::vec3 cameraPosition(0.0f, 180.0f, -440.0f);
    const glm::mat4 view = glm::lookAt(
        cameraPosition, glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(
        glm::radians(70.0f), 1.0f, 0.1f, 2'000.0f);
    const glm::vec3 light = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));

    std::vector<double> retiredFrameMs;
    retiredFrameMs.reserve(kMeasuredFrames);
    for (uint32_t frame = 0; frame < kWarmupFrames + kMeasuredFrames; ++frame) {
        const auto start = Clock::now();
        world.update(1.0f / 60.0f);
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDesc);
        world.encodeGpuStep(encoder);
        renderer.render(
            encoder, colorView, depthView, view, projection, cameraPosition,
            light, 128, 128, false);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
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

    const double p50 = percentile(retiredFrameMs, 0.50);
    const double p95 = percentile(retiredFrameMs, 0.95);
    EXPECT_EQ(renderer.lastCompactUploadStats().bytesUploaded, 0u);
    EXPECT_EQ(renderer.lastUploadStats().bytesUploaded, 0u);
    EXPECT_TRUE(world.dynamicBodies().empty());
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 0u);
    EXPECT_LT(p95, 16.667);
    std::cout << std::fixed << std::setprecision(3)
              << "gpu_static bodies=" << kBodyCount
              << " retired_p50_ms=" << p50
              << " retired_p95_ms=" << p95
              << " persistent_mib="
              << static_cast<double>(world.stats().estimatedPersistentBytes)
                    / (1024.0 * 1024.0)
              << " scratch_mib="
              << static_cast<double>(world.stats().scratchBytes)
                    / (1024.0 * 1024.0)
              << '\n';

    wgpuTextureViewRelease(rayView);
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
