#include "game/wreckwater_match.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

namespace voxy::game {
namespace {

constexpr uint32_t kHashOffset = 2'166'136'261u;
constexpr uint32_t kHashPrime = 16'777'619u;
constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

[[nodiscard]] uint32_t hashWord(uint32_t hash, uint32_t word) noexcept {
    for (uint32_t byte = 0u; byte < 4u; ++byte) {
        hash = (hash ^ ((word >> (byte * 8u)) & 0xffu)) * kHashPrime;
    }
    return hash;
}

[[nodiscard]] uint32_t hashU64(uint32_t hash, uint64_t word) noexcept {
    hash = hashWord(hash, static_cast<uint32_t>(word));
    return hashWord(hash, static_cast<uint32_t>(word >> 32u));
}

template <typename Enum>
requires std::is_enum_v<Enum>
[[nodiscard]] uint32_t enumWord(Enum value) noexcept {
    return static_cast<uint32_t>(value);
}

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

void addSaturated(uint64_t& value, uint64_t addition) noexcept {
    if (addition > std::numeric_limits<uint64_t>::max() - value) {
        value = std::numeric_limits<uint64_t>::max();
    } else {
        value += addition;
    }
}

[[nodiscard]] uint32_t commandPriority(MatchCommandType type) noexcept {
    switch (type) {
        case MatchCommandType::BankCargo: return 0u;
        case MatchCommandType::CutTow: return 1u;
        case MatchCommandType::StealCargo: return 2u;
        case MatchCommandType::TowCargo: return 3u;
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] uint32_t worldEventPriority(
    AuthoritativeWorldEventType type) noexcept {
    switch (type) {
        case AuthoritativeWorldEventType::AttachmentBroken: return 0u;
        case AuthoritativeWorldEventType::CargoLost: return 1u;
        case AuthoritativeWorldEventType::SkiffSunk: return 2u;
        case AuthoritativeWorldEventType::SkiffRespawned: return 3u;
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] bool completeTowIdentity(
    const AuthoritativeWorldEvent& event) noexcept {
    return event.skiffId != 0u && event.skiffGeneration != 0u
        && event.attachmentId != 0u
        && event.attachmentGeneration != 0u;
}

[[nodiscard]] bool emptyTowIdentity(
    const AuthoritativeWorldEvent& event) noexcept {
    return event.skiffId == 0u && event.skiffGeneration == 0u
        && event.attachmentId == 0u
        && event.attachmentGeneration == 0u;
}

[[nodiscard]] bool completeCargoAttachmentEvidence(
    const AuthoritativeWorldEvent& event) noexcept {
    return event.cargoId != 0u && event.cargoGeneration != 0u
        && event.cargoRevision != 0u && event.attachmentId != 0u
        && event.attachmentGeneration != 0u;
}

[[nodiscard]] bool emptyCargoAttachmentEvidence(
    const AuthoritativeWorldEvent& event) noexcept {
    return event.cargoId == 0u && event.cargoGeneration == 0u
        && event.cargoRevision == 0u && event.attachmentId == 0u
        && event.attachmentGeneration == 0u;
}

[[nodiscard]] uint32_t hashCommand(
    uint32_t hash, const MatchCommand& command) noexcept {
    hash = hashWord(hash, command.schemaVersion);
    hash = hashWord(hash, enumWord(command.type));
    hash = hashU64(hash, command.matchId);
    hash = hashU64(hash, command.worldId);
    hash = hashWord(hash, command.worldEpoch);
    hash = hashWord(hash, command.authorityEpoch);
    hash = hashU64(hash, command.tick);
    hash = hashU64(hash, command.physicsEvidenceTick);
    hash = hashU64(hash, command.sequence);
    hash = hashU64(hash, command.playerId);
    hash = hashU64(hash, command.connectionId);
    hash = hashWord(hash, command.connectionGeneration);
    hash = hashWord(hash, command.cargoId);
    hash = hashWord(hash, command.cargoGeneration);
    hash = hashWord(hash, command.observedCargoRevision);
    hash = hashWord(hash, command.skiffId);
    hash = hashWord(hash, command.skiffGeneration);
    for (const uint32_t value : command.reserved) {
        hash = hashWord(hash, value);
    }
    return hash;
}

[[nodiscard]] uint32_t hashWorldEvent(
    uint32_t hash, const AuthoritativeWorldEvent& event) noexcept {
    hash = hashWord(hash, event.schemaVersion);
    hash = hashWord(hash, enumWord(event.type));
    hash = hashU64(hash, event.matchId);
    hash = hashU64(hash, event.worldId);
    hash = hashWord(hash, event.worldEpoch);
    hash = hashWord(hash, event.authorityEpoch);
    hash = hashWord(hash, event.streamId);
    hash = hashU64(hash, event.sourcePhysicsTick);
    hash = hashU64(hash, event.applicationTick);
    hash = hashU64(hash, event.sequence);
    hash = hashWord(hash, event.cargoId);
    hash = hashWord(hash, event.cargoGeneration);
    hash = hashWord(hash, event.cargoRevision);
    hash = hashWord(hash, event.skiffId);
    hash = hashWord(hash, event.skiffGeneration);
    hash = hashWord(hash, event.nextSkiffGeneration);
    hash = hashU64(hash, event.attachmentId);
    hash = hashWord(hash, event.attachmentGeneration);
    for (const uint32_t value : event.reserved) {
        hash = hashWord(hash, value);
    }
    return hash;
}

[[nodiscard]] uint32_t hashEvent(
    uint32_t hash, const MatchEvent& event) noexcept {
    hash = hashWord(hash, event.schemaVersion);
    hash = hashU64(hash, event.eventSequence);
    hash = hashU64(hash, event.matchId);
    hash = hashU64(hash, event.worldId);
    hash = hashWord(hash, event.worldEpoch);
    hash = hashWord(hash, event.authorityEpoch);
    hash = hashU64(hash, event.tick);
    hash = hashU64(hash, event.sourcePhysicsTick);
    hash = hashU64(hash, event.commandTick);
    hash = hashU64(hash, event.sourceSequence);
    hash = hashWord(hash, enumWord(event.source));
    hash = hashWord(hash, event.sourceStreamId);
    hash = hashWord(hash, enumWord(event.commandType));
    hash = hashWord(hash, enumWord(event.type));
    hash = hashWord(hash, enumWord(event.worldEventType));
    hash = hashWord(hash, enumWord(event.phase));
    hash = hashWord(hash, enumWord(event.outcome));
    hash = hashWord(hash, enumWord(event.faultReason));
    hash = hashWord(hash, enumWord(event.commandRejection));
    hash = hashWord(hash, enumWord(event.worldEventRejection));
    hash = hashU64(hash, event.playerId);
    hash = hashWord(hash, event.seat);
    hash = hashU64(hash, event.connectionId);
    hash = hashWord(hash, event.connectionGeneration);
    hash = hashWord(hash, enumWord(event.crew));
    hash = hashWord(hash, event.cargoId);
    hash = hashWord(hash, event.cargoGeneration);
    hash = hashWord(hash, event.observedCargoRevision);
    hash = hashWord(hash, event.cargoRevision);
    hash = hashWord(hash, event.sourceSkiff);
    hash = hashWord(hash, event.sourceSkiffGeneration);
    hash = hashWord(hash, event.targetSkiff);
    hash = hashWord(hash, event.targetSkiffGeneration);
    hash = hashU64(hash, event.attachmentId);
    hash = hashWord(hash, event.attachmentGeneration);
    hash = hashU64(hash, event.sourceAttachmentId);
    hash = hashWord(hash, event.sourceAttachmentGeneration);
    hash = hashU64(hash, event.resultingAttachmentId);
    hash = hashWord(hash, event.resultingAttachmentGeneration);
    hash = hashWord(hash, event.crewOneScore);
    hash = hashWord(hash, event.crewTwoScore);
    return hashWord(hash, event.stateHash);
}

} // namespace

uint32_t wreckwaterInitialMatchEventStreamRollingHash(
    uint32_t configurationHash) noexcept {
    return hashWord(kHashOffset, configurationHash);
}

uint32_t wreckwaterAppendMatchEventStreamRollingHash(
    uint32_t rollingHash, const MatchEvent& event) noexcept {
    return hashEvent(rollingHash, event);
}

uint32_t wreckwaterFinalizeMatchEventStreamHash(
    uint32_t rollingHash,
    uint32_t configurationHash,
    uint32_t stateHash,
    uint64_t nextEventSequence,
    uint32_t eventCount) noexcept {
    uint32_t hash = hashWord(rollingHash, configurationHash);
    hash = hashWord(hash, stateHash);
    hash = hashU64(hash, nextEventSequence);
    return hashWord(hash, eventCount);
}

bool wreckwaterCommandLess(
    const MatchCommand& lhs, const MatchCommand& rhs) noexcept {
    return std::tuple{
        lhs.tick, lhs.cargoId, commandPriority(lhs.type), lhs.playerId,
        lhs.sequence, lhs.physicsEvidenceTick,
        lhs.connectionGeneration, lhs.connectionId,
        lhs.cargoGeneration, lhs.observedCargoRevision, lhs.skiffId,
        lhs.skiffGeneration}
        < std::tuple{
            rhs.tick, rhs.cargoId, commandPriority(rhs.type), rhs.playerId,
            rhs.sequence, rhs.physicsEvidenceTick,
            rhs.connectionGeneration, rhs.connectionId,
            rhs.cargoGeneration, rhs.observedCargoRevision, rhs.skiffId,
            rhs.skiffGeneration};
}

bool wreckwaterWorldEventLess(
    const AuthoritativeWorldEvent& lhs,
    const AuthoritativeWorldEvent& rhs) noexcept {
    return std::tuple{
        lhs.applicationTick, lhs.sourcePhysicsTick, lhs.sequence,
        worldEventPriority(lhs.type), lhs.cargoId, lhs.cargoGeneration,
        lhs.cargoRevision, lhs.skiffId, lhs.skiffGeneration,
        lhs.attachmentId, lhs.attachmentGeneration}
        < std::tuple{
            rhs.applicationTick, rhs.sourcePhysicsTick, rhs.sequence,
            worldEventPriority(rhs.type), rhs.cargoId, rhs.cargoGeneration,
            rhs.cargoRevision, rhs.skiffId, rhs.skiffGeneration,
            rhs.attachmentId, rhs.attachmentGeneration};
}

WreckwaterMatch::SequenceObservation
WreckwaterMatch::SequenceWindow::observe(
    uint64_t sequence, uint32_t maximumAdvance) noexcept {
    if (!initialized) {
        if (sequence > maximumAdvance) {
            return SequenceObservation::JumpTooLarge;
        }
        latest = sequence;
        bits = 1u;
        initialized = true;
        return SequenceObservation::Accepted;
    }
    if (sequence > latest) {
        const uint64_t advance = sequence - latest;
        if (advance > maximumAdvance) {
            return SequenceObservation::JumpTooLarge;
        }
        bits = advance >= 64u ? 1u : (bits << advance) | 1u;
        latest = sequence;
        return SequenceObservation::Accepted;
    }
    const uint64_t age = latest - sequence;
    if (age >= 64u) return SequenceObservation::TooOld;
    const uint64_t mask = uint64_t{1u} << age;
    if ((bits & mask) != 0u) return SequenceObservation::Duplicate;
    bits |= mask;
    return SequenceObservation::Accepted;
}

void WreckwaterMatch::SequenceWindow::reset() noexcept {
    latest = 0u;
    bits = 0u;
    initialized = false;
}

bool WreckwaterMatch::validConfig(const Config& config) noexcept {
    if (config.matchId == 0u || config.worldId == 0u
        || config.worldEpoch == 0u || config.authorityEpoch == 0u
        || config.worldEventStreamId == 0u
        || config.tickRateHz != kWreckwaterTickRateHz
        || config.warmupTicks == 0u || config.liveTicks == 0u
        || config.overtimeTicks == 0u
        || config.inputFutureWindow == 0u
        || config.worldEventFutureWindow == 0u
        || config.maximumWorldEventLagTicks == 0u
        || config.maximumActionEvidenceLagTicks == 0u
        || config.maximumCommandsPerPlayerTick == 0u
        || config.maximumCommandsPerPlayerTick
            > kWreckwaterMaximumCommandsPerPlayerTick
        || config.maximumWorldEventsPerTick == 0u
        || config.maximumWorldEventsPerTick
            > kWreckwaterMaximumWorldEventsPerTick
        || config.maximumSequenceAdvance == 0u
        || config.maximumWorldEventSequenceAdvance == 0u
        || config.maximumConnectionEventsPerPlayer < 3u
        || config.pendingCommandCapacity == 0u
        || config.pendingCommandCapacity > kWreckwaterMaximumPendingCommands
        || config.pendingWorldEventCapacity == 0u
        || config.pendingWorldEventCapacity
            > kWreckwaterMaximumPendingWorldEvents
        || config.eventCapacity == 0u
        || config.eventCapacity > kWreckwaterMaximumRecordedEvents
        || config.bankScore == 0u
        || config.reactorCargoId == 0u
        || config.reactorCargoGeneration == 0u
        || config.crewOneSkiffId == 0u
        || config.crewTwoSkiffId == 0u
        || config.crewOneSkiffId == config.crewTwoSkiffId) {
        return false;
    }

    const uint64_t totalTicks = uint64_t{config.warmupTicks}
        + config.liveTicks + config.overtimeTicks;
    if (totalTicks > kWreckwaterMaximumMatchTicks) return false;

    const uint64_t requiredPending =
        uint64_t{config.inputFutureWindow} * kWreckwaterPlayerCount
        * config.maximumCommandsPerPlayerTick;
    const uint64_t requiredWorldPending =
        uint64_t{config.worldEventFutureWindow}
        * config.maximumWorldEventsPerTick;
    if (requiredPending > config.pendingCommandCapacity
        || requiredWorldPending > config.pendingWorldEventCapacity) {
        return false;
    }
    const uint64_t maximumPurgedCommands =
        uint64_t{kWreckwaterPlayerCount}
        * config.maximumConnectionEventsPerPlayer
        * config.inputFutureWindow
        * config.maximumCommandsPerPlayerTick;

    const uint64_t actionTicks =
        uint64_t{config.liveTicks} + config.overtimeTicks;
    const uint64_t requiredEvents =
        actionTicks * kWreckwaterPlayerCount
            * config.maximumCommandsPerPlayerTick
        + totalTicks * config.maximumWorldEventsPerTick
        + uint64_t{kWreckwaterPlayerCount}
            * (uint64_t{1u} + config.maximumConnectionEventsPerPlayer)
        + maximumPurgedCommands + requiredPending
        + requiredWorldPending + 32u;
    return requiredEvents <= config.eventCapacity;
}

bool WreckwaterMatch::validCrew(CrewId crew) noexcept {
    return crew == CrewId::CrewOne || crew == CrewId::CrewTwo;
}

bool WreckwaterMatch::validCommandType(MatchCommandType type) noexcept {
    switch (type) {
        case MatchCommandType::TowCargo:
        case MatchCommandType::CutTow:
        case MatchCommandType::StealCargo:
        case MatchCommandType::BankCargo:
            return true;
    }
    return false;
}

bool WreckwaterMatch::validWorldEventType(
    AuthoritativeWorldEventType type) noexcept {
    switch (type) {
        case AuthoritativeWorldEventType::AttachmentBroken:
        case AuthoritativeWorldEventType::CargoLost:
        case AuthoritativeWorldEventType::SkiffSunk:
        case AuthoritativeWorldEventType::SkiffRespawned:
            return true;
    }
    return false;
}

uint32_t WreckwaterMatch::playerSlot(
    CrewId crew, uint32_t seat) noexcept {
    if (!validCrew(crew) || seat >= kWreckwaterPlayersPerCrew) {
        return kWreckwaterPlayerCount;
    }
    return (enumWord(crew) - 1u) * kWreckwaterPlayersPerCrew + seat;
}

bool WreckwaterMatch::initialize() {
    return initialize(Config{});
}

bool WreckwaterMatch::initialize(const Config& config) {
    if (!validConfig(config)) return false;

    std::vector<MatchCommand> replacementCommands;
    replacementCommands.reserve(config.pendingCommandCapacity);
    std::vector<AuthoritativeWorldEvent> replacementWorldEvents;
    replacementWorldEvents.reserve(config.pendingWorldEventCapacity);
    std::vector<MatchCommand> replacementPurgeScratch;
    replacementPurgeScratch.reserve(config.pendingCommandCapacity);
    std::vector<MatchEvent> replacementTentativeEvents;
    replacementTentativeEvents.reserve(
        static_cast<size_t>(config.pendingCommandCapacity)
        + config.pendingWorldEventCapacity + 32u);
    std::vector<MatchEvent> replacementEvents;
    replacementEvents.reserve(config.eventCapacity);

    config_ = config;
    players_ = {};
    commandWindows_ = {};
    worldEventWindow_ = {};
    crews_ = {{
        {CrewId::CrewOne, config.crewOneSkiffId, 0u},
        {CrewId::CrewTwo, config.crewTwoSkiffId, 0u},
    }};
    skiffs_ = {{
        {config.crewOneSkiffId, CrewId::CrewOne, 1u,
         SkiffDisposition::Active},
        {config.crewTwoSkiffId, CrewId::CrewTwo, 1u,
         SkiffDisposition::Active},
    }};
    cargo_ = {{
        {config.reactorCargoId, config.reactorCargoGeneration, 1u,
         CargoDisposition::Free, CrewId::None, 0u, 0u, 0u, 0u},
    }};
    pendingCommands_ = std::move(replacementCommands);
    pendingWorldEvents_ = std::move(replacementWorldEvents);
    purgedCommandsScratch_ = std::move(replacementPurgeScratch);
    tentativeEvents_ = std::move(replacementTentativeEvents);
    events_ = std::move(replacementEvents);
    tentativeIntents_ = {};
    tentativeAcks_ = {};
    tentativeIntentCount_ = 0u;
    publishedIntents_ = {};
    publishedIntentCount_ = 0u;
    currentTick_ = 0u;
    nextEventSequence_ = 1u;
    warmupEndTick_ = config.warmupTicks;
    liveEndTick_ = config.warmupTicks + config.liveTicks;
    overtimeEndTick_ = liveEndTick_ + config.overtimeTicks;
    stateHash_ = 0u;
    phase_ = MatchPhase::Warmup;
    outcome_ = {};
    fault_ = MatchFaultReason::None;
    telemetry_ = {};
    initialized_ = true;
    started_ = false;
    eventStreamRollingHash_ =
        wreckwaterInitialMatchEventStreamRollingHash(
            configurationHash());
    updateStateHash();
    return true;
}

size_t WreckwaterMatch::playerIndex(PlayerId playerId) const noexcept {
    for (size_t index = 0u; index < players_.size(); ++index) {
        if (players_[index].occupied
            && players_[index].playerId == playerId) {
            return index;
        }
    }
    return kInvalidIndex;
}

size_t WreckwaterMatch::cargoIndex(CargoId cargoId) const noexcept {
    for (size_t index = 0u; index < cargo_.size(); ++index) {
        if (cargo_[index].cargoId == cargoId) return index;
    }
    return kInvalidIndex;
}

size_t WreckwaterMatch::skiffIndex(SkiffId skiffId) const noexcept {
    for (size_t index = 0u; index < skiffs_.size(); ++index) {
        if (skiffs_[index].skiffId == skiffId) return index;
    }
    return kInvalidIndex;
}

RegistrationResult WreckwaterMatch::registerPlayer(
    const PlayerRegistration& registration) {
    clearPublishedIntents();
    if (!initialized_) return RegistrationResult::NotInitialized;
    if (started_) return RegistrationResult::RosterLocked;
    const uint32_t slot = playerSlot(registration.crew, registration.seat);
    if (registration.playerId == 0u || slot >= players_.size()) {
        return RegistrationResult::Invalid;
    }
    const size_t existing = playerIndex(registration.playerId);
    if (existing != kInvalidIndex) {
        const PlayerState& value = players_[existing];
        return value.crew == registration.crew
                && value.seat == registration.seat
            ? RegistrationResult::AlreadyRegistered
            : RegistrationResult::DuplicatePlayer;
    }
    PlayerState& destination = players_[slot];
    if (destination.occupied) return RegistrationResult::SeatOccupied;
    if (!hasEventCapacity(1u)) {
        incrementSaturated(telemetry_.eventCapacityFailures);
        return RegistrationResult::EventCapacity;
    }

    destination.playerId = registration.playerId;
    destination.crew = registration.crew;
    destination.seat = registration.seat;
    destination.occupied = true;
    incrementSaturated(telemetry_.registeredPlayers);
    updateStateHash();
    publishEvent(MatchEvent{
        .type = MatchEventType::PlayerRegistered,
        .playerId = destination.playerId,
        .seat = destination.seat,
        .crew = destination.crew,
    });
    return RegistrationResult::Registered;
}

void WreckwaterMatch::purgePendingCommands(
    PlayerId playerId, uint32_t connectionGeneration) {
    purgedCommandsScratch_.clear();
    size_t write = 0u;
    for (const MatchCommand& command : pendingCommands_) {
        if (command.playerId == playerId
            && command.connectionGeneration <= connectionGeneration) {
            purgedCommandsScratch_.push_back(command);
        } else {
            pendingCommands_[write] = command;
            ++write;
        }
    }
    pendingCommands_.resize(write);
}

ConnectionResult WreckwaterMatch::connectPlayer(
    PlayerId playerId, ConnectionId connectionId) {
    clearPublishedIntents();
    if (!initialized_) return ConnectionResult::NotInitialized;
    if (faulted()) return ConnectionResult::AuthorityFaulted;
    if (connectionId == 0u) return ConnectionResult::InvalidConnection;
    const size_t index = playerIndex(playerId);
    if (index == kInvalidIndex) return ConnectionResult::UnknownPlayer;
    PlayerState& value = players_[index];
    if (value.connected) {
        return value.connectionId == connectionId
            ? ConnectionResult::AlreadyConnected
            : ConnectionResult::MustDisconnectFirst;
    }
    for (size_t other = 0u; other < players_.size(); ++other) {
        if (other != index && players_[other].connected
            && players_[other].connectionId == connectionId) {
            return ConnectionResult::ConnectionInUse;
        }
    }
    if (value.connectionEventCount
        >= config_.maximumConnectionEventsPerPlayer) {
        return ConnectionResult::TransitionLimit;
    }
    const size_t purgeCount = static_cast<size_t>(std::count_if(
        pendingCommands_.begin(), pendingCommands_.end(),
        [playerId](const MatchCommand& command) {
            return command.playerId == playerId;
        }));
    if (!hasEventCapacity(purgeCount + 1u)) {
        incrementSaturated(telemetry_.eventCapacityFailures);
        return ConnectionResult::EventCapacity;
    }

    const bool reconnect = value.everConnected;
    const uint32_t oldGeneration = value.connectionGeneration;
    purgePendingCommands(playerId, oldGeneration);
    value.connectionId = connectionId;
    value.connected = true;
    value.everConnected = true;
    ++value.connectionGeneration;
    ++value.connectionEventCount;
    // Request sequences are player/session identities, not socket
    // identities. Preserve the replay window across reconnect so a resumed
    // client can continue monotonically and an old request cannot be replayed
    // under the new connection generation.
    incrementSaturated(telemetry_.connectionTransitions);
    if (reconnect) incrementSaturated(telemetry_.reconnects);
    const uint64_t purgedCount = purgedCommandsScratch_.size();
    addSaturated(telemetry_.rejectedCommands, purgedCount);
    updateStateHash();
    publishEvent(MatchEvent{
        .type = reconnect ? MatchEventType::PlayerReconnected
                          : MatchEventType::PlayerConnected,
        .playerId = value.playerId,
        .seat = value.seat,
        .connectionId = value.connectionId,
        .connectionGeneration = value.connectionGeneration,
        .crew = value.crew,
    });
    for (const MatchCommand& command : purgedCommandsScratch_) {
        publishEvent(MatchEvent{
            .sourcePhysicsTick = command.physicsEvidenceTick,
            .commandTick = command.tick,
            .sourceSequence = command.sequence,
            .source = MatchEventSource::PlayerCommand,
            .commandType = command.type,
            .type = MatchEventType::CommandRejected,
            .commandRejection = CommandRejectReason::StaleConnection,
            .playerId = command.playerId,
            .seat = value.seat,
            .connectionId = command.connectionId,
            .connectionGeneration = command.connectionGeneration,
            .crew = value.crew,
            .cargoId = command.cargoId,
            .cargoGeneration = command.cargoGeneration,
            .observedCargoRevision =
                command.observedCargoRevision,
            .cargoRevision = command.observedCargoRevision,
            .targetSkiff = command.skiffId,
            .targetSkiffGeneration = command.skiffGeneration,
        });
    }
    purgedCommandsScratch_.clear();
    return reconnect ? ConnectionResult::Reconnected
                     : ConnectionResult::Connected;
}

bool WreckwaterMatch::disconnectPlayer(
    PlayerId playerId, ConnectionId connectionId) {
    clearPublishedIntents();
    if (!initialized_ || faulted()) return false;
    const size_t index = playerIndex(playerId);
    if (index == kInvalidIndex) return false;
    PlayerState& value = players_[index];
    if (!value.connected || value.connectionId != connectionId
        || value.connectionEventCount
            >= config_.maximumConnectionEventsPerPlayer) {
        return false;
    }
    const uint32_t generation = value.connectionGeneration;
    const size_t purgeCount = static_cast<size_t>(std::count_if(
        pendingCommands_.begin(), pendingCommands_.end(),
        [playerId, generation](const MatchCommand& command) {
            return command.playerId == playerId
                && command.connectionGeneration <= generation;
        }));
    if (!hasEventCapacity(purgeCount + 1u)) {
        incrementSaturated(telemetry_.eventCapacityFailures);
        return false;
    }

    purgePendingCommands(playerId, generation);
    value.connected = false;
    ++value.connectionEventCount;
    // Keep the replay window while disconnected. Reconnect changes the
    // connection generation but never re-authorizes an old client sequence.
    incrementSaturated(telemetry_.connectionTransitions);
    const uint64_t purgedCount = purgedCommandsScratch_.size();
    addSaturated(telemetry_.rejectedCommands, purgedCount);
    updateStateHash();
    publishEvent(MatchEvent{
        .type = MatchEventType::PlayerDisconnected,
        .playerId = value.playerId,
        .seat = value.seat,
        .connectionId = value.connectionId,
        .connectionGeneration = value.connectionGeneration,
        .crew = value.crew,
    });
    for (const MatchCommand& command : purgedCommandsScratch_) {
        publishEvent(MatchEvent{
            .sourcePhysicsTick = command.physicsEvidenceTick,
            .commandTick = command.tick,
            .sourceSequence = command.sequence,
            .source = MatchEventSource::PlayerCommand,
            .commandType = command.type,
            .type = MatchEventType::CommandRejected,
            .commandRejection = CommandRejectReason::StaleConnection,
            .playerId = command.playerId,
            .seat = value.seat,
            .connectionId = command.connectionId,
            .connectionGeneration = command.connectionGeneration,
            .crew = value.crew,
            .cargoId = command.cargoId,
            .cargoGeneration = command.cargoGeneration,
            .observedCargoRevision =
                command.observedCargoRevision,
            .cargoRevision = command.observedCargoRevision,
            .targetSkiff = command.skiffId,
            .targetSkiffGeneration = command.skiffGeneration,
        });
    }
    purgedCommandsScratch_.clear();
    return true;
}

bool WreckwaterMatch::rosterComplete() const noexcept {
    return initialized_ && std::all_of(
        players_.begin(), players_.end(),
        [](const PlayerState& value) { return value.occupied; });
}

bool WreckwaterMatch::start() {
    clearPublishedIntents();
    if (!initialized_ || faulted() || started_ || !rosterComplete()
        || !std::all_of(
            players_.begin(), players_.end(),
            [](const PlayerState& value) { return value.connected; })) {
        return false;
    }
    if (!hasEventCapacity(1u)) {
        incrementSaturated(telemetry_.eventCapacityFailures);
        return false;
    }
    started_ = true;
    updateStateHash();
    publishEvent(MatchEvent{.type = MatchEventType::MatchStarted});
    return true;
}

CommandRejectReason WreckwaterMatch::validateSubmission(
    const MatchCommand& command, size_t& outPlayerIndex) const noexcept {
    outPlayerIndex = kInvalidIndex;
    if (!initialized_) return CommandRejectReason::NotInitialized;
    if (faulted()) return CommandRejectReason::AuthorityFaulted;
    if (!started_ || (phase_ != MatchPhase::Live
        && phase_ != MatchPhase::Overtime)) {
        return CommandRejectReason::MatchNotActive;
    }
    if (command.schemaVersion != kWreckwaterMatchSchemaVersion
        || !validCommandType(command.type)
        || command.matchId == 0u || command.worldId == 0u
        || command.worldEpoch == 0u || command.authorityEpoch == 0u
        || command.tick == 0u || command.physicsEvidenceTick == 0u
        || command.sequence == 0u
        || command.playerId == 0u || command.connectionId == 0u
        || command.connectionGeneration == 0u
        || command.cargoId == 0u || command.cargoGeneration == 0u
        || command.observedCargoRevision == 0u
        || command.skiffId == 0u || command.skiffGeneration == 0u
        || command.reserved[0] != 0u || command.reserved[1] != 0u) {
        return CommandRejectReason::Malformed;
    }
    if (command.matchId != config_.matchId
        || command.worldId != config_.worldId) {
        return CommandRejectReason::ForeignMatch;
    }
    if (command.worldEpoch != config_.worldEpoch) {
        return CommandRejectReason::StaleWorldEpoch;
    }
    if (command.authorityEpoch != config_.authorityEpoch) {
        return CommandRejectReason::StaleAuthorityEpoch;
    }
    outPlayerIndex = playerIndex(command.playerId);
    if (outPlayerIndex == kInvalidIndex) {
        return CommandRejectReason::UnknownPlayer;
    }
    const PlayerState& playerValue = players_[outPlayerIndex];
    if (!playerValue.connected) {
        return CommandRejectReason::PlayerDisconnected;
    }
    if (playerValue.connectionId != command.connectionId
        || playerValue.connectionGeneration
            != command.connectionGeneration) {
        return CommandRejectReason::StaleConnection;
    }
    const size_t cargoPosition = cargoIndex(command.cargoId);
    if (cargoPosition == kInvalidIndex) {
        return CommandRejectReason::UnknownCargo;
    }
    if (cargo_[cargoPosition].generation != command.cargoGeneration) {
        return CommandRejectReason::StaleCargoGeneration;
    }
    const size_t skiffPosition = skiffIndex(command.skiffId);
    if (skiffPosition == kInvalidIndex
        || skiffs_[skiffPosition].owner != playerValue.crew) {
        return CommandRejectReason::WrongSkiff;
    }
    if (skiffs_[skiffPosition].generation != command.skiffGeneration) {
        return CommandRejectReason::StaleSkiffGeneration;
    }
    if (command.tick <= currentTick_
        || command.tick - currentTick_ > config_.inputFutureWindow) {
        return CommandRejectReason::TickOutOfWindow;
    }
    if (command.physicsEvidenceTick > currentTick_) {
        return CommandRejectReason::PhysicsEvidenceInFuture;
    }
    if (command.tick - command.physicsEvidenceTick
        > config_.maximumActionEvidenceLagTicks) {
        return CommandRejectReason::PhysicsEvidenceTooOld;
    }
    return CommandRejectReason::None;
}

CommandSubmitResult WreckwaterMatch::submitCommand(
    const MatchCommand& command) {
    clearPublishedIntents();
    size_t playerPosition = kInvalidIndex;
    const CommandRejectReason reason =
        validateSubmission(command, playerPosition);
    if (reason != CommandRejectReason::None) {
        if (reason == CommandRejectReason::TickOutOfWindow
            && playerPosition != kInvalidIndex) {
            SequenceWindow replayProbe =
                commandWindows_[playerPosition];
            const SequenceObservation observation =
                replayProbe.observe(
                    command.sequence,
                    config_.maximumSequenceAdvance);
            if (observation == SequenceObservation::Duplicate
                || observation == SequenceObservation::TooOld) {
                incrementSaturated(telemetry_.rejectedCommands);
                incrementSaturated(telemetry_.replayedCommands);
                return {
                    false,
                    observation == SequenceObservation::Duplicate
                        ? CommandRejectReason::ReplayedSequence
                        : CommandRejectReason::SequenceTooOld,
                };
            }
        }
        incrementSaturated(telemetry_.rejectedCommands);
        if (reason == CommandRejectReason::Malformed) {
            incrementSaturated(telemetry_.malformedCommands);
        }
        return {false, reason};
    }

    SequenceWindow candidate = commandWindows_[playerPosition];
    switch (candidate.observe(
        command.sequence, config_.maximumSequenceAdvance)) {
        case SequenceObservation::Accepted:
            break;
        case SequenceObservation::Duplicate:
            incrementSaturated(telemetry_.rejectedCommands);
            incrementSaturated(telemetry_.replayedCommands);
            return {false, CommandRejectReason::ReplayedSequence};
        case SequenceObservation::TooOld:
            incrementSaturated(telemetry_.rejectedCommands);
            incrementSaturated(telemetry_.replayedCommands);
            return {false, CommandRejectReason::SequenceTooOld};
        case SequenceObservation::JumpTooLarge:
            incrementSaturated(telemetry_.rejectedCommands);
            incrementSaturated(telemetry_.malformedCommands);
            return {false, CommandRejectReason::SequenceJumpTooLarge};
    }

    const size_t samePlayerTick = static_cast<size_t>(std::count_if(
        pendingCommands_.begin(), pendingCommands_.end(),
        [&command](const MatchCommand& pending) {
            return pending.playerId == command.playerId
                && pending.connectionGeneration
                    == command.connectionGeneration
                && pending.tick == command.tick;
        }));
    if (samePlayerTick >= config_.maximumCommandsPerPlayerTick) {
        incrementSaturated(telemetry_.rejectedCommands);
        return {false, CommandRejectReason::PerTickLimit};
    }
    if (pendingCommands_.size() >= config_.pendingCommandCapacity) {
        incrementSaturated(telemetry_.rejectedCommands);
        return {false, CommandRejectReason::PendingCapacity};
    }

    commandWindows_[playerPosition] = candidate;
    const auto position = std::upper_bound(
        pendingCommands_.begin(), pendingCommands_.end(), command,
        [](const MatchCommand& value, const MatchCommand& existing) {
            return wreckwaterCommandLess(value, existing);
        });
    pendingCommands_.insert(position, command);
    incrementSaturated(telemetry_.queuedCommands);
    telemetry_.pendingHighWater = std::max(
        telemetry_.pendingHighWater,
        static_cast<uint32_t>(pendingCommands_.size()));
    updateStateHash();
    return {true, CommandRejectReason::None};
}

WorldEventRejectReason WreckwaterMatch::validateWorldEventSubmission(
    const AuthoritativeWorldEvent& event) const noexcept {
    if (!initialized_) return WorldEventRejectReason::NotInitialized;
    if (faulted()) return WorldEventRejectReason::AuthorityFaulted;
    if (!started_ || phase_ == MatchPhase::Finished) {
        return WorldEventRejectReason::MatchNotActive;
    }
    if (event.schemaVersion != kWreckwaterMatchSchemaVersion
        || !validWorldEventType(event.type)
        || event.matchId == 0u || event.worldId == 0u
        || event.worldEpoch == 0u || event.authorityEpoch == 0u
        || event.streamId == 0u || event.sourcePhysicsTick == 0u
        || event.applicationTick == 0u || event.sequence == 0u
        || event.reserved[0] != 0u || event.reserved[1] != 0u) {
        return WorldEventRejectReason::Malformed;
    }
    if (event.matchId != config_.matchId
        || event.worldId != config_.worldId) {
        return WorldEventRejectReason::ForeignMatch;
    }
    if (event.worldEpoch != config_.worldEpoch) {
        return WorldEventRejectReason::StaleWorldEpoch;
    }
    if (event.authorityEpoch != config_.authorityEpoch) {
        return WorldEventRejectReason::StaleAuthorityEpoch;
    }
    if (event.streamId != config_.worldEventStreamId) {
        return WorldEventRejectReason::ForeignStream;
    }
    if (event.applicationTick <= currentTick_
        || event.applicationTick - currentTick_
            > config_.worldEventFutureWindow) {
        return WorldEventRejectReason::TickOutOfWindow;
    }
    if (event.sourcePhysicsTick > event.applicationTick) {
        return WorldEventRejectReason::SourceTickInFuture;
    }
    if (event.applicationTick - event.sourcePhysicsTick
        > config_.maximumWorldEventLagTicks) {
        return WorldEventRejectReason::SourceTickTooOld;
    }

    const bool cargoKnown =
        cargoIndex(event.cargoId) != kInvalidIndex;
    const bool skiffKnown =
        skiffIndex(event.skiffId) != kInvalidIndex;
    switch (event.type) {
        case AuthoritativeWorldEventType::AttachmentBroken:
            if (event.cargoGeneration == 0u
                || event.cargoRevision == 0u
                || !completeTowIdentity(event)
                || event.nextSkiffGeneration != 0u) {
                return WorldEventRejectReason::Malformed;
            }
            if (!cargoKnown) return WorldEventRejectReason::UnknownCargo;
            if (!skiffKnown) return WorldEventRejectReason::UnknownSkiff;
            break;
        case AuthoritativeWorldEventType::CargoLost:
            if (event.cargoId == 0u || event.cargoGeneration == 0u
                || event.cargoRevision == 0u
                || (!completeTowIdentity(event)
                    && !emptyTowIdentity(event))
                || event.nextSkiffGeneration != 0u) {
                return WorldEventRejectReason::Malformed;
            }
            if (!cargoKnown) return WorldEventRejectReason::UnknownCargo;
            if (!emptyTowIdentity(event) && !skiffKnown) {
                return WorldEventRejectReason::UnknownSkiff;
            }
            break;
        case AuthoritativeWorldEventType::SkiffSunk:
            if (event.skiffId == 0u || event.skiffGeneration == 0u
                || event.nextSkiffGeneration != 0u
                || (!completeCargoAttachmentEvidence(event)
                    && !emptyCargoAttachmentEvidence(event))) {
                return WorldEventRejectReason::Malformed;
            }
            if (!skiffKnown) return WorldEventRejectReason::UnknownSkiff;
            if (!emptyCargoAttachmentEvidence(event) && !cargoKnown) {
                return WorldEventRejectReason::UnknownCargo;
            }
            break;
        case AuthoritativeWorldEventType::SkiffRespawned:
            if (event.skiffId == 0u || event.skiffGeneration == 0u
                || event.nextSkiffGeneration == 0u
                || !emptyCargoAttachmentEvidence(event)) {
                return WorldEventRejectReason::Malformed;
            }
            if (!skiffKnown) return WorldEventRejectReason::UnknownSkiff;
            break;
    }
    return WorldEventRejectReason::None;
}

WorldEventSubmitResult WreckwaterMatch::submitWorldEvent(
    const AuthoritativeWorldEvent& event) {
    clearPublishedIntents();
    const WorldEventRejectReason reason =
        validateWorldEventSubmission(event);
    if (reason != WorldEventRejectReason::None) {
        if (reason == WorldEventRejectReason::TickOutOfWindow) {
            SequenceWindow replayProbe = worldEventWindow_;
            const SequenceObservation observation =
                replayProbe.observe(
                    event.sequence,
                    config_.maximumWorldEventSequenceAdvance);
            if (observation == SequenceObservation::Duplicate
                || observation == SequenceObservation::TooOld) {
                incrementSaturated(telemetry_.rejectedWorldEvents);
                incrementSaturated(telemetry_.replayedWorldEvents);
                return {
                    false,
                    observation == SequenceObservation::Duplicate
                        ? WorldEventRejectReason::ReplayedSequence
                        : WorldEventRejectReason::SequenceTooOld,
                };
            }
        }
        incrementSaturated(telemetry_.rejectedWorldEvents);
        return {false, reason};
    }

    SequenceWindow candidate = worldEventWindow_;
    switch (candidate.observe(
        event.sequence,
        config_.maximumWorldEventSequenceAdvance)) {
        case SequenceObservation::Accepted:
            break;
        case SequenceObservation::Duplicate:
            incrementSaturated(telemetry_.rejectedWorldEvents);
            incrementSaturated(telemetry_.replayedWorldEvents);
            return {false, WorldEventRejectReason::ReplayedSequence};
        case SequenceObservation::TooOld:
            incrementSaturated(telemetry_.rejectedWorldEvents);
            incrementSaturated(telemetry_.replayedWorldEvents);
            return {false, WorldEventRejectReason::SequenceTooOld};
        case SequenceObservation::JumpTooLarge:
            incrementSaturated(telemetry_.rejectedWorldEvents);
            return {false, WorldEventRejectReason::SequenceJumpTooLarge};
    }

    const size_t sameTick = static_cast<size_t>(std::count_if(
        pendingWorldEvents_.begin(), pendingWorldEvents_.end(),
        [&event](const AuthoritativeWorldEvent& pending) {
            return pending.applicationTick == event.applicationTick;
        }));
    if (sameTick >= config_.maximumWorldEventsPerTick) {
        incrementSaturated(telemetry_.rejectedWorldEvents);
        return {false, WorldEventRejectReason::PerTickLimit};
    }
    if (pendingWorldEvents_.size()
        >= config_.pendingWorldEventCapacity) {
        incrementSaturated(telemetry_.rejectedWorldEvents);
        return {false, WorldEventRejectReason::PendingCapacity};
    }

    worldEventWindow_ = candidate;
    const auto position = std::upper_bound(
        pendingWorldEvents_.begin(), pendingWorldEvents_.end(), event,
        [](const AuthoritativeWorldEvent& value,
           const AuthoritativeWorldEvent& existing) {
            return wreckwaterWorldEventLess(value, existing);
        });
    pendingWorldEvents_.insert(position, event);
    incrementSaturated(telemetry_.queuedWorldEvents);
    telemetry_.pendingWorldEventHighWater = std::max(
        telemetry_.pendingWorldEventHighWater,
        static_cast<uint32_t>(pendingWorldEvents_.size()));
    updateStateHash();
    return {true, WorldEventRejectReason::None};
}

CommandRejectReason WreckwaterMatch::validateGameState(
    const StepTransaction& transaction, const MatchCommand& command,
    size_t playerPosition, size_t cargoPosition) const noexcept {
    if (transaction.phase != MatchPhase::Live
        && transaction.phase != MatchPhase::Overtime) {
        return CommandRejectReason::MatchNotActive;
    }
    const PlayerState& playerValue = players_[playerPosition];
    if (!playerValue.connected) {
        return CommandRejectReason::PlayerDisconnected;
    }
    if (playerValue.connectionId != command.connectionId
        || playerValue.connectionGeneration
            != command.connectionGeneration) {
        return CommandRejectReason::StaleConnection;
    }
    const size_t skiffPosition = skiffIndex(command.skiffId);
    if (skiffPosition == kInvalidIndex
        || transaction.skiffs[skiffPosition].owner != playerValue.crew) {
        return CommandRejectReason::WrongSkiff;
    }
    const SkiffState& skiffValue = transaction.skiffs[skiffPosition];
    if (skiffValue.generation != command.skiffGeneration) {
        return CommandRejectReason::StaleSkiffGeneration;
    }
    if (skiffValue.disposition != SkiffDisposition::Active) {
        return CommandRejectReason::SkiffUnavailable;
    }
    const CargoState& cargoValue = transaction.cargo[cargoPosition];
    if (cargoValue.generation != command.cargoGeneration
        || cargoValue.revision != command.observedCargoRevision) {
        return CommandRejectReason::StaleCargoRevision;
    }
    switch (command.type) {
        case MatchCommandType::TowCargo:
            return cargoValue.disposition == CargoDisposition::Free
                ? CommandRejectReason::None
                : CommandRejectReason::InvalidCargoState;
        case MatchCommandType::CutTow:
            return cargoValue.disposition == CargoDisposition::Towed
                    && cargoValue.towAttachmentId != 0u
                    && cargoValue.towAttachmentGeneration != 0u
                ? CommandRejectReason::None
                : CommandRejectReason::InvalidCargoState;
        case MatchCommandType::StealCargo:
            return cargoValue.disposition == CargoDisposition::Towed
                    && cargoValue.owner != playerValue.crew
                    && cargoValue.towAttachmentId != 0u
                    && cargoValue.towAttachmentGeneration != 0u
                ? CommandRejectReason::None
                : CommandRejectReason::InvalidCargoState;
        case MatchCommandType::BankCargo:
            return cargoValue.disposition == CargoDisposition::Towed
                    && cargoValue.owner == playerValue.crew
                    && cargoValue.towingSkiff == command.skiffId
                    && cargoValue.towingSkiffGeneration
                        == command.skiffGeneration
                    && cargoValue.towAttachmentId != 0u
                    && cargoValue.towAttachmentGeneration != 0u
                ? CommandRejectReason::None
                : CommandRejectReason::InvalidCargoState;
    }
    return CommandRejectReason::Malformed;
}

CommandRejectReason WreckwaterMatch::worldRejection(
    WorldValidationResult result) const noexcept {
    switch (result) {
        case WorldValidationResult::Allowed:
            return CommandRejectReason::None;
        case WorldValidationResult::ActorUnavailable:
            return CommandRejectReason::ActorUnavailable;
        case WorldValidationResult::SkiffUnavailable:
            return CommandRejectReason::SkiffUnavailable;
        case WorldValidationResult::OutOfRange:
            return CommandRejectReason::OutOfRange;
        case WorldValidationResult::OutsideExtraction:
            return CommandRejectReason::OutsideExtraction;
        case WorldValidationResult::Rejected:
            return CommandRejectReason::WorldRejected;
        case WorldValidationResult::PhysicsEvidenceUnavailable:
            return CommandRejectReason::PhysicsEvidenceUnavailable;
    }
    return CommandRejectReason::WorldRejected;
}

void WreckwaterMatch::appendTentativeEvent(
    const StepTransaction& transaction, MatchEvent event) {
    event.tick = transaction.tick;
    event.phase = transaction.phase;
    event.outcome = transaction.outcome.type;
    event.crewOneScore = transaction.crews[0].score;
    event.crewTwoScore = transaction.crews[1].score;
    tentativeEvents_.push_back(event);
}

void WreckwaterMatch::appendTentativeIntent(
    const MatchWorldIntent& intent) noexcept {
    if (tentativeIntentCount_ >= tentativeIntents_.size()) return;
    tentativeIntents_[tentativeIntentCount_] = intent;
    ++tentativeIntentCount_;
}

void WreckwaterMatch::rejectStepCommand(
    StepTransaction& transaction, const MatchCommand& command,
    CrewId crew, CommandRejectReason reason) {
    ++transaction.rejectedCommands;
    if (reason == CommandRejectReason::ConflictLost) {
        ++transaction.conflictRejections;
    }
    if (reason == CommandRejectReason::ActorUnavailable
        || reason == CommandRejectReason::SkiffUnavailable
        || reason == CommandRejectReason::OutOfRange
        || reason == CommandRejectReason::OutsideExtraction
        || reason == CommandRejectReason::WorldRejected) {
        ++transaction.worldRejections;
    }
    const size_t playerPosition = playerIndex(command.playerId);
    const PlayerState* playerValue = playerPosition == kInvalidIndex
        ? nullptr : &players_[playerPosition];
    const size_t cargoPosition = cargoIndex(command.cargoId);
    appendTentativeEvent(transaction, MatchEvent{
        .sourcePhysicsTick = command.physicsEvidenceTick,
        .commandTick = command.tick,
        .sourceSequence = command.sequence,
        .source = MatchEventSource::PlayerCommand,
        .commandType = command.type,
        .type = MatchEventType::CommandRejected,
        .commandRejection = reason,
        .playerId = command.playerId,
        .seat = playerValue == nullptr ? 0u : playerValue->seat,
        .connectionId = command.connectionId,
        .connectionGeneration = command.connectionGeneration,
        .crew = crew,
        .cargoId = command.cargoId,
        .cargoGeneration = command.cargoGeneration,
        .observedCargoRevision = command.observedCargoRevision,
        .cargoRevision = cargoPosition == kInvalidIndex
            ? command.observedCargoRevision
            : transaction.cargo[cargoPosition].revision,
        .targetSkiff = command.skiffId,
        .targetSkiffGeneration = command.skiffGeneration,
    });
}

void WreckwaterMatch::rejectStepWorldEvent(
    StepTransaction& transaction,
    const AuthoritativeWorldEvent& event,
    WorldEventRejectReason reason) {
    ++transaction.rejectedWorldEvents;
    appendTentativeEvent(transaction, MatchEvent{
        .sourcePhysicsTick = event.sourcePhysicsTick,
        .commandTick = event.applicationTick,
        .sourceSequence = event.sequence,
        .source = MatchEventSource::AuthoritativeWorldEvent,
        .sourceStreamId = event.streamId,
        .type = MatchEventType::WorldEventRejected,
        .worldEventType = event.type,
        .worldEventRejection = reason,
        .cargoId = event.cargoId,
        .cargoGeneration = event.cargoGeneration,
        .observedCargoRevision = event.cargoRevision,
        .cargoRevision = event.cargoRevision,
        .sourceSkiff = event.skiffId,
        .sourceSkiffGeneration = event.skiffGeneration,
        .attachmentId = event.attachmentId,
        .attachmentGeneration = event.attachmentGeneration,
        .sourceAttachmentId = event.attachmentId,
        .sourceAttachmentGeneration = event.attachmentGeneration,
    });
}

void WreckwaterMatch::applyWorldEvent(
    StepTransaction& transaction,
    const AuthoritativeWorldEvent& event) {
    const size_t cargoPosition = cargoIndex(event.cargoId);
    const size_t skiffPosition = skiffIndex(event.skiffId);
    switch (event.type) {
        case AuthoritativeWorldEventType::AttachmentBroken: {
            if (cargoPosition == kInvalidIndex) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::UnknownCargo);
                return;
            }
            if (skiffPosition == kInvalidIndex) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::UnknownSkiff);
                return;
            }
            CargoState& cargoValue = transaction.cargo[cargoPosition];
            if (cargoValue.generation != event.cargoGeneration) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleCargoGeneration);
                return;
            }
            if (cargoValue.revision != event.cargoRevision) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleCargoRevision);
                return;
            }
            if (transaction.skiffs[skiffPosition].generation
                    != event.skiffGeneration
                || cargoValue.towingSkiff != event.skiffId
                || cargoValue.towingSkiffGeneration
                    != event.skiffGeneration) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleSkiffGeneration);
                return;
            }
            if (cargoValue.disposition != CargoDisposition::Towed
                || cargoValue.towAttachmentId != event.attachmentId
                || cargoValue.towAttachmentGeneration
                    != event.attachmentGeneration) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleAttachment);
                return;
            }
            const uint32_t priorRevision = cargoValue.revision;
            const CrewId priorCrew =
                transaction.skiffs[skiffPosition].owner;
            ++cargoValue.revision;
            cargoValue.disposition = CargoDisposition::Free;
            cargoValue.owner = CrewId::None;
            cargoValue.towingSkiff = 0u;
            cargoValue.towingSkiffGeneration = 0u;
            cargoValue.towAttachmentId = 0u;
            cargoValue.towAttachmentGeneration = 0u;
            appendTentativeIntent(MatchWorldIntent{
                .type = MatchWorldIntentType::CutTow,
                .source =
                    MatchWorldIntentSource::
                        AuthoritativeWorldEvent,
                .matchId = config_.matchId,
                .worldId = config_.worldId,
                .worldEpoch = config_.worldEpoch,
                .authorityEpoch = config_.authorityEpoch,
                .applicationTick = transaction.tick,
                .physicsEvidenceTick = event.sourcePhysicsTick,
                .sourceStreamId = event.streamId,
                .sourceSequence = event.sequence,
                .crew = priorCrew,
                .cargoId = event.cargoId,
                .cargoGeneration = event.cargoGeneration,
                .priorCargoRevision = priorRevision,
                .resultingCargoRevision = cargoValue.revision,
                .sourceSkiff = event.skiffId,
                .sourceSkiffGeneration = event.skiffGeneration,
                .sourceAttachmentId = event.attachmentId,
                .sourceAttachmentGeneration =
                    event.attachmentGeneration,
            });
            ++transaction.appliedWorldEvents;
            appendTentativeEvent(transaction, MatchEvent{
                .sourcePhysicsTick = event.sourcePhysicsTick,
                .commandTick = event.applicationTick,
                .sourceSequence = event.sequence,
                .source =
                    MatchEventSource::AuthoritativeWorldEvent,
                .sourceStreamId = event.streamId,
                .type = MatchEventType::AttachmentBroken,
                .worldEventType = event.type,
                .cargoId = cargoValue.cargoId,
                .cargoGeneration = cargoValue.generation,
                .observedCargoRevision = event.cargoRevision,
                .cargoRevision = cargoValue.revision,
                .sourceSkiff = event.skiffId,
                .sourceSkiffGeneration = event.skiffGeneration,
                .attachmentId = event.attachmentId,
                .attachmentGeneration = event.attachmentGeneration,
                .sourceAttachmentId = event.attachmentId,
                .sourceAttachmentGeneration =
                    event.attachmentGeneration,
            });
            return;
        }
        case AuthoritativeWorldEventType::CargoLost: {
            if (cargoPosition == kInvalidIndex) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::UnknownCargo);
                return;
            }
            CargoState& cargoValue = transaction.cargo[cargoPosition];
            if (cargoValue.generation != event.cargoGeneration) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleCargoGeneration);
                return;
            }
            if (cargoValue.revision != event.cargoRevision) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleCargoRevision);
                return;
            }
            if (cargoValue.disposition == CargoDisposition::Banked
                || cargoValue.disposition == CargoDisposition::Lost) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::InvalidState);
                return;
            }
            const bool wasTowed =
                cargoValue.disposition == CargoDisposition::Towed;
            CrewId priorCrew = CrewId::None;
            if (wasTowed) {
                if (skiffPosition == kInvalidIndex) {
                    rejectStepWorldEvent(
                        transaction, event,
                        WorldEventRejectReason::UnknownSkiff);
                    return;
                }
                if (transaction.skiffs[skiffPosition].generation
                        != event.skiffGeneration
                    || cargoValue.towingSkiff != event.skiffId
                    || cargoValue.towingSkiffGeneration
                        != event.skiffGeneration
                    || cargoValue.towAttachmentId
                        != event.attachmentId
                    || cargoValue.towAttachmentGeneration
                        != event.attachmentGeneration) {
                    rejectStepWorldEvent(
                        transaction, event,
                        WorldEventRejectReason::StaleAttachment);
                    return;
                }
                priorCrew =
                    transaction.skiffs[skiffPosition].owner;
            } else if (!emptyTowIdentity(event)) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleAttachment);
                return;
            }
            const uint32_t priorRevision = cargoValue.revision;
            ++cargoValue.revision;
            cargoValue.disposition = CargoDisposition::Lost;
            cargoValue.owner = CrewId::None;
            cargoValue.towingSkiff = 0u;
            cargoValue.towingSkiffGeneration = 0u;
            cargoValue.towAttachmentId = 0u;
            cargoValue.towAttachmentGeneration = 0u;
            if (wasTowed) {
                appendTentativeIntent(MatchWorldIntent{
                    .type = MatchWorldIntentType::CutTow,
                    .source =
                        MatchWorldIntentSource::
                            AuthoritativeWorldEvent,
                    .matchId = config_.matchId,
                    .worldId = config_.worldId,
                    .worldEpoch = config_.worldEpoch,
                    .authorityEpoch = config_.authorityEpoch,
                    .applicationTick = transaction.tick,
                    .physicsEvidenceTick =
                        event.sourcePhysicsTick,
                    .sourceStreamId = event.streamId,
                    .sourceSequence = event.sequence,
                    .crew = priorCrew,
                    .cargoId = event.cargoId,
                    .cargoGeneration = event.cargoGeneration,
                    .priorCargoRevision = priorRevision,
                    .resultingCargoRevision =
                        cargoValue.revision,
                    .sourceSkiff = event.skiffId,
                    .sourceSkiffGeneration =
                        event.skiffGeneration,
                    .sourceAttachmentId =
                        event.attachmentId,
                    .sourceAttachmentGeneration =
                        event.attachmentGeneration,
                });
            }
            ++transaction.appliedWorldEvents;
            appendTentativeEvent(transaction, MatchEvent{
                .sourcePhysicsTick = event.sourcePhysicsTick,
                .commandTick = event.applicationTick,
                .sourceSequence = event.sequence,
                .source =
                    MatchEventSource::AuthoritativeWorldEvent,
                .sourceStreamId = event.streamId,
                .type = MatchEventType::CargoLost,
                .worldEventType = event.type,
                .cargoId = cargoValue.cargoId,
                .cargoGeneration = cargoValue.generation,
                .observedCargoRevision = event.cargoRevision,
                .cargoRevision = cargoValue.revision,
                .sourceSkiff = event.skiffId,
                .sourceSkiffGeneration = event.skiffGeneration,
                .attachmentId = event.attachmentId,
                .attachmentGeneration = event.attachmentGeneration,
                .sourceAttachmentId = event.attachmentId,
                .sourceAttachmentGeneration =
                    event.attachmentGeneration,
            });
            return;
        }
        case AuthoritativeWorldEventType::SkiffSunk: {
            if (skiffPosition == kInvalidIndex) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::UnknownSkiff);
                return;
            }
            SkiffState& skiffValue = transaction.skiffs[skiffPosition];
            if (skiffValue.generation != event.skiffGeneration) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleSkiffGeneration);
                return;
            }
            if (skiffValue.disposition != SkiffDisposition::Active) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::InvalidState);
                return;
            }

            CargoState* towedCargo = nullptr;
            for (CargoState& cargoValue : transaction.cargo) {
                if (cargoValue.disposition == CargoDisposition::Towed
                    && cargoValue.towingSkiff == event.skiffId) {
                    towedCargo = &cargoValue;
                    break;
                }
            }
            if (towedCargo != nullptr) {
                if (!completeCargoAttachmentEvidence(event)
                    || towedCargo->cargoId != event.cargoId
                    || towedCargo->generation
                        != event.cargoGeneration
                    || towedCargo->revision != event.cargoRevision
                    || towedCargo->towingSkiffGeneration
                        != event.skiffGeneration
                    || towedCargo->towAttachmentId
                        != event.attachmentId
                    || towedCargo->towAttachmentGeneration
                        != event.attachmentGeneration) {
                    rejectStepWorldEvent(
                        transaction, event,
                        WorldEventRejectReason::StaleAttachment);
                    return;
                }

                const uint32_t priorRevision = towedCargo->revision;
                ++towedCargo->revision;
                towedCargo->disposition = CargoDisposition::Lost;
                towedCargo->owner = CrewId::None;
                towedCargo->towingSkiff = 0u;
                towedCargo->towingSkiffGeneration = 0u;
                towedCargo->towAttachmentId = 0u;
                towedCargo->towAttachmentGeneration = 0u;
                appendTentativeIntent(MatchWorldIntent{
                    .type = MatchWorldIntentType::CutTow,
                    .source =
                        MatchWorldIntentSource::
                            AuthoritativeWorldEvent,
                    .matchId = config_.matchId,
                    .worldId = config_.worldId,
                    .worldEpoch = config_.worldEpoch,
                    .authorityEpoch = config_.authorityEpoch,
                    .applicationTick = transaction.tick,
                    .physicsEvidenceTick = event.sourcePhysicsTick,
                    .sourceStreamId = event.streamId,
                    .sourceSequence = event.sequence,
                    .crew = skiffValue.owner,
                    .cargoId = event.cargoId,
                    .cargoGeneration = event.cargoGeneration,
                    .priorCargoRevision = priorRevision,
                    .resultingCargoRevision = towedCargo->revision,
                    .sourceSkiff = event.skiffId,
                    .sourceSkiffGeneration =
                        event.skiffGeneration,
                    .sourceAttachmentId = event.attachmentId,
                    .sourceAttachmentGeneration =
                        event.attachmentGeneration,
                });
                appendTentativeEvent(transaction, MatchEvent{
                    .sourcePhysicsTick = event.sourcePhysicsTick,
                    .commandTick = event.applicationTick,
                    .sourceSequence = event.sequence,
                    .source =
                        MatchEventSource::AuthoritativeWorldEvent,
                    .sourceStreamId = event.streamId,
                    .type = MatchEventType::CargoLost,
                    .worldEventType = event.type,
                    .crew = skiffValue.owner,
                    .cargoId = towedCargo->cargoId,
                    .cargoGeneration = towedCargo->generation,
                    .observedCargoRevision = event.cargoRevision,
                    .cargoRevision = towedCargo->revision,
                    .sourceSkiff = event.skiffId,
                    .sourceSkiffGeneration =
                        event.skiffGeneration,
                    .attachmentId = event.attachmentId,
                    .attachmentGeneration =
                        event.attachmentGeneration,
                    .sourceAttachmentId = event.attachmentId,
                    .sourceAttachmentGeneration =
                        event.attachmentGeneration,
                });
            } else if (!emptyCargoAttachmentEvidence(event)) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleAttachment);
                return;
            }

            skiffValue.disposition = SkiffDisposition::Sunk;
            ++transaction.appliedWorldEvents;
            appendTentativeEvent(transaction, MatchEvent{
                .sourcePhysicsTick = event.sourcePhysicsTick,
                .commandTick = event.applicationTick,
                .sourceSequence = event.sequence,
                .source =
                    MatchEventSource::AuthoritativeWorldEvent,
                .sourceStreamId = event.streamId,
                .type = MatchEventType::SkiffSunk,
                .worldEventType = event.type,
                .crew = skiffValue.owner,
                .sourceSkiff = skiffValue.skiffId,
                .sourceSkiffGeneration = skiffValue.generation,
            });
            return;
        }
        case AuthoritativeWorldEventType::SkiffRespawned: {
            if (skiffPosition == kInvalidIndex) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::UnknownSkiff);
                return;
            }
            SkiffState& skiffValue = transaction.skiffs[skiffPosition];
            if (skiffValue.generation != event.skiffGeneration) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::StaleSkiffGeneration);
                return;
            }
            if (skiffValue.disposition != SkiffDisposition::Sunk
                || skiffValue.generation
                    == std::numeric_limits<uint32_t>::max()
                || event.nextSkiffGeneration
                    != skiffValue.generation + 1u) {
                rejectStepWorldEvent(
                    transaction, event,
                    WorldEventRejectReason::InvalidState);
                return;
            }
            const uint32_t priorGeneration = skiffValue.generation;
            skiffValue.generation = event.nextSkiffGeneration;
            skiffValue.disposition = SkiffDisposition::Active;
            ++transaction.appliedWorldEvents;
            appendTentativeEvent(transaction, MatchEvent{
                .sourcePhysicsTick = event.sourcePhysicsTick,
                .commandTick = event.applicationTick,
                .sourceSequence = event.sequence,
                .source =
                    MatchEventSource::AuthoritativeWorldEvent,
                .sourceStreamId = event.streamId,
                .type = MatchEventType::SkiffRespawned,
                .worldEventType = event.type,
                .crew = skiffValue.owner,
                .sourceSkiff = skiffValue.skiffId,
                .sourceSkiffGeneration = priorGeneration,
                .targetSkiff = skiffValue.skiffId,
                .targetSkiffGeneration = skiffValue.generation,
            });
            return;
        }
    }
    rejectStepWorldEvent(
        transaction, event, WorldEventRejectReason::Malformed);
}

void WreckwaterMatch::applyCommand(
    StepTransaction& transaction, const MatchCommand& command,
    size_t playerPosition, size_t cargoPosition,
    const IWreckwaterWorldAuthority& world,
    std::array<bool, kWreckwaterCargoCount>& cargoMutated) {
    const PlayerState& playerValue = players_[playerPosition];
    if (!playerValue.connected) {
        rejectStepCommand(
            transaction, command, playerValue.crew,
            CommandRejectReason::PlayerDisconnected);
        return;
    }
    if (playerValue.connectionId != command.connectionId
        || playerValue.connectionGeneration
            != command.connectionGeneration) {
        rejectStepCommand(
            transaction, command, playerValue.crew,
            CommandRejectReason::StaleConnection);
        return;
    }
    if (cargoMutated[cargoPosition]) {
        rejectStepCommand(
            transaction, command, playerValue.crew,
            CommandRejectReason::ConflictLost);
        return;
    }
    const CommandRejectReason stateReason = validateGameState(
        transaction, command, playerPosition, cargoPosition);
    if (stateReason != CommandRejectReason::None) {
        rejectStepCommand(
            transaction, command, playerValue.crew, stateReason);
        return;
    }

    const size_t actorSkiffPosition = skiffIndex(command.skiffId);
    const CargoState prior = transaction.cargo[cargoPosition];
    const MatchActionQuery query{
        .command = command,
        .applicationTick = transaction.tick,
        .physicsEvidenceTick = command.physicsEvidenceTick,
        .actorCrew = playerValue.crew,
        .cargo = prior,
        .actorSkiff = transaction.skiffs[actorSkiffPosition],
    };
    const CommandRejectReason authorityReason =
        worldRejection(world.evaluate(query));
    if (authorityReason != CommandRejectReason::None) {
        rejectStepCommand(
            transaction, command, playerValue.crew, authorityReason);
        return;
    }

    CargoState& cargoValue = transaction.cargo[cargoPosition];
    const SkiffId sourceSkiff = cargoValue.towingSkiff;
    const uint32_t sourceSkiffGeneration =
        cargoValue.towingSkiffGeneration;
    const AttachmentId sourceAttachmentId =
        cargoValue.towAttachmentId;
    const uint32_t sourceAttachmentGeneration =
        cargoValue.towAttachmentGeneration;
    ++cargoValue.revision;
    MatchWorldIntentType intentType = MatchWorldIntentType::AttachTow;
    MatchEventType eventType = MatchEventType::CargoTowAttached;
    SkiffId targetSkiff = command.skiffId;
    uint32_t targetSkiffGeneration = command.skiffGeneration;

    switch (command.type) {
        case MatchCommandType::TowCargo:
            cargoValue.disposition = CargoDisposition::Towed;
            cargoValue.owner = playerValue.crew;
            cargoValue.towingSkiff = command.skiffId;
            cargoValue.towingSkiffGeneration =
                command.skiffGeneration;
            cargoValue.towAttachmentId = 0u;
            cargoValue.towAttachmentGeneration = 0u;
            break;
        case MatchCommandType::CutTow:
            intentType = MatchWorldIntentType::CutTow;
            eventType = MatchEventType::CargoTowCut;
            targetSkiff = 0u;
            targetSkiffGeneration = 0u;
            cargoValue.disposition = CargoDisposition::Free;
            cargoValue.owner = CrewId::None;
            cargoValue.towingSkiff = 0u;
            cargoValue.towingSkiffGeneration = 0u;
            cargoValue.towAttachmentId = 0u;
            cargoValue.towAttachmentGeneration = 0u;
            break;
        case MatchCommandType::StealCargo:
            intentType = MatchWorldIntentType::TransferTow;
            eventType = MatchEventType::CargoStolen;
            cargoValue.disposition = CargoDisposition::Towed;
            cargoValue.owner = playerValue.crew;
            cargoValue.towingSkiff = command.skiffId;
            cargoValue.towingSkiffGeneration =
                command.skiffGeneration;
            cargoValue.towAttachmentId = 0u;
            cargoValue.towAttachmentGeneration = 0u;
            break;
        case MatchCommandType::BankCargo:
            intentType = MatchWorldIntentType::BankCargo;
            eventType = MatchEventType::CargoBanked;
            targetSkiff = 0u;
            targetSkiffGeneration = 0u;
            cargoValue.disposition = CargoDisposition::Banked;
            cargoValue.owner = playerValue.crew;
            cargoValue.towingSkiff = 0u;
            cargoValue.towingSkiffGeneration = 0u;
            cargoValue.towAttachmentId = 0u;
            cargoValue.towAttachmentGeneration = 0u;
            break;
    }

    appendTentativeIntent(MatchWorldIntent{
        .type = intentType,
        .source = MatchWorldIntentSource::PlayerCommand,
        .matchId = config_.matchId,
        .worldId = config_.worldId,
        .worldEpoch = config_.worldEpoch,
        .authorityEpoch = config_.authorityEpoch,
        .applicationTick = transaction.tick,
        .physicsEvidenceTick = command.physicsEvidenceTick,
        .sourceSequence = command.sequence,
        .playerId = command.playerId,
        .connectionId = command.connectionId,
        .connectionGeneration = command.connectionGeneration,
        .crew = playerValue.crew,
        .actorSkiff = command.skiffId,
        .actorSkiffGeneration = command.skiffGeneration,
        .cargoId = command.cargoId,
        .cargoGeneration = command.cargoGeneration,
        .priorCargoRevision = prior.revision,
        .resultingCargoRevision = cargoValue.revision,
        .sourceSkiff = sourceSkiff,
        .sourceSkiffGeneration = sourceSkiffGeneration,
        .targetSkiff = targetSkiff,
        .targetSkiffGeneration = targetSkiffGeneration,
        .sourceAttachmentId = sourceAttachmentId,
        .sourceAttachmentGeneration = sourceAttachmentGeneration,
    });
    appendTentativeEvent(transaction, MatchEvent{
        .sourcePhysicsTick = command.physicsEvidenceTick,
        .commandTick = command.tick,
        .sourceSequence = command.sequence,
        .source = MatchEventSource::PlayerCommand,
        .commandType = command.type,
        .type = eventType,
        .playerId = command.playerId,
        .seat = playerValue.seat,
        .connectionId = command.connectionId,
        .connectionGeneration = command.connectionGeneration,
        .crew = playerValue.crew,
        .cargoId = command.cargoId,
        .cargoGeneration = command.cargoGeneration,
        .observedCargoRevision = command.observedCargoRevision,
        .cargoRevision = cargoValue.revision,
        .sourceSkiff = sourceSkiff,
        .sourceSkiffGeneration = sourceSkiffGeneration,
        .targetSkiff = targetSkiff,
        .targetSkiffGeneration = targetSkiffGeneration,
        .attachmentId = sourceAttachmentId,
        .attachmentGeneration = sourceAttachmentGeneration,
        .sourceAttachmentId = sourceAttachmentId,
        .sourceAttachmentGeneration = sourceAttachmentGeneration,
    });

    if (command.type == MatchCommandType::BankCargo) {
        CrewState& crewValue =
            transaction.crews[enumWord(playerValue.crew) - 1u];
        crewValue.score += config_.bankScore;
        appendTentativeEvent(transaction, MatchEvent{
            .sourcePhysicsTick = command.physicsEvidenceTick,
            .commandTick = command.tick,
            .sourceSequence = command.sequence,
            .source = MatchEventSource::PlayerCommand,
            .commandType = command.type,
            .type = MatchEventType::ScoreChanged,
            .playerId = command.playerId,
            .seat = playerValue.seat,
            .connectionId = command.connectionId,
            .connectionGeneration = command.connectionGeneration,
            .crew = playerValue.crew,
            .cargoId = command.cargoId,
            .cargoGeneration = command.cargoGeneration,
            .observedCargoRevision =
                command.observedCargoRevision,
            .cargoRevision = cargoValue.revision,
        });
        if (transaction.phase == MatchPhase::Overtime) {
            finishMatch(transaction);
        }
    }

    cargoMutated[cargoPosition] = true;
    ++transaction.appliedCommands;
}

bool WreckwaterMatch::validateAndApplyAcks(
    StepTransaction& transaction) noexcept {
    for (size_t index = 0u; index < tentativeIntentCount_; ++index) {
        MatchWorldIntent& intent = tentativeIntents_[index];
        const MatchWorldIntentAck& ack = tentativeAcks_[index];
        if (ack.schemaVersion != intent.schemaVersion
            || ack.type != intent.type
            || ack.source != intent.source
            || ack.matchId != intent.matchId
            || ack.worldId != intent.worldId
            || ack.worldEpoch != intent.worldEpoch
            || ack.authorityEpoch != intent.authorityEpoch
            || ack.applicationTick != intent.applicationTick
            || ack.physicsEvidenceTick != intent.physicsEvidenceTick
            || ack.sourceStreamId != intent.sourceStreamId
            || ack.sourceSequence != intent.sourceSequence
            || ack.playerId != intent.playerId
            || ack.connectionId != intent.connectionId
            || ack.connectionGeneration
                != intent.connectionGeneration
            || ack.crew != intent.crew
            || ack.actorSkiff != intent.actorSkiff
            || ack.actorSkiffGeneration
                != intent.actorSkiffGeneration
            || ack.cargoId != intent.cargoId
            || ack.cargoGeneration != intent.cargoGeneration
            || ack.priorCargoRevision
                != intent.priorCargoRevision
            || ack.resultingCargoRevision
                != intent.resultingCargoRevision
            || ack.sourceSkiff != intent.sourceSkiff
            || ack.sourceSkiffGeneration
                != intent.sourceSkiffGeneration
            || ack.targetSkiff != intent.targetSkiff
            || ack.targetSkiffGeneration
                != intent.targetSkiffGeneration
            || ack.sourceAttachmentId
                != intent.sourceAttachmentId
            || ack.sourceAttachmentGeneration
                != intent.sourceAttachmentGeneration) {
            return false;
        }
        const bool createsAttachment =
            intent.type == MatchWorldIntentType::AttachTow
            || intent.type == MatchWorldIntentType::TransferTow;
        if (createsAttachment
            ? (ack.newAttachmentId == 0u
                || ack.newAttachmentGeneration == 0u)
            : (ack.newAttachmentId != 0u
                || ack.newAttachmentGeneration != 0u)) {
            return false;
        }
        const size_t cargoPosition = cargoIndex(intent.cargoId);
        if (cargoPosition == kInvalidIndex) return false;
        if (createsAttachment) {
            CargoState& cargoValue = transaction.cargo[cargoPosition];
            cargoValue.towAttachmentId = ack.newAttachmentId;
            cargoValue.towAttachmentGeneration =
                ack.newAttachmentGeneration;
            intent.resultingAttachmentId = ack.newAttachmentId;
            intent.resultingAttachmentGeneration =
                ack.newAttachmentGeneration;
            for (MatchEvent& event : tentativeEvents_) {
                if (event.sourceSequence == intent.sourceSequence
                    && event.playerId == intent.playerId
                    && event.cargoId == intent.cargoId
                    && (event.type == MatchEventType::CargoTowAttached
                        || event.type == MatchEventType::CargoStolen)) {
                    event.attachmentId = ack.newAttachmentId;
                    event.attachmentGeneration =
                        ack.newAttachmentGeneration;
                    event.resultingAttachmentId =
                        ack.newAttachmentId;
                    event.resultingAttachmentGeneration =
                        ack.newAttachmentGeneration;
                    break;
                }
            }
        }
    }
    return true;
}

bool WreckwaterMatch::validCommittedInvariants(
    const StepTransaction& transaction) const noexcept {
    for (const CargoState& cargoValue : transaction.cargo) {
        if (cargoValue.disposition == CargoDisposition::Towed) {
            const auto skiffIt = std::find_if(
                transaction.skiffs.begin(), transaction.skiffs.end(),
                [&cargoValue](const SkiffState& skiffValue) {
                    return skiffValue.skiffId
                        == cargoValue.towingSkiff;
                });
            if (!validCrew(cargoValue.owner)
                || skiffIt == transaction.skiffs.end()
                || skiffIt->owner != cargoValue.owner
                || skiffIt->generation
                    != cargoValue.towingSkiffGeneration
                || skiffIt->disposition != SkiffDisposition::Active
                || cargoValue.towAttachmentId == 0u
                || cargoValue.towAttachmentGeneration == 0u) {
                return false;
            }
        } else {
            if (cargoValue.towingSkiff != 0u
                || cargoValue.towingSkiffGeneration != 0u
                || cargoValue.towAttachmentId != 0u
                || cargoValue.towAttachmentGeneration != 0u) {
                return false;
            }
            if ((cargoValue.disposition == CargoDisposition::Free
                    || cargoValue.disposition
                        == CargoDisposition::Lost)
                && cargoValue.owner != CrewId::None) {
                return false;
            }
            if (cargoValue.disposition == CargoDisposition::Banked
                && !validCrew(cargoValue.owner)) {
                return false;
            }
        }
    }
    return true;
}

void WreckwaterMatch::enterFault(
    MatchFaultReason reason, const MatchWorldIntent* intent) {
    if (faulted()) return;
    fault_ = reason;
    updateStateHash();
    if (!hasEventCapacity(1u)) {
        incrementSaturated(telemetry_.eventCapacityFailures);
        return;
    }
    MatchEvent event{
        .type = MatchEventType::AuthorityFault,
        .faultReason = reason,
    };
    if (intent != nullptr) {
        event.sourcePhysicsTick = intent->physicsEvidenceTick;
        event.commandTick = intent->applicationTick;
        event.sourceSequence = intent->sourceSequence;
        event.source =
            intent->source == MatchWorldIntentSource::PlayerCommand
            ? MatchEventSource::PlayerCommand
            : MatchEventSource::AuthoritativeWorldEvent;
        event.sourceStreamId = intent->sourceStreamId;
        switch (intent->type) {
            case MatchWorldIntentType::AttachTow:
                event.commandType = MatchCommandType::TowCargo;
                break;
            case MatchWorldIntentType::CutTow:
                event.commandType = MatchCommandType::CutTow;
                break;
            case MatchWorldIntentType::TransferTow:
                event.commandType = MatchCommandType::StealCargo;
                break;
            case MatchWorldIntentType::BankCargo:
                event.commandType = MatchCommandType::BankCargo;
                break;
        }
        event.playerId = intent->playerId;
        event.connectionId = intent->connectionId;
        event.connectionGeneration = intent->connectionGeneration;
        event.crew = intent->crew;
        event.cargoId = intent->cargoId;
        event.cargoGeneration = intent->cargoGeneration;
        event.observedCargoRevision =
            intent->priorCargoRevision;
        event.cargoRevision = intent->resultingCargoRevision;
        event.sourceSkiff = intent->sourceSkiff;
        event.sourceSkiffGeneration =
            intent->sourceSkiffGeneration;
        event.targetSkiff = intent->targetSkiff;
        event.targetSkiffGeneration =
            intent->targetSkiffGeneration;
        event.attachmentId = intent->sourceAttachmentId;
        event.attachmentGeneration =
            intent->sourceAttachmentGeneration;
        event.sourceAttachmentId =
            intent->sourceAttachmentId;
        event.sourceAttachmentGeneration =
            intent->sourceAttachmentGeneration;
        event.resultingAttachmentId =
            intent->resultingAttachmentId;
        event.resultingAttachmentGeneration =
            intent->resultingAttachmentGeneration;
    }
    publishEvent(event);
}

void WreckwaterMatch::transitionPhase(
    StepTransaction& transaction, MatchPhase phaseValue) {
    transaction.phase = phaseValue;
    appendTentativeEvent(
        transaction, MatchEvent{.type = MatchEventType::PhaseChanged});
}

void WreckwaterMatch::finishMatch(StepTransaction& transaction) {
    if (transaction.phase == MatchPhase::Finished) return;
    if (transaction.crews[0].score == transaction.crews[1].score) {
        transaction.outcome = {
            MatchOutcomeType::Tie, CrewId::None};
    } else {
        transaction.outcome = {
            MatchOutcomeType::CrewVictory,
            transaction.crews[0].score > transaction.crews[1].score
                ? CrewId::CrewOne : CrewId::CrewTwo,
        };
    }
    transitionPhase(transaction, MatchPhase::Finished);
    appendTentativeEvent(transaction, MatchEvent{
        .type = MatchEventType::MatchFinished,
        .crew = transaction.outcome.winner,
    });
}

void WreckwaterMatch::clearTentative() noexcept {
    tentativeEvents_.clear();
    tentativeIntents_ = {};
    tentativeAcks_ = {};
    tentativeIntentCount_ = 0u;
}

void WreckwaterMatch::clearPublishedIntents() noexcept {
    publishedIntents_ = {};
    publishedIntentCount_ = 0u;
}

bool WreckwaterMatch::step(IWreckwaterWorldAuthority& world) {
    clearPublishedIntents();
    clearTentative();
    if (!initialized_ || faulted() || !started_
        || phase_ == MatchPhase::Finished
        || currentTick_ >= overtimeEndTick_) {
        return false;
    }

    const uint64_t nextTick = currentTick_ + 1u;
    const auto dueWorldEnd = std::upper_bound(
        pendingWorldEvents_.begin(), pendingWorldEvents_.end(), nextTick,
        [](uint64_t tick, const AuthoritativeWorldEvent& event) {
            return tick < event.applicationTick;
        });
    const size_t dueWorldCount = static_cast<size_t>(
        std::distance(pendingWorldEvents_.begin(), dueWorldEnd));
    const auto dueCommandEnd = std::upper_bound(
        pendingCommands_.begin(), pendingCommands_.end(), nextTick,
        [](uint64_t tick, const MatchCommand& command) {
            return tick < command.tick;
        });
    const size_t dueCommandCount = static_cast<size_t>(
        std::distance(pendingCommands_.begin(), dueCommandEnd));
    if (!hasEventCapacity(
            pendingCommands_.size() + pendingWorldEvents_.size() + 8u)) {
        incrementSaturated(telemetry_.eventCapacityFailures);
        clearTentative();
        return false;
    }

    StepTransaction transaction{
        .tick = nextTick,
        .crews = crews_,
        .skiffs = skiffs_,
        .cargo = cargo_,
        .phase = phase_,
        .outcome = outcome_,
    };
    std::array<bool, kWreckwaterCargoCount> cargoMutated{};

    for (size_t index = 0u; index < dueWorldCount; ++index) {
        const AuthoritativeWorldEvent& event = pendingWorldEvents_[index];
        if (event.applicationTick != nextTick) {
            rejectStepWorldEvent(
                transaction, event,
                WorldEventRejectReason::TickOutOfWindow);
            continue;
        }
        const std::array<uint32_t, kWreckwaterCargoCount> priorRevisions{
            transaction.cargo[0].revision,
        };
        applyWorldEvent(transaction, event);
        for (size_t cargoPosition = 0u;
             cargoPosition < transaction.cargo.size();
             ++cargoPosition) {
            if (transaction.cargo[cargoPosition].revision
                != priorRevisions[cargoPosition]) {
                cargoMutated[cargoPosition] = true;
            }
        }
    }

    for (size_t index = 0u; index < dueCommandCount; ++index) {
        const MatchCommand& command = pendingCommands_[index];
        const size_t playerPosition = playerIndex(command.playerId);
        const size_t cargoPosition = cargoIndex(command.cargoId);
        if (command.tick != nextTick
            || playerPosition == kInvalidIndex
            || cargoPosition == kInvalidIndex) {
            const CrewId crew = playerPosition == kInvalidIndex
                ? CrewId::None : players_[playerPosition].crew;
            rejectStepCommand(
                transaction, command, crew,
                command.tick != nextTick
                    ? CommandRejectReason::TickOutOfWindow
                    : CommandRejectReason::Malformed);
            continue;
        }
        applyCommand(
            transaction, command, playerPosition, cargoPosition,
            world, cargoMutated);
    }

    if (transaction.phase == MatchPhase::Warmup
        && nextTick >= warmupEndTick_) {
        transitionPhase(transaction, MatchPhase::Live);
    } else if (transaction.phase == MatchPhase::Live
        && nextTick >= liveEndTick_) {
        if (transaction.crews[0].score
            == transaction.crews[1].score) {
            transitionPhase(transaction, MatchPhase::Overtime);
        } else {
            finishMatch(transaction);
        }
    } else if (transaction.phase == MatchPhase::Overtime
        && nextTick >= overtimeEndTick_) {
        finishMatch(transaction);
    }

    if (transaction.phase == MatchPhase::Finished) {
        for (size_t index = dueCommandCount;
             index < pendingCommands_.size(); ++index) {
            const MatchCommand& command = pendingCommands_[index];
            const size_t playerPosition = playerIndex(command.playerId);
            rejectStepCommand(
                transaction, command,
                playerPosition == kInvalidIndex
                    ? CrewId::None : players_[playerPosition].crew,
                CommandRejectReason::MatchNotActive);
        }
        for (size_t index = dueWorldCount;
             index < pendingWorldEvents_.size(); ++index) {
            rejectStepWorldEvent(
                transaction, pendingWorldEvents_[index],
                WorldEventRejectReason::MatchNotActive);
        }
    }

    if (tentativeIntentCount_ != 0u) {
        const auto intents = std::span<const MatchWorldIntent>(
            tentativeIntents_.data(), tentativeIntentCount_);
        auto acknowledgements = std::span<MatchWorldIntentAck>(
            tentativeAcks_.data(), tentativeIntentCount_);
        if (!world.applyAtomically(intents, acknowledgements)) {
            incrementSaturated(telemetry_.transactionAborts);
            clearTentative();
            return false;
        }
        if (!validateAndApplyAcks(transaction)) {
            incrementSaturated(telemetry_.transactionAborts);
            incrementSaturated(telemetry_.invalidWorldAcks);
            enterFault(
                MatchFaultReason::InvalidWorldAcknowledgement,
                &tentativeIntents_[0]);
            clearTentative();
            return false;
        }
    }
    if (!validCommittedInvariants(transaction)) {
        incrementSaturated(telemetry_.transactionAborts);
        enterFault(
            MatchFaultReason::CommittedInvariantViolation,
            tentativeIntentCount_ == 0u
                ? nullptr : &tentativeIntents_[0]);
        clearTentative();
        return false;
    }

    currentTick_ = transaction.tick;
    crews_ = transaction.crews;
    skiffs_ = transaction.skiffs;
    cargo_ = transaction.cargo;
    phase_ = transaction.phase;
    outcome_ = transaction.outcome;
    if (phase_ == MatchPhase::Finished) {
        pendingCommands_.clear();
        pendingWorldEvents_.clear();
    } else {
        pendingCommands_.erase(
            pendingCommands_.begin(),
            pendingCommands_.begin()
                + static_cast<std::ptrdiff_t>(dueCommandCount));
        pendingWorldEvents_.erase(
            pendingWorldEvents_.begin(),
            pendingWorldEvents_.begin()
                + static_cast<std::ptrdiff_t>(dueWorldCount));
    }
    updateStateHash();
    for (MatchEvent event : tentativeEvents_) publishEvent(event);
    for (size_t index = 0u; index < tentativeIntentCount_; ++index) {
        publishedIntents_[index] = tentativeIntents_[index];
    }
    publishedIntentCount_ = tentativeIntentCount_;
    commitTelemetry(transaction);
    clearTentative();
    return true;
}

void WreckwaterMatch::commitTelemetry(
    const StepTransaction& transaction) noexcept {
    addSaturated(
        telemetry_.appliedCommands, transaction.appliedCommands);
    addSaturated(
        telemetry_.rejectedCommands, transaction.rejectedCommands);
    addSaturated(
        telemetry_.conflictRejections,
        transaction.conflictRejections);
    addSaturated(
        telemetry_.worldRejections, transaction.worldRejections);
    addSaturated(
        telemetry_.appliedWorldEvents,
        transaction.appliedWorldEvents);
    addSaturated(
        telemetry_.rejectedWorldEvents,
        transaction.rejectedWorldEvents);
}

bool WreckwaterMatch::hasEventCapacity(size_t count) const noexcept {
    return count <= config_.eventCapacity
        && events_.size() <= config_.eventCapacity - count;
}

void WreckwaterMatch::publishEvent(MatchEvent event) {
    event.schemaVersion = kWreckwaterMatchSchemaVersion;
    event.eventSequence = nextEventSequence_;
    ++nextEventSequence_;
    event.matchId = config_.matchId;
    event.worldId = config_.worldId;
    event.worldEpoch = config_.worldEpoch;
    event.authorityEpoch = config_.authorityEpoch;
    event.tick = currentTick_;
    event.phase = phase_;
    event.outcome = outcome_.type;
    event.faultReason = fault_;
    event.crewOneScore = crews_[0].score;
    event.crewTwoScore = crews_[1].score;
    event.stateHash = stateHash_;
    eventStreamRollingHash_ =
        wreckwaterAppendMatchEventStreamRollingHash(
            eventStreamRollingHash_, event);
    events_.push_back(event);
    telemetry_.eventsHighWater = std::max(
        telemetry_.eventsHighWater,
        static_cast<uint32_t>(events_.size()));
}

uint32_t WreckwaterMatch::configurationHash() const noexcept {
    uint32_t hash = hashWord(
        kHashOffset, kWreckwaterMatchSchemaVersion);
    hash = hashU64(hash, config_.matchId);
    hash = hashU64(hash, config_.worldId);
    hash = hashWord(hash, config_.worldEpoch);
    hash = hashWord(hash, config_.authorityEpoch);
    hash = hashWord(hash, config_.worldEventStreamId);
    hash = hashWord(hash, config_.tickRateHz);
    hash = hashWord(hash, config_.warmupTicks);
    hash = hashWord(hash, config_.liveTicks);
    hash = hashWord(hash, config_.overtimeTicks);
    hash = hashWord(hash, config_.inputFutureWindow);
    hash = hashWord(hash, config_.worldEventFutureWindow);
    hash = hashWord(hash, config_.maximumWorldEventLagTicks);
    hash = hashWord(hash, config_.maximumActionEvidenceLagTicks);
    hash = hashWord(hash, config_.maximumCommandsPerPlayerTick);
    hash = hashWord(hash, config_.maximumWorldEventsPerTick);
    hash = hashWord(hash, config_.maximumSequenceAdvance);
    hash = hashWord(
        hash, config_.maximumWorldEventSequenceAdvance);
    hash = hashWord(
        hash, config_.maximumConnectionEventsPerPlayer);
    hash = hashWord(hash, config_.pendingCommandCapacity);
    hash = hashWord(hash, config_.pendingWorldEventCapacity);
    hash = hashWord(hash, config_.eventCapacity);
    hash = hashWord(hash, config_.bankScore);
    hash = hashWord(hash, config_.reactorCargoId);
    hash = hashWord(hash, config_.reactorCargoGeneration);
    hash = hashWord(hash, config_.crewOneSkiffId);
    return hashWord(hash, config_.crewTwoSkiffId);
}

void WreckwaterMatch::updateStateHash() noexcept {
    uint32_t hash = hashWord(kHashOffset, configurationHash());
    hash = hashU64(hash, currentTick_);
    hash = hashWord(hash, initialized_ ? 1u : 0u);
    hash = hashWord(hash, started_ ? 1u : 0u);
    hash = hashWord(hash, enumWord(phase_));
    hash = hashWord(hash, enumWord(outcome_.type));
    hash = hashWord(hash, enumWord(outcome_.winner));
    hash = hashWord(hash, enumWord(fault_));
    for (const PlayerState& value : players_) {
        hash = hashWord(hash, value.occupied ? 1u : 0u);
        hash = hashU64(hash, value.playerId);
        hash = hashWord(hash, enumWord(value.crew));
        hash = hashWord(hash, value.seat);
        hash = hashU64(hash, value.connectionId);
        hash = hashWord(hash, value.connectionGeneration);
        hash = hashWord(hash, value.connectionEventCount);
        hash = hashWord(hash, value.connected ? 1u : 0u);
        hash = hashWord(hash, value.everConnected ? 1u : 0u);
    }
    for (const SequenceWindow& window : commandWindows_) {
        hash = hashU64(hash, window.latest);
        hash = hashU64(hash, window.bits);
        hash = hashWord(hash, window.initialized ? 1u : 0u);
    }
    hash = hashU64(hash, worldEventWindow_.latest);
    hash = hashU64(hash, worldEventWindow_.bits);
    hash = hashWord(
        hash, worldEventWindow_.initialized ? 1u : 0u);
    for (const CrewState& value : crews_) {
        hash = hashWord(hash, enumWord(value.crew));
        hash = hashWord(hash, value.skiffId);
        hash = hashWord(hash, value.score);
    }
    for (const SkiffState& value : skiffs_) {
        hash = hashWord(hash, value.skiffId);
        hash = hashWord(hash, enumWord(value.owner));
        hash = hashWord(hash, value.generation);
        hash = hashWord(hash, enumWord(value.disposition));
    }
    for (const CargoState& value : cargo_) {
        hash = hashWord(hash, value.cargoId);
        hash = hashWord(hash, value.generation);
        hash = hashWord(hash, value.revision);
        hash = hashWord(hash, enumWord(value.disposition));
        hash = hashWord(hash, enumWord(value.owner));
        hash = hashWord(hash, value.towingSkiff);
        hash = hashWord(hash, value.towingSkiffGeneration);
        hash = hashU64(hash, value.towAttachmentId);
        hash = hashWord(hash, value.towAttachmentGeneration);
    }
    hash = hashWord(
        hash, static_cast<uint32_t>(pendingCommands_.size()));
    for (const MatchCommand& command : pendingCommands_) {
        hash = hashCommand(hash, command);
    }
    hash = hashWord(
        hash, static_cast<uint32_t>(pendingWorldEvents_.size()));
    for (const AuthoritativeWorldEvent& event : pendingWorldEvents_) {
        hash = hashWorldEvent(hash, event);
    }
    stateHash_ = hash;
}

uint32_t WreckwaterMatch::eventStreamHash() const noexcept {
    return wreckwaterFinalizeMatchEventStreamHash(
        eventStreamRollingHash_, configurationHash(), stateHash_,
        nextEventSequence_, static_cast<uint32_t>(events_.size()));
}

uint32_t WreckwaterMatch::remainingPhaseTicks() const noexcept {
    if (!started_ || faulted()
        || phase_ == MatchPhase::Finished) {
        return 0u;
    }
    uint64_t endTick = warmupEndTick_;
    if (phase_ == MatchPhase::Live) endTick = liveEndTick_;
    if (phase_ == MatchPhase::Overtime) endTick = overtimeEndTick_;
    if (currentTick_ >= endTick) return 0u;
    return static_cast<uint32_t>(endTick - currentTick_);
}

uint32_t WreckwaterMatch::score(CrewId crew) const noexcept {
    if (!validCrew(crew)) return 0u;
    return crews_[enumWord(crew) - 1u].score;
}

const PlayerState* WreckwaterMatch::player(
    PlayerId playerIdValue) const noexcept {
    const size_t index = playerIndex(playerIdValue);
    return index == kInvalidIndex ? nullptr : &players_[index];
}

const CargoState* WreckwaterMatch::cargo(
    CargoId cargoIdValue) const noexcept {
    const size_t index = cargoIndex(cargoIdValue);
    return index == kInvalidIndex ? nullptr : &cargo_[index];
}

const SkiffState* WreckwaterMatch::skiff(
    SkiffId skiffIdValue) const noexcept {
    const size_t index = skiffIndex(skiffIdValue);
    return index == kInvalidIndex ? nullptr : &skiffs_[index];
}

std::optional<uint64_t> WreckwaterMatch::nextCommandSequence(
    PlayerId playerIdValue) const noexcept {
    const size_t index = playerIndex(playerIdValue);
    if (index == kInvalidIndex) return std::nullopt;
    const SequenceWindow& window = commandWindows_[index];
    if (!window.initialized) return 1u;
    if (window.latest == std::numeric_limits<uint64_t>::max()) {
        return std::nullopt;
    }
    return window.latest + 1u;
}

std::optional<uint64_t>
WreckwaterMatch::nextWorldEventSequence() const noexcept {
    if (!initialized_) return std::nullopt;
    if (!worldEventWindow_.initialized) return 1u;
    if (worldEventWindow_.latest
        == std::numeric_limits<uint64_t>::max()) {
        return std::nullopt;
    }
    return worldEventWindow_.latest + 1u;
}

WreckwaterStorageState
WreckwaterMatch::storageState() const noexcept {
    return {
        .pendingCommands = pendingCommands_.data(),
        .pendingWorldEvents = pendingWorldEvents_.data(),
        .purgedCommandsScratch = purgedCommandsScratch_.data(),
        .tentativeEvents = tentativeEvents_.data(),
        .recordedEvents = events_.data(),
        .pendingCommandCapacity = pendingCommands_.capacity(),
        .pendingWorldEventCapacity = pendingWorldEvents_.capacity(),
        .purgedCommandCapacity = purgedCommandsScratch_.capacity(),
        .tentativeEventCapacity = tentativeEvents_.capacity(),
        .recordedEventCapacity = events_.capacity(),
    };
}

} // namespace voxy::game
