#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "render/primitive_path.hpp"

#include <filesystem>
#include <array>
#include <span>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::render {

TEST(PrimitivePathTest, RejectsNullGpuHandles) {
    PrimitivePath path;
    EXPECT_FALSE(path.init(nullptr, nullptr));
}

TEST(PrimitivePathGPUTest, CompilesPrimitivePipeline) {
    gpu::Context context;
    if (!context.initHeadless()) {
        GTEST_SKIP() << "GPU context not available";
    }

    PrimitivePathConfig config;
    config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    for (const auto& candidate : {
             std::filesystem::path("shaders/physics_primitives.wgsl"),
             std::filesystem::path("../shaders/physics_primitives.wgsl"),
             std::filesystem::path("../../shaders/physics_primitives.wgsl")}) {
        if (std::filesystem::exists(candidate)) {
            config.shaderPath = candidate;
            break;
        }
    }
    ASSERT_TRUE(std::filesystem::exists(config.shaderPath));

    PrimitivePath path;
    EXPECT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    EXPECT_TRUE(path.isInitialized());

    auto colorTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::renderTarget(
            64, 64, WGPUTextureFormat_RGBA8Unorm, "primitive_test_color"));
    auto depthTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::depth(
            64, 64, WGPUTextureFormat_Depth32Float, "primitive_test_depth"));
    const float clearRayDepth = -1.0f;
    auto rayTexture = gpu::createTextureWithData(
        context.getDevice(), context.getQueue(),
        gpu::TextureDesc::tex2D(
            1, 1, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "primitive_test_ray_depth"),
        std::as_bytes(std::span<const float>(&clearRayDepth, 1)), sizeof(float));
    ASSERT_NE(colorTexture, nullptr);
    ASSERT_NE(depthTexture, nullptr);
    ASSERT_NE(rayTexture, nullptr);
    auto colorView = gpu::createTextureView(colorTexture);
    auto depthView = gpu::createTextureView(depthTexture);
    auto rayView = gpu::createTextureView(rayTexture);
    ASSERT_NE(colorView, nullptr);
    ASSERT_NE(depthView, nullptr);
    ASSERT_NE(rayView, nullptr);

    path.setRayDepthTexture(rayView);
    const std::array bodies = {
        physics::PhysicsWorld::DynamicBodySnapshot{
            physics::PhysicsWorld::ThrowableShape::Sphere,
            glm::vec3(-2.0f, 0.0f, 0.0f), {}, glm::vec3(1.2f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            physics::PhysicsWorld::ThrowableShape::Cube,
            glm::vec3(-1.0f, 0.0f, 0.0f), {}, glm::vec3(1.1f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            physics::PhysicsWorld::ThrowableShape::Box,
            glm::vec3(0.0f), {}, glm::vec3(1.8f, 0.8f, 1.0f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            physics::PhysicsWorld::ThrowableShape::Capsule,
            glm::vec3(1.0f, 0.0f, 0.0f), {}, glm::vec3(0.7f, 1.8f, 0.7f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            physics::PhysicsWorld::ThrowableShape::Cylinder,
            glm::vec3(2.0f, 0.0f, 0.0f), {}, glm::vec3(0.9f, 1.1f, 0.9f)}};
    path.setInstances(bodies);
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot> manyBodies(
        130, bodies.front());
    path.setInstances(manyBodies);

    WGPUCommandEncoderDescriptor encoderDesc{};
    auto encoder = wgpuDeviceCreateCommandEncoder(context.getDevice(), &encoderDesc);
    const glm::vec3 cameraPosition(0.0f, 0.0f, -5.0f);
    path.render(encoder, colorView, depthView,
                glm::lookAt(cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f),
                cameraPosition, glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f)),
                64, 64, true);
    WGPUCommandBufferDescriptor commandDesc{};
    auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    ASSERT_NE(command, nullptr);
    wgpuQueueSubmit(context.getQueue(), 1, &command);
    context.tick();

    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(rayView);
    wgpuTextureViewRelease(depthView);
    wgpuTextureViewRelease(colorView);
    wgpuTextureDestroy(rayTexture);
    wgpuTextureRelease(rayTexture);
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);
    wgpuTextureDestroy(colorTexture);
    wgpuTextureRelease(colorTexture);
    path.shutdown();
    context.shutdown();
}

} // namespace voxy::render
