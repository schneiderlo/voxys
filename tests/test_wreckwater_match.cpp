#include <gtest/gtest.h>

#include "game/wreckwater_match.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace voxy::game {
namespace {

static_assert(!std::is_copy_constructible_v<WreckwaterMatch>);
static_assert(!std::is_copy_assignable_v<WreckwaterMatch>);
static_assert(!std::is_move_constructible_v<WreckwaterMatch>);
static_assert(!std::is_move_assignable_v<WreckwaterMatch>);

class TransactionalWorld final : public IWreckwaterWorldAuthority {
public:
    [[nodiscard]] WorldValidationResult evaluate(
        const MatchActionQuery& query) const noexcept override {
        ++queryCount;
        lastQuery = query;
        const size_t action = static_cast<size_t>(query.command.type);
        if (action >= results.size()) {
            return WorldValidationResult::Rejected;
        }
        return results[action];
    }

    [[nodiscard]] bool applyAtomically(
        std::span<const MatchWorldIntent> intents,
        std::span<MatchWorldIntentAck> acknowledgements)
        noexcept override {
        ++batchCalls;
        lastIntentCount = std::min(
            intents.size(), lastIntents.size());
        std::copy_n(
            intents.begin(), lastIntentCount, lastIntents.begin());
        if (failBatch || acknowledgements.size() != intents.size()) {
            return false;
        }

        for (size_t index = 0u; index < intents.size(); ++index) {
            const MatchWorldIntent& intent = intents[index];
            MatchWorldIntentAck ack;
            ack.schemaVersion = intent.schemaVersion;
            ack.type = intent.type;
            ack.source = intent.source;
            ack.matchId = intent.matchId;
            ack.worldId = intent.worldId;
            ack.worldEpoch = intent.worldEpoch;
            ack.authorityEpoch = intent.authorityEpoch;
            ack.applicationTick = intent.applicationTick;
            ack.physicsEvidenceTick = intent.physicsEvidenceTick;
            ack.sourceStreamId = intent.sourceStreamId;
            ack.sourceSequence = intent.sourceSequence;
            ack.playerId = intent.playerId;
            ack.connectionId = intent.connectionId;
            ack.connectionGeneration = intent.connectionGeneration;
            ack.crew = intent.crew;
            ack.actorSkiff = intent.actorSkiff;
            ack.actorSkiffGeneration =
                intent.actorSkiffGeneration;
            ack.cargoId = intent.cargoId;
            ack.cargoGeneration = intent.cargoGeneration;
            ack.priorCargoRevision = intent.priorCargoRevision;
            ack.resultingCargoRevision =
                intent.resultingCargoRevision;
            ack.sourceSkiff = intent.sourceSkiff;
            ack.sourceSkiffGeneration =
                intent.sourceSkiffGeneration;
            ack.targetSkiff = intent.targetSkiff;
            ack.targetSkiffGeneration =
                intent.targetSkiffGeneration;
            ack.sourceAttachmentId = intent.sourceAttachmentId;
            ack.sourceAttachmentGeneration =
                intent.sourceAttachmentGeneration;
            if (intent.type == MatchWorldIntentType::AttachTow
                || intent.type
                    == MatchWorldIntentType::TransferTow) {
                ack.newAttachmentId =
                    10'000u + intent.applicationTick * 128u
                    + (intent.sourceSequence & 127u);
                ack.newAttachmentGeneration = 1u;
            }
            acknowledgements[index] = ack;
        }
        if (malformedAck && !acknowledgements.empty()) {
            ++acknowledgements[0].physicsEvidenceTick;
        }
        return true;
    }

    std::array<WorldValidationResult, 5> results{
        WorldValidationResult::Rejected,
        WorldValidationResult::Allowed,
        WorldValidationResult::Allowed,
        WorldValidationResult::Allowed,
        WorldValidationResult::Allowed,
    };
    bool failBatch = false;
    bool malformedAck = false;
    mutable MatchActionQuery lastQuery{};
    mutable uint32_t queryCount = 0u;
    uint32_t batchCalls = 0u;
    size_t lastIntentCount = 0u;
    std::array<
        MatchWorldIntent, kWreckwaterMaximumIntentsPerTick>
        lastIntents{};
};

WreckwaterMatch::Config shortConfig() {
    WreckwaterMatch::Config config;
    config.matchId = 77u;
    config.worldId = 88u;
    config.worldEpoch = 3u;
    config.authorityEpoch = 5u;
    config.worldEventStreamId = 9u;
    config.warmupTicks = 1u;
    config.liveTicks = 24u;
    config.overtimeTicks = 4u;
    config.inputFutureWindow = 4u;
    config.worldEventFutureWindow = 4u;
    config.maximumWorldEventLagTicks = 16u;
    config.maximumActionEvidenceLagTicks = 16u;
    config.maximumCommandsPerPlayerTick = 4u;
    config.maximumWorldEventsPerTick = 4u;
    config.maximumSequenceAdvance = 128u;
    config.maximumWorldEventSequenceAdvance = 128u;
    config.maximumConnectionEventsPerPlayer = 8u;
    config.pendingCommandCapacity = 64u;
    config.pendingWorldEventCapacity = 16u;
    config.eventCapacity = 2'048u;
    config.bankScore = 100u;
    config.reactorCargoId = 41u;
    config.reactorCargoGeneration = 7u;
    config.crewOneSkiffId = 51u;
    config.crewTwoSkiffId = 52u;
    return config;
}

void registerAndConnectRoster(WreckwaterMatch& match) {
    constexpr std::array<PlayerRegistration, kWreckwaterPlayerCount>
        registrations{{
            {10u, CrewId::CrewOne, 0u},
            {11u, CrewId::CrewOne, 1u},
            {20u, CrewId::CrewTwo, 0u},
            {21u, CrewId::CrewTwo, 1u},
        }};
    for (const PlayerRegistration& registration : registrations) {
        ASSERT_EQ(
            match.registerPlayer(registration),
            RegistrationResult::Registered);
        ASSERT_EQ(
            match.connectPlayer(
                registration.playerId,
                registration.playerId + 100u),
            ConnectionResult::Connected);
    }
}

void startLiveMatch(
    WreckwaterMatch& match,
    const WreckwaterMatch::Config& config,
    TransactionalWorld& world) {
    ASSERT_TRUE(match.initialize(config));
    registerAndConnectRoster(match);
    ASSERT_TRUE(match.start());
    for (uint32_t tick = 0u; tick < config.warmupTicks; ++tick) {
        ASSERT_TRUE(match.step(world));
    }
    ASSERT_EQ(match.phase(), MatchPhase::Live);
    ASSERT_GT(match.currentTick(), 0u);
}

SkiffId skiffForCrew(
    const WreckwaterMatch& match, CrewId crew) {
    return crew == CrewId::CrewOne
        ? match.config().crewOneSkiffId
        : match.config().crewTwoSkiffId;
}

MatchCommand commandFor(
    const WreckwaterMatch& match, PlayerId playerId,
    MatchCommandType type, uint64_t sequence,
    uint64_t tickOffset = 1u) {
    const PlayerState* player = match.player(playerId);
    const CargoState* cargo =
        match.cargo(match.config().reactorCargoId);
    EXPECT_NE(player, nullptr);
    EXPECT_NE(cargo, nullptr);
    if (player == nullptr || cargo == nullptr) return {};
    const SkiffId skiffId = skiffForCrew(match, player->crew);
    const SkiffState* skiff = match.skiff(skiffId);
    EXPECT_NE(skiff, nullptr);
    if (skiff == nullptr) return {};

    return MatchCommand{
        .type = type,
        .matchId = match.config().matchId,
        .worldId = match.config().worldId,
        .worldEpoch = match.config().worldEpoch,
        .authorityEpoch = match.config().authorityEpoch,
        .tick = match.currentTick() + tickOffset,
        .physicsEvidenceTick = match.currentTick(),
        .sequence = sequence,
        .playerId = playerId,
        .connectionId = player->connectionId,
        .connectionGeneration = player->connectionGeneration,
        .cargoId = cargo->cargoId,
        .cargoGeneration = cargo->generation,
        .observedCargoRevision = cargo->revision,
        .skiffId = skiff->skiffId,
        .skiffGeneration = skiff->generation,
    };
}

AuthoritativeWorldEvent baseWorldEvent(
    const WreckwaterMatch& match,
    AuthoritativeWorldEventType type, uint64_t sequence,
    uint64_t tickOffset = 1u) {
    AuthoritativeWorldEvent event;
    event.type = type;
    event.matchId = match.config().matchId;
    event.worldId = match.config().worldId;
    event.worldEpoch = match.config().worldEpoch;
    event.authorityEpoch = match.config().authorityEpoch;
    event.streamId = match.config().worldEventStreamId;
    event.sourcePhysicsTick = match.currentTick();
    event.applicationTick = match.currentTick() + tickOffset;
    event.sequence = sequence;
    return event;
}

AuthoritativeWorldEvent attachmentBrokenEvent(
    const WreckwaterMatch& match, uint64_t sequence) {
    AuthoritativeWorldEvent event = baseWorldEvent(
        match, AuthoritativeWorldEventType::AttachmentBroken,
        sequence);
    const CargoState* cargo =
        match.cargo(match.config().reactorCargoId);
    EXPECT_NE(cargo, nullptr);
    if (cargo == nullptr) return event;
    event.cargoId = cargo->cargoId;
    event.cargoGeneration = cargo->generation;
    event.cargoRevision = cargo->revision;
    event.skiffId = cargo->towingSkiff;
    event.skiffGeneration = cargo->towingSkiffGeneration;
    event.attachmentId = cargo->towAttachmentId;
    event.attachmentGeneration =
        cargo->towAttachmentGeneration;
    return event;
}

AuthoritativeWorldEvent cargoLostEvent(
    const WreckwaterMatch& match, uint64_t sequence) {
    AuthoritativeWorldEvent event = baseWorldEvent(
        match, AuthoritativeWorldEventType::CargoLost, sequence);
    const CargoState* cargo =
        match.cargo(match.config().reactorCargoId);
    EXPECT_NE(cargo, nullptr);
    if (cargo == nullptr) return event;
    event.cargoId = cargo->cargoId;
    event.cargoGeneration = cargo->generation;
    event.cargoRevision = cargo->revision;
    if (cargo->disposition == CargoDisposition::Towed) {
        event.skiffId = cargo->towingSkiff;
        event.skiffGeneration =
            cargo->towingSkiffGeneration;
        event.attachmentId = cargo->towAttachmentId;
        event.attachmentGeneration =
            cargo->towAttachmentGeneration;
    }
    return event;
}

AuthoritativeWorldEvent skiffEvent(
    const WreckwaterMatch& match,
    AuthoritativeWorldEventType type, SkiffId skiffId,
    uint64_t sequence, bool includeTow = false) {
    AuthoritativeWorldEvent event =
        baseWorldEvent(match, type, sequence);
    const SkiffState* skiff = match.skiff(skiffId);
    EXPECT_NE(skiff, nullptr);
    if (skiff == nullptr) return event;
    event.skiffId = skiff->skiffId;
    event.skiffGeneration = skiff->generation;
    if (type == AuthoritativeWorldEventType::SkiffRespawned) {
        event.nextSkiffGeneration = skiff->generation + 1u;
    }
    if (includeTow) {
        const CargoState* cargo =
            match.cargo(match.config().reactorCargoId);
        EXPECT_NE(cargo, nullptr);
        if (cargo != nullptr) {
            event.cargoId = cargo->cargoId;
            event.cargoGeneration = cargo->generation;
            event.cargoRevision = cargo->revision;
            event.attachmentId = cargo->towAttachmentId;
            event.attachmentGeneration =
                cargo->towAttachmentGeneration;
        }
    }
    return event;
}

void expectCommandAccepted(const CommandSubmitResult& result) {
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.reason, CommandRejectReason::None);
}

void expectWorldEventAccepted(
    const WorldEventSubmitResult& result) {
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.reason, WorldEventRejectReason::None);
}

const MatchEvent* findLastEvent(
    const WreckwaterMatch& match, MatchEventType type) {
    const auto events = match.events();
    const auto found = std::find_if(
        events.rbegin(), events.rend(),
        [type](const MatchEvent& event) {
            return event.type == type;
        });
    return found == events.rend() ? nullptr : &*found;
}

struct CoreSnapshot {
    uint64_t tick = 0u;
    uint32_t stateHash = 0u;
    uint32_t eventHash = 0u;
    size_t eventCount = 0u;
    CargoState cargo{};
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    WreckwaterStorageState storage{};
};

CoreSnapshot snapshot(const WreckwaterMatch& match) {
    const CargoState* cargo =
        match.cargo(match.config().reactorCargoId);
    EXPECT_NE(cargo, nullptr);
    return {
        .tick = match.currentTick(),
        .stateHash = match.stateHash(),
        .eventHash = match.eventStreamHash(),
        .eventCount = match.events().size(),
        .cargo = cargo == nullptr ? CargoState{} : *cargo,
        .crewOneScore = match.score(CrewId::CrewOne),
        .crewTwoScore = match.score(CrewId::CrewTwo),
        .storage = match.storageState(),
    };
}

void expectUnchanged(
    const WreckwaterMatch& match, const CoreSnapshot& before) {
    ASSERT_NE(
        match.cargo(match.config().reactorCargoId), nullptr);
    EXPECT_EQ(match.currentTick(), before.tick);
    EXPECT_EQ(match.stateHash(), before.stateHash);
    EXPECT_EQ(match.eventStreamHash(), before.eventHash);
    EXPECT_EQ(match.events().size(), before.eventCount);
    EXPECT_EQ(
        *match.cargo(match.config().reactorCargoId),
        before.cargo);
    EXPECT_EQ(
        match.score(CrewId::CrewOne), before.crewOneScore);
    EXPECT_EQ(
        match.score(CrewId::CrewTwo), before.crewTwoScore);
    EXPECT_EQ(match.storageState(), before.storage);
    EXPECT_TRUE(match.intentsThisTick().empty());
    EXPECT_FALSE(match.faulted());
}

TEST(WreckwaterMatchConfiguration,
     BoundsConfigurationHashesRulesAndOwnsStableStorage) {
    WreckwaterMatch defaults;
    EXPECT_TRUE(defaults.initialize());

    WreckwaterMatch match;
    WreckwaterMatch::Config config = shortConfig();
    ASSERT_TRUE(match.initialize(config));
    const uint32_t stateHash = match.stateHash();
    const WreckwaterStorageState storage = match.storageState();

    config.maximumActionEvidenceLagTicks = 0u;
    EXPECT_FALSE(match.initialize(config));
    EXPECT_EQ(match.stateHash(), stateHash);
    EXPECT_EQ(match.storageState(), storage);

    config = shortConfig();
    config.eventCapacity = 700u;
    EXPECT_FALSE(match.initialize(config));

    config = shortConfig();
    config.pendingWorldEventCapacity = 15u;
    EXPECT_FALSE(match.initialize(config));

    WreckwaterMatch differentRules;
    config = shortConfig();
    ++config.bankScore;
    ASSERT_TRUE(differentRules.initialize(config));
    EXPECT_NE(match.stateHash(), differentRules.stateHash());
    EXPECT_NE(
        match.eventStreamHash(),
        differentRules.eventStreamHash());
}

TEST(WreckwaterMatchIngress,
     BindsAllAuthorityIdentityGenerationAndPhysicsEvidence) {
    WreckwaterMatch::Config config = shortConfig();
    config.maximumActionEvidenceLagTicks = 1u;
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);

    const MatchCommand valid =
        commandFor(match, 10u, MatchCommandType::TowCargo, 1u);
    MatchCommand invalid = valid;
    ++invalid.matchId;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::ForeignMatch);
    invalid = valid;
    ++invalid.worldId;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::ForeignMatch);
    invalid = valid;
    ++invalid.worldEpoch;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::StaleWorldEpoch);
    invalid = valid;
    ++invalid.authorityEpoch;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::StaleAuthorityEpoch);
    invalid = valid;
    ++invalid.cargoGeneration;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::StaleCargoGeneration);
    invalid = valid;
    ++invalid.skiffGeneration;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::StaleSkiffGeneration);
    invalid = valid;
    invalid.physicsEvidenceTick = match.currentTick() + 1u;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::PhysicsEvidenceInFuture);
    invalid = valid;
    invalid.tick = match.currentTick() + 2u;
    EXPECT_EQ(
        match.submitCommand(invalid).reason,
        CommandRejectReason::PhysicsEvidenceTooOld);

    const uint32_t beforePendingHash = match.stateHash();
    expectCommandAccepted(match.submitCommand(valid));
    EXPECT_NE(match.stateHash(), beforePendingHash);
    world.results[static_cast<size_t>(MatchCommandType::TowCargo)] =
        WorldValidationResult::PhysicsEvidenceUnavailable;
    ASSERT_TRUE(match.step(world));
    EXPECT_TRUE(match.intentsThisTick().empty());
    const MatchEvent* rejection =
        findLastEvent(match, MatchEventType::CommandRejected);
    ASSERT_NE(rejection, nullptr);
    EXPECT_EQ(
        rejection->commandRejection,
        CommandRejectReason::PhysicsEvidenceUnavailable);
    EXPECT_EQ(
        rejection->sourcePhysicsTick,
        valid.physicsEvidenceTick);
    EXPECT_EQ(world.lastQuery.applicationTick, valid.tick);
    EXPECT_EQ(
        world.lastQuery.physicsEvidenceTick,
        valid.physicsEvidenceTick);
}

TEST(WreckwaterMatchConflict,
     CanonicalOrderIgnoresCommandArrivalPermutation) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld firstWorld;
    TransactionalWorld secondWorld;
    WreckwaterMatch first;
    WreckwaterMatch second;
    startLiveMatch(first, config, firstWorld);
    startLiveMatch(second, config, secondWorld);

    const MatchCommand firstCrewOne =
        commandFor(first, 10u, MatchCommandType::TowCargo, 1u);
    const MatchCommand firstCrewTwo =
        commandFor(first, 20u, MatchCommandType::TowCargo, 1u);
    const MatchCommand secondCrewOne =
        commandFor(second, 10u, MatchCommandType::TowCargo, 1u);
    const MatchCommand secondCrewTwo =
        commandFor(second, 20u, MatchCommandType::TowCargo, 1u);

    expectCommandAccepted(first.submitCommand(firstCrewTwo));
    expectCommandAccepted(first.submitCommand(firstCrewOne));
    expectCommandAccepted(second.submitCommand(secondCrewOne));
    expectCommandAccepted(second.submitCommand(secondCrewTwo));
    EXPECT_EQ(first.stateHash(), second.stateHash());

    ASSERT_TRUE(first.step(firstWorld));
    ASSERT_TRUE(second.step(secondWorld));
    ASSERT_NE(first.cargo(config.reactorCargoId), nullptr);
    EXPECT_EQ(
        first.cargo(config.reactorCargoId)->owner,
        CrewId::CrewOne);
    EXPECT_EQ(first.stateHash(), second.stateHash());
    EXPECT_EQ(
        first.eventStreamHash(), second.eventStreamHash());
    EXPECT_TRUE(std::equal(
        first.events().begin(), first.events().end(),
        second.events().begin(), second.events().end()));
    ASSERT_EQ(first.intentsThisTick().size(), 1u);
    const MatchWorldIntent& intent = first.intentsThisTick()[0];
    EXPECT_EQ(intent.source, MatchWorldIntentSource::PlayerCommand);
    EXPECT_EQ(
        intent.physicsEvidenceTick,
        firstCrewOne.physicsEvidenceTick);
    EXPECT_NE(intent.resultingAttachmentId, 0u);
    EXPECT_EQ(
        first.submitCommand(firstCrewOne).reason,
        CommandRejectReason::ReplayedSequence);
}

struct AbortScenario {
    MatchCommandType command;
    MatchWorldIntentType intent;
    PlayerId playerId;
};

void runRetryableAbortScenario(const AbortScenario& scenario) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);

    if (scenario.command != MatchCommandType::TowCargo) {
        expectCommandAccepted(match.submitCommand(commandFor(
            match, 10u, MatchCommandType::TowCargo, 1u)));
        ASSERT_TRUE(match.step(world));
        ASSERT_EQ(
            match.cargo(config.reactorCargoId)->disposition,
            CargoDisposition::Towed);
    }

    const uint64_t sequence =
        scenario.playerId == 10u
            ? (scenario.command == MatchCommandType::TowCargo
                    ? 1u : 2u)
            : 1u;
    expectCommandAccepted(match.submitCommand(commandFor(
        match, scenario.playerId, scenario.command, sequence)));
    const CoreSnapshot before = snapshot(match);

    world.failBatch = true;
    EXPECT_FALSE(match.step(world));
    expectUnchanged(match, before);
    EXPECT_FALSE(match.step(world));
    expectUnchanged(match, before);

    world.failBatch = false;
    ASSERT_TRUE(match.step(world));
    ASSERT_EQ(match.intentsThisTick().size(), 1u);
    const MatchWorldIntent& committedIntent =
        match.intentsThisTick()[0];
    EXPECT_EQ(committedIntent.type, scenario.intent);
    EXPECT_EQ(
        committedIntent.physicsEvidenceTick,
        before.tick);
    EXPECT_NE(committedIntent.actorSkiff, 0u);
    EXPECT_NE(committedIntent.actorSkiffGeneration, 0u);
    EXPECT_EQ(match.telemetry().transactionAborts, 2u);
    EXPECT_FALSE(match.faulted());

    const CargoState* cargo =
        match.cargo(config.reactorCargoId);
    ASSERT_NE(cargo, nullptr);
    MatchEventType actionEventType =
        MatchEventType::CargoTowAttached;
    switch (scenario.command) {
        case MatchCommandType::TowCargo:
            EXPECT_EQ(
                cargo->disposition, CargoDisposition::Towed);
            break;
        case MatchCommandType::CutTow:
            actionEventType = MatchEventType::CargoTowCut;
            EXPECT_EQ(
                cargo->disposition, CargoDisposition::Free);
            break;
        case MatchCommandType::StealCargo:
            actionEventType = MatchEventType::CargoStolen;
            EXPECT_EQ(cargo->owner, CrewId::CrewTwo);
            EXPECT_NE(cargo->towAttachmentId, 0u);
            break;
        case MatchCommandType::BankCargo:
            actionEventType = MatchEventType::CargoBanked;
            EXPECT_EQ(
                cargo->disposition, CargoDisposition::Banked);
            EXPECT_EQ(
                match.score(CrewId::CrewOne), config.bankScore);
            break;
    }
    const MatchEvent* actionEvent =
        findLastEvent(match, actionEventType);
    ASSERT_NE(actionEvent, nullptr);
    EXPECT_EQ(actionEvent->commandType, scenario.command);
    EXPECT_EQ(
        actionEvent->observedCargoRevision,
        committedIntent.priorCargoRevision);
    EXPECT_EQ(
        actionEvent->cargoRevision,
        committedIntent.resultingCargoRevision);
    EXPECT_EQ(
        actionEvent->sourceAttachmentId,
        committedIntent.sourceAttachmentId);
    EXPECT_EQ(
        actionEvent->sourceAttachmentGeneration,
        committedIntent.sourceAttachmentGeneration);
    EXPECT_EQ(
        actionEvent->resultingAttachmentId,
        committedIntent.resultingAttachmentId);
    EXPECT_EQ(
        actionEvent->resultingAttachmentGeneration,
        committedIntent.resultingAttachmentGeneration);
}

TEST(WreckwaterMatchTransaction,
     FalseBatchIsRetryableForAttachCutTransferAndBank) {
    constexpr std::array<AbortScenario, 4> scenarios{{
        {
            MatchCommandType::TowCargo,
            MatchWorldIntentType::AttachTow,
            10u,
        },
        {
            MatchCommandType::CutTow,
            MatchWorldIntentType::CutTow,
            10u,
        },
        {
            MatchCommandType::StealCargo,
            MatchWorldIntentType::TransferTow,
            20u,
        },
        {
            MatchCommandType::BankCargo,
            MatchWorldIntentType::BankCargo,
            10u,
        },
    }};
    for (const AbortScenario& scenario : scenarios) {
        SCOPED_TRACE(static_cast<uint32_t>(scenario.command));
        runRetryableAbortScenario(scenario);
    }
}

TEST(WreckwaterMatchTransaction,
     TrueWithMismatchedAckEntersCanonicalFailStop) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);
    expectCommandAccepted(match.submitCommand(
        commandFor(match, 10u, MatchCommandType::TowCargo, 1u)));
    const CoreSnapshot before = snapshot(match);

    world.malformedAck = true;
    EXPECT_FALSE(match.step(world));
    EXPECT_EQ(match.currentTick(), before.tick);
    EXPECT_EQ(
        *match.cargo(config.reactorCargoId), before.cargo);
    EXPECT_EQ(match.fault(), MatchFaultReason::InvalidWorldAcknowledgement);
    EXPECT_EQ(match.remainingPhaseTicks(), 0u);
    EXPECT_NE(match.stateHash(), before.stateHash);
    EXPECT_EQ(match.events().size(), before.eventCount + 1u);
    EXPECT_NE(match.eventStreamHash(), before.eventHash);
    EXPECT_TRUE(match.intentsThisTick().empty());
    ASSERT_EQ(
        match.events().back().type, MatchEventType::AuthorityFault);
    EXPECT_EQ(
        match.events().back().faultReason,
        MatchFaultReason::InvalidWorldAcknowledgement);
    EXPECT_EQ(
        match.events().back().sourcePhysicsTick,
        before.tick);

    const size_t faultEventCount = match.events().size();
    EXPECT_FALSE(match.step(world));
    EXPECT_EQ(match.events().size(), faultEventCount);
    EXPECT_EQ(
        match.submitCommand(commandFor(
            match, 10u, MatchCommandType::TowCargo, 2u)).reason,
        CommandRejectReason::AuthorityFaulted);
}

TEST(WreckwaterMatchWorldEvents,
     BreakUsesSourceTickAndRunsBeforeSameTickPlayerAction) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);
    expectCommandAccepted(match.submitCommand(
        commandFor(match, 10u, MatchCommandType::TowCargo, 1u)));
    ASSERT_TRUE(match.step(world));

    AuthoritativeWorldEvent stale =
        attachmentBrokenEvent(match, 1u);
    --stale.cargoRevision;
    const AuthoritativeWorldEvent exact =
        attachmentBrokenEvent(match, 2u);
    expectWorldEventAccepted(match.submitWorldEvent(stale));
    expectWorldEventAccepted(match.submitWorldEvent(exact));
    EXPECT_EQ(
        match.submitWorldEvent(exact).reason,
        WorldEventRejectReason::ReplayedSequence);
    const MatchCommand steal =
        commandFor(match, 20u, MatchCommandType::StealCargo, 1u);
    expectCommandAccepted(match.submitCommand(steal));
    const uint32_t priorBatches = world.batchCalls;
    const CoreSnapshot before = snapshot(match);

    world.failBatch = true;
    EXPECT_FALSE(match.step(world));
    expectUnchanged(match, before);
    world.failBatch = false;
    ASSERT_TRUE(match.step(world));
    EXPECT_EQ(world.batchCalls, priorBatches + 2u);
    ASSERT_NE(match.cargo(config.reactorCargoId), nullptr);
    EXPECT_EQ(
        match.cargo(config.reactorCargoId)->disposition,
        CargoDisposition::Free);
    const MatchEvent* broken =
        findLastEvent(match, MatchEventType::AttachmentBroken);
    ASSERT_NE(broken, nullptr);
    EXPECT_EQ(broken->sourcePhysicsTick, exact.sourcePhysicsTick);
    EXPECT_EQ(
        broken->source,
        MatchEventSource::AuthoritativeWorldEvent);
    EXPECT_EQ(broken->sourceStreamId, exact.streamId);
    EXPECT_EQ(broken->tick, exact.applicationTick);
    EXPECT_EQ(broken->commandTick, exact.applicationTick);
    EXPECT_EQ(broken->sourceSequence, exact.sequence);
    EXPECT_EQ(broken->stateHash, match.stateHash());
    ASSERT_EQ(match.intentsThisTick().size(), 1u);
    const MatchWorldIntent& cleanup =
        match.intentsThisTick().front();
    EXPECT_EQ(cleanup.type, MatchWorldIntentType::CutTow);
    EXPECT_EQ(
        cleanup.source,
        MatchWorldIntentSource::AuthoritativeWorldEvent);
    EXPECT_EQ(cleanup.actorSkiff, 0u);
    EXPECT_EQ(cleanup.actorSkiffGeneration, 0u);
    EXPECT_EQ(cleanup.sourceStreamId, exact.streamId);
    EXPECT_EQ(cleanup.sourceSequence, exact.sequence);
    EXPECT_EQ(
        cleanup.physicsEvidenceTick,
        exact.sourcePhysicsTick);
    EXPECT_EQ(
        cleanup.sourceAttachmentId,
        exact.attachmentId);
    EXPECT_EQ(
        cleanup.sourceAttachmentGeneration,
        exact.attachmentGeneration);

    const MatchEvent* commandRejection =
        findLastEvent(match, MatchEventType::CommandRejected);
    ASSERT_NE(commandRejection, nullptr);
    EXPECT_EQ(
        commandRejection->commandRejection,
        CommandRejectReason::ConflictLost);
    EXPECT_EQ(
        commandRejection->source,
        MatchEventSource::PlayerCommand);
    const MatchEvent* worldRejection =
        findLastEvent(match, MatchEventType::WorldEventRejected);
    ASSERT_NE(worldRejection, nullptr);
    EXPECT_EQ(
        worldRejection->worldEventRejection,
        WorldEventRejectReason::StaleCargoRevision);
    EXPECT_EQ(
        match.submitWorldEvent(exact).reason,
        WorldEventRejectReason::ReplayedSequence);
}

TEST(WreckwaterMatchWorldEvents,
     TowedCargoLossCleanupIsAckGatedAndRetiresTowIdentity) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);
    expectCommandAccepted(match.submitCommand(
        commandFor(
            match, 10u, MatchCommandType::TowCargo, 1u)));
    ASSERT_TRUE(match.step(world));
    const CargoState attached =
        *match.cargo(config.reactorCargoId);
    ASSERT_EQ(attached.disposition, CargoDisposition::Towed);

    const AuthoritativeWorldEvent loss =
        cargoLostEvent(match, 1u);
    expectWorldEventAccepted(match.submitWorldEvent(loss));
    const CoreSnapshot before = snapshot(match);
    const uint32_t priorBatches = world.batchCalls;
    world.failBatch = true;
    EXPECT_FALSE(match.step(world));
    expectUnchanged(match, before);

    world.failBatch = false;
    ASSERT_TRUE(match.step(world));
    EXPECT_EQ(world.batchCalls, priorBatches + 2u);
    const CargoState* cargo =
        match.cargo(config.reactorCargoId);
    ASSERT_NE(cargo, nullptr);
    EXPECT_EQ(cargo->disposition, CargoDisposition::Lost);
    EXPECT_EQ(cargo->owner, CrewId::None);
    EXPECT_EQ(cargo->towingSkiff, 0u);
    EXPECT_EQ(cargo->towingSkiffGeneration, 0u);
    EXPECT_EQ(cargo->towAttachmentId, 0u);
    EXPECT_EQ(cargo->towAttachmentGeneration, 0u);
    EXPECT_EQ(cargo->revision, attached.revision + 1u);

    ASSERT_EQ(match.intentsThisTick().size(), 1u);
    const MatchWorldIntent& cleanup =
        match.intentsThisTick().front();
    EXPECT_EQ(cleanup.type, MatchWorldIntentType::CutTow);
    EXPECT_EQ(
        cleanup.source,
        MatchWorldIntentSource::AuthoritativeWorldEvent);
    EXPECT_EQ(cleanup.sourceStreamId, loss.streamId);
    EXPECT_EQ(cleanup.sourceSequence, loss.sequence);
    EXPECT_EQ(
        cleanup.physicsEvidenceTick,
        loss.sourcePhysicsTick);
    EXPECT_EQ(cleanup.cargoId, attached.cargoId);
    EXPECT_EQ(
        cleanup.priorCargoRevision,
        attached.revision);
    EXPECT_EQ(
        cleanup.resultingCargoRevision,
        attached.revision + 1u);
    EXPECT_EQ(
        cleanup.sourceAttachmentId,
        attached.towAttachmentId);
    EXPECT_EQ(
        cleanup.sourceAttachmentGeneration,
        attached.towAttachmentGeneration);
}

TEST(WreckwaterMatchWorldEvents,
     TowedCargoSinkIsAckGatedLostAndCannotPointAtSunkSkiff) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);
    expectCommandAccepted(match.submitCommand(
        commandFor(match, 10u, MatchCommandType::TowCargo, 1u)));
    ASSERT_TRUE(match.step(world));
    const CargoState attached =
        *match.cargo(config.reactorCargoId);
    ASSERT_EQ(attached.disposition, CargoDisposition::Towed);

    const AuthoritativeWorldEvent sink = skiffEvent(
        match, AuthoritativeWorldEventType::SkiffSunk,
        config.crewOneSkiffId, 1u, true);
    expectWorldEventAccepted(match.submitWorldEvent(sink));
    const CoreSnapshot before = snapshot(match);
    world.failBatch = true;
    EXPECT_FALSE(match.step(world));
    expectUnchanged(match, before);

    world.failBatch = false;
    ASSERT_TRUE(match.step(world));
    const CargoState* cargo =
        match.cargo(config.reactorCargoId);
    const SkiffState* skiff =
        match.skiff(config.crewOneSkiffId);
    ASSERT_NE(cargo, nullptr);
    ASSERT_NE(skiff, nullptr);
    EXPECT_EQ(cargo->disposition, CargoDisposition::Lost);
    EXPECT_EQ(cargo->owner, CrewId::None);
    EXPECT_EQ(cargo->towingSkiff, 0u);
    EXPECT_EQ(cargo->revision, attached.revision + 1u);
    EXPECT_EQ(skiff->disposition, SkiffDisposition::Sunk);
    ASSERT_EQ(match.intentsThisTick().size(), 1u);
    const MatchWorldIntent& intent = match.intentsThisTick()[0];
    EXPECT_EQ(intent.type, MatchWorldIntentType::CutTow);
    EXPECT_EQ(
        intent.source,
        MatchWorldIntentSource::AuthoritativeWorldEvent);
    EXPECT_EQ(intent.sourceStreamId, sink.streamId);
    EXPECT_EQ(intent.sourceSequence, sink.sequence);
    EXPECT_EQ(intent.physicsEvidenceTick, sink.sourcePhysicsTick);
    EXPECT_EQ(intent.sourceAttachmentId, attached.towAttachmentId);
    const MatchEvent* cargoLost =
        findLastEvent(match, MatchEventType::CargoLost);
    ASSERT_NE(cargoLost, nullptr);
    EXPECT_EQ(cargoLost->sourceStreamId, sink.streamId);
    EXPECT_EQ(
        cargoLost->sourceAttachmentId,
        attached.towAttachmentId);
    EXPECT_NE(findLastEvent(match, MatchEventType::SkiffSunk), nullptr);
}

TEST(WreckwaterMatchWorldEvents,
     SinkRespawnGenerationRejectsQueuedAndNewOldGenerationCommands) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);

    const MatchCommand atSink =
        commandFor(match, 10u, MatchCommandType::TowCargo, 1u);
    expectCommandAccepted(match.submitCommand(atSink));
    expectWorldEventAccepted(match.submitWorldEvent(skiffEvent(
        match, AuthoritativeWorldEventType::SkiffSunk,
        config.crewOneSkiffId, 1u)));
    ASSERT_TRUE(match.step(world));
    EXPECT_EQ(
        match.skiff(config.crewOneSkiffId)->disposition,
        SkiffDisposition::Sunk);
    EXPECT_EQ(
        findLastEvent(match, MatchEventType::CommandRejected)
            ->commandRejection,
        CommandRejectReason::SkiffUnavailable);

    MatchCommand queuedOld =
        commandFor(match, 10u, MatchCommandType::TowCargo, 2u);
    expectCommandAccepted(match.submitCommand(queuedOld));
    expectWorldEventAccepted(match.submitWorldEvent(skiffEvent(
        match, AuthoritativeWorldEventType::SkiffRespawned,
        config.crewOneSkiffId, 2u)));
    ASSERT_TRUE(match.step(world));
    ASSERT_NE(match.skiff(config.crewOneSkiffId), nullptr);
    EXPECT_EQ(match.skiff(config.crewOneSkiffId)->generation, 2u);
    EXPECT_EQ(
        findLastEvent(match, MatchEventType::CommandRejected)
            ->commandRejection,
        CommandRejectReason::StaleSkiffGeneration);

    queuedOld.tick = match.currentTick() + 1u;
    queuedOld.physicsEvidenceTick = match.currentTick();
    queuedOld.sequence = 3u;
    EXPECT_EQ(
        match.submitCommand(queuedOld).reason,
        CommandRejectReason::StaleSkiffGeneration);

    MatchCommand current =
        commandFor(match, 10u, MatchCommandType::TowCargo, 3u);
    expectCommandAccepted(match.submitCommand(current));
    ASSERT_TRUE(match.step(world));
    EXPECT_EQ(
        match.cargo(config.reactorCargoId)->disposition,
        CargoDisposition::Towed);
    EXPECT_EQ(
        match.cargo(config.reactorCargoId)
            ->towingSkiffGeneration,
        2u);
}

TEST(WreckwaterMatchWorldEvents,
     CargoLossAndWorldArrivalPermutationAreDeterministic) {
    const WreckwaterMatch::Config config = shortConfig();
    TransactionalWorld firstWorld;
    TransactionalWorld secondWorld;
    WreckwaterMatch first;
    WreckwaterMatch second;
    startLiveMatch(first, config, firstWorld);
    startLiveMatch(second, config, secondWorld);

    const AuthoritativeWorldEvent firstSinkOne = skiffEvent(
        first, AuthoritativeWorldEventType::SkiffSunk,
        config.crewOneSkiffId, 1u);
    const AuthoritativeWorldEvent firstSinkTwo = skiffEvent(
        first, AuthoritativeWorldEventType::SkiffSunk,
        config.crewTwoSkiffId, 2u);
    const AuthoritativeWorldEvent secondSinkOne = skiffEvent(
        second, AuthoritativeWorldEventType::SkiffSunk,
        config.crewOneSkiffId, 1u);
    const AuthoritativeWorldEvent secondSinkTwo = skiffEvent(
        second, AuthoritativeWorldEventType::SkiffSunk,
        config.crewTwoSkiffId, 2u);
    expectWorldEventAccepted(first.submitWorldEvent(firstSinkTwo));
    expectWorldEventAccepted(first.submitWorldEvent(firstSinkOne));
    expectWorldEventAccepted(second.submitWorldEvent(secondSinkOne));
    expectWorldEventAccepted(second.submitWorldEvent(secondSinkTwo));
    EXPECT_EQ(first.stateHash(), second.stateHash());
    ASSERT_TRUE(first.step(firstWorld));
    ASSERT_TRUE(second.step(secondWorld));
    EXPECT_EQ(first.stateHash(), second.stateHash());
    EXPECT_EQ(
        first.eventStreamHash(), second.eventStreamHash());

    TransactionalWorld lostWorld;
    WreckwaterMatch lost;
    startLiveMatch(lost, config, lostWorld);
    const AuthoritativeWorldEvent loss =
        cargoLostEvent(lost, 1u);
    expectWorldEventAccepted(lost.submitWorldEvent(loss));
    ASSERT_TRUE(lost.step(lostWorld));
    EXPECT_EQ(
        lost.cargo(config.reactorCargoId)->disposition,
        CargoDisposition::Lost);
    const MatchEvent* lostEvent =
        findLastEvent(lost, MatchEventType::CargoLost);
    ASSERT_NE(lostEvent, nullptr);
    EXPECT_EQ(
        lostEvent->sourcePhysicsTick,
        loss.sourcePhysicsTick);
}

TEST(WreckwaterMatchReconnect,
     PurgesOldGenerationWithEvidenceAndDoesNotConsumeNewSlots) {
    WreckwaterMatch::Config config = shortConfig();
    config.maximumCommandsPerPlayerTick = 1u;
    TransactionalWorld world;
    WreckwaterMatch match;
    ASSERT_TRUE(match.initialize(config));
    const WreckwaterStorageState initializedStorage =
        match.storageState();
    registerAndConnectRoster(match);
    ASSERT_TRUE(match.start());
    ASSERT_TRUE(match.step(world));

    const MatchCommand old = commandFor(
        match, 10u, MatchCommandType::TowCargo, 1u, 2u);
    expectCommandAccepted(match.submitCommand(old));
    ASSERT_EQ(match.nextCommandSequence(10u), 2u);
    ASSERT_TRUE(match.disconnectPlayer(10u, 110u));
    ASSERT_EQ(match.nextCommandSequence(10u), 2u);
    const MatchEvent* purge =
        findLastEvent(match, MatchEventType::CommandRejected);
    ASSERT_NE(purge, nullptr);
    EXPECT_EQ(
        purge->commandRejection,
        CommandRejectReason::StaleConnection);
    EXPECT_EQ(purge->commandTick, old.tick);
    EXPECT_EQ(purge->sourceSequence, old.sequence);
    EXPECT_EQ(
        purge->sourcePhysicsTick, old.physicsEvidenceTick);
    EXPECT_EQ(purge->connectionGeneration, 1u);

    ASSERT_EQ(
        match.connectPlayer(10u, 110u),
        ConnectionResult::Reconnected);
    ASSERT_EQ(match.nextCommandSequence(10u), 2u);
    MatchCommand replacement = commandFor(
        match, 10u, MatchCommandType::TowCargo,
        *match.nextCommandSequence(10u), 2u);
    EXPECT_EQ(replacement.sequence, old.sequence + 1u);
    expectCommandAccepted(match.submitCommand(replacement));
    MatchCommand replayOnNewConnection = replacement;
    replayOnNewConnection.sequence = old.sequence;
    EXPECT_EQ(
        match.submitCommand(replayOnNewConnection).reason,
        CommandRejectReason::ReplayedSequence);
    EXPECT_EQ(
        match.submitCommand(old).reason,
        CommandRejectReason::StaleConnection);
    ASSERT_TRUE(match.step(world));
    ASSERT_TRUE(match.step(world));
    EXPECT_EQ(
        match.cargo(config.reactorCargoId)->owner,
        CrewId::CrewOne);
    EXPECT_EQ(
        match.player(10u)->connectionGeneration, 2u);
    EXPECT_EQ(match.storageState(), initializedStorage);
}

TEST(WreckwaterMatchClock,
     TieRunsBoundedOvertimeAndFinishedStepClearsOutputs) {
    WreckwaterMatch::Config config = shortConfig();
    config.warmupTicks = 1u;
    config.liveTicks = 2u;
    config.overtimeTicks = 2u;
    config.inputFutureWindow = 2u;
    config.worldEventFutureWindow = 2u;
    config.maximumCommandsPerPlayerTick = 1u;
    config.maximumWorldEventsPerTick = 1u;
    config.pendingCommandCapacity = 8u;
    config.pendingWorldEventCapacity = 2u;
    config.eventCapacity = 256u;
    TransactionalWorld world;
    WreckwaterMatch match;
    ASSERT_TRUE(match.initialize(config));
    registerAndConnectRoster(match);
    ASSERT_TRUE(match.start());

    for (uint32_t tick = 0u; tick < 5u; ++tick) {
        ASSERT_TRUE(match.step(world));
    }
    EXPECT_EQ(match.currentTick(), 5u);
    EXPECT_EQ(match.phase(), MatchPhase::Finished);
    EXPECT_EQ(
        match.outcome(),
        (MatchOutcome{MatchOutcomeType::Tie, CrewId::None}));
    EXPECT_FALSE(match.step(world));
    EXPECT_TRUE(match.intentsThisTick().empty());
    EXPECT_EQ(
        match.events().back().type,
        MatchEventType::MatchFinished);
}

TEST(WreckwaterMatchClock,
     OvertimeBankFinishesImmediatelyWithCanonicalWinner) {
    WreckwaterMatch::Config config = shortConfig();
    config.liveTicks = 1u;
    config.overtimeTicks = 4u;
    TransactionalWorld world;
    WreckwaterMatch match;
    startLiveMatch(match, config, world);
    ASSERT_TRUE(match.step(world));
    ASSERT_EQ(match.phase(), MatchPhase::Overtime);

    expectCommandAccepted(match.submitCommand(
        commandFor(match, 10u, MatchCommandType::TowCargo, 1u)));
    ASSERT_TRUE(match.step(world));
    ASSERT_EQ(match.phase(), MatchPhase::Overtime);
    expectCommandAccepted(match.submitCommand(
        commandFor(match, 10u, MatchCommandType::BankCargo, 2u)));
    ASSERT_TRUE(match.step(world));

    EXPECT_EQ(match.phase(), MatchPhase::Finished);
    EXPECT_EQ(
        match.outcome(),
        (MatchOutcome{
            MatchOutcomeType::CrewVictory,
            CrewId::CrewOne,
        }));
    EXPECT_EQ(
        match.score(CrewId::CrewOne), config.bankScore);
    EXPECT_EQ(
        match.events().back().type,
        MatchEventType::MatchFinished);
}

} // namespace
} // namespace voxy::game
