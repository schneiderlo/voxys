#include <gtest/gtest.h>

#include "gameplay/structural_assembly.hpp"
#include "gameplay/platform_parity.hpp"
#include "gameplay/vertical_slices.hpp"
#include "gameplay/water_dynamics.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace voxy::gameplay {
namespace {

TEST(GameplayIntegerTest, RoundsSignedDivisionWithoutSignedShifts) {
    EXPECT_EQ(roundedDivide(5, 2), 3);
    EXPECT_EQ(roundedDivide(-5, 2), -3);
    EXPECT_EQ(roundedDivide(5, -2), -3);
    EXPECT_EQ(roundedDivide(-5, -2), 3);
    EXPECT_EQ(multiplyQ16(kScalarOne / 2, kScalarOne / 2),
              kScalarOne / 4);
}

TEST(GameplayIntegerTest, PlatformCorpusMatchesReviewedGolden) {
    const PlatformParityResult result = runPlatformParityCorpus();
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.aggregateHash, kExpectedPlatformParityHash);
}

TEST(StructuralAssemblyTest, CanonicalDamageProducesStableComponents) {
    const std::array<AssemblyNode, 4> nodes{{
        {1u, {0, 0, 0}, 4 * kScalarOne, 4 * kScalarOne,
         AssemblyNodeSealed},
        {2u, {kPositionOne, 0, 0}, 2 * kScalarOne, 2 * kScalarOne,
         AssemblyNodeSealed},
        {3u, {2 * kPositionOne, 0, 0}, 2 * kScalarOne, 2 * kScalarOne,
         AssemblyNodeSealed},
        {4u, {3 * kPositionOne, 0, 0}, kScalarOne / 4,
         kScalarOne / 4, 0u},
    }};
    const std::array<AssemblyEdge, 3> edges{{
        {10u, 1u, 2u, 2 * kScalarOne, 1u},
        {11u, 2u, 3u, 2 * kScalarOne, 1u},
        {12u, 3u, 4u, kScalarOne, 1u},
    }};
    StructuralAssembly first(StructuralAssembly::Config{
        .maximumNodes = 8,
        .maximumEdges = 8,
        .keelNodeId = 1,
        .significantMassQ16 = kScalarOne,
    });
    ASSERT_TRUE(first.initialize(nodes, edges));
    EXPECT_EQ(first.components().size(), 1u);

    const AssemblyDamageCommand breakSignificant{
        .tick = 1,
        .sequence = 2,
        .source = 7,
        .edgeId = 11,
        .damageQ16 = 2 * kScalarOne,
    };
    const AssemblyDamageCommand breakTiny{
        .tick = 1,
        .sequence = 1,
        .source = 7,
        .edgeId = 12,
        .damageQ16 = kScalarOne,
    };
    ASSERT_TRUE(first.queueDamage(breakSignificant));
    ASSERT_TRUE(first.queueDamage(breakTiny));
    EXPECT_TRUE(first.queueDamage(breakTiny));
    AssemblyDamageCommand conflictingDuplicate = breakTiny;
    conflictingDuplicate.damageQ16 /= 2;
    EXPECT_FALSE(first.queueDamage(conflictingDuplicate));
    ASSERT_TRUE(first.step(1));

    ASSERT_EQ(first.components().size(), 3u);
    EXPECT_TRUE(first.components()[0].containsKeel);
    EXPECT_TRUE(first.components()[1].significant);
    EXPECT_TRUE(first.components()[2].tinyDebris);
    EXPECT_EQ(first.components()[1].centerOfMassQ12[0],
              2 * kPositionOne);
    ASSERT_EQ(first.fractureEvents().size(), 1u);
    EXPECT_EQ(first.fractureEvents()[0].brokenEdgeIds,
              (std::vector<uint32_t>{11u, 12u}));
    EXPECT_FALSE(first.queueDamage(AssemblyDamageCommand{
        .tick = 2,
        .sequence = 3,
        .source = 7,
        .edgeId = 11,
        .damageQ16 = kScalarOne,
    }));
    const uint32_t workingHash = first.stateHash();
    const std::array<AssemblyNode, 2> invalidReplacement{{
        {1u, {}, kScalarOne, kScalarOne, 0u},
        {1u, {}, kScalarOne, kScalarOne, 0u},
    }};
    EXPECT_FALSE(first.initialize(invalidReplacement, {}));
    EXPECT_TRUE(first.initialized());
    EXPECT_EQ(first.currentTick(), 1u);
    EXPECT_EQ(first.stateHash(), workingHash);

    StructuralAssembly second(StructuralAssembly::Config{
        .maximumNodes = 8,
        .maximumEdges = 8,
        .keelNodeId = 1,
        .significantMassQ16 = kScalarOne,
    });
    ASSERT_TRUE(second.initialize(nodes, edges));
    // Arrival order differs; canonical command order and final hash do not.
    ASSERT_TRUE(second.queueDamage(breakTiny));
    ASSERT_TRUE(second.queueDamage(breakSignificant));
    ASSERT_TRUE(second.step(1));
    EXPECT_EQ(second.stateHash(), first.stateHash());
}

TEST(StructuralAssemblyTest, RejectsInvalidTopologyAndLimits) {
    const std::array<AssemblyNode, 2> duplicateNodes{{
        {1u, {}, kScalarOne, kScalarOne, 0u},
        {1u, {}, kScalarOne, kScalarOne, 0u},
    }};
    const std::array<AssemblyEdge, 1> invalidEdge{{
        {1u, 1u, 9u, kScalarOne, 0u},
    }};
    StructuralAssembly assembly;
    EXPECT_FALSE(assembly.initialize(duplicateNodes, {}));
    const std::array<AssemblyNode, 1> node{{
        {1u, {}, kScalarOne, kScalarOne, 0u},
    }};
    EXPECT_FALSE(assembly.initialize(node, invalidEdge));
}

TEST(DeterministicWaterTest, IsPeriodicAndNativeIntegerStable) {
    DeterministicWaterField water;
    ASSERT_TRUE(water.valid());
    const auto first = water.sample(3 * kPositionOne,
                                    -2 * kPositionOne, 17);
    const auto again = water.sample(3 * kPositionOne,
                                    -2 * kPositionOne, 17);
    EXPECT_EQ(first.heightQ12, again.heightQ12);
    EXPECT_EQ(first.verticalVelocityQ16, again.verticalVelocityQ16);

    const std::array<BuoyancyPointQ, 1> point{{
        {{0, -kPositionOne / 2, 0}, 2 * kScalarOne, 0},
    }};
    const auto floating = evaluateBuoyancy(
        point, BuoyancyInputQ{
            .worldVerticalOffsetQ12 = 0,
            .verticalVelocityQ16 = 0,
            .massQ16 = kScalarOne,
            .dragCoefficientQ16 = kScalarOne / 2,
        }, water, 0);
    EXPECT_GT(floating.submergedVolumeQ16, 0);
    EXPECT_GT(floating.accelerationQ16, -kGravityQ16);
    EXPECT_EQ(water.sample(0, 0, std::numeric_limits<uint64_t>::max())
                  .verticalVelocityQ16,
              0);

    const std::array<BuoyancyPointQ, 3> extremePoints{{
        {{0, std::numeric_limits<int32_t>::min(), 0},
         std::numeric_limits<int32_t>::max(), 0},
        {{0, std::numeric_limits<int32_t>::min(), 0},
         std::numeric_limits<int32_t>::max(), 0},
        {{0, std::numeric_limits<int32_t>::min(), 0},
         std::numeric_limits<int32_t>::max(), 0},
    }};
    const auto extreme = evaluateBuoyancy(
        extremePoints,
        BuoyancyInputQ{
            .worldVerticalOffsetQ12 =
                std::numeric_limits<int32_t>::min(),
            .massQ16 = 1,
        },
        water, 0u);
    EXPECT_EQ(extreme.effectiveVolumeQ16,
              std::numeric_limits<int32_t>::max());
    EXPECT_EQ(extreme.submergedVolumeQ16,
              std::numeric_limits<int32_t>::max());
}

TEST(DemolitionLeagueSliceTest, CollapseScoresAndReplaysExactly) {
    DemolitionLeagueSlice first(DemolitionLeagueSlice::Config{
        .roundTicks = 240,
        .significantMassQ16 = 2 * kScalarOne,
    });
    ASSERT_TRUE(first.initialize());
    EXPECT_FALSE(first.submitImpact(AssemblyDamageCommand{
        .tick = 241,
        .sequence = 99,
        .source = 1,
        .edgeId = 101,
        .damageQ16 = kScalarOne,
    }));
    const AssemblyDamageCommand impact{
        .tick = 1,
        .sequence = 1,
        .source = 1,
        .edgeId = 101,
        .damageQ16 = 4 * kScalarOne,
    };
    ASSERT_TRUE(first.submitImpact(impact));
    ASSERT_TRUE(first.step());
    EXPECT_EQ(first.building().components().size(), 2u);
    ASSERT_EQ(first.fragments().size(), 1u);
    EXPECT_GT(first.scoreQ16(), 10 * kScalarOne);

    for (uint32_t tick = 2; tick <= 180u; ++tick)
        ASSERT_TRUE(first.step());
    EXPECT_TRUE(first.fragments()[0].grounded);
    EXPECT_EQ(first.telemetry().fractureEvents, 1u);
    EXPECT_EQ(first.replayCommands().size(), 1u);

    DemolitionLeagueSlice replay(DemolitionLeagueSlice::Config{
        .roundTicks = 240,
        .significantMassQ16 = 2 * kScalarOne,
    });
    ASSERT_TRUE(replay.initialize());
    ASSERT_TRUE(replay.submitImpact(impact));
    for (uint32_t tick = 1; tick <= 180u; ++tick)
        ASSERT_TRUE(replay.step());
    EXPECT_EQ(replay.scoreQ16(), first.scoreQ16());
    EXPECT_EQ(replay.stateHash(), first.stateHash());
}

TEST(DeadweightSliceTest, RejectsUntrustedOrImpossibleInputs) {
    DeadweightSlice slice;
    ASSERT_TRUE(slice.initialize());
    EXPECT_FALSE(slice.submitInput(DeadweightInput{
        .tick = 1,
        .sequence = 1,
        .clientId = 9,
        .pullZQ16 = kScalarOne,
    }));
    EXPECT_FALSE(slice.submitInput(DeadweightInput{
        .tick = 1,
        .sequence = 2,
        .clientId = 1,
        .pullXQ16 = kScalarOne,
        .pullZQ16 = kScalarOne,
    }));
    const DeadweightInput valid{
        .tick = 1,
        .sequence = 3,
        .clientId = 1,
        .pullZQ16 = kScalarOne,
    };
    EXPECT_TRUE(slice.submitInput(valid));
    EXPECT_TRUE(slice.submitInput(valid));
    EXPECT_EQ(slice.telemetry().duplicateInputs, 1u);
    EXPECT_EQ(slice.telemetry().rejectedInputs, 2u);
    std::array<DeadweightInput, 17> oversized{};
    EXPECT_FALSE(slice.submitRedundantInputs(oversized));

    DeadweightSlice overflowing(DeadweightSlice::Config{
        .roundTicks = std::numeric_limits<uint64_t>::max(),
        .inputFutureWindow = 8u,
    });
    EXPECT_FALSE(overflowing.initialize());
}

TEST(DeadweightSliceTest, TwoClientsCrossHazardsAndReplayExactly) {
    DeadweightSlice first;
    ASSERT_TRUE(first.initialize());
    uint64_t sequence = 1;
    while (first.state().outcome == TransportOutcome::Running) {
        const uint64_t tick = first.state().tick + 1u;
        int32_t correctionX = 0;
        if (first.state().cargoPositionQ12[0] > kPositionOne / 8)
            correctionX = -kScalarOne / 4;
        else if (first.state().cargoPositionQ12[0] < -kPositionOne / 8)
            correctionX = kScalarOne / 4;
        ASSERT_TRUE(first.submitInput(DeadweightInput{
            .tick = tick,
            .sequence = sequence++,
            .clientId = 1,
            .pullXQ16 = correctionX,
            .pullZQ16 = kScalarOne,
        }));
        ASSERT_TRUE(first.submitInput(DeadweightInput{
            .tick = tick,
            .sequence = sequence++,
            .clientId = 2,
            .pullXQ16 = correctionX,
            .pullZQ16 = kScalarOne,
        }));
        ASSERT_TRUE(first.step());
        ASSERT_LT(first.state().tick, 1'800u);
    }
    ASSERT_EQ(first.state().outcome, TransportOutcome::Delivered);
    EXPECT_GT(first.telemetry().hazardTicks, 0u);
    EXPECT_LE(first.history().size(), 64u);
    EXPECT_GT(first.telemetry().historyEvictions, 0u);

    const std::vector<DeadweightInput> recording(
        first.replayCommands().begin(), first.replayCommands().end());
    DeadweightSlice replay;
    ASSERT_TRUE(replay.initialize());
    for (uint64_t tick = 1; tick <= first.state().tick; ++tick) {
        for (const auto& input : recording) {
            if (input.tick == tick) {
                ASSERT_TRUE(replay.submitInput(input));
            }
        }
        ASSERT_TRUE(replay.step());
    }
    EXPECT_EQ(replay.state().outcome, TransportOutcome::Delivered);
    EXPECT_EQ(replay.state().stateHash, first.state().stateHash);
}

TEST(WreckwaterSliceTest, FractureFloodingAndBuoyancyReplayExactly) {
    WreckwaterSlice first;
    WreckwaterSlice replay;
    ASSERT_TRUE(first.initialize());
    ASSERT_TRUE(replay.initialize());
    ASSERT_EQ(first.fragments().size(), 1u);

    const std::array<AssemblyDamageCommand, 4> impacts{{
        {1u, 1u, 1u, 12u, 3 * kScalarOne},
        {1u, 2u, 1u, 14u, 2 * kScalarOne},
        {1u, 3u, 1u, 15u, 2 * kScalarOne},
        {1u, 4u, 1u, 18u, kScalarOne},
    }};
    for (const auto& impact : impacts) {
        ASSERT_TRUE(first.submitImpact(impact));
        ASSERT_TRUE(replay.submitImpact(impact));
    }
    for (uint32_t tick = 1; tick <= 300u; ++tick) {
        ASSERT_TRUE(first.step());
        ASSERT_TRUE(replay.step());
        ASSERT_EQ(first.stateHash(), replay.stateHash());
    }
    EXPECT_EQ(first.assembly().components().size(), 3u);
    EXPECT_EQ(first.telemetry().significantFragments, 1u);
    EXPECT_EQ(first.telemetry().tinyDebrisFragments, 1u);
    EXPECT_EQ(first.telemetry().fractureEvents, 1u);
    EXPECT_GT(first.flooding().size(), 0u);
    EXPECT_GT(first.flooding()[0].floodedFractionQ16, 0);
    EXPECT_GT(first.telemetry().floodedNodeTicks, 0u);
    EXPECT_EQ(first.replayCommands().size(), impacts.size());
}

TEST(WreckwaterSliceTest, RejectsClampedAwayDragConfiguration) {
    WreckwaterSlice invalid(WreckwaterSlice::Config{
        .dragCoefficientQ16 = 4 * kScalarOne + 1,
    });
    EXPECT_FALSE(invalid.initialize());
}

PlaytestObservation observation(
    uint64_t id, ProductCandidate candidate, uint32_t fun,
    uint32_t blockers = 0, uint32_t support = 0,
    bool crashed = false) {
    return PlaytestObservation{
        .sessionId = id,
        .candidate = candidate,
        .participants = 2,
        .durationMinutes = 20,
        .funRating = fun,
        .blockerCount = blockers,
        .supportMinutes = support,
        .completed = true,
        .crashed = crashed,
        .humanVerified = true,
    };
}

TEST(PlaytestLedgerTest, RefusesSyntheticOrInsufficientEvidence) {
    PlaytestLedger ledger;
    auto fake = observation(1, ProductCandidate::Deadweight, 5);
    fake.humanVerified = false;
    EXPECT_FALSE(ledger.record(fake));
    EXPECT_TRUE(ledger.record(
        observation(2, ProductCandidate::Deadweight, 5)));
    EXPECT_EQ(ledger.decision().status,
              ProductDecisionStatus::InsufficientEvidence);
}

TEST(PlaytestLedgerTest, SelectsOnlyFromComparativeOperationalEvidence) {
    PlaytestLedger ledger;
    uint64_t id = 10;
    for (uint32_t sample = 0; sample < 3u; ++sample) {
        ASSERT_TRUE(ledger.record(observation(
            id++, ProductCandidate::DemolitionLeague, 3, 1, 10)));
        ASSERT_TRUE(ledger.record(observation(
            id++, ProductCandidate::Deadweight, 5, 0, 2)));
        ASSERT_TRUE(ledger.record(observation(
            id++, ProductCandidate::Wreckwater, 4, 1, 20)));
    }
    const ProductDecision decision = ledger.decision();
    ASSERT_EQ(decision.status, ProductDecisionStatus::Ready);
    ASSERT_TRUE(decision.candidate.has_value());
    EXPECT_EQ(*decision.candidate, ProductCandidate::Deadweight);
    EXPECT_GT(decision.evidenceScores[1], decision.evidenceScores[0]);
    EXPECT_GT(decision.evidenceScores[1], decision.evidenceScores[2]);
}

TEST(PlaytestLedgerTest, DoesNotForceAChoiceWhenEvidenceTies) {
    PlaytestLedger ledger;
    uint64_t id = 100;
    for (uint32_t sample = 0; sample < 3u; ++sample) {
        for (const ProductCandidate candidate : {
                 ProductCandidate::DemolitionLeague,
                 ProductCandidate::Deadweight,
                 ProductCandidate::Wreckwater}) {
            ASSERT_TRUE(ledger.record(observation(id++, candidate, 4)));
        }
    }
    const ProductDecision decision = ledger.decision();
    EXPECT_EQ(decision.status, ProductDecisionStatus::Tie);
    EXPECT_FALSE(decision.candidate.has_value());
}

} // namespace
} // namespace voxy::gameplay
