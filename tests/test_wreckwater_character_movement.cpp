#include <gtest/gtest.h>

#include "game/wreckwater_character_movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

namespace voxy::game {
namespace {

WreckwaterCharacterPlatformSample platform(
    SkiffId skiffId, uint32_t skiffGeneration,
    physics::BodyHandle body, const glm::dvec3& position,
    const glm::quat& orientation = glm::quat(
        1.0f, 0.0f, 0.0f, 0.0f),
    const glm::vec3& linearVelocity = glm::vec3(0.0f),
    const glm::vec3& angularVelocity = glm::vec3(0.0f),
    const glm::vec2& halfExtents = glm::vec2(4.0f),
    float deckHeight = 0.0f) {
    return {
        .skiffId = skiffId,
        .skiffGeneration = skiffGeneration,
        .body = body,
        .position = physics::worldPositionFromAbsolute(position),
        .orientation = orientation,
        .linearVelocity = linearVelocity,
        .angularVelocity = angularVelocity,
        .deckHalfExtents = halfExtents,
        .deckLocalHeight = deckHeight,
    };
}

WreckwaterCharacterSpawn airborneSpawn(
    PlayerId playerId, const glm::dvec3& position,
    const glm::vec3& velocity = glm::vec3(0.0f),
    WreckwaterCharacterMode mode =
        WreckwaterCharacterMode::Airborne) {
    return {
        .playerId = playerId,
        .connectionGeneration = 1u,
        .mode = mode,
        .feetPosition =
            physics::worldPositionFromAbsolute(position),
        .worldVelocity = velocity,
    };
}

WreckwaterCharacterSpawn aboardSpawn(
    PlayerId playerId,
    const WreckwaterCharacterPlatformSample& skiff,
    const glm::vec3& localFeet,
    const glm::vec3& localVelocity = glm::vec3(0.0f)) {
    physics::WorldPosition world;
    const glm::dvec3 absolute =
        physics::worldPositionToAbsolute(skiff.position)
        + glm::dvec3(skiff.orientation * localFeet);
    world = physics::worldPositionFromAbsolute(absolute);
    return {
        .playerId = playerId,
        .connectionGeneration = 1u,
        .mode = WreckwaterCharacterMode::OnSkiff,
        .feetPosition = world,
        .worldVelocity = skiff.linearVelocity
            + glm::cross(
                skiff.angularVelocity,
                skiff.orientation * localFeet)
            + skiff.orientation * localVelocity,
        .skiffId = skiff.skiffId,
        .skiffGeneration = skiff.skiffGeneration,
        .skiffBody = skiff.body,
        .skiffLocalFeetPosition = localFeet,
        .skiffLocalVelocity = localVelocity,
    };
}

WreckwaterCharacterInput input(
    uint64_t tick, uint64_t sequence,
    WreckwaterCharacterHandle character, PlayerId playerId,
    glm::vec2 move = glm::vec2(0.0f),
    bool jump = false, bool board = false,
    uint32_t connectionGeneration = 1u) {
    return {
        .targetTick = tick,
        .characterInputSequence = sequence,
        .character = character,
        .playerId = playerId,
        .connectionGeneration = connectionGeneration,
        .move = move,
        .jump = jump,
        .board = board,
    };
}

glm::dvec3 absolute(
    const physics::WorldPosition& position) {
    return physics::worldPositionToAbsolute(position);
}

TEST(WreckwaterCharacterMovement,
     RequiresFixedSixtyHertzAndOwnsStableBoundedStorage) {
    WreckwaterCharacterMovementAuthority authority;
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.tickRateHz = 59u;
    EXPECT_FALSE(authority.initialize(config));
    EXPECT_FALSE(authority.initialized());

    config = {};
    config.maximumCharacters = 0u;
    EXPECT_FALSE(authority.initialize(config));
    config = {};
    config.maximumCharacters =
        kWreckwaterMaximumCharacters + 1u;
    EXPECT_FALSE(authority.initialize(config));
    config = {};
    config.swimBuoyancyRatio = 1.0f;
    EXPECT_FALSE(authority.initialize(config));
    config = {};
    config.maximumCharacterWorldSpeed = 100.0f;
    EXPECT_FALSE(authority.initialize(config));

    ASSERT_TRUE(authority.initialize());
    const auto storage = authority.storageState();
    EXPECT_NE(storage.characters, nullptr);
    EXPECT_NE(storage.platformHistory, nullptr);
    EXPECT_NE(storage.pendingInputs, nullptr);
    EXPECT_NE(storage.transitions, nullptr);
    EXPECT_EQ(
        storage.characterCapacity,
        kWreckwaterMaximumCharacters);
    EXPECT_EQ(
        storage.platformCapacity,
        kWreckwaterMaximumCharacterPlatforms);
    EXPECT_EQ(
        storage.pendingInputCapacity,
        kWreckwaterMaximumCharacters);
    EXPECT_EQ(
        storage.transitionCapacity,
        kWreckwaterMaximumCharacterTransitionsPerTick);

    const auto spawned = authority.spawnCharacter(
        airborneSpawn(1u, {0.0, 2.0, 0.0}));
    ASSERT_TRUE(spawned);
    for (uint64_t tick = 1u; tick <= 1'000u; ++tick) {
        ASSERT_TRUE(authority.closeExactTick(tick, {}));
    }
    EXPECT_EQ(authority.storageState(), storage);
}

TEST(WreckwaterCharacterMovement,
     RejectsExtremeConfigurationAndSequencePoisoning) {
    WreckwaterCharacterMovementAuthority authority;
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.swimBuoyancyRatio =
        std::numeric_limits<float>::max();
    EXPECT_FALSE(authority.initialize(config));
    config = {};
    config.boardAssistHorizontalReach =
        std::numeric_limits<float>::max();
    EXPECT_FALSE(authority.initialize(config));
    config = {};
    config.landingSkin = 1.0f;
    EXPECT_FALSE(authority.initialize(config));
    config = {};
    config.waterHeight = std::numeric_limits<float>::max();
    EXPECT_FALSE(authority.initialize(config));

    WreckwaterCharacterMovementAuthority positiveZero;
    WreckwaterCharacterMovementAuthority negativeZero;
    auto positiveConfig =
        WreckwaterCharacterMovementAuthority::Config{};
    auto negativeConfig = positiveConfig;
    positiveConfig.waterEnterDepth = 0.0f;
    negativeConfig.waterEnterDepth = -0.0f;
    ASSERT_TRUE(positiveZero.initialize(positiveConfig));
    ASSERT_TRUE(negativeZero.initialize(negativeConfig));
    EXPECT_EQ(positiveZero.stateHash(), negativeZero.stateHash());
    EXPECT_FALSE(std::signbit(
        negativeZero.config().waterEnterDepth));

    const auto spawned = positiveZero.spawnCharacter(
        airborneSpawn(2u, {0.0, 2.0, 0.0}));
    ASSERT_TRUE(spawned);
    EXPECT_EQ(
        positiveZero.submitInput(input(
            1u, std::numeric_limits<uint64_t>::max(),
            spawned.handle, 2u)),
        WreckwaterCharacterStatus::InputSequenceExhausted);
    EXPECT_EQ(
        positiveZero.submitInput(input(
            1u, 1'025u, spawned.handle, 2u)),
        WreckwaterCharacterStatus::InputSequenceJumpTooLarge);
    EXPECT_EQ(
        positiveZero.submitInput(input(
            1u, 1u, spawned.handle, 2u)),
        WreckwaterCharacterStatus::Accepted);
}

TEST(WreckwaterCharacterMovement,
     MovingPlatformVelocityIsRecomposedAndInheritedOnce) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    constexpr physics::BodyHandle body{11u, 7u};
    auto skiff = platform(
        1u, 3u, body, {0.0, 2.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {2.0f, 0.0f, 0.0f});
    const auto spawned = authority.spawnCharacter(
        aboardSpawn(10u, skiff, {1.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(spawned);

    for (uint64_t tick = 1u; tick <= 600u; ++tick) {
        skiff.position = physics::worldPositionFromAbsolute(
            {2.0 * static_cast<double>(tick) / 60.0,
             2.0, 0.0});
        const auto result = authority.closeExactTick(
            tick, std::span(&skiff, 1u));
        ASSERT_TRUE(result);
        const auto* state = authority.character(spawned.handle);
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->mode, WreckwaterCharacterMode::OnSkiff);
        EXPECT_FLOAT_EQ(state->skiffLocalFeetPosition.x, 1.0f);
        EXPECT_FLOAT_EQ(state->worldVelocity.x, 2.0f);
        EXPECT_FLOAT_EQ(state->worldVelocity.y, 0.0f);
        EXPECT_FLOAT_EQ(state->worldVelocity.z, 0.0f);
    }
    const auto* beforeJump = authority.character(spawned.handle);
    ASSERT_NE(beforeJump, nullptr);
    EXPECT_NEAR(absolute(beforeJump->feetPosition).x, 21.0, 1e-5);

    ASSERT_EQ(
        authority.submitInput(input(
            601u, 1u, spawned.handle, 10u,
            glm::vec2(0.0f), true)),
        WreckwaterCharacterStatus::Accepted);
    skiff.position = physics::worldPositionFromAbsolute(
        {2.0 * 601.0 / 60.0, 2.0, 0.0});
    const auto jumped = authority.closeExactTick(
        601u, std::span(&skiff, 1u));
    ASSERT_TRUE(jumped);
    ASSERT_EQ(jumped.transitions.size(), 1u);
    EXPECT_EQ(
        jumped.transitions[0].from,
        WreckwaterCharacterMode::OnSkiff);
    EXPECT_EQ(
        jumped.transitions[0].to,
        WreckwaterCharacterMode::Airborne);
    const auto* airborne = authority.character(spawned.handle);
    ASSERT_NE(airborne, nullptr);
    EXPECT_EQ(airborne->mode, WreckwaterCharacterMode::Airborne);
    EXPECT_NEAR(airborne->worldVelocity.x, 2.0f, 1e-6f);
    EXPECT_NEAR(
        airborne->worldVelocity.y,
        authority.config().jumpSpeed, 1e-6f);

    const glm::dvec3 jumpPosition = absolute(
        airborne->feetPosition);
    ASSERT_TRUE(authority.closeExactTick(602u, {}));
    airborne = authority.character(spawned.handle);
    ASSERT_NE(airborne, nullptr);
    EXPECT_NEAR(
        absolute(airborne->feetPosition).x - jumpPosition.x,
        2.0 / 60.0, 2e-5);
    // The platform contribution was inherited once, not added again.
    EXPECT_NEAR(airborne->worldVelocity.x, 2.0f, 1e-6f);
}

TEST(WreckwaterCharacterMovement,
     HighSpeedYawUsesAngularPointVelocityWithoutEnergyGrowth) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    constexpr float linearSpeed = 120.0f;
    constexpr float angularSpeed = 12.0f;
    constexpr glm::vec3 local(3.0f, 0.0f, 0.0f);
    auto skiff = platform(
        2u, 9u, {21u, 4u}, {0.0, 5.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {linearSpeed, 0.0f, 0.0f},
        {0.0f, angularSpeed, 0.0f},
        {8.0f, 8.0f});
    const auto spawned = authority.spawnCharacter(
        aboardSpawn(20u, skiff, local));
    ASSERT_TRUE(spawned);

    float maximumError = 0.0f;
    for (uint64_t tick = 1u; tick <= 360u; ++tick) {
        const float angle =
            angularSpeed * static_cast<float>(tick) / 60.0f;
        skiff.orientation = glm::angleAxis(
            angle, glm::vec3(0.0f, 1.0f, 0.0f));
        skiff.position = physics::worldPositionFromAbsolute(
            {static_cast<double>(linearSpeed)
                 * static_cast<double>(tick) / 60.0,
             5.0, 0.0});
        ASSERT_TRUE(authority.closeExactTick(
            tick, std::span(&skiff, 1u)));
        const auto* state = authority.character(spawned.handle);
        ASSERT_NE(state, nullptr);
        const glm::vec3 lever = skiff.orientation * local;
        const glm::vec3 expectedVelocity =
            skiff.linearVelocity
            + glm::cross(skiff.angularVelocity, lever);
        const glm::dvec3 expectedPosition =
            absolute(skiff.position) + glm::dvec3(lever);
        maximumError = std::max(
            maximumError,
            glm::length(
                state->worldVelocity - expectedVelocity));
        EXPECT_NEAR(
            absolute(state->feetPosition).x,
            expectedPosition.x, 3e-5);
        EXPECT_NEAR(
            absolute(state->feetPosition).z,
            expectedPosition.z, 3e-5);
    }
    EXPECT_LT(maximumError, 2e-4f);
}

TEST(WreckwaterCharacterMovement,
     WalkingPastDeckEdgeInheritsOffCentrePlatformMotion) {
    WreckwaterCharacterMovementAuthority authority;
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.deckAcceleration = 100.0f;
    ASSERT_TRUE(authority.initialize(config));
    auto skiff = platform(
        1u, 1u, {31u, 1u}, {0.0, 1.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {10.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f},
        {2.0f, 2.0f});
    const auto spawned = authority.spawnCharacter(
        aboardSpawn(30u, skiff, {1.59f, 0.0f, 0.0f}));
    ASSERT_TRUE(spawned);
    ASSERT_EQ(
        authority.submitInput(input(
            1u, 1u, spawned.handle, 30u, {1.0f, 0.0f})),
        WreckwaterCharacterStatus::Accepted);
    const auto result = authority.closeExactTick(
        1u, std::span(&skiff, 1u));
    ASSERT_TRUE(result);
    ASSERT_EQ(result.transitions.size(), 1u);
    const auto* state = authority.character(spawned.handle);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->mode, WreckwaterCharacterMode::Airborne);
    const float relativeSpeed =
        config.deckAcceleration / 60.0f;
    const float detachedX =
        1.59f + relativeSpeed / 60.0f;
    const glm::vec3 expected =
        skiff.linearVelocity
        + glm::cross(
            skiff.angularVelocity,
            glm::vec3(detachedX, 0.0f, 0.0f))
        + glm::vec3(relativeSpeed, 0.0f, 0.0f);
    EXPECT_NEAR(state->worldVelocity.x, expected.x, 2e-5f);
    EXPECT_NEAR(state->worldVelocity.z, expected.z, 2e-5f);
}

TEST(WreckwaterCharacterMovement,
     AirborneCharacterSweepsOntoMovingDeck) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    const auto spawned = authority.spawnCharacter(
        airborneSpawn(40u, {0.0, 0.50, 0.0}, {0.0f, -3.0f, 0.0f}));
    ASSERT_TRUE(spawned);
    auto skiff = platform(
        1u, 1u, {41u, 1u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {1.0f, 0.0f, 0.0f}, glm::vec3(0.0f),
        {5.0f, 5.0f});

    bool landed = false;
    for (uint64_t tick = 1u; tick <= 20u; ++tick) {
        skiff.position = physics::worldPositionFromAbsolute(
            {static_cast<double>(tick) / 60.0, 0.0, 0.0});
        const auto result = authority.closeExactTick(
            tick, std::span(&skiff, 1u));
        ASSERT_TRUE(result);
        const auto* state = authority.character(spawned.handle);
        ASSERT_NE(state, nullptr);
        if (state->mode == WreckwaterCharacterMode::OnSkiff) {
            landed = true;
            EXPECT_EQ(state->skiffBody, skiff.body);
            EXPECT_NEAR(
                absolute(state->feetPosition).y, 0.0, 1e-5);
            break;
        }
    }
    EXPECT_TRUE(landed);
}

TEST(WreckwaterCharacterMovement,
     AirborneSweepChoosesEarliestDeckBeforeStableIdentity) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());

    // Stable identity orders skiff 1 first, but skiff 2 is the upper deck
    // and must receive the character at the earlier swept impact.
    const auto lower = platform(
        1u, 1u, {45u, 1u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), glm::vec3(0.0f),
        {5.0f, 5.0f});
    const auto upper = platform(
        2u, 1u, {46u, 1u}, {0.0, 0.30, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), glm::vec3(0.0f),
        {5.0f, 5.0f});
    const std::array platforms{lower, upper};
    ASSERT_TRUE(authority.closeExactTick(1u, platforms));
    const auto spawned = authority.spawnCharacter(
        airborneSpawn(
            45u, {0.0, 0.50, 0.0}, {0.0f, -40.0f, 0.0f}));
    ASSERT_TRUE(spawned);

    const auto result = authority.closeExactTick(2u, platforms);
    ASSERT_TRUE(result);
    const auto* state = authority.character(spawned.handle);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->mode, WreckwaterCharacterMode::OnSkiff);
    EXPECT_EQ(state->skiffId, upper.skiffId);
    EXPECT_EQ(state->skiffBody, upper.body);
    EXPECT_NEAR(absolute(state->feetPosition).y, 0.30, 1e-5);
}

TEST(WreckwaterCharacterMovement,
     PlatformContinuityRejectsTeleportMutationAndStaleReturnAtomically) {
    WreckwaterCharacterMovementAuthority authority;
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.waterHeight = -100.0f;
    ASSERT_TRUE(authority.initialize(config));

    auto skiff = platform(
        1u, 1u, {47u, 3u}, {127.75, 5.0, 0.0});
    const auto spawned = authority.spawnCharacter(
        aboardSpawn(46u, skiff, glm::vec3(0.0f)));
    ASSERT_TRUE(spawned);
    ASSERT_TRUE(authority.closeExactTick(
        1u, std::span(&skiff, 1u)));
    ASSERT_EQ(
        authority.submitInput(input(
            2u, 1u, spawned.handle, 46u, {0.5f, 0.0f})),
        WreckwaterCharacterStatus::Accepted);
    const uint64_t stagedHash = authority.stateHash();
    const auto storage = authority.storageState();

    auto invalid = skiff;
    invalid.position = physics::worldPositionFromAbsolute(
        {300.0, 5.0, 0.0});
    EXPECT_EQ(
        authority.closeExactTick(
            2u, std::span(&invalid, 1u)).status,
        WreckwaterCharacterStatus::PlatformMotionDiscontinuity);
    EXPECT_EQ(authority.lastClosedTick(), 1u);
    EXPECT_EQ(authority.stateHash(), stagedHash);
    EXPECT_EQ(authority.storageState(), storage);

    invalid = skiff;
    invalid.orientation = glm::angleAxis(
        glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(
        authority.closeExactTick(
            2u, std::span(&invalid, 1u)).status,
        WreckwaterCharacterStatus::PlatformMotionDiscontinuity);
    EXPECT_EQ(authority.stateHash(), stagedHash);

    invalid = skiff;
    invalid.deckLocalHeight += 1.0f;
    EXPECT_EQ(
        authority.closeExactTick(
            2u, std::span(&invalid, 1u)).status,
        WreckwaterCharacterStatus::PlatformMotionDiscontinuity);
    invalid = skiff;
    invalid.deckHalfExtents.x += 1.0f;
    EXPECT_EQ(
        authority.closeExactTick(
            2u, std::span(&invalid, 1u)).status,
        WreckwaterCharacterStatus::PlatformMotionDiscontinuity);
    EXPECT_EQ(authority.stateHash(), stagedHash);

    // Quaternion sign is not a motion discontinuity.
    skiff.orientation = -skiff.orientation;
    const auto recovered = authority.closeExactTick(
        2u, std::span(&skiff, 1u));
    ASSERT_TRUE(recovered);
    EXPECT_EQ(recovered.appliedInputCount, 1u);

    ASSERT_TRUE(authority.closeExactTick(3u, {}));
    ASSERT_EQ(
        authority.character(spawned.handle)->mode,
        WreckwaterCharacterMode::Airborne);
    const uint64_t missingHash = authority.stateHash();
    EXPECT_EQ(
        authority.closeExactTick(
            4u, std::span(&skiff, 1u)).status,
        WreckwaterCharacterStatus::PlatformMotionDiscontinuity);
    EXPECT_EQ(authority.stateHash(), missingHash);

    auto skippedGeneration = skiff;
    skippedGeneration.skiffGeneration = 3u;
    skippedGeneration.body.generation += 2u;
    EXPECT_EQ(
        authority.closeExactTick(
            4u, std::span(&skippedGeneration, 1u)).status,
        WreckwaterCharacterStatus::StalePlatformIdentity);
    auto reusedBody = skiff;
    reusedBody.skiffGeneration = 2u;
    EXPECT_EQ(
        authority.closeExactTick(
            4u, std::span(&reusedBody, 1u)).status,
        WreckwaterCharacterStatus::StalePlatformIdentity);
    EXPECT_EQ(authority.stateHash(), missingHash);

    auto successor = skiff;
    successor.skiffGeneration = 2u;
    successor.body.generation += 1u;
    ASSERT_TRUE(authority.closeExactTick(
        4u, std::span(&successor, 1u)));
    const auto* detached = authority.character(spawned.handle);
    ASSERT_NE(detached, nullptr);
    EXPECT_EQ(detached->mode, WreckwaterCharacterMode::Airborne);
    EXPECT_EQ(detached->skiffId, 0u);
}

TEST(WreckwaterCharacterMovement,
     SweepRejectsUndersideAndBreaksExactTiesByStableIdentity) {
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.waterHeight = -100.0f;
    WreckwaterCharacterMovementAuthority first;
    WreckwaterCharacterMovementAuthority second;
    ASSERT_TRUE(first.initialize(config));
    ASSERT_TRUE(second.initialize(config));
    const auto lowIdentity = platform(
        1u, 1u, {51u, 1u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), glm::vec3(0.0f),
        {5.0f, 5.0f});
    const auto highIdentity = platform(
        2u, 1u, {52u, 1u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), glm::vec3(0.0f),
        {5.0f, 5.0f});
    const std::array forward{lowIdentity, highIdentity};
    const std::array reverse{highIdentity, lowIdentity};
    ASSERT_TRUE(first.closeExactTick(1u, forward));
    ASSERT_TRUE(second.closeExactTick(1u, reverse));
    const auto firstSpawn = first.spawnCharacter(
        airborneSpawn(
            47u, {0.0, 0.25, 0.0}, {0.0f, -20.0f, 0.0f}));
    const auto secondSpawn = second.spawnCharacter(
        airborneSpawn(
            47u, {0.0, 0.25, 0.0}, {0.0f, -20.0f, 0.0f}));
    const auto underside = first.spawnCharacter(
        airborneSpawn(
            48u, {2.0, -0.01, 0.0}, {0.0f, -1.0f, 0.0f}));
    ASSERT_TRUE(firstSpawn);
    ASSERT_TRUE(secondSpawn);
    ASSERT_TRUE(underside);

    ASSERT_TRUE(first.closeExactTick(2u, reverse));
    ASSERT_TRUE(second.closeExactTick(2u, forward));
    ASSERT_EQ(
        first.character(firstSpawn.handle)->mode,
        WreckwaterCharacterMode::OnSkiff);
    ASSERT_EQ(
        second.character(secondSpawn.handle)->mode,
        WreckwaterCharacterMode::OnSkiff);
    EXPECT_EQ(first.character(firstSpawn.handle)->skiffId, 1u);
    EXPECT_EQ(second.character(secondSpawn.handle)->skiffId, 1u);
    EXPECT_NE(
        first.character(underside.handle)->mode,
        WreckwaterCharacterMode::OnSkiff);
}

TEST(WreckwaterCharacterMovement,
     RigidSweepLandsOnContinuouslyRotatingDeck) {
    WreckwaterCharacterMovementAuthority authority;
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.waterHeight = -100.0f;
    ASSERT_TRUE(authority.initialize(config));
    constexpr float angularSpeed = 12.0f;
    auto skiff = platform(
        1u, 1u, {53u, 1u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), {0.0f, 0.0f, angularSpeed},
        {5.0f, 5.0f});
    ASSERT_TRUE(authority.closeExactTick(
        1u, std::span(&skiff, 1u)));
    const auto spawned = authority.spawnCharacter(
        airborneSpawn(
            49u, {1.0, 0.30, 0.0}, {0.0f, -10.0f, 0.0f}));
    ASSERT_TRUE(spawned);
    skiff.orientation = glm::angleAxis(
        angularSpeed / 60.0f,
        glm::vec3(0.0f, 0.0f, 1.0f));

    ASSERT_TRUE(authority.closeExactTick(
        2u, std::span(&skiff, 1u)));
    const auto* state = authority.character(spawned.handle);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->mode, WreckwaterCharacterMode::OnSkiff);
    EXPECT_EQ(state->skiffBody, skiff.body);
}

TEST(WreckwaterCharacterMovement,
     WaterEntryAndExitUseSeparateHysteresisThresholds) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    const auto aboveEntry = authority.spawnCharacter(
        airborneSpawn(51u, {0.0, -0.05, 0.0}));
    const auto heldSwimming = authority.spawnCharacter(
        airborneSpawn(
            52u, {2.0, 0.05, 0.0}, glm::vec3(0.0f),
            WreckwaterCharacterMode::Swimming));
    const auto belowEntry = authority.spawnCharacter(
        airborneSpawn(53u, {4.0, -0.20, 0.0}));
    const auto aboveExit = authority.spawnCharacter(
        airborneSpawn(
            54u, {6.0, 0.30, 0.0}, glm::vec3(0.0f),
            WreckwaterCharacterMode::Swimming));
    ASSERT_TRUE(aboveEntry);
    ASSERT_TRUE(heldSwimming);
    ASSERT_TRUE(belowEntry);
    ASSERT_TRUE(aboveExit);

    ASSERT_TRUE(authority.closeExactTick(1u, {}));
    EXPECT_EQ(
        authority.character(aboveEntry.handle)->mode,
        WreckwaterCharacterMode::Airborne);
    EXPECT_EQ(
        authority.character(heldSwimming.handle)->mode,
        WreckwaterCharacterMode::Swimming);
    EXPECT_EQ(
        authority.character(belowEntry.handle)->mode,
        WreckwaterCharacterMode::Swimming);
    EXPECT_EQ(
        authority.character(aboveExit.handle)->mode,
        WreckwaterCharacterMode::Airborne);

    const auto* swimming =
        authority.character(heldSwimming.handle);
    ASSERT_NE(swimming, nullptr);
    EXPECT_GT(
        absolute(swimming->feetPosition).y,
        -authority.config().waterEnterDepth);
    EXPECT_LT(
        absolute(swimming->feetPosition).y,
        authority.config().waterExitHeight);
}

TEST(WreckwaterCharacterMovement,
     SwimmingUsesBuoyancyDragAndBoundedEdgeBoarding) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    const auto swimmer = authority.spawnCharacter(
        airborneSpawn(
            60u, {2.0, -1.0, 0.0}, {10.0f, 0.0f, 0.0f},
            WreckwaterCharacterMode::Swimming));
    const auto boarder = authority.spawnCharacter(
        airborneSpawn(
            61u, {2.0, 0.0, 0.0}, glm::vec3(0.0f),
            WreckwaterCharacterMode::Swimming));
    ASSERT_TRUE(swimmer);
    ASSERT_TRUE(boarder);
    auto skiff = platform(
        1u, 1u, {61u, 5u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), glm::vec3(0.0f),
        {2.0f, 2.0f}, 0.8f);

    ASSERT_EQ(
        authority.submitInput(input(
            1u, 1u, boarder.handle, 61u,
            glm::vec2(-1.0f, 0.0f), false, true)),
        WreckwaterCharacterStatus::Accepted);
    ASSERT_TRUE(authority.closeExactTick(
        1u, std::span(&skiff, 1u)));
    const auto* boarded = authority.character(boarder.handle);
    ASSERT_NE(boarded, nullptr);
    EXPECT_EQ(boarded->mode, WreckwaterCharacterMode::OnSkiff);
    EXPECT_NEAR(
        boarded->skiffLocalFeetPosition.x,
        skiff.deckHalfExtents.x
            - authority.config().capsule.radius,
        1e-5f);
    EXPECT_NEAR(
        absolute(boarded->feetPosition).y, 0.8, 1e-5);

    const auto* damped = authority.character(swimmer.handle);
    ASSERT_NE(damped, nullptr);
    EXPECT_LT(damped->worldVelocity.x, 10.0f);
    EXPECT_GT(damped->worldVelocity.y, 0.0f);
    for (uint64_t tick = 2u; tick <= 600u; ++tick) {
        ASSERT_TRUE(authority.closeExactTick(
            tick, std::span(&skiff, 1u)));
    }
    damped = authority.character(swimmer.handle);
    ASSERT_NE(damped, nullptr);
    EXPECT_TRUE(std::isfinite(damped->worldVelocity.x));
    EXPECT_TRUE(std::isfinite(damped->worldVelocity.y));
    EXPECT_TRUE(std::isfinite(damped->worldVelocity.z));
    EXPECT_LT(
        glm::length(damped->worldVelocity),
        authority.config().maximumCharacterWorldSpeed);
}

TEST(WreckwaterCharacterMovement,
     BoardingRejectsDeckCentreAndExcessiveRelativeSpeed) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    const auto fastBoarder = authority.spawnCharacter(
        airborneSpawn(
            62u, {2.0, 0.0, 0.0}, glm::vec3(0.0f),
            WreckwaterCharacterMode::Swimming));
    const auto centreBoarder = authority.spawnCharacter(
        airborneSpawn(
            63u, {0.0, 0.0, 0.0}, glm::vec3(0.0f),
            WreckwaterCharacterMode::Swimming));
    ASSERT_TRUE(fastBoarder);
    ASSERT_TRUE(centreBoarder);
    auto skiff = platform(
        1u, 1u, {63u, 2u}, {0.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {20.0f, 0.0f, 0.0f}, glm::vec3(0.0f),
        {2.0f, 2.0f}, 0.8f);
    ASSERT_EQ(
        authority.submitInput(input(
            1u, 1u, fastBoarder.handle, 62u,
            {-1.0f, 0.0f}, false, true)),
        WreckwaterCharacterStatus::Accepted);
    ASSERT_EQ(
        authority.submitInput(input(
            1u, 1u, centreBoarder.handle, 63u,
            {1.0f, 0.0f}, false, true)),
        WreckwaterCharacterStatus::Accepted);
    ASSERT_TRUE(authority.closeExactTick(
        1u, std::span(&skiff, 1u)));
    EXPECT_EQ(
        authority.character(fastBoarder.handle)->mode,
        WreckwaterCharacterMode::Swimming);
    EXPECT_EQ(
        authority.character(centreBoarder.handle)->mode,
        WreckwaterCharacterMode::Swimming);
}

TEST(WreckwaterCharacterMovement,
     DisconnectPurgesInputAndReconnectRejectsStaleIdentity) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    const auto first = authority.spawnCharacter(
        airborneSpawn(70u, {0.0, 3.0, 0.0}));
    ASSERT_TRUE(first);
    ASSERT_EQ(
        authority.submitInput(input(
            1u, 1u, first.handle, 70u,
            {1.0f, 0.0f}, true)),
        WreckwaterCharacterStatus::Accepted);
    ASSERT_EQ(
        authority.disconnectCharacter(
            first.handle, 70u, 1u),
        WreckwaterCharacterStatus::Accepted);
    const auto disconnectedTick =
        authority.closeExactTick(1u, {});
    ASSERT_TRUE(disconnectedTick);
    EXPECT_EQ(disconnectedTick.appliedInputCount, 0u);
    EXPECT_EQ(disconnectedTick.neutralInputCount, 1u);
    const auto* disconnected = authority.character(first.handle);
    ASSERT_NE(disconnected, nullptr);
    EXPECT_FALSE(disconnected->connected);
    EXPECT_FLOAT_EQ(disconnected->worldVelocity.x, 0.0f);
    EXPECT_LT(disconnected->worldVelocity.y, 0.0f);

    EXPECT_EQ(
        authority.submitInput(input(
            2u, 2u, first.handle, 70u,
            {1.0f, 0.0f}, false, false, 1u)),
        WreckwaterCharacterStatus::Disconnected);
    ASSERT_EQ(
        authority.reconnectCharacter(
            first.handle, 70u, 1u, 2u),
        WreckwaterCharacterStatus::Accepted);
    EXPECT_EQ(
        authority.submitInput(input(
            2u, 2u, first.handle, 70u,
            {1.0f, 0.0f}, false, false, 1u)),
        WreckwaterCharacterStatus::StaleConnectionGeneration);
    ASSERT_EQ(
        authority.submitInput(input(
            2u, 1u, first.handle, 70u,
            {0.25f, 0.0f}, false, false, 2u)),
        WreckwaterCharacterStatus::Accepted);
    ASSERT_EQ(
        authority.submitInput(input(
            2u, 3u, first.handle, 70u,
            {0.75f, 0.0f}, false, false, 2u)),
        WreckwaterCharacterStatus::Accepted);
    EXPECT_EQ(
        authority.submitInput(input(
            2u, 2u, first.handle, 70u,
            {1.0f, 0.0f}, false, false, 2u)),
        WreckwaterCharacterStatus::ReplayedInputSequence);
    ASSERT_TRUE(authority.closeExactTick(2u, {}));
    const auto* reconnected = authority.character(first.handle);
    ASSERT_NE(reconnected, nullptr);
    EXPECT_EQ(
        reconnected->lastAppliedCharacterInputSequence, 3u);

    ASSERT_EQ(
        authority.destroyCharacter(first.handle, 70u),
        WreckwaterCharacterStatus::Accepted);
    const auto replacement = authority.spawnCharacter(
        airborneSpawn(71u, {0.0, 3.0, 0.0}));
    ASSERT_TRUE(replacement);
    EXPECT_NE(replacement.handle, first.handle);
    EXPECT_EQ(
        authority.submitInput(input(
            3u, 1u, first.handle, 70u)),
        WreckwaterCharacterStatus::StaleCharacterIdentity);
}

TEST(WreckwaterCharacterMovement,
     CapacityAndPlatformFailuresAreAtomic) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    std::array<WreckwaterCharacterSpawnResult, 4> spawned{};
    for (size_t index = 0u; index < spawned.size(); ++index) {
        spawned[index] = authority.spawnCharacter(
            airborneSpawn(
                100u + index,
                {static_cast<double>(index), 10.0, 0.0}));
        ASSERT_TRUE(spawned[index]);
    }
    const auto fifth = authority.spawnCharacter(
        airborneSpawn(105u, {5.0, 10.0, 0.0}));
    EXPECT_EQ(
        fifth.status,
        WreckwaterCharacterStatus::CharacterCapacityExceeded);

    const auto first = platform(
        1u, 2u, {81u, 1u}, {0.0, 0.0, 0.0});
    const auto second = platform(
        2u, 4u, {82u, 1u}, {10.0, 0.0, 0.0});
    const auto third = platform(
        3u, 1u, {83u, 1u}, {20.0, 0.0, 0.0});
    const std::array tooMany{first, second, third};
    const uint64_t initialHash = authority.stateHash();
    const auto initialStorage = authority.storageState();
    const auto overflow = authority.closeExactTick(1u, tooMany);
    EXPECT_EQ(
        overflow.status,
        WreckwaterCharacterStatus::PlatformCapacityExceeded);
    EXPECT_EQ(authority.lastClosedTick(), 0u);
    EXPECT_EQ(authority.stateHash(), initialHash);
    EXPECT_EQ(authority.storageState(), initialStorage);

    auto duplicate = second;
    duplicate.body = first.body;
    const std::array duplicates{first, duplicate};
    const auto duplicateResult =
        authority.closeExactTick(1u, duplicates);
    EXPECT_EQ(
        duplicateResult.status,
        WreckwaterCharacterStatus::DuplicatePlatformIdentity);
    EXPECT_EQ(authority.lastClosedTick(), 0u);
    EXPECT_EQ(authority.stateHash(), initialHash);

    const std::array valid{second, first};
    ASSERT_TRUE(authority.closeExactTick(1u, valid));
    const uint64_t closedHash = authority.stateHash();
    auto stale = first;
    stale.skiffGeneration = 1u;
    const auto staleResult = authority.closeExactTick(
        2u, std::span(&stale, 1u));
    EXPECT_EQ(
        staleResult.status,
        WreckwaterCharacterStatus::StalePlatformIdentity);
    EXPECT_EQ(authority.lastClosedTick(), 1u);
    EXPECT_EQ(authority.stateHash(), closedHash);

    const auto newIdentityResult = authority.closeExactTick(
        2u, std::span(&third, 1u));
    EXPECT_EQ(
        newIdentityResult.status,
        WreckwaterCharacterStatus::PlatformCapacityExceeded);
    EXPECT_EQ(authority.lastClosedTick(), 1u);
    EXPECT_EQ(authority.stateHash(), closedHash);
}

TEST(WreckwaterCharacterMovement,
     LongRunConvergesExactlyAcrossInputAndPlatformOrder) {
    WreckwaterCharacterMovementAuthority first;
    WreckwaterCharacterMovementAuthority second;
    ASSERT_TRUE(first.initialize());
    ASSERT_TRUE(second.initialize());

    auto firstSkiff = platform(
        1u, 1u, {91u, 2u}, {-8.0, 2.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), {0.0f, 0.18f, 0.0f},
        {20.0f, 20.0f});
    auto secondSkiff = platform(
        2u, 1u, {92u, 2u}, {8.0, 2.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f), {0.0f, -0.12f, 0.0f},
        {20.0f, 20.0f});
    const std::array spawns{
        aboardSpawn(201u, firstSkiff, {-2.0f, 0.0f, 0.0f}),
        aboardSpawn(202u, firstSkiff, {2.0f, 0.0f, 0.0f}),
        aboardSpawn(203u, secondSkiff, {-2.0f, 0.0f, 0.0f}),
        aboardSpawn(204u, secondSkiff, {2.0f, 0.0f, 0.0f}),
    };
    std::array<WreckwaterCharacterHandle, 4> handles{};
    for (size_t index = 0u; index < spawns.size(); ++index) {
        const auto a = first.spawnCharacter(spawns[index]);
        const auto b = second.spawnCharacter(spawns[index]);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        ASSERT_EQ(a.handle, b.handle);
        handles[index] = a.handle;
    }
    ASSERT_EQ(first.stateHash(), second.stateHash());
    const auto firstStorage = first.storageState();
    const auto secondStorage = second.storageState();

    constexpr uint64_t tickCount = 20'000u;
    for (uint64_t tick = 1u; tick <= tickCount; ++tick) {
        const float time = static_cast<float>(tick) / 60.0f;
        const double wideTime =
            static_cast<double>(tick) / 60.0;
        const float firstAngle = 0.18f * time;
        const float secondAngle = -0.12f * time;
        firstSkiff.orientation = glm::angleAxis(
            firstAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        secondSkiff.orientation = glm::angleAxis(
            secondAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        firstSkiff.position = physics::worldPositionFromAbsolute(
            {-8.0 + 2.0 * std::sin(0.07 * wideTime),
             2.0, 1.5 * std::cos(0.07 * wideTime)});
        secondSkiff.position = physics::worldPositionFromAbsolute(
            {8.0 + 1.5 * std::cos(0.05 * wideTime),
             2.0, 2.0 * std::sin(0.05 * wideTime)});
        firstSkiff.linearVelocity = {
            0.14f * std::cos(0.07f * time),
            0.0f,
            -0.105f * std::sin(0.07f * time),
        };
        secondSkiff.linearVelocity = {
            -0.075f * std::sin(0.05f * time),
            0.0f,
            0.10f * std::cos(0.05f * time),
        };

        std::array<WreckwaterCharacterInput, 4> inputs{};
        for (size_t index = 0u; index < handles.size(); ++index) {
            const float direction =
                ((tick / 120u + index) & 1u) != 0u
                ? 0.25f : -0.25f;
            inputs[index] = input(
                tick, tick, handles[index], 201u + index,
                {direction, 0.0f});
        }
        for (size_t index = 0u; index < inputs.size(); ++index) {
            if (first.submitInput(inputs[index])
                    != WreckwaterCharacterStatus::Accepted
                || second.submitInput(
                       inputs[inputs.size() - 1u - index])
                    != WreckwaterCharacterStatus::Accepted) {
                ADD_FAILURE() << "input divergence at tick " << tick;
                return;
            }
        }
        if (first.stateHash() != second.stateHash()) {
            ADD_FAILURE()
                << "pending state diverged at tick " << tick;
            return;
        }

        const std::array forward{firstSkiff, secondSkiff};
        const std::array reverse{secondSkiff, firstSkiff};
        const auto a = first.closeExactTick(tick, forward);
        const auto b = second.closeExactTick(tick, reverse);
        if (!a || !b) {
            ADD_FAILURE()
                << "tick failed at " << tick << ": "
                << wreckwaterCharacterStatusName(a.status)
                << " / "
                << wreckwaterCharacterStatusName(b.status);
            return;
        }
        if (first.stateHash() != second.stateHash()
            || !std::equal(
                first.characters().begin(),
                first.characters().end(),
                second.characters().begin(),
                second.characters().end())) {
            ADD_FAILURE()
                << "closed state diverged at tick " << tick;
            return;
        }
    }
    EXPECT_EQ(first.lastClosedTick(), tickCount);
    EXPECT_EQ(first.stateHash(), second.stateHash());
    EXPECT_EQ(first.storageState(), firstStorage);
    EXPECT_EQ(second.storageState(), secondStorage);
}

} // namespace
} // namespace voxy::game
