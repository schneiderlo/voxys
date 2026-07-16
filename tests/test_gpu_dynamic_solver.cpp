#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_dynamic_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
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
    glm::vec4 inverseInertiaMaterial{0.0f};
};
using TestMetadata = std::array<uint32_t, 4>;

TestMetadata makeMetadata(bool awake) {
    const uint32_t flags = kGpuBodyAliveFlag
        | (awake ? kGpuBodyAwakeFlag : 0u);
    return {0u, 0u, 0u, packGpuBodyMetadata(1u, flags)};
}

struct SolverSnapshot {
    GpuDynamicSolverTelemetry telemetry;
    std::vector<uint32_t> colors;
    std::vector<GpuContactManifold> manifolds;
    std::vector<TestPose> poses;
    std::vector<TestMotion> motions;
};

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

SolverSnapshot runAndRead(gpu::Context& context, GpuDynamicSolver& solver,
                          WGPUBuffer poseBuffer, WGPUBuffer motionBuffer,
                          WGPUBuffer manifoldBuffer, uint32_t bodyCapacity,
                          uint32_t contactCapacity, uint32_t tickCount = 1u,
                          bool compactColorSolve = false) {
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    for (uint32_t tick = 0; tick < tickCount; ++tick) {
        EXPECT_TRUE(solver.encode(encoder, compactColorSolve));
    }
    const size_t colorBytes = size_t{contactCapacity} * sizeof(uint32_t);
    const size_t manifoldBytes = size_t{contactCapacity}
                               * sizeof(GpuContactManifold);
    const size_t poseBytes = size_t{bodyCapacity} * sizeof(TestPose);
    const size_t motionBytes = size_t{bodyCapacity} * sizeof(TestMotion);
    constexpr size_t telemetryBytes = 64u * sizeof(uint32_t);
    const size_t totalBytes = colorBytes + manifoldBytes + poseBytes
                            + motionBytes + telemetryBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "dynamic_solver_readback",
            .size = totalBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    EXPECT_NE(readback, nullptr);
    if (!readback) return {};
    size_t offset = 0;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, solver.colors(), 0, readback, offset, colorBytes);
    offset += colorBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, manifoldBuffer, 0, readback, offset, manifoldBytes);
    offset += manifoldBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, poseBuffer, 0, readback, offset, poseBytes);
    offset += poseBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, motionBuffer, 0, readback, offset, motionBytes);
    offset += motionBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, solver.telemetryBuffer(), 0, readback, offset, telemetryBytes);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    struct MapState { bool done = false; bool success = false; } mapState;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& state = *static_cast<MapState*>(userdata);
        state.success = status == WGPUBufferMapAsyncStatus_Success;
        state.done = true;
    };
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, totalBytes,
                       callback, &mapState);
    while (!mapState.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    EXPECT_TRUE(mapState.success);
    SolverSnapshot result;
    if (mapState.success) {
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
        offset = 0;
        result.colors.resize(contactCapacity);
        std::memcpy(result.colors.data(), bytes + offset, colorBytes);
        offset += colorBytes;
        result.manifolds.resize(contactCapacity);
        std::memcpy(result.manifolds.data(), bytes + offset, manifoldBytes);
        offset += manifoldBytes;
        result.poses.resize(bodyCapacity);
        std::memcpy(result.poses.data(), bytes + offset, poseBytes);
        offset += poseBytes;
        result.motions.resize(bodyCapacity);
        std::memcpy(result.motions.data(), bytes + offset, motionBytes);
        offset += motionBytes;
        std::array<uint32_t, 64> telemetry{};
        std::memcpy(telemetry.data(), bytes + offset, telemetryBytes);
        result.telemetry = GpuDynamicSolver::decodeTelemetry(telemetry);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferDestroy(readback);
    wgpuBufferRelease(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    return result;
}

GpuContactManifold makeContact(uint32_t bodyA, uint32_t bodyB,
                               const glm::vec3& normal,
                               const glm::vec3& localAnchorA,
                               const glm::vec3& localAnchorB,
                               float separation) {
    GpuContactManifold result;
    result.pair = {bodyB, bodyA, 0u, 0u};
    result.state = {1u, 0u, 0u, 0u};
    result.normal = {normal.x, normal.y, normal.z, separation};
    const glm::vec3 tangent1 = std::abs(normal.x) < 0.7f
        ? glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), normal))
        : glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal));
    const glm::vec3 tangent2 = glm::cross(normal, tangent1);
    result.tangent1 = {tangent1.x, tangent1.y, tangent1.z, 0.0f};
    result.tangent2 = {tangent2.x, tangent2.y, tangent2.z, 0.0f};
    result.frictionAnchorA = {
        localAnchorA.x, localAnchorA.y, localAnchorA.z, 0.0f};
    result.frictionAnchorB = {
        localAnchorB.x, localAnchorB.y, localAnchorB.z, 0.0f};
    result.points[0].localAnchorASeparation = {
        localAnchorA.x, localAnchorA.y, localAnchorA.z, separation};
    result.points[0].localAnchorBNormalImpulse = {
        localAnchorB.x, localAnchorB.y, localAnchorB.z, 0.0f};
    result.points[0].features = {1u, 1u, 0u, 0u};
    return result;
}

class GpuDynamicColoringTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(GpuDynamicColoringTest, ColorsConflictsAndGathersOverflowDeterministically) {
    // Cross the production solver's compact-world threshold so colors 4-15
    // exercise the batched tail-color path.
    constexpr uint32_t bodyCapacity = 4'097;
    constexpr uint32_t contactCapacity = 32;
    constexpr uint32_t contactCount = 18;
    // Cross the eight-round continuation gate and still leave explicit
    // overflow in this single-body contact fan.
    constexpr uint32_t colorCount = 16;
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
    std::vector<GpuContactManifold> manifolds(contactCapacity);
    for (uint32_t body = 1; body <= contactCount + 1u; ++body) {
        poses[body].positionInvMass = {float(body), 0.0f, 0.0f, 0.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
        metadata[body] = makeMetadata(true);
    }
    for (uint32_t rank = 0; rank < contactCount; ++rank) {
        manifolds[rank] = makeContact(
            1u, rank + 2u, glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f), glm::vec3(0.0f), 0.0f);
        manifolds[rank].pair.ordinal = rank;
    }
    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[10] = contactCount;
    narrowTelemetry[11] = contactCount;
    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "color_poses");
    WGPUBuffer motionBuffer = makeStorage<TestMotion>(
        context, motions, "color_motions");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(context, shapes, "color_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "color_metadata");
    WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "color_manifolds");
    WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
        context, narrowTelemetry, "color_narrow_telemetry");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(motionBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);
    ASSERT_NE(manifoldBuffer, nullptr);
    ASSERT_NE(narrowTelemetryBuffer, nullptr);

    GpuDynamicSolver solver;
    GpuDynamicSolver::Config config;
    config.bodyCapacity = bodyCapacity;
    config.contactCapacity = contactCapacity;
    config.colorCount = colorCount;
    config.workgroupSize = GetParam();
    config.substeps = 1;
    config.gravity = {0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(solver.initialize(
        context.getDevice(), context.getQueue(), config));
    solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                     manifoldBuffer, narrowTelemetryBuffer,
                     bodyCapacity, contactCapacity});
    const std::array<uint32_t, 32> emptyNarrowTelemetry{};
    gpu::writeBuffer(context.getQueue(), narrowTelemetryBuffer, 0,
        std::as_bytes(std::span<const uint32_t>(emptyNarrowTelemetry)));
    const SolverSnapshot empty = runAndRead(
        context, solver, poseBuffer, motionBuffer, manifoldBuffer,
        bodyCapacity, contactCapacity);
    EXPECT_EQ(empty.telemetry.contactCount, 0u);
    gpu::writeBuffer(context.getQueue(), narrowTelemetryBuffer, 0,
        std::as_bytes(std::span<const uint32_t>(narrowTelemetry)));
    const SolverSnapshot first = runAndRead(
        context, solver, poseBuffer, motionBuffer, manifoldBuffer,
        bodyCapacity, contactCapacity);
    EXPECT_EQ(solver.inputBindGroupCacheMisses(), 9u);
    EXPECT_EQ(first.telemetry.contactCount, contactCount);
    EXPECT_EQ(first.telemetry.coloredContacts, colorCount);
    EXPECT_EQ(first.telemetry.overflowContacts, contactCount - colorCount);
    EXPECT_EQ(first.telemetry.maximumBodyDegree, contactCount - colorCount);
    EXPECT_EQ(first.telemetry.conflictErrors, 0u);
    EXPECT_EQ(first.telemetry.overflowIterations, config.overflowIterations);
    for (uint32_t rank = 0; rank < colorCount; ++rank) {
        EXPECT_EQ(first.colors[rank], rank);
        EXPECT_EQ(first.telemetry.colorCounts[rank], 1u);
    }
    for (uint32_t rank = colorCount; rank < contactCount; ++rank) {
        EXPECT_EQ(first.colors[rank], 0xffff'ffffu);
    }
    for (uint32_t color = 0; color < colorCount; ++color) {
        std::set<uint32_t> bodies;
        for (uint32_t rank = 0; rank < contactCount; ++rank) {
            if (first.colors[rank] != color) continue;
            EXPECT_TRUE(bodies.insert(first.manifolds[rank].pair.keyHigh).second);
            EXPECT_TRUE(bodies.insert(first.manifolds[rank].pair.keyLow).second);
        }
    }

    const SolverSnapshot second = runAndRead(
        context, solver, poseBuffer, motionBuffer, manifoldBuffer,
        bodyCapacity, contactCapacity);
    EXPECT_EQ(solver.inputBindGroupCacheMisses(), 9u);
    EXPECT_EQ(second.colors, first.colors);
    EXPECT_EQ(second.telemetry.persistentColorsRetained, colorCount);
    EXPECT_EQ(second.telemetry.tick, 3u);

    WGPUBuffer alternateManifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "color_alternate_manifolds");
    ASSERT_NE(alternateManifoldBuffer, nullptr);
    solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                     alternateManifoldBuffer, narrowTelemetryBuffer,
                     bodyCapacity, contactCapacity});
    static_cast<void>(runAndRead(
        context, solver, poseBuffer, motionBuffer, alternateManifoldBuffer,
        bodyCapacity, contactCapacity));
    EXPECT_EQ(solver.inputBindGroupCacheMisses(), 18u);
    solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                     manifoldBuffer, narrowTelemetryBuffer,
                     bodyCapacity, contactCapacity});
    static_cast<void>(runAndRead(
        context, solver, poseBuffer, motionBuffer, manifoldBuffer,
        bodyCapacity, contactCapacity));
    EXPECT_EQ(solver.inputBindGroupCacheMisses(), 18u);

    solver.setInput({});
    releaseBuffer(alternateManifoldBuffer);
    releaseBuffer(narrowTelemetryBuffer);
    releaseBuffer(manifoldBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(motionBuffer);
    releaseBuffer(poseBuffer);
}

INSTANTIATE_TEST_SUITE_P(
    WorkgroupProfiles, GpuDynamicColoringTest,
    ::testing::Values(64u, 128u, 256u));

TEST(GpuDynamicSolverTest, SoftStepSeparatesAndFrictionSlowsContact) {
    constexpr uint32_t bodyCapacity = 4;
    constexpr uint32_t contactCapacity = 4;
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
    std::vector<GpuContactManifold> manifolds(contactCapacity);
    poses[1].positionInvMass = {-0.4f, 0.0f, 0.0f, 1.0f};
    poses[2].positionInvMass = {0.4f, 0.0f, 0.0f, 1.0f};
    motions[1].linearVelocitySleep = {1.0f, 1.0f, 0.0f, 0.0f};
    motions[2].linearVelocitySleep = {-1.0f, -1.0f, 0.0f, 0.0f};
    for (uint32_t body : {1u, 2u}) {
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
        shapes[body].inverseInertiaMaterial = {10.0f, 10.0f, 10.0f, 0.0f};
        metadata[body] = makeMetadata(true);
    }
    manifolds[0] = makeContact(
        1u, 2u, glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.5f, 0.0f, 0.0f),
        glm::vec3(-0.5f, 0.0f, 0.0f), -0.2f);
    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[10] = 1u;
    narrowTelemetry[11] = 1u;
    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "solve_poses");
    WGPUBuffer motionBuffer = makeStorage<TestMotion>(
        context, motions, "solve_motions");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(context, shapes, "solve_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "solve_metadata");
    WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "solve_manifolds");
    WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
        context, narrowTelemetry, "solve_narrow_telemetry");

    GpuDynamicSolver solver;
    GpuDynamicSolver::Config config;
    config.bodyCapacity = bodyCapacity;
    config.contactCapacity = contactCapacity;
    config.colorCount = 8;
    config.workgroupSize = 64;
    config.gravity = {0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(solver.initialize(
        context.getDevice(), context.getQueue(), config));
    solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                     manifoldBuffer, narrowTelemetryBuffer,
                     bodyCapacity, contactCapacity});
    const SolverSnapshot snapshot = runAndRead(
        context, solver, poseBuffer, motionBuffer, manifoldBuffer,
        bodyCapacity, contactCapacity);
    ASSERT_EQ(snapshot.telemetry.coloredContacts, 0u);
    ASSERT_EQ(snapshot.telemetry.smallIslandContacts, 1u);
    ASSERT_EQ(snapshot.telemetry.smallIslandBodies, 2u);
    EXPECT_LT(snapshot.motions[1].linearVelocitySleep.x,
              motions[1].linearVelocitySleep.x);
    EXPECT_GT(snapshot.motions[2].linearVelocitySleep.x,
              motions[2].linearVelocitySleep.x);
    const float initialTangentRelative = 2.0f;
    const float finalTangentRelative = std::abs(
        snapshot.motions[1].linearVelocitySleep.y
        - snapshot.motions[2].linearVelocitySleep.y);
    EXPECT_LT(finalTangentRelative, initialTangentRelative);
    EXPECT_GT(snapshot.manifolds[0].points[0]
                  .localAnchorBNormalImpulse[3], 0.0f);
    EXPECT_GT(snapshot.poses[2].positionInvMass.x
            - snapshot.poses[1].positionInvMass.x, 0.8f);
    EXPECT_NEAR(snapshot.motions[1].linearVelocitySleep.x
              + snapshot.motions[2].linearVelocitySleep.x, 0.0f, 1e-4f);

    releaseBuffer(narrowTelemetryBuffer);
    releaseBuffer(manifoldBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(motionBuffer);
    releaseBuffer(poseBuffer);
}

TEST(GpuDynamicSolverTest, SmallIslandFastPathMatchesGlobalSolver) {
    constexpr uint32_t bodyCapacity = 4;
    constexpr uint32_t contactCapacity = 4;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> initialPoses(bodyCapacity);
    std::vector<TestMotion> initialMotions(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    std::vector<GpuContactManifold> initialManifolds(contactCapacity);
    initialPoses[1].positionInvMass = {-0.4f, 0.1f, 0.0f, 1.0f};
    initialPoses[2].positionInvMass = {0.4f, -0.1f, 0.0f, 0.75f};
    initialMotions[1].linearVelocitySleep = {1.0f, 0.8f, 0.2f, 0.0f};
    initialMotions[2].linearVelocitySleep = {-0.5f, -0.3f, -0.1f, 0.0f};
    initialMotions[1].angularVelocityFlags = {0.1f, 0.2f, -0.3f, 0.0f};
    initialMotions[2].angularVelocityFlags = {-0.2f, 0.3f, 0.1f, 0.0f};
    for (uint32_t body : {1u, 2u}) {
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
        shapes[body].inverseInertiaMaterial = {8.0f, 9.0f, 10.0f, 0.0f};
        metadata[body] = makeMetadata(true);
    }
    initialManifolds[0] = makeContact(
        1u, 2u, glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.5f, -0.1f, 0.0f),
        glm::vec3(-0.5f, 0.1f, 0.0f), -0.2f);
    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[10] = 1u;
    narrowTelemetry[11] = 1u;

    const auto run = [&](bool enableFastPath) {
        WGPUBuffer poseBuffer = makeStorage<TestPose>(
            context, initialPoses, "differential_poses");
        WGPUBuffer motionBuffer = makeStorage<TestMotion>(
            context, initialMotions, "differential_motions");
        WGPUBuffer shapeBuffer = makeStorage<TestShape>(
            context, shapes, "differential_shapes");
        WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
            context, metadata, "differential_metadata");
        WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
            context, initialManifolds, "differential_manifolds");
        WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
            context, narrowTelemetry, "differential_narrow_telemetry");
        GpuDynamicSolver solver;
        GpuDynamicSolver::Config config;
        config.bodyCapacity = bodyCapacity;
        config.contactCapacity = contactCapacity;
        config.colorCount = 8;
        config.workgroupSize = 64;
        config.gravity = {0.0f, -9.81f, 0.0f};
        config.enableSmallIslandFastPath = enableFastPath;
        EXPECT_TRUE(solver.initialize(
            context.getDevice(), context.getQueue(), config));
        solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                         manifoldBuffer, narrowTelemetryBuffer,
                         bodyCapacity, contactCapacity});
        SolverSnapshot snapshot = runAndRead(
            context, solver, poseBuffer, motionBuffer, manifoldBuffer,
            bodyCapacity, contactCapacity);
        releaseBuffer(narrowTelemetryBuffer);
        releaseBuffer(manifoldBuffer);
        releaseBuffer(metadataBuffer);
        releaseBuffer(shapeBuffer);
        releaseBuffer(motionBuffer);
        releaseBuffer(poseBuffer);
        return snapshot;
    };

    SolverSnapshot fast = run(true);
    SolverSnapshot global = run(false);
    ASSERT_EQ(fast.telemetry.smallIslandContacts, 1u);
    ASSERT_EQ(global.telemetry.smallIslandContacts, 0u);
    ASSERT_EQ(global.telemetry.coloredContacts, 1u);
    ASSERT_EQ(fast.poses.size(), global.poses.size());
    ASSERT_EQ(fast.motions.size(), global.motions.size());
    EXPECT_EQ(std::memcmp(fast.poses.data(), global.poses.data(),
                          fast.poses.size() * sizeof(TestPose)), 0);
    EXPECT_EQ(std::memcmp(fast.motions.data(), global.motions.data(),
                          fast.motions.size() * sizeof(TestMotion)), 0);
    fast.manifolds[0].state[2] &= 255u;
    global.manifolds[0].state[2] &= 255u;
    EXPECT_EQ(std::memcmp(&fast.manifolds[0], &global.manifolds[0],
                          sizeof(GpuContactManifold)), 0);

    // Repeat at a large sector boundary. The common velocity moves only body A
    // through the boundary during the four substeps. Different local frames
    // can round at different magnitudes, so compare physical state by a tight
    // float tolerance rather than by bytes.
    constexpr uint32_t baseSector = 1'500'000u;
    initialPoses[1].positionInvMass = {127.995f, 0.1f, 0.0f, 1.0f};
    initialPoses[2].positionInvMass = {-127.205f, -0.1f, 0.0f, 0.75f};
    initialMotions[1].linearVelocitySleep = {11.0f, 0.8f, 0.2f, 0.0f};
    initialMotions[2].linearVelocitySleep = {9.5f, -0.3f, -0.1f, 0.0f};
    metadata[1][0] = baseSector;
    metadata[2][0] = baseSector + 1u;
    fast = run(true);
    global = run(false);
    for (uint32_t body : {1u, 2u}) {
        for (int lane = 0; lane < 4; ++lane) {
            EXPECT_NEAR(fast.poses[body].positionInvMass[lane],
                        global.poses[body].positionInvMass[lane], 2e-4f);
            EXPECT_NEAR(fast.poses[body].orientation[lane],
                        global.poses[body].orientation[lane], 2e-4f);
            EXPECT_NEAR(fast.motions[body].linearVelocitySleep[lane],
                        global.motions[body].linearVelocitySleep[lane], 2e-4f);
            EXPECT_NEAR(fast.motions[body].angularVelocityFlags[lane],
                        global.motions[body].angularVelocityFlags[lane], 2e-4f);
        }
        EXPECT_GE(global.poses[body].positionInvMass.x, -128.0f);
        EXPECT_LT(global.poses[body].positionInvMass.x, 128.0f);
    }
    EXPECT_LT(global.poses[1].positionInvMass.x, -127.0f);
}

TEST(GpuDynamicSolverTest, BoxTowerMixedPileAndAvalancheStayBounded) {
    constexpr uint32_t bodyCapacity = 64;
    constexpr uint32_t contactCapacity = 64;
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
    std::vector<GpuContactManifold> manifolds(contactCapacity);
    const auto makeStatic = [&](uint32_t body, const glm::vec3& position) {
        poses[body].positionInvMass = {position.x, position.y, position.z, 0.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 1.0f};
        metadata[body] = makeMetadata(false);
    };
    const auto makeDynamic = [&](uint32_t body, const glm::vec3& position,
                                 uint32_t shapeType) {
        poses[body].positionInvMass = {position.x, position.y, position.z, 1.0f};
        shapes[body].dimensionsType = {
            0.5f, 0.5f, 0.5f, static_cast<float>(shapeType)};
        const float inertia = 7.0f + static_cast<float>(shapeType);
        shapes[body].inverseInertiaMaterial = {
            inertia, inertia * 0.9f, inertia * 1.1f, 0.0f};
        metadata[body] = makeMetadata(true);
    };
    uint32_t contactCount = 0;
    const auto addContact = [&](uint32_t bodyA, uint32_t bodyB,
                                const glm::vec3& normal,
                                const glm::vec3& anchorA,
                                const glm::vec3& anchorB) {
        manifolds[contactCount] = makeContact(
            bodyA, bodyB, normal, anchorA, anchorB, 0.0f);
        manifolds[contactCount].pair.ordinal = contactCount;
        ++contactCount;
    };

    // Eight-box vertical tower. Every middle box participates in two colors.
    makeStatic(0u, glm::vec3(0.0f));
    for (uint32_t level = 0; level < 8u; ++level) {
        const uint32_t body = level + 1u;
        makeDynamic(body, glm::vec3(0.0f, 0.5f + float(level), 0.0f), 1u);
        if (level == 0u) {
            addContact(0u, body, glm::vec3(0.0f, 1.0f, 0.0f),
                       glm::vec3(0.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        } else {
            addContact(body - 1u, body, glm::vec3(0.0f, 1.0f, 0.0f),
                       glm::vec3(0.0f, 0.5f, 0.0f),
                       glm::vec3(0.0f, -0.5f, 0.0f));
        }
    }

    // Connected 4x4 mixed-shape pile with vertical and horizontal contacts.
    makeStatic(10u, glm::vec3(4.5f, 0.0f, 0.0f));
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            const uint32_t body = 11u + y * 4u + x;
            makeDynamic(body,
                glm::vec3(3.0f + float(x), 0.5f + float(y), 0.0f),
                (x + y) % 5u);
            if (y == 0u) {
                addContact(10u, body, glm::vec3(0.0f, 1.0f, 0.0f),
                           glm::vec3(float(x) - 1.5f, 0.0f, 0.0f),
                           glm::vec3(0.0f, -0.5f, 0.0f));
            } else {
                addContact(body - 4u, body, glm::vec3(0.0f, 1.0f, 0.0f),
                           glm::vec3(0.0f, 0.5f, 0.0f),
                           glm::vec3(0.0f, -0.5f, 0.0f));
            }
            if (x != 0u) {
                addContact(body - 1u, body, glm::vec3(1.0f, 0.0f, 0.0f),
                           glm::vec3(0.5f, 0.0f, 0.0f),
                           glm::vec3(-0.5f, 0.0f, 0.0f));
            }
        }
    }

    // Eight disconnected slope contacts exercise the small-island path.
    const glm::vec3 slopeNormal(-0.3f, std::sqrt(0.91f), 0.0f);
    std::array<float, 8> avalancheStartX{};
    for (uint32_t index = 0; index < 8u; ++index) {
        const uint32_t staticBody = 40u + index;
        const uint32_t dynamicBody = 48u + index;
        const glm::vec3 center(
            14.0f + float(index) * 1.5f,
            5.0f - float(index) * 0.45f, 0.0f);
        avalancheStartX[index] = center.x;
        makeDynamic(dynamicBody, center, index % 5u);
        motions[dynamicBody].linearVelocitySleep = {-0.25f, 0.0f, 0.0f, 0.0f};
        const glm::vec3 contactPoint = center - slopeNormal * 0.5f;
        makeStatic(staticBody, contactPoint);
        addContact(staticBody, dynamicBody, slopeNormal,
                   glm::vec3(0.0f), -slopeNormal * 0.5f);
    }
    ASSERT_LE(contactCount, contactCapacity);

    std::array<uint32_t, 32> narrowTelemetry{};
    narrowTelemetry[10] = contactCount;
    narrowTelemetry[11] = contactCount;
    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "stress_poses");
    WGPUBuffer motionBuffer = makeStorage<TestMotion>(
        context, motions, "stress_motions");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(context, shapes, "stress_shapes");
    WGPUBuffer metadataBuffer = makeStorage<TestMetadata>(
        context, metadata, "stress_metadata");
    WGPUBuffer manifoldBuffer = makeStorage<GpuContactManifold>(
        context, manifolds, "stress_manifolds");
    WGPUBuffer narrowTelemetryBuffer = makeStorage<uint32_t>(
        context, narrowTelemetry, "stress_narrow_telemetry");

    GpuDynamicSolver solver;
    GpuDynamicSolver::Config config;
    config.bodyCapacity = bodyCapacity;
    config.contactCapacity = contactCapacity;
    config.colorCount = 16;
    config.workgroupSize = 128;
    config.substeps = 4;
    config.gravity = {0.0f, -9.81f, 0.0f};
    config.linearDamping = 0.15f;
    config.angularDamping = 0.15f;
    ASSERT_TRUE(solver.initialize(
        context.getDevice(), context.getQueue(), config));
    solver.setInput({poseBuffer, motionBuffer, shapeBuffer, metadataBuffer,
                     manifoldBuffer, narrowTelemetryBuffer,
                     bodyCapacity, contactCapacity});
    const SolverSnapshot snapshot = runAndRead(
        context, solver, poseBuffer, motionBuffer, manifoldBuffer,
        bodyCapacity, contactCapacity, 120u, true);

    EXPECT_EQ(snapshot.telemetry.tick, 120u);
    EXPECT_EQ(snapshot.telemetry.conflictErrors, 0u);
    EXPECT_EQ(snapshot.telemetry.overflowContacts, 0u);
    EXPECT_EQ(snapshot.telemetry.smallIslandContacts, 8u);
    for (uint32_t body = 1u; body <= 55u; ++body) {
        if ((metadata[body][3] & kGpuBodyAwakeFlag) == 0u) continue;
        for (float value : {snapshot.poses[body].positionInvMass.x,
                            snapshot.poses[body].positionInvMass.y,
                            snapshot.poses[body].positionInvMass.z,
                            snapshot.motions[body].linearVelocitySleep.x,
                            snapshot.motions[body].linearVelocitySleep.y,
                            snapshot.motions[body].linearVelocitySleep.z}) {
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_LT(std::abs(value), 100.0f);
        }
    }
    EXPECT_GT(snapshot.poses[8].positionInvMass.y, 6.0f);
    for (uint32_t index = 0; index < 8u; ++index) {
        EXPECT_LT(snapshot.poses[48u + index].positionInvMass.x,
                  avalancheStartX[index]);
    }

    releaseBuffer(narrowTelemetryBuffer);
    releaseBuffer(manifoldBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(motionBuffer);
    releaseBuffer(poseBuffer);
}

} // namespace
} // namespace voxy::physics
