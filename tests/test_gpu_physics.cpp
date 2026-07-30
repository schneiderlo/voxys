#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_buffer_arena.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

#ifndef WGPUWrappedSubmissionIndex
struct WGPUWrappedSubmissionIndex;
#endif

extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);

namespace voxy::physics {
namespace {

TEST(GpuBufferArenaTest, TracksAndReleasesSuccessfulAllocations) {
    gpu::Context context;
    gpu::ContextConfig config;
    config.enableValidation = false;
    if (!context.initHeadless(config)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    GpuBufferArena arena;
    arena.initialize(context.getDevice());
    ASSERT_NE(arena.create(
        "persistent", 64u,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst), nullptr);
    ASSERT_NE(arena.create(
        "scratch", 128u,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, true), nullptr);
    EXPECT_EQ(arena.persistentBytes(), 64u);
    EXPECT_EQ(arena.scratchBytes(), 128u);
    arena.shutdown();
    EXPECT_EQ(arena.persistentBytes(), 0u);
    EXPECT_EQ(arena.scratchBytes(), 0u);
}

class GpuPhysicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        gpu::ContextConfig gpuConfig;
        gpuConfig.enableValidation = false;
        if (!gpuContext.initHeadless(gpuConfig)) {
            GTEST_SKIP() << "Headless WebGPU is unavailable";
        }
        PhysicsInitContext context;
        context.requestedBackend = BackendType::WebGpuSoft;
        context.device = gpuContext.getDevice();
        context.queue = gpuContext.getQueue();
        context.maxBodies = 1'024;
        context.maxActiveBodies = 1'024;
        context.maxPairs = 1'024;
        context.maxContacts = 512;
        context.maxManifolds = 1'024;
        context.gpu.commandCapacity = 2'048;
        context.gpu.debugReadbackBodyCapacity = 32;
        context.gpu.maximumLinearSpeed = 5.0f;
        context.gpu.maximumAngularSpeed = 1.0f;
        ASSERT_TRUE(world.initialize(context));
    }

    void encodeAndSubmit() {
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            gpuContext.getDevice(), &encoderDesc);
        world.encodeGpuStep(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        wgpuQueueSubmit(gpuContext.getQueue(), 1, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
    }

    std::optional<DebugSnapshot> retireDebugReadback() {
        auto result = world.pollDebugSnapshot();
        for (uint32_t attempt = 0; !result && attempt < 8; ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                gpuContext.getDevice(), true, nullptr));
            result = world.pollDebugSnapshot();
        }
        return result;
    }

    std::optional<PhysicsQueryBatch> retireQueryReadback() {
        auto result = world.pollQueryResults();
        for (uint32_t attempt = 0; !result && attempt < 8; ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                gpuContext.getDevice(), true, nullptr));
            result = world.pollQueryResults();
        }
        return result;
    }

    std::optional<PhysicsEventBatch> retireEventReadback() {
        auto result = world.pollEvents();
        for (uint32_t attempt = 0; !result && attempt < 8; ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                gpuContext.getDevice(), true, nullptr));
            result = world.pollEvents();
        }
        return result;
    }

    PhysicsStats retireTelemetry(uint64_t minimumTick = 1u) {
        PhysicsStats stats = world.stats();
        for (uint32_t attempt = 0; stats.telemetryTick < minimumTick
             && attempt < 8u; ++attempt) {
            world.update(1.0e-6f);
            static_cast<void>(wgpuDevicePoll(
                gpuContext.getDevice(), true, nullptr));
            stats = world.stats();
        }
        return stats;
    }

    void stepTicks(uint32_t tickCount) {
        while (tickCount != 0u) {
            const uint32_t batch = std::min(tickCount, 8u);
            world.update(static_cast<float>(batch) / 60.0f);
            encodeAndSubmit();
            tickCount -= batch;
        }
    }

    std::optional<DebugSnapshot> snapshotRange(uint32_t firstBody,
                                                uint32_t bodyCount) {
        world.requestDebugSnapshot({firstBody, bodyCount});
        encodeAndSubmit();
        return retireDebugReadback();
    }

    void attachFlatTerrain(uint16_t sample = 32'768u,
                           float heightScale = 10.0f) {
        constexpr uint32_t extent = 64;
        std::vector<uint16_t> samples(extent * extent, sample);
        ASSERT_TRUE(world.setTerrain(samples, extent, extent,
                                     heightScale, 1.0f));
    }

    gpu::Context gpuContext;
    PhysicsWorld world;
};

TEST_F(GpuPhysicsTest, RenderViewTracksSparseBodyRange) {
    const PhysicsRenderView empty = world.renderView();
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.poseBuffer, nullptr);
    EXPECT_EQ(empty.shapeBuffer, nullptr);
    EXPECT_EQ(empty.metadataBuffer, nullptr);
    EXPECT_EQ(empty.residentBodyCapacity, 0u);
    // Empty ticks still advance the device-side tick used by later commands.
    stepTicks(2u);

    const BodyHandle first = world.spawnBody({});
    const BodyHandle second = world.spawnBody({});
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(first.index, 1u);
    EXPECT_EQ(second.index, 2u);
    stepTicks(1u);
    EXPECT_EQ(world.renderView().residentBodyCapacity, 3u);
    const auto spawned = snapshotRange(first.index, 2u);
    ASSERT_TRUE(spawned.has_value());
    ASSERT_EQ(spawned->bodies.size(), 2u);
    EXPECT_TRUE(spawned->bodies[0].alive);
    EXPECT_TRUE(spawned->bodies[1].alive);

    ASSERT_TRUE(world.destroyBody(second));
    stepTicks(1u);
    EXPECT_EQ(world.renderView().residentBodyCapacity, 2u);

    ASSERT_TRUE(world.destroyBody(first));
    stepTicks(1u);
    EXPECT_FALSE(world.renderView().valid());
    EXPECT_EQ(world.renderView().residentBodyCapacity, 0u);

    const BodyHandle recycled = world.spawnBody({});
    ASSERT_TRUE(recycled.valid());
    EXPECT_EQ(recycled.index, 1u);
    EXPECT_EQ(world.renderView().residentBodyCapacity, 2u);
}

TEST_F(GpuPhysicsTest, IntegratesPersistentBodyAndReadsItAsynchronously) {
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Box;
    desc.position = {1.0f, 10.0f, 2.0f};
    desc.linearVelocity = {3.0f, 0.0f, -2.0f};
    desc.angularVelocity = {0.0f, 2.0f, 0.0f};
    desc.dimensions = {1.0f, 2.0f, 3.0f};
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({body.index, 1});
    encodeAndSubmit();

    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->tick, 1u);
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    const auto& state = snapshot->bodies.front();
    EXPECT_EQ(state.handle, body);
    EXPECT_TRUE(state.alive);
    EXPECT_TRUE(state.awake);
    EXPECT_EQ(state.shape, ThrowableShape::Box);
    EXPECT_GT(state.position.x, desc.position.x);
    EXPECT_LT(state.position.y, desc.position.y);
    EXPECT_LT(state.position.z, desc.position.z);

    const PhysicsRenderView view = world.renderView();
    EXPECT_TRUE(view.valid());
    EXPECT_EQ(view.residentBodyCapacity, 2u);
    EXPECT_EQ(world.dynamicBodies().size(), 1u);
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 0u);
}

TEST_F(GpuPhysicsTest, ComposesBodyContactsEventsAndAsyncQueries) {
    EXPECT_TRUE(world.capabilities().bodyBodyContacts);
    EXPECT_TRUE(world.capabilities().asynchronousQueries);
    EXPECT_TRUE(world.capabilities().eventReadback);
    const size_t scratchBeforeEventReadback = world.stats().scratchBytes;
    world.setEventReadbackEnabled(true);
    EXPECT_GT(world.stats().scratchBytes, scratchBeforeEventReadback);

    BodySpawnDesc leftDesc;
    leftDesc.dimensions = throwableShapeDimensions(ThrowableShape::Sphere);
    leftDesc.position = {-0.50f, 5.0f, 0.0f};
    leftDesc.linearVelocity = {2.0f, 0.0f, 0.0f};
    BodySpawnDesc rightDesc = leftDesc;
    rightDesc.position.x = 0.50f;
    rightDesc.linearVelocity.x = -2.0f;
    const BodyHandle left = world.spawnBody(leftDesc);
    const BodyHandle right = world.spawnBody(rightDesc);
    ASSERT_TRUE(left.valid());
    ASSERT_TRUE(right.valid());

    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({left.index, 2});
    encodeAndSubmit();
    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 2u);
    EXPECT_LT(snapshot->bodies[0].linearVelocity.x,
              leftDesc.linearVelocity.x);
    EXPECT_GT(snapshot->bodies[1].linearVelocity.x,
              rightDesc.linearVelocity.x);

    const auto events = retireEventReadback();
    ASSERT_TRUE(events.has_value());
    EXPECT_FALSE(events->overflow);
    ASSERT_FALSE(events->events.empty())
        << "overflow=" << events->overflow;
    EXPECT_TRUE(std::any_of(events->events.begin(), events->events.end(),
        [left, right](const PhysicsEvent& event) {
            return event.type == PhysicsEventType::ContactBegin
                && ((event.bodyHandleA() == left
                     && event.bodyHandleB() == right)
                    || (event.bodyHandleA() == right
                        && event.bodyHandleB() == left));
        })) << "first type=" << static_cast<uint32_t>(events->events[0].type)
            << " bodies=" << events->events[0].bodyA << ','
            << events->events[0].bodyB;
    EXPECT_TRUE(std::any_of(events->events.begin(), events->events.end(),
        [left, right](const PhysicsEvent& event) {
            return event.type == PhysicsEventType::ContactHit
                && event.bodyA == std::min(left.index, right.index)
                && event.bodyB == std::max(left.index, right.index);
        }));

    const PhysicsStats telemetry = retireTelemetry();
    EXPECT_EQ(telemetry.telemetryTick, 1u);
    EXPECT_GE(telemetry.highPairs, 1u);
    EXPECT_GE(telemetry.highManifolds, 1u);
    EXPECT_GE(telemetry.highContacts, 1u);
    EXPECT_EQ(telemetry.activeBodyUsage.current, 2u);
    EXPECT_EQ(telemetry.activeBodyUsage.capacity, 1'024u);
    EXPECT_GE(telemetry.commandUsage.highWater, 2u);
    EXPECT_GE(telemetry.uniquePairUsage.current, 1u);
    EXPECT_GE(telemetry.manifoldUsage.current, 1u);
    EXPECT_GE(telemetry.contactUsage.current, 1u);
    EXPECT_GE(telemetry.compactIslandContacts, 1u);
    EXPECT_GE(telemetry.compactIslandBodies, 2u);
    EXPECT_TRUE(telemetry.serialWorldSolver);
    EXPECT_GE(telemetry.eventUsage.current, 1u);
    EXPECT_GT(telemetry.gpuReadbackBytes, 0u);
    EXPECT_FALSE(telemetry.pairCapacityOverflow);
    EXPECT_FALSE(telemetry.contactCapacityOverflow);

    PhysicsQueryRequest ray;
    ray.requestId = 77u;
    ray.type = PhysicsQueryType::RayCast;
    ray.maximumHits = 4u;
    ray.origin = {-5.0f, 5.0f, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};
    ray.maximumDistance = 10.0f;
    ASSERT_TRUE(world.submitQueries(
        std::span<const PhysicsQueryRequest>(&ray, 1), 91u));
    encodeAndSubmit();
    const auto queries = retireQueryReadback();
    ASSERT_TRUE(queries.has_value());
    EXPECT_EQ(queries->tick, 91u);
    ASSERT_EQ(queries->outputs.size(), 1u);
    EXPECT_EQ(queries->outputs[0].requestId, ray.requestId);
    EXPECT_GE(queries->outputs[0].hits.size(), 2u);
    EXPECT_LE(queries->outputs[0].hits[0].distance,
              queries->outputs[0].hits[1].distance);
    for (const PhysicsQueryHit& hit : queries->outputs[0].hits) {
        if (hit.bodyIndex == left.index) {
            EXPECT_EQ(hit.bodyHandle(), left);
        }
        if (hit.bodyIndex == right.index) {
            EXPECT_EQ(hit.bodyHandle(), right);
        }
    }
}

TEST_F(GpuPhysicsTest, SpeculativeSweepPreventsThrownCapsulesFromCrossing) {
    PhysicsWorld crossingWorld;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::WebGpuSoft;
    context.device = gpuContext.getDevice();
    context.queue = gpuContext.getQueue();
    context.maxBodies = 64;
    context.maxActiveBodies = 64;
    context.maxPairs = 64;
    context.maxContacts = 32;
    context.maxManifolds = 64;
    context.gpu.commandCapacity = 64;
    context.gpu.debugReadbackSlots = 2;
    context.gpu.debugReadbackBodyCapacity = 2;
    context.gpu.gravity = glm::vec3(0.0f);
    context.gpu.maximumLinearSpeed = 500.0f;
    ASSERT_TRUE(crossingWorld.initialize(context));

    BodySpawnDesc leftDesc;
    leftDesc.shape = ThrowableShape::Capsule;
    leftDesc.dimensions = throwableShapeDimensions(leftDesc.shape);
    constexpr float initialGap = 0.03f;
    constexpr float throwSpeed = 28.0f;
    const float halfSeparation =
        0.5f * (leftDesc.dimensions.x + initialGap);
    leftDesc.position = {-halfSeparation, 5.0f, 0.0f};
    leftDesc.linearVelocity = {throwSpeed, 0.0f, 0.0f};
    BodySpawnDesc rightDesc = leftDesc;
    rightDesc.position.x = halfSeparation;
    rightDesc.linearVelocity.x = -throwSpeed;
    const BodyHandle left = crossingWorld.spawnBody(leftDesc);
    const BodyHandle right = crossingWorld.spawnBody(rightDesc);
    ASSERT_TRUE(left.valid());
    ASSERT_TRUE(right.valid());

    crossingWorld.update(1.0f / 60.0f);
    crossingWorld.requestDebugSnapshot({left.index, 2u});
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext.getDevice(), &encoderDesc);
    crossingWorld.encodeGpuStep(encoder);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    wgpuQueueSubmit(gpuContext.getQueue(), 1u, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    auto snapshot = crossingWorld.pollDebugSnapshot();
    for (uint32_t attempt = 0u; !snapshot && attempt < 8u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            gpuContext.getDevice(), true, nullptr));
        snapshot = crossingWorld.pollDebugSnapshot();
    }
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 2u);
    const DebugBodyState& leftState = snapshot->bodies[0];
    const DebugBodyState& rightState = snapshot->bodies[1];
    EXPECT_LT(leftState.position.x, rightState.position.x);
    EXPECT_LT(leftState.linearVelocity.x, throwSpeed * 0.75f);
    EXPECT_GT(rightState.linearVelocity.x, -throwSpeed * 0.75f);
}

class GpuPhysicsRestitutionParityTest
    : public GpuPhysicsTest,
      public ::testing::WithParamInterface<ThrowableShape> {};

TEST_P(GpuPhysicsRestitutionParityTest, MatchesJoltHeadOnBounceDistance) {
    PhysicsWorld gpuParityWorld;
    PhysicsInitContext gpuContextConfig;
    gpuContextConfig.requestedBackend = BackendType::WebGpuSoft;
    gpuContextConfig.device = gpuContext.getDevice();
    gpuContextConfig.queue = gpuContext.getQueue();
    gpuContextConfig.maxBodies = 64u;
    gpuContextConfig.maxActiveBodies = 64u;
    gpuContextConfig.maxPairs = 64u;
    gpuContextConfig.maxContacts = 64u;
    gpuContextConfig.maxManifolds = 64u;
    gpuContextConfig.gpu.commandCapacity = 64u;
    gpuContextConfig.gpu.debugReadbackSlots = 2u;
    gpuContextConfig.gpu.debugReadbackBodyCapacity = 2u;
    gpuContextConfig.gpu.enableTelemetryReadback = false;
    ASSERT_TRUE(gpuParityWorld.initialize(gpuContextConfig));

    PhysicsWorld joltWorld;
    PhysicsInitContext joltContext;
    joltContext.requestedBackend = BackendType::JoltLegacy;
    joltContext.maxBodies = 64u;
    joltContext.maxActiveBodies = 64u;
    joltContext.maxPairs = 64u;
    joltContext.maxContacts = 64u;
    joltContext.maxManifolds = 64u;
    joltContext.joltJobSystem = JoltJobSystemMode::SingleThreaded;
    ASSERT_TRUE(joltWorld.initialize(joltContext));

    BodySpawnDesc leftDesc;
    leftDesc.shape = GetParam();
    leftDesc.dimensions = throwableShapeDimensions(leftDesc.shape);
    leftDesc.position = {-1.0f, 50.0f, 0.0f};
    leftDesc.linearVelocity = {2.0f, 0.0f, 0.0f};
    BodySpawnDesc rightDesc = leftDesc;
    rightDesc.position.x = 1.0f;
    rightDesc.linearVelocity.x = -2.0f;

    const BodyHandle gpuLeft = gpuParityWorld.spawnBody(leftDesc);
    const BodyHandle gpuRight = gpuParityWorld.spawnBody(rightDesc);
    ASSERT_TRUE(gpuLeft.valid());
    ASSERT_TRUE(gpuRight.valid());
    ASSERT_TRUE(joltWorld.spawnBody(leftDesc).valid());
    ASSERT_TRUE(joltWorld.spawnBody(rightDesc).valid());

    constexpr uint32_t tickCount = 60u;
    for (uint32_t tick = 0u; tick < tickCount; ++tick) {
        joltWorld.update(1.0f / 60.0f);
        gpuParityWorld.update(1.0f / 60.0f);

        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            gpuContext.getDevice(), &encoderDesc);
        gpuParityWorld.encodeGpuStep(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        wgpuQueueSubmit(gpuContext.getQueue(), 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
    }

    gpuParityWorld.requestDebugSnapshot({gpuLeft.index, 2u});
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext.getDevice(), &encoderDesc);
    gpuParityWorld.encodeGpuStep(encoder);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    wgpuQueueSubmit(gpuContext.getQueue(), 1u, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    auto gpuSnapshot = gpuParityWorld.pollDebugSnapshot();
    for (uint32_t attempt = 0u; !gpuSnapshot && attempt < 8u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            gpuContext.getDevice(), true, nullptr));
        gpuSnapshot = gpuParityWorld.pollDebugSnapshot();
    }
    ASSERT_TRUE(gpuSnapshot.has_value());
    ASSERT_EQ(gpuSnapshot->bodies.size(), 2u);
    const auto joltSnapshot = joltWorld.dynamicBodies();
    ASSERT_EQ(joltSnapshot.size(), 2u);

    const float leftError = std::abs(
        gpuSnapshot->bodies[0].position.x - joltSnapshot[0].position.x);
    const float rightError = std::abs(
        gpuSnapshot->bodies[1].position.x - joltSnapshot[1].position.x);
    EXPECT_LT(leftError, 0.04f);
    EXPECT_LT(rightError, 0.04f);
    EXPECT_LT(gpuSnapshot->bodies[0].position.x,
              gpuSnapshot->bodies[1].position.x);
    EXPECT_LT(joltSnapshot[0].position.x, joltSnapshot[1].position.x);
}

INSTANTIATE_TEST_SUITE_P(
    Materials, GpuPhysicsRestitutionParityTest,
    ::testing::Values(ThrowableShape::Sphere, ThrowableShape::Box));

TEST_F(GpuPhysicsTest, MatchesJoltSphereTerrainBounceTrajectory) {
    PhysicsWorld gpuParityWorld;
    PhysicsInitContext gpuContextConfig;
    gpuContextConfig.requestedBackend = BackendType::WebGpuSoft;
    gpuContextConfig.device = gpuContext.getDevice();
    gpuContextConfig.queue = gpuContext.getQueue();
    gpuContextConfig.maxBodies = 64u;
    gpuContextConfig.maxActiveBodies = 64u;
    gpuContextConfig.maxPairs = 64u;
    gpuContextConfig.maxContacts = 64u;
    gpuContextConfig.maxManifolds = 64u;
    gpuContextConfig.gpu.commandCapacity = 64u;
    gpuContextConfig.gpu.debugReadbackSlots = 2u;
    gpuContextConfig.gpu.debugReadbackBodyCapacity = 1u;
    gpuContextConfig.gpu.enableTelemetryReadback = false;
    ASSERT_TRUE(gpuParityWorld.initialize(gpuContextConfig));

    PhysicsWorld joltWorld;
    PhysicsInitContext joltContext;
    joltContext.requestedBackend = BackendType::JoltLegacy;
    joltContext.maxBodies = 64u;
    joltContext.maxActiveBodies = 64u;
    joltContext.maxPairs = 64u;
    joltContext.maxContacts = 64u;
    joltContext.maxManifolds = 64u;
    joltContext.joltJobSystem = JoltJobSystemMode::SingleThreaded;
    ASSERT_TRUE(joltWorld.initialize(joltContext));

    constexpr uint32_t terrainExtent = 64u;
    const std::vector<uint16_t> terrain(
        terrainExtent * terrainExtent, 32'768u);
    ASSERT_TRUE(gpuParityWorld.setTerrain(
        terrain, terrainExtent, terrainExtent, 10.0f, 1.0f));
    ASSERT_TRUE(joltWorld.setTerrain(
        terrain, terrainExtent, terrainExtent, 10.0f, 1.0f));

    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Sphere;
    desc.dimensions = throwableShapeDimensions(desc.shape);
    desc.position = {0.0f, 3.0f, 0.0f};
    desc.linearVelocity = {3.0f, 0.0f, 0.0f};
    const BodyHandle gpuBody = gpuParityWorld.spawnBody(desc);
    ASSERT_TRUE(gpuBody.valid());
    ASSERT_TRUE(joltWorld.spawnBody(desc).valid());

    constexpr uint32_t tickCount = 60u;
    float squaredError = 0.0f;
    float maximumError = 0.0f;
    float squaredHorizontalError = 0.0f;
    float maximumHorizontalError = 0.0f;
    float finalGpuY = 0.0f;
    float finalJoltY = 0.0f;
    for (uint32_t tick = 0u; tick < tickCount; ++tick) {
        joltWorld.update(1.0f / 60.0f);
        gpuParityWorld.update(1.0f / 60.0f);
        gpuParityWorld.requestDebugSnapshot({gpuBody.index, 1u});

        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            gpuContext.getDevice(), &encoderDesc);
        gpuParityWorld.encodeGpuStep(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        wgpuQueueSubmit(gpuContext.getQueue(), 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);

        auto gpuSnapshot = gpuParityWorld.pollDebugSnapshot();
        for (uint32_t attempt = 0u; !gpuSnapshot && attempt < 8u;
             ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                gpuContext.getDevice(), true, nullptr));
            gpuSnapshot = gpuParityWorld.pollDebugSnapshot();
        }
        ASSERT_TRUE(gpuSnapshot.has_value());
        ASSERT_EQ(gpuSnapshot->bodies.size(), 1u);
        const auto joltSnapshot = joltWorld.dynamicBodies();
        ASSERT_EQ(joltSnapshot.size(), 1u);
        finalGpuY = gpuSnapshot->bodies[0].position.y;
        finalJoltY = joltSnapshot[0].position.y;
        const float error = std::abs(finalGpuY - finalJoltY);
        const float horizontalError = std::abs(
            gpuSnapshot->bodies[0].position.x
            - joltSnapshot[0].position.x);
        squaredError += error * error;
        maximumError = std::max(maximumError, error);
        squaredHorizontalError += horizontalError * horizontalError;
        maximumHorizontalError = std::max(
            maximumHorizontalError, horizontalError);
    }

    EXPECT_LT(std::sqrt(squaredError / tickCount), 0.01f);
    EXPECT_LT(maximumError, 0.02f);
    EXPECT_LT(std::sqrt(squaredHorizontalError / tickCount), 0.05f);
    EXPECT_LT(maximumHorizontalError, 0.12f);
}

TEST_F(GpuPhysicsTest, ReportsJoltWebGpuSphereWaterTrajectory) {
    PhysicsWorld gpuParityWorld;
    PhysicsInitContext gpuConfig;
    gpuConfig.requestedBackend = BackendType::WebGpuSoft;
    gpuConfig.device = gpuContext.getDevice();
    gpuConfig.queue = gpuContext.getQueue();
    gpuConfig.maxBodies = 64u;
    gpuConfig.maxActiveBodies = 64u;
    gpuConfig.maxPairs = 64u;
    gpuConfig.maxContacts = 64u;
    gpuConfig.maxManifolds = 64u;
    gpuConfig.gpu.commandCapacity = 64u;
    gpuConfig.gpu.debugReadbackSlots = 2u;
    gpuConfig.gpu.debugReadbackBodyCapacity = 1u;
    gpuConfig.gpu.enableTelemetryReadback = false;
    gpuConfig.gpu.waterLinearDrag = 0.93f;
    ASSERT_TRUE(gpuParityWorld.initialize(gpuConfig));

    PhysicsWorld joltWorld;
    PhysicsInitContext joltConfig;
    joltConfig.requestedBackend = BackendType::JoltLegacy;
    joltConfig.maxBodies = 64u;
    joltConfig.maxActiveBodies = 64u;
    joltConfig.maxPairs = 64u;
    joltConfig.maxContacts = 64u;
    joltConfig.maxManifolds = 64u;
    joltConfig.joltJobSystem = JoltJobSystemMode::SingleThreaded;
    ASSERT_TRUE(joltWorld.initialize(joltConfig));
    gpuParityWorld.setWaterPlane(0.0f, true);
    joltWorld.setWaterPlane(0.0f, true);

    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Sphere;
    desc.dimensions = throwableShapeDimensions(desc.shape);
    desc.position = {0.0f, 0.0f, 0.0f};
    desc.linearVelocity = {0.0f, -2.0f, 0.0f};
    const BodyHandle gpuBody = gpuParityWorld.spawnBody(desc);
    ASSERT_TRUE(gpuBody.valid());
    ASSERT_TRUE(joltWorld.spawnBody(desc).valid());

    constexpr uint32_t tickCount = 120u;
    float squaredError = 0.0f;
    float maximumError = 0.0f;
    float finalGpuY = 0.0f;
    float finalJoltY = 0.0f;
    for (uint32_t tick = 0u; tick < tickCount; ++tick) {
        joltWorld.update(1.0f / 60.0f);
        gpuParityWorld.update(1.0f / 60.0f);
        gpuParityWorld.requestDebugSnapshot({gpuBody.index, 1u});
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            gpuContext.getDevice(), &encoderDesc);
        gpuParityWorld.encodeGpuStep(encoder);
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command =
            wgpuCommandEncoderFinish(encoder, &commandDesc);
        wgpuQueueSubmit(gpuContext.getQueue(), 1u, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);

        auto gpuSnapshot = gpuParityWorld.pollDebugSnapshot();
        for (uint32_t attempt = 0u; !gpuSnapshot && attempt < 8u;
             ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                gpuContext.getDevice(), true, nullptr));
            gpuSnapshot = gpuParityWorld.pollDebugSnapshot();
        }
        ASSERT_TRUE(gpuSnapshot.has_value());
        const auto joltSnapshot = joltWorld.dynamicBodies();
        ASSERT_EQ(joltSnapshot.size(), 1u);
        finalGpuY = gpuSnapshot->bodies[0].position.y;
        finalJoltY = joltSnapshot[0].position.y;
        const float error = std::abs(finalGpuY - finalJoltY);
        squaredError += error * error;
        maximumError = std::max(maximumError, error);
    }

    std::cerr << "water sphere y: gpu=" << finalGpuY
              << " jolt=" << finalJoltY
              << " rms=" << std::sqrt(squaredError / tickCount)
              << " max=" << maximumError << '\n';
}

TEST_F(GpuPhysicsTest, UsesGlobalSolverAboveSerialWorldLimit) {
    constexpr uint32_t bodyCount = 257u;
    for (uint32_t index = 0u; index < bodyCount; ++index) {
        BodySpawnDesc desc;
        desc.shape = ThrowableShape::Sphere;
        desc.dimensions = throwableShapeDimensions(desc.shape);
        desc.position = {
            -32.0f + 4.0f * static_cast<float>(index % 17u),
            50.0f,
            -28.0f + 4.0f * static_cast<float>(index / 17u)};
        if (index == 1u) {
            desc.position.x = -31.25f;
        }
        ASSERT_TRUE(world.spawnBody(desc).valid());
    }

    stepTicks(1u);

    const PhysicsStats telemetry = retireTelemetry();
    EXPECT_EQ(telemetry.telemetryTick, 1u);
    EXPECT_GE(telemetry.highContacts, 1u);
    EXPECT_FALSE(telemetry.serialWorldSolver);
}

TEST_F(GpuPhysicsTest,
       CollidesAcrossLargeSectorBoundaryWithoutPairingDistantSector) {
    constexpr int32_t baseSector = 1'500'000;
    BodySpawnDesc leftDesc;
    leftDesc.shape = ThrowableShape::Sphere;
    leftDesc.dimensions = throwableShapeDimensions(leftDesc.shape);
    leftDesc.position = {127.6f, 5.0f, 0.0f};
    leftDesc.sector = {baseSector, 0, 0};

    BodySpawnDesc rightDesc = leftDesc;
    rightDesc.position.x = -127.6f;
    rightDesc.sector.x += 1;

    BodySpawnDesc distantDesc = rightDesc;
    distantDesc.sector.x += 3;

    const BodyHandle left = world.spawnBody(leftDesc);
    const BodyHandle right = world.spawnBody(rightDesc);
    const BodyHandle distant = world.spawnBody(distantDesc);
    ASSERT_TRUE(left.valid());
    ASSERT_TRUE(right.valid());
    ASSERT_TRUE(distant.valid());

    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({left.index, 3u});
    encodeAndSubmit();
    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 3u);

    const DebugBodyState& leftState = snapshot->bodies[0];
    const DebugBodyState& rightState = snapshot->bodies[1];
    const DebugBodyState& distantState = snapshot->bodies[2];
    glm::vec3 leftInFrame;
    glm::vec3 rightInFrame;
    ASSERT_TRUE(worldPositionRelativeToSector(
        {leftState.sector, leftState.position}, leftState.sector,
        leftInFrame));
    ASSERT_TRUE(worldPositionRelativeToSector(
        {rightState.sector, rightState.position}, leftState.sector,
        rightInFrame));
    EXPECT_GT(rightInFrame.x - leftInFrame.x, 0.8f);
    EXPECT_LT(rightInFrame.x - leftInFrame.x, 1.2f);
    EXPECT_EQ(distantState.sector.x, baseSector + 4);

    const PhysicsStats telemetry = retireTelemetry();
    EXPECT_EQ(telemetry.uniquePairUsage.current, 1u);
    EXPECT_EQ(telemetry.manifoldUsage.current, 1u);
    EXPECT_EQ(telemetry.oversizedBodies, 0u);
    EXPECT_EQ(telemetry.gridEntryUsage.current, 3u);

    PhysicsQueryRequest ray;
    ray.requestId = 1'500'000u;
    ray.maximumHits = 4u;
    ray.origin = {126.0f, leftState.position.y, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};
    ray.maximumDistance = 5.0f;
    ray.sector = {baseSector, 0, 0};
    ASSERT_TRUE(world.submitQueries(
        std::span<const PhysicsQueryRequest>(&ray, 1), 1'500'001u));
    encodeAndSubmit();
    const auto queryBatch = retireQueryReadback();
    ASSERT_TRUE(queryBatch.has_value());
    ASSERT_EQ(queryBatch->outputs.size(), 1u);
    ASSERT_EQ(queryBatch->outputs[0].hits.size(), 2u);
    EXPECT_TRUE(std::any_of(
        queryBatch->outputs[0].hits.begin(),
        queryBatch->outputs[0].hits.end(),
        [left](const PhysicsQueryHit& hit) {
            return hit.bodyIndex == left.index;
        }));
    EXPECT_TRUE(std::any_of(
        queryBatch->outputs[0].hits.begin(),
        queryBatch->outputs[0].hits.end(),
        [right](const PhysicsQueryHit& hit) {
            return hit.bodyIndex == right.index;
        }));
    EXPECT_TRUE(std::none_of(
        queryBatch->outputs[0].hits.begin(),
        queryBatch->outputs[0].hits.end(),
        [distant](const PhysicsQueryHit& hit) {
            return hit.bodyIndex == distant.index;
        }));
}

TEST_F(GpuPhysicsTest, ClassifiesWaterAtLargePositiveAndNegativeSectors) {
    world.setWaterPlane(0.0f, true);

    BodySpawnDesc submergedDesc;
    submergedDesc.shape = ThrowableShape::Sphere;
    submergedDesc.dimensions = throwableShapeDimensions(
        submergedDesc.shape);
    submergedDesc.position = {0.0f, 0.0f, 0.0f};
    submergedDesc.sector = {0, -1'500'000, 0};
    BodySpawnDesc dryDesc = submergedDesc;
    dryDesc.sector.y = 1'500'000;

    const BodyHandle submerged = world.spawnBody(submergedDesc);
    const BodyHandle dry = world.spawnBody(dryDesc);
    ASSERT_TRUE(submerged.valid());
    ASSERT_TRUE(dry.valid());

    stepTicks(1);
    const auto snapshot = snapshotRange(submerged.index, 2u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 2u);
    const DebugBodyState& submergedState = snapshot->bodies[0];
    const DebugBodyState& dryState = snapshot->bodies[1];
    EXPECT_TRUE(submergedState.submerged);
    EXPECT_FALSE(dryState.submerged);
    EXPECT_EQ(submergedState.sector.y, -1'500'000);
    EXPECT_EQ(dryState.sector.y, 1'500'000);
    EXPECT_GT(submergedState.linearVelocity.y, dryState.linearVelocity.y);

    const PhysicsStats telemetry = retireTelemetry();
    EXPECT_EQ(telemetry.submergedBodies, 1u);
    EXPECT_EQ(telemetry.uniquePairUsage.current, 0u);
    EXPECT_EQ(telemetry.oversizedBodies, 0u);
}

TEST_F(GpuPhysicsTest,
       BodyContactSwitchKeepsIntegrationButSuppressesPairs) {
    PhysicsWorld isolatedWorld;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::WebGpuSoft;
    context.device = gpuContext.getDevice();
    context.queue = gpuContext.getQueue();
    context.maxBodies = 64;
    context.maxActiveBodies = 64;
    context.maxPairs = 64;
    context.maxContacts = 32;
    context.maxManifolds = 64;
    context.gpu.commandCapacity = 64;
    context.gpu.debugReadbackBodyCapacity = 4;
    context.gpu.enableBodyBodyContacts = false;
    ASSERT_TRUE(isolatedWorld.initialize(context));
    EXPECT_FALSE(isolatedWorld.capabilities().bodyBodyContacts);

    BodySpawnDesc desc;
    desc.position = {-0.1f, 10.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle first = isolatedWorld.spawnBody(desc);
    desc.position.x = 0.1f;
    const BodyHandle second = isolatedWorld.spawnBody(desc);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    isolatedWorld.update(1.0f / 60.0f);
    isolatedWorld.requestDebugSnapshot({first.index, 2u});
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext.getDevice(), &encoderDesc);
    isolatedWorld.encodeGpuStep(encoder);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    wgpuQueueSubmit(gpuContext.getQueue(), 1, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    auto snapshot = isolatedWorld.pollDebugSnapshot();
    for (uint32_t attempt = 0u; !snapshot && attempt < 8u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            gpuContext.getDevice(), true, nullptr));
        snapshot = isolatedWorld.pollDebugSnapshot();
    }
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 2u);
    EXPECT_LT(snapshot->bodies[0].position.y, 10.0f);
    EXPECT_LT(snapshot->bodies[1].position.y, 10.0f);

    PhysicsStats telemetry = isolatedWorld.stats();
    for (uint32_t attempt = 0u;
         telemetry.telemetryTick == 0u && attempt < 8u; ++attempt) {
        isolatedWorld.update(1.0e-6f);
        static_cast<void>(wgpuDevicePoll(
            gpuContext.getDevice(), true, nullptr));
        telemetry = isolatedWorld.stats();
    }
    EXPECT_EQ(telemetry.uniquePairUsage.current, 0u);
    EXPECT_EQ(telemetry.contactUsage.current, 0u);
    EXPECT_EQ(telemetry.manifoldUsage.current, 0u);
}

TEST_F(GpuPhysicsTest, ActiveCapacityClampsWithoutWritingPastCompactList) {
    PhysicsWorld limitedWorld;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::WebGpuSoft;
    context.device = gpuContext.getDevice();
    context.queue = gpuContext.getQueue();
    context.maxBodies = 64;
    context.maxActiveBodies = 4;
    context.maxPairs = 64;
    context.maxContacts = 32;
    context.maxManifolds = 64;
    context.gpu.commandCapacity = 64;
    ASSERT_TRUE(limitedWorld.initialize(context));

    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    for (uint32_t body = 0; body < 8u; ++body) {
        desc.position.x = static_cast<float>(body) * 4.0f;
        ASSERT_TRUE(limitedWorld.spawnBody(desc).valid());
    }

    limitedWorld.update(1.0f / 60.0f);
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext.getDevice(), &encoderDesc);
    limitedWorld.encodeGpuStep(encoder);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    wgpuQueueSubmit(gpuContext.getQueue(), 1, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    PhysicsStats telemetry = limitedWorld.stats();
    for (uint32_t attempt = 0u;
         telemetry.telemetryTick == 0u && attempt < 8u; ++attempt) {
        limitedWorld.update(1.0e-6f);
        static_cast<void>(wgpuDevicePoll(
            gpuContext.getDevice(), true, nullptr));
        telemetry = limitedWorld.stats();
    }
    EXPECT_EQ(telemetry.activeBodyUsage.current, 4u);
    EXPECT_EQ(telemetry.activeBodyUsage.capacity, 4u);
    EXPECT_EQ(telemetry.activeBodyUsage.highWater, 4u);
    EXPECT_TRUE(telemetry.activeBodyUsage.overflow);
    EXPECT_TRUE(telemetry.activeCapacityOverflow);
}

TEST_F(GpuPhysicsTest, RejectsConfigurationAndSpawnValuesThatPoisonGpuMath) {
    const auto makeContext = [&] {
        PhysicsInitContext context;
        context.requestedBackend = BackendType::WebGpuSoft;
        context.device = gpuContext.getDevice();
        context.queue = gpuContext.getQueue();
        context.maxBodies = 16u;
        context.maxActiveBodies = 16u;
        context.maxPairs = 16u;
        context.maxContacts = 16u;
        context.maxManifolds = 16u;
        context.gpu.commandCapacity = 16u;
        return context;
    };

    PhysicsWorld invalidCatchUp;
    auto context = makeContext();
    context.gpu.maximumCatchUpTicks = 0u;
    EXPECT_FALSE(invalidCatchUp.initialize(context));

    PhysicsWorld invalidSubsteps;
    context = makeContext();
    context.gpu.substeps = 17u;
    EXPECT_FALSE(invalidSubsteps.initialize(context));

    PhysicsWorld invalidColorCount;
    context = makeContext();
    context.gpu.solverColorCount = 0u;
    EXPECT_FALSE(invalidColorCount.initialize(context));

    PhysicsWorld invalidParallelColorCount;
    context = makeContext();
    context.gpu.solverColorCount = 8u;
    context.gpu.solverParallelColorCount = 9u;
    EXPECT_FALSE(invalidParallelColorCount.initialize(context));

    PhysicsWorld invalidTimeStep;
    context = makeContext();
    context.gpu.fixedTickSeconds =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(invalidTimeStep.initialize(context));

    PhysicsWorld excessiveCatchUp;
    context = makeContext();
    context.gpu.maximumCatchUpTicks = std::numeric_limits<uint32_t>::max();
    EXPECT_FALSE(excessiveCatchUp.initialize(context));

    PhysicsWorld oversizedBuffers;
    context = makeContext();
    context.maxManifolds = std::numeric_limits<uint32_t>::max() / 2u;
    EXPECT_FALSE(oversizedBuffers.initialize(context));

    BodySpawnDesc invalidSpawn;
    invalidSpawn.dimensions = throwableShapeDimensions(invalidSpawn.shape);
    invalidSpawn.linearVelocity.x =
        std::numeric_limits<float>::infinity();
    EXPECT_FALSE(world.spawnBody(invalidSpawn).valid());
    const std::array<uint16_t, 4> terrain{};
    EXPECT_FALSE(world.setTerrain(
        terrain, 2u, 2u, std::numeric_limits<float>::quiet_NaN(), 1.0f));
    const BodyHandle valid = world.spawnBody({});
    EXPECT_EQ(valid.index, 1u);
}

TEST_F(GpuPhysicsTest, MaxBodiesCountsUsableHandlesNotTheNullSentinel) {
    PhysicsWorld tinyWorld;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::WebGpuSoft;
    context.device = gpuContext.getDevice();
    context.queue = gpuContext.getQueue();
    context.maxBodies = 3u;
    context.maxActiveBodies = 3u;
    context.maxPairs = 8u;
    context.maxContacts = 8u;
    context.maxManifolds = 8u;
    context.gpu.commandCapacity = 8u;
    context.gpu.debugReadbackBodyCapacity = 3u;
    ASSERT_TRUE(tinyWorld.initialize(context));

    BodySpawnDesc desc;
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle first = tinyWorld.spawnBody(desc);
    const BodyHandle second = tinyWorld.spawnBody(desc);
    const BodyHandle third = tinyWorld.spawnBody(desc);
    EXPECT_EQ(first.index, 1u);
    EXPECT_EQ(second.index, 2u);
    EXPECT_EQ(third.index, 3u);
    EXPECT_FALSE(tinyWorld.spawnBody(desc).valid());
    EXPECT_EQ(tinyWorld.stats().bodyCapacity, 3u);
    EXPECT_EQ(tinyWorld.stats().residentBodies, 3u);

    tinyWorld.update(1.0f / 60.0f);
    tinyWorld.requestDebugSnapshot({1u, 3u});
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext.getDevice(), &encoderDesc);
    tinyWorld.encodeGpuStep(encoder);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(
        encoder, &commandDesc);
    wgpuQueueSubmit(gpuContext.getQueue(), 1u, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    auto snapshot = tinyWorld.pollDebugSnapshot();
    for (uint32_t attempt = 0u; !snapshot && attempt < 8u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            gpuContext.getDevice(), true, nullptr));
        snapshot = tinyWorld.pollDebugSnapshot();
    }
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 3u);
    EXPECT_TRUE(std::ranges::all_of(
        snapshot->bodies, [](const auto& body) { return body.alive; }));
}

TEST_F(GpuPhysicsTest, ReusesFreedHandlesInAscendingOrderAtTickBoundary) {
    BodySpawnDesc desc;
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle first = world.spawnBody(desc);
    const BodyHandle second = world.spawnBody(desc);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    world.update(1.0f / 60.0f);
    encodeAndSubmit();

    ASSERT_TRUE(world.destroyBody(first));
    world.update(1.0f / 60.0f);
    encodeAndSubmit();
    const BodyHandle reused = world.spawnBody(desc);
    EXPECT_EQ(reused.index, first.index);
    EXPECT_EQ(reused.generation, first.generation + 1u);
}

TEST_F(GpuPhysicsTest, CatchUpDebtStaysBoundedWhileGpuEncodingIsPaused) {
    constexpr uint64_t maximumCatchUpTicks = 8u;
    // Simulate an application that keeps receiving update callbacks while its
    // minimized window does not produce command encoders.
    for (uint32_t frame = 0u; frame < 100u; ++frame) {
        world.update(1.0f / 60.0f);
    }

    encodeAndSubmit();
    EXPECT_EQ(world.encodedTick(), maximumCatchUpTicks);

    // The cap includes both already-pending ticks and fractional accumulated
    // time, so the first ordinary frame immediately resumes one-tick pacing.
    world.update(1.0f / 60.0f);
    encodeAndSubmit();
    EXPECT_EQ(world.encodedTick(), maximumCatchUpTicks + 1u);

    world.update(1.0f / 60.0f);
    encodeAndSubmit();
    EXPECT_EQ(world.encodedTick(), maximumCatchUpTicks + 2u);
}

TEST_F(GpuPhysicsTest, CancelsSpawnDestroyedBeforeItsFirstGpuTick) {
    BodySpawnDesc discardedDesc;
    discardedDesc.position = {-20.0f, 10.0f, 0.0f};
    discardedDesc.dimensions = throwableShapeDimensions(discardedDesc.shape);
    const BodyHandle discarded = world.spawnBody(discardedDesc);
    ASSERT_TRUE(discarded.valid());
    ASSERT_TRUE(world.destroyBody(discarded));

    BodySpawnDesc replacementDesc = discardedDesc;
    replacementDesc.position.x = 20.0f;
    const BodyHandle replacement = world.spawnBody(replacementDesc);
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, discarded.index);
    EXPECT_EQ(replacement.generation, discarded.generation + 1u);

    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({replacement.index, 1u});
    encodeAndSubmit();
    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_EQ(snapshot->bodies[0].handle, replacement);
    EXPECT_TRUE(snapshot->bodies[0].alive);
    EXPECT_GT(snapshot->bodies[0].position.x, 19.0f);
    EXPECT_EQ(world.stats().residentBodies, 1u);
}

TEST_F(GpuPhysicsTest, FutureDestroyKeepsBodyAliveUntilItsTargetTick) {
    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    stepTicks(1u);

    PhysicsCommand destroy;
    destroy.type = PhysicsCommandType::DestroyBody;
    destroy.body = body;
    destroy.targetTick = world.encodedTick() + 3u;
    PhysicsCommand velocity;
    velocity.type = PhysicsCommandType::SetVelocity;
    velocity.body = body;
    velocity.targetTick = world.encodedTick() + 2u;
    velocity.a = glm::vec4(4.0f, 0.0f, 0.0f, 0.0f);
    const std::array commands{destroy, velocity};
    world.enqueue(commands);

    EXPECT_EQ(world.stats().residentBodies, 1u);
    EXPECT_TRUE(world.renderView().valid());
    stepTicks(2u);
    const auto beforeDestroy = snapshotRange(body.index, 1u);
    ASSERT_TRUE(beforeDestroy.has_value());
    ASSERT_EQ(beforeDestroy->bodies.size(), 1u);
    EXPECT_TRUE(beforeDestroy->bodies[0].alive);
    EXPECT_GT(beforeDestroy->bodies[0].linearVelocity.x, 3.0f);
    EXPECT_EQ(world.stats().residentBodies, 1u);

    stepTicks(1u);
    const auto destroyed = snapshotRange(body.index, 1u);
    ASSERT_TRUE(destroyed.has_value());
    ASSERT_EQ(destroyed->bodies.size(), 1u);
    EXPECT_FALSE(destroyed->bodies[0].alive);
    EXPECT_EQ(world.stats().residentBodies, 0u);
    EXPECT_FALSE(world.renderView().valid());
}

TEST_F(GpuPhysicsTest, LaterDestroyDoesNotCancelScheduledReplaySpawn) {
    PhysicsCommand spawn;
    spawn.type = PhysicsCommandType::SpawnBody;
    spawn.body = {1u, 1u};
    spawn.targetTick = 2u;
    spawn.a = glm::vec4(2.0f, 20.0f, 0.0f, 1.0f);
    spawn.b = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    spawn.e = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    PhysicsCommand destroy;
    destroy.type = PhysicsCommandType::DestroyBody;
    destroy.body = spawn.body;
    destroy.targetTick = 4u;
    const std::array commands{spawn, destroy};
    world.enqueue(commands);

    EXPECT_EQ(world.stats().residentBodies, 1u);
    stepTicks(2u);
    const auto spawned = snapshotRange(spawn.body.index, 1u);
    ASSERT_TRUE(spawned.has_value());
    ASSERT_EQ(spawned->bodies.size(), 1u);
    EXPECT_TRUE(spawned->bodies[0].alive);
    EXPECT_EQ(spawned->bodies[0].handle, spawn.body);

    stepTicks(2u);
    const auto destroyed = snapshotRange(spawn.body.index, 1u);
    ASSERT_TRUE(destroyed.has_value());
    ASSERT_EQ(destroyed->bodies.size(), 1u);
    EXPECT_FALSE(destroyed->bodies[0].alive);
    const BodyHandle replacement = world.spawnBody({});
    EXPECT_EQ(replacement.index, spawn.body.index);
    EXPECT_EQ(replacement.generation, spawn.body.generation + 1u);
}

TEST_F(GpuPhysicsTest, ReplayLifecycleCommandsMaintainHostAllocationState) {
    PhysicsCommand spawn;
    spawn.type = PhysicsCommandType::SpawnBody;
    spawn.body = {1u, 1u};
    spawn.shape = ThrowableShape::Box;
    spawn.a = glm::vec4(3.0f, 10.0f, -2.0f, 1.0f);
    spawn.b = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    spawn.e = glm::vec4(1.0f, 2.0f, 3.0f, 0.0f);
    world.enqueue(std::span<const PhysicsCommand>(&spawn, 1u));
    stepTicks(1u);

    const auto spawned = snapshotRange(1u, 1u);
    ASSERT_TRUE(spawned.has_value());
    ASSERT_EQ(spawned->bodies.size(), 1u);
    EXPECT_TRUE(spawned->bodies[0].alive);
    EXPECT_EQ(spawned->bodies[0].shape, ThrowableShape::Box);
    EXPECT_EQ(world.stats().residentBodies, 1u);

    PhysicsCommand destroy;
    destroy.type = PhysicsCommandType::DestroyBody;
    destroy.body = spawn.body;
    world.enqueue(std::span<const PhysicsCommand>(&destroy, 1u));
    stepTicks(1u);
    EXPECT_EQ(world.stats().residentBodies, 0u);
    EXPECT_FALSE(world.renderView().valid());

    BodySpawnDesc replacementDesc;
    replacementDesc.dimensions = throwableShapeDimensions(
        replacementDesc.shape);
    const BodyHandle replacement = world.spawnBody(replacementDesc);
    EXPECT_EQ(replacement.index, 1u);
    EXPECT_EQ(replacement.generation, 2u);
}

TEST_F(GpuPhysicsTest, AutomaticSequencesFollowExplicitReplaySequences) {
    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.linearVelocity = {1.0f, 0.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    stepTicks(1u);

    PhysicsCommand sleep;
    sleep.type = PhysicsCommandType::Sleep;
    sleep.body = body;
    sleep.sequence = 100u;
    PhysicsCommand wake;
    wake.type = PhysicsCommandType::Wake;
    wake.body = body;
    const std::array commands{sleep, wake};
    world.enqueue(commands);
    stepTicks(1u);

    const auto snapshot = snapshotRange(body.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_TRUE(snapshot->bodies[0].awake);
}

TEST_F(GpuPhysicsTest, AppliesOrderedCommandsClampsSpeedsAndCompactsSleep) {
    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.angularVelocity = {0.0f, 10.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    PhysicsCommand impulse;
    impulse.type = PhysicsCommandType::ApplyImpulse;
    impulse.body = body;
    impulse.a = glm::vec4(100.0f, 0.0f, 0.0f, 0.0f);
    world.enqueue(std::span<const PhysicsCommand>(&impulse, 1));
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({body.index, 1});
    encodeAndSubmit();
    auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState moving = snapshot->bodies.front();
    EXPECT_LE(glm::length(moving.linearVelocity), 5.001f);
    EXPECT_LE(glm::length(moving.angularVelocity), 1.001f);
    EXPECT_NE(moving.orientation, desc.orientation);

    PhysicsCommand sleep;
    sleep.type = PhysicsCommandType::Sleep;
    sleep.body = body;
    world.enqueue(std::span<const PhysicsCommand>(&sleep, 1));
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({body.index, 1});
    encodeAndSubmit();
    snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState sleeping = snapshot->bodies.front();
    EXPECT_FALSE(sleeping.awake);
    EXPECT_EQ(sleeping.position, moving.position);
    EXPECT_EQ(sleeping.linearVelocity, glm::vec3(0.0f));

    PhysicsCommand force;
    force.type = PhysicsCommandType::ApplyForce;
    force.body = body;
    force.a = glm::vec4(300.0f, 0.0f, 0.0f, 0.0f);
    world.enqueue(std::span<const PhysicsCommand>(&force, 1));
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({body.index, 1});
    encodeAndSubmit();
    snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState awakened = snapshot->bodies.front();
    EXPECT_TRUE(awakened.awake);
    EXPECT_GT(awakened.position.x, sleeping.position.x);
}

TEST_F(GpuPhysicsTest, VelocityCommandsWakeSleepingBodies) {
    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    stepTicks(1u);

    PhysicsCommand sleep;
    sleep.type = PhysicsCommandType::Sleep;
    sleep.body = body;
    world.enqueue(std::span<const PhysicsCommand>(&sleep, 1u));
    stepTicks(1u);
    const auto sleeping = snapshotRange(body.index, 1u);
    ASSERT_TRUE(sleeping.has_value());
    ASSERT_FALSE(sleeping->bodies[0].awake);

    PhysicsCommand velocity;
    velocity.type = PhysicsCommandType::SetVelocity;
    velocity.body = body;
    velocity.a = glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
    world.enqueue(std::span<const PhysicsCommand>(&velocity, 1u));
    stepTicks(1u);
    const auto linear = snapshotRange(body.index, 1u);
    ASSERT_TRUE(linear.has_value());
    EXPECT_TRUE(linear->bodies[0].awake);
    EXPECT_GT(linear->bodies[0].position.x,
              sleeping->bodies[0].position.x);

    world.enqueue(std::span<const PhysicsCommand>(&sleep, 1u));
    stepTicks(1u);
    PhysicsCommand angular;
    angular.type = PhysicsCommandType::SetAngularVelocity;
    angular.body = body;
    angular.a = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    world.enqueue(std::span<const PhysicsCommand>(&angular, 1u));
    stepTicks(1u);
    const auto rotating = snapshotRange(body.index, 1u);
    ASSERT_TRUE(rotating.has_value());
    EXPECT_TRUE(rotating->bodies[0].awake);
    EXPECT_GT(glm::length(rotating->bodies[0].angularVelocity), 0.1f);
}

TEST_F(GpuPhysicsTest, SleepClearsSameTickForceInsteadOfDeferringIt) {
    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    stepTicks(1u);

    std::array<PhysicsCommand, 2u> commands;
    commands[0].type = PhysicsCommandType::ApplyForce;
    commands[0].body = body;
    commands[0].a = glm::vec4(300.0f, 0.0f, 0.0f, 0.0f);
    commands[1].type = PhysicsCommandType::Sleep;
    commands[1].body = body;
    world.enqueue(commands);
    stepTicks(1u);
    const auto sleeping = snapshotRange(body.index, 1u);
    ASSERT_TRUE(sleeping.has_value());
    ASSERT_FALSE(sleeping->bodies[0].awake);

    PhysicsCommand wake;
    wake.type = PhysicsCommandType::Wake;
    wake.body = body;
    world.enqueue(std::span<const PhysicsCommand>(&wake, 1u));
    stepTicks(1u);
    const auto awakened = snapshotRange(body.index, 1u);
    ASSERT_TRUE(awakened.has_value());
    EXPECT_NEAR(awakened->bodies[0].position.x,
                sleeping->bodies[0].position.x, 1.0e-5f);
    EXPECT_NEAR(awakened->bodies[0].linearVelocity.x, 0.0f, 1.0e-5f);
}

TEST_F(GpuPhysicsTest, KinematicTargetsCarryVelocityIntoContactsThenStop) {
    BodySpawnDesc moverDesc;
    moverDesc.shape = ThrowableShape::Cube;
    moverDesc.position = {-1.0f, 20.0f, 0.0f};
    moverDesc.dimensions = glm::vec3(1.0f);
    moverDesc.inverseMass = 0.0f;
    const BodyHandle mover = world.spawnBody(moverDesc);

    BodySpawnDesc dynamicDesc;
    dynamicDesc.shape = ThrowableShape::Sphere;
    dynamicDesc.position = {1.0f, 20.0f, 0.0f};
    dynamicDesc.dimensions = glm::vec3(1.0f);
    const BodyHandle dynamic = world.spawnBody(dynamicDesc);
    ASSERT_TRUE(mover.valid());
    ASSERT_TRUE(dynamic.valid());
    stepTicks(1u);

    PhysicsCommand target;
    target.type = PhysicsCommandType::SetKinematicTarget;
    target.body = mover;
    target.a = glm::vec4(0.0f, 20.0f, 0.0f, 0.0f);
    target.b = glm::vec4(0.0f, 0.0f, std::sin(0.3926990817f),
                         std::cos(0.3926990817f));
    world.enqueue(std::span<const PhysicsCommand>(&target, 1u));
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({mover.index, 2u});
    encodeAndSubmit();
    const auto moving = retireDebugReadback();
    ASSERT_TRUE(moving.has_value());
    ASSERT_EQ(moving->bodies.size(), 2u);
    EXPECT_TRUE(moving->bodies[0].kinematic)
        << "runtime flags=" << std::hex << moving->bodies[0].runtimeFlags;
    EXPECT_NEAR(moving->bodies[0].position.x, 0.0f, 1e-5f);
    EXPECT_GT(moving->bodies[1].linearVelocity.x, 0.1f)
        << "a zero-penetration kinematic contact did not transfer motion";

    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({mover.index, 1u});
    encodeAndSubmit();
    const auto stopped = retireDebugReadback();
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(stopped->bodies.size(), 1u);
    EXPECT_TRUE(stopped->bodies[0].kinematic);
    EXPECT_EQ(stopped->bodies[0].linearVelocity, glm::vec3(0.0f));
    EXPECT_EQ(stopped->bodies[0].angularVelocity, glm::vec3(0.0f));
    EXPECT_EQ(retireTelemetry(world.encodedTick()).kinematicBodies, 1u);
}

TEST_F(GpuPhysicsTest, LastSameTickKinematicTargetUsesWholeTickVelocity) {
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Cube;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = glm::vec3(1.0f);
    desc.inverseMass = 0.0f;
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    stepTicks(1u);

    PhysicsCommand first;
    first.type = PhysicsCommandType::SetKinematicTarget;
    first.body = body;
    first.a = glm::vec4(0.02f, 20.0f, 0.0f, 0.0f);
    first.b = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    PhysicsCommand last = first;
    last.a.x = 0.04f;
    const std::array targets{first, last};
    world.enqueue(targets);
    stepTicks(1u);

    const auto snapshot = snapshotRange(body.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_NEAR(snapshot->bodies[0].position.x, 0.04f, 1.0e-6f);
    EXPECT_NEAR(snapshot->bodies[0].linearVelocity.x, 2.4f, 1.0e-4f);
}

TEST_F(GpuPhysicsTest, RedundantKinematicTargetsDoNotDropFinalPoseAtCapacity) {
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Cube;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = glm::vec3(1.0f);
    desc.inverseMass = 0.0f;
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    stepTicks(1u);

    std::vector<PhysicsCommand> targets(2'049u);
    for (PhysicsCommand& target : targets) {
        target.type = PhysicsCommandType::SetKinematicTarget;
        target.body = body;
        target.a = glm::vec4(1.0f, 20.0f, 0.0f, 0.0f);
        target.b = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    targets.back().a.x = 10.0f;
    world.enqueue(targets);
    stepTicks(1u);

    const auto snapshot = snapshotRange(body.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_NEAR(snapshot->bodies[0].position.x, 10.0f, 1.0e-6f);
    EXPECT_FALSE(world.stats().commandCapacityOverflow);
}

TEST_F(GpuPhysicsTest, SetMaterialChangesResidentContactResponse) {
    const auto spawnPair = [&](float y) {
        BodySpawnDesc left;
        left.position = {-0.5f, y, 0.0f};
        left.linearVelocity = {2.0f, 0.0f, 0.0f};
        left.dimensions = glm::vec3(1.0f);
        BodySpawnDesc right = left;
        right.position.x = 0.5f;
        right.linearVelocity.x = -2.0f;
        return std::array{world.spawnBody(left), world.spawnBody(right)};
    };
    const auto inelastic = spawnPair(30.0f);
    const auto elastic = spawnPair(40.0f);
    ASSERT_TRUE(std::ranges::all_of(inelastic,
        [](BodyHandle handle) { return handle.valid(); }));
    ASSERT_TRUE(std::ranges::all_of(elastic,
        [](BodyHandle handle) { return handle.valid(); }));

    std::array<PhysicsCommand, 4u> commands;
    for (size_t index = 0; index < commands.size(); ++index) {
        commands[index].type = PhysicsCommandType::SetMaterial;
        commands[index].body = index < 2u
            ? inelastic[index] : elastic[index - 2u];
        commands[index].material = PhysicsMaterial{
            .friction = 0.0f,
            .restitution = index < 2u ? 0.0f : 1.0f,
            .rollingResistance = 0.0f,
            .density = 1.0f,
            .flags = 0xf123'4560u + static_cast<uint32_t>(index),
        };
    }
    world.enqueue(commands);
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({inelastic[0].index, 4u});
    encodeAndSubmit();
    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 4u);
    for (size_t index = 0; index < snapshot->bodies.size(); ++index) {
        EXPECT_FLOAT_EQ(snapshot->bodies[index].material.restitution,
                        index < 2u ? 0.0f : 1.0f);
        EXPECT_EQ(snapshot->bodies[index].material.flags,
                  0xf123'4560u + static_cast<uint32_t>(index));
    }
    const float inelasticSeparationVelocity =
        snapshot->bodies[1].linearVelocity.x
        - snapshot->bodies[0].linearVelocity.x;
    const float elasticSeparationVelocity =
        snapshot->bodies[3].linearVelocity.x
        - snapshot->bodies[2].linearVelocity.x;
    EXPECT_GT(elasticSeparationVelocity,
              inelasticSeparationVelocity + 1.0f);
    EXPECT_GT(elasticSeparationVelocity, 1.0f);
}

TEST_F(GpuPhysicsTest, CapsuleUsesSpherocylinderInertiaAndSnapshotsDimensions) {
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Capsule;
    desc.position = {0.0f, 30.0f, 0.0f};
    desc.dimensions = {1.0f, 4.0f, 1.0f};
    desc.inverseMass = 1.0f;
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({body.index, 1u});
    encodeAndSubmit();
    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    const DebugBodyState& state = snapshot->bodies[0];
    EXPECT_EQ(state.dimensions, desc.dimensions);

    constexpr float radius = 0.5f;
    constexpr float cylinderLength = 3.0f;
    constexpr float cylinderFraction = cylinderLength
        / (cylinderLength + (4.0f / 3.0f) * radius);
    constexpr float capFraction = 1.0f - cylinderFraction;
    constexpr float axial = cylinderFraction * 0.5f * radius * radius
        + capFraction * 0.4f * radius * radius;
    constexpr float capCenter = 0.5f * cylinderLength + 0.375f * radius;
    constexpr float transverse = cylinderFraction
            * (3.0f * radius * radius
                + cylinderLength * cylinderLength) / 12.0f
        + capFraction * ((83.0f / 320.0f) * radius * radius
                         + capCenter * capCenter);
    EXPECT_NEAR(state.inverseInertia.x, 1.0f / transverse, 1e-5f);
    EXPECT_NEAR(state.inverseInertia.y, 1.0f / axial, 1e-5f);
    EXPECT_NEAR(state.inverseInertia.z, 1.0f / transverse, 1e-5f);
    EXPECT_GT(state.inverseInertia.y, 8.0f);
}

TEST_F(GpuPhysicsTest, MaxHeightMipRejectsBodyFarAboveTerrain) {
    attachFlatTerrain();
    BodySpawnDesc desc;
    desc.position = {0.0f, 20.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    stepTicks(1);
    const auto snapshot = snapshotRange(body.index, 1);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_TRUE(snapshot->bodies.front().terrainRejectedByMip);
    EXPECT_EQ(snapshot->bodies.front().staticContactCount, 0u);
}

TEST_F(GpuPhysicsTest, AllPrimitiveShapesSettleOnFlatTerrain) {
    attachFlatTerrain();
    std::array<BodyHandle, static_cast<size_t>(ThrowableShape::Count)> bodies{};
    for (uint32_t shapeIndex = 0;
         shapeIndex < static_cast<uint32_t>(ThrowableShape::Count);
         ++shapeIndex) {
        BodySpawnDesc desc;
        desc.shape = static_cast<ThrowableShape>(shapeIndex);
        desc.position = {-8.0f + 4.0f * static_cast<float>(shapeIndex),
                         3.0f + 0.25f * static_cast<float>(shapeIndex), 0.0f};
        desc.angularVelocity = {0.1f, 0.2f, -0.1f};
        desc.dimensions = throwableShapeDimensions(desc.shape);
        bodies[shapeIndex] = world.spawnBody(desc);
        ASSERT_TRUE(bodies[shapeIndex].valid());
    }

    stepTicks(300);
    const auto snapshot = snapshotRange(
        bodies.front().index, static_cast<uint32_t>(bodies.size()));
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), bodies.size());
    for (const DebugBodyState& body : snapshot->bodies) {
        EXPECT_TRUE(body.alive);
        EXPECT_FALSE(body.awake)
            << throwableShapeName(body.shape)
            << " linear=" << body.linearVelocity.x << ','
            << body.linearVelocity.y << ',' << body.linearVelocity.z
            << " angular=" << body.angularVelocity.x << ','
            << body.angularVelocity.y << ',' << body.angularVelocity.z;
        EXPECT_GT(body.position.y, -0.15f) << throwableShapeName(body.shape);
        EXPECT_LT(body.position.y, 1.35f) << throwableShapeName(body.shape);
        EXPECT_LT(glm::length(body.linearVelocity), 0.20f)
            << throwableShapeName(body.shape);
        EXPECT_TRUE(body.staticContactCount != 0u || !body.awake)
            << throwableShapeName(body.shape);
    }
}

TEST_F(GpuPhysicsTest, ThrownTerrainBodyEventuallySleeps) {
    attachFlatTerrain();
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Sphere;
    desc.position = {0.0f, 3.0f, 0.0f};
    desc.linearVelocity = {5.0f, 0.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    stepTicks(600u);
    const auto snapshot = snapshotRange(body.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    const DebugBodyState& state = snapshot->bodies.front();
    EXPECT_GT(state.position.x, 1.0f);
    EXPECT_FALSE(state.awake)
        << "linear=" << state.linearVelocity.x << ','
        << state.linearVelocity.y << ',' << state.linearVelocity.z
        << " angular=" << state.angularVelocity.x << ','
        << state.angularVelocity.y << ',' << state.angularVelocity.z;
}

TEST_F(GpuPhysicsTest, RestingTerrainPileEventuallySleeps) {
    attachFlatTerrain();
    constexpr uint32_t bodyCount = 32u;
    BodyHandle first{};
    for (uint32_t index = 0u; index < bodyCount; ++index) {
        BodySpawnDesc desc;
        desc.shape = ThrowableShape::Sphere;
        desc.dimensions = throwableShapeDimensions(desc.shape);
        const uint32_t layer = index / 16u;
        const uint32_t cell = index % 16u;
        desc.position = {
            (static_cast<float>(cell % 4u) - 1.5f) * 1.0f,
            3.0f + static_cast<float>(layer) * 1.0f,
            (static_cast<float>(cell / 4u) - 1.5f) * 1.0f,
        };
        const BodyHandle body = world.spawnBody(desc);
        ASSERT_TRUE(body.valid());
        if (index == 0u) first = body;
    }

    stepTicks(600u);
    const auto firstSnapshot = snapshotRange(first.index, bodyCount);
    ASSERT_TRUE(firstSnapshot.has_value());
    ASSERT_EQ(firstSnapshot->bodies.size(), bodyCount);
    stepTicks(120u);
    const auto snapshot = snapshotRange(first.index, bodyCount);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), bodyCount);
    uint32_t awakeCount = 0u;
    float maximumLinearSpeed = 0.0f;
    float maximumAngularSpeed = 0.0f;
    float maximumDisplacement = 0.0f;
    for (uint32_t index = 0u; index < bodyCount; ++index) {
        const DebugBodyState& state = snapshot->bodies[index];
        awakeCount += state.awake ? 1u : 0u;
        maximumLinearSpeed = std::max(
            maximumLinearSpeed, glm::length(state.linearVelocity));
        maximumAngularSpeed = std::max(
            maximumAngularSpeed, glm::length(state.angularVelocity));
        maximumDisplacement = std::max(maximumDisplacement, glm::length(
            state.position - firstSnapshot->bodies[index].position));
    }
    EXPECT_EQ(awakeCount, 0u)
        << "maximum linear speed=" << maximumLinearSpeed
        << " maximum angular speed=" << maximumAngularSpeed
        << " displacement over 120 ticks=" << maximumDisplacement;
    EXPECT_LT(maximumDisplacement, 1.0e-3f);
}

TEST_F(GpuPhysicsTest, SettlesOnTerrainAcrossSectorBoundary) {
    constexpr uint32_t width = 520;
    constexpr uint32_t height = 4;
    std::vector<uint16_t> samples(width * height, 32'768u);
    ASSERT_TRUE(world.setTerrain(
        samples, width, height, 10.0f, 1.0f));

    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Sphere;
    desc.dimensions = throwableShapeDimensions(desc.shape);
    desc.position = {-127.75f, 3.0f, 0.0f};
    desc.sector = {1, 0, 0};
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    stepTicks(240);
    const auto snapshot = snapshotRange(body.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    const DebugBodyState& state = snapshot->bodies.front();
    EXPECT_EQ(state.sector, glm::ivec3(1, 0, 0));
    EXPECT_NEAR(state.position.x, -127.75f, 0.01f);
    EXPECT_GT(state.position.y, 0.45f);
    EXPECT_LT(state.position.y, 0.65f);
    EXPECT_TRUE(state.staticContactCount != 0u || !state.awake);
}

TEST_F(GpuPhysicsTest, SphereSlidesAcrossCanonicalDiagonalWithoutSnagging) {
    constexpr uint32_t extent = 64;
    std::vector<uint16_t> samples(extent * extent);
    for (uint32_t z = 0; z < extent; ++z) {
        for (uint32_t x = 0; x < extent; ++x) {
            samples[z * extent + x] = static_cast<uint16_t>(
                30'000u + x * 120u + z * 20u);
        }
    }
    ASSERT_TRUE(world.setTerrain(samples, extent, extent, 10.0f, 1.0f));

    BodySpawnDesc desc;
    desc.position = {8.25f, 5.0f, 8.25f};
    desc.linearVelocity = {-0.5f, 0.0f, -0.5f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    stepTicks(240);
    const auto snapshot = snapshotRange(body.index, 1);
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState& state = snapshot->bodies.front();
    EXPECT_LT(state.position.x, desc.position.x - 1.0f);
    EXPECT_LT(state.position.z, desc.position.z - 0.2f);
    EXPECT_GT(state.position.y, -9.0f);
    EXPECT_LT(glm::length(state.linearVelocity), 5.01f);
}

TEST_F(GpuPhysicsTest, WaterKeepsBodyAfloatAndDryBodyFallsToSeabed) {
    attachFlatTerrain(0u, 10.0f);
    world.setWaterPlane(0.0f, true);
    BodySpawnDesc wetDesc;
    wetDesc.position = {-4.0f, 3.0f, 0.0f};
    wetDesc.dimensions = throwableShapeDimensions(wetDesc.shape);
    const BodyHandle wet = world.spawnBody(wetDesc);
    ASSERT_TRUE(wet.valid());

    stepTicks(360);
    auto snapshot = snapshotRange(wet.index, 1);
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState floating = snapshot->bodies.front();
    EXPECT_TRUE(floating.submerged);
    EXPECT_GT(floating.position.y, -1.5f);
    EXPECT_LT(floating.position.y, 1.0f);

    world.setWaterPlane(0.0f, false);
    BodySpawnDesc dryDesc = wetDesc;
    dryDesc.position = {4.0f, 3.0f, 0.0f};
    const BodyHandle dry = world.spawnBody(dryDesc);
    ASSERT_TRUE(dry.valid());
    stepTicks(240);
    snapshot = snapshotRange(dry.index, 1);
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState seabed = snapshot->bodies.front();
    EXPECT_FALSE(seabed.submerged);
    EXPECT_LT(seabed.position.y, -8.5f);
    EXPECT_GT(seabed.position.y, -10.2f);
}

TEST_F(GpuPhysicsTest, SamplesSharedGpuWaterDisplacementWithoutReadback) {
    gpu::TextureDesc textureDesc = gpu::TextureDesc::tex2D(
        1u, 1u, WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "physics_test_water_displacement");
    textureDesc.depthOrArrayLayers = 3u;
    WGPUTexture texture = gpu::createTexture(
        gpuContext.getDevice(), textureDesc);
    ASSERT_NE(texture, nullptr);
    const std::array<uint8_t, 4> raisedSurface{255u, 0u, 0u, 0u};
    gpu::writeTexture(
        gpuContext.getQueue(), texture, std::as_bytes(std::span(raisedSurface)),
        1u, 1u, 4u);
    gpu::TextureViewDesc viewDesc;
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2DArray;
    viewDesc.arrayLayerCount = 3u;
    WGPUTextureView view = gpu::createTextureView(texture, viewDesc);
    WGPUSampler sampler = gpu::createSampler(
        gpuContext.getDevice(), gpu::SamplerDesc::repeat(
            WGPUFilterMode_Linear, "physics_test_water_sampler"));
    ASSERT_NE(view, nullptr);
    ASSERT_NE(sampler, nullptr);

    world.setWaterPlane(0.0f, true);
    world.setWaterGpuResources({view, sampler, 1.0f});
    BodySpawnDesc raisedDesc;
    raisedDesc.position = {0.0f, 1.25f, 0.0f};
    raisedDesc.dimensions = throwableShapeDimensions(raisedDesc.shape);
    const BodyHandle raised = world.spawnBody(raisedDesc);
    ASSERT_TRUE(raised.valid());
    stepTicks(1u);
    auto snapshot = snapshotRange(raised.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_TRUE(snapshot->bodies.front().submerged);

    world.setWaterGpuResources({});
    BodySpawnDesc flatDesc = raisedDesc;
    flatDesc.position.x = 4.0f;
    const BodyHandle flat = world.spawnBody(flatDesc);
    ASSERT_TRUE(flat.valid());
    stepTicks(1u);
    snapshot = snapshotRange(flat.index, 1u);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_FALSE(snapshot->bodies.front().submerged);

    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(view);
    wgpuTextureDestroy(texture);
    wgpuTextureRelease(texture);
}

TEST_F(GpuPhysicsTest, ShorelineBodyHasTerrainAndWaterContact) {
    constexpr uint32_t extent = 64;
    std::vector<uint16_t> samples(extent * extent);
    for (uint32_t z = 0; z < extent; ++z) {
        for (uint32_t x = 0; x < extent; ++x) {
            samples[z * extent + x] = static_cast<uint16_t>(
                28'000u + x * 150u);
        }
    }
    ASSERT_TRUE(world.setTerrain(samples, extent, extent, 10.0f, 1.0f));
    world.setWaterPlane(0.0f, true);
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Box;
    desc.position = {0.0f, 3.0f, 0.0f};
    desc.dimensions = throwableShapeDimensions(desc.shape);
    const BodyHandle body = world.spawnBody(desc);
    ASSERT_TRUE(body.valid());

    stepTicks(300);
    const auto snapshot = snapshotRange(body.index, 1);
    ASSERT_TRUE(snapshot.has_value());
    const DebugBodyState& state = snapshot->bodies.front();
    EXPECT_TRUE(state.submerged);
    EXPECT_GT(state.position.y, -2.0f);
    EXPECT_LT(state.position.y, 1.0f);
}

TEST_F(GpuPhysicsTest, CharacterUsesCpuTerrainMoverWithoutWorldReadback) {
    attachFlatTerrain();
    EXPECT_TRUE(world.capabilities().synchronousCharacter);
    CharacterSettings settings;
    const CharacterHandle character = world.createCharacter(
        {0.0f, 4.0f, 0.0f}, settings);
    ASSERT_NE(character, InvalidCharacter);
    CharacterMotion motion;
    for (uint32_t tick = 0; tick < 120; ++tick) {
        motion = world.moveCharacter(
            character, {2.0f, 0.0f, 0.0f}, false, 6.0f,
            20.0f, 50.0f, 1.0f / 60.0f);
    }
    EXPECT_TRUE(motion.grounded);
    EXPECT_GT(motion.position.x, 3.0f);
    EXPECT_NEAR(motion.position.y, 0.0f, 0.01f);
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 0u);
    world.destroyCharacter(character);
}

TEST_F(GpuPhysicsTest, ExplicitBulletDoesNotTunnelThroughTerrain) {
    attachFlatTerrain();
    EXPECT_TRUE(world.capabilities().continuousCollision);
    BodySpawnDesc desc;
    desc.shape = ThrowableShape::Sphere;
    desc.dimensions = {1.0f, 1.0f, 1.0f};
    desc.position = {0.0f, 5.0f, 0.0f};
    desc.linearVelocity = {0.0f, -600.0f, 0.0f};
    desc.bullet = true;
    const BodyHandle bullet = world.spawnBody(desc);
    ASSERT_TRUE(bullet.valid());
    world.update(1.0f / 60.0f);
    world.requestDebugSnapshot({bullet.index, 1});
    encodeAndSubmit();
    const auto snapshot = retireDebugReadback();
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->bodies.size(), 1u);
    EXPECT_GT(snapshot->bodies[0].position.y, 0.45f);
    EXPECT_LT(snapshot->bodies[0].position.y, 0.60f);
    EXPECT_GE(snapshot->bodies[0].linearVelocity.y, -0.1f);
}

} // namespace
} // namespace voxy::physics
