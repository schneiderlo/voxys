#include <gtest/gtest.h>

#include "physics/character/cpu_capsule_mover.hpp"
#include "physics/terrain_topology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace voxy::physics {
namespace {

uint16_t rawHeight(float worldHeight, float scale) {
    const float normalized = std::clamp(
        0.5f * (worldHeight / scale + 1.0f), 0.0f, 1.0f);
    return static_cast<uint16_t>(std::lround(normalized * 65'535.0f));
}

TEST(CpuCapsuleMover, UsesCanonicalDiagonalAndTerrainOnlyPolicy) {
    CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    constexpr float scale = 10.0f;
    const std::vector<uint16_t> samples = {
        rawHeight(0.0f, scale), rawHeight(2.0f, scale),
        rawHeight(4.0f, scale), rawHeight(8.0f, scale),
    };
    ASSERT_TRUE(mover.setTerrain(samples, 2, 2, scale, 1.0f));

    const auto first = mover.sampleTerrain(-0.25f, 0.25f);
    ASSERT_TRUE(first.valid);
    const float tl = terrain_topology::worldHeight(samples[0], scale);
    const float tr = terrain_topology::worldHeight(samples[1], scale);
    const float bl = terrain_topology::worldHeight(samples[2], scale);
    const float br = terrain_topology::worldHeight(samples[3], scale);
    const float expectedFirst = tl + 0.25f * (br - bl)
                              + 0.75f * (bl - tl);
    EXPECT_NEAR(first.height, expectedFirst, 1e-5f);
    EXPECT_EQ(first.featureId, 0u);

    const auto second = mover.sampleTerrain(0.25f, -0.25f);
    ASSERT_TRUE(second.valid);
    const float expectedSecond = tl + 0.75f * (tr - tl)
                               + 0.25f * (br - tr);
    EXPECT_NEAR(second.height, expectedSecond, 1e-5f);
    EXPECT_EQ(second.featureId, 1u);
    EXPECT_EQ(mover.nearbyDynamicPolicy(),
              NearbyDynamicBodyPolicy::TerrainOnly);
}

TEST(CpuCapsuleMover, LandsStepsJumpsAndReplaysExactly) {
    constexpr uint32_t extent = 16;
    constexpr float heightScale = 10.0f;
    std::vector<uint16_t> samples(extent * extent);
    for (uint32_t z = 0; z < extent; ++z) {
        for (uint32_t x = 0; x < extent; ++x) {
            // A walkable 1:4 ramp exercises capsule footprint and step-up/down.
            const float height = x >= 8u ? 0.25f * float(x - 7u) : 0.0f;
            samples[size_t{z} * extent + x] = rawHeight(height, heightScale);
        }
    }

    CpuCapsuleMoverWorld firstWorld;
    CpuCapsuleMoverWorld secondWorld;
    ASSERT_TRUE(firstWorld.initialize());
    ASSERT_TRUE(secondWorld.initialize());
    ASSERT_TRUE(firstWorld.setTerrain(
        samples, extent, extent, heightScale, 1.0f));
    ASSERT_TRUE(secondWorld.setTerrain(
        samples, extent, extent, heightScale, 1.0f));
    CharacterSettings settings;
    settings.radius = 0.4f;
    settings.height = 1.8f;
    settings.maxSlopeAngleDegrees = 45.0f;
    settings.stepUp = 0.5f;
    settings.stepDown = 0.5f;
    const glm::vec3 start(-3.0f, 3.0f, 0.0f);
    const CharacterHandle first = firstWorld.createCharacter(start, settings);
    const CharacterHandle second = secondWorld.createCharacter(start, settings);
    ASSERT_NE(first, InvalidCharacter);
    ASSERT_EQ(first, second);

    CharacterMotion a;
    CharacterMotion b;
    for (uint32_t tick = 0; tick < 180; ++tick) {
        const glm::vec3 desired = tick < 120u
            ? glm::vec3(3.0f, 0.0f, 0.0f) : glm::vec3(0.0f);
        const bool jump = tick == 130u;
        a = firstWorld.moveCharacter(
            first, desired, jump, 6.0f, 20.0f, 50.0f, 1.0f / 60.0f);
        b = secondWorld.moveCharacter(
            second, desired, jump, 6.0f, 20.0f, 50.0f, 1.0f / 60.0f);
        EXPECT_EQ(a.position, b.position);
        EXPECT_EQ(a.velocity, b.velocity);
        EXPECT_EQ(a.grounded, b.grounded);
        EXPECT_EQ(a.onSteepGround, b.onSteepGround);
    }
    EXPECT_GT(a.position.x, start.x + 4.0f);
    EXPECT_GT(a.position.y, 0.1f);
    EXPECT_FALSE(a.onSteepGround);

    ASSERT_TRUE(firstWorld.setCharacterPosition(first, {0.0f, 2.0f, 0.0f}));
    for (uint32_t tick = 0; tick < 120; ++tick) {
        a = firstWorld.moveCharacter(
            first, glm::vec3(0.0f), false, 6.0f, 20.0f, 50.0f,
            1.0f / 60.0f);
    }
    EXPECT_TRUE(a.grounded);
    const auto ground = firstWorld.sampleTerrain(a.position.x, a.position.z);
    ASSERT_TRUE(ground.valid);
    EXPECT_NEAR(a.position.y, ground.height, 0.06f);
}

TEST(CpuCapsuleMover, RejectsSteepUpslopeAsAContactPlane) {
    constexpr uint32_t extent = 8;
    constexpr float heightScale = 10.0f;
    std::vector<uint16_t> samples(extent * extent);
    for (uint32_t z = 0; z < extent; ++z) {
        for (uint32_t x = 0; x < extent; ++x) {
            const float height = -7.0f + 2.0f * float(x);
            samples[size_t{z} * extent + x] = rawHeight(height, heightScale);
        }
    }
    CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    ASSERT_TRUE(mover.setTerrain(
        samples, extent, extent, heightScale, 1.0f));
    CharacterSettings settings;
    settings.maxSlopeAngleDegrees = 45.0f;
    const auto surface = mover.sampleTerrain(-2.0f, 0.0f);
    ASSERT_TRUE(surface.valid);
    const CharacterHandle character = mover.createCharacter(
        {-2.0f, surface.height, 0.0f}, settings);
    ASSERT_NE(character, InvalidCharacter);
    CharacterMotion motion;
    for (uint32_t tick = 0; tick < 1; ++tick) {
        motion = mover.moveCharacter(
            character, {4.0f, 0.0f, 0.0f}, false, 6.0f,
            20.0f, 50.0f, 1.0f / 60.0f);
    }
    EXPECT_FALSE(motion.grounded);
    EXPECT_TRUE(motion.onSteepGround);
    EXPECT_LT(motion.position.x, -1.0f);
}

TEST(CpuCapsuleMover, CanonicalizesMovementAtLargeSectorBoundary) {
    CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    const WorldPosition start{
        .sector = {1'500'000, -7, 12},
        .local = {127.75f, 3.0f, -4.0f},
    };
    const CharacterHandle character = mover.createCharacter(
        start, CharacterSettings{});
    ASSERT_NE(character, InvalidCharacter);

    const CharacterMotion motion = mover.moveCharacter(
        character, {4.0f, 0.0f, 0.0f}, false, 0.0f, 0.0f, 0.0f, 0.25f);
    EXPECT_EQ(motion.sector, glm::ivec3(1'500'001, -7, 12));
    EXPECT_FLOAT_EQ(motion.position.x, -127.25f);
    EXPECT_FLOAT_EQ(motion.position.y, 3.0f);
    EXPECT_FLOAT_EQ(motion.position.z, -4.0f);
    EXPECT_TRUE(isValidWorldPosition({motion.sector, motion.position}));
    const glm::dvec3 displacement = worldPositionToAbsolute(
        {motion.sector, motion.position}) - worldPositionToAbsolute(start);
    EXPECT_DOUBLE_EQ(displacement.x, 1.0);
    EXPECT_DOUBLE_EQ(displacement.y, 0.0);
    EXPECT_DOUBLE_EQ(displacement.z, 0.0);
}

TEST(CpuCapsuleMover, SamplesTerrainAcrossSectorBoundaryInLocalFrame) {
    constexpr uint32_t width = 520;
    constexpr uint32_t height = 4;
    constexpr float heightScale = 10.0f;
    std::vector<uint16_t> samples(
        size_t{width} * height, rawHeight(0.0f, heightScale));
    CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    ASSERT_TRUE(mover.setTerrain(
        samples, width, height, heightScale, 1.0f));
    const CharacterHandle character = mover.createCharacter(
        WorldPosition{{0, 0, 0}, {127.75f, 0.0f, 0.0f}},
        CharacterSettings{});
    ASSERT_NE(character, InvalidCharacter);

    const CharacterMotion motion = mover.moveCharacter(
        character, {1.0f, 0.0f, 0.0f}, false, 0.0f,
        20.0f, 50.0f, 0.5f);
    EXPECT_TRUE(motion.grounded);
    EXPECT_EQ(motion.sector, glm::ivec3(1, 0, 0));
    EXPECT_NEAR(motion.position.x, -127.75f, 1e-5f);
    EXPECT_NEAR(motion.position.y, 0.0f, 2e-4f);
}

} // namespace
} // namespace voxy::physics
