#include "client/wreckwater_client_probe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace voxy::client {
namespace {

network::WreckwaterClientSample makeSample(
    uint64_t sequence, uint64_t applicationTick,
    network::WreckwaterCargoDisposition disposition,
    network::WreckwaterCrew owner, uint32_t revision) {
    network::WreckwaterClientSample sample;
    sample.authoritative.snapshotSequence = sequence;
    sample.authoritative.applicationTick = applicationTick;
    sample.authoritative.physicsEvidenceTick =
        applicationTick > 1u ? applicationTick - 1u : 1u;
    sample.authoritative.phase = network::WreckwaterPhase::Live;
    sample.authoritative.crewOneScore = 10u;
    sample.authoritative.crewTwoScore = 20u;
    sample.authoritative.matchStateHash = 30u;
    sample.authoritative.eventStreamHash = 40u;
    sample.authoritative.serializedByteHash = 50u;
    sample.entityCount = 1u;
    network::WreckwaterEntityState& cargo =
        sample.entities[0].authoritativeState;
    cargo.netEntityId = 3u;
    cargo.netGeneration = 1u;
    cargo.kind = network::WreckwaterEntityKind::Cargo;
    cargo.cargo = {
        .cargoId = 1u,
        .generation = 1u,
        .revision = revision,
        .disposition = disposition,
        .ownerCrew = owner,
        .towingSkiffId =
            disposition
                    == network::WreckwaterCargoDisposition::Towed
                ? (owner == network::WreckwaterCrew::CrewOne
                        ? 1u : 2u)
                : 0u,
        .towingSkiffGeneration =
            disposition
                    == network::WreckwaterCargoDisposition::Towed
                ? 1u : 0u,
    };
    return sample;
}

void addCharacterRoster(
    network::WreckwaterClientSample& sample,
    uint32_t localGeneration = 1u,
    uint32_t generationPlayer = 0u) {
    sample.characterCount = kWreckwaterProbePeerCount;
    for (uint32_t index = 0u;
         index < sample.characterCount; ++index) {
        const uint32_t playerId = index + 1u;
        network::WreckwaterSampledCharacter& sampled =
            sample.characters[index];
        sampled.characterHandle = 100u + playerId;
        sampled.authoritativeState = {
            .characterHandle = 100u + playerId,
            .stateFlags =
                static_cast<uint32_t>(
                    network::WreckwaterCharacterMode::OnSkiff)
                | network::kWreckwaterCharacterStateActiveFlag
                | network::
                    kWreckwaterCharacterStateConnectedFlag,
            .playerId = playerId,
            .connectionGeneration =
                playerId == generationPlayer
                ? localGeneration : 1u,
            .sector = {0, 0, 0},
            .localFeetPosition = {
                static_cast<float>(playerId), 1.0f, 0.0f},
            .skiffId = playerId <= 2u ? 1u : 2u,
            .skiffGeneration = 1u,
            .skiffLocalFeetPosition = {
                static_cast<float>(playerId), 1.0f, 0.0f},
        };
    }
}

TEST(
    WreckwaterClientProbeOptionsTest,
    ParsesStrictCompleteConfigurationWithoutRetainingHexText) {
    const std::string key =
        "0102030405060708090a0b0c0d0e0f10"
        "1112131415161718191A1B1C1D1E1FA0";
    const std::array<std::string_view, 24u> arguments{
        "--server", "127.0.0.1",
        "--port", "4567",
        "--peer", "4",
        "--key", key,
        "--session", "11",
        "--match", "12",
        "--world", "13",
        "--world-epoch", "14",
        "--authority-epoch", "15",
        "--max-ticks", "600",
        "--reconnect-at", "300",
        "--chaos-seed", "18446744073709551615",
    };
    WreckwaterClientProbeOptions options;
    const WreckwaterClientProbeOptionResult parsed =
        parseWreckwaterClientProbeOptions(arguments, options);
    ASSERT_TRUE(parsed)
        << wreckwaterClientProbeOptionErrorName(parsed.error);
    EXPECT_EQ(options.serverAddress, "127.0.0.1");
    EXPECT_EQ(options.serverPort, 4567u);
    EXPECT_EQ(options.peerId, 4u);
    EXPECT_EQ(options.sessionId, 11u);
    EXPECT_EQ(options.matchId, 12u);
    EXPECT_EQ(options.worldId, 13u);
    EXPECT_EQ(options.worldEpoch, 14u);
    EXPECT_EQ(options.authorityEpoch, 15u);
    EXPECT_EQ(options.maximumPumpTicks, 600u);
    EXPECT_EQ(options.reconnectAtPumpTick, 300u);
    EXPECT_EQ(
        options.chaosSeed,
        std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
        std::to_integer<uint8_t>(options.authenticationKey.front()),
        0x01u);
    EXPECT_EQ(
        std::to_integer<uint8_t>(options.authenticationKey.back()),
        0xa0u);
}

TEST(
    WreckwaterClientProbeOptionsTest,
    RejectsMissingDuplicateAndMalformedAuthorityInputs) {
    WreckwaterClientProbeOptions options;
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions({}, options).error,
        WreckwaterClientProbeOptionError::MissingPeerId);

    const std::string validKey(63u, 'a');
    const std::array<std::string_view, 4u> shortKey{
        "--peer", "1", "--key", validKey};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(shortKey, options).error,
        WreckwaterClientProbeOptionError::InvalidAuthenticationKey);

    const std::string zeroKey(64u, '0');
    const std::array<std::string_view, 4u> allZero{
        "--peer", "1", "--key", zeroKey};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(allZero, options).error,
        WreckwaterClientProbeOptionError::InvalidAuthenticationKey);

    const std::string goodKey =
        std::string(63u, '0') + "1";
    const std::array<std::string_view, 6u> duplicate{
        "--peer", "1", "--peer", "2", "--key", goodKey};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(duplicate, options).error,
        WreckwaterClientProbeOptionError::DuplicateOption);

    const std::array<std::string_view, 4u> badPeer{
        "--peer", "5", "--key", goodKey};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(badPeer, options).error,
        WreckwaterClientProbeOptionError::InvalidPeerId);

    const std::array<std::string_view, 6u> missingKey{
        "--peer", "1", "--session", "1", "--world", "1"};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(missingKey, options).error,
        WreckwaterClientProbeOptionError::MissingAuthenticationKey);

    const std::array<std::string_view, 6u> zeroIdentity{
        "--peer", "1", "--key", goodKey, "--match", "0"};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(zeroIdentity, options).error,
        WreckwaterClientProbeOptionError::InvalidIdentity);

    const std::array<std::string_view, 8u> lateReconnect{
        "--peer", "1", "--key", goodKey,
        "--max-ticks", "20", "--reconnect-at", "20"};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(lateReconnect, options).error,
        WreckwaterClientProbeOptionError::InvalidReconnectTick);

    const std::array<std::string_view, 6u> badChaosSeed{
        "--peer", "1", "--key", goodKey,
        "--chaos-seed", "-1"};
    EXPECT_EQ(
        parseWreckwaterClientProbeOptions(
            badChaosSeed, options).error,
        WreckwaterClientProbeOptionError::InvalidChaosSeed);
}

TEST(WreckwaterClientProbeOptionsTest, HelpNeedsNoSecretOrPeer) {
    const std::array<std::string_view, 1u> arguments{"--help"};
    WreckwaterClientProbeOptions options;
    ASSERT_TRUE(parseWreckwaterClientProbeOptions(arguments, options));
    EXPECT_TRUE(options.help);
    EXPECT_EQ(options.peerId, 0u);
}

TEST(
    WreckwaterClientProbeScriptTest,
    EmitsNoAuthorityActionsDuringWarmupOrFinishedPhases) {
    WreckwaterClientProbeScript helm(1u);
    WreckwaterClientProbeScript deck(2u);
    network::WreckwaterClientSample sample = makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    sample.authoritative.phase = network::WreckwaterPhase::Warmup;
    EXPECT_FALSE(helm.plan(sample).has_value());
    EXPECT_FALSE(deck.plan(sample).has_value());
    sample.authoritative.phase = network::WreckwaterPhase::Finished;
    EXPECT_FALSE(helm.plan(sample).has_value());
    EXPECT_FALSE(deck.plan(sample).has_value());
}

TEST(
    WreckwaterClientProbeSnapshotLogGateTest,
    BoundsSteadySnapshotsButKeepsHeartbeatAndLogicalTransitions) {
    WreckwaterClientProbeSnapshotLogGate gate;
    network::WreckwaterClientSample sample = makeSample(
        1u, 3u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    EXPECT_TRUE(gate.shouldLog(sample));

    sample.authoritative.snapshotSequence = 2u;
    sample.authoritative.applicationTick = 6u;
    sample.authoritative.matchStateHash += 1u;
    sample.authoritative.eventStreamHash += 1u;
    sample.authoritative.serializedByteHash += 1u;
    EXPECT_FALSE(gate.shouldLog(sample))
        << "Hash churn alone must not restore 20 Hz logging";

    sample.authoritative.snapshotSequence = 21u;
    sample.authoritative.applicationTick = 63u;
    EXPECT_TRUE(gate.shouldLog(sample))
        << "The simulated one-second heartbeat must remain visible";

    sample.authoritative.snapshotSequence = 22u;
    sample.authoritative.applicationTick = 66u;
    sample.entities[0].authoritativeState.cargo.revision = 2u;
    sample.entities[0].authoritativeState.cargo.disposition =
        network::WreckwaterCargoDisposition::Towed;
    sample.entities[0].authoritativeState.cargo.ownerCrew =
        network::WreckwaterCrew::CrewOne;
    EXPECT_TRUE(gate.shouldLog(sample));

    sample.authoritative.snapshotSequence = 23u;
    sample.authoritative.applicationTick = 69u;
    sample.authoritative.crewOneScore += 1u;
    EXPECT_TRUE(gate.shouldLog(sample));

    sample.authoritative.snapshotSequence = 24u;
    sample.authoritative.applicationTick = 72u;
    sample.authoritative.phase =
        network::WreckwaterPhase::Overtime;
    sample.authoritative.outcome =
        network::WreckwaterOutcomeType::CrewVictory;
    sample.authoritative.winner = network::WreckwaterCrew::CrewOne;
    EXPECT_TRUE(gate.shouldLog(sample));
}

TEST(
    WreckwaterClientProbeSnapshotLogGateTest,
    FourMinuteSteadyMatchProducesAtMostOneHeartbeatPerSecond) {
    WreckwaterClientProbeSnapshotLogGate gate;
    uint32_t logCount = 0u;
    constexpr uint64_t snapshotCount = 4u * 60u * 20u;
    for (uint64_t sequence = 1u;
         sequence <= snapshotCount; ++sequence) {
        network::WreckwaterClientSample sample = makeSample(
            sequence, sequence * 3u,
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u);
        sample.authoritative.matchStateHash +=
            static_cast<uint32_t>(sequence);
        sample.authoritative.eventStreamHash +=
            static_cast<uint32_t>(sequence);
        sample.authoritative.serializedByteHash += sequence;
        if (gate.shouldLog(sample)) ++logCount;
    }
    EXPECT_LE(logCount, 4u * 60u + 1u);
    EXPECT_GE(logCount, 4u * 60u);
}

TEST(
    WreckwaterClientProbeScriptTest,
    HelmPlanIsDeterministicBoundedAndRetryStable) {
    WreckwaterClientProbeScript script(1u);
    const network::WreckwaterClientSample sample = makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    const auto first = script.plan(sample);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->action, network::WreckwaterAction::Helm);
    EXPECT_EQ(first->requestedApplicationTick, 11u);
    EXPECT_NE(first->helmThrottleQ15, 0);
    EXPECT_NE(first->helmSteeringQ15, 0);
    EXPECT_EQ(script.plan(sample), first);
    EXPECT_FALSE(script.markSent({
        .action = network::WreckwaterAction::Helm,
        .requestedApplicationTick = 12u,
    }));
    ASSERT_TRUE(script.markSent(*first));
    EXPECT_EQ(script.lastSuccessfulRequestTick(), 11u);

    const auto second = script.plan(sample);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->requestedApplicationTick, 12u);
    ASSERT_TRUE(script.markSent(*second));
    ASSERT_TRUE(script.markSent(*script.plan(sample)));
    ASSERT_TRUE(script.markSent(*script.plan(sample)));
    EXPECT_FALSE(script.plan(sample).has_value())
        << "Helm must wait rather than exceed the snapshot lead bound";
}

TEST(
    WreckwaterClientProbeScriptTest,
    HelmQuiescesAfterCargoReachesTerminalDisposition) {
    WreckwaterClientProbeScript bankedScript(1u);
    const network::WreckwaterClientSample banked = makeSample(
        2u, 20u, network::WreckwaterCargoDisposition::Banked,
        network::WreckwaterCrew::CrewOne, 6u);
    EXPECT_FALSE(bankedScript.plan(banked).has_value());

    WreckwaterClientProbeScript lostScript(3u);
    const network::WreckwaterClientSample lost = makeSample(
        3u, 21u, network::WreckwaterCargoDisposition::Lost,
        network::WreckwaterCrew::None, 7u);
    EXPECT_FALSE(lostScript.plan(lost).has_value());
}

TEST(
    WreckwaterClientProbeScriptTest,
    DeckRolesDeriveAllCargoActionsFromSnapshotState) {
    WreckwaterClientProbeScript crewOneDeck(2u);
    auto action = crewOneDeck.plan(makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u));
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->action, network::WreckwaterAction::Tow);
    ASSERT_TRUE(crewOneDeck.markSent(*action));

    action = crewOneDeck.plan(makeSample(
        2u, 13u, network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewOne, 5u));
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->action, network::WreckwaterAction::Bank);
    ASSERT_TRUE(crewOneDeck.markSent(*action));

    action = crewOneDeck.plan(makeSample(
        3u, 16u, network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewTwo, 3u));
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->action, network::WreckwaterAction::Cut);

    WreckwaterClientProbeScript crewTwoDeck(4u);
    action = crewTwoDeck.plan(makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewOne, 2u));
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->action, network::WreckwaterAction::Steal);
    ASSERT_TRUE(crewTwoDeck.markSent(*action));

    action = crewTwoDeck.plan(makeSample(
        2u, 13u, network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewTwo, 6u));
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->action, network::WreckwaterAction::Bank);
}

TEST(
    WreckwaterClientProbeScriptTest,
    DeckRevisionTurnsNeverRaceTheSameCargoTransition) {
    WreckwaterClientProbeScript crewOneDeck(2u);
    WreckwaterClientProbeScript crewTwoDeck(4u);

    const network::WreckwaterClientSample firstCrewOneTow =
        makeSample(
            1u, 10u,
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewOne, 2u);
    EXPECT_FALSE(crewOneDeck.plan(firstCrewOneTow).has_value());
    const auto steal = crewTwoDeck.plan(firstCrewOneTow);
    ASSERT_TRUE(steal.has_value());
    EXPECT_EQ(steal->action, network::WreckwaterAction::Steal);

    WreckwaterClientProbeScript nextCrewOneDeck(2u);
    WreckwaterClientProbeScript nextCrewTwoDeck(4u);
    const network::WreckwaterClientSample secondCrewOneTow =
        makeSample(
            2u, 13u,
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewOne, 5u);
    const auto bank = nextCrewOneDeck.plan(secondCrewOneTow);
    ASSERT_TRUE(bank.has_value());
    EXPECT_EQ(bank->action, network::WreckwaterAction::Bank);
    EXPECT_FALSE(
        nextCrewTwoDeck.plan(secondCrewOneTow).has_value());

    WreckwaterClientProbeScript firstCrewTwoDeck(2u);
    WreckwaterClientProbeScript firstCrewTwoOpponent(4u);
    const network::WreckwaterClientSample firstCrewTwoTow =
        makeSample(
            3u, 16u,
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewTwo, 3u);
    const auto cut = firstCrewTwoDeck.plan(firstCrewTwoTow);
    ASSERT_TRUE(cut.has_value());
    EXPECT_EQ(cut->action, network::WreckwaterAction::Cut);
    EXPECT_FALSE(
        firstCrewTwoOpponent.plan(firstCrewTwoTow).has_value());
}

TEST(
    WreckwaterClientProbeScriptTest,
    ProofCargoSequenceExercisesAllCommandsWithoutARevisionRace) {
    WreckwaterClientProbeScript crewOneDeck(2u);
    WreckwaterClientProbeScript crewTwoDeck(4u);
    const auto expectTurn =
        [&crewOneDeck, &crewTwoDeck](
            const network::WreckwaterClientSample& sample,
            uint32_t expectedPeer,
            network::WreckwaterAction expectedAction) {
            const auto crewOne = crewOneDeck.plan(sample);
            const auto crewTwo = crewTwoDeck.plan(sample);
            if (expectedPeer == 2u) {
                ASSERT_TRUE(crewOne.has_value());
                EXPECT_EQ(crewOne->action, expectedAction);
                EXPECT_FALSE(crewTwo.has_value());
                EXPECT_TRUE(crewOneDeck.markSent(*crewOne));
            } else {
                ASSERT_TRUE(crewTwo.has_value());
                EXPECT_EQ(crewTwo->action, expectedAction);
                EXPECT_FALSE(crewOne.has_value());
                EXPECT_TRUE(crewTwoDeck.markSent(*crewTwo));
            }
        };

    expectTurn(
        makeSample(
            1u, 181u,
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u),
        2u, network::WreckwaterAction::Tow);
    expectTurn(
        makeSample(
            2u, 184u,
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewOne, 2u),
        4u, network::WreckwaterAction::Steal);
    expectTurn(
        makeSample(
            3u, 187u,
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewTwo, 3u),
        2u, network::WreckwaterAction::Cut);
    expectTurn(
        makeSample(
            4u, 190u,
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 4u),
        2u, network::WreckwaterAction::Tow);
    expectTurn(
        makeSample(
            5u, 193u,
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewOne, 5u),
        2u, network::WreckwaterAction::Bank);
}

TEST(
    WreckwaterClientProbeScriptTest,
    SameCargoRevisionRetriesOnlyAfterBoundedSnapshotGap) {
    WreckwaterClientProbeScript script(2u);
    auto action = script.plan(makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u));
    ASSERT_TRUE(action.has_value());
    ASSERT_TRUE(script.markSent(*action));
    for (uint64_t sequence = 2u;
         sequence < 1u + kWreckwaterProbeCargoRetrySnapshots;
         ++sequence) {
        EXPECT_FALSE(script.plan(makeSample(
            sequence, 10u + sequence,
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u)).has_value());
    }
    const uint64_t retrySequence =
        1u + kWreckwaterProbeCargoRetrySnapshots;
    action = script.plan(makeSample(
        retrySequence, 10u + retrySequence,
        network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u));
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->action, network::WreckwaterAction::Tow);
}

TEST(
    WreckwaterClientProbeCharacterScriptTest,
    PlansDeterministicMovementInItsOwnCertifiedTickDomain) {
    network::WreckwaterClientSample sample = makeSample(
        7u, 40u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    sample.authoritative.physicsEvidenceTick = 39u;
    addCharacterRoster(sample);

    const std::array<std::pair<int16_t, int16_t>, 4u> expected{{
        {kWreckwaterProbeCharacterMoveQ15, 0},
        {-kWreckwaterProbeCharacterMoveQ15, 0},
        {0, kWreckwaterProbeCharacterMoveQ15},
        {0, -kWreckwaterProbeCharacterMoveQ15},
    }};
    for (uint32_t peerId = 1u;
         peerId <= kWreckwaterProbePeerCount; ++peerId) {
        WreckwaterClientProbeCharacterScript script(peerId);
        ASSERT_TRUE(script.initialized());
        const auto input = script.plan(sample);
        ASSERT_TRUE(input.has_value());
        EXPECT_EQ(
            input->requestedApplicationTick,
            sample.authoritative.physicsEvidenceTick
                + kWreckwaterProbeCharacterTargetLeadTicks);
        EXPECT_EQ(input->moveXQ15, expected[peerId - 1u].first);
        EXPECT_EQ(input->moveZQ15, expected[peerId - 1u].second);
        EXPECT_EQ(input->sourceSnapshotSequence, 7u);
        EXPECT_EQ(input->sourceEvidenceTick, 39u);
        EXPECT_EQ(input->sourceConnectionGeneration, 1u);
        EXPECT_FALSE(input->jump);
        EXPECT_FALSE(input->board);
    }

    WreckwaterClientProbeScript actionScript(1u);
    const auto action = actionScript.plan(sample);
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(action->requestedApplicationTick, 41u);
    EXPECT_NE(
        action->requestedApplicationTick,
        sample.authoritative.physicsEvidenceTick
            + kWreckwaterProbeCharacterTargetLeadTicks);
}

TEST(
    WreckwaterClientProbeCharacterScriptTest,
    RetriesOneSnapshotInputStablyThenUsesTheNextSnapshot) {
    WreckwaterClientProbeCharacterScript script(1u);
    network::WreckwaterClientSample first = makeSample(
        10u, 60u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    first.authoritative.physicsEvidenceTick = 59u;
    addCharacterRoster(first);
    const auto input = script.plan(first);
    ASSERT_TRUE(input.has_value());
    EXPECT_EQ(script.plan(first), input);
    EXPECT_FALSE(script.markSent({
        .requestedApplicationTick =
            input->requestedApplicationTick + 1u,
    }));
    ASSERT_TRUE(script.markSent(*input));
    EXPECT_EQ(
        script.lastSuccessfulRequestTick(),
        input->requestedApplicationTick);
    EXPECT_FALSE(script.plan(first).has_value())
        << "One accepted snapshot emits at most one logical input";

    network::WreckwaterClientSample second = first;
    second.authoritative.snapshotSequence = 11u;
    second.authoritative.applicationTick = 63u;
    second.authoritative.physicsEvidenceTick = 62u;
    const auto next = script.plan(second);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(
        next->requestedApplicationTick,
        input->requestedApplicationTick + 3u);

    script.discardPending();
    EXPECT_FALSE(script.hasPendingInput());
}

TEST(
    WreckwaterClientProbeCharacterScriptTest,
    RequiresExactConnectedRosterAndTracksCertifiedGeneration) {
    WreckwaterClientProbeCharacterScript script(1u);
    network::WreckwaterClientSample sample = makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    EXPECT_FALSE(script.plan(sample).has_value());

    sample.authoritative.snapshotSequence = 2u;
    addCharacterRoster(sample, 2u, 1u);
    sample.characters[0].authoritativeState.stateFlags &=
        ~network::kWreckwaterCharacterStateConnectedFlag;
    EXPECT_FALSE(script.plan(sample).has_value());

    sample.authoritative.snapshotSequence = 3u;
    sample.characters[0].authoritativeState.stateFlags |=
        network::kWreckwaterCharacterStateConnectedFlag;
    const auto input = script.plan(sample);
    ASSERT_TRUE(input.has_value());
    EXPECT_EQ(input->sourceConnectionGeneration, 2u);

    WreckwaterClientProbeCharacterScript invalidPeer(5u);
    EXPECT_FALSE(invalidPeer.initialized());
    EXPECT_FALSE(invalidPeer.plan(sample).has_value());
}

TEST(
    WreckwaterClientProbeCharacterScriptTest,
    TargetLeadAcceptsTheLastProtocolTickWithoutOverflow) {
    EXPECT_GT(
        kWreckwaterProbeCharacterTargetLeadTicks,
        2u * kWreckwaterProbeChaosMaximumDelayServiceQuanta);
    EXPECT_LE(
        kWreckwaterProbeCharacterTargetLeadTicks
            - 2u
                * kWreckwaterProbeChaosMinimumDelayServiceQuanta,
        kWreckwaterProbeAuthorityCharacterLeadWindowTicks);

    network::WreckwaterClientSample sample = makeSample(
        1u, 1u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    addCharacterRoster(sample);
    sample.authoritative.physicsEvidenceTick =
        network::kWreckwaterMaximumApplicationTick
        - kWreckwaterProbeCharacterTargetLeadTicks;
    WreckwaterClientProbeCharacterScript boundary(4u);
    const auto atBoundary = boundary.plan(sample);
    ASSERT_TRUE(atBoundary.has_value());
    EXPECT_EQ(
        atBoundary->requestedApplicationTick,
        network::kWreckwaterMaximumApplicationTick);

    ++sample.authoritative.snapshotSequence;
    ++sample.authoritative.physicsEvidenceTick;
    WreckwaterClientProbeCharacterScript overflow(4u);
    EXPECT_FALSE(overflow.plan(sample).has_value());
}

TEST(
    WreckwaterClientProbeCharacterScriptTest,
    RosterHashIsCanonicalAndCoversAckAndFeetEvidence) {
    network::WreckwaterClientSample sample = makeSample(
        1u, 10u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u);
    addCharacterRoster(sample);
    ASSERT_TRUE(wreckwaterClientProbeHasExactCharacterRoster(sample));
    const uint64_t original =
        wreckwaterClientProbeCharacterRosterHash(sample);
    ASSERT_NE(original, 0u);

    std::swap(sample.characters[0], sample.characters[3]);
    EXPECT_EQ(
        wreckwaterClientProbeCharacterRosterHash(sample), original);
    sample.characters[0]
        .authoritativeState.lastAppliedCharacterInputSequence = 9u;
    EXPECT_NE(
        wreckwaterClientProbeCharacterRosterHash(sample), original);
    sample.characters[0]
        .authoritativeState.lastAppliedCharacterInputSequence = 0u;
    sample.characters[0]
        .authoritativeState.skiffLocalFeetPosition.x += 0.25f;
    EXPECT_NE(
        wreckwaterClientProbeCharacterRosterHash(sample), original);

    sample.characterCount = 3u;
    EXPECT_FALSE(wreckwaterClientProbeHasExactCharacterRoster(sample));
    EXPECT_EQ(wreckwaterClientProbeCharacterRosterHash(sample), 0u);
}

TEST(
    WreckwaterClientProbeScriptTest,
    FinalTupleCopiesOnlyCertifiedAuthoritativeFields) {
    network::WreckwaterClientSample sample = makeSample(
        9u, 25u, network::WreckwaterCargoDisposition::Banked,
        network::WreckwaterCrew::CrewOne, 4u);
    sample.authoritative.outcome =
        network::WreckwaterOutcomeType::CrewVictory;
    sample.authoritative.winner = network::WreckwaterCrew::CrewOne;
    const WreckwaterClientProbeFinalSnapshot final =
        wreckwaterClientProbeFinalSnapshot(sample);
    EXPECT_EQ(final.sequence, 9u);
    EXPECT_EQ(final.applicationTick, 25u);
    EXPECT_EQ(final.evidenceTick, 24u);
    EXPECT_EQ(final.crewOneScore, 10u);
    EXPECT_EQ(final.crewTwoScore, 20u);
    EXPECT_EQ(final.stateHash, 30u);
    EXPECT_EQ(final.eventHash, 40u);
    EXPECT_EQ(final.serializedHash, 50u);
    EXPECT_EQ(final.outcome, network::WreckwaterOutcomeType::CrewVictory);
    EXPECT_EQ(final.winner, network::WreckwaterCrew::CrewOne);
}

TEST(WreckwaterClientProbeScriptTest, RejectsUnknownPeerRole) {
    WreckwaterClientProbeScript script(5u);
    EXPECT_FALSE(script.initialized());
    EXPECT_FALSE(script.plan(makeSample(
        1u, 1u, network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u)).has_value());
}

} // namespace
} // namespace voxy::client
