#include <gtest/gtest.h>

#include "physics/physics_world.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
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
}

class PhysicsWorldTest : public ::testing::Test {
protected:
    void SetUp() override {
        heights.assign(static_cast<size_t>(kTerrainSize) * kTerrainSize, 32768u);
        ASSERT_TRUE(world.initialize());
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

TEST_F(PhysicsWorldTest, InitializesWithStreamedTerrain) {
    EXPECT_TRUE(world.isInitialized());
    EXPECT_TRUE(world.hasTerrain());
}

TEST_F(PhysicsWorldTest, CharacterFallsAndSettlesOnTerrain) {
    PhysicsWorld::CharacterSettings settings;
    const auto character = world.createCharacter(glm::vec3(0.0f, 4.0f, 0.0f), settings);
    ASSERT_NE(character, PhysicsWorld::InvalidCharacter);

    const auto motion = settle(character);
    EXPECT_TRUE(motion.grounded);
    EXPECT_NEAR(motion.position.y, 32.0f / 65535.0f, 0.05f);
    EXPECT_NEAR(motion.groundNormal.y, 1.0f, 0.01f);
}

TEST_F(PhysicsWorldTest, CharacterMovesAndJumps) {
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

TEST_F(PhysicsWorldTest, TerrainTilesFollowTeleportedCharacter) {
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

TEST_F(PhysicsWorldTest, ThrownBodyMovesUnderJoltSimulation) {
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

TEST_F(PhysicsWorldTest, KeepsEachThrownShapeDistinct) {
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

TEST_F(PhysicsWorldTest, KeepsMoreThanSixtyFourBodies) {
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

TEST_F(PhysicsWorldTest, SupportsTenThousandDynamicBodies) {
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

TEST_F(PhysicsWorldTest, WaterAppliesBuoyancyAndDrag) {
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

TEST_F(PhysicsWorldTest, SamplesAnimatedWaterForBuoyancy) {
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

TEST_F(PhysicsWorldTest, PreservesWaterSamplerOrderAcrossBatchRead) {
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

TEST_F(PhysicsWorldTest, DryBodiesMatchWaterDisabledBitForBit) {
    PhysicsWorld waterDisabled;
    ASSERT_TRUE(waterDisabled.initialize());
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

} // namespace
} // namespace voxy::physics
