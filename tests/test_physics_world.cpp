#include <gtest/gtest.h>

#include "physics/physics_world.hpp"

#include <cstdint>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kTerrainSize = 1024;

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
    const glm::vec3 start = bodies.front().position;

    for (int i = 0; i < 30; ++i) {
        world.update(1.0f / 60.0f);
    }
    bodies = world.dynamicBodies();
    ASSERT_EQ(bodies.size(), 1u);
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

} // namespace
} // namespace voxy::physics
