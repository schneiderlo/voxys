#include <gtest/gtest.h>

#include "game/wreckwater_live_world.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace voxy::game {
namespace {

using physics::AttachmentHandle;
using physics::BodyHandle;
using physics::PhysicsMutationBatch;
using physics::PhysicsMutationResult;
using physics::PhysicsMutationStatus;
using physics::PreparedPhysicsMutation;
using physics::WorldPosition;

static_assert(!std::is_copy_constructible_v<WreckwaterLiveWorld>);
static_assert(!std::is_move_constructible_v<WreckwaterLiveWorld>);
static_assert(noexcept(
    std::declval<WreckwaterLiveWorld&>().applyAtomically(
        std::span<const MatchWorldIntent>{},
        std::span<MatchWorldIntentAck>{})));

constexpr WreckwaterEntityKey playerKey(
    PlayerId player = 101u, uint32_t generation = 7u) {
    return {WreckwaterEntityKind::Player, player, generation};
}

constexpr WreckwaterEntityKey skiffKey(
    SkiffId skiff, uint32_t generation = 1u) {
    return {WreckwaterEntityKind::Skiff, skiff, generation};
}

constexpr WreckwaterEntityKey cargoKey(
    CargoId cargo, uint32_t generation = 1u) {
    return {WreckwaterEntityKind::Cargo, cargo, generation};
}

WorldPosition position(float x, float y = 0.0f, float z = 0.0f) {
    return {.sector = {0, 0, 0}, .local = {x, y, z}};
}

class FakePhysicsTransactions final
    : public IWreckwaterPhysicsTransactions {
public:
    enum class PrepareMode {
        Normal,
        Reject,
        MissingCreatedHandle,
        DuplicateCreatedHandle,
        InvalidCreatedHandle,
    };

    PreparedPhysicsMutation prepare(
        const PhysicsMutationBatch& batch) noexcept override {
        ++prepareCalls;
        lastDestroyCount = batch.attachmentDestroys.size();
        lastCreateCount = batch.attachmentCreates.size();
        for (size_t index = 0u; index < lastDestroyCount; ++index) {
            destroyed[index] = batch.attachmentDestroys[index];
        }
        for (size_t index = 0u; index < lastCreateCount; ++index) {
            createdDescs[index] = batch.attachmentCreates[index];
        }

        PreparedPhysicsMutation result;
        result.targetTick = targetTick;
        if (prepareMode == PrepareMode::Reject) {
            result.status = PhysicsMutationStatus::CapacityExceeded;
            return result;
        }
        result.status = PhysicsMutationStatus::Prepared;
        result.token = nextToken++;
        preparedCreatedCount = lastCreateCount;
        for (size_t index = 0u; index < lastCreateCount; ++index) {
            AttachmentHandle handle =
                nextCreatedCount > index
                ? nextCreated[index]
                : AttachmentHandle{
                    static_cast<uint32_t>(100u + index),
                    1u,
                };
            if (prepareMode == PrepareMode::DuplicateCreatedHandle
                && index != 0u) {
                handle = preparedCreated[0];
            } else if (
                prepareMode == PrepareMode::InvalidCreatedHandle
                && index == 0u) {
                handle = {};
            }
            preparedCreated[index] = handle;
        }
        if (prepareMode == PrepareMode::MissingCreatedHandle
            && preparedCreatedCount != 0u) {
            --preparedCreatedCount;
        }
        result.createdAttachments = std::span(
            preparedCreated.data(), preparedCreatedCount);
        activeToken = result.token;
        return result;
    }

    PhysicsMutationResult commit(
        const PreparedPhysicsMutation& prepared) noexcept override {
        ++commitCalls;
        if (commitReject || prepared.token != activeToken) {
            return {
                .status = PhysicsMutationStatus::InvalidToken,
            };
        }
        activeToken = 0u;
        return {
            .status = PhysicsMutationStatus::Committed,
            .targetTick = targetTick,
            .bodyCommandCount = 0u,
            .destroyedAttachmentCount =
                static_cast<uint32_t>(lastDestroyCount),
            .createdAttachmentCount =
                static_cast<uint32_t>(lastCreateCount),
        };
    }

    bool discard(
        const PreparedPhysicsMutation& prepared) noexcept override {
        ++discardCalls;
        if (prepared.token == 0u || prepared.token != activeToken) {
            return false;
        }
        activeToken = 0u;
        return true;
    }

    void useCreated(
        std::span<const AttachmentHandle> handles) {
        nextCreatedCount = handles.size();
        std::copy(
            handles.begin(), handles.end(), nextCreated.begin());
    }

    PrepareMode prepareMode = PrepareMode::Normal;
    bool commitReject = false;
    uint64_t targetTick = 11u;
    uint64_t nextToken = 1u;
    uint64_t activeToken = 0u;
    uint32_t prepareCalls = 0u;
    uint32_t commitCalls = 0u;
    uint32_t discardCalls = 0u;
    size_t lastDestroyCount = 0u;
    size_t lastCreateCount = 0u;
    std::array<
        AttachmentHandle,
        kWreckwaterMaximumIntentsPerTick> destroyed{};
    std::array<
        physics::DistanceAttachmentDesc,
        kWreckwaterMaximumIntentsPerTick> createdDescs{};
    std::array<
        AttachmentHandle,
        kWreckwaterMaximumIntentsPerTick> nextCreated{};
    size_t nextCreatedCount = 0u;
    std::array<
        AttachmentHandle,
        kWreckwaterMaximumIntentsPerTick> preparedCreated{};
    size_t preparedCreatedCount = 0u;
};

WreckwaterLiveWorld::Config liveConfig() {
    WreckwaterLiveWorld::Config config;
    config.matchId = 77u;
    config.worldId = 88u;
    config.worldEpoch = 3u;
    config.authorityEpoch = 5u;
    config.maximumInteractionDistance = 10.0f;
    config.extractionRadius = 5.0f;
    config.extractionCenters[0] = position(0.0f);
    config.extractionCenters[1] = position(40.0f);
    return config;
}

struct BasicWorld {
    FakePhysicsTransactions physics;
    WreckwaterLiveWorld world;
    std::array<WreckwaterEvidenceBody, 4u> evidence{
        WreckwaterEvidenceBody{playerKey(), position(1.0f), true},
        WreckwaterEvidenceBody{skiffKey(1u), position(1.0f), true},
        WreckwaterEvidenceBody{skiffKey(2u), position(2.0f), true},
        WreckwaterEvidenceBody{cargoKey(1u), position(2.0f), true},
    };

    void initialize(bool certify = true) {
        ASSERT_TRUE(world.initialize(liveConfig(), physics));
        EXPECT_EQ(
            world.bindEntity(playerKey(), BodyHandle{21u, 3u}),
            WreckwaterBindingResult::Bound);
        EXPECT_EQ(
            world.bindEntity(skiffKey(1u), BodyHandle{21u, 3u}),
            WreckwaterBindingResult::Bound);
        EXPECT_EQ(
            world.bindEntity(skiffKey(2u), BodyHandle{22u, 3u}),
            WreckwaterBindingResult::Bound);
        EXPECT_EQ(
            world.bindEntity(cargoKey(1u), BodyHandle{31u, 4u}),
            WreckwaterBindingResult::Bound);
        if (certify) certifyAt(10u);
    }

    void certifyAt(uint64_t tick) {
        ASSERT_TRUE(world.certifyEvidenceFrame({
            .worldId = 88u,
            .worldEpoch = 3u,
            .physicsTick = tick,
            .bodies = evidence,
        }));
    }
};

MatchWorldIntent attachIntent(
    uint64_t applicationTick = 11u,
    uint64_t evidenceTick = 10u,
    uint64_t sequence = 1u,
    CargoId cargo = 1u,
    SkiffId targetSkiff = 1u) {
    return {
        .type = MatchWorldIntentType::AttachTow,
        .source = MatchWorldIntentSource::PlayerCommand,
        .matchId = 77u,
        .worldId = 88u,
        .worldEpoch = 3u,
        .authorityEpoch = 5u,
        .applicationTick = applicationTick,
        .physicsEvidenceTick = evidenceTick,
        .sourceSequence = sequence,
        .playerId = 101u,
        .connectionId = 501u,
        // Deliberately unrelated to playerKey().generation.
        .connectionGeneration = 19u,
        .crew = CrewId::CrewOne,
        .actorSkiff = targetSkiff,
        .actorSkiffGeneration = 1u,
        .cargoId = cargo,
        .cargoGeneration = 1u,
        .priorCargoRevision = 1u,
        .resultingCargoRevision = 2u,
        .targetSkiff = targetSkiff,
        .targetSkiffGeneration = 1u,
    };
}

MatchWorldIntent detachIntent(
    MatchWorldIntentType type,
    const MatchWorldIntentAck& source,
    uint64_t applicationTick,
    uint64_t sequence) {
    return {
        .type = type,
        .source = MatchWorldIntentSource::PlayerCommand,
        .matchId = 77u,
        .worldId = 88u,
        .worldEpoch = 3u,
        .authorityEpoch = 5u,
        .applicationTick = applicationTick,
        .physicsEvidenceTick = 10u,
        .sourceSequence = sequence,
        .playerId = 101u,
        .connectionId = 501u,
        .connectionGeneration = 19u,
        .crew = CrewId::CrewOne,
        .actorSkiff = source.targetSkiff,
        .actorSkiffGeneration = source.targetSkiffGeneration,
        .cargoId = source.cargoId,
        .cargoGeneration = source.cargoGeneration,
        .priorCargoRevision = source.resultingCargoRevision,
        .resultingCargoRevision =
            source.resultingCargoRevision + 1u,
        .sourceSkiff = source.targetSkiff,
        .sourceSkiffGeneration = source.targetSkiffGeneration,
        .sourceAttachmentId = source.newAttachmentId,
        .sourceAttachmentGeneration =
            source.newAttachmentGeneration,
    };
}

MatchActionQuery actionQuery(
    MatchCommandType type = MatchCommandType::TowCargo,
    uint64_t applicationTick = 11u,
    uint64_t evidenceTick = 10u) {
    MatchActionQuery query;
    query.command.schemaVersion = kWreckwaterMatchSchemaVersion;
    query.command.type = type;
    query.command.matchId = 77u;
    query.command.worldId = 88u;
    query.command.worldEpoch = 3u;
    query.command.authorityEpoch = 5u;
    query.command.tick = applicationTick;
    query.command.physicsEvidenceTick = evidenceTick;
    query.command.playerId = 101u;
    query.command.cargoId = 1u;
    query.command.cargoGeneration = 1u;
    query.command.observedCargoRevision = 1u;
    query.command.skiffId = 1u;
    query.command.skiffGeneration = 1u;
    query.applicationTick = applicationTick;
    query.physicsEvidenceTick = evidenceTick;
    query.actorCrew = CrewId::CrewOne;
    query.actorSkiff = {
        .skiffId = 1u,
        .owner = CrewId::CrewOne,
        .generation = 1u,
        .disposition = SkiffDisposition::Active,
    };
    query.cargo = {
        .cargoId = 1u,
        .generation = 1u,
        .revision = 1u,
        .disposition = CargoDisposition::Free,
    };
    return query;
}

TEST(WreckwaterLiveWorld, UsesExactCertifiedEvidenceAndEvictsAt32) {
    BasicWorld fixture;
    fixture.initialize();

    EXPECT_EQ(
        fixture.world.evaluate(actionQuery()),
        WorldValidationResult::Allowed);
    EXPECT_EQ(
        fixture.world.evaluate(actionQuery(
            MatchCommandType::TowCargo, 11u, 9u)),
        WorldValidationResult::PhysicsEvidenceUnavailable);

    auto malformed = fixture.evidence;
    malformed[0].position.local.x =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(fixture.world.certifyEvidenceFrame({
        .worldId = 88u,
        .worldEpoch = 3u,
        .physicsTick = 11u,
        .bodies = malformed,
    }));
    EXPECT_FALSE(fixture.world.certifyEvidenceFrame({
        .worldId = 88u,
        .worldEpoch = 4u,
        .physicsTick = 11u,
        .bodies = fixture.evidence,
    }));
    auto aliasMismatch = fixture.evidence;
    aliasMismatch[0].position = position(9.0f);
    EXPECT_FALSE(fixture.world.certifyEvidenceFrame({
        .worldId = 88u,
        .worldEpoch = 3u,
        .physicsTick = 11u,
        .bodies = aliasMismatch,
    }));

    for (uint64_t tick = 11u; tick <= 42u; ++tick) {
        fixture.certifyAt(tick);
    }
    EXPECT_EQ(
        fixture.world.evidenceFrameCount(),
        kWreckwaterCertifiedEvidenceFrameCount);
    EXPECT_EQ(
        fixture.world.evaluate(actionQuery(
            MatchCommandType::TowCargo, 43u, 10u)),
        WorldValidationResult::PhysicsEvidenceUnavailable);
    EXPECT_EQ(
        fixture.world.evaluate(actionQuery(
            MatchCommandType::TowCargo, 43u, 11u)),
        WorldValidationResult::Allowed);
}

TEST(WreckwaterLiveWorld, RealAdapterRejectsUninitializedPhysicsWorld) {
    physics::PhysicsWorld physicsWorld;
    WreckwaterLiveWorld world;
    EXPECT_FALSE(world.initialize(liveConfig(), physicsWorld));
}

TEST(WreckwaterLiveWorld, EvaluatesRangeExtractionAndStaleGenerations) {
    BasicWorld fixture;
    fixture.initialize();

    fixture.evidence[0].position = position(0.0f);
    fixture.evidence[1].position = position(0.0f);
    fixture.evidence[3].position = position(11.0f);
    fixture.certifyAt(11u);
    EXPECT_EQ(
        fixture.world.evaluate(actionQuery(
            MatchCommandType::TowCargo, 12u, 11u)),
        WorldValidationResult::OutOfRange);

    fixture.evidence[0].position = position(6.0f);
    fixture.evidence[1].position = position(6.0f);
    fixture.evidence[3].position = position(6.0f);
    fixture.certifyAt(12u);
    fixture.physics.targetTick = 13u;
    const MatchWorldIntent attach =
        attachIntent(13u, 12u, 1u);
    MatchWorldIntentAck attached{};
    ASSERT_TRUE(fixture.world.applyAtomically(
        std::span(&attach, 1u), std::span(&attached, 1u)));
    MatchActionQuery bank = actionQuery(
        MatchCommandType::BankCargo, 14u, 12u);
    bank.command.observedCargoRevision = 2u;
    bank.cargo.revision = 2u;
    bank.cargo.disposition = CargoDisposition::Towed;
    bank.cargo.owner = CrewId::CrewOne;
    bank.cargo.towingSkiff = 1u;
    bank.cargo.towingSkiffGeneration = 1u;
    bank.cargo.towAttachmentId = attached.newAttachmentId;
    bank.cargo.towAttachmentGeneration =
        attached.newAttachmentGeneration;
    EXPECT_EQ(
        fixture.world.evaluate(bank),
        WorldValidationResult::OutsideExtraction);

    MatchActionQuery staleSkiff = actionQuery(
        MatchCommandType::TowCargo, 14u, 12u);
    staleSkiff.command.skiffGeneration = 2u;
    staleSkiff.actorSkiff.generation = 2u;
    EXPECT_EQ(
        fixture.world.evaluate(staleSkiff),
        WorldValidationResult::SkiffUnavailable);

    MatchActionQuery staleCargo = actionQuery(
        MatchCommandType::TowCargo, 14u, 12u);
    staleCargo.command.cargoGeneration = 2u;
    staleCargo.cargo.generation = 2u;
    EXPECT_EQ(
        fixture.world.evaluate(staleCargo),
        WorldValidationResult::Rejected);
}

TEST(WreckwaterLiveWorld, RejectsDuplicateAndStaleBodyBindings) {
    FakePhysicsTransactions physics;
    WreckwaterLiveWorld world;
    ASSERT_TRUE(world.initialize(liveConfig(), physics));
    EXPECT_EQ(
        world.bindEntity(skiffKey(1u), BodyHandle{21u, 3u}),
        WreckwaterBindingResult::Bound);
    EXPECT_EQ(
        world.bindEntity(skiffKey(1u), BodyHandle{21u, 3u}),
        WreckwaterBindingResult::AlreadyBound);
    EXPECT_EQ(
        world.bindEntity(playerKey(), BodyHandle{21u, 3u}),
        WreckwaterBindingResult::Bound);
    ASSERT_TRUE(world.logicalEntity(BodyHandle{21u, 3u}));
    EXPECT_EQ(
        *world.logicalEntity(BodyHandle{21u, 3u}),
        skiffKey(1u));
    EXPECT_EQ(
        world.bindEntity(cargoKey(1u), BodyHandle{21u, 3u}),
        WreckwaterBindingResult::DuplicatePhysicsHandle);
    EXPECT_EQ(
        world.bindEntity(skiffKey(2u), BodyHandle{21u, 3u}),
        WreckwaterBindingResult::DuplicatePhysicsHandle);
    EXPECT_EQ(
        world.bindEntity(skiffKey(2u), BodyHandle{21u, 99u}),
        WreckwaterBindingResult::DuplicatePhysicsHandle);
    EXPECT_EQ(
        world.bindEntity(skiffKey(1u), BodyHandle{99u, 1u}),
        WreckwaterBindingResult::DuplicateLogicalEntity);
    EXPECT_EQ(
        world.bindEntity(skiffKey(1u, 2u), BodyHandle{22u, 4u}),
        WreckwaterBindingResult::Rebound);
    EXPECT_EQ(
        world.bindEntity(playerKey(101u, 8u), BodyHandle{22u, 4u}),
        WreckwaterBindingResult::Rebound);
    EXPECT_EQ(
        world.bindEntity(skiffKey(1u), BodyHandle{23u, 5u}),
        WreckwaterBindingResult::StaleGeneration);
    EXPECT_FALSE(world.logicalEntity(BodyHandle{21u, 3u}));
    ASSERT_TRUE(world.logicalEntity(BodyHandle{22u, 4u}));
    EXPECT_EQ(
        *world.logicalEntity(BodyHandle{22u, 4u}),
        skiffKey(1u, 2u));
}

TEST(WreckwaterLiveWorld, AttachIsAtomicAndRetryIsByteIdentical) {
    BasicWorld fixture;
    fixture.initialize();
    const MatchWorldIntent intent = attachIntent();
    MatchWorldIntentAck first{};

    ASSERT_TRUE(fixture.world.applyAtomically(
        std::span(&intent, 1u), std::span(&first, 1u)));
    EXPECT_EQ(fixture.physics.prepareCalls, 1u);
    EXPECT_EQ(fixture.physics.commitCalls, 1u);
    EXPECT_EQ(fixture.physics.lastCreateCount, 1u);
    EXPECT_EQ(
        fixture.physics.createdDescs[0].bodyA,
        (BodyHandle{21u, 3u}));
    EXPECT_EQ(
        fixture.physics.createdDescs[0].bodyB,
        (BodyHandle{31u, 4u}));
    EXPECT_NE(first.newAttachmentId, 0u);
    EXPECT_EQ(first.newAttachmentGeneration, 1u);
    ASSERT_TRUE(fixture.world.towBinding(
        first.newAttachmentId, first.newAttachmentGeneration));
    ASSERT_TRUE(fixture.world.logicalTow(
        AttachmentHandle{100u, 1u}));

    MatchWorldIntentAck retry{};
    ASSERT_TRUE(fixture.world.applyAtomically(
        std::span(&intent, 1u), std::span(&retry, 1u)));
    EXPECT_EQ(fixture.physics.prepareCalls, 1u);
    EXPECT_EQ(
        std::memcmp(&first, &retry, sizeof(first)), 0);

    MatchWorldIntent conflicting = intent;
    conflicting.connectionId = 999u;
    MatchWorldIntentAck untouched{};
    untouched.matchId = 123'456u;
    const MatchWorldIntentAck sentinel = untouched;
    EXPECT_FALSE(fixture.world.applyAtomically(
        std::span(&conflicting, 1u),
        std::span(&untouched, 1u)));
    EXPECT_EQ(untouched, sentinel);
    EXPECT_EQ(fixture.world.towCount(), 1u);
}

TEST(WreckwaterLiveWorld, CutAndBankDestroyTheExactLogicalTow) {
    {
        BasicWorld fixture;
        fixture.initialize();
        const MatchWorldIntent attach = attachIntent();
        MatchWorldIntentAck attached{};
        ASSERT_TRUE(fixture.world.applyAtomically(
            std::span(&attach, 1u), std::span(&attached, 1u)));

        MatchWorldIntent unmountedCut = detachIntent(
            MatchWorldIntentType::CutTow, attached, 12u, 2u);
        unmountedCut.actorSkiff = 2u;
        unmountedCut.actorSkiffGeneration = 1u;
        MatchWorldIntentAck unmountedAck{};
        unmountedAck.matchId = 999u;
        const MatchWorldIntentAck unmountedSentinel = unmountedAck;
        EXPECT_FALSE(fixture.world.applyAtomically(
            std::span(&unmountedCut, 1u),
            std::span(&unmountedAck, 1u)));
        EXPECT_EQ(unmountedAck, unmountedSentinel);
        EXPECT_EQ(fixture.world.towCount(), 1u);
        EXPECT_EQ(fixture.physics.prepareCalls, 1u);

        ASSERT_EQ(
            fixture.world.bindEntity(
                playerKey(101u, 8u), BodyHandle{22u, 3u}),
            WreckwaterBindingResult::Rebound);
        fixture.evidence[0].entity = playerKey(101u, 8u);
        fixture.evidence[0].position =
            fixture.evidence[2].position;
        fixture.certifyAt(11u);
        fixture.physics.targetTick = 12u;
        MatchWorldIntent cut = unmountedCut;
        // CutTow keeps the cargo's towing skiff as sourceSkiff while the
        // authenticated actor may be on another skiff.
        cut.actorSkiff = 2u;
        cut.actorSkiffGeneration = 1u;
        cut.physicsEvidenceTick = 11u;
        MatchWorldIntentAck cutAck{};
        ASSERT_TRUE(fixture.world.applyAtomically(
            std::span(&cut, 1u), std::span(&cutAck, 1u)));
        EXPECT_EQ(fixture.physics.lastDestroyCount, 1u);
        EXPECT_EQ(
            fixture.physics.destroyed[0],
            (AttachmentHandle{100u, 1u}));
        EXPECT_EQ(cutAck.newAttachmentId, 0u);
        EXPECT_EQ(fixture.world.towCount(), 0u);
        EXPECT_FALSE(fixture.world.towBinding(
            attached.newAttachmentId,
            attached.newAttachmentGeneration));
    }
    {
        BasicWorld fixture;
        fixture.initialize();
        const MatchWorldIntent attach = attachIntent();
        MatchWorldIntentAck attached{};
        ASSERT_TRUE(fixture.world.applyAtomically(
            std::span(&attach, 1u), std::span(&attached, 1u)));

        fixture.physics.targetTick = 12u;
        const MatchWorldIntent bank = detachIntent(
            MatchWorldIntentType::BankCargo, attached, 12u, 2u);
        MatchWorldIntentAck bankAck{};
        ASSERT_TRUE(fixture.world.applyAtomically(
            std::span(&bank, 1u), std::span(&bankAck, 1u)));
        EXPECT_EQ(fixture.physics.lastDestroyCount, 1u);
        EXPECT_EQ(fixture.world.towCount(), 0u);
    }
}

TEST(WreckwaterLiveWorld, TransferSucceedsAtFullTowCapacity) {
    FakePhysicsTransactions physics;
    WreckwaterLiveWorld world;
    ASSERT_TRUE(world.initialize(liveConfig(), physics));
    ASSERT_EQ(
        world.bindEntity(playerKey(), BodyHandle{21u, 1u}),
        WreckwaterBindingResult::Bound);
    ASSERT_EQ(
        world.bindEntity(skiffKey(1u), BodyHandle{21u, 1u}),
        WreckwaterBindingResult::Bound);
    ASSERT_EQ(
        world.bindEntity(skiffKey(2u), BodyHandle{22u, 1u}),
        WreckwaterBindingResult::Bound);

    std::array<
        WreckwaterEvidenceBody,
        3u + kWreckwaterLiveMaximumTows> evidence{};
    evidence[0] = {playerKey(), position(0.0f), true};
    evidence[1] = {skiffKey(1u), position(0.0f), true};
    evidence[2] = {skiffKey(2u), position(0.0f), true};
    for (size_t index = 0u;
         index < kWreckwaterLiveMaximumTows; ++index) {
        const CargoId cargo = static_cast<CargoId>(index + 1u);
        ASSERT_EQ(
            world.bindEntity(
                cargoKey(cargo),
                BodyHandle{
                    static_cast<uint32_t>(31u + index), 1u}),
            WreckwaterBindingResult::Bound);
        evidence[3u + index] = {
            cargoKey(cargo), position(0.0f), true};
    }
    ASSERT_TRUE(world.certifyEvidenceFrame({
        .worldId = 88u,
        .worldEpoch = 3u,
        .physicsTick = 10u,
        .bodies = evidence,
    }));

    std::array<
        MatchWorldIntent,
        kWreckwaterLiveMaximumTows> attaches{};
    std::array<
        MatchWorldIntentAck,
        kWreckwaterLiveMaximumTows> attached{};
    for (size_t index = 0u; index < attaches.size(); ++index) {
        attaches[index] = attachIntent(
            11u, 10u, index + 1u,
            static_cast<CargoId>(index + 1u), 1u);
    }
    ASSERT_TRUE(world.applyAtomically(attaches, attached));
    ASSERT_EQ(world.towCount(), kWreckwaterLiveMaximumTows);

    ASSERT_EQ(
        world.bindEntity(
            playerKey(101u, 8u), BodyHandle{22u, 1u}),
        WreckwaterBindingResult::Rebound);
    evidence[0].entity = playerKey(101u, 8u);
    evidence[0].position = evidence[2].position;
    ASSERT_TRUE(world.certifyEvidenceFrame({
        .worldId = 88u,
        .worldEpoch = 3u,
        .physicsTick = 11u,
        .bodies = evidence,
    }));
    physics.targetTick = 12u;
    const std::array replacementHandle{
        AttachmentHandle{100u, 2u}};
    physics.useCreated(replacementHandle);
    MatchWorldIntent transfer = detachIntent(
        MatchWorldIntentType::TransferTow, attached[0], 12u, 20u);
    transfer.targetSkiff = 2u;
    transfer.targetSkiffGeneration = 1u;
    transfer.actorSkiff = 2u;
    transfer.actorSkiffGeneration = 1u;
    transfer.physicsEvidenceTick = 11u;
    MatchWorldIntentAck transferAck{};
    ASSERT_TRUE(world.applyAtomically(
        std::span(&transfer, 1u),
        std::span(&transferAck, 1u)));
    EXPECT_EQ(world.towCount(), kWreckwaterLiveMaximumTows);
    EXPECT_EQ(physics.lastDestroyCount, 1u);
    EXPECT_EQ(physics.lastCreateCount, 1u);
    EXPECT_EQ(
        physics.destroyed[0], (AttachmentHandle{100u, 1u}));
    EXPECT_FALSE(world.logicalTow(AttachmentHandle{100u, 1u}));
    ASSERT_TRUE(world.logicalTow(AttachmentHandle{100u, 2u}));
    EXPECT_EQ(
        world.logicalTow(AttachmentHandle{100u, 2u})->skiffId,
        2u);
    EXPECT_NE(
        transferAck.newAttachmentId,
        attached[0].newAttachmentId);
}

void expectAttachFailure(
    FakePhysicsTransactions::PrepareMode prepareMode,
    bool commitReject) {
    BasicWorld fixture;
    fixture.physics.prepareMode = prepareMode;
    fixture.physics.commitReject = commitReject;
    fixture.initialize();
    const MatchWorldIntent intent = attachIntent();
    MatchWorldIntentAck output{};
    output.matchId = 999u;
    output.newAttachmentId = 888u;
    const MatchWorldIntentAck sentinel = output;

    EXPECT_FALSE(fixture.world.applyAtomically(
        std::span(&intent, 1u), std::span(&output, 1u)));
    EXPECT_EQ(output, sentinel);
    EXPECT_EQ(fixture.world.towCount(), 0u);
    EXPECT_EQ(fixture.world.committedReceiptCount(), 0u);
    EXPECT_FALSE(fixture.world.logicalTow(
        AttachmentHandle{100u, 1u}));

    fixture.physics.prepareMode =
        FakePhysicsTransactions::PrepareMode::Normal;
    fixture.physics.commitReject = false;
    MatchWorldIntentAck retried{};
    ASSERT_TRUE(fixture.world.applyAtomically(
        std::span(&intent, 1u), std::span(&retried, 1u)));
    EXPECT_EQ(retried.newAttachmentId, 1u);
    EXPECT_EQ(retried.newAttachmentGeneration, 1u);
}

TEST(WreckwaterLiveWorld, PrepareRejectLeavesStateAndAcksUntouched) {
    expectAttachFailure(
        FakePhysicsTransactions::PrepareMode::Reject, false);
}

TEST(WreckwaterLiveWorld, MalformedPreparedOutputIsDiscardedAtomically) {
    expectAttachFailure(
        FakePhysicsTransactions::PrepareMode::MissingCreatedHandle,
        false);
    expectAttachFailure(
        FakePhysicsTransactions::PrepareMode::InvalidCreatedHandle,
        false);

    FakePhysicsTransactions physics;
    WreckwaterLiveWorld world;
    ASSERT_TRUE(world.initialize(liveConfig(), physics));
    ASSERT_EQ(
        world.bindEntity(playerKey(), BodyHandle{21u, 1u}),
        WreckwaterBindingResult::Bound);
    ASSERT_EQ(
        world.bindEntity(skiffKey(1u), BodyHandle{21u, 1u}),
        WreckwaterBindingResult::Bound);
    ASSERT_EQ(
        world.bindEntity(cargoKey(1u), BodyHandle{31u, 1u}),
        WreckwaterBindingResult::Bound);
    ASSERT_EQ(
        world.bindEntity(cargoKey(2u), BodyHandle{32u, 1u}),
        WreckwaterBindingResult::Bound);
    const std::array evidence{
        WreckwaterEvidenceBody{playerKey(), position(0.0f), true},
        WreckwaterEvidenceBody{skiffKey(1u), position(0.0f), true},
        WreckwaterEvidenceBody{cargoKey(1u), position(0.0f), true},
        WreckwaterEvidenceBody{cargoKey(2u), position(0.0f), true},
    };
    ASSERT_TRUE(world.certifyEvidenceFrame({
        .worldId = 88u,
        .worldEpoch = 3u,
        .physicsTick = 10u,
        .bodies = evidence,
    }));
    physics.prepareMode =
        FakePhysicsTransactions::PrepareMode::DuplicateCreatedHandle;
    const std::array intents{
        attachIntent(11u, 10u, 1u, 1u),
        attachIntent(11u, 10u, 2u, 2u),
    };
    std::array<MatchWorldIntentAck, 2u> acks{};
    acks[0].matchId = 123u;
    const auto sentinels = acks;
    EXPECT_FALSE(world.applyAtomically(intents, acks));
    EXPECT_EQ(acks, sentinels);
    EXPECT_EQ(world.towCount(), 0u);
    EXPECT_EQ(physics.discardCalls, 1u);
}

TEST(WreckwaterLiveWorld, CommitRejectLeavesStateAndAcksUntouched) {
    expectAttachFailure(
        FakePhysicsTransactions::PrepareMode::Normal, true);
}

TEST(WreckwaterLiveWorld, AuthoritativeCleanupUsesExactEvidenceAndTow) {
    BasicWorld fixture;
    fixture.initialize();
    const MatchWorldIntent attach = attachIntent();
    MatchWorldIntentAck attached{};
    ASSERT_TRUE(fixture.world.applyAtomically(
        std::span(&attach, 1u), std::span(&attached, 1u)));

    fixture.evidence[0].available = false;
    fixture.evidence[1].available = false;
    fixture.evidence[3].available = false;
    fixture.certifyAt(11u);
    fixture.physics.targetTick = 12u;
    MatchWorldIntent cleanup = detachIntent(
        MatchWorldIntentType::CutTow, attached, 12u, 30u);
    cleanup.physicsEvidenceTick = 11u;
    cleanup.source =
        MatchWorldIntentSource::AuthoritativeWorldEvent;
    cleanup.sourceStreamId = 9u;
    cleanup.playerId = 0u;
    cleanup.connectionId = 0u;
    cleanup.connectionGeneration = 0u;
    cleanup.actorSkiff = 0u;
    cleanup.actorSkiffGeneration = 0u;
    MatchWorldIntentAck ack{};
    ASSERT_TRUE(fixture.world.applyAtomically(
        std::span(&cleanup, 1u), std::span(&ack, 1u)));
    EXPECT_EQ(fixture.world.towCount(), 0u);

    cleanup.applicationTick = 13u;
    cleanup.sourceSequence = 31u;
    cleanup.sourceAttachmentId += 1u;
    fixture.physics.targetTick = 13u;
    MatchWorldIntentAck untouched{};
    untouched.matchId = 444u;
    const MatchWorldIntentAck sentinel = untouched;
    EXPECT_FALSE(fixture.world.applyAtomically(
        std::span(&cleanup, 1u), std::span(&untouched, 1u)));
    EXPECT_EQ(untouched, sentinel);
}

} // namespace
} // namespace voxy::game
