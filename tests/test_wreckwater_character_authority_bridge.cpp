#include <gtest/gtest.h>

#include "game/wreckwater_character_authority_bridge.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace voxy::game {
namespace {

constexpr physics::BodyHandle kFirstSkiffBody{101u, 3u};
constexpr physics::BodyHandle kSecondSkiffBody{102u, 5u};

WreckwaterCharacterPlatformSample makePlatform(
    SkiffId skiffId, physics::BodyHandle body,
    const glm::dvec3& position) {
    return {
        .skiffId = skiffId,
        .skiffGeneration = 1u,
        .body = body,
        .position = physics::worldPositionFromAbsolute(position),
        .orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        .linearVelocity = glm::vec3(0.0f),
        .angularVelocity = glm::vec3(0.0f),
        .deckHalfExtents = glm::vec2(8.0f, 5.0f),
        .deckLocalHeight = 0.0f,
    };
}

std::array<
    WreckwaterCharacterPlatformSample,
    kWreckwaterMaximumCharacterPlatforms>
makePlatforms() {
    return {
        makePlatform(1u, kFirstSkiffBody, {-12.0, 2.0, 0.0}),
        makePlatform(2u, kSecondSkiffBody, {12.0, 2.0, 0.0}),
    };
}

WreckwaterCharacterSpawn aboardSpawn(
    PlayerId playerId,
    const WreckwaterCharacterPlatformSample& platform,
    const glm::vec3& localFeet) {
    return {
        .playerId = playerId,
        .connectionGeneration = 1u,
        .mode = WreckwaterCharacterMode::OnSkiff,
        .feetPosition = physics::worldPositionFromAbsolute(
            physics::worldPositionToAbsolute(platform.position)
            + glm::dvec3(platform.orientation * localFeet)),
        .worldVelocity = glm::vec3(0.0f),
        .skiffId = platform.skiffId,
        .skiffGeneration = platform.skiffGeneration,
        .skiffBody = platform.body,
        .skiffLocalFeetPosition = localFeet,
        .skiffLocalVelocity = glm::vec3(0.0f),
    };
}

std::array<
    WreckwaterCharacterSpawn,
    kWreckwaterMaximumCharacters>
makeRoster() {
    const auto platforms = makePlatforms();
    // Deliberately not in player order. Initialization canonicalizes it.
    return {
        aboardSpawn(40u, platforms[1], {1.0f, 0.0f, 0.0f}),
        aboardSpawn(20u, platforms[0], {1.0f, 0.0f, 0.0f}),
        aboardSpawn(30u, platforms[1], {-1.0f, 0.0f, 0.0f}),
        aboardSpawn(10u, platforms[0], {-1.0f, 0.0f, 0.0f}),
    };
}

WreckwaterCharacterCertifiedPlatformTick makeFrame(uint64_t tick) {
    return {
        .tick = tick,
        .platforms = makePlatforms(),
        .platformCount =
            static_cast<uint32_t>(
                kWreckwaterMaximumCharacterPlatforms),
        .overflow = false,
    };
}

WreckwaterCharacterInput makeInput(
    const WreckwaterCharacterAuthorityBridge& bridge,
    PlayerId playerId, uint64_t tick, uint64_t sequence,
    const glm::vec2& move = glm::vec2(0.0f),
    uint32_t connectionGeneration = 1u) {
    const auto* player = bridge.player(playerId);
    EXPECT_NE(player, nullptr);
    return {
        .targetTick = tick,
        .characterInputSequence = sequence,
        .character = player != nullptr
            ? player->character
            : kInvalidWreckwaterCharacter,
        .playerId = playerId,
        .connectionGeneration = connectionGeneration,
        .move = move,
    };
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    RequiresFixedRosterAndOwnsStableBoundedStorage) {
    WreckwaterCharacterAuthorityBridge bridge;
    auto config = WreckwaterCharacterAuthorityBridge::Config{};
    config.maximumReadbackLagTicks = 0u;
    EXPECT_FALSE(bridge.initialize(config, makeRoster()));
    config.maximumReadbackLagTicks =
        kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks
        + 1u;
    EXPECT_FALSE(bridge.initialize(config, makeRoster()));
    config = {};
    config.movement.maximumCharacters =
        kWreckwaterMaximumCharacters - 1u;
    EXPECT_FALSE(bridge.initialize(config, makeRoster()));

    auto duplicateRoster = makeRoster();
    duplicateRoster[0].playerId = duplicateRoster[1].playerId;
    EXPECT_FALSE(bridge.initialize(duplicateRoster));
    EXPECT_FALSE(bridge.initialized());

    ASSERT_TRUE(bridge.initialize(makeRoster()));
    ASSERT_TRUE(bridge.initialized());
    ASSERT_EQ(bridge.players().size(), 4u);
    EXPECT_TRUE(std::is_sorted(
        bridge.players().begin(), bridge.players().end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.playerId < rhs.playerId;
        }));
    EXPECT_EQ(bridge.players()[0].playerId, 10u);
    EXPECT_EQ(bridge.players()[3].playerId, 40u);

    const auto storage = bridge.storageState();
    EXPECT_NE(storage.players, nullptr);
    EXPECT_NE(storage.inputHistory, nullptr);
    EXPECT_NE(storage.transitions, nullptr);
    EXPECT_EQ(storage.playerCapacity, 4u);
    EXPECT_EQ(
        storage.inputCapacityPerPlayer,
        kWreckwaterCharacterAuthorityBridgeInputRingSlots);
    EXPECT_EQ(storage.transitionCapacity, 4u);
    EXPECT_NE(storage.authority.characters, nullptr);
    EXPECT_NE(storage.authority.pendingInputs, nullptr);

    for (uint64_t tick = 1u; tick <= 2'000u; ++tick) {
        for (const auto& player : bridge.players()) {
            ASSERT_EQ(
                bridge.submitInput(makeInput(
                    bridge, player.playerId, tick, tick)),
                WreckwaterCharacterAuthorityBridgeStatus::Accepted);
        }
        const auto closed =
            bridge.closeCertifiedPlatformTick(makeFrame(tick));
        ASSERT_TRUE(closed)
            << wreckwaterCharacterAuthorityBridgeStatusName(
                   closed.status)
            << " / "
            << wreckwaterCharacterStatusName(
                   closed.movementStatus);
        EXPECT_EQ(closed.appliedInputCount, 4u);
    }
    EXPECT_EQ(bridge.storageState(), storage);
    EXPECT_EQ(bridge.state().lastCertifiedTick, 2'000u);
    EXPECT_EQ(bridge.telemetry().inputPacketsAccepted, 8'000u);
    EXPECT_EQ(bridge.telemetry().inputsApplied, 8'000u);
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    LagSixteenWindowHoldsEveryPlayerTickAndRejectsFuture) {
    WreckwaterCharacterAuthorityBridge bridge;
    ASSERT_TRUE(bridge.initialize(makeRoster()));
    const uint64_t unstagedAuthorityHash =
        bridge.authority().stateHash();

    for (uint64_t tick = 1u;
         tick
         <= kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks;
         ++tick) {
        for (const auto& player : bridge.players()) {
            ASSERT_EQ(
                bridge.submitInput(makeInput(
                    bridge, player.playerId, tick, tick)),
                WreckwaterCharacterAuthorityBridgeStatus::Accepted);
        }
    }
    EXPECT_EQ(bridge.state().bufferedInputCount, 64u);
    EXPECT_EQ(bridge.telemetry().maximumBufferedInputs, 64u);
    EXPECT_EQ(
        bridge.authority().stateHash(), unstagedAuthorityHash);
    for (const auto& player : bridge.players()) {
        EXPECT_EQ(player.bufferedInputCount, 16u);
        EXPECT_EQ(player.oldestBufferedTick, 1u);
        EXPECT_EQ(player.latestBufferedTick, 16u);
        const auto* character =
            bridge.authority().character(player.character);
        ASSERT_NE(character, nullptr);
        EXPECT_EQ(character->latestCharacterInputSequence, 0u);
    }

    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 17u, 17u)),
        WreckwaterCharacterAuthorityBridgeStatus::
            InputFutureWindowExceeded);
    const uint64_t retainedHash = bridge.state().stateHash;
    for (uint64_t tick = 1u; tick <= 16u; ++tick) {
        const auto closed =
            bridge.closeCertifiedPlatformTick(makeFrame(tick));
        ASSERT_TRUE(closed);
        EXPECT_EQ(closed.appliedInputCount, 4u);
    }
    EXPECT_EQ(bridge.state().bufferedInputCount, 0u);
    EXPECT_NE(bridge.state().stateHash, retainedHash);
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    SameTickHighestSequenceConvergesAcrossArrivalOrder) {
    WreckwaterCharacterAuthorityBridge first;
    WreckwaterCharacterAuthorityBridge second;
    ASSERT_TRUE(first.initialize(makeRoster()));
    ASSERT_TRUE(second.initialize(makeRoster()));

    ASSERT_EQ(
        first.submitInput(makeInput(
            first, 10u, 1u, 2u, {0.2f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    ASSERT_EQ(
        first.submitInput(makeInput(
            first, 10u, 1u, 7u, {0.7f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    EXPECT_EQ(
        first.submitInput(makeInput(
            first, 10u, 1u, 5u, {0.5f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::
            IgnoredLowerSequence);

    ASSERT_EQ(
        second.submitInput(makeInput(
            second, 10u, 1u, 5u, {0.5f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    EXPECT_EQ(
        second.submitInput(makeInput(
            second, 10u, 1u, 2u, {0.2f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::
            IgnoredLowerSequence);
    ASSERT_EQ(
        second.submitInput(makeInput(
            second, 10u, 1u, 7u, {0.7f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    EXPECT_EQ(first.state().stateHash, second.state().stateHash);

    const uint64_t beforeReplay = first.state().stateHash;
    EXPECT_EQ(
        first.submitInput(makeInput(
            first, 10u, 1u, 7u, {0.1f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::
            ReplayedInputSequence);
    EXPECT_EQ(first.state().stateHash, beforeReplay);

    auto reverseFrame = makeFrame(1u);
    std::swap(reverseFrame.platforms[0], reverseFrame.platforms[1]);
    ASSERT_TRUE(first.closeCertifiedPlatformTick(makeFrame(1u)));
    ASSERT_TRUE(second.closeCertifiedPlatformTick(reverseFrame));
    EXPECT_EQ(first.state().stateHash, second.state().stateHash);
    const auto* firstCharacter = first.authority().character(
        first.player(10u)->character);
    const auto* secondCharacter = second.authority().character(
        second.player(10u)->character);
    ASSERT_NE(firstCharacter, nullptr);
    ASSERT_NE(secondCharacter, nullptr);
    EXPECT_EQ(
        firstCharacter->lastAppliedCharacterInputSequence, 7u);
    EXPECT_EQ(*firstCharacter, *secondCharacter);
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    SequencePoisoningAndCrossTickReplaysDoNotMutateState) {
    WreckwaterCharacterAuthorityBridge bridge;
    ASSERT_TRUE(bridge.initialize(makeRoster()));

    const uint64_t initialHash = bridge.state().stateHash;
    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u,
            std::numeric_limits<uint64_t>::max())),
        WreckwaterCharacterAuthorityBridgeStatus::
            InputSequenceExhausted);
    EXPECT_EQ(bridge.state().stateHash, initialHash);
    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 1'025u)),
        WreckwaterCharacterAuthorityBridgeStatus::
            InputSequenceJumpTooLarge);
    EXPECT_EQ(bridge.state().stateHash, initialHash);

    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 2u, 10u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 3u, 10u)),
        WreckwaterCharacterAuthorityBridgeStatus::
            ReplayedInputSequence);
    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 5u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    const uint64_t orderedHash = bridge.state().stateHash;
    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 11u)),
        WreckwaterCharacterAuthorityBridgeStatus::
            InputSequenceJumpTooLarge);
    EXPECT_EQ(bridge.state().stateHash, orderedHash);

    ASSERT_TRUE(bridge.closeCertifiedPlatformTick(makeFrame(1u)));
    ASSERT_TRUE(bridge.closeCertifiedPlatformTick(makeFrame(2u)));
    const auto* character = bridge.authority().character(
        bridge.player(10u)->character);
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(
        character->lastAppliedCharacterInputSequence, 10u);
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    DisconnectReconnectFencesGenerationAndPurgesBufferedInput) {
    WreckwaterCharacterAuthorityBridge bridge;
    ASSERT_TRUE(bridge.initialize(makeRoster()));
    const auto* originalPlayer = bridge.player(10u);
    ASSERT_NE(originalPlayer, nullptr);
    const WreckwaterCharacterHandle handle =
        originalPlayer->character;
    const auto storage = bridge.storageState();

    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 4u, 1u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    ASSERT_EQ(bridge.state().bufferedInputCount, 1u);
    ASSERT_EQ(
        bridge.disconnectCharacter(handle, 10u, 1u),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    EXPECT_EQ(bridge.state().bufferedInputCount, 0u);
    ASSERT_EQ(bridge.lastTransitions().size(), 1u);
    EXPECT_EQ(
        bridge.lastTransitions()[0].kind,
        WreckwaterCharacterAuthorityBridgeTransitionKind::
            Disconnected);
    EXPECT_FALSE(bridge.player(10u)->connected);

    const uint64_t disconnectedHash = bridge.state().stateHash;
    EXPECT_EQ(
        bridge.disconnectCharacter(handle, 10u, 2u),
        WreckwaterCharacterAuthorityBridgeStatus::
            StaleConnectionGeneration);
    EXPECT_EQ(bridge.state().stateHash, disconnectedHash);
    EXPECT_EQ(
        bridge.reconnectCharacter(handle, 10u, 1u, 3u),
        WreckwaterCharacterAuthorityBridgeStatus::InvalidInput);
    EXPECT_EQ(bridge.state().stateHash, disconnectedHash);
    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 1u)),
        WreckwaterCharacterAuthorityBridgeStatus::Disconnected);

    ASSERT_EQ(
        bridge.reconnectCharacter(handle, 10u, 1u, 2u),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    ASSERT_EQ(bridge.lastTransitions().size(), 1u);
    EXPECT_EQ(
        bridge.lastTransitions()[0].kind,
        WreckwaterCharacterAuthorityBridgeTransitionKind::
            Reconnected);
    EXPECT_EQ(
        bridge.lastTransitions()[0].priorConnectionGeneration, 1u);
    EXPECT_EQ(
        bridge.lastTransitions()[0].nextConnectionGeneration, 2u);
    EXPECT_TRUE(bridge.player(10u)->connected);
    EXPECT_EQ(bridge.player(10u)->connectionGeneration, 2u);

    EXPECT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 1u, {}, 1u)),
        WreckwaterCharacterAuthorityBridgeStatus::
            StaleConnectionGeneration);
    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 1u, {}, 2u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    ASSERT_TRUE(bridge.closeCertifiedPlatformTick(makeFrame(1u)));
    const auto* character = bridge.authority().character(handle);
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->connectionGeneration, 2u);
    EXPECT_EQ(
        character->lastAppliedCharacterInputSequence, 1u);
    EXPECT_EQ(bridge.storageState(), storage);
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    PoseGapOverflowAndTeleportAreRejectedAtomically) {
    WreckwaterCharacterAuthorityBridge bridge;
    ASSERT_TRUE(bridge.initialize(makeRoster()));
    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 2u, 1u, {0.5f, 0.0f})),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    const uint64_t bufferedHash = bridge.state().stateHash;
    const auto storage = bridge.storageState();

    auto frame = makeFrame(2u);
    EXPECT_EQ(
        bridge.closeCertifiedPlatformTick(frame).status,
        WreckwaterCharacterAuthorityBridgeStatus::PoseTickGap);
    EXPECT_EQ(bridge.state().stateHash, bufferedHash);
    frame = makeFrame(1u);
    frame.overflow = true;
    EXPECT_EQ(
        bridge.closeCertifiedPlatformTick(frame).status,
        WreckwaterCharacterAuthorityBridgeStatus::
            PoseReadbackOverflow);
    EXPECT_EQ(bridge.state().stateHash, bufferedHash);
    frame = makeFrame(1u);
    frame.platformCount = 1u;
    EXPECT_EQ(
        bridge.closeCertifiedPlatformTick(frame).status,
        WreckwaterCharacterAuthorityBridgeStatus::
            PosePlatformCountMismatch);
    EXPECT_EQ(bridge.state().stateHash, bufferedHash);
    EXPECT_EQ(bridge.storageState(), storage);

    ASSERT_TRUE(bridge.closeCertifiedPlatformTick(makeFrame(1u)));
    const uint64_t beforeTeleport = bridge.state().stateHash;
    frame = makeFrame(2u);
    frame.platforms[0].position =
        physics::worldPositionFromAbsolute({500.0, 2.0, 0.0});
    const auto rejected =
        bridge.closeCertifiedPlatformTick(frame);
    EXPECT_EQ(
        rejected.status,
        WreckwaterCharacterAuthorityBridgeStatus::PoseRejected);
    EXPECT_EQ(
        rejected.movementStatus,
        WreckwaterCharacterStatus::PlatformMotionDiscontinuity);
    EXPECT_EQ(bridge.state().stateHash, beforeTeleport);
    EXPECT_EQ(bridge.state().bufferedInputCount, 1u);
    EXPECT_EQ(bridge.authority().lastClosedTick(), 1u);

    const auto recovered =
        bridge.closeCertifiedPlatformTick(makeFrame(2u));
    ASSERT_TRUE(recovered);
    EXPECT_EQ(recovered.appliedInputCount, 1u);
    EXPECT_EQ(bridge.state().bufferedInputCount, 0u);
    EXPECT_EQ(
        bridge.closeCertifiedPlatformTick(makeFrame(2u)).status,
        WreckwaterCharacterAuthorityBridgeStatus::StalePoseTick);
}

TEST(
    WreckwaterCharacterAuthorityBridge,
    RingWrapDoesNotAliasRetainedFutureInput) {
    WreckwaterCharacterAuthorityBridge bridge;
    ASSERT_TRUE(bridge.initialize(makeRoster()));
    const auto storage = bridge.storageState();

    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 1u, 1u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 16u, 2u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    ASSERT_TRUE(bridge.closeCertifiedPlatformTick(makeFrame(1u)));
    ASSERT_TRUE(bridge.closeCertifiedPlatformTick(makeFrame(2u)));

    // 1 and 18 share a physical ring slot. Tick 1 was consumed, while tick
    // 16 remains live in a different slot.
    ASSERT_EQ(
        bridge.submitInput(makeInput(
            bridge, 10u, 18u, 3u)),
        WreckwaterCharacterAuthorityBridgeStatus::Accepted);
    EXPECT_EQ(bridge.player(10u)->bufferedInputCount, 2u);
    EXPECT_EQ(bridge.player(10u)->oldestBufferedTick, 16u);
    EXPECT_EQ(bridge.player(10u)->latestBufferedTick, 18u);

    for (uint64_t tick = 3u; tick <= 18u; ++tick) {
        ASSERT_TRUE(
            bridge.closeCertifiedPlatformTick(makeFrame(tick)));
    }
    const auto* character = bridge.authority().character(
        bridge.player(10u)->character);
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(
        character->lastAppliedCharacterInputSequence, 3u);
    EXPECT_EQ(bridge.state().bufferedInputCount, 0u);
    EXPECT_EQ(bridge.storageState(), storage);
}

} // namespace
} // namespace voxy::game
