#include "network/wreckwater_client_replication.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace voxy::network {
namespace {

WreckwaterEntityState makeSkiff(
    float x = 0.0f, uint32_t netGeneration = 1u,
    uint32_t logicalGeneration = 1u) {
    WreckwaterEntityState entity;
    entity.netEntityId = 10u;
    entity.netGeneration = netGeneration;
    entity.kind = WreckwaterEntityKind::Skiff;
    entity.crew = WreckwaterCrew::CrewOne;
    entity.localPosition = {x, 0.0f, 0.0f};
    entity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    entity.linearVelocity = {0.0f, 0.0f, 0.0f};
    entity.angularVelocity = {0.0f, 0.0f, 0.0f};
    entity.shape = WreckwaterShape::Box;
    entity.dimensions = {3.0f, 1.0f, 6.0f};
    entity.packedMaterialFlags = 0x1000'0001u;
    entity.skiff = {
        .skiffId = 11u,
        .generation = logicalGeneration,
        .disposition = WreckwaterSkiffDisposition::Active,
    };
    return entity;
}

WreckwaterEntityState makeFreeCargo(float x = 0.0f) {
    WreckwaterEntityState entity;
    entity.netEntityId = 20u;
    entity.netGeneration = 1u;
    entity.kind = WreckwaterEntityKind::Cargo;
    entity.crew = WreckwaterCrew::None;
    entity.localPosition = {x, 0.5f, 0.0f};
    entity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    entity.linearVelocity = {0.0f, 0.0f, 0.0f};
    entity.angularVelocity = {0.0f, 0.0f, 0.0f};
    entity.shape = WreckwaterShape::Box;
    entity.dimensions = {1.0f, 1.0f, 1.0f};
    entity.packedMaterialFlags = 0x2000'0001u;
    entity.cargo = {
        .cargoId = 1u,
        .generation = 1u,
        .revision = 1u,
        .disposition = WreckwaterCargoDisposition::Free,
        .ownerCrew = WreckwaterCrew::None,
        .towingSkiffId = 0u,
        .towingSkiffGeneration = 0u,
    };
    return entity;
}

WreckwaterCharacterState makeCharacter(float x = 0.0f) {
    WreckwaterCharacterState character;
    character.characterHandle = 0x0001'0001u;
    character.stateFlags =
        static_cast<uint32_t>(WreckwaterCharacterMode::Airborne)
        | kWreckwaterCharacterStateActiveFlag
        | kWreckwaterCharacterStateConnectedFlag;
    character.playerId = 1u;
    character.connectionGeneration = 1u;
    character.localFeetPosition = {x, 2.0f, 0.0f};
    return character;
}

WreckwaterCharacterState makeAboardCharacter(
    float localX = 0.0f) {
    WreckwaterCharacterState character = makeCharacter(localX);
    character.stateFlags =
        static_cast<uint32_t>(WreckwaterCharacterMode::OnSkiff)
        | kWreckwaterCharacterStateActiveFlag
        | kWreckwaterCharacterStateConnectedFlag;
    character.localFeetPosition = {localX, 0.5f, 0.0f};
    character.skiffId = 11u;
    character.skiffGeneration = 1u;
    character.skiffLocalFeetPosition = {localX, 0.5f, 0.0f};
    return character;
}

WreckwaterCertifiedSnapshot makeSnapshot(
    uint64_t sequence, uint64_t tick,
    WreckwaterEntityState entity = makeSkiff()) {
    WreckwaterCertifiedSnapshot snapshot;
    snapshot.sessionId = 101u;
    snapshot.matchId = 202u;
    snapshot.worldId = 303u;
    snapshot.worldEpoch = 4u;
    snapshot.authorityEpoch = 5u;
    snapshot.snapshotSequence = sequence;
    snapshot.applicationTick = tick;
    snapshot.physicsEvidenceTick = tick;
    snapshot.phase = WreckwaterPhase::Live;
    snapshot.outcome = WreckwaterOutcomeType::Undecided;
    snapshot.winner = WreckwaterCrew::None;
    snapshot.matchStateHash = static_cast<uint32_t>(0x1000u + sequence);
    snapshot.eventStreamHash =
        static_cast<uint32_t>(0x2000u + sequence);
    snapshot.entities = {entity};
    EXPECT_TRUE(canonicalizeWreckwaterSnapshot(snapshot));
    return snapshot;
}

WreckwaterCertifiedSnapshot makeCharacterSnapshot(
    uint64_t sequence, uint64_t tick,
    WreckwaterCharacterState character) {
    WreckwaterCertifiedSnapshot snapshot =
        makeSnapshot(sequence, tick);
    snapshot.characters = {character};
    EXPECT_TRUE(canonicalizeWreckwaterSnapshot(snapshot));
    return snapshot;
}

void recanonicalize(WreckwaterCertifiedSnapshot& snapshot) {
    EXPECT_TRUE(canonicalizeWreckwaterSnapshot(snapshot));
    EXPECT_TRUE(isCanonicalWreckwaterSnapshot(snapshot));
}

const WreckwaterSampledEntity& sampledEntity(
    const WreckwaterClientSample& sample, NetEntityId id) {
    for (uint32_t index = 0u; index < sample.entityCount; ++index) {
        if (sample.entities[index].netEntityId == id) {
            return sample.entities[index];
        }
    }
    ADD_FAILURE() << "missing sampled entity " << id;
    return sample.entities[0];
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    RejectsDuplicateOutOfOrderIdentityAndTickRegressionUntilReset) {
    WreckwaterClientSnapshotBuffer buffer;
    EXPECT_TRUE(buffer.ingest(makeSnapshot(10u, 100u)));

    EXPECT_EQ(
        buffer.ingest(makeSnapshot(10u, 103u)).error,
        WreckwaterClientReplicationError::NonMonotonicSequence);
    EXPECT_TRUE(buffer.ingest(makeSnapshot(12u, 106u)));
    EXPECT_EQ(
        buffer.ingest(makeSnapshot(11u, 109u)).error,
        WreckwaterClientReplicationError::NonMonotonicSequence);
    EXPECT_EQ(
        buffer.ingest(makeSnapshot(13u, 105u)).error,
        WreckwaterClientReplicationError::RegressingApplicationTick);

    WreckwaterCertifiedSnapshot evidenceRegression =
        makeSnapshot(13u, 109u);
    evidenceRegression.physicsEvidenceTick = 99u;
    recanonicalize(evidenceRegression);
    EXPECT_EQ(
        buffer.ingest(evidenceRegression).error,
        WreckwaterClientReplicationError::RegressingEvidenceTick);

    WreckwaterCertifiedSnapshot foreign = makeSnapshot(13u, 109u);
    foreign.authorityEpoch += 1u;
    recanonicalize(foreign);
    EXPECT_EQ(
        buffer.ingest(foreign).error,
        WreckwaterClientReplicationError::IdentityMismatch);

    buffer.reset();
    EXPECT_TRUE(buffer.ingest(foreign));
    EXPECT_EQ(buffer.size(), 1u);
    EXPECT_EQ(buffer.identity().authorityEpoch, 6u);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    InterpolatesAcrossSectorBoundaryWithoutFarFloatConversion) {
    WreckwaterEntityState olderEntity = makeSkiff(127.0f);
    olderEntity.linearVelocity.x = 40.0f;
    olderEntity.sector = {900'000, -800'000, 700'000};
    WreckwaterEntityState newerEntity = olderEntity;
    newerEntity.localPosition.x = -127.0f;
    newerEntity.sector[0] += 1;

    WreckwaterClientSnapshotBuffer buffer;
    EXPECT_TRUE(buffer.ingest(makeSnapshot(1u, 100u, olderEntity)));
    EXPECT_TRUE(buffer.ingest(makeSnapshot(2u, 103u, newerEntity)));

    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled)
        << wreckwaterClientReplicationErrorName(sampled.error);
    const WreckwaterSampledEntity& entity =
        sampledEntity(sampled.sample, 10u);
    EXPECT_EQ(
        entity.motionMode, WreckwaterVisualMotionMode::Interpolated);
    EXPECT_EQ(
        entity.visualPose.sector,
        (std::array<int32_t, 3>{
            900'001, -800'000, 700'000}));
    EXPECT_FLOAT_EQ(entity.visualPose.localPosition.x, -128.0f);
    EXPECT_FLOAT_EQ(entity.visualPose.localPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(entity.visualPose.localPosition.z, 0.0f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    UsesPhysicsEvidenceTimelineWhenApplicationTickIsLater) {
    WreckwaterEntityState olderEntity = makeSkiff(-3.0f);
    WreckwaterEntityState newerEntity = makeSkiff(3.0f);
    WreckwaterCertifiedSnapshot older =
        makeSnapshot(1u, 103u, olderEntity);
    older.physicsEvidenceTick = 100u;
    recanonicalize(older);
    WreckwaterCertifiedSnapshot newer =
        makeSnapshot(2u, 106u, newerEntity);
    newer.physicsEvidenceTick = 103u;
    recanonicalize(newer);

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(older));
    ASSERT_TRUE(buffer.ingest(newer));
    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);

    const WreckwaterSampledEntity& entity =
        sampledEntity(sampled.sample, 10u);
    EXPECT_EQ(
        entity.motionMode, WreckwaterVisualMotionMode::Interpolated);
    EXPECT_NEAR(entity.visualPose.localPosition.x, 0.0f, 1.0e-6f);
    EXPECT_EQ(sampled.sample.authoritative.applicationTick, 106u);
    EXPECT_EQ(sampled.sample.authoritative.physicsEvidenceTick, 103u);
    EXPECT_EQ(sampled.sample.requestedPhysicsTick.whole, 101u);
    EXPECT_FLOAT_EQ(
        sampled.sample.requestedPhysicsTick.fraction, 0.5f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    UsesShortestQuaternionPathAcrossCanonicalAntipodes) {
    constexpr float sine85 = 0.9961947f;
    constexpr float cosine85 = 0.08715574f;
    WreckwaterEntityState olderEntity = makeSkiff(-1.0f);
    olderEntity.orientation = {0.0f, sine85, 0.0f, cosine85};
    WreckwaterEntityState newerEntity = makeSkiff(1.0f);
    newerEntity.orientation = {0.0f, -sine85, 0.0f, cosine85};

    WreckwaterClientSnapshotBuffer buffer;
    EXPECT_TRUE(buffer.ingest(makeSnapshot(1u, 100u, olderEntity)));
    EXPECT_TRUE(buffer.ingest(makeSnapshot(2u, 103u, newerEntity)));

    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    const WreckwaterQuaternion& orientation =
        sampledEntity(sampled.sample, 10u).visualPose.orientation;
    EXPECT_GT(orientation.y, 0.9999f);
    EXPECT_NEAR(orientation.w, 0.0f, 1.0e-5f);
    EXPECT_TRUE(isCanonicalWreckwaterQuaternion(orientation));
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    InterpolatesCharacterFeetAcrossSectorBoundary) {
    WreckwaterCharacterState older = makeCharacter(127.0f);
    older.sector = {900'000, -800'000, 700'000};
    older.worldVelocity.x = 40.0f;
    WreckwaterCharacterState newer = older;
    newer.sector[0] += 1;
    newer.localFeetPosition.x = -127.0f;

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(1u, 100u, older)));
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(2u, 103u, newer)));

    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    ASSERT_EQ(sampled.sample.characterCount, 1u);
    const WreckwaterSampledCharacter& character =
        sampled.sample.characters[0];
    EXPECT_EQ(
        character.motionMode,
        WreckwaterVisualMotionMode::Interpolated);
    EXPECT_EQ(
        character.visualPose.sector,
        (std::array<int32_t, 3>{
            900'001, -800'000, 700'000}));
    EXPECT_FLOAT_EQ(
        character.visualPose.localFeetPosition.x, -128.0f);
    EXPECT_FLOAT_EQ(
        character.visualPose.localFeetPosition.y, 2.0f);
    EXPECT_EQ(character.authoritativeState, newer);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    CharacterLifecycleDiscontinuityWaitsForItsEvidenceTick) {
    WreckwaterCharacterState older = makeCharacter(-20.0f);
    WreckwaterCharacterState newer = makeCharacter(20.0f);
    newer.connectionGeneration = 2u;

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(1u, 100u, older)));
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(2u, 103u, newer)));

    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    const WreckwaterSampledCharacter& character =
        sampled.sample.characters[0];
    EXPECT_EQ(
        character.motionMode, WreckwaterVisualMotionMode::Snapped);
    EXPECT_FLOAT_EQ(
        character.visualPose.localFeetPosition.x, -20.0f);
    EXPECT_EQ(
        character.authoritativeState.connectionGeneration, 2u);

    const WreckwaterClientSampleResult exact =
        buffer.sample({.whole = 103u, .fraction = 0.0f});
    ASSERT_TRUE(exact);
    EXPECT_FLOAT_EQ(
        exact.sample.characters[0]
            .visualPose.localFeetPosition.x,
        20.0f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    LongCharacterGapHoldsOlderPoseWithoutHermiteWarp) {
    WreckwaterCharacterState older = makeCharacter(-10.0f);
    older.worldVelocity.x = 100.0f;
    WreckwaterCharacterState newer = older;
    newer.localFeetPosition.x = 10.0f;

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(1u, 100u, older)));
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(2u, 112u, newer)));

    const WreckwaterClientSampleResult between =
        buffer.sample({.whole = 106u, .fraction = 0.0f});
    ASSERT_TRUE(between);
    EXPECT_EQ(
        between.sample.characters[0].motionMode,
        WreckwaterVisualMotionMode::Snapped);
    EXPECT_FLOAT_EQ(
        between.sample.characters[0]
            .visualPose.localFeetPosition.x,
        -10.0f);

    const WreckwaterClientSampleResult exact =
        buffer.sample({.whole = 112u, .fraction = 0.0f});
    ASSERT_TRUE(exact);
    EXPECT_FLOAT_EQ(
        exact.sample.characters[0]
            .visualPose.localFeetPosition.x,
        10.0f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    RotatingSkiffInterpolationKeepsCharacterAtLocalDeckPoint) {
    constexpr float sine45 = 0.70710677f;
    constexpr float cosine45 = 0.70710677f;
    constexpr float angularSpeed = 31.415926f;
    WreckwaterEntityState olderSkiff = makeSkiff();
    olderSkiff.angularVelocity.y = angularSpeed;
    WreckwaterEntityState newerSkiff = olderSkiff;
    newerSkiff.orientation = {
        0.0f, sine45, 0.0f, cosine45};

    WreckwaterCharacterState older = makeAboardCharacter(2.0f);
    WreckwaterCharacterState newer = older;
    newer.localFeetPosition = {0.0f, 0.5f, -2.0f};

    WreckwaterCertifiedSnapshot olderSnapshot =
        makeSnapshot(1u, 100u, olderSkiff);
    olderSnapshot.characters = {older};
    recanonicalize(olderSnapshot);
    WreckwaterCertifiedSnapshot newerSnapshot =
        makeSnapshot(2u, 103u, newerSkiff);
    newerSnapshot.characters = {newer};
    recanonicalize(newerSnapshot);

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(olderSnapshot));
    ASSERT_TRUE(buffer.ingest(newerSnapshot));
    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    const WreckwaterVisualCharacterPose& pose =
        sampled.sample.characters[0].visualPose;
    EXPECT_EQ(
        sampled.sample.characters[0].motionMode,
        WreckwaterVisualMotionMode::Interpolated);
    EXPECT_NEAR(
        pose.localFeetPosition.x, 1.4142135f, 2.0e-5f);
    EXPECT_NEAR(
        pose.localFeetPosition.y, 0.5f, 2.0e-5f);
    EXPECT_NEAR(
        pose.localFeetPosition.z, -1.4142135f, 2.0e-5f);
    EXPECT_NEAR(
        std::hypot(
            pose.localFeetPosition.x,
            pose.localFeetPosition.z),
        2.0f, 2.0e-5f);
    EXPECT_NEAR(
        std::hypot(
            pose.worldVelocity.x,
            pose.worldVelocity.z),
        2.0f * angularSpeed, 1.0e-3f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    SkiffExtrapolationPreservesRigidCharacterAttachment) {
    WreckwaterEntityState skiff = makeSkiff();
    skiff.angularVelocity.y = 1.5707963f;
    WreckwaterCharacterState character =
        makeAboardCharacter(2.0f);
    character.worldVelocity.z = -3.1415926f;

    WreckwaterCertifiedSnapshot snapshot =
        makeSnapshot(1u, 100u, skiff);
    snapshot.characters = {character};
    recanonicalize(snapshot);

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(snapshot));
    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.0f});
    ASSERT_TRUE(sampled);
    const WreckwaterVisualCharacterPose& pose =
        sampled.sample.characters[0].visualPose;
    EXPECT_EQ(
        sampled.sample.characters[0].motionMode,
        WreckwaterVisualMotionMode::Extrapolated);
    EXPECT_NEAR(
        std::hypot(
            pose.localFeetPosition.x,
            pose.localFeetPosition.z),
        2.0f, 2.0e-5f);
    EXPECT_LT(pose.localFeetPosition.z, 0.0f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    CharacterLifetimeHighWaterRejectsRebindRegressionAndAckRollback) {
    WreckwaterCharacterState baseline = makeCharacter();
    baseline.connectionGeneration = 2u;
    baseline.lastAppliedCharacterInputSequence = 10u;

    WreckwaterClientSnapshotBuffer generationBuffer;
    ASSERT_TRUE(generationBuffer.ingest(
        makeCharacterSnapshot(1u, 100u, baseline)));
    WreckwaterCharacterState regressedGeneration = baseline;
    regressedGeneration.connectionGeneration = 1u;
    EXPECT_EQ(
        generationBuffer.ingest(makeCharacterSnapshot(
            2u, 103u, regressedGeneration)).error,
        WreckwaterClientReplicationError::
            InvalidCharacterGeneration);

    WreckwaterClientSnapshotBuffer ackBuffer;
    ASSERT_TRUE(ackBuffer.ingest(
        makeCharacterSnapshot(1u, 100u, baseline)));
    WreckwaterCharacterState regressedAck = baseline;
    regressedAck.lastAppliedCharacterInputSequence = 9u;
    EXPECT_EQ(
        ackBuffer.ingest(makeCharacterSnapshot(
            2u, 103u, regressedAck)).error,
        WreckwaterClientReplicationError::
            InvalidCharacterGeneration);

    WreckwaterClientSnapshotBuffer handleBuffer;
    ASSERT_TRUE(handleBuffer.ingest(
        makeCharacterSnapshot(1u, 100u, baseline)));
    WreckwaterCharacterState reboundHandle = baseline;
    ++reboundHandle.characterHandle;
    EXPECT_EQ(
        handleBuffer.ingest(makeCharacterSnapshot(
            2u, 103u, reboundHandle)).error,
        WreckwaterClientReplicationError::
            InvalidCharacterGeneration);

    WreckwaterClientSnapshotBuffer playerBuffer;
    ASSERT_TRUE(playerBuffer.ingest(
        makeCharacterSnapshot(1u, 100u, baseline)));
    WreckwaterCharacterState reboundPlayer = baseline;
    ++reboundPlayer.playerId;
    EXPECT_EQ(
        playerBuffer.ingest(makeCharacterSnapshot(
            2u, 103u, reboundPlayer)).error,
        WreckwaterClientReplicationError::
            InvalidCharacterGeneration);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    DisconnectedCharacterReconnectRequiresNewGeneration) {
    WreckwaterCharacterState disconnected = makeCharacter();
    disconnected.stateFlags =
        static_cast<uint32_t>(WreckwaterCharacterMode::Airborne)
        | kWreckwaterCharacterStateActiveFlag;

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(1u, 100u, disconnected)));
    WreckwaterCharacterState sameGenerationConnected = disconnected;
    sameGenerationConnected.stateFlags |=
        kWreckwaterCharacterStateConnectedFlag;
    EXPECT_EQ(
        buffer.ingest(makeCharacterSnapshot(
            2u, 103u, sameGenerationConnected)).error,
        WreckwaterClientReplicationError::
            InvalidCharacterGeneration);

    ++sameGenerationConnected.connectionGeneration;
    ASSERT_TRUE(buffer.ingest(makeCharacterSnapshot(
        3u, 106u, sameGenerationConnected)));
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    CharacterExtrapolationUsesTheSameBoundedStaleHorizon) {
    WreckwaterCharacterState character = makeCharacter(4.0f);
    character.worldVelocity.x = 60.0f;

    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(
        makeCharacterSnapshot(1u, 100u, character)));

    const WreckwaterClientSampleResult moving =
        buffer.sample({.whole = 102u, .fraction = 0.5f});
    ASSERT_TRUE(moving);
    EXPECT_EQ(
        moving.sample.characters[0].motionMode,
        WreckwaterVisualMotionMode::Extrapolated);
    EXPECT_FLOAT_EQ(
        moving.sample.characters[0]
            .visualPose.localFeetPosition.x,
        6.5f);

    const WreckwaterClientSampleResult frozen =
        buffer.sample({.whole = 999u, .fraction = 0.0f});
    ASSERT_TRUE(frozen);
    EXPECT_TRUE(frozen.sample.stale);
    EXPECT_EQ(
        frozen.sample.characters[0].motionMode,
        WreckwaterVisualMotionMode::FrozenStale);
    EXPECT_FLOAT_EQ(
        frozen.sample.characters[0]
            .visualPose.localFeetPosition.x,
        7.0f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    SnapsAcrossEntityGenerationDiscontinuity) {
    WreckwaterEntityState olderEntity = makeSkiff(-20.0f, 7u);
    WreckwaterEntityState newerEntity = makeSkiff(20.0f, 8u);

    WreckwaterClientSnapshotBuffer buffer;
    EXPECT_TRUE(buffer.ingest(makeSnapshot(1u, 100u, olderEntity)));
    EXPECT_TRUE(buffer.ingest(makeSnapshot(2u, 103u, newerEntity)));

    const WreckwaterClientSampleResult sampled =
        buffer.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    const WreckwaterSampledEntity& entity =
        sampledEntity(sampled.sample, 10u);
    EXPECT_EQ(entity.netGeneration, 8u);
    EXPECT_EQ(entity.motionMode, WreckwaterVisualMotionMode::Snapped);
    EXPECT_FLOAT_EQ(entity.visualPose.localPosition.x, 20.0f);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    ExtrapolatesExactlyThreeTicksThenFreezesAndMarksStale) {
    WreckwaterEntityState entity = makeSkiff();
    entity.linearVelocity.x = 60.0f;
    WreckwaterClientSnapshotBuffer buffer;
    EXPECT_TRUE(buffer.ingest(makeSnapshot(1u, 100u, entity)));

    const WreckwaterClientSampleResult atLimit =
        buffer.sample({.whole = 103u, .fraction = 0.0f});
    ASSERT_TRUE(atLimit);
    const WreckwaterSampledEntity& limitEntity =
        sampledEntity(atLimit.sample, 10u);
    EXPECT_EQ(
        limitEntity.motionMode,
        WreckwaterVisualMotionMode::Extrapolated);
    EXPECT_FLOAT_EQ(limitEntity.visualPose.localPosition.x, 3.0f);
    EXPECT_FALSE(atLimit.sample.stale);

    const WreckwaterClientSampleResult stale =
        buffer.sample({.whole = 103u, .fraction = 0.5f});
    ASSERT_TRUE(stale);
    const WreckwaterSampledEntity& staleEntity =
        sampledEntity(stale.sample, 10u);
    EXPECT_EQ(
        staleEntity.motionMode,
        WreckwaterVisualMotionMode::FrozenStale);
    EXPECT_FLOAT_EQ(staleEntity.visualPose.localPosition.x, 3.0f);
    EXPECT_TRUE(stale.sample.stale);
    EXPECT_EQ(stale.sample.evaluatedPhysicsTick.whole, 103u);
    EXPECT_FLOAT_EQ(
        stale.sample.evaluatedPhysicsTick.fraction, 0.0f);

    const WreckwaterClientSampleResult hugeTick = buffer.sample(
        {.whole = std::numeric_limits<uint64_t>::max(),
         .fraction = 0.0f});
    ASSERT_TRUE(hugeTick);
    EXPECT_EQ(
        hugeTick.sample.evaluatedPhysicsTick,
        stale.sample.evaluatedPhysicsTick);
    EXPECT_EQ(
        sampledEntity(hugeTick.sample, 10u).visualPose,
        staleEntity.visualPose);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    RejectsVisualPositionOverflowAtWorldSectorLimit) {
    WreckwaterEntityState entity = makeSkiff(127.0f);
    entity.sector[0] = kWreckwaterMaximumSectorMagnitude;
    entity.linearVelocity.x = 60.0f;
    WreckwaterClientSnapshotBuffer buffer;
    ASSERT_TRUE(buffer.ingest(makeSnapshot(1u, 100u, entity)));

    EXPECT_EQ(
        buffer.sample({.whole = 103u, .fraction = 0.0f}).error,
        WreckwaterClientReplicationError::PositionOverflow);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    GameplayCollisionHashesAndAttachmentStateAreNeverPredicted) {
    WreckwaterCertifiedSnapshot older =
        makeSnapshot(1u, 100u, makeSkiff(-4.0f));
    WreckwaterEntityState olderCargo = makeFreeCargo(-2.0f);
    older.entities.push_back(olderCargo);
    older.crewOneScore = 1u;
    older.matchStateHash = 0x1111'1111u;
    older.eventStreamHash = 0x2222'2222u;
    recanonicalize(older);

    WreckwaterCertifiedSnapshot newer =
        makeSnapshot(2u, 103u, makeSkiff(4.0f));
    WreckwaterEntityState newerCargo = makeFreeCargo(2.0f);
    newerCargo.crew = WreckwaterCrew::CrewOne;
    newerCargo.shape = WreckwaterShape::Cylinder;
    newerCargo.dimensions = {1.5f, 2.5f, 1.5f};
    newerCargo.cargo.revision = 2u;
    newerCargo.cargo.disposition =
        WreckwaterCargoDisposition::Towed;
    newerCargo.cargo.ownerCrew = WreckwaterCrew::CrewOne;
    newerCargo.cargo.towingSkiffId = 11u;
    newerCargo.cargo.towingSkiffGeneration = 1u;
    newerCargo.attachment = {
        .attachmentId = 9001u,
        .generation = 3u,
        .state = WreckwaterAttachmentState::Attached,
    };
    newer.entities.push_back(newerCargo);
    newer.phase = WreckwaterPhase::Finished;
    newer.crewOneScore = 9u;
    newer.outcome = WreckwaterOutcomeType::CrewVictory;
    newer.winner = WreckwaterCrew::CrewOne;
    newer.matchStateHash = 0xaaaa'aaaau;
    newer.eventStreamHash = 0xbbbb'bbbbu;
    recanonicalize(newer);

    WreckwaterClientSnapshotBuffer newerPolicy;
    ASSERT_TRUE(newerPolicy.ingest(older));
    ASSERT_TRUE(newerPolicy.ingest(newer));
    const WreckwaterClientSampleResult sampled =
        newerPolicy.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    EXPECT_EQ(sampled.sample.authoritative.snapshotSequence, 2u);
    EXPECT_EQ(sampled.sample.authoritative.crewOneScore, 9u);
    EXPECT_EQ(
        sampled.sample.authoritative.outcome,
        WreckwaterOutcomeType::CrewVictory);
    EXPECT_EQ(sampled.sample.authoritative.winner, WreckwaterCrew::CrewOne);
    EXPECT_EQ(
        sampled.sample.authoritative.matchStateHash, 0xaaaa'aaaau);
    EXPECT_EQ(
        sampled.sample.authoritative.eventStreamHash, 0xbbbb'bbbbu);

    const WreckwaterSampledEntity& cargo =
        sampledEntity(sampled.sample, 20u);
    EXPECT_EQ(cargo.motionMode, WreckwaterVisualMotionMode::Interpolated);
    EXPECT_NE(
        cargo.visualPose.localPosition.x,
        cargo.authoritativeState.localPosition.x);
    EXPECT_EQ(
        cargo.authoritativeState.cargo.disposition,
        WreckwaterCargoDisposition::Towed);
    EXPECT_EQ(
        cargo.authoritativeState.cargo.ownerCrew,
        WreckwaterCrew::CrewOne);
    EXPECT_EQ(cargo.authoritativeState.attachment.attachmentId, 9001u);
    EXPECT_EQ(
        cargo.authoritativeState.attachment.state,
        WreckwaterAttachmentState::Attached);
    EXPECT_EQ(
        cargo.authoritativeState.shape, WreckwaterShape::Cylinder);
    EXPECT_EQ(cargo.authoritativeState.dimensions, newerCargo.dimensions);

    WreckwaterClientSnapshotBuffer olderPolicy({
        .authoritativePolicy =
            WreckwaterAuthoritativeSamplePolicy::OlderSnapshot,
    });
    ASSERT_TRUE(olderPolicy.ingest(older));
    ASSERT_TRUE(olderPolicy.ingest(newer));
    const WreckwaterClientSampleResult oldSample =
        olderPolicy.sample({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(oldSample);
    EXPECT_EQ(oldSample.sample.authoritative.snapshotSequence, 1u);
    EXPECT_EQ(oldSample.sample.authoritative.crewOneScore, 1u);
    const WreckwaterSampledEntity& oldCargo =
        sampledEntity(oldSample.sample, 20u);
    EXPECT_EQ(
        oldCargo.authoritativeState.cargo.disposition,
        WreckwaterCargoDisposition::Free);
    EXPECT_EQ(
        oldCargo.authoritativeState.attachment.state,
        WreckwaterAttachmentState::None);
    EXPECT_EQ(oldCargo.authoritativeState.shape, WreckwaterShape::Box);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    RejectsMalformedStateGenerationRegressionAndInvalidRenderFractions) {
    WreckwaterClientSnapshotBuffer buffer;
    WreckwaterCertifiedSnapshot first =
        makeSnapshot(1u, 100u, makeSkiff(0.0f, 7u, 4u));
    ASSERT_TRUE(buffer.ingest(first));

    WreckwaterCertifiedSnapshot netRegression =
        makeSnapshot(2u, 103u, makeSkiff(1.0f, 6u, 4u));
    EXPECT_EQ(
        buffer.ingest(netRegression).error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);

    WreckwaterCertifiedSnapshot logicalRegression =
        makeSnapshot(2u, 103u, makeSkiff(1.0f, 8u, 3u));
    EXPECT_EQ(
        buffer.ingest(logicalRegression).error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);

    WreckwaterCertifiedSnapshot reusedGeneration =
        makeSnapshot(2u, 103u, makeSkiff(1.0f, 7u, 4u));
    reusedGeneration.entities[0].skiff.skiffId = 22u;
    recanonicalize(reusedGeneration);
    EXPECT_EQ(
        buffer.ingest(reusedGeneration).error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);

    WreckwaterCertifiedSnapshot malformed = makeSnapshot(2u, 103u);
    malformed.entities[0].localPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(
        buffer.ingest(malformed).error,
        WreckwaterClientReplicationError::MalformedSnapshot);

    EXPECT_EQ(
        buffer.sample({
            .whole = 100u,
            .fraction = std::numeric_limits<float>::infinity(),
        }).error,
        WreckwaterClientReplicationError::InvalidRenderTick);
    EXPECT_EQ(
        buffer.sample({.whole = 100u, .fraction = -0.0f}).error,
        WreckwaterClientReplicationError::InvalidRenderTick);
    EXPECT_EQ(
        buffer.sample({.whole = 100u, .fraction = 1.0f}).error,
        WreckwaterClientReplicationError::InvalidRenderTick);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    GenerationHighWaterSurvivesPoseHistoryEviction) {
    WreckwaterClientSnapshotBuffer buffer({
        .historySnapshots = 2u,
    });
    ASSERT_TRUE(buffer.ingest(
        makeSnapshot(1u, 100u, makeSkiff(0.0f, 7u, 4u))));

    WreckwaterEntityState second = makeSkiff();
    second.netEntityId = 30u;
    second.skiff.skiffId = 31u;
    ASSERT_TRUE(buffer.ingest(makeSnapshot(2u, 103u, second)));
    WreckwaterEntityState third = makeSkiff();
    third.netEntityId = 40u;
    third.skiff.skiffId = 41u;
    ASSERT_TRUE(buffer.ingest(makeSnapshot(3u, 106u, third)));
    ASSERT_EQ(buffer.size(), 2u);

    EXPECT_EQ(
        buffer.ingest(
            makeSnapshot(4u, 109u, makeSkiff(0.0f, 6u, 4u)))
            .error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);

    WreckwaterEntityState staleLogical =
        makeSkiff(0.0f, 1u, 3u);
    staleLogical.netEntityId = 50u;
    EXPECT_EQ(
        buffer.ingest(makeSnapshot(4u, 109u, staleLogical)).error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    AttachmentGenerationAndOwnerBindingSurviveHistoryEviction) {
    const auto attachedSnapshot =
        [](uint64_t sequence, uint64_t tick, NetEntityId cargoNetId,
           uint32_t cargoId, uint32_t attachmentGeneration) {
            WreckwaterCertifiedSnapshot snapshot =
                makeSnapshot(sequence, tick, makeSkiff());
            WreckwaterEntityState cargo = makeFreeCargo();
            cargo.netEntityId = cargoNetId;
            cargo.crew = WreckwaterCrew::CrewOne;
            cargo.cargo.cargoId = cargoId;
            cargo.cargo.disposition =
                WreckwaterCargoDisposition::Towed;
            cargo.cargo.ownerCrew = WreckwaterCrew::CrewOne;
            cargo.cargo.towingSkiffId = 11u;
            cargo.cargo.towingSkiffGeneration = 1u;
            cargo.attachment = {
                .attachmentId = 9001u,
                .generation = attachmentGeneration,
                .state = WreckwaterAttachmentState::Attached,
            };
            snapshot.entities.push_back(cargo);
            recanonicalize(snapshot);
            return snapshot;
        };

    WreckwaterClientSnapshotBuffer buffer({
        .historySnapshots = 2u,
    });
    ASSERT_TRUE(buffer.ingest(attachedSnapshot(1u, 100u, 20u, 1u, 5u)));

    WreckwaterEntityState second = makeSkiff();
    second.netEntityId = 30u;
    second.skiff.skiffId = 31u;
    ASSERT_TRUE(buffer.ingest(makeSnapshot(2u, 103u, second)));
    WreckwaterEntityState third = makeSkiff();
    third.netEntityId = 40u;
    third.skiff.skiffId = 41u;
    ASSERT_TRUE(buffer.ingest(makeSnapshot(3u, 106u, third)));

    EXPECT_EQ(
        buffer.ingest(
            attachedSnapshot(4u, 109u, 20u, 1u, 4u))
            .error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);
    EXPECT_EQ(
        buffer.ingest(
            attachedSnapshot(4u, 109u, 25u, 2u, 5u))
            .error,
        WreckwaterClientReplicationError::InvalidEntityGeneration);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    LifetimeRegistryIsBoundedAndClearsOnlyOnReset) {
    WreckwaterClientSnapshotBuffer buffer({
        .historySnapshots = 2u,
    });
    for (uint32_t index = 0u;
         index < kWreckwaterMaximumSnapshotEntities; ++index) {
        WreckwaterEntityState entity = makeSkiff();
        entity.netEntityId = 1'000u + index;
        entity.skiff.skiffId = 2'000u + index;
        ASSERT_TRUE(buffer.ingest(makeSnapshot(
            uint64_t{index} + 1u, uint64_t{index} * 3u, entity)));
    }

    WreckwaterEntityState excess = makeSkiff();
    excess.netEntityId = 9'000u;
    excess.skiff.skiffId = 9'001u;
    const WreckwaterCertifiedSnapshot excessSnapshot = makeSnapshot(
        65u, 192u, excess);
    EXPECT_EQ(
        buffer.ingest(excessSnapshot).error,
        WreckwaterClientReplicationError::
            IdentityRegistryCapacityExceeded);
    EXPECT_EQ(buffer.size(), 2u);

    buffer.reset();
    EXPECT_TRUE(buffer.ingest(excessSnapshot));
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    SameEvidenceReplacementAndBoundedHistoryReplayDeterministically) {
    WreckwaterClientSnapshotBuffer buffer({
        .historySnapshots = 3u,
        .maximumEntities = 1u,
    });
    ASSERT_TRUE(buffer.ingest(makeSnapshot(1u, 100u)));

    WreckwaterCertifiedSnapshot replacement = makeSnapshot(2u, 103u);
    replacement.physicsEvidenceTick = 100u;
    replacement.matchStateHash = 0xdead'beefu;
    recanonicalize(replacement);
    const WreckwaterClientIngestResult replaced =
        buffer.ingest(replacement);
    ASSERT_TRUE(replaced);
    EXPECT_TRUE(replaced.replacedSameEvidenceTick);
    EXPECT_EQ(buffer.size(), 1u);
    const WreckwaterClientSampleResult replacementSample =
        buffer.sample({.whole = 100u, .fraction = 0.0f});
    ASSERT_TRUE(replacementSample);
    EXPECT_EQ(
        replacementSample.sample.authoritative.applicationTick, 103u);
    EXPECT_EQ(
        replacementSample.sample.authoritative.physicsEvidenceTick, 100u);
    EXPECT_EQ(
        replacementSample.sample.authoritative.matchStateHash,
        0xdead'beefu);

    for (uint64_t sequence = 3u; sequence <= 7u; ++sequence) {
        const uint64_t tick = 100u + (sequence - 2u) * 3u;
        ASSERT_TRUE(buffer.ingest(makeSnapshot(sequence, tick)));
    }
    EXPECT_EQ(buffer.size(), 3u);
    EXPECT_EQ(buffer.capacity(), 3u);
    EXPECT_EQ(buffer.telemetry().historyHighWater, 3u);
    EXPECT_EQ(buffer.telemetry().evictedSnapshots, 3u);
    EXPECT_EQ(
        buffer.telemetry().replacedSameEvidenceTickSnapshots, 1u);

    const WreckwaterPhysicsRenderTick renderTick{
        .whole = 112u,
        .fraction = 0.25f,
    };
    const WreckwaterClientSampleResult firstReplay =
        buffer.sample(renderTick);
    const WreckwaterClientSampleResult secondReplay =
        buffer.sample(renderTick);
    ASSERT_TRUE(firstReplay);
    ASSERT_TRUE(secondReplay);
    EXPECT_EQ(firstReplay.sample, secondReplay.sample);

    WreckwaterClientSnapshotBuffer tooSmall({
        .historySnapshots = 2u,
        .maximumEntities = 1u,
    });
    WreckwaterCertifiedSnapshot twoEntities = makeSnapshot(1u, 1u);
    twoEntities.entities.push_back(makeFreeCargo());
    recanonicalize(twoEntities);
    EXPECT_EQ(
        tooSmall.ingest(twoEntities).error,
        WreckwaterClientReplicationError::EntityCapacityExceeded);
    EXPECT_EQ(tooSmall.size(), 0u);
}

TEST(
    WreckwaterClientSnapshotBufferTest,
    InvalidConfigurationAndEmptyHistoryFailClosed) {
    WreckwaterClientSnapshotBuffer invalid({
        .historySnapshots = 1u,
    });
    EXPECT_FALSE(invalid.initialized());
    EXPECT_EQ(
        invalid.ingest(makeSnapshot(1u, 1u)).error,
        WreckwaterClientReplicationError::InvalidConfiguration);
    EXPECT_EQ(
        invalid.sample({}).error,
        WreckwaterClientReplicationError::InvalidConfiguration);

    WreckwaterClientSnapshotBuffer empty;
    EXPECT_EQ(
        empty.sample({}).error,
        WreckwaterClientReplicationError::EmptyHistory);
}

} // namespace
} // namespace voxy::network
