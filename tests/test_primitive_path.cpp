#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "render/primitive_path.hpp"

#include <filesystem>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <span>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::render {

TEST(PrimitivePathTest, RejectsNullGpuHandles) {
    PrimitivePath path;
    EXPECT_FALSE(path.init(nullptr, nullptr));
}

TEST(PrimitivePathTest, SleepingInstanceCacheIsBitExact) {
    using Shape = physics::PhysicsWorld::ThrowableShape;
    std::array bodies = {
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Cylinder, glm::vec3(2.0f, 1.0f, -3.0f),
            glm::quat(0.5f, 0.5f, 0.5f, 0.5f), glm::vec3(0.9f, 1.1f, 0.9f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Sphere, glm::vec3(-2.0f, 0.0f, 0.0f), {}, glm::vec3(1.2f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Box, glm::vec3(0.0f), {}, glm::vec3(1.8f, 0.8f, 1.0f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Sphere, glm::vec3(4.0f, 5.0f, 6.0f),
            glm::quat(0.9238795f, 0.0f, 0.38268343f, 0.0f), glm::vec3(0.7f)},
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Count, glm::vec3(99.0f), {}, glm::vec3(99.0f)},
    };

    detail::PrimitiveInstanceCache cache;
    const auto expected = detail::packPrimitiveInstances(bodies);
    const auto first = detail::packPrimitiveInstances(bodies, cache);
    const auto cached = detail::packPrimitiveInstances(bodies, cache);

    ASSERT_EQ(cached.instances.size(), expected.instances.size());
    EXPECT_EQ(cached.firstInstances, expected.firstInstances);
    EXPECT_EQ(cached.instanceCounts, expected.instanceCounts);
    EXPECT_EQ(first.firstInstances, expected.firstInstances);
    EXPECT_EQ(first.instanceCounts, expected.instanceCounts);
    EXPECT_EQ(std::memcmp(cached.instances.data(), expected.instances.data(),
                          expected.instances.size() * sizeof(detail::GpuInstance)), 0);

    bodies[2].position.x = -0.0f;
    const auto signedZeroExpected = detail::packPrimitiveInstances(bodies);
    const auto signedZeroCached = detail::packPrimitiveInstances(bodies, cache);
    ASSERT_EQ(signedZeroCached.instances.size(), signedZeroExpected.instances.size());
    EXPECT_EQ(std::memcmp(signedZeroCached.instances.data(),
                          signedZeroExpected.instances.data(),
                          signedZeroExpected.instances.size()
                              * sizeof(detail::GpuInstance)), 0);
    EXPECT_EQ(std::bit_cast<uint32_t>(cache.entries[2].body.position.x),
              std::bit_cast<uint32_t>(-0.0f));
}

TEST(PrimitivePathTest, ActiveBodiesBypassInstanceCache) {
    using Shape = physics::PhysicsWorld::ThrowableShape;
    const std::array bodies = {
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Sphere, glm::vec3(1.0f, 2.0f, 3.0f), {}, glm::vec3(1.2f), true},
        physics::PhysicsWorld::DynamicBodySnapshot{
            Shape::Cube, glm::vec3(-3.0f, -2.0f, -1.0f), {}, glm::vec3(1.1f), true},
    };

    detail::PrimitiveInstanceCache cache;
    const auto expected = detail::packPrimitiveInstances(bodies);
    const auto actual = detail::packPrimitiveInstances(bodies, cache);
    ASSERT_EQ(actual.instances.size(), expected.instances.size());
    EXPECT_EQ(std::memcmp(actual.instances.data(), expected.instances.data(),
                          expected.instances.size() * sizeof(detail::GpuInstance)), 0);
    EXPECT_TRUE(cache.entries.empty());
}

TEST(PrimitivePathTest, PlansOnlyProvenDifferentInstanceRanges) {
    std::vector<uint64_t> previous(64);
    for (size_t index = 0; index < previous.size(); ++index) {
        previous[index] = index + 1;
    }
    auto current = previous;

    const auto first = detail::planPrimitiveInstanceUpload(
        current.size(), current, {}, false);
    EXPECT_TRUE(first.fullUpload);
    EXPECT_EQ(first.byteCount,
              current.size() * sizeof(detail::GpuInstance));

    const auto unchanged = detail::planPrimitiveInstanceUpload(
        current.size(), current, previous, true);
    EXPECT_FALSE(unchanged.fullUpload);
    EXPECT_EQ(unchanged.rangeCount, 0u);
    EXPECT_EQ(unchanged.byteCount, 0u);

    current[7] = 1001;
    current[8] = 1002;
    current[40] = 0;
    const auto sparse = detail::planPrimitiveInstanceUpload(
        current.size(), current, previous, true);
    ASSERT_FALSE(sparse.fullUpload);
    ASSERT_EQ(sparse.rangeCount, 2u);
    EXPECT_EQ(sparse.ranges[0].firstInstance, 7u);
    EXPECT_EQ(sparse.ranges[0].instanceCount, 2u);
    EXPECT_EQ(sparse.ranges[1].firstInstance, 40u);
    EXPECT_EQ(sparse.ranges[1].instanceCount, 1u);
    EXPECT_EQ(sparse.byteCount, 3u * sizeof(detail::GpuInstance));

    std::vector<detail::GpuInstance> previousInstances(current.size());
    for (size_t index = 0; index < previousInstances.size(); ++index) {
        previousInstances[index].model[3][0] = static_cast<float>(index);
    }
    auto currentInstances = previousInstances;
    currentInstances[7].model[3][0] = -0.0f;
    currentInstances[8].model[3][0] = 123.0f;
    currentInstances[40].color.x = 0.25f;
    auto mirror = previousInstances;
    for (size_t index = 0; index < sparse.rangeCount; ++index) {
        const auto& range = sparse.ranges[index];
        std::memcpy(mirror.data() + range.firstInstance,
                    currentInstances.data() + range.firstInstance,
                    range.instanceCount * sizeof(detail::GpuInstance));
    }
    EXPECT_EQ(std::memcmp(mirror.data(), currentInstances.data(),
                          currentInstances.size()
                              * sizeof(detail::GpuInstance)), 0);
}

TEST(PrimitivePathTest, FallsBackForDenseOrFragmentedChanges) {
    std::vector<uint64_t> previous(64, 1);
    auto dense = previous;
    for (size_t index = 0; index < dense.size() / 2; ++index) {
        dense[index] = 2;
    }
    const auto densePlan = detail::planPrimitiveInstanceUpload(
        dense.size(), dense, previous, true);
    EXPECT_TRUE(densePlan.fullUpload);
    EXPECT_EQ(densePlan.byteCount,
              dense.size() * sizeof(detail::GpuInstance));

    std::vector<uint64_t> fragmentedPrevious(68, 1);
    auto fragmented = fragmentedPrevious;
    for (size_t index = 0; index < fragmented.size(); index += 4) {
        fragmented[index] = 2;
    }
    const auto fragmentedPlan = detail::planPrimitiveInstanceUpload(
        fragmented.size(), fragmented, fragmentedPrevious, true);
    EXPECT_TRUE(fragmentedPlan.fullUpload);
    EXPECT_EQ(fragmentedPlan.byteCount,
              fragmented.size() * sizeof(detail::GpuInstance));
}

TEST(PrimitivePathTest, SleepingTokenCannotSkipAnActiveOverwrite) {
    using Shape = physics::PhysicsWorld::ThrowableShape;
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot> bodies(
        64, {Shape::Sphere, glm::vec3(1.0f, 2.0f, 3.0f), {},
             glm::vec3(1.0f), false});
    detail::PrimitiveInstanceCache cache;
    const auto sleeping = detail::packPrimitiveInstances(bodies, cache);
    ASSERT_EQ(sleeping.cacheTokens.size(), bodies.size());
    ASSERT_NE(sleeping.cacheTokens.front(), 0u);

    bodies.front().active = true;
    bodies.front().position.x = 9.0f;
    const auto active = detail::packPrimitiveInstances(bodies, cache);
    ASSERT_EQ(active.cacheTokens.size(), bodies.size());
    EXPECT_EQ(active.cacheTokens.front(), 0u);
    const auto activePlan = detail::planPrimitiveInstanceUpload(
        active.instances.size(), active.cacheTokens, sleeping.cacheTokens,
        true);
    ASSERT_FALSE(activePlan.fullUpload);
    ASSERT_EQ(activePlan.rangeCount, 1u);
    EXPECT_EQ(activePlan.ranges[0].firstInstance, 0u);

    bodies.front().active = false;
    bodies.front().position.x = 1.0f;
    const auto sleepingAgain = detail::packPrimitiveInstances(bodies, cache);
    EXPECT_EQ(sleepingAgain.cacheTokens.front(),
              sleeping.cacheTokens.front());
    const auto restoredPlan = detail::planPrimitiveInstanceUpload(
        sleepingAgain.instances.size(), sleepingAgain.cacheTokens,
        active.cacheTokens, true);
    ASSERT_FALSE(restoredPlan.fullUpload);
    ASSERT_EQ(restoredPlan.rangeCount, 1u);
    EXPECT_EQ(restoredPlan.ranges[0].firstInstance, 0u);
    EXPECT_EQ(restoredPlan.byteCount, sizeof(detail::GpuInstance));
}

TEST(PrimitivePathTest, TokenWrapForcesFullUploadBeforeReuse) {
    using Shape = physics::PhysicsWorld::ThrowableShape;
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot> bodies(
        64, {Shape::Sphere, glm::vec3(1.0f), {}, glm::vec3(1.0f), false});
    detail::PrimitiveInstanceCache cache;
    cache.nextToken = std::numeric_limits<uint64_t>::max();
    const auto batch = detail::packPrimitiveInstances(bodies, cache);
    ASSERT_TRUE(batch.forceFullUpload);
    ASSERT_EQ(batch.cacheTokens.size(), batch.instances.size());

    const auto plan = detail::planPrimitiveInstanceUpload(
        batch.instances.size(), batch.cacheTokens, batch.cacheTokens, true,
        batch.forceFullUpload);
    EXPECT_TRUE(plan.fullUpload);
    EXPECT_EQ(plan.byteCount,
              batch.instances.size() * sizeof(detail::GpuInstance));
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
    EXPECT_TRUE(path.lastUploadStats().fullUpload);
    EXPECT_EQ(path.lastUploadStats().bytesUploaded,
              bodies.size() * sizeof(detail::GpuInstance));
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot> manyBodies(
        130, bodies.front());
    path.setInstances(manyBodies);
    EXPECT_TRUE(path.lastUploadStats().fullUpload);
    EXPECT_EQ(path.lastUploadStats().bytesUploaded,
              manyBodies.size() * sizeof(detail::GpuInstance));
    path.setInstances(manyBodies);
    EXPECT_FALSE(path.lastUploadStats().fullUpload);
    EXPECT_EQ(path.lastUploadStats().writeCalls, 0u);
    EXPECT_EQ(path.lastUploadStats().bytesUploaded, 0u);
    manyBodies[64].position.x = -0.0f;
    path.setInstances(manyBodies);
    EXPECT_FALSE(path.lastUploadStats().fullUpload);
    EXPECT_EQ(path.lastUploadStats().writeCalls, 1u);
    EXPECT_EQ(path.lastUploadStats().bytesUploaded,
              sizeof(detail::GpuInstance));

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
