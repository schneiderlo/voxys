#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

    PhysicsStats retireTelemetry() {
        PhysicsStats stats = world.stats();
        for (uint32_t attempt = 0; stats.telemetryTick == 0u
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
                && event.bodyA == std::min(left.index, right.index)
                && event.bodyB == std::max(left.index, right.index);
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
