#include <gtest/gtest.h>

#include "game/wreckwater_character_step.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtx/quaternion.hpp>

namespace voxy::game {
namespace {

static_assert(noexcept(wreckwaterCharacterStep(
    std::declval<const WreckwaterCharacterStepInput&>())));

WreckwaterCharacterPlatformSample platform(
    SkiffId skiffId, physics::BodyHandle body,
    const glm::dvec3& position,
    const glm::quat& orientation =
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
    const glm::vec3& linearVelocity = glm::vec3(0.0f),
    const glm::vec3& angularVelocity = glm::vec3(0.0f),
    const glm::vec2& halfExtents = glm::vec2(4.0f),
    float deckHeight = 0.0f) {
    return {
        .skiffId = skiffId,
        .skiffGeneration = 1u,
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
    const glm::vec3& localFeet) {
    const glm::vec3 worldOffset =
        skiff.orientation * localFeet;
    return {
        .playerId = playerId,
        .connectionGeneration = 1u,
        .mode = WreckwaterCharacterMode::OnSkiff,
        .feetPosition = physics::worldPositionFromAbsolute(
            physics::worldPositionToAbsolute(skiff.position)
            + glm::dvec3(worldOffset)),
        .worldVelocity = skiff.linearVelocity
            + glm::cross(
                skiff.angularVelocity, worldOffset),
        .skiffId = skiff.skiffId,
        .skiffGeneration = skiff.skiffGeneration,
        .skiffBody = skiff.body,
        .skiffLocalFeetPosition = localFeet,
    };
}

WreckwaterCharacterInput characterInput(
    uint64_t tick, uint64_t sequence,
    WreckwaterCharacterHandle character, PlayerId playerId,
    glm::vec2 move = glm::vec2(0.0f),
    bool jump = false, bool board = false) {
    return {
        .targetTick = tick,
        .characterInputSequence = sequence,
        .character = character,
        .playerId = playerId,
        .connectionGeneration = 1u,
        .move = move,
        .jump = jump,
        .board = board,
    };
}

void expectAuthorityParity(
    const WreckwaterCharacterStepResult& stepped,
    const WreckwaterCharacterTickResult& authoritative,
    const WreckwaterCharacterMovementAuthority& authority,
    WreckwaterCharacterHandle character) {
    ASSERT_EQ(
        stepped.status, WreckwaterCharacterStatus::Accepted);
    ASSERT_EQ(
        authoritative.status,
        WreckwaterCharacterStatus::Accepted);
    const auto* state = authority.character(character);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(stepped.nextState, *state);
    if (stepped.transition.has_value()) {
        ASSERT_EQ(authoritative.transitions.size(), 1u);
        EXPECT_EQ(
            *stepped.transition,
            authoritative.transitions.front());
    } else {
        EXPECT_TRUE(authoritative.transitions.empty());
    }
}

TEST(WreckwaterCharacterStep,
     AboardMovementAndJumpMatchAuthorityExactly) {
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize());
    const auto skiff = platform(
        1u, {101u, 1u}, {0.0, 2.0, 0.0});
    const auto spawned = authority.spawnCharacter(
        aboardSpawn(101u, skiff, {0.5f, 0.0f, -0.5f}));
    ASSERT_TRUE(spawned);

    const auto walk = characterInput(
        1u, 1u, spawned.handle, 101u, {1.00005f, 0.0f});
    ASSERT_EQ(
        authority.submitInput(walk),
        WreckwaterCharacterStatus::Accepted);
    const WreckwaterCharacterState walkPrior =
        *authority.character(spawned.handle);
    const auto walked = wreckwaterCharacterStep({
        .config = authority.config(),
        .priorState = walkPrior,
        .input = walk,
        .previousPlatforms = {},
        .currentPlatforms = std::span(&skiff, 1u),
        .tick = 1u,
    });
    const auto authoritativeWalk = authority.closeExactTick(
        1u, std::span(&skiff, 1u));
    expectAuthorityParity(
        walked, authoritativeWalk, authority, spawned.handle);
    ASSERT_EQ(
        walked.nextState.mode,
        WreckwaterCharacterMode::OnSkiff);

    const auto jump = characterInput(
        2u, 2u, spawned.handle, 101u,
        glm::vec2(0.0f), true);
    ASSERT_EQ(
        authority.submitInput(jump),
        WreckwaterCharacterStatus::Accepted);
    const WreckwaterCharacterState jumpPrior =
        *authority.character(spawned.handle);
    const auto jumped = wreckwaterCharacterStep({
        .config = authority.config(),
        .priorState = jumpPrior,
        .input = jump,
        .previousPlatforms = std::span(&skiff, 1u),
        .currentPlatforms = std::span(&skiff, 1u),
        .tick = 2u,
    });
    const auto authoritativeJump = authority.closeExactTick(
        2u, std::span(&skiff, 1u));
    expectAuthorityParity(
        jumped, authoritativeJump, authority, spawned.handle);
    ASSERT_TRUE(jumped.transition.has_value());
    EXPECT_EQ(
        jumped.transition->from,
        WreckwaterCharacterMode::OnSkiff);
    EXPECT_EQ(
        jumped.transition->to,
        WreckwaterCharacterMode::Airborne);
}

TEST(WreckwaterCharacterStep,
     FallingLandsOnMovingRotatingDeckExactly) {
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.waterHeight = -100.0f;
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize(config));

    constexpr float angularSpeed = 12.0f;
    const auto previous = platform(
        1u, {102u, 1u}, {127.0, 0.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {2.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, angularSpeed},
        {5.0f, 5.0f});
    ASSERT_TRUE(authority.closeExactTick(
        1u, std::span(&previous, 1u)));
    const auto spawned = authority.spawnCharacter(
        airborneSpawn(
            102u, {128.0, 0.30, 0.0},
            {2.0f, -10.0f, 0.0f}));
    ASSERT_TRUE(spawned);

    auto current = previous;
    current.position = physics::worldPositionFromAbsolute(
        {127.0 + 2.0 / 60.0, 0.0, 0.0});
    current.orientation = glm::angleAxis(
        angularSpeed / 60.0f,
        glm::vec3(0.0f, 0.0f, 1.0f));
    current.orientation *= glm::inversesqrt(
        glm::dot(current.orientation, current.orientation));
    const auto neutral = characterInput(
        2u, 0u, spawned.handle, 102u);
    const WreckwaterCharacterState prior =
        *authority.character(spawned.handle);
    const auto stepped = wreckwaterCharacterStep({
        .config = authority.config(),
        .priorState = prior,
        .input = neutral,
        .previousPlatforms = std::span(&previous, 1u),
        .currentPlatforms = std::span(&current, 1u),
        .tick = 2u,
    });
    const auto authoritative = authority.closeExactTick(
        2u, std::span(&current, 1u));
    expectAuthorityParity(
        stepped, authoritative, authority, spawned.handle);
    ASSERT_EQ(
        stepped.nextState.mode,
        WreckwaterCharacterMode::OnSkiff);
    ASSERT_TRUE(stepped.transition.has_value());
    EXPECT_EQ(
        stepped.transition->from,
        WreckwaterCharacterMode::Airborne);
    EXPECT_EQ(
        stepped.transition->to,
        WreckwaterCharacterMode::OnSkiff);
}

TEST(WreckwaterCharacterStep,
     MovingDeckCrossesWorldSectorExactly) {
    auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    config.waterHeight = -100.0f;
    WreckwaterCharacterMovementAuthority authority;
    ASSERT_TRUE(authority.initialize(config));

    const auto previous = platform(
        1u, {103u, 1u}, {127.99, 2.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {2.0f, 0.0f, 0.0f});
    const auto spawned = authority.spawnCharacter(
        aboardSpawn(103u, previous, glm::vec3(0.0f)));
    ASSERT_TRUE(spawned);
    ASSERT_TRUE(authority.closeExactTick(
        1u, std::span(&previous, 1u)));
    ASSERT_EQ(
        authority.character(spawned.handle)
            ->feetPosition.sector.x,
        0);

    auto current = previous;
    current.position = physics::worldPositionFromAbsolute(
        {127.99 + 2.0 / 60.0, 2.0, 0.0});
    const auto neutral = characterInput(
        2u, 0u, spawned.handle, 103u);
    const WreckwaterCharacterState prior =
        *authority.character(spawned.handle);
    const auto stepped = wreckwaterCharacterStep({
        .config = authority.config(),
        .priorState = prior,
        .input = neutral,
        .previousPlatforms = std::span(&previous, 1u),
        .currentPlatforms = std::span(&current, 1u),
        .tick = 2u,
    });
    const auto authoritative = authority.closeExactTick(
        2u, std::span(&current, 1u));
    expectAuthorityParity(
        stepped, authoritative, authority, spawned.handle);
    EXPECT_EQ(stepped.nextState.feetPosition.sector.x, 1);
}

TEST(WreckwaterCharacterStep,
     SwimmingBoardAssistAndWaterExitMatchAuthorityExactly) {
    {
        WreckwaterCharacterMovementAuthority authority;
        ASSERT_TRUE(authority.initialize());
        const auto spawned = authority.spawnCharacter(
            airborneSpawn(
                104u, {0.0, -1.0, 0.0},
                {3.0f, 0.0f, 0.0f},
                WreckwaterCharacterMode::Swimming));
        ASSERT_TRUE(spawned);
        const auto swim = characterInput(
            1u, 1u, spawned.handle, 104u,
            {0.4f, -0.2f}, true);
        ASSERT_EQ(
            authority.submitInput(swim),
            WreckwaterCharacterStatus::Accepted);
        const WreckwaterCharacterState prior =
            *authority.character(spawned.handle);
        const auto stepped = wreckwaterCharacterStep({
            .config = authority.config(),
            .priorState = prior,
            .input = swim,
            .previousPlatforms = {},
            .currentPlatforms = {},
            .tick = 1u,
        });
        const auto authoritative =
            authority.closeExactTick(1u, {});
        expectAuthorityParity(
            stepped, authoritative, authority, spawned.handle);
        EXPECT_EQ(
            stepped.nextState.mode,
            WreckwaterCharacterMode::Swimming);
    }

    {
        WreckwaterCharacterMovementAuthority authority;
        ASSERT_TRUE(authority.initialize());
        const auto skiff = platform(
            1u, {104u, 2u}, {0.0, 0.0, 0.0},
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f), glm::vec3(0.0f),
            {2.0f, 2.0f}, 0.8f);
        const auto spawned = authority.spawnCharacter(
            airborneSpawn(
                105u, {2.0, 0.0, 0.0},
                glm::vec3(0.0f),
                WreckwaterCharacterMode::Swimming));
        ASSERT_TRUE(spawned);
        const auto board = characterInput(
            1u, 1u, spawned.handle, 105u,
            {-1.0f, 0.0f}, false, true);
        ASSERT_EQ(
            authority.submitInput(board),
            WreckwaterCharacterStatus::Accepted);
        const WreckwaterCharacterState prior =
            *authority.character(spawned.handle);
        const auto stepped = wreckwaterCharacterStep({
            .config = authority.config(),
            .priorState = prior,
            .input = board,
            .previousPlatforms = {},
            .currentPlatforms = std::span(&skiff, 1u),
            .tick = 1u,
        });
        const auto authoritative = authority.closeExactTick(
            1u, std::span(&skiff, 1u));
        expectAuthorityParity(
            stepped, authoritative, authority, spawned.handle);
        ASSERT_TRUE(stepped.transition.has_value());
        EXPECT_EQ(
            stepped.transition->from,
            WreckwaterCharacterMode::Swimming);
        EXPECT_EQ(
            stepped.transition->to,
            WreckwaterCharacterMode::OnSkiff);
    }

    {
        WreckwaterCharacterMovementAuthority authority;
        ASSERT_TRUE(authority.initialize());
        const auto spawned = authority.spawnCharacter(
            airborneSpawn(
                106u, {0.0, 0.30, 0.0},
                glm::vec3(0.0f),
                WreckwaterCharacterMode::Swimming));
        ASSERT_TRUE(spawned);
        const auto neutral = characterInput(
            1u, 0u, spawned.handle, 106u);
        const WreckwaterCharacterState prior =
            *authority.character(spawned.handle);
        const auto stepped = wreckwaterCharacterStep({
            .config = authority.config(),
            .priorState = prior,
            .input = neutral,
            .previousPlatforms = {},
            .currentPlatforms = {},
            .tick = 1u,
        });
        const auto authoritative =
            authority.closeExactTick(1u, {});
        expectAuthorityParity(
            stepped, authoritative, authority, spawned.handle);
        ASSERT_TRUE(stepped.transition.has_value());
        EXPECT_EQ(
            stepped.transition->from,
            WreckwaterCharacterMode::Swimming);
        EXPECT_EQ(
            stepped.transition->to,
            WreckwaterCharacterMode::Airborne);
    }
}

TEST(WreckwaterCharacterStep,
     RejectsMalformedBoundaryInputsWithoutMutatingPriorState) {
    const auto config =
        WreckwaterCharacterMovementAuthority::Config{};
    WreckwaterCharacterState prior;
    prior.handle = physics::makeCharacterHandle(0u, 1u);
    prior.playerId = 107u;
    prior.connectionGeneration = 1u;
    prior.mode = WreckwaterCharacterMode::Airborne;
    prior.feetPosition =
        physics::worldPositionFromAbsolute({0.0, 2.0, 0.0});
    prior.active = true;
    prior.connected = true;
    auto valid = characterInput(
        1u, 1u, prior.handle, prior.playerId);

    const auto expectRejected = [&](const auto& step) {
        const auto result = wreckwaterCharacterStep(step);
        EXPECT_EQ(
            result.status,
            WreckwaterCharacterStatus::InvalidInput);
        EXPECT_EQ(result.nextState, prior);
        EXPECT_FALSE(result.transition.has_value());
    };

    auto wrongIdentity = valid;
    wrongIdentity.character =
        physics::makeCharacterHandle(1u, 1u);
    expectRejected(WreckwaterCharacterStepInput{
        .config = config,
        .priorState = prior,
        .input = wrongIdentity,
        .previousPlatforms = {},
        .currentPlatforms = {},
        .tick = 1u,
    });

    auto nonFinite = valid;
    nonFinite.move.x =
        std::numeric_limits<float>::quiet_NaN();
    expectRejected(WreckwaterCharacterStepInput{
        .config = config,
        .priorState = prior,
        .input = nonFinite,
        .previousPlatforms = {},
        .currentPlatforms = {},
        .tick = 1u,
    });

    auto activeNeutral = valid;
    activeNeutral.characterInputSequence = 0u;
    activeNeutral.jump = true;
    expectRejected(WreckwaterCharacterStepInput{
        .config = config,
        .priorState = prior,
        .input = activeNeutral,
        .previousPlatforms = {},
        .currentPlatforms = {},
        .tick = 1u,
    });

    std::array<WreckwaterCharacterPlatformSample, 3u>
        tooManyPlatforms{};
    expectRejected(WreckwaterCharacterStepInput{
        .config = config,
        .priorState = prior,
        .input = valid,
        .previousPlatforms = {},
        .currentPlatforms = tooManyPlatforms,
        .tick = 1u,
    });

    auto unsafeDeck = platform(
        1u, {107u, 1u}, {0.0, 0.0, 0.0});
    unsafeDeck.deckHalfExtents.x = 0.1f;
    expectRejected(WreckwaterCharacterStepInput{
        .config = config,
        .priorState = prior,
        .input = valid,
        .previousPlatforms = {},
        .currentPlatforms = std::span(&unsafeDeck, 1u),
        .tick = 1u,
    });

    WreckwaterCharacterState edgePrior = prior;
    edgePrior.feetPosition.sector.x =
        std::numeric_limits<int32_t>::max();
    edgePrior.feetPosition.local.x =
        physics::kWorldSectorHalf - 0.01f;
    edgePrior.worldVelocity.x = 100.0f;
    auto edgeInput = valid;
    const auto overflowed = wreckwaterCharacterStep({
        .config = config,
        .priorState = edgePrior,
        .input = edgeInput,
        .previousPlatforms = {},
        .currentPlatforms = {},
        .tick = 1u,
    });
    EXPECT_EQ(
        overflowed.status,
        WreckwaterCharacterStatus::StateOutOfRange);
    EXPECT_EQ(overflowed.nextState, edgePrior);
    EXPECT_FALSE(overflowed.transition.has_value());
}

} // namespace
} // namespace voxy::game
