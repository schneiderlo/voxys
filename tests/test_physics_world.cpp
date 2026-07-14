#include <gtest/gtest.h>

#include "physics/physics_world.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kTerrainSize = 1024;

void expectSameFloatBits(float lhs, float rhs) {
    EXPECT_EQ(std::bit_cast<uint32_t>(lhs), std::bit_cast<uint32_t>(rhs));
}

void expectSameBodyState(const PhysicsWorld::DynamicBodySnapshot& lhs,
                         const PhysicsWorld::DynamicBodySnapshot& rhs) {
    EXPECT_EQ(lhs.shape, rhs.shape);
    EXPECT_EQ(lhs.sector, rhs.sector);
    expectSameFloatBits(lhs.position.x, rhs.position.x);
    expectSameFloatBits(lhs.position.y, rhs.position.y);
    expectSameFloatBits(lhs.position.z, rhs.position.z);
    expectSameFloatBits(lhs.rotation.x, rhs.rotation.x);
    expectSameFloatBits(lhs.rotation.y, rhs.rotation.y);
    expectSameFloatBits(lhs.rotation.z, rhs.rotation.z);
    expectSameFloatBits(lhs.rotation.w, rhs.rotation.w);
    expectSameFloatBits(lhs.dimensions.x, rhs.dimensions.x);
    expectSameFloatBits(lhs.dimensions.y, rhs.dimensions.y);
    expectSameFloatBits(lhs.dimensions.z, rhs.dimensions.z);
    EXPECT_EQ(lhs.active, rhs.active);
}

TEST(WorldPositionTest, CanonicalizesWithoutFarFloatConversion) {
    const WorldPosition position = canonicalWorldPosition(
        glm::ivec3(2'000'000, -2'000'000, 7),
        glm::dvec3(384.25, -384.5, 127.999));
    EXPECT_EQ(position.sector, glm::ivec3(2'000'002, -2'000'002, 7));
    EXPECT_FLOAT_EQ(position.local.x, -127.75f);
    EXPECT_FLOAT_EQ(position.local.y, 127.5f);
    EXPECT_NEAR(position.local.z, 127.999f, 1e-5f);
    EXPECT_TRUE(isValidWorldPosition(position));
}

TEST(WorldPositionTest, ConvertsOnlyNearbySectorsToHotFloatFrame) {
    const WorldPosition position{
        .sector = {1'500'001, -9, 4},
        .local = {-127.5f, 10.0f, 3.0f},
    };
    glm::vec3 relative;
    EXPECT_TRUE(worldPositionRelativeToSector(
        position, glm::ivec3(1'500'000, -9, 4), relative));
    EXPECT_EQ(relative, glm::vec3(128.5f, 10.0f, 3.0f));
    EXPECT_FALSE(worldPositionRelativeToSector(
        position, glm::ivec3(0), relative));
}

TEST(WorldPositionTest, RoundTripsLargeAbsoluteCoordinatesOnHost) {
    const glm::dvec3 absolute(
        512'000'127.75, -400'000'128.25, 42.125);
    const WorldPosition position = worldPositionFromAbsolute(absolute);
    const glm::dvec3 roundTrip = worldPositionToAbsolute(position);
    EXPECT_NEAR(roundTrip.x, absolute.x, 1e-5);
    EXPECT_NEAR(roundTrip.y, absolute.y, 1e-5);
    EXPECT_NEAR(roundTrip.z, absolute.z, 1e-5);
    EXPECT_TRUE(isValidWorldPosition(position));
}

class PhysicsWorldTest : public ::testing::TestWithParam<BackendType> {
protected:
    void SetUp() override {
        heights.assign(static_cast<size_t>(kTerrainSize) * kTerrainSize, 32768u);
        PhysicsInitContext context;
        context.requestedBackend = GetParam();
        ASSERT_TRUE(world.initialize(context));
        ASSERT_TRUE(world.setTerrain(heights, kTerrainSize, kTerrainSize, 32.0f, 1.0f));
    }

    PhysicsWorld::CharacterMotion settle(PhysicsWorld::CharacterHandle character) {
        PhysicsWorld::CharacterMotion motion;
        for (int i = 0; i < 240; ++i) {
            motion = world.moveCharacter(
                character, glm::vec3(0.0f), false, 6.0f, 20.0f, 50.0f,
                1.0f / 60.0f);
            if (motion.grounded) {
                break;
            }
        }
        return motion;
    }

    PhysicsWorld world;
    std::vector<uint16_t> heights;
};

TEST_P(PhysicsWorldTest, InitializesWithTerrain) {
    EXPECT_TRUE(world.isInitialized());
    EXPECT_TRUE(world.hasTerrain());
    EXPECT_EQ(world.backendType(), GetParam());
    EXPECT_TRUE(world.capabilities().synchronousCharacter);
    EXPECT_TRUE(world.capabilities().bodyBodyContacts);

    const PhysicsStats stats = world.stats();
    EXPECT_EQ(stats.backend, GetParam());
    EXPECT_EQ(stats.bodyCapacity, 16'384u);
    EXPECT_EQ(stats.pairCapacity, 65'536u);
    EXPECT_EQ(stats.contactCapacity, 16'384u);
    if (GetParam() == BackendType::JoltLegacy) {
        EXPECT_GT(stats.workerConcurrency, 1u);
    } else {
        EXPECT_EQ(stats.workerConcurrency, 1u);
    }
    EXPECT_GT(stats.estimatedPersistentBytes + stats.scratchBytes, 0u);
}

TEST(PhysicsWorldBackendConfigurationTest, RejectsUnavailableBackendExplicitly) {
    PhysicsWorld world;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::WebGpuSoft;
    EXPECT_FALSE(world.initialize(context));
    EXPECT_FALSE(world.isInitialized());
}

TEST(PhysicsWorldBackendConfigurationTest,
     FallsBackToBox3DOnlyWhenExplicitlyEnabled) {
    PhysicsWorld world;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::WebGpuSoft;
    context.allowCpuFallback = true;
    ASSERT_TRUE(world.initialize(context));
    EXPECT_TRUE(world.isInitialized());
    EXPECT_EQ(world.backendType(), BackendType::Box3DReference);
    EXPECT_TRUE(world.capabilities().synchronousCharacter);
    EXPECT_TRUE(world.capabilities().bodyBodyContacts);
    EXPECT_TRUE(world.capabilities().deterministicFloat);

    // Repeating the same requested configuration is idempotent even though
    // the selected backend is the configured fallback.
    EXPECT_TRUE(world.initialize(context));
}

#if !defined(__EMSCRIPTEN__)
TEST(PhysicsWorldBackendConfigurationTest, SupportsMultithreadedJoltBaseline) {
    PhysicsWorld world;
    PhysicsInitContext context;
    context.joltJobSystem = JoltJobSystemMode::ThreadPool;
    context.joltWorkerThreads = 1;
    ASSERT_TRUE(world.initialize(context));
    EXPECT_EQ(world.stats().workerConcurrency, 2u);
    ASSERT_TRUE(world.throwBody(
        PhysicsWorld::ThrowableShape::Sphere,
        glm::vec3(0.0f, 8.0f, 0.0f), glm::vec3(0.0f)));
    world.update(1.0f / 60.0f);
    EXPECT_EQ(world.dynamicBodies().size(), 1u);
}

TEST(PhysicsWorldBackendConfigurationTest, SupportsMultithreadedBox3DBaseline) {
    PhysicsWorld world;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::Box3DReference;
    context.box3dWorkerThreads = 2;
    ASSERT_TRUE(world.initialize(context));
    EXPECT_EQ(world.stats().workerConcurrency, 2u);
}
#endif

TEST_P(PhysicsWorldTest, CharacterFallsAndSettlesOnTerrain) {
    PhysicsWorld::CharacterSettings settings;
    const auto character = world.createCharacter(glm::vec3(0.0f, 4.0f, 0.0f), settings);
    ASSERT_NE(character, PhysicsWorld::InvalidCharacter);

    const auto motion = settle(character);
    EXPECT_TRUE(motion.grounded);
    EXPECT_NEAR(motion.position.y, 32.0f / 65535.0f, 0.05f);
    EXPECT_NEAR(motion.groundNormal.y, 1.0f, 0.01f);
}

TEST_P(PhysicsWorldTest, CharacterMovesAndJumps) {
    PhysicsWorld::CharacterSettings settings;
    const auto character = world.createCharacter(glm::vec3(0.0f, 2.0f, 0.0f), settings);
    ASSERT_NE(character, PhysicsWorld::InvalidCharacter);
    ASSERT_TRUE(settle(character).grounded);

    auto motion = world.moveCharacter(
        character, glm::vec3(4.0f, 0.0f, 0.0f), true, 7.0f, 20.0f,
        50.0f, 1.0f / 60.0f);
    EXPECT_GT(motion.position.x, 0.0f);
    EXPECT_GT(motion.velocity.y, 0.0f);
    EXPECT_FALSE(motion.grounded);
}

TEST_P(PhysicsWorldTest, TerrainSupportsTeleportedCharacter) {
    PhysicsWorld::CharacterSettings settings;
    const auto character = world.createCharacter(glm::vec3(-450.0f, 3.0f, 0.0f), settings);
    ASSERT_NE(character, PhysicsWorld::InvalidCharacter);
    ASSERT_TRUE(settle(character).grounded);

    ASSERT_TRUE(world.setCharacterPosition(
        character, glm::vec3(450.0f, 3.0f, 0.0f)));
    const auto motion = settle(character);
    EXPECT_TRUE(motion.grounded);
    EXPECT_NEAR(motion.position.x, 450.0f, 0.01f);
}

TEST_P(PhysicsWorldTest, ThrownBodyMovesUnderSimulation) {
    ASSERT_TRUE(world.throwBody(
        PhysicsWorld::ThrowableShape::Sphere,
        glm::vec3(0.0f, 8.0f, 0.0f),
        glm::vec3(5.0f, 2.0f, 0.0f)));
    auto bodies = world.dynamicBodies();
    ASSERT_EQ(bodies.size(), 1u);
    EXPECT_TRUE(bodies.front().active);
    const glm::vec3 start = bodies.front().position;

    for (int i = 0; i < 30; ++i) {
        world.update(1.0f / 60.0f);
    }
    bodies = world.dynamicBodies();
    ASSERT_EQ(bodies.size(), 1u);
    EXPECT_TRUE(bodies.front().active);
    EXPECT_GT(bodies.front().position.x, start.x + 1.0f);
    EXPECT_LT(bodies.front().position.y, start.y);
}

TEST(PhysicsWorldShapeTest, ThrowableNamesAreReadable) {
    EXPECT_STREQ(PhysicsWorld::throwableShapeName(
                     PhysicsWorld::ThrowableShape::Sphere), "ball");
    EXPECT_STREQ(PhysicsWorld::throwableShapeName(
                     PhysicsWorld::ThrowableShape::Capsule), "capsule");
}

TEST(PhysicsBackendTypeTest, NamesParseToStableBackendIdentifiers) {
    EXPECT_EQ(PhysicsInitContext{}.joltJobSystem,
              JoltJobSystemMode::ThreadPool);
    EXPECT_EQ(backendTypeFromName("jolt"), BackendType::JoltLegacy);
    EXPECT_EQ(backendTypeFromName("box3d"), BackendType::Box3DReference);
    EXPECT_EQ(backendTypeFromName("webgpu"), BackendType::WebGpuSoft);
    EXPECT_STREQ(backendTypeName(BackendType::WebGpuSoft), "webgpu_soft");
    EXPECT_EQ(joltJobSystemModeFromName("thread_pool"),
              JoltJobSystemMode::ThreadPool);
}

TEST_P(PhysicsWorldTest, KeepsEachThrownShapeDistinct) {
    constexpr uint32_t shapeCount = static_cast<uint32_t>(
        PhysicsWorld::ThrowableShape::Count);
    for (uint32_t index = 0; index < shapeCount; ++index) {
        const auto shape = static_cast<PhysicsWorld::ThrowableShape>(index);
        ASSERT_TRUE(world.throwBody(shape, glm::vec3(0.0f, 8.0f, 0.0f),
                                    glm::vec3(0.0f)));
    }

    const auto bodies = world.dynamicBodies();
    ASSERT_EQ(bodies.size(), shapeCount);
    for (uint32_t index = 0; index < shapeCount; ++index) {
        EXPECT_EQ(bodies[index].shape,
                  static_cast<PhysicsWorld::ThrowableShape>(index));
    }
}

TEST_P(PhysicsWorldTest, KeepsMoreThanSixtyFourBodies) {
    constexpr uint32_t bodyCount = 96;
    for (uint32_t index = 0; index < bodyCount; ++index) {
        ASSERT_TRUE(world.throwBody(
            PhysicsWorld::ThrowableShape::Sphere,
            glm::vec3(static_cast<float>(index % 12), 8.0f,
                      static_cast<float>(index / 12)),
            glm::vec3(0.0f)));
    }
    EXPECT_EQ(world.dynamicBodies().size(), bodyCount);
}

TEST_P(PhysicsWorldTest, ReusesOnlyContinuouslySleepingTransforms) {
    ASSERT_TRUE(world.throwBody(
        PhysicsWorld::ThrowableShape::Box,
        glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.0f)));

    PhysicsWorld::DynamicBodySnapshot firstSleep;
    bool settled = false;
    for (uint32_t frame = 0; frame < 900; ++frame) {
        world.update(1.0f / 60.0f);
        const auto bodies = world.dynamicBodies();
        ASSERT_EQ(bodies.size(), 1u);
        if (!bodies.front().active) {
            firstSleep = bodies.front();
            settled = true;
            break;
        }
    }
    ASSERT_TRUE(settled);
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 1u);
    EXPECT_EQ(world.lastDynamicBodyReadStats().cachedBodyCount, 0u);

    const auto cached = world.dynamicBodies();
    ASSERT_EQ(cached.size(), 1u);
    expectSameBodyState(cached.front(), firstSleep);
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 0u);
    EXPECT_EQ(world.lastDynamicBodyReadStats().cachedBodyCount, 1u);

    world.setWaterPlane(3.0f, true);
    world.setWaterSurfaceSampler([](glm::vec2, float) {
        return PhysicsWorld::WaterSurfaceSample{
            0.0f, glm::vec2(0.0f), glm::vec3(8.0f, 0.0f, 0.0f)};
    });
    for (uint32_t frame = 0; frame < 30; ++frame) {
        world.update(1.0f / 60.0f);
    }
    world.setWaterPlane(3.0f, false);
    world.setWaterSurfaceSampler({});
    for (uint32_t frame = 0; frame < 900; ++frame) {
        world.update(1.0f / 60.0f);
    }

    const auto resettled = world.dynamicBodies();
    ASSERT_EQ(resettled.size(), 1u);
    ASSERT_FALSE(resettled.front().active);
    EXPECT_GT(std::abs(resettled.front().position.x - firstSleep.position.x),
              0.01f);
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 1u);
    EXPECT_EQ(world.lastDynamicBodyReadStats().cachedBodyCount, 0u);

    const auto recached = world.dynamicBodies();
    ASSERT_EQ(recached.size(), 1u);
    expectSameBodyState(recached.front(), resettled.front());
    EXPECT_EQ(world.lastDynamicBodyReadStats().lockedBodyCount, 0u);
    EXPECT_EQ(world.lastDynamicBodyReadStats().cachedBodyCount, 1u);
}

TEST_P(PhysicsWorldTest, ReservesCallerOverlayCapacityWithoutGrowth) {
    constexpr size_t overlayCount = 36;
    for (uint32_t index = 0; index < 96; ++index) {
        ASSERT_TRUE(world.throwBody(
            PhysicsWorld::ThrowableShape::Sphere,
            glm::vec3(static_cast<float>(index), 8.0f, 0.0f),
            glm::vec3(0.0f)));
    }

    auto bodies = world.dynamicBodies(overlayCount);
    ASSERT_EQ(bodies.size(), 96u);
    ASSERT_GE(bodies.capacity(), bodies.size() + overlayCount);
    const auto* storage = bodies.data();
    for (size_t index = 0; index < overlayCount; ++index) {
        bodies.push_back({});
    }
    EXPECT_EQ(bodies.data(), storage);
}

TEST_P(PhysicsWorldTest, SupportsTenThousandDynamicBodies) {
    constexpr uint32_t columns = 100;
    constexpr float spacing = 2.1f;
    for (uint32_t index = 0; index < 10000; ++index) {
        const glm::vec3 position{
            static_cast<float>(index % columns) * spacing,
            300.0f,
            static_cast<float>(index / columns) * spacing};
        const auto shape = static_cast<PhysicsWorld::ThrowableShape>(
            index % static_cast<uint32_t>(PhysicsWorld::ThrowableShape::Count));
        ASSERT_TRUE(world.throwBody(shape, position, {0.0f, -1.0f, 0.0f}));
    }

    ASSERT_EQ(world.dynamicBodies().size(), 10000u);
    world.update(1.0f / 60.0f);
    EXPECT_EQ(world.dynamicBodies().size(), 10000u);
}

TEST_P(PhysicsWorldTest, WaterAppliesBuoyancyAndDrag) {
    world.setWaterPlane(5.0f);
    ASSERT_TRUE(world.throwBody(
        PhysicsWorld::ThrowableShape::Box,
        glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(4.0f, -2.0f, 0.0f)));

    for (int step = 0; step < 120; ++step) {
        world.update(1.0f / 60.0f);
    }

    const auto bodies = world.dynamicBodies();
    ASSERT_EQ(bodies.size(), 1u);
    EXPECT_GT(bodies.front().position.y, 3.5f);
    EXPECT_LT(bodies.front().position.x, 6.0f);
}

TEST_P(PhysicsWorldTest, SamplesAnimatedWaterForBuoyancy) {
    uint32_t sampleCount = 0;
    world.setWaterPlane(5.0f);
    world.setWaterSurfaceSampler(
        [&sampleCount](glm::vec2 position, float timeSeconds) {
            ++sampleCount;
            return PhysicsWorld::WaterSurfaceSample{
                std::sin(position.x * 0.1f + timeSeconds),
                glm::vec2(0.1f, -0.05f),
                glm::vec3(0.2f, 0.4f, 0.0f)};
        });
    ASSERT_TRUE(world.throwBody(
        PhysicsWorld::ThrowableShape::Sphere,
        glm::vec3(0.0f, 4.5f, 0.0f), glm::vec3(0.0f)));

    world.update(1.0f / 60.0f);
    EXPECT_GT(sampleCount, 0u);
}

TEST_P(PhysicsWorldTest, PreservesWaterSamplerOrderAcrossBatchRead) {
    constexpr uint32_t bodyCount = 17;
    std::vector<glm::vec2> sampledPositions;
    sampledPositions.reserve(bodyCount);
    world.setWaterPlane(5.0f);
    world.setWaterSurfaceSampler(
        [&sampledPositions](glm::vec2 position, float) {
            sampledPositions.push_back(position);
            return PhysicsWorld::WaterSurfaceSample{};
        });

    for (uint32_t index = 0; index < bodyCount; ++index) {
        ASSERT_TRUE(world.throwBody(
            PhysicsWorld::ThrowableShape::Sphere,
            glm::vec3(static_cast<float>(index) * 3.0f, 4.5f, 0.0f),
            glm::vec3(0.0f)));
    }

    world.update(1.0f / 60.0f);

    ASSERT_EQ(sampledPositions.size(), bodyCount);
    for (uint32_t index = 0; index < bodyCount; ++index) {
        EXPECT_FLOAT_EQ(sampledPositions[index].x,
                        static_cast<float>(index) * 3.0f);
        EXPECT_FLOAT_EQ(sampledPositions[index].y, 0.0f);
    }
}

TEST_P(PhysicsWorldTest, DryBodiesMatchWaterDisabledBitForBit) {
    PhysicsWorld waterDisabled;
    PhysicsInitContext context;
    context.requestedBackend = GetParam();
    ASSERT_TRUE(waterDisabled.initialize(context));
    ASSERT_TRUE(waterDisabled.setTerrain(
        heights, kTerrainSize, kTerrainSize, 32.0f, 1.0f));

    world.setWaterPlane(5.0f, true);
    waterDisabled.setWaterPlane(5.0f, false);
    constexpr uint32_t shapeCount = static_cast<uint32_t>(
        PhysicsWorld::ThrowableShape::Count);
    for (uint32_t index = 0; index < shapeCount; ++index) {
        const auto shape = static_cast<PhysicsWorld::ThrowableShape>(index);
        const glm::vec3 position{static_cast<float>(index) * 3.0f, 13.0f, 0.0f};
        const glm::vec3 velocity{2.0f, -1.0f, 0.5f};
        ASSERT_TRUE(world.throwBody(shape, position, velocity));
        ASSERT_TRUE(waterDisabled.throwBody(shape, position, velocity));
    }

    world.update(1.0f / 60.0f);
    waterDisabled.update(1.0f / 60.0f);
    const auto withWater = world.dynamicBodies();
    const auto withoutWater = waterDisabled.dynamicBodies();
    ASSERT_EQ(withWater.size(), withoutWater.size());
    for (size_t index = 0; index < withWater.size(); ++index) {
        expectSameBodyState(withWater[index], withoutWater[index]);
    }
}

TEST(Box3DReferenceBackendTest, PreservesCanonicalTerrainDiagonal) {
    PhysicsWorld world;
    PhysicsInitContext context;
    context.requestedBackend = BackendType::Box3DReference;
    ASSERT_TRUE(world.initialize(context));

    // Only the central cell's BR is high. At that cell's center the TL-BR
    // diagonal is exactly y=0; Box3D's unadapted diagonal would be y=-0.2.
    std::array<uint16_t, 16> heights{};
    heights[2u * 4u + 2u] = 65'535u;
    ASSERT_TRUE(world.setTerrain(heights, 4, 4, 0.2f, 1.0f));
    const auto character = world.createCharacter(
        glm::vec3(0.0f, 2.0f, 0.0f), {});
    ASSERT_NE(character, PhysicsWorld::InvalidCharacter);

    PhysicsWorld::CharacterMotion motion;
    for (int step = 0; step < 240 && !motion.grounded; ++step) {
        motion = world.moveCharacter(
            character, glm::vec3(0.0f), false, 6.0f, 20.0f, 50.0f,
            1.0f / 60.0f);
    }
    ASSERT_TRUE(motion.grounded);
    EXPECT_NEAR(motion.position.y, 0.0f, 0.02f);
}

std::string backendTestName(
    const ::testing::TestParamInfo<BackendType>& info) {
    switch (info.param) {
        case BackendType::JoltLegacy: return "JoltLegacy";
        case BackendType::Box3DReference: return "Box3DReference";
        case BackendType::WebGpuSoft: return "WebGpuSoft";
    }
    return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    CpuBackends, PhysicsWorldTest,
    ::testing::Values(BackendType::JoltLegacy,
                      BackendType::Box3DReference),
    backendTestName);

} // namespace
} // namespace voxy::physics
