#include "game/wreckwater_replay_playback.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace voxy::game {
namespace {

struct TestPlayer {
    PlayerId playerId = 0u;
    ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    uint32_t characterHandle = 0u;
    CrewId crew = CrewId::None;
    uint32_t seat = 0u;
    bool connected = false;
};

constexpr std::array<TestPlayer, 4> kInitialRoster{{
    {
        .playerId = 101u,
        .connectionId = 1'001u,
        .characterHandle = 10'001u,
        .crew = CrewId::CrewOne,
        .seat = 0u,
    },
    {
        .playerId = 102u,
        .connectionId = 1'002u,
        .characterHandle = 10'002u,
        .crew = CrewId::CrewOne,
        .seat = 1u,
    },
    {
        .playerId = 201u,
        .connectionId = 2'001u,
        .characterHandle = 20'001u,
        .crew = CrewId::CrewTwo,
        .seat = 0u,
    },
    {
        .playerId = 202u,
        .connectionId = 2'002u,
        .characterHandle = 20'002u,
        .crew = CrewId::CrewTwo,
        .seat = 1u,
    },
}};

WreckwaterReplayConfig makeReplayConfig() {
    return {
        .sessionId = 101u,
        .matchId = 202u,
        .worldId = 303u,
        .worldEpoch = 4u,
        .authorityEpoch = 5u,
        .tickRateHz = kWreckwaterReplayTickRateHz,
        .contentHash = 0xaabb'ccdd'eeff'0011ull,
        .matchConfigurationHash = 0x2468'1357u,
        .maximumRecords = 128u,
        .maximumPayloadBytes = 256u * 1024u,
    };
}

network::WreckwaterEntityState makeSkiff(
    network::NetEntityId netId,
    uint32_t netGeneration,
    uint32_t skiffId,
    uint32_t skiffGeneration,
    network::WreckwaterCrew crew,
    network::WreckwaterSkiffDisposition disposition,
    std::array<int32_t, 3> sector,
    float localX,
    float velocityX = 0.0f) {
    network::WreckwaterEntityState entity;
    entity.netEntityId = netId;
    entity.netGeneration = netGeneration;
    entity.kind = network::WreckwaterEntityKind::Skiff;
    entity.crew = crew;
    entity.sector = sector;
    entity.localPosition = {localX, 1.0f, 0.0f};
    entity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    entity.linearVelocity = {velocityX, 0.0f, 0.0f};
    entity.angularVelocity = {0.0f, 0.1f, 0.0f};
    entity.shape = network::WreckwaterShape::Compound;
    entity.dimensions = {3.5f, 1.25f, 6.0f};
    entity.packedMaterialFlags = 0x1000'0001u;
    entity.skiff = {
        .skiffId = skiffId,
        .generation = skiffGeneration,
        .disposition = disposition,
    };
    return entity;
}

network::WreckwaterEntityState makeCargo(
    network::WreckwaterCargoDisposition disposition,
    network::WreckwaterCrew owner,
    uint32_t revision,
    uint32_t towingSkiff = 0u,
    uint32_t towingSkiffGeneration = 0u,
    network::LogicalAttachmentId attachmentId = 0u,
    float localX = -2.0f) {
    network::WreckwaterEntityState entity;
    entity.netEntityId = 30u;
    entity.netGeneration = 1u;
    entity.kind = network::WreckwaterEntityKind::Cargo;
    entity.crew = owner;
    entity.sector = {900'000, -800'000, 700'000};
    entity.localPosition = {localX, 0.5f, 0.0f};
    entity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    entity.linearVelocity = {2.0f, 0.0f, 0.0f};
    entity.angularVelocity = {0.0f, 0.0f, 0.0f};
    entity.shape = network::WreckwaterShape::Cylinder;
    entity.dimensions = {1.0f, 1.5f, 1.0f};
    entity.packedMaterialFlags = 0x2000'0001u;
    entity.cargo = {
        .cargoId = 1u,
        .generation = 1u,
        .revision = revision,
        .disposition = disposition,
        .ownerCrew = owner,
        .towingSkiffId = towingSkiff,
        .towingSkiffGeneration = towingSkiffGeneration,
    };
    if (attachmentId != 0u) {
        entity.attachment = {
            .attachmentId = attachmentId,
            .generation = 1u,
            .state = network::WreckwaterAttachmentState::Attached,
        };
    }
    return entity;
}

std::vector<network::WreckwaterEntityState> makeEntities(
    network::WreckwaterEntityState cargo,
    bool moved = false,
    network::WreckwaterSkiffDisposition firstDisposition =
        network::WreckwaterSkiffDisposition::Active,
    uint32_t firstSkiffGeneration = 1u,
    uint32_t firstNetGeneration = 1u) {
    std::vector<network::WreckwaterEntityState> entities;
    entities.push_back(makeSkiff(
        10u, firstNetGeneration, 1u, firstSkiffGeneration,
        network::WreckwaterCrew::CrewOne, firstDisposition,
        {
            moved ? 900'001 : 900'000,
            -800'000,
            700'000,
        },
        moved ? -127.0f : 127.0f, 12.0f));
    entities.push_back(makeSkiff(
        20u, 1u, 2u, 1u, network::WreckwaterCrew::CrewTwo,
        network::WreckwaterSkiffDisposition::Active,
        {900'000, -800'000, 700'000},
        moved ? 8.0f : 6.0f, -2.0f));
    entities.push_back(std::move(cargo));
    return entities;
}

class RecordingBuilder {
public:
    void initialize() {
        ASSERT_EQ(
            recorder.initialize(makeReplayConfig()),
            WreckwaterReplayError::None);
    }

    [[nodiscard]] MatchEvent event(
        uint64_t tick, MatchEventType type) const {
        MatchEvent value;
        value.tick = tick;
        value.type = type;
        value.phase = phase;
        value.outcome = outcome;
        value.crewOneScore = crewOneScore;
        value.crewTwoScore = crewTwoScore;
        value.stateHash = stateHash;
        return value;
    }

    void append(MatchEvent value) {
        const WreckwaterReplayConfig config = makeReplayConfig();
        value.eventSequence = nextEventSequence;
        value.matchId = config.matchId;
        value.worldId = config.worldId;
        value.worldEpoch = config.worldEpoch;
        value.authorityEpoch = config.authorityEpoch;
        ASSERT_EQ(
            recorder.appendMatchEvent(value),
            WreckwaterReplayError::None);
        ++nextEventSequence;
    }

    void bumpState() {
        ++stateHash;
    }

    void appendRegistrationsOnly() {
        for (const TestPlayer& player : players) {
            bumpState();
            MatchEvent registered =
                event(0u, MatchEventType::PlayerRegistered);
            registered.playerId = player.playerId;
            registered.crew = player.crew;
            registered.seat = player.seat;
            append(registered);
        }
    }

    void appendConnectionsOnly() {
        for (TestPlayer& player : players) {
            player.connectionGeneration = 1u;
            player.connected = true;
            bumpState();
            MatchEvent connected =
                event(0u, MatchEventType::PlayerConnected);
            connected.playerId = player.playerId;
            connected.connectionId = player.connectionId;
            connected.connectionGeneration =
                player.connectionGeneration;
            connected.crew = player.crew;
            connected.seat = player.seat;
            append(connected);
        }
    }

    void appendRosterOnly() {
        appendRegistrationsOnly();
        appendConnectionsOnly();
    }

    void appendStartAndLive() {
        bumpState();
        append(event(0u, MatchEventType::MatchStarted));
        phase = MatchPhase::Live;
        bumpState();
        append(event(10u, MatchEventType::PhaseChanged));
    }

    void appendRosterAndLive() {
        appendRosterOnly();
        appendStartAndLive();
    }

    void disconnect(size_t playerIndex, uint64_t tick) {
        TestPlayer& player = players[playerIndex];
        player.connected = false;
        bumpState();
        MatchEvent disconnected =
            event(tick, MatchEventType::PlayerDisconnected);
        disconnected.playerId = player.playerId;
        disconnected.connectionId = player.connectionId;
        disconnected.connectionGeneration =
            player.connectionGeneration;
        disconnected.crew = player.crew;
        disconnected.seat = player.seat;
        append(disconnected);
    }

    void reconnect(
        size_t playerIndex,
        ConnectionId connectionId,
        uint64_t tick) {
        TestPlayer& player = players[playerIndex];
        player.connectionId = connectionId;
        ++player.connectionGeneration;
        player.connected = true;
        bumpState();
        MatchEvent reconnected =
            event(tick, MatchEventType::PlayerReconnected);
        reconnected.playerId = player.playerId;
        reconnected.connectionId = player.connectionId;
        reconnected.connectionGeneration =
            player.connectionGeneration;
        reconnected.crew = player.crew;
        reconnected.seat = player.seat;
        append(reconnected);
    }

    [[nodiscard]] std::vector<network::WreckwaterCharacterState>
    characters() const {
        std::vector<network::WreckwaterCharacterState> result;
        for (const TestPlayer& player : players) {
            network::WreckwaterCharacterState character;
            character.characterHandle = player.characterHandle;
            character.stateFlags =
                network::kWreckwaterCharacterStateActiveFlag
                | (player.connected
                    ? network::
                        kWreckwaterCharacterStateConnectedFlag
                    : 0u);
            character.playerId = player.playerId;
            character.connectionGeneration =
                player.connectionGeneration;
            character.localFeetPosition = {
                static_cast<float>(player.seat), 0.0f, 0.0f};
            result.push_back(character);
        }
        return result;
    }

    [[nodiscard]] network::WreckwaterCertifiedSnapshot snapshot(
        uint64_t tick,
        uint64_t physicsEvidenceTick,
        std::vector<network::WreckwaterEntityState> entities) const {
        network::WreckwaterCertifiedSnapshot value;
        const WreckwaterReplayConfig config = makeReplayConfig();
        value.sessionId = config.sessionId;
        value.matchId = config.matchId;
        value.worldId = config.worldId;
        value.worldEpoch = config.worldEpoch;
        value.authorityEpoch = config.authorityEpoch;
        value.snapshotSequence = nextSnapshotSequence;
        value.applicationTick = tick;
        value.physicsEvidenceTick = physicsEvidenceTick;
        value.phase =
            static_cast<network::WreckwaterPhase>(phase);
        value.crewOneScore = crewOneScore;
        value.crewTwoScore = crewTwoScore;
        value.outcome =
            static_cast<network::WreckwaterOutcomeType>(outcome);
        if (outcome == MatchOutcomeType::CrewVictory) {
            value.winner =
                crewOneScore > crewTwoScore
                ? network::WreckwaterCrew::CrewOne
                : network::WreckwaterCrew::CrewTwo;
        }
        value.matchStateHash = stateHash;
        value.entities = std::move(entities);
        value.characters = characters();
        EXPECT_TRUE(network::canonicalizeWreckwaterSnapshot(value));
        return value;
    }

    void appendSnapshot(
        network::WreckwaterCertifiedSnapshot value) {
        value.eventStreamHash =
            recorder.expectedEventStreamHash(value.matchStateHash);
        const network::WreckwaterWriteResult encoded =
            network::WreckwaterSnapshotCodec::encode(value);
        ASSERT_TRUE(encoded)
            << network::wreckwaterCodecErrorName(encoded.error);
        value.serializedByteHash = encoded.serializedByteHash;
        ASSERT_EQ(
            recorder.appendCertifiedSnapshot(value, encoded.bytes),
            WreckwaterReplayError::None);
        ++nextSnapshotSequence;
    }

    void certify(
        uint64_t tick,
        uint64_t physicsEvidenceTick,
        std::vector<network::WreckwaterEntityState> entities) {
        appendSnapshot(snapshot(
            tick, physicsEvidenceTick, std::move(entities)));
    }

    void enterFinishedState() {
        phase = MatchPhase::Finished;
        outcome =
            crewOneScore == crewTwoScore
            ? MatchOutcomeType::Tie
            : MatchOutcomeType::CrewVictory;
        bumpState();
    }

    void appendFinish(uint64_t tick) {
        enterFinishedState();
        append(event(tick, MatchEventType::PhaseChanged));
        MatchEvent finished =
            event(tick, MatchEventType::MatchFinished);
        if (outcome == MatchOutcomeType::CrewVictory) {
            finished.crew =
                crewOneScore > crewTwoScore
                ? CrewId::CrewOne : CrewId::CrewTwo;
        }
        append(finished);
    }

    void finalizeWith(
        network::WreckwaterCertifiedSnapshot terminal) {
        const uint32_t terminalState = terminal.matchStateHash;
        const uint32_t terminalEvents =
            recorder.expectedEventStreamHash(terminalState);
        appendSnapshot(std::move(terminal));
        ASSERT_EQ(
            recorder.finalize(terminalState, terminalEvents),
            WreckwaterReplayError::None);
    }

    void finishAndFinalize(
        std::vector<network::WreckwaterEntityState> entities,
        uint64_t tick = 30u) {
        appendFinish(tick);
        finalizeWith(snapshot(
            tick, tick, std::move(entities)));
    }

    WreckwaterReplayRecorder recorder;
    std::array<TestPlayer, 4> players = kInitialRoster;
    uint64_t nextEventSequence = 1u;
    uint64_t nextSnapshotSequence = 1u;
    MatchPhase phase = MatchPhase::Warmup;
    MatchOutcomeType outcome = MatchOutcomeType::Undecided;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    uint32_t stateHash = 0x5000u;
};

void setPlayerCommandSource(
    MatchEvent& event,
    const TestPlayer& player,
    uint64_t sourceSequence,
    MatchCommandType commandType) {
    event.sourcePhysicsTick = 11u;
    event.commandTick = 20u;
    event.sourceSequence = sourceSequence;
    event.source = MatchEventSource::PlayerCommand;
    event.commandType = commandType;
    event.playerId = player.playerId;
    event.seat = player.seat;
    event.connectionId = player.connectionId;
    event.connectionGeneration = player.connectionGeneration;
    event.crew = player.crew;
}

void setWorldSource(
    MatchEvent& event,
    uint64_t sourceSequence,
    AuthoritativeWorldEventType worldEventType) {
    event.sourcePhysicsTick = 11u;
    event.commandTick = 20u;
    event.sourceSequence = sourceSequence;
    event.source = MatchEventSource::AuthoritativeWorldEvent;
    event.sourceStreamId = 7u;
    event.worldEventType = worldEventType;
}

MatchEvent makeTowEvent(
    const RecordingBuilder& builder,
    uint64_t sourceSequence = 1u) {
    MatchEvent event =
        builder.event(20u, MatchEventType::CargoTowAttached);
    setPlayerCommandSource(
        event, builder.players[0], sourceSequence,
        MatchCommandType::TowCargo);
    event.cargoId = 1u;
    event.cargoGeneration = 1u;
    event.observedCargoRevision = 1u;
    event.cargoRevision = 2u;
    event.targetSkiff = 1u;
    event.targetSkiffGeneration = 1u;
    event.attachmentId = 900u;
    event.attachmentGeneration = 1u;
    event.resultingAttachmentId = 900u;
    event.resultingAttachmentGeneration = 1u;
    return event;
}

MatchEvent makeStealEvent(
    const RecordingBuilder& builder,
    uint64_t sourceSequence = 1u) {
    MatchEvent event =
        builder.event(20u, MatchEventType::CargoStolen);
    setPlayerCommandSource(
        event, builder.players[2], sourceSequence,
        MatchCommandType::StealCargo);
    event.cargoId = 1u;
    event.cargoGeneration = 1u;
    event.observedCargoRevision = 1u;
    event.cargoRevision = 2u;
    event.sourceSkiff = 1u;
    event.sourceSkiffGeneration = 1u;
    event.targetSkiff = 2u;
    event.targetSkiffGeneration = 1u;
    event.attachmentId = 901u;
    event.attachmentGeneration = 1u;
    event.sourceAttachmentId = 900u;
    event.sourceAttachmentGeneration = 1u;
    event.resultingAttachmentId = 901u;
    event.resultingAttachmentGeneration = 1u;
    return event;
}

enum class TransitionCase : uint32_t {
    Tow,
    Cut,
    Steal,
    Bank,
    AttachmentBroken,
    CargoLostFree,
    CargoLostTowed,
    SkiffSunk,
    SkiffSunkWithCargo,
    SkiffRespawned,
};

struct TransitionStates {
    std::vector<network::WreckwaterEntityState> before;
    std::vector<network::WreckwaterEntityState> after;
};

TransitionStates appendTransition(
    RecordingBuilder& builder, TransitionCase transition) {
    const auto freeCargo = [] {
        return makeCargo(
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u);
    };
    const auto towedCargo = [] {
        return makeCargo(
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewOne, 1u,
            1u, 1u, 900u);
    };
    TransitionStates states;
    switch (transition) {
        case TransitionCase::Tow:
            states.before = makeEntities(freeCargo());
            states.after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Towed,
                network::WreckwaterCrew::CrewOne, 2u,
                1u, 1u, 900u), true);
            break;
        case TransitionCase::Cut:
        case TransitionCase::Bank:
        case TransitionCase::AttachmentBroken:
        case TransitionCase::CargoLostTowed:
        case TransitionCase::SkiffSunkWithCargo:
            states.before = makeEntities(towedCargo());
            break;
        case TransitionCase::Steal:
            states.before = makeEntities(towedCargo());
            states.after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Towed,
                network::WreckwaterCrew::CrewTwo, 2u,
                2u, 1u, 901u, 2.0f), true);
            break;
        case TransitionCase::CargoLostFree:
        case TransitionCase::SkiffSunk:
            states.before = makeEntities(freeCargo());
            break;
        case TransitionCase::SkiffRespawned:
            states.before = makeEntities(
                freeCargo(), false,
                network::WreckwaterSkiffDisposition::Sunk);
            states.after = makeEntities(
                freeCargo(), true,
                network::WreckwaterSkiffDisposition::Active,
                2u, 2u);
            break;
    }

    builder.certify(10u, 10u, states.before);
    if (transition == TransitionCase::Bank) {
        builder.crewOneScore = 1'000u;
    }
    builder.bumpState();

    switch (transition) {
        case TransitionCase::Tow:
            builder.append(makeTowEvent(builder));
            break;
        case TransitionCase::Cut: {
            MatchEvent event =
                builder.event(20u, MatchEventType::CargoTowCut);
            setPlayerCommandSource(
                event, builder.players[0], 2u,
                MatchCommandType::CutTow);
            event.cargoId = 1u;
            event.cargoGeneration = 1u;
            event.observedCargoRevision = 1u;
            event.cargoRevision = 2u;
            event.sourceSkiff = 1u;
            event.sourceSkiffGeneration = 1u;
            event.attachmentId = 900u;
            event.attachmentGeneration = 1u;
            event.sourceAttachmentId = 900u;
            event.sourceAttachmentGeneration = 1u;
            builder.append(event);
            states.after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Free,
                network::WreckwaterCrew::None, 2u), true);
            break;
        }
        case TransitionCase::Steal:
            builder.append(makeStealEvent(builder, 3u));
            break;
        case TransitionCase::Bank: {
            MatchEvent banked =
                builder.event(20u, MatchEventType::CargoBanked);
            setPlayerCommandSource(
                banked, builder.players[0], 4u,
                MatchCommandType::BankCargo);
            banked.cargoId = 1u;
            banked.cargoGeneration = 1u;
            banked.observedCargoRevision = 1u;
            banked.cargoRevision = 2u;
            banked.sourceSkiff = 1u;
            banked.sourceSkiffGeneration = 1u;
            banked.attachmentId = 900u;
            banked.attachmentGeneration = 1u;
            banked.sourceAttachmentId = 900u;
            banked.sourceAttachmentGeneration = 1u;
            builder.append(banked);
            MatchEvent score =
                builder.event(20u, MatchEventType::ScoreChanged);
            setPlayerCommandSource(
                score, builder.players[0], 4u,
                MatchCommandType::BankCargo);
            score.cargoId = 1u;
            score.cargoGeneration = 1u;
            score.observedCargoRevision = 1u;
            score.cargoRevision = 2u;
            builder.append(score);
            states.after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Banked,
                network::WreckwaterCrew::CrewOne, 2u), true);
            break;
        }
        case TransitionCase::AttachmentBroken: {
            MatchEvent broken =
                builder.event(20u, MatchEventType::AttachmentBroken);
            setWorldSource(
                broken, 5u,
                AuthoritativeWorldEventType::AttachmentBroken);
            broken.cargoId = 1u;
            broken.cargoGeneration = 1u;
            broken.observedCargoRevision = 1u;
            broken.cargoRevision = 2u;
            broken.sourceSkiff = 1u;
            broken.sourceSkiffGeneration = 1u;
            broken.attachmentId = 900u;
            broken.attachmentGeneration = 1u;
            broken.sourceAttachmentId = 900u;
            broken.sourceAttachmentGeneration = 1u;
            builder.append(broken);
            states.after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Free,
                network::WreckwaterCrew::None, 2u), true);
            break;
        }
        case TransitionCase::CargoLostFree:
        case TransitionCase::CargoLostTowed: {
            MatchEvent lost =
                builder.event(20u, MatchEventType::CargoLost);
            setWorldSource(
                lost, 6u,
                AuthoritativeWorldEventType::CargoLost);
            lost.cargoId = 1u;
            lost.cargoGeneration = 1u;
            lost.observedCargoRevision = 1u;
            lost.cargoRevision = 2u;
            if (transition == TransitionCase::CargoLostTowed) {
                lost.sourceSkiff = 1u;
                lost.sourceSkiffGeneration = 1u;
                lost.attachmentId = 900u;
                lost.attachmentGeneration = 1u;
                lost.sourceAttachmentId = 900u;
                lost.sourceAttachmentGeneration = 1u;
            }
            builder.append(lost);
            states.after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Lost,
                network::WreckwaterCrew::None, 2u), true);
            break;
        }
        case TransitionCase::SkiffSunk:
        case TransitionCase::SkiffSunkWithCargo: {
            constexpr uint64_t kSource = 7u;
            if (transition == TransitionCase::SkiffSunkWithCargo) {
                MatchEvent lost =
                    builder.event(20u, MatchEventType::CargoLost);
                setWorldSource(
                    lost, kSource,
                    AuthoritativeWorldEventType::SkiffSunk);
                lost.crew = CrewId::CrewOne;
                lost.cargoId = 1u;
                lost.cargoGeneration = 1u;
                lost.observedCargoRevision = 1u;
                lost.cargoRevision = 2u;
                lost.sourceSkiff = 1u;
                lost.sourceSkiffGeneration = 1u;
                lost.attachmentId = 900u;
                lost.attachmentGeneration = 1u;
                lost.sourceAttachmentId = 900u;
                lost.sourceAttachmentGeneration = 1u;
                builder.append(lost);
            }
            MatchEvent sunk =
                builder.event(20u, MatchEventType::SkiffSunk);
            setWorldSource(
                sunk, kSource,
                AuthoritativeWorldEventType::SkiffSunk);
            sunk.crew = CrewId::CrewOne;
            sunk.sourceSkiff = 1u;
            sunk.sourceSkiffGeneration = 1u;
            builder.append(sunk);
            network::WreckwaterEntityState resultCargo =
                transition == TransitionCase::SkiffSunkWithCargo
                ? makeCargo(
                    network::WreckwaterCargoDisposition::Lost,
                    network::WreckwaterCrew::None, 2u)
                : freeCargo();
            states.after = makeEntities(
                std::move(resultCargo), true,
                network::WreckwaterSkiffDisposition::Sunk);
            break;
        }
        case TransitionCase::SkiffRespawned: {
            MatchEvent respawned =
                builder.event(20u, MatchEventType::SkiffRespawned);
            setWorldSource(
                respawned, 8u,
                AuthoritativeWorldEventType::SkiffRespawned);
            respawned.crew = CrewId::CrewOne;
            respawned.sourceSkiff = 1u;
            respawned.sourceSkiffGeneration = 1u;
            respawned.targetSkiff = 1u;
            respawned.targetSkiffGeneration = 2u;
            builder.append(respawned);
            break;
        }
    }
    builder.certify(20u, 11u, states.after);
    return states;
}

void buildTransitionRecording(
    RecordingBuilder& builder,
    TransitionCase transition) {
    builder.initialize();
    builder.appendRosterAndLive();
    TransitionStates states =
        appendTransition(builder, transition);
    builder.finishAndFinalize(std::move(states.after));
}

WreckwaterReplayWriteResult encodedStealRecording() {
    RecordingBuilder builder;
    buildTransitionRecording(builder, TransitionCase::Steal);
    WreckwaterReplayWriteResult encoded =
        WreckwaterReplayCodec::encode(builder.recorder);
    EXPECT_TRUE(encoded)
        << wreckwaterReplayErrorName(encoded.error);
    return encoded;
}

const WreckwaterReplayPlaybackEntity& playbackEntity(
    const WreckwaterReplayPlaybackFrame& frame,
    network::NetEntityId id) {
    for (uint32_t index = 0u; index < frame.entityCount; ++index) {
        if (frame.entities[index].netEntityId == id) {
            return frame.entities[index];
        }
    }
    ADD_FAILURE() << "missing playback entity " << id;
    return frame.entities[0];
}

TEST(
    WreckwaterReplayPlaybackTest,
    LoadsProductionShapedArchiveAndKeepsCertifiedStateExact) {
    const WreckwaterReplayWriteResult encoded =
        encodedStealRecording();
    WreckwaterReplayPlayback playback;
    const WreckwaterReplayPlaybackLoadResult loaded =
        playback.initialize(encoded.bytes);
    ASSERT_TRUE(loaded)
        << wreckwaterReplayPlaybackErrorName(loaded.error);
    ASSERT_EQ(playback.snapshots().size(), 3u);
    ASSERT_EQ(playback.events().size(), 13u);
    EXPECT_EQ(playback.visibleEventCount(), 10u);
    for (const auto& snapshot : playback.snapshots()) {
        EXPECT_EQ(snapshot.characters.size(), 4u);
    }
    EXPECT_EQ(playback.firstTime(), (WreckwaterReplayTime{10u, 0u}));
    EXPECT_EQ(playback.lastTime(), (WreckwaterReplayTime{30u, 0u}));

    ASSERT_EQ(
        playback.seek({.wholeTick = 11u, .subTick = 0u}),
        WreckwaterReplayPlaybackError::None);
    ASSERT_NE(playback.exactSnapshot(), nullptr);
    EXPECT_EQ(playback.exactSnapshot()->snapshotSequence, 2u);
    EXPECT_EQ(
        playback.frame().authoritative.matchStateHash,
        playback.exactSnapshot()->matchStateHash);
    EXPECT_EQ(
        playback.frame().authoritative.eventStreamHash,
        playback.exactSnapshot()->eventStreamHash);
    const auto& cargo = playbackEntity(playback.frame(), 30u);
    EXPECT_EQ(
        cargo.authoritativeState.cargo.ownerCrew,
        network::WreckwaterCrew::CrewTwo);
    EXPECT_EQ(
        cargo.authoritativeState.attachment.attachmentId, 901u);
}

TEST(
    WreckwaterReplayPlaybackTest,
    InterpolatesMotionButHoldsTopologyUntilItsCertifiedBoundary) {
    WreckwaterReplayPlayback playback;
    ASSERT_TRUE(playback.initialize(encodedStealRecording().bytes));

    ASSERT_EQ(
        playback.seek({
            .wholeTick = 10u,
            .subTick = kWreckwaterReplaySubticksPerTick / 2u,
        }),
        WreckwaterReplayPlaybackError::None);
    const auto& skiff = playbackEntity(playback.frame(), 10u);
    EXPECT_EQ(
        skiff.motion, WreckwaterReplayVisualMotion::Interpolated);
    EXPECT_EQ(
        skiff.visualPose.sector,
        (std::array<int32_t, 3>{
            900'001, -800'000, 700'000}));
    EXPECT_FLOAT_EQ(skiff.visualPose.localPosition.x, -128.0f);

    const auto& cargoBefore =
        playbackEntity(playback.frame(), 30u);
    EXPECT_EQ(
        cargoBefore.motion,
        WreckwaterReplayVisualMotion::HeldTopology);
    EXPECT_EQ(
        cargoBefore.authoritativeState.cargo.ownerCrew,
        network::WreckwaterCrew::CrewOne);

    ASSERT_EQ(
        playback.seek({.wholeTick = 11u, .subTick = 0u}),
        WreckwaterReplayPlaybackError::None);
    EXPECT_EQ(
        playbackEntity(playback.frame(), 30u)
            .authoritativeState.cargo.ownerCrew,
        network::WreckwaterCrew::CrewTwo);
}

TEST(
    WreckwaterReplayPlaybackTest,
    AcceptsApplicationAheadOfEvidenceButRejectsFutureSourceEvidence) {
    WreckwaterReplayPlayback valid;
    ASSERT_TRUE(valid.initialize(encodedStealRecording().bytes));
    ASSERT_EQ(valid.snapshots()[1].applicationTick, 20u);
    ASSERT_EQ(valid.snapshots()[1].physicsEvidenceTick, 11u);
    ASSERT_EQ(valid.events()[10].sourcePhysicsTick, 11u);

    RecordingBuilder builder;
    builder.initialize();
    builder.appendRosterAndLive();
    const auto before = makeEntities(makeCargo(
        network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewOne, 1u,
        1u, 1u, 900u));
    const auto after = makeEntities(makeCargo(
        network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewTwo, 2u,
        2u, 1u, 901u), true);
    builder.certify(10u, 10u, before);
    builder.bumpState();
    MatchEvent future = makeStealEvent(builder, 30u);
    future.sourcePhysicsTick = 12u;
    builder.append(future);
    builder.certify(20u, 11u, after);
    builder.finishAndFinalize(after);

    WreckwaterReplayPlayback playback;
    EXPECT_EQ(
        playback.initialize(builder.recorder).error,
        WreckwaterReplayPlaybackError::GenerationAlias);
}

TEST(
    WreckwaterReplayPlaybackTest,
    AcceptsEveryProductionLogicalTransition) {
    constexpr std::array transitions{
        TransitionCase::Tow,
        TransitionCase::Cut,
        TransitionCase::Steal,
        TransitionCase::Bank,
        TransitionCase::AttachmentBroken,
        TransitionCase::CargoLostFree,
        TransitionCase::CargoLostTowed,
        TransitionCase::SkiffSunk,
        TransitionCase::SkiffSunkWithCargo,
        TransitionCase::SkiffRespawned,
    };
    for (const TransitionCase transition : transitions) {
        SCOPED_TRACE(static_cast<uint32_t>(transition));
        RecordingBuilder builder;
        buildTransitionRecording(builder, transition);
        WreckwaterReplayPlayback playback;
        const auto result = playback.initialize(builder.recorder);
        EXPECT_TRUE(result)
            << wreckwaterReplayPlaybackErrorName(result.error);
    }
}

TEST(
    WreckwaterReplayPlaybackTest,
    RejectsWrongPriorRevisionAndWrongCertifiedAfterState) {
    for (uint32_t caseIndex = 0u; caseIndex < 2u; ++caseIndex) {
        SCOPED_TRACE(caseIndex);
        RecordingBuilder builder;
        builder.initialize();
        builder.appendRosterAndLive();
        const auto before = makeEntities(makeCargo(
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewOne, 1u,
            1u, 1u, 900u));
        const auto after = makeEntities(makeCargo(
            network::WreckwaterCargoDisposition::Towed,
            network::WreckwaterCrew::CrewTwo, 2u,
            2u, 1u, 901u), true);
        builder.certify(10u, 10u, before);
        builder.bumpState();
        MatchEvent stolen = makeStealEvent(builder, 40u);
        if (caseIndex == 0u) {
            stolen.observedCargoRevision = 2u;
        }
        builder.append(stolen);
        builder.certify(
            20u, 11u, caseIndex == 0u ? after : before);
        builder.finishAndFinalize(
            caseIndex == 0u ? after : before);

        WreckwaterReplayPlayback playback;
        EXPECT_EQ(
            playback.initialize(builder.recorder).error,
            WreckwaterReplayPlaybackError::GenerationAlias);
    }
}

TEST(
    WreckwaterReplayPlaybackTest,
    RejectsPhysicalMutationWithoutABeforeCertificate) {
    RecordingBuilder builder;
    builder.initialize();
    builder.appendRosterAndLive();
    builder.bumpState();
    builder.append(makeTowEvent(builder, 50u));
    const auto after = makeEntities(makeCargo(
        network::WreckwaterCargoDisposition::Towed,
        network::WreckwaterCrew::CrewOne, 2u,
        1u, 1u, 900u));
    builder.certify(20u, 11u, after);
    builder.finishAndFinalize(after);

    WreckwaterReplayPlayback playback;
    EXPECT_EQ(
        playback.initialize(builder.recorder).error,
        WreckwaterReplayPlaybackError::GenerationAlias);
}

TEST(
    WreckwaterReplayPlaybackTest,
    RejectsUncertifiedSubjectsOnNonMutatingEvents) {
    RecordingBuilder builder;
    builder.initialize();
    builder.appendRosterAndLive();
    const auto entities = makeEntities(makeCargo(
        network::WreckwaterCargoDisposition::Free,
        network::WreckwaterCrew::None, 1u));
    builder.certify(10u, 10u, entities);

    MatchEvent rejected =
        builder.event(20u, MatchEventType::CommandRejected);
    setPlayerCommandSource(
        rejected, builder.players[0], 55u,
        MatchCommandType::TowCargo);
    rejected.commandRejection = CommandRejectReason::ConflictLost;
    rejected.cargoId = 99u;
    rejected.cargoGeneration = 1u;
    rejected.observedCargoRevision = 1u;
    rejected.cargoRevision = 1u;
    rejected.targetSkiff = 1u;
    rejected.targetSkiffGeneration = 1u;
    builder.append(rejected);
    builder.certify(20u, 11u, entities);
    builder.finishAndFinalize(entities);

    WreckwaterReplayPlayback playback;
    EXPECT_EQ(
        playback.initialize(builder.recorder).error,
        WreckwaterReplayPlaybackError::GenerationAlias);
}

TEST(
    WreckwaterReplayPlaybackTest,
    EnforcesExplicitRosterConnectionAndGenerationProvenance) {
    enum class BadLifecycle : uint32_t {
        ConnectBeforeRegister,
        ReconnectBeforeInitialConnect,
        DuplicateSeat,
        ReusedConnection,
        OldReconnectGeneration,
        CurrentConnectionMarkedStale,
        CommandBootstrap,
    };
    constexpr std::array cases{
        BadLifecycle::ConnectBeforeRegister,
        BadLifecycle::ReconnectBeforeInitialConnect,
        BadLifecycle::DuplicateSeat,
        BadLifecycle::ReusedConnection,
        BadLifecycle::OldReconnectGeneration,
        BadLifecycle::CurrentConnectionMarkedStale,
        BadLifecycle::CommandBootstrap,
    };
    for (const BadLifecycle bad : cases) {
        SCOPED_TRACE(static_cast<uint32_t>(bad));
        RecordingBuilder builder;
        builder.initialize();
        if (bad == BadLifecycle::ConnectBeforeRegister) {
            MatchEvent connected =
                builder.event(0u, MatchEventType::PlayerConnected);
            const TestPlayer& player = builder.players[0];
            connected.playerId = player.playerId;
            connected.connectionId = player.connectionId;
            connected.connectionGeneration = 1u;
            connected.crew = player.crew;
            connected.seat = player.seat;
            builder.append(connected);
        }
        if (bad == BadLifecycle::ReconnectBeforeInitialConnect) {
            builder.appendRegistrationsOnly();
            MatchEvent reconnect =
                builder.event(0u, MatchEventType::PlayerReconnected);
            const TestPlayer& player = builder.players[0];
            reconnect.playerId = player.playerId;
            reconnect.connectionId = player.connectionId;
            reconnect.connectionGeneration = 1u;
            reconnect.crew = player.crew;
            reconnect.seat = player.seat;
            builder.append(reconnect);
            builder.appendConnectionsOnly();
            builder.appendStartAndLive();
        } else if (bad == BadLifecycle::DuplicateSeat) {
            builder.appendRosterOnly();
            MatchEvent duplicate =
                builder.event(0u, MatchEventType::PlayerRegistered);
            duplicate.playerId = 999u;
            duplicate.crew = CrewId::CrewOne;
            duplicate.seat = 0u;
            builder.append(duplicate);
            builder.appendStartAndLive();
        } else {
            builder.appendRosterAndLive();
        }

        const auto entities = makeEntities(makeCargo(
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u));
        builder.certify(10u, 10u, entities);
        if (bad == BadLifecycle::ReusedConnection
            || bad == BadLifecycle::OldReconnectGeneration) {
            builder.disconnect(0u, 12u);
            MatchEvent reconnect =
                builder.event(13u, MatchEventType::PlayerReconnected);
            const TestPlayer& player = builder.players[0];
            reconnect.playerId = player.playerId;
            reconnect.connectionId =
                bad == BadLifecycle::ReusedConnection
                ? builder.players[1].connectionId : 9'999u;
            reconnect.connectionGeneration =
                bad == BadLifecycle::ReusedConnection ? 2u : 1u;
            reconnect.crew = player.crew;
            reconnect.seat = player.seat;
            builder.append(reconnect);
        } else if (
            bad == BadLifecycle::CurrentConnectionMarkedStale
            || bad == BadLifecycle::CommandBootstrap) {
            MatchEvent rejected =
                builder.event(12u, MatchEventType::CommandRejected);
            rejected.sourcePhysicsTick = 11u;
            rejected.commandTick = 12u;
            rejected.sourceSequence = 1u;
            rejected.source = MatchEventSource::PlayerCommand;
            rejected.commandRejection =
                bad == BadLifecycle::CurrentConnectionMarkedStale
                ? CommandRejectReason::StaleConnection
                : CommandRejectReason::ConflictLost;
            const TestPlayer& player =
                bad == BadLifecycle::CurrentConnectionMarkedStale
                ? builder.players[0] : TestPlayer{
                    .playerId = 999u,
                    .connectionId = 9'999u,
                    .connectionGeneration = 1u,
                    .crew = CrewId::CrewOne,
                };
            rejected.playerId = player.playerId;
            rejected.connectionId = player.connectionId;
            rejected.connectionGeneration =
                player.connectionGeneration;
            rejected.crew = player.crew;
            rejected.seat = player.seat;
            rejected.cargoId = 1u;
            rejected.cargoGeneration = 1u;
            rejected.observedCargoRevision = 1u;
            rejected.cargoRevision = 1u;
            rejected.targetSkiff = 1u;
            rejected.targetSkiffGeneration = 1u;
            builder.append(rejected);
        }
        builder.finishAndFinalize(entities);

        WreckwaterReplayPlayback playback;
        EXPECT_EQ(
            playback.initialize(builder.recorder).error,
            WreckwaterReplayPlaybackError::GenerationAlias);
    }
}

TEST(
    WreckwaterReplayPlaybackTest,
    EnforcesExclusiveAndCompleteSourceBundles) {
    for (uint32_t caseIndex = 0u; caseIndex < 4u; ++caseIndex) {
        SCOPED_TRACE(caseIndex);
        RecordingBuilder builder;
        builder.initialize();
        builder.appendRosterAndLive();
        const auto before = makeEntities(
            caseIndex == 1u || caseIndex == 3u
            ? makeCargo(
                network::WreckwaterCargoDisposition::Towed,
                network::WreckwaterCrew::CrewOne, 1u,
                1u, 1u, 900u)
            : makeCargo(
                network::WreckwaterCargoDisposition::Free,
                network::WreckwaterCrew::None, 1u));
        builder.certify(10u, 10u, before);
        builder.bumpState();

        std::vector<network::WreckwaterEntityState> after;
        if (caseIndex == 0u) {
            MatchEvent rejected =
                builder.event(20u, MatchEventType::CommandRejected);
            setPlayerCommandSource(
                rejected, builder.players[0], 60u,
                MatchCommandType::TowCargo);
            rejected.commandRejection =
                CommandRejectReason::ConflictLost;
            rejected.cargoId = 1u;
            rejected.cargoGeneration = 1u;
            rejected.observedCargoRevision = 1u;
            rejected.cargoRevision = 2u;
            rejected.targetSkiff = 1u;
            rejected.targetSkiffGeneration = 1u;
            builder.append(rejected);
            builder.append(makeTowEvent(builder, 60u));
            after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Towed,
                network::WreckwaterCrew::CrewOne, 2u,
                1u, 1u, 900u), true);
        } else if (caseIndex == 1u) {
            builder.crewOneScore = 1'000u;
            MatchEvent banked =
                builder.event(20u, MatchEventType::CargoBanked);
            banked.crewOneScore = builder.crewOneScore;
            setPlayerCommandSource(
                banked, builder.players[0], 61u,
                MatchCommandType::BankCargo);
            banked.cargoId = 1u;
            banked.cargoGeneration = 1u;
            banked.observedCargoRevision = 1u;
            banked.cargoRevision = 2u;
            banked.sourceSkiff = 1u;
            banked.sourceSkiffGeneration = 1u;
            banked.attachmentId = 900u;
            banked.attachmentGeneration = 1u;
            banked.sourceAttachmentId = 900u;
            banked.sourceAttachmentGeneration = 1u;
            builder.append(banked);
            after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Banked,
                network::WreckwaterCrew::CrewOne, 2u), true);
        } else if (caseIndex == 2u) {
            MatchEvent sunk =
                builder.event(20u, MatchEventType::SkiffSunk);
            setWorldSource(
                sunk, 62u,
                AuthoritativeWorldEventType::SkiffSunk);
            sunk.crew = CrewId::CrewOne;
            sunk.sourceSkiff = 1u;
            sunk.sourceSkiffGeneration = 1u;
            builder.append(sunk);
            MatchEvent lost =
                builder.event(20u, MatchEventType::CargoLost);
            setWorldSource(
                lost, 62u,
                AuthoritativeWorldEventType::SkiffSunk);
            lost.crew = CrewId::CrewOne;
            lost.cargoId = 1u;
            lost.cargoGeneration = 1u;
            lost.observedCargoRevision = 1u;
            lost.cargoRevision = 2u;
            lost.sourceSkiff = 1u;
            lost.sourceSkiffGeneration = 1u;
            lost.attachmentId = 900u;
            lost.attachmentGeneration = 1u;
            lost.sourceAttachmentId = 900u;
            lost.sourceAttachmentGeneration = 1u;
            builder.append(lost);
            after = before;
        } else {
            builder.crewOneScore = 1'000u;
            MatchEvent score =
                builder.event(20u, MatchEventType::ScoreChanged);
            setPlayerCommandSource(
                score, builder.players[0], 63u,
                MatchCommandType::BankCargo);
            score.cargoId = 1u;
            score.cargoGeneration = 1u;
            score.observedCargoRevision = 1u;
            score.cargoRevision = 2u;
            builder.append(score);
            MatchEvent banked =
                builder.event(20u, MatchEventType::CargoBanked);
            setPlayerCommandSource(
                banked, builder.players[0], 63u,
                MatchCommandType::BankCargo);
            banked.cargoId = 1u;
            banked.cargoGeneration = 1u;
            banked.observedCargoRevision = 1u;
            banked.cargoRevision = 2u;
            banked.sourceSkiff = 1u;
            banked.sourceSkiffGeneration = 1u;
            banked.attachmentId = 900u;
            banked.attachmentGeneration = 1u;
            banked.sourceAttachmentId = 900u;
            banked.sourceAttachmentGeneration = 1u;
            builder.append(banked);
            after = makeEntities(makeCargo(
                network::WreckwaterCargoDisposition::Banked,
                network::WreckwaterCrew::CrewOne, 2u), true);
        }
        builder.certify(20u, 11u, after);
        builder.finishAndFinalize(after);

        WreckwaterReplayPlayback playback;
        EXPECT_EQ(
            playback.initialize(builder.recorder).error,
            WreckwaterReplayPlaybackError::InvalidEvent);
    }
}

TEST(
    WreckwaterReplayPlaybackTest,
    RequiresFourRosterBoundLifecycleAccurateStableCharacters) {
    enum class BadCharacter : uint32_t {
        Missing,
        UnknownPlayer,
        WrongGeneration,
        WrongConnectedFlag,
        ChangedHandle,
    };
    constexpr std::array cases{
        BadCharacter::Missing,
        BadCharacter::UnknownPlayer,
        BadCharacter::WrongGeneration,
        BadCharacter::WrongConnectedFlag,
        BadCharacter::ChangedHandle,
    };
    for (const BadCharacter bad : cases) {
        SCOPED_TRACE(static_cast<uint32_t>(bad));
        RecordingBuilder builder;
        builder.initialize();
        builder.appendRosterAndLive();
        const auto entities = makeEntities(makeCargo(
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u));
        if (bad == BadCharacter::ChangedHandle) {
            builder.certify(10u, 10u, entities);
        }
        auto malformed = builder.snapshot(
            bad == BadCharacter::ChangedHandle ? 20u : 10u,
            bad == BadCharacter::ChangedHandle ? 20u : 10u,
            entities);
        switch (bad) {
            case BadCharacter::Missing:
                malformed.characters.pop_back();
                break;
            case BadCharacter::UnknownPlayer:
                malformed.characters[3].playerId = 999u;
                break;
            case BadCharacter::WrongGeneration:
                ++malformed.characters[0].connectionGeneration;
                break;
            case BadCharacter::WrongConnectedFlag:
                malformed.characters[0].stateFlags &=
                    ~network::
                        kWreckwaterCharacterStateConnectedFlag;
                break;
            case BadCharacter::ChangedHandle:
                malformed.characters[0].characterHandle = 99'999u;
                break;
        }
        ASSERT_TRUE(
            network::canonicalizeWreckwaterSnapshot(malformed));
        builder.appendSnapshot(malformed);
        builder.finishAndFinalize(entities);

        WreckwaterReplayPlayback playback;
        const auto result = playback.initialize(builder.recorder);
        EXPECT_TRUE(
            result.error
                == WreckwaterReplayPlaybackError::InvalidSnapshot
            || result.error
                == WreckwaterReplayPlaybackError::GenerationAlias)
            << wreckwaterReplayPlaybackErrorName(result.error);
    }
}

TEST(
    WreckwaterReplayPlaybackTest,
    TiesSystemPhaseScoreOutcomeAndStateHashToCertificates) {
    for (uint32_t caseIndex = 0u; caseIndex < 3u; ++caseIndex) {
        SCOPED_TRACE(caseIndex);
        RecordingBuilder builder;
        builder.initialize();
        builder.appendRosterAndLive();
        const auto entities = makeEntities(makeCargo(
            network::WreckwaterCargoDisposition::Free,
            network::WreckwaterCrew::None, 1u));
        if (caseIndex == 0u) {
            MatchEvent noncanonical =
                builder.event(12u, MatchEventType::PlayerDisconnected);
            const TestPlayer& player = builder.players[0];
            noncanonical.sourcePhysicsTick = 1u;
            noncanonical.playerId = player.playerId;
            noncanonical.connectionId = player.connectionId;
            noncanonical.connectionGeneration =
                player.connectionGeneration;
            noncanonical.crew = player.crew;
            noncanonical.seat = player.seat;
            builder.append(noncanonical);
            builder.certify(20u, 20u, entities);
            builder.finishAndFinalize(entities);
        } else if (caseIndex == 1u) {
            auto wrongHash = builder.snapshot(10u, 10u, entities);
            ++wrongHash.matchStateHash;
            builder.appendSnapshot(wrongHash);
            builder.finishAndFinalize(entities);
        } else {
            builder.certify(10u, 10u, entities);
            builder.enterFinishedState();
            MatchEvent finished =
                builder.event(30u, MatchEventType::MatchFinished);
            builder.append(finished);
            builder.finalizeWith(
                builder.snapshot(30u, 30u, entities));
        }

        WreckwaterReplayPlayback playback;
        const auto error =
            playback.initialize(builder.recorder).error;
        if (caseIndex == 0u) {
            EXPECT_EQ(
                error,
                WreckwaterReplayPlaybackError::InvalidEvent);
        } else {
            EXPECT_EQ(
                error,
                WreckwaterReplayPlaybackError::InvalidSnapshot);
        }
    }
}

TEST(
    WreckwaterReplayPlaybackTest,
    AggregateLifetimeBudgetCannotBeMultipliedAcrossRegistries) {
    const auto encoded = encodedStealRecording();
    WreckwaterReplayPlaybackConfig limited;
    limited.maximumTrackedLifetimes = 15u;
    WreckwaterReplayPlayback playback;
    EXPECT_EQ(
        playback.initialize(encoded.bytes, limited).error,
        WreckwaterReplayPlaybackError::LifetimeCapacity);
    EXPECT_FALSE(playback.initialized());
}

TEST(
    WreckwaterReplayPlaybackTest,
    OptionalLocalContentGateRejectsBeforePlaybackInitialization) {
    const auto encoded = encodedStealRecording();

    WreckwaterReplayPlaybackConfig matching;
    matching.expectedContentHash = makeReplayConfig().contentHash;
    WreckwaterReplayPlayback compatible;
    ASSERT_TRUE(compatible.initialize(encoded.bytes, matching));

    WreckwaterReplayPlaybackConfig foreign = matching;
    foreign.expectedContentHash ^= 0x1u;
    WreckwaterReplayPlayback rejected;
    const WreckwaterReplayPlaybackLoadResult result =
        rejected.initialize(encoded.bytes, foreign);
    EXPECT_EQ(
        result.error,
        WreckwaterReplayPlaybackError::ContentMismatch);
    EXPECT_FALSE(rejected.initialized());
    EXPECT_TRUE(rejected.snapshots().empty());
    EXPECT_TRUE(rejected.events().empty());
}

TEST(
    WreckwaterReplayPlaybackTest,
    RespawnGenerationSnapsOnlyAtItsCertifiedBoundary) {
    RecordingBuilder builder;
    buildTransitionRecording(
        builder, TransitionCase::SkiffRespawned);
    WreckwaterReplayPlayback playback;
    ASSERT_TRUE(playback.initialize(builder.recorder));

    ASSERT_EQ(
        playback.seek({
            .wholeTick = 10u,
            .subTick = kWreckwaterReplaySubticksPerTick / 2u,
        }),
        WreckwaterReplayPlaybackError::None);
    const auto& held = playbackEntity(playback.frame(), 10u);
    EXPECT_EQ(
        held.motion, WreckwaterReplayVisualMotion::HeldTopology);
    EXPECT_EQ(held.authoritativeState.netGeneration, 1u);
    EXPECT_EQ(held.authoritativeState.skiff.generation, 1u);

    ASSERT_EQ(
        playback.seek({.wholeTick = 11u, .subTick = 0u}),
        WreckwaterReplayPlaybackError::None);
    const auto& respawned = playbackEntity(playback.frame(), 10u);
    EXPECT_EQ(
        respawned.motion, WreckwaterReplayVisualMotion::Exact);
    EXPECT_EQ(respawned.authoritativeState.netGeneration, 2u);
    EXPECT_EQ(respawned.authoritativeState.skiff.generation, 2u);
}

TEST(
    WreckwaterReplayPlaybackTest,
    StepAndWreckCamNeverExposeUncertifiedFutureEvents) {
    WreckwaterReplayPlayback playback;
    ASSERT_TRUE(playback.initialize(encodedStealRecording().bytes));
    EXPECT_EQ(playback.visibleEventCount(), 10u);

    ASSERT_EQ(
        playback.seek({.wholeTick = 10u, .subTick = 65'535u}),
        WreckwaterReplayPlaybackError::None);
    EXPECT_EQ(playback.visibleEventCount(), 10u);
    EXPECT_NE(
        playback.wreckCamSelection().reasonEventSequence, 11u);

    const WreckwaterReplayStepResult step =
        playback.stepSubticks(1);
    ASSERT_TRUE(step);
    EXPECT_EQ(step.eventBegin, 10u);
    EXPECT_EQ(step.eventEnd, 11u);
    EXPECT_EQ(playback.visibleEventCount(), 11u);
    const WreckwaterWreckCamSelection camera =
        playback.wreckCamSelection();
    ASSERT_TRUE(camera.valid);
    EXPECT_EQ(camera.reasonEventSequence, 11u);

    const WreckwaterReplayStepResult reverse =
        playback.stepSubticks(-1);
    ASSERT_TRUE(reverse);
    EXPECT_EQ(reverse.direction, WreckwaterReplayStepDirection::Reverse);
    EXPECT_EQ(reverse.eventBegin, 10u);
    EXPECT_EQ(reverse.eventEnd, 11u);
    EXPECT_EQ(playback.visibleEventCount(), 10u);
}

TEST(
    WreckwaterReplayPlaybackTest,
    CapacityAndInvalidSeekFailuresAreAtomic) {
    const auto encoded = encodedStealRecording();
    WreckwaterReplayPlaybackConfig entityLimited;
    entityLimited.maximumEntities = 2u;
    WreckwaterReplayPlayback entityPlayback;
    EXPECT_EQ(
        entityPlayback.initialize(encoded.bytes, entityLimited).error,
        WreckwaterReplayPlaybackError::EntityCapacity);
    EXPECT_FALSE(entityPlayback.initialized());

    WreckwaterReplayPlaybackConfig densityLimited;
    densityLimited.maximumEventsPerTick = 8u;
    WreckwaterReplayPlayback densityPlayback;
    EXPECT_EQ(
        densityPlayback.initialize(encoded.bytes, densityLimited).error,
        WreckwaterReplayPlaybackError::EventCapacity);
    EXPECT_FALSE(densityPlayback.initialized());

    WreckwaterReplayPlayback playback;
    ASSERT_TRUE(playback.initialize(encoded.bytes));
    ASSERT_EQ(
        playback.seek({.wholeTick = 20u, .subTick = 0u}),
        WreckwaterReplayPlaybackError::None);
    const WreckwaterReplayPlaybackFrame before = playback.frame();
    EXPECT_EQ(
        playback.seek({
            .wholeTick =
                network::kWreckwaterMaximumApplicationTick + 1u,
            .subTick = 0u,
        }),
        WreckwaterReplayPlaybackError::InvalidTime);
    EXPECT_EQ(playback.frame(), before);
}

TEST(
    WreckwaterReplayPlaybackTest,
    RuntimeQueriesKeepPreparedStorageStable) {
    WreckwaterReplayPlayback playback;
    ASSERT_TRUE(playback.initialize(encodedStealRecording().bytes));
    const WreckwaterReplayPlaybackStorageState storage =
        playback.storageState();
    std::array<const void*, 3> entityStorage{};
    std::array<const void*, 3> characterStorage{};
    for (size_t index = 0u; index < 3u; ++index) {
        entityStorage[index] =
            playback.snapshots()[index].entities.data();
        characterStorage[index] =
            playback.snapshots()[index].characters.data();
    }

    for (uint32_t iteration = 0u; iteration < 2'000u; ++iteration) {
        ASSERT_EQ(
            playback.seek({
                .wholeTick = 10u + iteration % 21u,
                .subTick = static_cast<uint16_t>(iteration),
            }),
            WreckwaterReplayPlaybackError::None);
        ASSERT_TRUE(playback.wreckCamSelection().valid);
    }
    EXPECT_EQ(playback.storageState(), storage);
    for (size_t index = 0u; index < 3u; ++index) {
        EXPECT_EQ(
            playback.snapshots()[index].entities.data(),
            entityStorage[index]);
        EXPECT_EQ(
            playback.snapshots()[index].characters.data(),
            characterStorage[index]);
    }
}

} // namespace
} // namespace voxy::game
