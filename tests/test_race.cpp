#include <gtest/gtest.h>

#include "moto/race.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string>

namespace voxy::moto {
namespace {

[[nodiscard]] std::array<RaceCheckpoint, 4> shortCourse() {
    return {{
        {.center = {10.0f, 0.0f, 0.0f},
         .forward = {1.0f, 0.0f, 0.0f}, .halfWidth = 2.0f,
         .halfHeight = 2.0f},
        {.center = {30.0f, 0.0f, 0.0f},
         .forward = {0.0f, 0.0f, 1.0f}, .halfWidth = 2.0f,
         .halfHeight = 2.0f},
        {.center = {30.0f, 0.0f, 20.0f},
         .forward = {-1.0f, 0.0f, 0.0f}, .halfWidth = 2.0f,
         .halfHeight = 2.0f},
        {.center = {10.0f, 0.0f, 20.0f},
         .forward = {0.0f, 0.0f, -1.0f}, .halfWidth = 2.0f,
         .halfHeight = 2.0f},
    }};
}

void stepPosition(RaceSession& session, uint16_t player, glm::vec3 position,
                  bool discontinuity = false) {
    const RaceRiderFrame frame{
        .player = player,
        .position = position,
        .discontinuity = discontinuity,
    };
    session.step(std::span(&frame, 1u));
}

[[nodiscard]] RaceTrickEvent trickEvent(uint64_t sequence,
                                        uint64_t landingIdentity,
                                        uint64_t authorityTick,
                                        RaceTrick trick) {
    return {.sequence = sequence,
            .landingIdentity = landingIdentity,
            .authorityTick = authorityTick,
            .trick = trick};
}

TEST(RaceSessionTest, CircuitGridIsBehindAndAlignedWithStartGate) {
    const auto course = shortCourse();
    CircuitGridPose grid;
    std::string error;
    ASSERT_TRUE(buildCircuitGridPose(course, 10.0f, &grid, &error)) << error;
    EXPECT_TRUE(error.empty());

    const glm::vec3 forward = glm::normalize(course.front().forward);
    EXPECT_NEAR(glm::dot(grid.position - course.front().center, forward),
                -10.0f, 1.0e-6f);
    const glm::vec3 bikeForward{
        std::sin(grid.yaw), 0.0f, std::cos(grid.yaw)};
    EXPECT_NEAR(glm::dot(bikeForward, forward), 1.0f, 1.0e-6f);

    EXPECT_FALSE(buildCircuitGridPose({}, 10.0f, &grid, &error));
    EXPECT_FALSE(buildCircuitGridPose(course, 1.0f, &grid, &error));
    EXPECT_FALSE(buildCircuitGridPose(course, 10.0f, nullptr, &error));
}

TEST(RaceSessionTest, CountdownIgnoresGateCrossingsAndStartsWithoutDnf) {
    EXPECT_TRUE(isCircuitGridLocked(
        RaceMode::Circuit, RacePhase::Countdown));
    EXPECT_FALSE(isCircuitGridLocked(
        RaceMode::Freeride, RacePhase::Countdown));
    EXPECT_FALSE(isCircuitGridLocked(
        RaceMode::Circuit, RacePhase::Lobby));
    EXPECT_FALSE(isCircuitGridLocked(
        RaceMode::Circuit, RacePhase::Running));
    EXPECT_FALSE(isCircuitGridLocked(
        RaceMode::Circuit, RacePhase::Finished));

    RaceSession session;
    RaceConfig config;
    config.countdownTicks = 3u;
    config.durationTicks = 4u;
    config.maximumTravelPerTick = 50.0f;
    const auto course = shortCourse();
    ASSERT_TRUE(session.configure(config, course));
    ASSERT_TRUE(session.join(7u));
    ASSERT_TRUE(session.start());
    ASSERT_EQ(session.phase(), RacePhase::Countdown);
    EXPECT_EQ(session.countdownTicksRemaining(), 3u);

    // Even adversarial movement cannot launch through gate zero early.
    stepPosition(session, 7u, {8.0f, 0.0f, 0.0f}, true);
    EXPECT_EQ(session.phase(), RacePhase::Countdown);
    EXPECT_EQ(session.countdownTicksRemaining(), 2u);
    stepPosition(session, 7u, {12.0f, 0.0f, 0.0f});
    EXPECT_EQ(session.rider(7u)->nextCheckpoint, 0u);
    EXPECT_EQ(session.countdownTicksRemaining(), 1u);

    CircuitGridPose grid;
    ASSERT_TRUE(buildCircuitGridPose(course, 10.0f, &grid));
    stepPosition(session, 7u, grid.position, true);
    EXPECT_EQ(session.phase(), RacePhase::Running);
    EXPECT_EQ(session.runningTick(), 0u);
    EXPECT_EQ(session.rider(7u)->nextCheckpoint, 0u);
    EXPECT_FALSE(session.rider(7u)->finished);
    EXPECT_FALSE(session.rider(7u)->didNotFinish);

    stepPosition(session, 7u, grid.position);
    EXPECT_EQ(session.phase(), RacePhase::Running);
    EXPECT_EQ(session.runningTick(), 1u);
    EXPECT_FALSE(session.rider(7u)->didNotFinish);
}

TEST(RaceSessionTest, ApplicationPolicyOwnsControlsGridAndHudTransitions) {
    const MotoRaceApplicationPolicy practiceReset =
        evaluateMotoRaceApplicationPolicy(
            false, true, RaceMode::Freeride, RacePhase::Running, 99u);
    EXPECT_EQ(practiceReset.action, MotoRaceControlAction::ResetPractice);
    EXPECT_FALSE(practiceReset.gridOwned);
    EXPECT_FALSE(practiceReset.hudRaceActive);
    EXPECT_EQ(practiceReset.hudPhase, RacePhase::Lobby);
    EXPECT_EQ(practiceReset.hudCountdownTicks, 0u);

    const MotoRaceApplicationPolicy circuitStart =
        evaluateMotoRaceApplicationPolicy(
            true, true, RaceMode::Freeride, RacePhase::Running, 0u);
    EXPECT_EQ(circuitStart.action, MotoRaceControlAction::StartCircuit);

    const MotoRaceApplicationPolicy circuitReset =
        evaluateMotoRaceApplicationPolicy(
            false, true, RaceMode::Circuit, RacePhase::Running, 0u);
    EXPECT_EQ(circuitReset.action, MotoRaceControlAction::StartCircuit);

    const MotoRaceApplicationPolicy countdown =
        evaluateMotoRaceApplicationPolicy(
            false, false, RaceMode::Circuit, RacePhase::Countdown, 1u);
    EXPECT_EQ(countdown.action, MotoRaceControlAction::None);
    EXPECT_TRUE(countdown.gridOwned);
    EXPECT_TRUE(countdown.hudRaceActive);
    EXPECT_EQ(countdown.hudPhase, RacePhase::Countdown);
    EXPECT_EQ(countdown.hudCountdownTicks, 1u);

    const MotoRaceApplicationPolicy green =
        evaluateMotoRaceApplicationPolicy(
            false, false, RaceMode::Circuit, RacePhase::Running, 0u);
    EXPECT_FALSE(green.gridOwned);
    EXPECT_TRUE(green.hudRaceActive);
    EXPECT_EQ(green.hudPhase, RacePhase::Running);
    EXPECT_EQ(green.hudCountdownTicks, 0u);
}

TEST(RaceSessionTest, RejectsUnsafeConfigurationAndWholeCourseGeometry) {
    RaceSession session;
    RaceConfig config;
    std::string error;
    const auto course = shortCourse();

    config.maximumPlayers = kMaximumRacePlayers + 1u;
    EXPECT_FALSE(session.configure(config, course, &error));
    EXPECT_FALSE(error.empty());

    config = {};
    auto badCourse = course;
    badCourse[0].center.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(session.configure(config, badCourse, &error));

    badCourse = course;
    badCourse[1].center = {12.0f, 0.0f, 0.0f};
    EXPECT_FALSE(session.configure(config, badCourse, &error));
    EXPECT_EQ(error, "checkpoint gates overlap");

    badCourse = course;
    badCourse[2].forward = {1.0f, 0.0f, 0.0f};
    EXPECT_FALSE(session.configure(config, badCourse, &error));
    EXPECT_EQ(error, "checkpoint direction does not follow course");

    badCourse = course;
    badCourse[0].forward = {1.0f, 0.1f, 0.0f};
    EXPECT_FALSE(session.configure(config, badCourse, &error));

    EXPECT_FALSE(session.configure(config, {}, &error));
    config.mode = RaceMode::Freeride;
    EXPECT_TRUE(session.configure(config, {}, &error)) << error;

    config = {};
    config.durationTicks = 0u;
    EXPECT_FALSE(session.configure(config, course, &error));
    config.mode = RaceMode::Freeride;
    EXPECT_TRUE(session.configure(config, {}, &error)) << error;
}

TEST(RaceSessionTest, EnforcesCapacityAndUniquePlayers) {
    RaceSession session;
    RaceConfig config;
    config.maximumPlayers = 4u;
    ASSERT_TRUE(session.configure(config, shortCourse()));

    EXPECT_TRUE(session.join(10u));
    EXPECT_TRUE(session.join(10u));
    EXPECT_TRUE(session.join(11u));
    EXPECT_TRUE(session.join(12u));
    EXPECT_TRUE(session.join(13u));
    EXPECT_FALSE(session.join(14u));
    EXPECT_FALSE(session.join(kInvalidRacePlayer));
}

TEST(RaceSessionTest, DirectedGateCrossingsFinishInCanonicalOrder) {
    RaceSession session;
    RaceConfig config;
    config.countdownTicks = 1u;
    config.maximumTravelPerTick = 50.0f;
    ASSERT_TRUE(session.configure(config, shortCourse()));
    ASSERT_TRUE(session.join(7u));
    ASSERT_TRUE(session.start());

    stepPosition(session, 7u, {8.0f, 0.0f, 0.0f}, true);
    EXPECT_EQ(session.phase(), RacePhase::Running);

    stepPosition(session, 7u, {12.0f, 0.0f, 0.0f});
    EXPECT_EQ(session.rider(7u)->nextCheckpoint, 1u);
    stepPosition(session, 7u, {30.0f, 0.0f, -2.0f});
    stepPosition(session, 7u, {30.0f, 0.0f, 2.0f});
    EXPECT_EQ(session.rider(7u)->nextCheckpoint, 2u);
    stepPosition(session, 7u, {32.0f, 0.0f, 20.0f});
    stepPosition(session, 7u, {28.0f, 0.0f, 20.0f});
    EXPECT_EQ(session.rider(7u)->nextCheckpoint, 3u);
    stepPosition(session, 7u, {10.0f, 0.0f, 22.0f});
    stepPosition(session, 7u, {10.0f, 0.0f, 18.0f});

    ASSERT_NE(session.rider(7u), nullptr);
    EXPECT_TRUE(session.rider(7u)->finished);
    EXPECT_EQ(session.rider(7u)->completedLaps, 1u);
    EXPECT_EQ(session.rider(7u)->finishPlace, 1u);
    EXPECT_EQ(session.phase(), RacePhase::Finished);
}

TEST(RaceSessionTest, CircuitWinnerAlwaysRanksAheadOfTimeoutDnf) {
    RaceSession session;
    RaceConfig config;
    config.countdownTicks = 0u;
    config.durationTicks = 20u;
    config.maximumTravelPerTick = 50.0f;
    ASSERT_TRUE(session.configure(config, shortCourse()));
    ASSERT_TRUE(session.join(7u));
    ASSERT_TRUE(session.join(9u));
    ASSERT_TRUE(session.start());

    stepPosition(session, 7u, {8.0f, 0.0f, 0.0f}, true);
    stepPosition(session, 7u, {12.0f, 0.0f, 0.0f});
    stepPosition(session, 7u, {30.0f, 0.0f, -2.0f});
    stepPosition(session, 7u, {30.0f, 0.0f, 2.0f});
    stepPosition(session, 7u, {32.0f, 0.0f, 20.0f});
    stepPosition(session, 7u, {28.0f, 0.0f, 20.0f});
    stepPosition(session, 7u, {10.0f, 0.0f, 22.0f});
    stepPosition(session, 7u, {10.0f, 0.0f, 18.0f});
    ASSERT_TRUE(session.rider(7u)->finished);
    ASSERT_EQ(session.rider(7u)->finishPlace, 1u);

    while (session.phase() != RacePhase::Finished) session.step({});
    ASSERT_TRUE(session.rider(9u)->finished);
    EXPECT_TRUE(session.rider(9u)->didNotFinish);
    EXPECT_EQ(session.rider(9u)->finishPlace, 0u);
    ASSERT_EQ(session.standings().size(), 2u);
    EXPECT_EQ(session.standings()[0], 7u);
    EXPECT_EQ(session.standings()[1], 9u);
}

TEST(RaceSessionTest, ReconfigureAfterFinishIsADeterministicFreshStart) {
    RaceSession session;
    RaceConfig config;
    config.countdownTicks = 0u;
    config.durationTicks = 30u;
    config.maximumTravelPerTick = 50.0f;
    const auto course = shortCourse();
    ASSERT_TRUE(session.configure(config, course));
    ASSERT_TRUE(session.join(7u));
    ASSERT_TRUE(session.start());

    stepPosition(session, 7u, {8.0f, 0.0f, 0.0f}, true);
    stepPosition(session, 7u, {12.0f, 0.0f, 0.0f});
    stepPosition(session, 7u, {30.0f, 0.0f, -2.0f});
    stepPosition(session, 7u, {30.0f, 0.0f, 2.0f});
    stepPosition(session, 7u, {32.0f, 0.0f, 20.0f});
    stepPosition(session, 7u, {28.0f, 0.0f, 20.0f});
    stepPosition(session, 7u, {10.0f, 0.0f, 22.0f});
    stepPosition(session, 7u, {10.0f, 0.0f, 18.0f});
    ASSERT_EQ(session.phase(), RacePhase::Finished);
    ASSERT_EQ(session.rider(7u)->finishPlace, 1u);

    config.countdownTicks = 3u;
    ASSERT_TRUE(session.configure(config, course));
    ASSERT_TRUE(session.join(7u));
    ASSERT_TRUE(session.start());
    ASSERT_EQ(session.phase(), RacePhase::Countdown);
    ASSERT_NE(session.rider(7u), nullptr);
    EXPECT_EQ(session.tick(), 0u);
    EXPECT_EQ(session.runningTick(), 0u);
    EXPECT_EQ(session.rider(7u)->nextCheckpoint, 0u);
    EXPECT_EQ(session.rider(7u)->completedLaps, 0u);
    EXPECT_EQ(session.rider(7u)->finishPlace, 0u);
    EXPECT_FALSE(session.rider(7u)->finished);
    EXPECT_FALSE(session.rider(7u)->didNotFinish);

    session.step({});
    session.step({});
    session.step({});
    EXPECT_EQ(session.phase(), RacePhase::Running);
    EXPECT_EQ(session.runningTick(), 0u);
    EXPECT_FALSE(session.rider(7u)->didNotFinish);
}

TEST(RaceSessionTest, StationaryReverseAndOutsideGateExploitsFail) {
    RaceSession session;
    RaceConfig config;
    config.countdownTicks = 0u;
    config.maximumTravelPerTick = 20.0f;
    ASSERT_TRUE(session.configure(config, shortCourse()));
    ASSERT_TRUE(session.join(3u));
    ASSERT_TRUE(session.start());

    // Spawning in the gate and remaining there is not a crossing.
    stepPosition(session, 3u, {10.0f, 0.0f, 0.0f}, true);
    stepPosition(session, 3u, {10.0f, 0.0f, 0.0f});
    EXPECT_EQ(session.rider(3u)->nextCheckpoint, 0u);

    // Crossing front-to-back is the wrong direction.
    stepPosition(session, 3u, {12.0f, 0.0f, 0.0f});
    stepPosition(session, 3u, {8.0f, 0.0f, 0.0f});
    EXPECT_EQ(session.rider(3u)->nextCheckpoint, 0u);

    // A forward crossing beyond the authored gate width is not proximity.
    stepPosition(session, 3u, {8.0f, 0.0f, 3.0f});
    stepPosition(session, 3u, {12.0f, 0.0f, 3.0f});
    EXPECT_EQ(session.rider(3u)->nextCheckpoint, 0u);

    stepPosition(session, 3u, {8.0f, 0.0f, 0.0f});
    stepPosition(session, 3u, {12.0f, 0.0f, 0.0f});
    EXPECT_EQ(session.rider(3u)->nextCheckpoint, 1u);
}

TEST(RaceSessionTest, TeleportsCannotCollectGates) {
    RaceSession session;
    RaceConfig config;
    config.countdownTicks = 0u;
    config.maximumTravelPerTick = 3.0f;
    ASSERT_TRUE(session.configure(config, shortCourse()));
    ASSERT_TRUE(session.join(3u));
    ASSERT_TRUE(session.start());

    stepPosition(session, 3u, {8.0f, 0.0f, 0.0f}, true);
    stepPosition(session, 3u, {12.0f, 0.0f, 0.0f});
    EXPECT_EQ(session.rider(3u)->nextCheckpoint, 0u);
}

TEST(RaceSessionTest, CanonicalTrickEventsRejectReplayForgeryAndOverflow) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(9u));
    ASSERT_TRUE(session.start());

    const std::array<RaceTrickEvent, 1> first{{
        trickEvent(1u, 1u, 1u, RaceTrick::Backflip),
    }};
    RaceRiderFrame frame{.player = 9u, .position = {0.0f, 0.0f, 0.0f},
                         .trickEvents = first};
    session.step(std::span(&frame, 1u));
    EXPECT_EQ(session.rider(9u)->score, 2'500u);
    EXPECT_EQ(session.rider(9u)->lastTrickSequence, 1u);

    const std::array<RaceTrickEvent, 4> attacks{{
        trickEvent(1u, 1u, 1u, RaceTrick::DoubleBackflip), // replay
        trickEvent(3u, 2u, 2u, RaceTrick::DoubleBackflip), // sequence gap
        trickEvent(2u, 2u, 2u, static_cast<RaceTrick>(255u)),
        trickEvent(2u, 2u, 2u, RaceTrick::Whip), // landing too soon
    }};
    frame.trickEvents = attacks;
    session.step(std::span(&frame, 1u));
    EXPECT_EQ(session.rider(9u)->score, 2'500u);
    EXPECT_EQ(session.rider(9u)->lastTrickSequence, 1u);
    EXPECT_EQ(session.rider(9u)->rejectedTrickEvents, 4u);

    while (session.tick() + 1u < 31u) session.step({});
    const std::array<RaceTrickEvent, 1> validNext{{
        trickEvent(2u, 2u, 31u, RaceTrick::Whip),
    }};
    frame.trickEvents = validNext;
    session.step(std::span(&frame, 1u));
    EXPECT_EQ(session.rider(9u)->score, 3'000u);
    EXPECT_EQ(session.rider(9u)->lastTrickSequence, 2u);

    const std::array<RaceTrickEvent, 1> extreme{{
        trickEvent(std::numeric_limits<uint64_t>::max(), 3u, 32u,
                   RaceTrick::DoubleBackflip),
    }};
    frame.trickEvents = extreme;
    session.step(std::span(&frame, 1u));
    EXPECT_EQ(session.rider(9u)->score, 3'000u);
    EXPECT_EQ(session.rider(9u)->rejectedTrickEvents, 5u);
}

TEST(RaceSessionTest, OneLandingAwardsEachTrickOnlyOnceAndCannotRetryLater) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    config.maximumTrickEventsPerFrame = 2u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(1u));
    ASSERT_TRUE(session.start());

    const std::array<RaceTrickEvent, 3> burst{{
        trickEvent(1u, 1u, 1u, RaceTrick::Whip),
        trickEvent(2u, 1u, 1u, RaceTrick::NoHander),
        trickEvent(3u, 1u, 1u, RaceTrick::DoubleBackflip),
    }};
    RaceRiderFrame frame{.player = 1u, .trickEvents = burst};
    session.step(std::span(&frame, 1u));
    EXPECT_EQ(session.rider(1u)->score, 1'250u);
    EXPECT_EQ(session.rider(1u)->lastTrickSequence, 2u);
    EXPECT_EQ(session.rider(1u)->rejectedTrickEvents, 1u);

    const std::array<RaceTrickEvent, 1> retry{{
        trickEvent(3u, 1u, 2u, RaceTrick::DoubleBackflip),
    }};
    frame.trickEvents = retry;
    session.step(std::span(&frame, 1u));
    EXPECT_EQ(session.rider(1u)->score, 1'250u);
    EXPECT_EQ(session.rider(1u)->lastTrickSequence, 2u);
    EXPECT_EQ(session.rider(1u)->rejectedTrickEvents, 2u);
}

TEST(RaceSessionTest, DuplicateTrickOnSameLandingIsRejected) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(6u));
    ASSERT_TRUE(session.start());

    const std::array<RaceTrickEvent, 3> events{{
        trickEvent(1u, 1u, 1u, RaceTrick::Whip),
        trickEvent(2u, 1u, 1u, RaceTrick::Whip),
        trickEvent(2u, 1u, 1u, RaceTrick::Backflip),
    }};
    const RaceRiderFrame frame{.player = 6u, .trickEvents = events};
    session.step(std::span(&frame, 1u));

    EXPECT_EQ(session.rider(6u)->score, 3'000u);
    EXPECT_EQ(session.rider(6u)->lastTrickSequence, 2u);
    EXPECT_EQ(session.rider(6u)->lastLandingIdentity, 1u);
    EXPECT_EQ(session.rider(6u)->rejectedTrickEvents, 1u);
}

TEST(RaceSessionTest, FreshSequentialLandingsCannotSpamEveryTick) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(5u));
    ASSERT_TRUE(session.start());

    for (uint64_t tick = 1u; tick <= 3u; ++tick) {
        const std::array<RaceTrickEvent, 1> event{{
            trickEvent(tick, tick, tick, RaceTrick::DoubleBackflip),
        }};
        const RaceRiderFrame frame{.player = 5u, .trickEvents = event};
        session.step(std::span(&frame, 1u));
    }
    EXPECT_EQ(session.rider(5u)->score, 6'500u);
    EXPECT_EQ(session.rider(5u)->lastTrickSequence, 1u);
    EXPECT_EQ(session.rider(5u)->rejectedTrickEvents, 2u);
}

TEST(RaceSessionTest, FullSessionMaximumCanonicalScoreIsBounded) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    config.durationTicks = 21'600u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(4u));
    ASSERT_TRUE(session.start());

    while (session.phase() != RacePhase::Finished) {
        const RaceRiderState* state = session.rider(4u);
        ASSERT_NE(state, nullptr);
        const uint64_t nextTick = session.tick() + 1u;
        const uint64_t landing = state->lastLandingIdentity + 1u;
        const uint64_t sequence = state->lastTrickSequence;
        const std::array<RaceTrickEvent, 8> events{{
            trickEvent(sequence + 1u, landing, nextTick, RaceTrick::Whip),
            trickEvent(sequence + 2u, landing, nextTick,
                       RaceTrick::NoHander),
            trickEvent(sequence + 3u, landing, nextTick, RaceTrick::NacNac),
            trickEvent(sequence + 4u, landing, nextTick,
                       RaceTrick::Superman),
            trickEvent(sequence + 5u, landing, nextTick,
                       RaceTrick::Backflip),
            trickEvent(sequence + 6u, landing, nextTick,
                       RaceTrick::Frontflip),
            trickEvent(sequence + 7u, landing, nextTick,
                       RaceTrick::DoubleBackflip),
            trickEvent(sequence + 8u, landing, nextTick,
                       RaceTrick::BarrelRoll),
        }};
        const RaceRiderFrame frame{.player = 4u, .trickEvents = events};
        session.step(std::span(&frame, 1u));
    }

    constexpr uint64_t maximumPhysicalLandings = 720u;
    constexpr uint64_t maximumPointsPerLanding = 17'750u;
    EXPECT_EQ(session.rider(4u)->lastLandingIdentity,
              maximumPhysicalLandings);
    EXPECT_EQ(session.rider(4u)->score,
              maximumPhysicalLandings * maximumPointsPerLanding);
    EXPECT_LE(session.rider(4u)->score, 12'780'000u);
    EXPECT_GT(session.rider(4u)->rejectedTrickEvents, 160'000u);
}

TEST(RaceSessionTest, FreerideStandingsExpireAtConfiguredLimit) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    config.durationTicks = 3u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(2u));
    ASSERT_TRUE(session.join(1u));
    ASSERT_TRUE(session.start());

    const std::array<RaceTrickEvent, 1> high{{
        trickEvent(1u, 1u, 1u, RaceTrick::Frontflip),
    }};
    const std::array<RaceTrickEvent, 1> low{{
        trickEvent(1u, 1u, 1u, RaceTrick::Whip),
    }};
    const std::array frames{
        RaceRiderFrame{.player = 1u, .trickEvents = high},
        RaceRiderFrame{.player = 2u, .trickEvents = low},
    };
    session.step(frames);
    session.step({});
    ASSERT_EQ(session.standings().size(), 2u);
    EXPECT_EQ(session.standings().front(), 1u);
    EXPECT_EQ(session.rider(1u)->score, 3'000u);

    session.step({});
    EXPECT_EQ(session.phase(), RacePhase::Finished);
    EXPECT_TRUE(session.rider(1u)->finished);
    EXPECT_TRUE(session.rider(2u)->finished);
}

TEST(RaceSessionTest, UntimedFreeridePracticeDoesNotSecretlyExpire) {
    RaceSession session;
    const RaceConfig config = makeUntimedPracticeRaceConfig();
    EXPECT_EQ(config.mode, RaceMode::Freeride);
    EXPECT_EQ(config.countdownTicks, 0u);
    EXPECT_EQ(config.durationTicks, 0u);
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(8u));
    ASSERT_TRUE(session.start());

    for (uint32_t tick = 0u; tick < 30'000u; ++tick) session.step({});
    EXPECT_EQ(session.phase(), RacePhase::Running);
    EXPECT_FALSE(session.rider(8u)->finished);
    EXPECT_FALSE(session.rider(8u)->didNotFinish);
}

TEST(RaceSessionTest, RejectsDuplicateAndNonFiniteFrames) {
    RaceSession session;
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    ASSERT_TRUE(session.configure(config, {}));
    ASSERT_TRUE(session.join(9u));
    ASSERT_TRUE(session.start());

    const std::array duplicate{
        RaceRiderFrame{.player = 9u, .position = {0.0f, 0.0f, 0.0f}},
        RaceRiderFrame{.player = 9u, .position = {1.0f, 0.0f, 0.0f}},
    };
    session.step(duplicate);
    EXPECT_EQ(session.rider(9u)->rejectedFrames, 1u);

    const RaceRiderFrame invalid{
        .player = 9u,
        .position = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}};
    session.step(std::span(&invalid, 1u));
    EXPECT_EQ(session.rider(9u)->rejectedFrames, 2u);
}

} // namespace
} // namespace voxy::moto
