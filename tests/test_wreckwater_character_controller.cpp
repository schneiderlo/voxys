#include "client/wreckwater_character_controller.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::client {
namespace {

constexpr uint64_t kLocalPlayer = 7001u;

static_assert(noexcept(
    std::declval<WreckwaterCharacterController&>()
        .receiveLatestCertifiedSample(
            std::declval<
                const network::WreckwaterClientSample&>())));
static_assert(noexcept(
    std::declval<WreckwaterCharacterController&>()
        .recordSuccessfullySentInput(
            1u, 1u, 0, 0, false, false)));
static_assert(noexcept(
    std::declval<WreckwaterCharacterController&>()
        .advancePrediction(
            1u,
            std::declval<
                std::span<
                    const game::
                        WreckwaterCharacterPlatformSample>>())));
static_assert(noexcept(
    std::declval<WreckwaterCharacterController&>()
        .advancePrediction(1u)));

[[nodiscard]] glm::dvec3 absolute(
    const physics::WorldPosition& position) {
    return physics::worldPositionToAbsolute(position);
}

[[nodiscard]] game::WreckwaterCharacterPlatformSample makePlatform(
    game::SkiffId skiffId = 11u,
    uint32_t skiffGeneration = 1u,
    const glm::dvec3& position = glm::dvec3(40.0, -20.0, 40.0),
    const glm::quat& orientation =
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
    const glm::vec3& linearVelocity = glm::vec3(0.0f),
    const glm::vec3& angularVelocity = glm::vec3(0.0f)) {
    return {
        .skiffId = skiffId,
        .skiffGeneration = skiffGeneration,
        .body = {skiffId + 100u, skiffGeneration + 100u},
        .position = physics::worldPositionFromAbsolute(position),
        .orientation = orientation,
        .linearVelocity = linearVelocity,
        .angularVelocity = angularVelocity,
        .deckHalfExtents = {4.0f, 4.0f},
        .deckLocalHeight = 0.5f,
    };
}

[[nodiscard]] network::WreckwaterEntityState networkPlatform(
    const game::WreckwaterCharacterPlatformSample& platform) {
    network::WreckwaterEntityState entity;
    entity.netEntityId = 100u + platform.skiffId;
    entity.netGeneration = platform.skiffGeneration;
    entity.kind = network::WreckwaterEntityKind::Skiff;
    entity.crew = network::WreckwaterCrew::CrewOne;
    entity.sector = {
        platform.position.sector.x,
        platform.position.sector.y,
        platform.position.sector.z,
    };
    entity.localPosition = {
        platform.position.local.x,
        platform.position.local.y,
        platform.position.local.z,
    };
    entity.orientation = {
        platform.orientation.x,
        platform.orientation.y,
        platform.orientation.z,
        platform.orientation.w,
    };
    entity.linearVelocity = {
        platform.linearVelocity.x,
        platform.linearVelocity.y,
        platform.linearVelocity.z,
    };
    entity.angularVelocity = {
        platform.angularVelocity.x,
        platform.angularVelocity.y,
        platform.angularVelocity.z,
    };
    entity.shape = network::WreckwaterShape::Box;
    entity.dimensions = {8.0f, 1.0f, 8.0f};
    entity.packedMaterialFlags = 0x1000'0001u;
    entity.skiff = {
        .skiffId = platform.skiffId,
        .generation = platform.skiffGeneration,
        .disposition =
            network::WreckwaterSkiffDisposition::Active,
    };
    EXPECT_TRUE(
        network::canonicalizeWreckwaterEntityState(entity));
    return entity;
}

[[nodiscard]] network::WreckwaterCharacterState makeCharacter(
    game::WreckwaterCharacterMode mode,
    const glm::dvec3& position,
    const glm::vec3& velocity = glm::vec3(0.0f),
    uint64_t acknowledgedSequence = 0u,
    uint32_t handle = 0x0001'0001u,
    uint32_t connectionGeneration = 1u,
    uint64_t playerId = kLocalPlayer) {
    const physics::WorldPosition world =
        physics::worldPositionFromAbsolute(position);
    network::WreckwaterCharacterState character;
    character.characterHandle = handle;
    character.stateFlags = static_cast<uint32_t>(mode)
        | network::kWreckwaterCharacterStateActiveFlag
        | network::kWreckwaterCharacterStateConnectedFlag;
    character.playerId = playerId;
    character.connectionGeneration = connectionGeneration;
    character.sector = {
        world.sector.x, world.sector.y, world.sector.z};
    character.localFeetPosition = {
        world.local.x, world.local.y, world.local.z};
    character.worldVelocity = {
        velocity.x, velocity.y, velocity.z};
    character.lastAppliedCharacterInputSequence =
        acknowledgedSequence;
    EXPECT_TRUE(
        network::canonicalizeWreckwaterCharacterState(character));
    return character;
}

[[nodiscard]] network::WreckwaterCharacterState makeAboardCharacter(
    const game::WreckwaterCharacterPlatformSample& platform,
    const glm::vec3& localFeet,
    const glm::vec3& localVelocity = glm::vec3(0.0f),
    uint64_t acknowledgedSequence = 0u,
    uint32_t handle = 0x0001'0001u,
    uint32_t connectionGeneration = 1u) {
    const glm::vec3 lever = platform.orientation * localFeet;
    const glm::vec3 worldVelocity =
        platform.linearVelocity
        + glm::cross(platform.angularVelocity, lever)
        + platform.orientation * localVelocity;
    const physics::WorldPosition world =
        physics::worldPositionFromAbsolute(
            absolute(platform.position) + glm::dvec3(lever));
    network::WreckwaterCharacterState character;
    character.characterHandle = handle;
    character.stateFlags = static_cast<uint32_t>(
        game::WreckwaterCharacterMode::OnSkiff)
        | network::kWreckwaterCharacterStateActiveFlag
        | network::kWreckwaterCharacterStateConnectedFlag;
    character.playerId = kLocalPlayer;
    character.connectionGeneration = connectionGeneration;
    character.sector = {
        world.sector.x, world.sector.y, world.sector.z};
    character.localFeetPosition = {
        world.local.x, world.local.y, world.local.z};
    character.worldVelocity = {
        worldVelocity.x, worldVelocity.y, worldVelocity.z};
    character.skiffId = platform.skiffId;
    character.skiffGeneration = platform.skiffGeneration;
    character.skiffLocalFeetPosition = {
        localFeet.x, localFeet.y, localFeet.z};
    character.skiffLocalVelocity = {
        localVelocity.x, localVelocity.y, localVelocity.z};
    character.lastAppliedCharacterInputSequence =
        acknowledgedSequence;
    EXPECT_TRUE(
        network::canonicalizeWreckwaterCharacterState(character));
    return character;
}

[[nodiscard]] network::WreckwaterClientSample makeSample(
    uint64_t snapshotSequence,
    uint64_t evidenceTick,
    network::WreckwaterCharacterState character,
    const game::WreckwaterCharacterPlatformSample& platform =
        makePlatform()) {
    network::WreckwaterClientSample sample;
    sample.requestedPhysicsTick = {
        .whole = evidenceTick, .fraction = 0.0f};
    sample.evaluatedPhysicsTick = sample.requestedPhysicsTick;
    sample.authoritative.schemaVersion =
        network::kWreckwaterWireSchemaVersion;
    sample.authoritative.flags =
        network::kWreckwaterCertifiedFullSnapshotFlag;
    sample.authoritative.identity = {
        .sessionId = 101u,
        .matchId = 202u,
        .worldId = 303u,
        .worldEpoch = 4u,
        .authorityEpoch = 5u,
    };
    sample.authoritative.snapshotSequence = snapshotSequence;
    sample.authoritative.applicationTick = evidenceTick;
    sample.authoritative.physicsEvidenceTick = evidenceTick;
    sample.authoritative.phase = network::WreckwaterPhase::Live;
    sample.entityCount = 1u;
    sample.entities[0].authoritativeState =
        networkPlatform(platform);
    sample.entities[0].netEntityId =
        sample.entities[0].authoritativeState.netEntityId;
    sample.entities[0].netGeneration =
        sample.entities[0].authoritativeState.netGeneration;
    sample.characterCount = 1u;
    sample.characters[0].authoritativeState = character;
    sample.characters[0].characterHandle =
        character.characterHandle;
    return sample;
}

[[nodiscard]] WreckwaterCharacterController::Config config(
    uint32_t inputTicks =
        kWreckwaterCharacterControllerMaximumTicks,
    uint32_t stateTicks =
        kWreckwaterCharacterControllerMaximumTicks) {
    WreckwaterCharacterController::Config result;
    result.localPlayerId = kLocalPlayer;
    result.inputHistoryTicks = inputTicks;
    result.stateHistoryTicks = stateTicks;
    result.hardSnapDistance = 100.0f;
    result.movement.waterHeight = -100.0f;
    return result;
}

void expectAccepted(WreckwaterCharacterControllerStatus status) {
    EXPECT_EQ(status, WreckwaterCharacterControllerStatus::Accepted)
        << wreckwaterCharacterControllerStatusName(status);
}

TEST(WreckwaterCharacterController,
     BindsOnlyExactLocalCertifiedStateAndPinsIdentity) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));

    auto fractional = makeSample(
        1u, 10u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}));
    fractional.evaluatedPhysicsTick.fraction = 0.5f;
    EXPECT_EQ(
        controller.receiveLatestCertifiedSample(fractional),
        WreckwaterCharacterControllerStatus::
            InvalidCertifiedSample);
    EXPECT_EQ(
        controller.binding(),
        WreckwaterCharacterControllerBinding::
            AwaitingCertifiedSnapshot);

    const auto otherPlayer = makeSample(
        1u, 10u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}, {}, 0u, 0x0001'0002u, 1u,
            kLocalPlayer + 1u));
    EXPECT_EQ(
        controller.receiveLatestCertifiedSample(otherPlayer),
        WreckwaterCharacterControllerStatus::
            CharacterUnavailable);
    EXPECT_EQ(
        controller.binding(),
        WreckwaterCharacterControllerBinding::
            AwaitingCertifiedSnapshot);

    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            2u, 10u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    EXPECT_EQ(
        controller.binding(),
        WreckwaterCharacterControllerBinding::Ready);
    EXPECT_EQ(controller.characterHandle(), 0x0001'0001u);

    auto foreign = makeSample(
        3u, 11u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}));
    ++foreign.authoritative.identity.authorityEpoch;
    EXPECT_EQ(
        controller.receiveLatestCertifiedSample(foreign),
        WreckwaterCharacterControllerStatus::
            SnapshotIdentityMismatch);
    EXPECT_EQ(controller.authoritativeTick(), 10u);
    EXPECT_EQ(controller.characterHandle(), 0x0001'0001u);
}

TEST(WreckwaterCharacterController,
     PrunesAcknowledgedExpiresCoveredAndReplaysRetainedInput) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 10u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));

    expectAccepted(controller.recordSuccessfullySentInput(
        11u, 1u, 32'767, 0, false, false));
    expectAccepted(controller.recordSuccessfullySentInput(
        12u, 2u, 32'767, 0, false, false));
    expectAccepted(controller.recordSuccessfullySentInput(
        13u, 3u, -32'767, 0, false, false));
    for (uint64_t tick = 11u; tick <= 13u; ++tick) {
        expectAccepted(controller.advancePrediction(tick));
    }

    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            2u, 12u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}, {}, 1u))));
    EXPECT_EQ(controller.bufferedInputCount(), 1u);
    EXPECT_EQ(
        controller.lastAppliedCharacterInputSequence(), 3u);
    ASSERT_TRUE(controller.predictedPose());
    EXPECT_LT(
        absolute(controller.predictedPose().pose.feetPosition).x,
        0.0);

    const auto& reconciled = controller.telemetry();
    EXPECT_EQ(reconciled.acknowledgedInputsPruned, 1u);
    EXPECT_EQ(reconciled.expiredInputs, 1u);
    EXPECT_EQ(reconciled.rewinds, 1u);
    EXPECT_EQ(reconciled.replayedTicks, 1u);
    EXPECT_EQ(reconciled.replayedInputs, 1u);

    const auto predicted = controller.predictedPose();
    ASSERT_TRUE(predicted);
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            3u, 13u,
            makeCharacter(
                predicted.pose.mode,
                absolute(predicted.pose.feetPosition),
                predicted.pose.worldVelocity, 3u))));
    EXPECT_EQ(controller.bufferedInputCount(), 0u);
    EXPECT_EQ(
        controller.telemetry().acknowledgedInputsPruned, 2u);
}

TEST(WreckwaterCharacterController,
     HighestSequenceWinsSameTickAndReplayFillsNeutralGaps) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    const auto baseline = makeSample(
        1u, 1u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}));
    expectAccepted(
        controller.receiveLatestCertifiedSample(baseline));

    expectAccepted(controller.recordSuccessfullySentInput(
        3u, 1u, 32'767, 0, false, false));
    expectAccepted(controller.recordSuccessfullySentInput(
        3u, 2u, -32'767, 0, false, false));
    expectAccepted(controller.advancePrediction(2u));
    expectAccepted(controller.advancePrediction(3u));
    expectAccepted(controller.advancePrediction(4u));

    auto replacement = baseline;
    replacement.authoritative.snapshotSequence = 2u;
    expectAccepted(
        controller.receiveLatestCertifiedSample(replacement));

    ASSERT_TRUE(controller.predictedPose());
    EXPECT_LT(
        absolute(controller.predictedPose().pose.feetPosition).x,
        0.0);
    EXPECT_EQ(
        controller.lastAppliedCharacterInputSequence(), 2u);
    EXPECT_EQ(
        controller.telemetry().sameTickInputsSuperseded, 1u);
    EXPECT_EQ(controller.telemetry().replayedTicks, 3u);
    EXPECT_EQ(controller.telemetry().replayedInputs, 1u);
    EXPECT_EQ(controller.telemetry().neutralReplayTicks, 2u);
}

TEST(WreckwaterCharacterController,
     ModeHandleAndConnectionDiscontinuitiesHardPurge) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    expectAccepted(controller.recordSuccessfullySentInput(
        2u, 1u, 32'767, 0, false, false));
    expectAccepted(controller.advancePrediction(2u));

    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            2u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Swimming,
                {0.0, 10.0, 0.0}))));
    EXPECT_EQ(controller.bufferedInputCount(), 0u);
    EXPECT_EQ(controller.predictedTick(), 1u);
    EXPECT_EQ(controller.telemetry().purges, 1u);
    EXPECT_EQ(controller.telemetry().hardSnaps, 1u);

    expectAccepted(controller.recordSuccessfullySentInput(
        2u, 1u, 0, 32'767, false, false));
    expectAccepted(controller.advancePrediction(2u));
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            3u, 2u,
            makeCharacter(
                game::WreckwaterCharacterMode::Swimming,
                {0.0, 9.0, 0.0}, {}, 0u,
                0x0001'0001u, 2u))));
    EXPECT_EQ(controller.connectionGeneration(), 2u);
    EXPECT_EQ(controller.bufferedInputCount(), 0u);
    EXPECT_EQ(controller.telemetry().purges, 2u);

    expectAccepted(controller.recordSuccessfullySentInput(
        3u, 1u, 0, -32'767, false, false));
    expectAccepted(controller.advancePrediction(3u));
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            4u, 3u,
            makeCharacter(
                game::WreckwaterCharacterMode::Swimming,
                {0.0, 8.0, 0.0}, {}, 0u,
                0x0001'0002u, 2u))));
    EXPECT_EQ(controller.characterHandle(), 0x0001'0002u);
    EXPECT_EQ(controller.bufferedInputCount(), 0u);
    EXPECT_EQ(controller.telemetry().purges, 3u);
    EXPECT_EQ(
        controller.telemetry().acknowledgedInputsPruned, 0u);
}

TEST(WreckwaterCharacterController,
     SkiffGenerationDiscontinuityPurgesWithoutUsingAuthorityBody) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    const auto first = makePlatform(11u, 1u, {0.0, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeAboardCharacter(
                first, {0.0f, 0.5f, 0.0f}),
            first)));
    expectAccepted(controller.recordSuccessfullySentInput(
        2u, 1u, 32'767, 0, false, false));
    auto firstCurrent = first;
    firstCurrent.body = {900u, 900u};
    expectAccepted(controller.advancePrediction(
        2u, std::span(&firstCurrent, 1u)));

    const auto second = makePlatform(11u, 2u, {0.0, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            2u, 2u,
            makeAboardCharacter(
                second, {0.0f, 0.5f, 0.0f}),
            second)));
    EXPECT_EQ(controller.bufferedInputCount(), 0u);
    EXPECT_EQ(controller.predictedTick(), 2u);
    EXPECT_EQ(controller.telemetry().purges, 1u);
    EXPECT_EQ(controller.telemetry().hardSnaps, 1u);
}

TEST(WreckwaterCharacterController,
     SyntheticPlatformIdentityTracksRotatingDeckAcrossSector) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    constexpr float angularSpeed = 12.0f;
    const auto previous = makePlatform(
        11u, 1u, {127.99, 2.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {2.0f, 0.0f, 0.0f},
        {0.0f, angularSpeed, 0.0f});
    const glm::vec3 localFeet(0.0f, 0.5f, 1.0f);
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeAboardCharacter(previous, localFeet),
            previous)));

    expectAccepted(controller.advancePrediction(2u));
    const auto predictedPlatform =
        controller.platformPredictionFrame(2u);
    ASSERT_TRUE(predictedPlatform);
    ASSERT_EQ(predictedPlatform.platforms.size(), 1u);
    const auto& current = predictedPlatform.platforms[0];

    const auto pose = controller.predictedPose();
    ASSERT_TRUE(pose);
    EXPECT_EQ(
        pose.pose.mode,
        game::WreckwaterCharacterMode::OnSkiff);
    EXPECT_EQ(pose.pose.feetPosition.sector.x, 1);
    const glm::vec3 lever = current.orientation * localFeet;
    const glm::dvec3 expectedPosition =
        absolute(current.position) + glm::dvec3(lever);
    const glm::dvec3 actualPosition =
        absolute(pose.pose.feetPosition);
    EXPECT_NEAR(actualPosition.x, expectedPosition.x, 1.0e-5);
    EXPECT_NEAR(actualPosition.y, expectedPosition.y, 1.0e-5);
    EXPECT_NEAR(actualPosition.z, expectedPosition.z, 1.0e-5);
    const glm::vec3 expectedVelocity =
        current.linearVelocity
        + glm::cross(current.angularVelocity, lever);
    EXPECT_NEAR(
        pose.pose.worldVelocity.x, expectedVelocity.x, 1.0e-5f);
    EXPECT_NEAR(
        pose.pose.worldVelocity.y, expectedVelocity.y, 1.0e-5f);
    EXPECT_NEAR(
        pose.pose.worldVelocity.z, expectedVelocity.z, 1.0e-5f);
}

TEST(WreckwaterCharacterController,
     PlatformDiscontinuitiesFailClosedBeforeInvalidTickCommits) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    const auto baseline =
        makePlatform(11u, 1u, {0.0, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeAboardCharacter(
                baseline, {0.0f, 0.5f, 0.0f}),
            baseline)));
    const auto authoritativeBefore =
        controller.authoritativePose();
    ASSERT_TRUE(authoritativeBefore);

    auto invalid = baseline;
    invalid.position = physics::worldPositionFromAbsolute(
        {20.0, 2.0, 0.0});
    EXPECT_EQ(
        controller.advancePrediction(
            2u, std::span(&invalid, 1u)),
        WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity);
    EXPECT_EQ(
        controller.authoritativePose().pose,
        authoritativeBefore.pose);
    EXPECT_EQ(controller.predictedTick(), 1u);
    EXPECT_EQ(
        absolute(controller.predictedPose().pose.feetPosition),
        absolute(authoritativeBefore.pose.feetPosition));

    invalid = baseline;
    invalid.deckHalfExtents.x += 1.0f;
    EXPECT_EQ(
        controller.advancePrediction(
            2u, std::span(&invalid, 1u)),
        WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity);

    invalid = baseline;
    invalid.linearVelocity = {100.0f, 0.0f, 0.0f};
    EXPECT_EQ(
        controller.advancePrediction(
            2u, std::span(&invalid, 1u)),
        WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity);

    invalid = baseline;
    invalid.orientation = glm::angleAxis(
        1.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(
        controller.advancePrediction(
            2u, std::span(&invalid, 1u)),
        WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity);

    invalid = baseline;
    invalid.angularVelocity = {0.0f, 40.0f, 0.0f};
    EXPECT_EQ(
        controller.advancePrediction(
            2u, std::span(&invalid, 1u)),
        WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity);
    EXPECT_EQ(
        controller.telemetry().platformDiscontinuities, 5u);
    EXPECT_EQ(controller.telemetry().purges, 5u);
    EXPECT_EQ(controller.telemetry().hardSnaps, 5u);
    EXPECT_EQ(controller.historyCount(), 0u);
}

TEST(WreckwaterCharacterController,
     MissingOrReplacedLinkedSkiffClearsSyntheticIdentity) {
    const auto first =
        makePlatform(11u, 1u, {0.0, 2.0, 0.0});

    WreckwaterCharacterController missing;
    ASSERT_TRUE(missing.initialize(config()));
    expectAccepted(missing.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeAboardCharacter(
                first, {0.0f, 0.5f, 0.0f}),
            first)));
    expectAccepted(missing.advancePrediction(2u, {}));
    ASSERT_TRUE(missing.predictedPose());
    EXPECT_EQ(
        missing.predictedPose().pose.mode,
        game::WreckwaterCharacterMode::Airborne);
    EXPECT_EQ(missing.predictedPose().pose.skiffId, 0u);
    EXPECT_EQ(missing.predictedPose().pose.skiffGeneration, 0u);

    auto staleReturn = first;
    EXPECT_EQ(
        missing.advancePrediction(
            3u, std::span(&staleReturn, 1u)),
        WreckwaterCharacterControllerStatus::
            PlatformDiscontinuity);

    WreckwaterCharacterController replaced;
    ASSERT_TRUE(replaced.initialize(config()));
    expectAccepted(replaced.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeAboardCharacter(
                first, {0.0f, 0.5f, 0.0f}),
            first)));
    auto successor = first;
    successor.skiffGeneration = 2u;
    successor.body.generation += 1u;
    expectAccepted(replaced.advancePrediction(
        2u, std::span(&successor, 1u)));
    ASSERT_TRUE(replaced.predictedPose());
    EXPECT_EQ(
        replaced.predictedPose().pose.mode,
        game::WreckwaterCharacterMode::Airborne);
    EXPECT_EQ(replaced.predictedPose().pose.skiffId, 0u);
    EXPECT_EQ(replaced.predictedPose().pose.skiffGeneration, 0u);
}

TEST(WreckwaterCharacterController,
     RingAndSequenceExhaustionFailClosedAtExplicitBounds) {
    WreckwaterCharacterController inputBounded;
    ASSERT_TRUE(inputBounded.initialize(config(2u, 8u)));
    expectAccepted(inputBounded.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    expectAccepted(inputBounded.recordSuccessfullySentInput(
        2u, 1u, 0, 0, false, false));
    EXPECT_EQ(
        inputBounded.recordSuccessfullySentInput(
            4u, 2u, 0, 0, false, false),
        WreckwaterCharacterControllerStatus::
            InputCapacityExceeded);
    EXPECT_EQ(inputBounded.bufferedInputCount(), 0u);
    EXPECT_EQ(inputBounded.predictedTick(), 1u);
    EXPECT_EQ(inputBounded.telemetry().inputOverflows, 1u);

    WreckwaterCharacterController sequenceBounded;
    ASSERT_TRUE(sequenceBounded.initialize(config()));
    expectAccepted(sequenceBounded.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    EXPECT_EQ(
        sequenceBounded.recordSuccessfullySentInput(
            2u, std::numeric_limits<uint64_t>::max(),
            0, 0, false, false),
        WreckwaterCharacterControllerStatus::
            InputSequenceExhausted);
    EXPECT_EQ(sequenceBounded.bufferedInputCount(), 0u);

    WreckwaterCharacterController historyBounded;
    ASSERT_TRUE(historyBounded.initialize(config(8u, 2u)));
    expectAccepted(historyBounded.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    expectAccepted(historyBounded.advancePrediction(2u, {}));
    expectAccepted(historyBounded.advancePrediction(3u, {}));
    EXPECT_EQ(
        historyBounded.advancePrediction(4u, {}),
        WreckwaterCharacterControllerStatus::
            HistoryCapacityExceeded);
    EXPECT_EQ(historyBounded.predictedTick(), 1u);
    EXPECT_EQ(historyBounded.historyCount(), 0u);
    EXPECT_EQ(historyBounded.telemetry().historyOverflows, 1u);
}

TEST(WreckwaterCharacterController,
     CertifiedCorrectionRegeneratesPlatformsBeforeInputReplay) {
    WreckwaterCharacterController controller;
    auto controllerConfig = config();
    controllerConfig.platformPredictionTicks = 8u;
    ASSERT_TRUE(controller.initialize(controllerConfig));
    const auto baseline = makePlatform(
        11u, 1u, {0.0, 2.0, 0.0},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        {6.0f, 0.0f, 0.0f});
    const glm::vec3 localFeet(0.0f, 0.5f, 0.0f);
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 10u,
            makeAboardCharacter(baseline, localFeet),
            baseline)));

    for (uint64_t sequence = 1u; sequence <= 3u; ++sequence) {
        const uint64_t tick = 10u + sequence;
        expectAccepted(controller.recordSuccessfullySentInput(
            tick, sequence, 0, 0, false, false));
        expectAccepted(controller.advancePrediction(tick));
    }
    const double staleCharacterX =
        absolute(controller.predictedPose().pose.feetPosition).x;
    EXPECT_NEAR(staleCharacterX, 0.3, 1.0e-6);

    auto correction = baseline;
    correction.position = physics::worldPositionFromAbsolute(
        {0.105, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            2u, 11u,
            makeAboardCharacter(correction, localFeet),
            correction)));

    ASSERT_TRUE(controller.predictedPose());
    EXPECT_EQ(controller.predictedTick(), 13u);
    EXPECT_EQ(controller.bufferedInputCount(), 2u);
    EXPECT_EQ(controller.telemetry().replayedTicks, 2u);
    EXPECT_EQ(controller.telemetry().replayedInputs, 2u);
    const auto regenerated =
        controller.platformPredictionFrame(13u);
    ASSERT_TRUE(regenerated);
    ASSERT_EQ(regenerated.platforms.size(), 1u);
    EXPECT_EQ(regenerated.certifiedSnapshotSequence, 2u);
    EXPECT_EQ(regenerated.certifiedBaseTick, 11u);
    EXPECT_NEAR(
        absolute(regenerated.platforms[0].position).x,
        0.305, 1.0e-6);
    EXPECT_NEAR(
        absolute(controller.predictedPose().pose.feetPosition).x,
        0.305, 1.0e-6);
    EXPECT_NE(
        absolute(controller.predictedPose().pose.feetPosition).x,
        staleCharacterX);
}

TEST(WreckwaterCharacterController,
     PlatformDisappearanceReplacementAndCorrectionHardSnap) {
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(config()));
    const auto first =
        makePlatform(11u, 1u, {0.0, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}),
            first)));
    expectAccepted(controller.recordSuccessfullySentInput(
        2u, 1u, 0, 0, false, false));
    expectAccepted(controller.advancePrediction(2u));

    auto disappeared = makeSample(
        2u, 2u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}));
    disappeared.entityCount = 0u;
    disappeared.entities = {};
    expectAccepted(
        controller.receiveLatestCertifiedSample(disappeared));
    EXPECT_EQ(controller.bufferedInputCount(), 0u);
    EXPECT_EQ(controller.historyCount(), 0u);
    EXPECT_EQ(controller.predictedTick(), 2u);
    ASSERT_TRUE(controller.platformPredictionFrame(2u));
    EXPECT_TRUE(
        controller.platformPredictionFrame(2u)
            .platforms.empty());
    EXPECT_EQ(
        controller.telemetry().platformDiscontinuities, 1u);

    const auto replacement =
        makePlatform(11u, 2u, {0.0, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            3u, 3u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}),
            replacement)));
    EXPECT_EQ(
        controller.telemetry().platformDiscontinuities, 2u);
    ASSERT_TRUE(controller.platformPredictionFrame(3u));
    ASSERT_EQ(
        controller.platformPredictionFrame(3u)
            .platforms.size(),
        1u);
    EXPECT_EQ(
        controller.platformPredictionFrame(3u)
            .platforms[0].skiffGeneration,
        2u);

    auto impossible = replacement;
    impossible.position = physics::worldPositionFromAbsolute(
        {100.0, 2.0, 0.0});
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            4u, 4u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}),
            impossible)));
    EXPECT_EQ(
        controller.telemetry().platformDiscontinuities, 3u);
    EXPECT_EQ(controller.predictedTick(), 4u);
    EXPECT_EQ(
        controller.platformPredictionFrame(4u)
            .platforms[0].position,
        impossible.position);
}

TEST(WreckwaterCharacterController,
     PlatformPredictionHorizonFailsClosed) {
    auto boundedConfig = config(8u, 8u);
    boundedConfig.platformPredictionTicks = 2u;
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(boundedConfig));
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 1u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    expectAccepted(controller.advancePrediction(2u));
    expectAccepted(controller.advancePrediction(3u));
    EXPECT_EQ(
        controller.advancePrediction(4u),
        WreckwaterCharacterControllerStatus::
            PlatformPredictionHorizonExceeded);
    EXPECT_EQ(controller.predictedTick(), 1u);
    EXPECT_EQ(controller.historyCount(), 0u);
    EXPECT_EQ(controller.telemetry().historyOverflows, 1u);
}

TEST(WreckwaterCharacterController,
     ExactAndCertifiedTimelinePredictionSourcesDoNotMix) {
    const auto baseline = makeSample(
        1u, 1u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}));

    WreckwaterCharacterController timelineFirst;
    ASSERT_TRUE(timelineFirst.initialize(config()));
    expectAccepted(
        timelineFirst.receiveLatestCertifiedSample(baseline));
    expectAccepted(timelineFirst.advancePrediction(2u));
    EXPECT_EQ(
        timelineFirst.advancePrediction(3u, {}),
        WreckwaterCharacterControllerStatus::
            PlatformPredictionSourceMismatch);
    EXPECT_EQ(timelineFirst.predictedTick(), 1u);
    EXPECT_EQ(timelineFirst.historyCount(), 0u);

    expectAccepted(
        timelineFirst.advancePrediction(2u, {}));
    EXPECT_EQ(
        timelineFirst.advancePrediction(3u),
        WreckwaterCharacterControllerStatus::
            PlatformPredictionSourceMismatch);
    EXPECT_EQ(timelineFirst.predictedTick(), 1u);
    EXPECT_EQ(timelineFirst.historyCount(), 0u);

    WreckwaterCharacterController exactRewind;
    ASSERT_TRUE(exactRewind.initialize(config()));
    const auto rewindBase = makeSample(
        1u, 10u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {0.0, 10.0, 0.0}));
    expectAccepted(
        exactRewind.receiveLatestCertifiedSample(rewindBase));
    expectAccepted(exactRewind.recordSuccessfullySentInput(
        11u, 1u, 0, 0, false, false));
    const auto exactPlatform =
        makePlatform();
    expectAccepted(exactRewind.advancePrediction(
        11u, std::span(&exactPlatform, 1u)));

    auto correction = rewindBase;
    correction.authoritative.snapshotSequence = 2u;
    expectAccepted(
        exactRewind.receiveLatestCertifiedSample(correction));
    EXPECT_EQ(exactRewind.predictedTick(), 10u);
    EXPECT_EQ(exactRewind.bufferedInputCount(), 0u);
    EXPECT_EQ(exactRewind.historyCount(), 0u);
    EXPECT_EQ(exactRewind.telemetry().rewinds, 1u);
    EXPECT_EQ(exactRewind.telemetry().replayedTicks, 0u);
    EXPECT_EQ(exactRewind.telemetry().hardSnaps, 1u);
}

TEST(WreckwaterCharacterController,
     ReconciliationReplayIsBitStableForIdenticalEventStreams) {
    WreckwaterCharacterController first;
    WreckwaterCharacterController second;
    ASSERT_TRUE(first.initialize(config()));
    ASSERT_TRUE(second.initialize(config()));
    const auto baseline = makeSample(
        1u, 100u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {1.0, 20.0, 1.0}));
    expectAccepted(first.receiveLatestCertifiedSample(baseline));
    expectAccepted(second.receiveLatestCertifiedSample(baseline));

    for (uint64_t sequence = 1u; sequence <= 20u; ++sequence) {
        const uint64_t tick = 100u + sequence;
        const int16_t x = sequence % 3u == 0u
            ? int16_t{-20'000}
            : int16_t{20'000};
        const int16_t z = sequence % 2u == 0u
            ? int16_t{10'000}
            : int16_t{-10'000};
        expectAccepted(first.recordSuccessfullySentInput(
            tick, sequence, x, z, false, false));
        expectAccepted(second.recordSuccessfullySentInput(
            tick, sequence, x, z, false, false));
        expectAccepted(first.advancePrediction(tick));
        expectAccepted(second.advancePrediction(tick));
    }

    const auto correction = makeSample(
        2u, 110u,
        makeCharacter(
            game::WreckwaterCharacterMode::Airborne,
            {5.0, 18.0, 1.0}, {}, 8u));
    expectAccepted(first.receiveLatestCertifiedSample(correction));
    expectAccepted(second.receiveLatestCertifiedSample(correction));
    expectAccepted(first.advanceRenderCorrection(0.0375f));
    expectAccepted(second.advanceRenderCorrection(0.0375f));

    ASSERT_TRUE(first.authoritativePose());
    ASSERT_TRUE(first.predictedPose());
    ASSERT_TRUE(first.renderPose());
    EXPECT_EQ(
        first.authoritativePose().pose,
        second.authoritativePose().pose);
    EXPECT_EQ(first.predictedPose().pose, second.predictedPose().pose);
    EXPECT_EQ(first.renderPose().pose, second.renderPose().pose);
    EXPECT_EQ(first.telemetry(), second.telemetry());
    EXPECT_EQ(first.bufferedInputCount(), 10u);
    EXPECT_EQ(first.telemetry().acknowledgedInputsPruned, 8u);
    EXPECT_EQ(first.telemetry().expiredInputs, 2u);
    EXPECT_EQ(first.telemetry().replayedTicks, 10u);
    EXPECT_EQ(first.telemetry().replayedInputs, 10u);
}

TEST(WreckwaterCharacterController,
     PresentationCorrectionDecaysAndLargeErrorHardSnaps) {
    auto correctionConfig = config();
    correctionConfig.correctionHalfLifeSeconds = 0.1f;
    correctionConfig.maximumCorrectionDeltaSeconds = 0.25f;
    correctionConfig.hardSnapDistance = 2.0f;
    WreckwaterCharacterController controller;
    ASSERT_TRUE(controller.initialize(correctionConfig));
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            1u, 10u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.0, 10.0, 0.0}))));
    expectAccepted(controller.recordSuccessfullySentInput(
        11u, 1u, 32'767, 0, false, false));
    expectAccepted(controller.advancePrediction(11u));
    const double oldRenderedX =
        absolute(controller.renderPose().pose.feetPosition).x;

    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            2u, 10u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {0.5, 10.0, 0.0}))));
    const double correctedPredictedX =
        absolute(controller.predictedPose().pose.feetPosition).x;
    const double correctedRenderedX =
        absolute(controller.renderPose().pose.feetPosition).x;
    EXPECT_NEAR(correctedRenderedX, oldRenderedX, 1.0e-6);
    const double fullError =
        std::abs(correctedPredictedX - correctedRenderedX);
    EXPECT_GT(fullError, 0.49);

    expectAccepted(controller.advanceRenderCorrection(0.1f));
    const double halfRenderedX =
        absolute(controller.renderPose().pose.feetPosition).x;
    EXPECT_NEAR(
        std::abs(correctedPredictedX - halfRenderedX),
        fullError * 0.5, 1.0e-6);

    const uint64_t hardSnapsBefore =
        controller.telemetry().hardSnaps;
    expectAccepted(controller.receiveLatestCertifiedSample(
        makeSample(
            3u, 10u,
            makeCharacter(
                game::WreckwaterCharacterMode::Airborne,
                {10.0, 10.0, 0.0}))));
    EXPECT_EQ(
        controller.telemetry().hardSnaps,
        hardSnapsBefore + 1u);
    EXPECT_EQ(
        controller.renderPose().pose.feetPosition,
        controller.predictedPose().pose.feetPosition);
}

} // namespace
} // namespace voxy::client
