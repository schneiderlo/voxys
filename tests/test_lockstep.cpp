#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/deterministic/fixed.hpp"
#include "physics/deterministic/lockstep_world.hpp"
#include "physics/gpu/gpu_lockstep.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
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

namespace voxy::physics::deterministic {
namespace {

int32_t q12(float value) {
    return Position::fromDouble(value).raw();
}

int32_t q16(float value) {
    return LinearVelocity::fromDouble(value).raw();
}

LockstepBody body(uint32_t id, float x, float y, float z, float radius,
                  float vx = 0.0f, float vy = 0.0f, float vz = 0.0f,
                  bool isStatic = false) {
    LockstepBody result;
    result.identity = {
        id, 1u, LockstepBodyAlive | LockstepBodyAwake
            | (isStatic ? LockstepBodyStatic : 0u), 0u};
    result.sectorRadius = {0, 0, 0, q12(radius)};
    result.positionInvMass = {q12(x), q12(y), q12(z),
                              isStatic ? 0 : q16(1.0f)};
    result.linearVelocity = {q16(vx), q16(vy), q16(vz), 0};
    return result;
}

int32_t saturateI32(int64_t value) {
    return static_cast<int32_t>(std::clamp(
        value, int64_t{std::numeric_limits<int32_t>::min()},
        int64_t{std::numeric_limits<int32_t>::max()}));
}

uint32_t magnitudeI32(int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    return value < 0 ? 0u - bits : bits;
}

bool bruteForceContact(const LockstepBody& a, const LockstepBody& b) {
    const auto isAlive = [](const LockstepBody& value) {
        return (value.identity[2] & LockstepBodyAlive) != 0u;
    };
    const auto isDynamic = [&](const LockstepBody& value) {
        return isAlive(value)
            && (value.identity[2] & LockstepBodyStatic) == 0u
            && value.positionInvMass[3] > 0;
    };
    if (!isAlive(a) || !isAlive(b) || (!isDynamic(a) && !isDynamic(b))) {
        return false;
    }

    uint64_t squaredDistance = 0;
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        const int64_t sectorDelta = int64_t{b.sectorRadius[axis]}
                                  - a.sectorRadius[axis];
        if (sectorDelta < -1 || sectorDelta > 1) return false;
        const int32_t delta = saturateI32(
            sectorDelta * kLockstepSectorSize
            + int64_t{b.positionInvMass[axis]}
            - a.positionInvMass[axis]);
        const uint64_t component = magnitudeI32(delta);
        squaredDistance += component * component;
    }
    const int32_t radiusSum = saturateI32(
        int64_t{a.sectorRadius[3]} + b.sectorRadius[3]);
    if (radiusSum <= 0) return false;
    const uint64_t radius = static_cast<uint32_t>(radiusSum);
    return squaredDistance < radius * radius;
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

TEST(FixedPoint, SaturatesRoundsAndUsesIntegerSquareRoot) {
    using Q = Fixed32<16>;
    EXPECT_EQ(Q::fromInteger(-2).raw(), q16(-2.0f));
    EXPECT_EQ((Q::fromDouble(1.5) * Q::fromDouble(2.0)).raw(), q16(3.0f));
    EXPECT_EQ((Q::fromDouble(-1.5) * Q::fromDouble(2.0)).raw(), q16(-3.0f));
    EXPECT_NEAR((Q::fromDouble(1.0) / Q::fromDouble(3.0)).toDouble(),
                1.0 / 3.0, 2e-5);
    EXPECT_EQ((Q::fromRaw(std::numeric_limits<int32_t>::max())
               + Q::fromRaw(1)).raw(), std::numeric_limits<int32_t>::max());
    EXPECT_EQ((Q::fromRaw(std::numeric_limits<int32_t>::min())
               / Q::fromRaw(-1)).raw(),
              std::numeric_limits<int32_t>::max());
    EXPECT_EQ(lockstepMultiplyShift(q16(3.0f), kLockstepUnitOne, 30u),
              q16(3.0f));
    EXPECT_EQ(lockstepMultiplyShift(q16(-3.0f), kLockstepUnitOne, 31u),
              q16(-1.5f));
    for (uint64_t value : {uint64_t{0}, uint64_t{1}, uint64_t{2},
                           uint64_t{15}, uint64_t{16}, uint64_t{17},
                           uint64_t{1} << 32u,
                           std::numeric_limits<uint64_t>::max()}) {
        const uint32_t root = lockstepIntegerSquareRoot(value);
        EXPECT_LE(uint64_t{root} * root, value);
        if (root != std::numeric_limits<uint32_t>::max()) {
            EXPECT_GT(uint64_t{root + 1u} * (root + 1u), value);
        }
    }
}

TEST(Lockstep, RejectedBodyUploadPreservesWorkingState) {
    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = 4;
    config.contactCapacity = 4;
    ASSERT_TRUE(world.initialize(config));

    std::array<LockstepBody, 4> accepted{};
    accepted[1] = body(1u, 1.0f, 2.0f, 3.0f, 0.5f);
    ASSERT_TRUE(world.setBodies(accepted));

    auto invalid = accepted;
    invalid[1].identity[0] = 2u;
    EXPECT_FALSE(world.setBodies(invalid));
    EXPECT_EQ(std::memcmp(
                  world.bodies().data(), accepted.data(), sizeof(accepted)),
              0);

    invalid = accepted;
    invalid[1].sectorRadius[3] = 0;
    EXPECT_FALSE(world.setBodies(invalid));
    EXPECT_EQ(std::memcmp(
                  world.bodies().data(), accepted.data(), sizeof(accepted)),
              0);

    ASSERT_TRUE(world.setBodies(world.bodies()));
    EXPECT_EQ(std::memcmp(
                  world.bodies().data(), accepted.data(), sizeof(accepted)),
              0);

    LockstepWorld::Config invalidConfig = config;
    invalidConfig.substeps = 0;
    EXPECT_FALSE(world.initialize(invalidConfig));
    EXPECT_EQ(std::memcmp(
                  world.bodies().data(), accepted.data(), sizeof(accepted)),
              0);
}

TEST(Lockstep, ExtremeSectorsDoNotAliasAndSaturateCanonically) {
    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = 4;
    config.contactCapacity = 4;
    config.substeps = 1;
    config.solverIterations = 1;
    config.gravityPerSubstepQ16 = 0;
    ASSERT_TRUE(world.initialize(config));

    std::array<LockstepBody, 4> distant{};
    distant[1] = body(1u, 0.0f, 0.0f, 0.0f, 0.5f);
    distant[2] = body(2u, 0.0f, 0.0f, 0.0f, 0.5f);
    distant[1].sectorRadius[0] = std::numeric_limits<int32_t>::min();
    distant[2].sectorRadius[0] = std::numeric_limits<int32_t>::max();
    ASSERT_TRUE(world.setBodies(distant));
    EXPECT_EQ(world.step(1u).contacts, 0u);

    std::array<LockstepBody, 4> adjacent{};
    adjacent[1] = body(1u, 127.75f, 0.0f, 0.0f, 0.5f);
    adjacent[2] = body(2u, -127.75f, 0.0f, 0.0f, 0.5f);
    adjacent[1].sectorRadius[0] = -1'500'000;
    adjacent[2].sectorRadius[0] = -1'499'999;
    ASSERT_TRUE(world.setBodies(adjacent));
    // The solver resolves the overlap during the substep, so the final pair
    // set is empty. The symmetric position correction proves the adjacent
    // large-sector pair was processed.
    EXPECT_EQ(world.step(2u).contacts, 0u);
    EXPECT_LT(world.bodies()[1].positionInvMass[0], q12(127.75f));
    EXPECT_GT(world.bodies()[2].positionInvMass[0], q12(-127.75f));

    std::array<LockstepBody, 4> saturated{};
    saturated[1] = body(
        1u, 127.9f, 20.0f, 0.0f, 0.5f, 1000.0f, 0.0f, 0.0f);
    saturated[2] = body(
        2u, -127.9f, -20.0f, 0.0f, 0.5f, -1000.0f, 0.0f, 0.0f);
    saturated[1].sectorRadius[0] = std::numeric_limits<int32_t>::max();
    saturated[2].sectorRadius[0] = std::numeric_limits<int32_t>::min();
    ASSERT_TRUE(world.setBodies(saturated));
    static_cast<void>(world.step(3u));
    EXPECT_EQ(world.bodies()[1].sectorRadius[0],
              std::numeric_limits<int32_t>::max());
    EXPECT_EQ(world.bodies()[1].positionInvMass[0],
              kLockstepSectorHalf - 1);
    EXPECT_EQ(world.bodies()[2].sectorRadius[0],
              std::numeric_limits<int32_t>::min());
    EXPECT_EQ(world.bodies()[2].positionInvMass[0],
              -kLockstepSectorHalf);
}

TEST(Lockstep, SweepAndPruneMatchesBruteForceWithSparseSlotsAndOverflow) {
    constexpr uint32_t bodyCapacity = 4'096;
    constexpr uint32_t contactCapacity = 32;
    constexpr uint32_t liveBodies = 192;
    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = bodyCapacity;
    config.contactCapacity = contactCapacity;
    config.substeps = 1;
    config.solverIterations = 1;
    config.gravityPerSubstepQ16 = 0;
    ASSERT_TRUE(world.initialize(config));

    std::vector<LockstepBody> initial(bodyCapacity);
    for (uint32_t ordinal = 0; ordinal < liveBodies; ++ordinal) {
        const uint32_t id = 1u + ordinal * 20u;
        const float x = static_cast<float>(ordinal % 16u) * 0.70f;
        const float y = static_cast<float>((ordinal / 16u) % 4u) * 0.70f;
        const float z = static_cast<float>(ordinal / 64u) * 0.70f;
        initial[id] = body(
            id, x, y, z, 0.45f, 0.0f, 0.0f, 0.0f,
            ordinal % 9u == 0u);
        initial[id].identity[2] &= ~LockstepBodyAwake;
    }
    ASSERT_TRUE(world.setBodies(initial));

    const LockstepTelemetry telemetry = world.step(1u);
    std::vector<std::array<uint32_t, 2>> expected;
    for (uint32_t bodyA = 0; bodyA < bodyCapacity; ++bodyA) {
        for (uint32_t bodyB = bodyA + 1u; bodyB < bodyCapacity; ++bodyB) {
            if (bruteForceContact(
                    world.bodies()[bodyA], world.bodies()[bodyB])) {
                expected.push_back({bodyA, bodyB});
            }
        }
    }

    const size_t retained = std::min<size_t>(
        expected.size(), contactCapacity);
    ASSERT_EQ(world.contacts().size(), retained);
    EXPECT_EQ(telemetry.contacts, retained);
    EXPECT_EQ(telemetry.contactOverflow,
              expected.size() > contactCapacity);
    for (size_t index = 0; index < retained; ++index) {
        EXPECT_EQ(world.contacts()[index].ids[0], expected[index][0]);
        EXPECT_EQ(world.contacts()[index].ids[1], expected[index][1]);
    }
}

TEST(Lockstep, SweepAndPruneHandlesTenThousandSeparatedBodies) {
    constexpr uint32_t liveBodies = 10'000;
    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = liveBodies + 1u;
    config.contactCapacity = 1u;
    config.solverIterations = 1u;
    config.gravityPerSubstepQ16 = 0;
    ASSERT_TRUE(world.initialize(config));

    std::vector<LockstepBody> initial(config.bodyCapacity);
    for (uint32_t id = 1u; id <= liveBodies; ++id) {
        initial[id] = body(id, 0.0f, 0.0f, 0.0f, 0.5f);
        initial[id].sectorRadius[0] = static_cast<int32_t>(id * 2u);
        initial[id].identity[2] &= ~LockstepBodyAwake;
    }
    ASSERT_TRUE(world.setBodies(initial));

    const LockstepTelemetry telemetry = world.step(1u);

    EXPECT_EQ(telemetry.liveBodies, liveBodies);
    EXPECT_EQ(telemetry.contacts, 0u);
    EXPECT_FALSE(telemetry.contactOverflow);
}

TEST(Lockstep, CpuAndWgslProduceIdenticalStateTopologySolverAndHashes) {
    constexpr uint32_t bodyCapacity = 16;
    constexpr uint32_t contactCapacity = 32;
    constexpr uint32_t ticks = 20;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        if (const char* required = std::getenv("VOXY_REQUIRE_WEBGPU");
            required != nullptr && std::string_view(required) == "1") {
            FAIL() << "VOXY_REQUIRE_WEBGPU=1 but headless WebGPU is unavailable";
        }
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<LockstepBody> initial(bodyCapacity);
    initial[1] = body(1u, -0.40f, 1.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f);
    initial[2] = body(2u, 0.40f, 1.0f, 0.0f, 0.5f, -1.0f, 0.0f, 0.0f);
    initial[3] = body(3u, 0.0f, 2.2f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f);
    initial[4] = body(4u, 0.0f, 0.0f, 0.0f, 0.5f,
                      0.0f, 0.0f, 0.0f, true);
    initial[5] = body(5u, 3.0f, 1.0f, 0.0f, 0.6f,
                      -0.25f, 0.0f, 0.0f);
    initial[6] = body(6u, 127.75f, 10.0f, 0.0f, 0.5f);
    initial[7] = body(7u, -127.75f, 10.0f, 0.0f, 0.5f);
    initial[6].sectorRadius[0] = 1'500'000;
    initial[7].sectorRadius[0] = 1'500'001;
    initial[9] = body(9u, 0.0f, 30.0f, 0.0f, 0.5f);
    initial[10] = body(10u, 0.0f, 30.0f, 0.0f, 0.5f);
    initial[9].sectorRadius[0] = std::numeric_limits<int32_t>::min();
    initial[10].sectorRadius[0] = std::numeric_limits<int32_t>::max();
    initial[11] = body(
        11u, 127.9f, 50.0f, 0.0f, 0.5f, 1000.0f, 0.0f, 0.0f);
    initial[12] = body(
        12u, -127.9f, -50.0f, 0.0f, 0.5f, -1000.0f, 0.0f, 0.0f);
    initial[11].sectorRadius[0] = std::numeric_limits<int32_t>::max();
    initial[12].sectorRadius[0] = std::numeric_limits<int32_t>::min();

    LockstepWorld cpu;
    LockstepWorld::Config cpuConfig;
    cpuConfig.bodyCapacity = bodyCapacity;
    cpuConfig.contactCapacity = contactCapacity;
    ASSERT_TRUE(cpu.initialize(cpuConfig));
    ASSERT_TRUE(cpu.setBodies(initial));

    GpuLockstepWorld gpuWorld;
    GpuLockstepWorld::Config gpuConfig;
    gpuConfig.bodyCapacity = bodyCapacity;
    gpuConfig.contactCapacity = contactCapacity;
    ASSERT_TRUE(gpuWorld.initialize(
        context.getDevice(), context.getQueue(), gpuConfig));
    const WGPUBuffer workingBodyBuffer = gpuWorld.bodyBuffer();
    auto invalidGpuConfig = gpuConfig;
    invalidGpuConfig.bodyCapacity = 0u;
    EXPECT_FALSE(gpuWorld.initialize(
        context.getDevice(), context.getQueue(), invalidGpuConfig));
    EXPECT_EQ(gpuWorld.bodyBuffer(), workingBodyBuffer);
    ASSERT_TRUE(gpuWorld.uploadBodies(initial));

    LockstepTelemetry cpuTelemetry;
    WGPUSubmissionIndex finalSubmissionIndex = 0;
    for (uint32_t tick = 1; tick <= ticks; ++tick) {
        cpuTelemetry = cpu.step(tick);
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDesc);
        ASSERT_TRUE(gpuWorld.encode(encoder, tick));
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(
            encoder, &commandDesc);
        finalSubmissionIndex = wgpuQueueSubmitForIndex(
            context.getQueue(), 1, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
    }

    const size_t bodyBytes = bodyCapacity * sizeof(LockstepBody);
    const size_t contactBytes = contactCapacity * sizeof(LockstepContact);
    const size_t rootBytes = bodyCapacity * sizeof(uint32_t);
    const size_t bodyHashBytes = bodyCapacity * sizeof(uint32_t);
    const size_t contactHashBytes = contactCapacity * sizeof(uint32_t);
    const size_t islandHashBytes = bodyCapacity * sizeof(uint32_t);
    constexpr size_t telemetryBytes = 32u * sizeof(uint32_t);
    const size_t totalBytes = bodyBytes + contactBytes + rootBytes
        + bodyHashBytes + contactHashBytes + islandHashBytes + telemetryBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "lockstep_test_readback",
            .size = totalBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    size_t offset = 0;
    auto copy = [&](WGPUBuffer source, size_t bytes) {
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, source, 0, readback, offset, bytes);
        offset += bytes;
    };
    copy(gpuWorld.bodyBuffer(), bodyBytes);
    copy(gpuWorld.contactBuffer(), contactBytes);
    copy(gpuWorld.rootBuffer(), rootBytes);
    copy(gpuWorld.bodyHashBuffer(), bodyHashBytes);
    copy(gpuWorld.contactHashBuffer(), contactHashBytes);
    copy(gpuWorld.islandHashBuffer(), islandHashBytes);
    copy(gpuWorld.telemetryBuffer(), telemetryBytes);
    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    finalSubmissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), finalSubmissionIndex};
    struct MapState { bool done = false; bool success = false; } state;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& map = *static_cast<MapState*>(userdata);
        map.success = status == WGPUBufferMapAsyncStatus_Success;
        map.done = true;
    };
    wgpuBufferMapAsync(
        readback, WGPUMapMode_Read, 0, totalBytes, callback, &state);
    while (!state.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(state.success);
    const auto* bytes = static_cast<const std::byte*>(
        wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
    offset = 0;
    EXPECT_EQ(std::memcmp(bytes + offset, cpu.bodies().data(), bodyBytes), 0);
    offset += bodyBytes;
    ASSERT_EQ(cpuTelemetry.contacts, cpu.contacts().size());
    EXPECT_EQ(std::memcmp(bytes + offset, cpu.contacts().data(),
                          cpu.contacts().size_bytes()), 0);
    offset += contactBytes;
    EXPECT_EQ(std::memcmp(bytes + offset, cpu.islandRoots().data(), rootBytes), 0);
    offset += rootBytes;
    EXPECT_EQ(std::memcmp(bytes + offset, cpuTelemetry.hashes.bodies.data(),
                          bodyHashBytes), 0);
    offset += bodyHashBytes;
    EXPECT_EQ(std::memcmp(bytes + offset, cpuTelemetry.hashes.contacts.data(),
                          contactHashBytes), 0);
    offset += contactHashBytes;
    EXPECT_EQ(std::memcmp(bytes + offset, cpuTelemetry.hashes.islands.data(),
                          islandHashBytes), 0);
    offset += islandHashBytes;
    std::array<uint32_t, 32> telemetryWords{};
    std::memcpy(telemetryWords.data(), bytes + offset, telemetryBytes);
    const LockstepTelemetry gpuTelemetry =
        GpuLockstepWorld::decodeTelemetry(telemetryWords);
    EXPECT_EQ(gpuTelemetry.liveBodies, cpuTelemetry.liveBodies);
    EXPECT_EQ(gpuTelemetry.contacts, cpuTelemetry.contacts);
    EXPECT_EQ(gpuTelemetry.contactOverflow, cpuTelemetry.contactOverflow);
    EXPECT_EQ(gpuTelemetry.tick, cpuTelemetry.tick);
    EXPECT_EQ(gpuTelemetry.hashes.world, cpuTelemetry.hashes.world);
    EXPECT_EQ(gpuTelemetry.hashes.bodyAggregate,
              cpuTelemetry.hashes.bodyAggregate);
    EXPECT_EQ(gpuTelemetry.hashes.contactAggregate,
              cpuTelemetry.hashes.contactAggregate);
    EXPECT_EQ(gpuTelemetry.hashes.islandAggregate,
              cpuTelemetry.hashes.islandAggregate);
    EXPECT_GT(gpuWorld.allocatedBytes(), 0u);

    wgpuBufferUnmap(readback);
    releaseBuffer(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    gpuWorld.shutdown();
}

} // namespace
} // namespace voxy::physics::deterministic
