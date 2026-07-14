#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"
#include "physics/physics_types.hpp"

#include <box3d/collision.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
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

namespace voxy::physics {
namespace {

struct alignas(16) TestPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) TestShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 properties{0.0f};
};

struct NarrowSnapshot {
    GpuNarrowPhaseTelemetry telemetry;
    std::vector<GpuContactManifold> manifolds;
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

NarrowSnapshot runAndRead(gpu::Context& context, GpuNarrowPhase& narrowPhase,
                          uint32_t pairCount) {
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    EXPECT_TRUE(narrowPhase.encode(encoder));
    const size_t manifoldBytes = size_t{narrowPhase.capacity()}
                               * sizeof(GpuContactManifold);
    constexpr size_t telemetryBytes = 32u * sizeof(uint32_t);
    const size_t totalBytes = manifoldBytes + telemetryBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "narrow_phase_readback",
            .size = totalBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    EXPECT_NE(readback, nullptr);
    if (!readback) return {};
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, narrowPhase.manifolds(), 0, readback, 0, manifoldBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, narrowPhase.telemetryBuffer(), 0, readback,
        manifoldBytes, telemetryBytes);
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
    NarrowSnapshot result;
    if (mapState.success) {
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
        result.manifolds.resize(pairCount);
        std::memcpy(result.manifolds.data(), bytes,
                    result.manifolds.size() * sizeof(GpuContactManifold));
        std::array<uint32_t, 32> telemetry{};
        std::memcpy(telemetry.data(), bytes + manifoldBytes, telemetryBytes);
        result.telemetry = GpuNarrowPhase::decodeTelemetry(telemetry);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferDestroy(readback);
    wgpuBufferRelease(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    return result;
}

glm::quat orientation(const TestPose& pose) {
    return glm::quat(pose.orientation.w, pose.orientation.x,
                     pose.orientation.y, pose.orientation.z);
}

struct Box3dProxyOwner {
    std::array<b3Vec3, 2> roundedPoints{};
    b3BoxHull box{};
    b3HullData* cylinder = nullptr;
    b3ShapeProxy proxy{};

    Box3dProxyOwner() = default;
    Box3dProxyOwner(const Box3dProxyOwner&) = delete;
    Box3dProxyOwner& operator=(const Box3dProxyOwner&) = delete;
    ~Box3dProxyOwner() {
        if (cylinder) b3DestroyHull(cylinder);
    }
};

void makeBox3dProxy(Box3dProxyOwner& owner, ThrowableShape shape,
                    const glm::vec3& dimensions) {
    if (shape == ThrowableShape::Sphere) {
        owner.roundedPoints[0] = {0.0f, 0.0f, 0.0f};
        owner.proxy = {owner.roundedPoints.data(), 1, 0.5f * dimensions.x};
        return;
    }
    if (shape == ThrowableShape::Capsule) {
        const float radius = 0.25f * (dimensions.x + dimensions.z);
        const float segment = std::max(0.5f * dimensions.y - radius, 0.0f);
        owner.roundedPoints[0] = {0.0f, -segment, 0.0f};
        owner.roundedPoints[1] = {0.0f, segment, 0.0f};
        owner.proxy = {owner.roundedPoints.data(), 2, radius};
        return;
    }
    if (shape == ThrowableShape::Cylinder) {
        owner.cylinder = b3CreateCylinder(
            dimensions.y, 0.25f * (dimensions.x + dimensions.z), 0.0f, 8);
        ASSERT_NE(owner.cylinder, nullptr);
        owner.proxy = {b3GetHullPoints(owner.cylinder),
                       owner.cylinder->vertexCount, 0.0f};
        return;
    }
    owner.box = b3MakeBoxHull(
        0.5f * dimensions.x, 0.5f * dimensions.y, 0.5f * dimensions.z);
    owner.proxy = {
        b3GetHullPoints(&owner.box.base), owner.box.base.vertexCount, 0.0f};
}

b3Transform toBox3dTransform(const TestPose& pose) {
    return {
        {pose.positionInvMass.x, pose.positionInvMass.y,
         pose.positionInvMass.z},
        {{pose.orientation.x, pose.orientation.y, pose.orientation.z},
         pose.orientation.w},
    };
}

bool box3dOverlaps(ThrowableShape shapeA, const glm::vec3& dimensionsA,
                   const TestPose& poseA, ThrowableShape shapeB,
                   const glm::vec3& dimensionsB, const TestPose& poseB) {
    Box3dProxyOwner ownerA;
    Box3dProxyOwner ownerB;
    makeBox3dProxy(ownerA, shapeA, dimensionsA);
    makeBox3dProxy(ownerB, shapeB, dimensionsB);
    b3DistanceInput input{};
    input.proxyA = ownerA.proxy;
    input.proxyB = ownerB.proxy;
    input.transform = b3InvMulTransforms(
        toBox3dTransform(poseA), toBox3dTransform(poseB));
    input.useRadii = true;
    b3SimplexCache cache{};
    const b3DistanceOutput distance = b3ShapeDistance(
        &input, &cache, nullptr, 0);
    return distance.distance <= 1e-5f;
}

class GpuNarrowPhaseTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(GpuNarrowPhaseTest, GeneratesAllPairClassesAndPersistsPoints) {
    constexpr uint32_t pairCapacity = 64;
    constexpr uint32_t overlapPairCount = 10;
    constexpr uint32_t pairCount = overlapPairCount + 1u;
    constexpr uint32_t bodyCapacity = pairCount * 2u + 1u;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    constexpr std::array<std::array<ThrowableShape, 2>, overlapPairCount>
        pairShapes{{
            {ThrowableShape::Sphere, ThrowableShape::Sphere},
            {ThrowableShape::Sphere, ThrowableShape::Capsule},
            {ThrowableShape::Capsule, ThrowableShape::Capsule},
            {ThrowableShape::Sphere, ThrowableShape::Box},
            {ThrowableShape::Capsule, ThrowableShape::Cube},
            {ThrowableShape::Box, ThrowableShape::Cube},
            {ThrowableShape::Sphere, ThrowableShape::Cylinder},
            {ThrowableShape::Capsule, ThrowableShape::Cylinder},
            {ThrowableShape::Box, ThrowableShape::Cylinder},
            {ThrowableShape::Cylinder, ThrowableShape::Cylinder},
        }};
    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<GpuKeyValue> pairs(pairCapacity, {
        0xffff'ffffu, 0xffff'ffffu, 0xffff'ffffu, 0xffff'ffffu});
    auto dimensions = [](ThrowableShape shape) {
        switch (shape) {
            case ThrowableShape::Sphere: return glm::vec3(1.0f);
            case ThrowableShape::Capsule: return glm::vec3(0.6f, 1.4f, 0.6f);
            case ThrowableShape::Cylinder: return glm::vec3(1.0f);
            case ThrowableShape::Cube:
            case ThrowableShape::Box: return glm::vec3(1.0f);
            case ThrowableShape::Count: break;
        }
        return glm::vec3(1.0f);
    };
    for (uint32_t pairIndex = 0; pairIndex < overlapPairCount; ++pairIndex) {
        const uint32_t bodyA = pairIndex * 2u + 1u;
        const uint32_t bodyB = bodyA + 1u;
        const glm::vec3 base(0.0f, float(pairIndex) * 3.0f, 0.0f);
        poses[bodyA].positionInvMass = glm::vec4(base, 1.0f);
        poses[bodyB].positionInvMass = glm::vec4(
            base + glm::vec3(0.55f, 0.03f, 0.02f), 1.0f);
        const glm::quat rotation = glm::angleAxis(
            0.12f * float(pairIndex % 3u),
            glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        poses[bodyB].orientation = {
            rotation.x, rotation.y, rotation.z, rotation.w};
        shapes[bodyA].dimensionsType = glm::vec4(
            dimensions(pairShapes[pairIndex][0]),
            static_cast<float>(pairShapes[pairIndex][0]));
        shapes[bodyB].dimensionsType = glm::vec4(
            dimensions(pairShapes[pairIndex][1]),
            static_cast<float>(pairShapes[pairIndex][1]));
        pairs[pairIndex] = {bodyB, bodyA, 0u, pairIndex};
    }
    const uint32_t separatedA = overlapPairCount * 2u + 1u;
    const uint32_t separatedB = separatedA + 1u;
    poses[separatedA].positionInvMass = {0.0f, 40.0f, 0.0f, 1.0f};
    poses[separatedB].positionInvMass = {3.0f, 40.0f, 0.0f, 1.0f};
    shapes[separatedA].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
    shapes[separatedB].dimensionsType = {1.0f, 1.0f, 1.0f, 0.0f};
    pairs[overlapPairCount] = {
        separatedB, separatedA, 0u, overlapPairCount};
    std::array<uint32_t, 32> broadTelemetry{};
    broadTelemetry[3] = pairCount;

    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "narrow_poses");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(
        context, shapes, "narrow_shapes");
    const std::vector<glm::ivec4> metadata(bodyCapacity, glm::ivec4(0));
    WGPUBuffer metadataBuffer = makeStorage<glm::ivec4>(
        context, metadata, "narrow_metadata");
    WGPUBuffer pairBuffer = makeStorage<GpuKeyValue>(
        context, pairs, "narrow_pairs");
    WGPUBuffer broadTelemetryBuffer = makeStorage<uint32_t>(
        context, broadTelemetry, "narrow_broad_telemetry");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);
    ASSERT_NE(pairBuffer, nullptr);
    ASSERT_NE(broadTelemetryBuffer, nullptr);

    GpuNarrowPhase narrowPhase;
    GpuNarrowPhase::Config config;
    config.pairCapacity = pairCapacity;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(narrowPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    narrowPhase.setInput({poseBuffer, shapeBuffer, pairBuffer,
                          broadTelemetryBuffer, bodyCapacity, pairCapacity,
                          metadataBuffer});
    NarrowSnapshot first = runAndRead(context, narrowPhase, pairCount);
    ASSERT_EQ(first.manifolds.size(), pairCount);
    EXPECT_EQ(first.telemetry.inputPairs, pairCount);
    EXPECT_EQ(first.telemetry.manifolds, overlapPairCount);
    EXPECT_FALSE(first.telemetry.pairOverflow);
    EXPECT_EQ(first.telemetry.pairClasses[0], 2u);
    for (uint32_t pairClass = 1u; pairClass < kGpuNarrowPhasePairClassCount;
         ++pairClass) {
        EXPECT_EQ(first.telemetry.pairClasses[pairClass], 1u);
    }

    for (uint32_t pairIndex = 0; pairIndex < overlapPairCount; ++pairIndex) {
        auto& manifold = first.manifolds[pairIndex];
        ASSERT_GE(manifold.state[0], 1u) << "pair class " << pairIndex;
        EXPECT_LE(manifold.state[0], 4u);
        EXPECT_EQ(manifold.state[1], pairIndex);
        const glm::vec3 normal(
            manifold.normal[0], manifold.normal[1], manifold.normal[2]);
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
        EXPECT_NEAR(glm::length(normal), 1.0f, 2e-4f);
        const uint32_t bodyA = manifold.pair.keyHigh;
        const uint32_t bodyB = manifold.pair.keyLow;
        for (uint32_t pointIndex = 0; pointIndex < manifold.state[0];
             ++pointIndex) {
            auto& point = manifold.points[pointIndex];
            const glm::vec3 localA(point.localAnchorASeparation[0],
                                   point.localAnchorASeparation[1],
                                   point.localAnchorASeparation[2]);
            const glm::vec3 localB(point.localAnchorBNormalImpulse[0],
                                   point.localAnchorBNormalImpulse[1],
                                   point.localAnchorBNormalImpulse[2]);
            const glm::vec3 worldA = glm::vec3(poses[bodyA].positionInvMass)
                + orientation(poses[bodyA]) * localA;
            const glm::vec3 worldB = glm::vec3(poses[bodyB].positionInvMass)
                + orientation(poses[bodyB]) * localB;
            EXPECT_NEAR(glm::dot(worldB - worldA, normal),
                        point.localAnchorASeparation[3], 2e-3f);
            EXPECT_LE(point.localAnchorASeparation[3],
                      config.speculativeDistance + 1e-5f);
        }
        manifold.points[0].localAnchorBNormalImpulse[3] =
            0.25f + 0.01f * float(pairIndex);
        manifold.points[0].impulses[0] = 0.5f;
    }
    EXPECT_EQ(first.manifolds.back().state[0], 0u);

    gpu::writeBuffer(context.getQueue(), narrowPhase.manifolds(), 0,
        std::as_bytes(std::span<const GpuContactManifold>(first.manifolds)));
    for (uint32_t body = 1u; body < bodyCapacity; ++body) {
        poses[body].positionInvMass.x += 0.001f;
    }
    gpu::writeBuffer(context.getQueue(), poseBuffer, 0,
        std::as_bytes(std::span<const TestPose>(poses)));
    const NarrowSnapshot second = runAndRead(context, narrowPhase, pairCount);
    EXPECT_EQ(second.telemetry.tick, 2u);
    EXPECT_GT(second.telemetry.matchedFeaturePoints
            + second.telemetry.recycledAnchorPoints, 0u);
    for (uint32_t pairIndex = 0; pairIndex < overlapPairCount; ++pairIndex) {
        const auto& point = second.manifolds[pairIndex].points[0];
        EXPECT_GT(point.features[2], 0u);
        EXPECT_NEAR(point.localAnchorBNormalImpulse[3],
                    0.25f + 0.01f * float(pairIndex), 1e-6f);
        EXPECT_NEAR(point.impulses[0], 0.5f, 1e-6f);
    }

    releaseBuffer(broadTelemetryBuffer);
    releaseBuffer(pairBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

INSTANTIATE_TEST_SUITE_P(
    WorkgroupProfiles, GpuNarrowPhaseTest,
    ::testing::Values(64u, 128u, 256u));

TEST(GpuNarrowPhaseDifferentialTest, MatchesBox3dOnRandomBoundedPairs) {
    constexpr uint32_t casesPerClass = 10;
    constexpr uint32_t pairCount =
        kGpuNarrowPhasePairClassCount * casesPerClass;
    constexpr uint32_t pairCapacity = 128;
    constexpr uint32_t bodyCapacity = pairCount * 2u + 1u;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    constexpr std::array<std::array<ThrowableShape, 2>,
                         kGpuNarrowPhasePairClassCount> pairShapes{{
        {ThrowableShape::Sphere, ThrowableShape::Sphere},
        {ThrowableShape::Sphere, ThrowableShape::Capsule},
        {ThrowableShape::Capsule, ThrowableShape::Capsule},
        {ThrowableShape::Sphere, ThrowableShape::Box},
        {ThrowableShape::Capsule, ThrowableShape::Box},
        {ThrowableShape::Box, ThrowableShape::Box},
        {ThrowableShape::Sphere, ThrowableShape::Cylinder},
        {ThrowableShape::Capsule, ThrowableShape::Cylinder},
        {ThrowableShape::Box, ThrowableShape::Cylinder},
        {ThrowableShape::Cylinder, ThrowableShape::Cylinder},
    }};
    std::mt19937 random(0x6e61'7272u);
    std::uniform_real_distribution<float> size(0.7f, 1.4f);
    std::uniform_real_distribution<float> smallOffset(-0.12f, 0.12f);
    std::uniform_real_distribution<float> angle(-0.7f, 0.7f);
    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<GpuKeyValue> pairs(pairCapacity, {
        0xffff'ffffu, 0xffff'ffffu, 0xffff'ffffu, 0xffff'ffffu});
    std::vector<bool> expected(pairCount);
    for (uint32_t pairIndex = 0; pairIndex < pairCount; ++pairIndex) {
        const uint32_t pairClass = pairIndex / casesPerClass;
        const uint32_t localCase = pairIndex % casesPerClass;
        const uint32_t bodyA = pairIndex * 2u + 1u;
        const uint32_t bodyB = bodyA + 1u;
        const glm::vec3 dimensionsA(size(random), size(random), size(random));
        const glm::vec3 dimensionsB(size(random), size(random), size(random));
        const glm::vec3 base(0.0f, 5.0f * float(pairIndex), 0.0f);
        poses[bodyA].positionInvMass = glm::vec4(base, 1.0f);
        const bool shouldOverlap = (localCase & 1u) == 0u;
        poses[bodyB].positionInvMass = glm::vec4(
            base + (shouldOverlap
                ? glm::vec3(smallOffset(random), smallOffset(random),
                            smallOffset(random))
                : glm::vec3(4.0f + 0.2f * float(localCase),
                            smallOffset(random), smallOffset(random))),
            1.0f);
        const glm::quat rotationA = glm::angleAxis(
            angle(random), glm::normalize(glm::vec3(0.2f, 1.0f, 0.4f)));
        const glm::quat rotationB = glm::angleAxis(
            angle(random), glm::normalize(glm::vec3(0.7f, 0.3f, 1.0f)));
        poses[bodyA].orientation = {
            rotationA.x, rotationA.y, rotationA.z, rotationA.w};
        poses[bodyB].orientation = {
            rotationB.x, rotationB.y, rotationB.z, rotationB.w};
        const ThrowableShape shapeA = pairShapes[pairClass][0];
        const ThrowableShape shapeB = pairShapes[pairClass][1];
        shapes[bodyA].dimensionsType = glm::vec4(
            dimensionsA, static_cast<float>(shapeA));
        shapes[bodyB].dimensionsType = glm::vec4(
            dimensionsB, static_cast<float>(shapeB));
        pairs[pairIndex] = {bodyB, bodyA, 0u, pairIndex};
        expected[pairIndex] = box3dOverlaps(
            shapeA, dimensionsA, poses[bodyA],
            shapeB, dimensionsB, poses[bodyB]);
        EXPECT_EQ(expected[pairIndex], shouldOverlap);
    }
    std::array<uint32_t, 32> broadTelemetry{};
    broadTelemetry[3] = pairCount;
    WGPUBuffer poseBuffer = makeStorage<TestPose>(context, poses, "diff_poses");
    WGPUBuffer shapeBuffer = makeStorage<TestShape>(context, shapes, "diff_shapes");
    const std::vector<glm::ivec4> metadata(bodyCapacity, glm::ivec4(0));
    WGPUBuffer metadataBuffer = makeStorage<glm::ivec4>(
        context, metadata, "diff_metadata");
    WGPUBuffer pairBuffer = makeStorage<GpuKeyValue>(context, pairs, "diff_pairs");
    WGPUBuffer broadTelemetryBuffer = makeStorage<uint32_t>(
        context, broadTelemetry, "diff_broad_telemetry");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);
    ASSERT_NE(pairBuffer, nullptr);
    ASSERT_NE(broadTelemetryBuffer, nullptr);

    GpuNarrowPhase narrowPhase;
    GpuNarrowPhase::Config config;
    config.pairCapacity = pairCapacity;
    config.workgroupSize = 128;
    ASSERT_TRUE(narrowPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    narrowPhase.setInput({poseBuffer, shapeBuffer, pairBuffer,
                          broadTelemetryBuffer, bodyCapacity, pairCapacity,
                          metadataBuffer});
    const NarrowSnapshot snapshot = runAndRead(context, narrowPhase, pairCount);
    ASSERT_EQ(snapshot.manifolds.size(), pairCount);
    for (uint32_t pairIndex = 0; pairIndex < pairCount; ++pairIndex) {
        EXPECT_EQ(snapshot.manifolds[pairIndex].state[0] != 0u,
                  expected[pairIndex])
            << "pair " << pairIndex
            << " class " << pairIndex / casesPerClass;
    }
    EXPECT_EQ(snapshot.telemetry.manifolds,
              static_cast<uint32_t>(std::count(
                  expected.begin(), expected.end(), true)));

    releaseBuffer(broadTelemetryBuffer);
    releaseBuffer(pairBuffer);
    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

} // namespace
} // namespace voxy::physics
