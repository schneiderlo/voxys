#include "game/wreckwater_replay_playback.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace voxy::game {
namespace {

constexpr std::array<std::byte, 8> kInitialHashTag{
    std::byte{'W'}, std::byte{'R'}, std::byte{'E'}, std::byte{'P'},
    std::byte{'L'}, std::byte{'A'}, std::byte{'Y'}, std::byte{'1'}};
constexpr std::array<std::byte, 8> kFinalHashTag{
    std::byte{'W'}, std::byte{'R'}, std::byte{'F'}, std::byte{'I'},
    std::byte{'N'}, std::byte{'A'}, std::byte{'L'}, std::byte{'1'}};
constexpr uint64_t kFnv64Offset = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnv64Prime = 1'099'511'628'211ull;
constexpr double kSectorSizeMeters = 256.0;

template <typename Enum>
[[nodiscard]] constexpr uint32_t enumWord(Enum value) noexcept {
    static_assert(std::is_enum_v<Enum>);
    return static_cast<uint32_t>(value);
}

class HashWriter {
public:
    void raw(std::span<const std::byte> bytes) noexcept {
        for (const std::byte value : bytes) {
            hash_ =
                (hash_ ^ std::to_integer<uint64_t>(value)) * kFnv64Prime;
        }
    }

    void u32(uint32_t value) noexcept {
        for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
            const std::byte byte{
                static_cast<uint8_t>(value >> shift)};
            raw(std::span<const std::byte>(&byte, 1u));
        }
    }

    void u64(uint64_t value) noexcept {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }

    [[nodiscard]] uint64_t value() const noexcept { return hash_; }

private:
    uint64_t hash_ = kFnv64Offset;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool u32(uint32_t& value) noexcept {
        if (remaining() < sizeof(uint32_t)) return false;
        value = 0u;
        for (uint32_t byte = 0u; byte < 4u; ++byte) {
            value |= std::to_integer<uint32_t>(
                         bytes_[offset_ + byte])
                << (byte * 8u);
        }
        offset_ += sizeof(uint32_t);
        return true;
    }

    [[nodiscard]] bool u64(uint64_t& value) noexcept {
        uint32_t low = 0u;
        uint32_t high = 0u;
        if (!u32(low) || !u32(high)) return false;
        value = uint64_t{low} | (uint64_t{high} << 32u);
        return true;
    }

    [[nodiscard]] size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::byte> bytes_;
    size_t offset_ = 0u;
};

[[nodiscard]] bool validPlaybackConfig(
    const WreckwaterReplayPlaybackConfig& config) noexcept {
    return config.readLimits.maximumRecords != 0u
        && config.readLimits.maximumRecords
            <= kWreckwaterReplayMaximumRecords
        && config.readLimits.maximumPayloadBytes != 0u
        && config.readLimits.maximumPayloadBytes
            <= kWreckwaterReplayMaximumPayloadBytes
        && config.readLimits.maximumArchiveBytes
            >= kWreckwaterReplayArchiveHeaderBytes
        && config.readLimits.maximumArchiveBytes
            <= kWreckwaterReplayMaximumArchiveBytes
        && config.maximumSnapshots != 0u
        && config.maximumSnapshots
            <= config.readLimits.maximumRecords
        && config.maximumEvents != 0u
        && config.maximumEvents <= config.readLimits.maximumRecords
        && config.maximumEntities != 0u
        && config.maximumEntities
            <= network::kWreckwaterMaximumSnapshotEntities
        && config.maximumTrackedLifetimes != 0u
        && config.maximumTrackedLifetimes
            <= kWreckwaterReplayMaximumRecords
        && config.cameraInterestTicks != 0u
        && config.cameraInterestTicks
            <= network::kWreckwaterMaximumApplicationTick
        && config.maximumEventsPerTick != 0u
        && config.maximumEventsPerTick <= config.maximumEvents
        && config.maximumCameraEvents != 0u
        && config.maximumCameraEvents <= config.maximumEvents;
}

[[nodiscard]] bool validReplayConfig(
    const WreckwaterReplayConfig& config) noexcept {
    return config.sessionId != 0u
        && config.matchId != 0u
        && config.worldId != 0u
        && config.worldEpoch != 0u
        && config.authorityEpoch != 0u
        && config.tickRateHz == kWreckwaterReplayTickRateHz
        && config.contentHash != 0u
        && config.matchConfigurationHash != 0u
        && config.maximumRecords != 0u
        && config.maximumRecords <= kWreckwaterReplayMaximumRecords
        && config.maximumPayloadBytes != 0u
        && config.maximumPayloadBytes
            <= kWreckwaterReplayMaximumPayloadBytes;
}

[[nodiscard]] bool validRecordType(
    WreckwaterReplayRecordType type) noexcept {
    return type == WreckwaterReplayRecordType::MatchEvent
        || type == WreckwaterReplayRecordType::CertifiedSnapshot;
}

[[nodiscard]] bool validMatchEventSource(
    MatchEventSource source) noexcept {
    switch (source) {
        case MatchEventSource::MatchSystem:
        case MatchEventSource::PlayerCommand:
        case MatchEventSource::AuthoritativeWorldEvent:
            return true;
    }
    return false;
}

[[nodiscard]] bool validMatchCommandType(
    MatchCommandType type) noexcept {
    switch (type) {
        case MatchCommandType::TowCargo:
        case MatchCommandType::CutTow:
        case MatchCommandType::StealCargo:
        case MatchCommandType::BankCargo:
            return true;
    }
    return false;
}

[[nodiscard]] bool validMatchEventType(
    MatchEventType type) noexcept {
    return enumWord(type) >= enumWord(MatchEventType::PlayerRegistered)
        && enumWord(type) <= enumWord(MatchEventType::AuthorityFault);
}

[[nodiscard]] bool validWorldEventType(
    AuthoritativeWorldEventType type) noexcept {
    return enumWord(type)
            >= enumWord(AuthoritativeWorldEventType::AttachmentBroken)
        && enumWord(type)
            <= enumWord(AuthoritativeWorldEventType::SkiffRespawned);
}

[[nodiscard]] bool validPhase(MatchPhase phase) noexcept {
    return enumWord(phase) >= enumWord(MatchPhase::Warmup)
        && enumWord(phase) <= enumWord(MatchPhase::Finished);
}

[[nodiscard]] bool validOutcome(MatchOutcomeType outcome) noexcept {
    return enumWord(outcome)
            >= enumWord(MatchOutcomeType::Undecided)
        && enumWord(outcome) <= enumWord(MatchOutcomeType::Tie);
}

[[nodiscard]] bool validFault(MatchFaultReason reason) noexcept {
    return enumWord(reason) >= enumWord(MatchFaultReason::None)
        && enumWord(reason)
            <= enumWord(MatchFaultReason::CommittedInvariantViolation);
}

[[nodiscard]] bool validCommandRejection(
    CommandRejectReason reason) noexcept {
    return enumWord(reason) >= enumWord(CommandRejectReason::None)
        && enumWord(reason)
            <= enumWord(CommandRejectReason::AuthorityFaulted);
}

[[nodiscard]] bool validWorldEventRejection(
    WorldEventRejectReason reason) noexcept {
    return enumWord(reason) >= enumWord(WorldEventRejectReason::None)
        && enumWord(reason)
            <= enumWord(WorldEventRejectReason::AuthorityFaulted);
}

[[nodiscard]] bool validCrew(CrewId crew) noexcept {
    return crew == CrewId::None
        || crew == CrewId::CrewOne
        || crew == CrewId::CrewTwo;
}

[[nodiscard]] bool validEvent(
    const MatchEvent& event,
    const WreckwaterReplayConfig& config) noexcept {
    return event.schemaVersion == kWreckwaterMatchSchemaVersion
        && event.eventSequence != 0u
        && event.matchId == config.matchId
        && event.worldId == config.worldId
        && event.worldEpoch == config.worldEpoch
        && event.authorityEpoch == config.authorityEpoch
        && event.tick <= network::kWreckwaterMaximumApplicationTick
        && event.sourcePhysicsTick
            <= network::kWreckwaterMaximumApplicationTick
        && event.commandTick
            <= network::kWreckwaterMaximumApplicationTick
        && event.sourcePhysicsTick <= event.tick
        && validMatchEventSource(event.source)
        && validMatchCommandType(event.commandType)
        && validMatchEventType(event.type)
        && validWorldEventType(event.worldEventType)
        && validPhase(event.phase)
        && validOutcome(event.outcome)
        && validFault(event.faultReason)
        && validCommandRejection(event.commandRejection)
        && validWorldEventRejection(event.worldEventRejection)
        && validCrew(event.crew)
        && event.seat < kWreckwaterPlayersPerCrew
        && event.crewOneScore <= network::kWreckwaterMaximumScore
        && event.crewTwoScore <= network::kWreckwaterMaximumScore;
}

[[nodiscard]] bool paired(uint64_t id, uint32_t generation) noexcept {
    return (id == 0u) == (generation == 0u);
}

[[nodiscard]] bool playableCrew(CrewId crew) noexcept {
    return crew == CrewId::CrewOne || crew == CrewId::CrewTwo;
}

[[nodiscard]] bool playerCommandSubject(
    const MatchEvent& event) noexcept {
    return event.source == MatchEventSource::PlayerCommand
        && event.sourceSequence != 0u
        && event.playerId != 0u
        && event.connectionId != 0u
        && event.connectionGeneration != 0u
        && playableCrew(event.crew);
}

[[nodiscard]] bool worldEventSubject(
    const MatchEvent& event) noexcept {
    return event.source
            == MatchEventSource::AuthoritativeWorldEvent
        && event.sourceStreamId != 0u
        && event.sourceSequence != 0u;
}

[[nodiscard]] bool noCargoSubject(
    const MatchEvent& event) noexcept {
    return event.cargoId == 0u
        && event.cargoGeneration == 0u
        && event.observedCargoRevision == 0u
        && event.cargoRevision == 0u;
}

[[nodiscard]] bool noSkiffSubjects(
    const MatchEvent& event) noexcept {
    return event.sourceSkiff == 0u
        && event.sourceSkiffGeneration == 0u
        && event.targetSkiff == 0u
        && event.targetSkiffGeneration == 0u;
}

[[nodiscard]] bool noAttachmentSubjects(
    const MatchEvent& event) noexcept {
    return event.attachmentId == 0u
        && event.attachmentGeneration == 0u
        && event.sourceAttachmentId == 0u
        && event.sourceAttachmentGeneration == 0u
        && event.resultingAttachmentId == 0u
        && event.resultingAttachmentGeneration == 0u;
}

[[nodiscard]] bool noPlayerSubject(
    const MatchEvent& event) noexcept {
    return event.playerId == 0u
        && event.seat == 0u
        && event.connectionId == 0u
        && event.connectionGeneration == 0u;
}

[[nodiscard]] bool canonicalSystemSource(
    const MatchEvent& event) noexcept {
    return event.source == MatchEventSource::MatchSystem
        && event.sourcePhysicsTick == 0u
        && event.commandTick == 0u
        && event.sourceSequence == 0u
        && event.sourceStreamId == 0u
        && event.commandType == MatchCommandType::TowCargo
        && event.worldEventType
            == AuthoritativeWorldEventType::AttachmentBroken
        && event.commandRejection == CommandRejectReason::None
        && event.worldEventRejection
            == WorldEventRejectReason::None;
}

[[nodiscard]] bool canonicalPlayerCommandSource(
    const MatchEvent& event) noexcept {
    return playerCommandSubject(event)
        && event.phase != MatchPhase::Warmup
        && event.sourcePhysicsTick != 0u
        && event.commandTick != 0u
        && event.sourcePhysicsTick <= event.commandTick
        && event.sourceStreamId == 0u
        && event.worldEventType
            == AuthoritativeWorldEventType::AttachmentBroken
        && event.worldEventRejection
            == WorldEventRejectReason::None;
}

[[nodiscard]] bool canonicalWorldSource(
    const MatchEvent& event) noexcept {
    return worldEventSubject(event)
        && event.sourcePhysicsTick != 0u
        && event.commandTick != 0u
        && event.sourcePhysicsTick <= event.commandTick
        && noPlayerSubject(event)
        && event.commandRejection == CommandRejectReason::None;
}

[[nodiscard]] bool canonicalNonFaultOutcome(
    const MatchEvent& event) noexcept {
    return event.faultReason == MatchFaultReason::None
        && (event.phase == MatchPhase::Finished
            ? event.outcome != MatchOutcomeType::Undecided
            : event.outcome == MatchOutcomeType::Undecided);
}

[[nodiscard]] bool validEventContract(
    const MatchEvent& event) noexcept {
    if (!paired(event.connectionId, event.connectionGeneration)
        || !paired(event.cargoId, event.cargoGeneration)
        || !paired(event.sourceSkiff, event.sourceSkiffGeneration)
        || !paired(event.targetSkiff, event.targetSkiffGeneration)
        || !paired(event.attachmentId, event.attachmentGeneration)
        || !paired(
            event.sourceAttachmentId,
            event.sourceAttachmentGeneration)
        || !paired(
            event.resultingAttachmentId,
            event.resultingAttachmentGeneration)) {
        return false;
    }

    const bool cargo =
        event.cargoId != 0u && event.cargoGeneration != 0u;
    const bool sourceSkiff =
        event.sourceSkiff != 0u
        && event.sourceSkiffGeneration != 0u;
    const bool targetSkiff =
        event.targetSkiff != 0u
        && event.targetSkiffGeneration != 0u;
    const bool sourceAttachment =
        event.sourceAttachmentId != 0u
        && event.sourceAttachmentGeneration != 0u;
    const bool resultingAttachment =
        event.resultingAttachmentId != 0u
        && event.resultingAttachmentGeneration != 0u;

    switch (event.type) {
        case MatchEventType::PlayerRegistered:
            return canonicalSystemSource(event)
                && canonicalNonFaultOutcome(event)
                && event.phase == MatchPhase::Warmup
                && event.outcome == MatchOutcomeType::Undecided
                && event.playerId != 0u
                && playableCrew(event.crew)
                && event.connectionId == 0u
                && event.connectionGeneration == 0u
                && noCargoSubject(event)
                && noSkiffSubjects(event)
                && noAttachmentSubjects(event);
        case MatchEventType::PlayerConnected:
        case MatchEventType::PlayerReconnected:
        case MatchEventType::PlayerDisconnected:
            return canonicalSystemSource(event)
                && canonicalNonFaultOutcome(event)
                && event.playerId != 0u
                && event.connectionId != 0u
                && event.connectionGeneration != 0u
                && playableCrew(event.crew)
                && noCargoSubject(event)
                && noSkiffSubjects(event)
                && noAttachmentSubjects(event);
        case MatchEventType::MatchStarted:
            return canonicalSystemSource(event)
                && event.phase == MatchPhase::Warmup
                && event.outcome == MatchOutcomeType::Undecided
                && event.faultReason == MatchFaultReason::None
                && event.crew == CrewId::None
                && event.crewOneScore == 0u
                && event.crewTwoScore == 0u
                && noPlayerSubject(event)
                && noCargoSubject(event)
                && noSkiffSubjects(event)
                && noAttachmentSubjects(event);
        case MatchEventType::PhaseChanged:
            return canonicalSystemSource(event)
                && canonicalNonFaultOutcome(event)
                && event.phase != MatchPhase::Warmup
                && event.crew == CrewId::None
                && noPlayerSubject(event)
                && noCargoSubject(event)
                && noSkiffSubjects(event)
                && noAttachmentSubjects(event);
        case MatchEventType::CommandRejected:
            return canonicalPlayerCommandSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && targetSkiff
                && !sourceSkiff
                && !sourceAttachment
                && !resultingAttachment
                && event.attachmentId == 0u
                && event.commandRejection
                    != CommandRejectReason::None;
        case MatchEventType::CargoTowAttached:
            return canonicalPlayerCommandSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::TowCargo
                && event.commandRejection == CommandRejectReason::None
                && targetSkiff && !sourceSkiff
                && !sourceAttachment && resultingAttachment
                && event.attachmentId
                    == event.resultingAttachmentId
                && event.attachmentGeneration
                    == event.resultingAttachmentGeneration;
        case MatchEventType::CargoTowCut:
            return canonicalPlayerCommandSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::CutTow
                && event.commandRejection == CommandRejectReason::None
                && sourceSkiff && !targetSkiff
                && sourceAttachment && !resultingAttachment
                && event.attachmentId == event.sourceAttachmentId
                && event.attachmentGeneration
                    == event.sourceAttachmentGeneration;
        case MatchEventType::CargoStolen:
            return canonicalPlayerCommandSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::StealCargo
                && event.commandRejection == CommandRejectReason::None
                && sourceSkiff && targetSkiff
                && event.sourceSkiff != event.targetSkiff
                && sourceAttachment && resultingAttachment
                && event.attachmentId
                    == event.resultingAttachmentId
                && event.attachmentGeneration
                    == event.resultingAttachmentGeneration;
        case MatchEventType::CargoBanked:
            return canonicalPlayerCommandSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::BankCargo
                && event.commandRejection == CommandRejectReason::None
                && sourceSkiff && !targetSkiff
                && sourceAttachment && !resultingAttachment
                && event.attachmentId == event.sourceAttachmentId
                && event.attachmentGeneration
                    == event.sourceAttachmentGeneration;
        case MatchEventType::ScoreChanged:
            return canonicalPlayerCommandSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::BankCargo
                && event.commandRejection == CommandRejectReason::None
                && !sourceSkiff && !targetSkiff
                && !sourceAttachment && !resultingAttachment
                && event.attachmentId == 0u;
        case MatchEventType::MatchFinished:
            if (!canonicalSystemSource(event)
                || event.phase != MatchPhase::Finished
                || event.outcome == MatchOutcomeType::Undecided
                || event.faultReason != MatchFaultReason::None
                || !noPlayerSubject(event)
                || !noCargoSubject(event)
                || !noSkiffSubjects(event)
                || !noAttachmentSubjects(event)) {
                return false;
            }
            if (event.outcome == MatchOutcomeType::Tie) {
                return event.crew == CrewId::None
                    && event.crewOneScore == event.crewTwoScore;
            }
            return playableCrew(event.crew)
                && ((event.crew == CrewId::CrewOne
                        && event.crewOneScore > event.crewTwoScore)
                    || (event.crew == CrewId::CrewTwo
                        && event.crewTwoScore
                            > event.crewOneScore));
        case MatchEventType::WorldEventRejected:
            if (!canonicalWorldSource(event)
                || !canonicalNonFaultOutcome(event)
                || event.worldEventRejection
                    == WorldEventRejectReason::None
                || event.commandType != MatchCommandType::TowCargo
                || event.crew != CrewId::None
                || targetSkiff || resultingAttachment
                || event.attachmentId
                    != event.sourceAttachmentId
                || event.attachmentGeneration
                    != event.sourceAttachmentGeneration
                || event.cargoRevision
                    != event.observedCargoRevision) {
                return false;
            }
            switch (event.worldEventType) {
                case AuthoritativeWorldEventType::AttachmentBroken:
                    return cargo
                        && event.observedCargoRevision != 0u
                        && sourceSkiff && sourceAttachment;
                case AuthoritativeWorldEventType::CargoLost:
                    return cargo
                        && event.observedCargoRevision != 0u
                        && (sourceSkiff == sourceAttachment);
                case AuthoritativeWorldEventType::SkiffSunk:
                    return sourceSkiff
                        && (cargo
                            ? event.observedCargoRevision != 0u
                                && sourceAttachment
                            : event.observedCargoRevision == 0u
                                && !sourceAttachment);
                case AuthoritativeWorldEventType::SkiffRespawned:
                    return sourceSkiff && !cargo
                        && event.observedCargoRevision == 0u
                        && !sourceAttachment;
            }
            return false;
        case MatchEventType::AttachmentBroken:
            return canonicalWorldSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::TowCargo
                && event.crew == CrewId::None
                && event.worldEventType
                    == AuthoritativeWorldEventType::AttachmentBroken
                && event.worldEventRejection
                    == WorldEventRejectReason::None
                && sourceSkiff && !targetSkiff
                && sourceAttachment && !resultingAttachment
                && event.attachmentId == event.sourceAttachmentId
                && event.attachmentGeneration
                    == event.sourceAttachmentGeneration;
        case MatchEventType::CargoLost:
            return canonicalWorldSource(event)
                && canonicalNonFaultOutcome(event) && cargo
                && event.commandType == MatchCommandType::TowCargo
                && (event.worldEventType
                        == AuthoritativeWorldEventType::CargoLost
                    || event.worldEventType
                        == AuthoritativeWorldEventType::SkiffSunk)
                && event.worldEventRejection
                    == WorldEventRejectReason::None
                && !targetSkiff && !resultingAttachment
                && (event.worldEventType
                        == AuthoritativeWorldEventType::SkiffSunk
                    ? playableCrew(event.crew)
                        && sourceSkiff && sourceAttachment
                    : event.crew == CrewId::None
                        && (sourceSkiff == sourceAttachment))
                && (sourceAttachment
                    ? event.attachmentId
                            == event.sourceAttachmentId
                        && event.attachmentGeneration
                            == event.sourceAttachmentGeneration
                    : event.attachmentId == 0u);
        case MatchEventType::SkiffSunk:
            return canonicalWorldSource(event)
                && canonicalNonFaultOutcome(event)
                && event.commandType == MatchCommandType::TowCargo
                && playableCrew(event.crew)
                && event.worldEventType
                    == AuthoritativeWorldEventType::SkiffSunk
                && event.worldEventRejection
                    == WorldEventRejectReason::None
                && sourceSkiff && !targetSkiff
                && noCargoSubject(event)
                && noAttachmentSubjects(event);
        case MatchEventType::SkiffRespawned:
            return canonicalWorldSource(event)
                && canonicalNonFaultOutcome(event)
                && event.commandType == MatchCommandType::TowCargo
                && playableCrew(event.crew)
                && event.worldEventType
                    == AuthoritativeWorldEventType::SkiffRespawned
                && event.worldEventRejection
                    == WorldEventRejectReason::None
                && sourceSkiff
                && targetSkiff
                && event.sourceSkiff == event.targetSkiff
                && event.sourceSkiffGeneration
                    != std::numeric_limits<uint32_t>::max()
                && event.targetSkiffGeneration
                    == event.sourceSkiffGeneration + 1u
                && noCargoSubject(event)
                && noAttachmentSubjects(event);
        case MatchEventType::AuthorityFault: {
            if (event.faultReason == MatchFaultReason::None
                || event.commandRejection
                    != CommandRejectReason::None
                || event.worldEventRejection
                    != WorldEventRejectReason::None) {
                return false;
            }
            if (event.source == MatchEventSource::MatchSystem) {
                return canonicalSystemSource(event)
                    && event.crew == CrewId::None
                    && noPlayerSubject(event)
                    && noCargoSubject(event)
                    && noSkiffSubjects(event)
                    && noAttachmentSubjects(event);
            }
            if (event.source == MatchEventSource::PlayerCommand) {
                return canonicalPlayerCommandSource(event) && cargo;
            }
            return canonicalWorldSource(event);
        }
    }
    return false;
}

struct PlayerConnectionLifetime {
    uint64_t connectionId = 0u;
    uint32_t generation = 0u;
    uint32_t characterHandle = 0u;
    CrewId crew = CrewId::None;
    uint32_t seat = 0u;
    bool registered = false;
    bool connected = false;
};

struct CommandSourceLifetime {
    uint64_t sourcePhysicsTick = 0u;
    uint64_t commandTick = 0u;
    uint64_t connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    MatchCommandType commandType = MatchCommandType::TowCargo;
    uint32_t cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t observedCargoRevision = 0u;
    uint32_t cargoRevision = 0u;
    uint32_t actorSkiff = 0u;
    uint32_t actorSkiffGeneration = 0u;
    MatchPhase phase = MatchPhase::Warmup;
    MatchOutcomeType outcome = MatchOutcomeType::Undecided;
    MatchFaultReason faultReason = MatchFaultReason::None;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    uint32_t stateHash = 0u;
    uint32_t eventTypeBits = 0u;
};

struct WorldSourceLifetime {
    uint64_t sourcePhysicsTick = 0u;
    uint64_t commandTick = 0u;
    AuthoritativeWorldEventType worldEventType =
        AuthoritativeWorldEventType::AttachmentBroken;
    uint32_t cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t observedCargoRevision = 0u;
    uint32_t sourceSkiff = 0u;
    uint32_t sourceSkiffGeneration = 0u;
    uint32_t targetSkiff = 0u;
    uint32_t targetSkiffGeneration = 0u;
    uint64_t sourceAttachmentId = 0u;
    uint32_t sourceAttachmentGeneration = 0u;
    MatchPhase phase = MatchPhase::Warmup;
    MatchOutcomeType outcome = MatchOutcomeType::Undecided;
    MatchFaultReason faultReason = MatchFaultReason::None;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    uint32_t stateHash = 0u;
    uint32_t eventTypeBits = 0u;
};

using SeatLifetimeKey = std::pair<uint32_t, uint32_t>;
using ConnectionLifetimeOwner = std::pair<PlayerId, uint32_t>;

[[nodiscard]] constexpr uint32_t eventTypeBit(
    MatchEventType type) noexcept {
    return 1u << (enumWord(type) - 1u);
}

[[nodiscard]] bool consumeLifetime(
    size_t maximumLifetimes, size_t& lifetimeCount) noexcept {
    if (lifetimeCount >= maximumLifetimes) return false;
    ++lifetimeCount;
    return true;
}

template <typename Id>
[[nodiscard]] bool mergeOptionalIdentity(
    Id& storedId, uint32_t& storedGeneration,
    Id eventId, uint32_t eventGeneration) noexcept {
    if (eventId == Id{}) return true;
    if (storedId == Id{}) {
        storedId = eventId;
        storedGeneration = eventGeneration;
        return true;
    }
    return storedId == eventId
        && storedGeneration == eventGeneration;
}

[[nodiscard]] std::pair<uint32_t, uint32_t> commandActor(
    const MatchEvent& event) noexcept {
    if (event.targetSkiff != 0u) {
        return {
            event.targetSkiff, event.targetSkiffGeneration};
    }
    if (event.type != MatchEventType::ScoreChanged) {
        return {
            event.sourceSkiff, event.sourceSkiffGeneration};
    }
    return {};
}

[[nodiscard]] bool sameCommandSource(
    CommandSourceLifetime& source,
    const MatchEvent& event) noexcept {
    if (source.sourcePhysicsTick != event.sourcePhysicsTick
        || source.commandTick != event.commandTick
        || source.connectionId != event.connectionId
        || source.connectionGeneration != event.connectionGeneration
        || source.commandType != event.commandType
        || source.cargoId != event.cargoId
        || source.cargoGeneration != event.cargoGeneration
        || source.observedCargoRevision
            != event.observedCargoRevision
        || source.cargoRevision != event.cargoRevision
        || source.phase != event.phase
        || source.outcome != event.outcome
        || source.faultReason != event.faultReason
        || source.crewOneScore != event.crewOneScore
        || source.crewTwoScore != event.crewTwoScore
        || source.stateHash != event.stateHash) {
        return false;
    }
    const auto [actorSkiff, actorGeneration] =
        commandActor(event);
    return mergeOptionalIdentity(
        source.actorSkiff, source.actorSkiffGeneration,
        actorSkiff, actorGeneration);
}

[[nodiscard]] bool sameWorldSource(
    WorldSourceLifetime& source,
    const MatchEvent& event) noexcept {
    if (source.sourcePhysicsTick != event.sourcePhysicsTick
        || source.commandTick != event.commandTick
        || source.worldEventType != event.worldEventType
        || source.phase != event.phase
        || source.outcome != event.outcome
        || source.faultReason != event.faultReason
        || source.crewOneScore != event.crewOneScore
        || source.crewTwoScore != event.crewTwoScore
        || source.stateHash != event.stateHash) {
        return false;
    }
    if (event.observedCargoRevision != 0u) {
        if (source.observedCargoRevision != 0u
            && source.observedCargoRevision
                != event.observedCargoRevision) {
            return false;
        }
        source.observedCargoRevision =
            event.observedCargoRevision;
    }
    return mergeOptionalIdentity(
               source.cargoId, source.cargoGeneration,
               event.cargoId, event.cargoGeneration)
        && mergeOptionalIdentity(
               source.sourceSkiff, source.sourceSkiffGeneration,
               event.sourceSkiff, event.sourceSkiffGeneration)
        && mergeOptionalIdentity(
               source.targetSkiff, source.targetSkiffGeneration,
               event.targetSkiff, event.targetSkiffGeneration)
        && mergeOptionalIdentity(
               source.sourceAttachmentId,
               source.sourceAttachmentGeneration,
               event.sourceAttachmentId,
               event.sourceAttachmentGeneration);
}

[[nodiscard]] WreckwaterReplayPlaybackError validateEventLifetime(
    const MatchEvent& event,
    size_t maximumLifetimes,
    size_t& lifetimeCount,
    bool matchStarted,
    std::map<PlayerId, PlayerConnectionLifetime>& players,
    std::map<SeatLifetimeKey, PlayerId>& seats,
    std::map<uint64_t, ConnectionLifetimeOwner>& connections,
    std::map<std::pair<PlayerId, uint64_t>,
        CommandSourceLifetime>& commands,
    std::map<std::pair<uint32_t, uint64_t>,
        WorldSourceLifetime>& worldSources) {
    if (event.type == MatchEventType::PlayerRegistered) {
        if (matchStarted) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        auto position = players.find(event.playerId);
        if (position != players.end() && position->second.registered) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        if (position == players.end()) {
            if (!consumeLifetime(maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            position = players.emplace(
                event.playerId, PlayerConnectionLifetime{}).first;
        }
        const SeatLifetimeKey seatKey{
            enumWord(event.crew), event.seat};
        if (seats.find(seatKey) != seats.end()) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        if (!consumeLifetime(maximumLifetimes, lifetimeCount)) {
            return WreckwaterReplayPlaybackError::LifetimeCapacity;
        }
        seats.emplace(seatKey, event.playerId);
        position->second.crew = event.crew;
        position->second.seat = event.seat;
        position->second.registered = true;
    } else if (
        event.type == MatchEventType::PlayerConnected
        || event.type == MatchEventType::PlayerReconnected
        || event.type == MatchEventType::PlayerDisconnected) {
        auto position = players.find(event.playerId);
        if (position == players.end()) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        PlayerConnectionLifetime& lifetime = position->second;
        if (!lifetime.registered
            || event.crew != lifetime.crew
            || event.seat != lifetime.seat) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        if (event.type == MatchEventType::PlayerConnected) {
            if (lifetime.connected || lifetime.generation != 0u
                || event.connectionGeneration != 1u) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
        } else if (event.type == MatchEventType::PlayerReconnected) {
            if (lifetime.connected || lifetime.generation == 0u
                || lifetime.generation
                    == std::numeric_limits<uint32_t>::max()
                || event.connectionGeneration
                    != lifetime.generation + 1u) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
        } else {
            if (!lifetime.connected
                || event.connectionId != lifetime.connectionId
                || event.connectionGeneration
                    != lifetime.generation) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            lifetime.connected = false;
        }
        if (event.type != MatchEventType::PlayerDisconnected) {
            if (connections.find(event.connectionId)
                != connections.end()) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (!consumeLifetime(
                    maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            connections.emplace(
                event.connectionId,
                ConnectionLifetimeOwner{
                    event.playerId, event.connectionGeneration});
            lifetime.connectionId = event.connectionId;
            lifetime.generation = event.connectionGeneration;
            lifetime.connected = true;
        }
    }

    if (event.source == MatchEventSource::PlayerCommand) {
        auto playerPosition = players.find(event.playerId);
        if (playerPosition == players.end()) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        const PlayerConnectionLifetime& connection =
            playerPosition->second;
        const bool staleRejection =
            event.type == MatchEventType::CommandRejected
            && event.commandRejection
                == CommandRejectReason::StaleConnection;
        const auto connectionOwner =
            connections.find(event.connectionId);
        if (!connection.registered
            || connectionOwner == connections.end()
            || connectionOwner->second
                != ConnectionLifetimeOwner{
                    event.playerId, event.connectionGeneration}
            || event.connectionGeneration > connection.generation
            || (staleRejection
                ? connection.connected
                    && event.connectionGeneration
                        == connection.generation
                    && event.connectionId == connection.connectionId
                : (!connection.connected
                    || event.connectionGeneration
                        != connection.generation
                    || event.connectionId
                        != connection.connectionId))
            || event.crew != connection.crew
            || (event.type != MatchEventType::AuthorityFault
                && event.seat != connection.seat)
            || (event.type == MatchEventType::AuthorityFault
                && event.seat != 0u)) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }

        const auto sourceKey =
            std::pair{event.playerId, event.sourceSequence};
        const uint32_t eventBit = eventTypeBit(event.type);
        auto sourcePosition = commands.find(sourceKey);
        if (sourcePosition == commands.end()) {
            if (event.type == MatchEventType::ScoreChanged) {
                return WreckwaterReplayPlaybackError::InvalidEvent;
            }
            if (!consumeLifetime(maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            const auto [actorSkiff, actorGeneration] =
                commandActor(event);
            commands.emplace(sourceKey, CommandSourceLifetime{
                .sourcePhysicsTick = event.sourcePhysicsTick,
                .commandTick = event.commandTick,
                .connectionId = event.connectionId,
                .connectionGeneration = event.connectionGeneration,
                .commandType = event.commandType,
                .cargoId = event.cargoId,
                .cargoGeneration = event.cargoGeneration,
                .observedCargoRevision =
                    event.observedCargoRevision,
                .cargoRevision = event.cargoRevision,
                .actorSkiff = actorSkiff,
                .actorSkiffGeneration = actorGeneration,
                .phase = event.phase,
                .outcome = event.outcome,
                .faultReason = event.faultReason,
                .crewOneScore = event.crewOneScore,
                .crewTwoScore = event.crewTwoScore,
                .stateHash = event.stateHash,
                .eventTypeBits = eventBit,
            });
        } else {
            CommandSourceLifetime& source = sourcePosition->second;
            if (!sameCommandSource(source, event)
                || (source.eventTypeBits & eventBit) != 0u) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (event.type == MatchEventType::ScoreChanged
                && source.eventTypeBits
                    != eventTypeBit(MatchEventType::CargoBanked)) {
                return WreckwaterReplayPlaybackError::InvalidEvent;
            }
            source.eventTypeBits |= eventBit;
        }
    } else if (
        event.source
        == MatchEventSource::AuthoritativeWorldEvent) {
        const auto sourceKey = std::pair{
            event.sourceStreamId, event.sourceSequence};
        const uint32_t eventBit = eventTypeBit(event.type);
        auto sourcePosition = worldSources.find(sourceKey);
        if (sourcePosition == worldSources.end()) {
            if (!consumeLifetime(maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            worldSources.emplace(
                sourceKey, WorldSourceLifetime{
                    .sourcePhysicsTick = event.sourcePhysicsTick,
                    .commandTick = event.commandTick,
                    .worldEventType = event.worldEventType,
                    .cargoId = event.cargoId,
                    .cargoGeneration = event.cargoGeneration,
                    .observedCargoRevision =
                        event.observedCargoRevision,
                    .sourceSkiff = event.sourceSkiff,
                    .sourceSkiffGeneration =
                        event.sourceSkiffGeneration,
                    .targetSkiff = event.targetSkiff,
                    .targetSkiffGeneration =
                        event.targetSkiffGeneration,
                    .sourceAttachmentId =
                        event.sourceAttachmentId,
                    .sourceAttachmentGeneration =
                        event.sourceAttachmentGeneration,
                    .phase = event.phase,
                    .outcome = event.outcome,
                    .faultReason = event.faultReason,
                    .crewOneScore = event.crewOneScore,
                    .crewTwoScore = event.crewTwoScore,
                    .stateHash = event.stateHash,
                    .eventTypeBits = eventBit,
                });
        } else {
            WorldSourceLifetime& source = sourcePosition->second;
            if (!sameWorldSource(source, event)
                || (source.eventTypeBits & eventBit) != 0u) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (event.type == MatchEventType::SkiffSunk
                && (source.eventTypeBits
                    & eventTypeBit(MatchEventType::CargoLost)) == 0u
                && source.eventTypeBits != 0u) {
                return WreckwaterReplayPlaybackError::InvalidEvent;
            }
            if (event.type == MatchEventType::CargoLost
                && (source.eventTypeBits
                    & eventTypeBit(MatchEventType::SkiffSunk)) != 0u) {
                return WreckwaterReplayPlaybackError::InvalidEvent;
            }
            source.eventTypeBits |= eventBit;
        }
    }
    return WreckwaterReplayPlaybackError::None;
}

[[nodiscard]] bool validCommandSourceBundle(
    const CommandSourceLifetime& source) noexcept {
    const uint32_t rejected =
        eventTypeBit(MatchEventType::CommandRejected);
    const uint32_t fault =
        eventTypeBit(MatchEventType::AuthorityFault);
    if (source.eventTypeBits == rejected
        || source.eventTypeBits == fault) {
        return true;
    }
    switch (source.commandType) {
        case MatchCommandType::TowCargo:
            return source.eventTypeBits
                == eventTypeBit(MatchEventType::CargoTowAttached);
        case MatchCommandType::CutTow:
            return source.eventTypeBits
                == eventTypeBit(MatchEventType::CargoTowCut);
        case MatchCommandType::StealCargo:
            return source.eventTypeBits
                == eventTypeBit(MatchEventType::CargoStolen);
        case MatchCommandType::BankCargo:
            return source.eventTypeBits
                == (eventTypeBit(MatchEventType::CargoBanked)
                    | eventTypeBit(MatchEventType::ScoreChanged));
    }
    return false;
}

[[nodiscard]] bool validWorldSourceBundle(
    const WorldSourceLifetime& source) noexcept {
    const uint32_t rejected =
        eventTypeBit(MatchEventType::WorldEventRejected);
    const uint32_t fault =
        eventTypeBit(MatchEventType::AuthorityFault);
    if (source.eventTypeBits == rejected
        || source.eventTypeBits == fault) {
        return true;
    }
    switch (source.worldEventType) {
        case AuthoritativeWorldEventType::AttachmentBroken:
            return source.eventTypeBits
                == eventTypeBit(MatchEventType::AttachmentBroken);
        case AuthoritativeWorldEventType::CargoLost:
            return source.eventTypeBits
                == eventTypeBit(MatchEventType::CargoLost);
        case AuthoritativeWorldEventType::SkiffSunk:
            return source.eventTypeBits
                    == eventTypeBit(MatchEventType::SkiffSunk)
                || source.eventTypeBits
                    == (eventTypeBit(MatchEventType::CargoLost)
                        | eventTypeBit(MatchEventType::SkiffSunk));
        case AuthoritativeWorldEventType::SkiffRespawned:
            return source.eventTypeBits
                == eventTypeBit(MatchEventType::SkiffRespawned);
    }
    return false;
}

[[nodiscard]] WreckwaterReplayPlaybackError
validateCertifiedCharacters(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    size_t maximumLifetimes,
    size_t& lifetimeCount,
    std::map<PlayerId, PlayerConnectionLifetime>& players,
    std::map<uint32_t, PlayerId>& characterHandles) {
    constexpr size_t kRequiredCharacters =
        2u * kWreckwaterPlayersPerCrew;
    if (snapshot.characters.size() != kRequiredCharacters
        || players.size() != kRequiredCharacters) {
        return WreckwaterReplayPlaybackError::InvalidSnapshot;
    }
    for (const network::WreckwaterCharacterState& character
         : snapshot.characters) {
        auto playerPosition = players.find(character.playerId);
        if (playerPosition == players.end()
            || !playerPosition->second.registered) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        PlayerConnectionLifetime& player = playerPosition->second;
        const bool certifiedConnected =
            (character.stateFlags
                & network::kWreckwaterCharacterStateConnectedFlag)
            != 0u;
        if (character.connectionGeneration != player.generation
            || certifiedConnected != player.connected) {
            return WreckwaterReplayPlaybackError::GenerationAlias;
        }
        if (player.characterHandle == 0u) {
            if (characterHandles.find(character.characterHandle)
                != characterHandles.end()) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (!consumeLifetime(
                    maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            characterHandles.emplace(
                character.characterHandle, character.playerId);
            player.characterHandle = character.characterHandle;
        } else {
            const auto handlePosition =
                characterHandles.find(character.characterHandle);
            if (player.characterHandle != character.characterHandle
                || handlePosition == characterHandles.end()
                || handlePosition->second != character.playerId) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
        }
    }
    return WreckwaterReplayPlaybackError::None;
}

[[nodiscard]] bool decodeEvent(
    std::span<const std::byte> payload,
    MatchEvent& event) noexcept {
    if (payload.size() != kWreckwaterReplayMatchEventBytes) return false;
    Reader reader(payload);
    uint32_t source = 0u;
    uint32_t commandType = 0u;
    uint32_t type = 0u;
    uint32_t worldEventType = 0u;
    uint32_t phase = 0u;
    uint32_t outcome = 0u;
    uint32_t fault = 0u;
    uint32_t commandRejection = 0u;
    uint32_t worldEventRejection = 0u;
    uint32_t crew = 0u;
    if (!reader.u32(event.schemaVersion)
        || !reader.u64(event.eventSequence)
        || !reader.u64(event.matchId)
        || !reader.u64(event.worldId)
        || !reader.u32(event.worldEpoch)
        || !reader.u32(event.authorityEpoch)
        || !reader.u64(event.tick)
        || !reader.u64(event.sourcePhysicsTick)
        || !reader.u64(event.commandTick)
        || !reader.u64(event.sourceSequence)
        || !reader.u32(source)
        || !reader.u32(event.sourceStreamId)
        || !reader.u32(commandType)
        || !reader.u32(type)
        || !reader.u32(worldEventType)
        || !reader.u32(phase)
        || !reader.u32(outcome)
        || !reader.u32(fault)
        || !reader.u32(commandRejection)
        || !reader.u32(worldEventRejection)
        || !reader.u64(event.playerId)
        || !reader.u32(event.seat)
        || !reader.u64(event.connectionId)
        || !reader.u32(event.connectionGeneration)
        || !reader.u32(crew)
        || !reader.u32(event.cargoId)
        || !reader.u32(event.cargoGeneration)
        || !reader.u32(event.observedCargoRevision)
        || !reader.u32(event.cargoRevision)
        || !reader.u32(event.sourceSkiff)
        || !reader.u32(event.sourceSkiffGeneration)
        || !reader.u32(event.targetSkiff)
        || !reader.u32(event.targetSkiffGeneration)
        || !reader.u64(event.attachmentId)
        || !reader.u32(event.attachmentGeneration)
        || !reader.u64(event.sourceAttachmentId)
        || !reader.u32(event.sourceAttachmentGeneration)
        || !reader.u64(event.resultingAttachmentId)
        || !reader.u32(event.resultingAttachmentGeneration)
        || !reader.u32(event.crewOneScore)
        || !reader.u32(event.crewTwoScore)
        || !reader.u32(event.stateHash)
        || reader.remaining() != 0u) {
        return false;
    }
    event.source = static_cast<MatchEventSource>(source);
    event.commandType = static_cast<MatchCommandType>(commandType);
    event.type = static_cast<MatchEventType>(type);
    event.worldEventType =
        static_cast<AuthoritativeWorldEventType>(worldEventType);
    event.phase = static_cast<MatchPhase>(phase);
    event.outcome = static_cast<MatchOutcomeType>(outcome);
    event.faultReason = static_cast<MatchFaultReason>(fault);
    event.commandRejection =
        static_cast<CommandRejectReason>(commandRejection);
    event.worldEventRejection =
        static_cast<WorldEventRejectReason>(worldEventRejection);
    event.crew = static_cast<CrewId>(crew);
    return true;
}

[[nodiscard]] uint64_t initialReplayHash(
    const WreckwaterReplayConfig& config) noexcept {
    HashWriter writer;
    writer.raw(kInitialHashTag);
    writer.u32(kWreckwaterReplaySchemaVersion);
    writer.u64(config.sessionId);
    writer.u64(config.matchId);
    writer.u64(config.worldId);
    writer.u32(config.worldEpoch);
    writer.u32(config.authorityEpoch);
    writer.u32(config.tickRateHz);
    writer.u64(config.contentHash);
    writer.u32(config.matchConfigurationHash);
    return writer.value();
}

[[nodiscard]] uint64_t recordRollingHash(
    uint64_t prior,
    const WreckwaterReplayRecord& record,
    std::span<const std::byte> payload) noexcept {
    HashWriter writer;
    writer.u64(prior);
    writer.u32(enumWord(record.type));
    writer.u32(record.payloadSchemaVersion);
    writer.u64(record.applicationTick);
    writer.u64(record.physicsEvidenceTick);
    writer.u64(record.sequence);
    writer.u32(record.payloadBytes);
    writer.u32(record.flags);
    writer.u64(record.payloadHash);
    writer.raw(payload);
    return writer.value();
}

[[nodiscard]] uint64_t finalReplayHash(
    uint64_t rollingHash,
    const WreckwaterReplaySummary& summary) noexcept {
    HashWriter writer;
    writer.raw(kFinalHashTag);
    writer.u64(rollingHash);
    writer.u64(summary.firstApplicationTick);
    writer.u64(summary.lastApplicationTick);
    writer.u64(summary.recordCount);
    writer.u64(summary.payloadBytes);
    writer.u32(summary.finalMatchStateHash);
    writer.u32(summary.finalEventStreamHash);
    return writer.value();
}

[[nodiscard]] bool snapshotIdentityMatches(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    const WreckwaterReplayConfig& config) noexcept {
    return snapshot.sessionId == config.sessionId
        && snapshot.matchId == config.matchId
        && snapshot.worldId == config.worldId
        && snapshot.worldEpoch == config.worldEpoch
        && snapshot.authorityEpoch == config.authorityEpoch;
}

struct NetworkLifetime {
    uint32_t generation = 0u;
    network::WreckwaterEntityKind kind =
        network::WreckwaterEntityKind::Skiff;
    uint32_t logicalId = 0u;
    uint32_t logicalGeneration = 0u;
    size_t lastSeenSnapshot = 0u;
};

struct LogicalLifetime {
    uint32_t generation = 0u;
    network::NetEntityId ownerNetId = 0u;
    uint32_t ownerNetGeneration = 0u;
    network::WreckwaterCrew crew = network::WreckwaterCrew::None;
    network::WreckwaterCargoLogicalState cargo{};
    network::WreckwaterSkiffLogicalState skiff{};
    network::WreckwaterAttachmentLogicalState attachment{};
    size_t lastSeenSnapshot = 0u;
};

struct AttachmentLifetime {
    uint32_t generation = 0u;
    network::NetEntityId ownerNetId = 0u;
    uint32_t ownerNetGeneration = 0u;
    uint32_t cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t towingSkiffId = 0u;
    uint32_t towingSkiffGeneration = 0u;
    size_t lastSeenSnapshot = 0u;
};

using LogicalLifetimeKey = std::tuple<uint32_t, uint32_t>;
using NetworkLifetimeMap =
    std::map<network::NetEntityId, NetworkLifetime>;
using LogicalLifetimeMap =
    std::map<LogicalLifetimeKey, LogicalLifetime>;
using AttachmentLifetimeMap =
    std::map<network::LogicalAttachmentId, AttachmentLifetime>;

[[nodiscard]] bool contiguous(
    size_t lastSeenSnapshot, size_t snapshotIndex) noexcept {
    return snapshotIndex != 0u
        && lastSeenSnapshot == snapshotIndex - 1u;
}

[[nodiscard]] bool validContinuingLogicalState(
    const LogicalLifetime& prior,
    const network::WreckwaterEntityState& entity) noexcept {
    if (entity.kind == network::WreckwaterEntityKind::Skiff) {
        return entity.crew == prior.crew
            && !(prior.skiff.disposition
                    == network::WreckwaterSkiffDisposition::Sunk
                && entity.skiff.disposition
                    == network::WreckwaterSkiffDisposition::Active);
    }

    if (entity.cargo.revision < prior.cargo.revision) return false;
    const bool terminalCargo =
        prior.cargo.disposition
                == network::WreckwaterCargoDisposition::Banked
            || prior.cargo.disposition
                == network::WreckwaterCargoDisposition::Lost;
    if (terminalCargo
        && entity.cargo.disposition != prior.cargo.disposition) {
        return false;
    }
    if (entity.cargo.revision == prior.cargo.revision) {
        return entity.crew == prior.crew
            && entity.cargo == prior.cargo
            && entity.attachment == prior.attachment;
    }
    return true;
}

[[nodiscard]] WreckwaterReplayPlaybackError validateLifetimes(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    size_t snapshotIndex,
    size_t maximumLifetimes,
    size_t& lifetimeCount,
    NetworkLifetimeMap& networkLifetimes,
    LogicalLifetimeMap& logicalLifetimes,
    AttachmentLifetimeMap& attachmentLifetimes) {
    for (const network::WreckwaterEntityState& entity
         : snapshot.entities) {
        const uint32_t logicalId =
            entity.kind == network::WreckwaterEntityKind::Cargo
            ? entity.cargo.cargoId : entity.skiff.skiffId;
        const uint32_t logicalGeneration =
            entity.kind == network::WreckwaterEntityKind::Cargo
            ? entity.cargo.generation : entity.skiff.generation;

        auto networkPosition =
            networkLifetimes.find(entity.netEntityId);
        if (networkPosition == networkLifetimes.end()) {
            if (!consumeLifetime(
                    maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            networkLifetimes.emplace(entity.netEntityId, NetworkLifetime{
                .generation = entity.netGeneration,
                .kind = entity.kind,
                .logicalId = logicalId,
                .logicalGeneration = logicalGeneration,
                .lastSeenSnapshot = snapshotIndex,
            });
        } else {
            NetworkLifetime& lifetime = networkPosition->second;
            if (entity.netGeneration < lifetime.generation
                || (entity.netGeneration
                        == lifetime.generation
                    && (entity.kind != lifetime.kind
                        || logicalId != lifetime.logicalId
                        || logicalGeneration
                            != lifetime.logicalGeneration
                        || !contiguous(
                            lifetime.lastSeenSnapshot,
                            snapshotIndex)))) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (entity.netGeneration > lifetime.generation) {
                lifetime.generation = entity.netGeneration;
                lifetime.kind = entity.kind;
                lifetime.logicalId = logicalId;
                lifetime.logicalGeneration = logicalGeneration;
            }
            lifetime.lastSeenSnapshot = snapshotIndex;
        }

        const LogicalLifetimeKey logicalKey{
            enumWord(entity.kind), logicalId};
        auto logicalPosition = logicalLifetimes.find(logicalKey);
        if (logicalPosition == logicalLifetimes.end()) {
            if (!consumeLifetime(
                    maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            logicalLifetimes.emplace(logicalKey, LogicalLifetime{
                .generation = logicalGeneration,
                .ownerNetId = entity.netEntityId,
                .ownerNetGeneration = entity.netGeneration,
                .crew = entity.crew,
                .cargo = entity.cargo,
                .skiff = entity.skiff,
                .attachment = entity.attachment,
                .lastSeenSnapshot = snapshotIndex,
            });
        } else {
            LogicalLifetime& lifetime = logicalPosition->second;
            if (logicalGeneration < lifetime.generation
                || (logicalGeneration == lifetime.generation
                    && (entity.netEntityId
                            != lifetime.ownerNetId
                        || entity.netGeneration
                            != lifetime.ownerNetGeneration
                        || !contiguous(
                            lifetime.lastSeenSnapshot,
                            snapshotIndex)
                        || !validContinuingLogicalState(
                            lifetime, entity)))) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (logicalGeneration > lifetime.generation) {
                lifetime.generation = logicalGeneration;
                lifetime.ownerNetId = entity.netEntityId;
                lifetime.ownerNetGeneration = entity.netGeneration;
            }
            lifetime.crew = entity.crew;
            lifetime.cargo = entity.cargo;
            lifetime.skiff = entity.skiff;
            lifetime.attachment = entity.attachment;
            lifetime.lastSeenSnapshot = snapshotIndex;
        }

        if (entity.attachment.attachmentId == 0u) continue;
        auto attachmentPosition = attachmentLifetimes.find(
            entity.attachment.attachmentId);
        if (attachmentPosition == attachmentLifetimes.end()) {
            if (!consumeLifetime(
                    maximumLifetimes, lifetimeCount)) {
                return WreckwaterReplayPlaybackError::LifetimeCapacity;
            }
            attachmentLifetimes.emplace(
                entity.attachment.attachmentId, AttachmentLifetime{
                .generation = entity.attachment.generation,
                .ownerNetId = entity.netEntityId,
                .ownerNetGeneration = entity.netGeneration,
                .cargoId = entity.cargo.cargoId,
                .cargoGeneration = entity.cargo.generation,
                .towingSkiffId = entity.cargo.towingSkiffId,
                .towingSkiffGeneration =
                    entity.cargo.towingSkiffGeneration,
                .lastSeenSnapshot = snapshotIndex,
            });
        } else {
            AttachmentLifetime& lifetime = attachmentPosition->second;
            if (entity.attachment.generation
                    < lifetime.generation
                || (entity.attachment.generation
                        == lifetime.generation
                    && (entity.netEntityId
                            != lifetime.ownerNetId
                        || entity.netGeneration
                            != lifetime.ownerNetGeneration
                        || entity.cargo.cargoId
                            != lifetime.cargoId
                        || entity.cargo.generation
                            != lifetime.cargoGeneration
                        || entity.cargo.towingSkiffId
                            != lifetime.towingSkiffId
                        || entity.cargo.towingSkiffGeneration
                            != lifetime.towingSkiffGeneration
                        || !contiguous(
                            lifetime.lastSeenSnapshot,
                            snapshotIndex)))) {
                return WreckwaterReplayPlaybackError::GenerationAlias;
            }
            if (entity.attachment.generation
                > lifetime.generation) {
                lifetime.generation =
                    entity.attachment.generation;
                lifetime.ownerNetId = entity.netEntityId;
                lifetime.ownerNetGeneration =
                    entity.netGeneration;
                lifetime.cargoId = entity.cargo.cargoId;
                lifetime.cargoGeneration =
                    entity.cargo.generation;
            }
            lifetime.towingSkiffId = entity.cargo.towingSkiffId;
            lifetime.towingSkiffGeneration =
                entity.cargo.towingSkiffGeneration;
            lifetime.lastSeenSnapshot = snapshotIndex;
        }
    }
    return WreckwaterReplayPlaybackError::None;
}

[[nodiscard]] WreckwaterReplayVisualPose poseOf(
    const network::WreckwaterEntityState& entity) noexcept {
    return {
        .sector = entity.sector,
        .localPosition = entity.localPosition,
        .orientation = entity.orientation,
        .linearVelocity = entity.linearVelocity,
        .angularVelocity = entity.angularVelocity,
    };
}

[[nodiscard]] bool sameLogicalIdentity(
    const network::WreckwaterEntityState& lhs,
    const network::WreckwaterEntityState& rhs) noexcept {
    if (lhs.kind != rhs.kind) return false;
    if (lhs.kind == network::WreckwaterEntityKind::Cargo) {
        return lhs.cargo.cargoId == rhs.cargo.cargoId
            && lhs.cargo.generation == rhs.cargo.generation;
    }
    return lhs.skiff.skiffId == rhs.skiff.skiffId
        && lhs.skiff.generation == rhs.skiff.generation;
}

[[nodiscard]] bool continuousEntity(
    const network::WreckwaterEntityState& older,
    const network::WreckwaterEntityState& newer) noexcept {
    return older.netEntityId == newer.netEntityId
        && older.netGeneration == newer.netGeneration
        && sameLogicalIdentity(older, newer);
}

[[nodiscard]] bool interpolationCompatible(
    const network::WreckwaterEntityState& older,
    const network::WreckwaterEntityState& newer) noexcept {
    return continuousEntity(older, newer)
        && older.crew == newer.crew
        && older.shape == newer.shape
        && older.dimensions == newer.dimensions
        && older.packedMaterialFlags == newer.packedMaterialFlags
        && older.cargo == newer.cargo
        && older.skiff == newer.skiff
        && older.attachment == newer.attachment
        && older.reserved == newer.reserved;
}

[[nodiscard]] const network::WreckwaterEntityState* findByNetId(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    network::NetEntityId id) noexcept {
    const auto found = std::lower_bound(
        snapshot.entities.begin(), snapshot.entities.end(), id,
        [](const network::WreckwaterEntityState& entity,
           network::NetEntityId candidate) {
            return entity.netEntityId < candidate;
        });
    return found != snapshot.entities.end()
            && found->netEntityId == id
        ? &*found : nullptr;
}

struct CertifiedLogicalState {
    uint32_t entityCount = 0u;
    std::array<
        network::WreckwaterEntityState,
        network::kWreckwaterMaximumSnapshotEntities>
        entities{};
};

[[nodiscard]] CertifiedLogicalState logicalStateOf(
    const network::WreckwaterCertifiedSnapshot& snapshot) noexcept {
    CertifiedLogicalState state;
    state.entityCount =
        static_cast<uint32_t>(snapshot.entities.size());
    for (uint32_t index = 0u; index < state.entityCount; ++index) {
        state.entities[index] = snapshot.entities[index];
    }
    return state;
}

[[nodiscard]] network::WreckwaterEntityState* findCargo(
    CertifiedLogicalState& state,
    uint32_t id, uint32_t generation) noexcept {
    for (uint32_t index = 0u; index < state.entityCount; ++index) {
        network::WreckwaterEntityState& entity = state.entities[index];
        if (entity.kind == network::WreckwaterEntityKind::Cargo
            && entity.cargo.cargoId == id
            && entity.cargo.generation == generation) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] network::WreckwaterEntityState* findSkiff(
    CertifiedLogicalState& state,
    uint32_t id, uint32_t generation) noexcept {
    for (uint32_t index = 0u; index < state.entityCount; ++index) {
        network::WreckwaterEntityState& entity = state.entities[index];
        if (entity.kind == network::WreckwaterEntityKind::Skiff
            && entity.skiff.skiffId == id
            && entity.skiff.generation == generation) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] const network::WreckwaterEntityState* findLogicalEntity(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    const network::WreckwaterEntityState& expected) noexcept {
    const auto position = std::find_if(
        snapshot.entities.begin(), snapshot.entities.end(),
        [&expected](const network::WreckwaterEntityState& candidate) {
            if (candidate.kind != expected.kind) return false;
            return candidate.kind == network::WreckwaterEntityKind::Cargo
                ? candidate.cargo.cargoId
                    == expected.cargo.cargoId
                : candidate.skiff.skiffId
                    == expected.skiff.skiffId;
        });
    return position == snapshot.entities.end() ? nullptr : &*position;
}

[[nodiscard]] bool logicalStateMatches(
    const CertifiedLogicalState& expected,
    const network::WreckwaterCertifiedSnapshot& certified) noexcept {
    if (expected.entityCount != certified.entities.size()) return false;
    for (uint32_t index = 0u;
         index < expected.entityCount; ++index) {
        const network::WreckwaterEntityState& prior =
            expected.entities[index];
        const network::WreckwaterEntityState* current =
            findLogicalEntity(certified, prior);
        if (current == nullptr || current->crew != prior.crew) {
            return false;
        }
        if (prior.kind == network::WreckwaterEntityKind::Cargo) {
            if (current->cargo != prior.cargo
                || current->attachment != prior.attachment) {
                return false;
            }
        } else if (current->skiff != prior.skiff) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validRevisionTransition(
    const network::WreckwaterEntityState& cargo,
    const MatchEvent& event) noexcept {
    return cargo.cargo.revision
            != std::numeric_limits<uint32_t>::max()
        && event.observedCargoRevision == cargo.cargo.revision
        && event.cargoRevision == cargo.cargo.revision + 1u;
}

[[nodiscard]] network::WreckwaterCrew networkCrew(
    CrewId crew) noexcept {
    return static_cast<network::WreckwaterCrew>(enumWord(crew));
}

void makeCargoUntowed(
    network::WreckwaterEntityState& cargo,
    network::WreckwaterCargoDisposition disposition,
    network::WreckwaterCrew owner) noexcept {
    cargo.crew = owner;
    cargo.cargo.disposition = disposition;
    cargo.cargo.ownerCrew = owner;
    cargo.cargo.towingSkiffId = 0u;
    cargo.cargo.towingSkiffGeneration = 0u;
    cargo.attachment = {};
}

[[nodiscard]] bool applyCertifiedEvent(
    CertifiedLogicalState& state,
    const MatchEvent& event) noexcept {
    switch (event.type) {
        case MatchEventType::CargoTowAttached: {
            network::WreckwaterEntityState* cargo =
                findCargo(
                    state, event.cargoId, event.cargoGeneration);
            network::WreckwaterEntityState* target =
                findSkiff(
                    state, event.targetSkiff,
                    event.targetSkiffGeneration);
            const network::WreckwaterCrew owner =
                networkCrew(event.crew);
            if (cargo == nullptr || target == nullptr
                || cargo->cargo.disposition
                    != network::WreckwaterCargoDisposition::Free
                || target->crew != owner
                || target->skiff.disposition
                    != network::WreckwaterSkiffDisposition::Active
                || !validRevisionTransition(*cargo, event)) {
                return false;
            }
            ++cargo->cargo.revision;
            cargo->crew = owner;
            cargo->cargo.disposition =
                network::WreckwaterCargoDisposition::Towed;
            cargo->cargo.ownerCrew = owner;
            cargo->cargo.towingSkiffId = event.targetSkiff;
            cargo->cargo.towingSkiffGeneration =
                event.targetSkiffGeneration;
            cargo->attachment = {
                .attachmentId = event.resultingAttachmentId,
                .generation = event.resultingAttachmentGeneration,
                .state =
                    network::WreckwaterAttachmentState::Attached,
            };
            return true;
        }
        case MatchEventType::CargoTowCut:
        case MatchEventType::CargoBanked: {
            network::WreckwaterEntityState* cargo =
                findCargo(
                    state, event.cargoId, event.cargoGeneration);
            network::WreckwaterEntityState* source =
                findSkiff(
                    state, event.sourceSkiff,
                    event.sourceSkiffGeneration);
            if (cargo == nullptr || source == nullptr
                || cargo->cargo.disposition
                    != network::WreckwaterCargoDisposition::Towed
                || cargo->cargo.towingSkiffId
                    != event.sourceSkiff
                || cargo->cargo.towingSkiffGeneration
                    != event.sourceSkiffGeneration
                || cargo->attachment.attachmentId
                    != event.sourceAttachmentId
                || cargo->attachment.generation
                    != event.sourceAttachmentGeneration
                || source->skiff.disposition
                    != network::WreckwaterSkiffDisposition::Active
                || source->crew != cargo->cargo.ownerCrew
                || !validRevisionTransition(*cargo, event)) {
                return false;
            }
            if ((event.type == MatchEventType::CargoBanked
                    || event.type == MatchEventType::CargoTowCut)
                && source->crew != networkCrew(event.crew)) {
                return false;
            }
            ++cargo->cargo.revision;
            makeCargoUntowed(
                *cargo,
                event.type == MatchEventType::CargoBanked
                    ? network::WreckwaterCargoDisposition::Banked
                    : network::WreckwaterCargoDisposition::Free,
                event.type == MatchEventType::CargoBanked
                    ? networkCrew(event.crew)
                    : network::WreckwaterCrew::None);
            return true;
        }
        case MatchEventType::CargoStolen: {
            network::WreckwaterEntityState* cargo =
                findCargo(
                    state, event.cargoId, event.cargoGeneration);
            network::WreckwaterEntityState* source =
                findSkiff(
                    state, event.sourceSkiff,
                    event.sourceSkiffGeneration);
            network::WreckwaterEntityState* target =
                findSkiff(
                    state, event.targetSkiff,
                    event.targetSkiffGeneration);
            const network::WreckwaterCrew owner =
                networkCrew(event.crew);
            if (cargo == nullptr || source == nullptr
                || target == nullptr
                || cargo->cargo.disposition
                    != network::WreckwaterCargoDisposition::Towed
                || cargo->cargo.ownerCrew == owner
                || cargo->cargo.towingSkiffId
                    != event.sourceSkiff
                || cargo->cargo.towingSkiffGeneration
                    != event.sourceSkiffGeneration
                || cargo->attachment.attachmentId
                    != event.sourceAttachmentId
                || cargo->attachment.generation
                    != event.sourceAttachmentGeneration
                || source->skiff.disposition
                    != network::WreckwaterSkiffDisposition::Active
                || source->crew != cargo->cargo.ownerCrew
                || target->crew != owner
                || target->skiff.disposition
                    != network::WreckwaterSkiffDisposition::Active
                || !validRevisionTransition(*cargo, event)) {
                return false;
            }
            ++cargo->cargo.revision;
            cargo->crew = owner;
            cargo->cargo.ownerCrew = owner;
            cargo->cargo.towingSkiffId = event.targetSkiff;
            cargo->cargo.towingSkiffGeneration =
                event.targetSkiffGeneration;
            cargo->attachment = {
                .attachmentId = event.resultingAttachmentId,
                .generation = event.resultingAttachmentGeneration,
                .state =
                    network::WreckwaterAttachmentState::Attached,
            };
            return true;
        }
        case MatchEventType::AttachmentBroken:
        case MatchEventType::CargoLost: {
            network::WreckwaterEntityState* cargo =
                findCargo(
                    state, event.cargoId, event.cargoGeneration);
            if (cargo == nullptr
                || !validRevisionTransition(*cargo, event)) {
                return false;
            }
            const bool wasTowed =
                cargo->cargo.disposition
                == network::WreckwaterCargoDisposition::Towed;
            if (event.type == MatchEventType::AttachmentBroken
                && !wasTowed) {
                return false;
            }
            if (wasTowed) {
                network::WreckwaterEntityState* source =
                    findSkiff(
                        state, event.sourceSkiff,
                        event.sourceSkiffGeneration);
                if (cargo->cargo.towingSkiffId
                        != event.sourceSkiff
                    || cargo->cargo.towingSkiffGeneration
                        != event.sourceSkiffGeneration
                    || cargo->attachment.attachmentId
                        != event.sourceAttachmentId
                    || cargo->attachment.generation
                        != event.sourceAttachmentGeneration
                    || source == nullptr
                    || source->skiff.disposition
                        != network::WreckwaterSkiffDisposition::Active
                    || source->crew != cargo->cargo.ownerCrew) {
                    return false;
                }
            } else if (event.sourceSkiff != 0u
                || event.sourceAttachmentId != 0u
                || cargo->cargo.disposition
                    != network::WreckwaterCargoDisposition::Free) {
                return false;
            }
            if (event.worldEventType
                    == AuthoritativeWorldEventType::SkiffSunk) {
                network::WreckwaterEntityState* source =
                    findSkiff(
                        state, event.sourceSkiff,
                        event.sourceSkiffGeneration);
                if (!wasTowed || source == nullptr
                    || networkCrew(event.crew) != source->crew) {
                    return false;
                }
            } else if (event.crew != CrewId::None) {
                return false;
            }
            ++cargo->cargo.revision;
            makeCargoUntowed(
                *cargo,
                event.type == MatchEventType::CargoLost
                    ? network::WreckwaterCargoDisposition::Lost
                    : network::WreckwaterCargoDisposition::Free,
                network::WreckwaterCrew::None);
            return true;
        }
        case MatchEventType::SkiffSunk: {
            network::WreckwaterEntityState* skiff =
                findSkiff(
                    state, event.sourceSkiff,
                    event.sourceSkiffGeneration);
            if (skiff == nullptr
                || skiff->skiff.disposition
                    != network::WreckwaterSkiffDisposition::Active
                || skiff->crew != networkCrew(event.crew)) {
                return false;
            }
            skiff->skiff.disposition =
                network::WreckwaterSkiffDisposition::Sunk;
            return true;
        }
        case MatchEventType::SkiffRespawned: {
            network::WreckwaterEntityState* skiff =
                findSkiff(
                    state, event.sourceSkiff,
                    event.sourceSkiffGeneration);
            if (skiff == nullptr
                || skiff->skiff.disposition
                    != network::WreckwaterSkiffDisposition::Sunk
                || skiff->crew != networkCrew(event.crew)) {
                return false;
            }
            skiff->skiff.generation =
                event.targetSkiffGeneration;
            skiff->skiff.disposition =
                network::WreckwaterSkiffDisposition::Active;
            return true;
        }
        case MatchEventType::PlayerRegistered:
        case MatchEventType::PlayerConnected:
        case MatchEventType::PlayerReconnected:
        case MatchEventType::PlayerDisconnected:
        case MatchEventType::MatchStarted:
        case MatchEventType::PhaseChanged:
        case MatchEventType::CommandRejected:
        case MatchEventType::ScoreChanged:
        case MatchEventType::MatchFinished:
        case MatchEventType::WorldEventRejected:
        case MatchEventType::AuthorityFault:
            return true;
    }
    return false;
}

[[nodiscard]] bool eventChangesLogicalState(
    MatchEventType type) noexcept {
    switch (type) {
        case MatchEventType::CargoTowAttached:
        case MatchEventType::CargoTowCut:
        case MatchEventType::CargoStolen:
        case MatchEventType::CargoBanked:
        case MatchEventType::AttachmentBroken:
        case MatchEventType::CargoLost:
        case MatchEventType::SkiffSunk:
        case MatchEventType::SkiffRespawned:
            return true;
        case MatchEventType::PlayerRegistered:
        case MatchEventType::PlayerConnected:
        case MatchEventType::PlayerReconnected:
        case MatchEventType::PlayerDisconnected:
        case MatchEventType::MatchStarted:
        case MatchEventType::PhaseChanged:
        case MatchEventType::CommandRejected:
        case MatchEventType::ScoreChanged:
        case MatchEventType::MatchFinished:
        case MatchEventType::WorldEventRejected:
        case MatchEventType::AuthorityFault:
            return false;
    }
    return false;
}

[[nodiscard]] bool snapshotHasCargoIdentity(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    uint32_t cargoId, uint32_t generation) noexcept {
    return std::any_of(
        snapshot.entities.begin(), snapshot.entities.end(),
        [cargoId, generation](
            const network::WreckwaterEntityState& entity) {
            return entity.kind == network::WreckwaterEntityKind::Cargo
                && entity.cargo.cargoId == cargoId
                && entity.cargo.generation == generation;
        });
}

[[nodiscard]] bool snapshotHasSkiffIdentity(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    uint32_t skiffId, uint32_t generation) noexcept {
    return std::any_of(
        snapshot.entities.begin(), snapshot.entities.end(),
        [skiffId, generation](
            const network::WreckwaterEntityState& entity) {
            return entity.kind == network::WreckwaterEntityKind::Skiff
                && entity.skiff.skiffId == skiffId
                && entity.skiff.generation == generation;
        });
}

[[nodiscard]] bool snapshotHasAttachmentIdentity(
    const network::WreckwaterCertifiedSnapshot& snapshot,
    uint64_t attachmentId, uint32_t generation) noexcept {
    return std::any_of(
        snapshot.entities.begin(), snapshot.entities.end(),
        [attachmentId, generation](
            const network::WreckwaterEntityState& entity) {
            return entity.attachment.attachmentId == attachmentId
                && entity.attachment.generation == generation;
        });
}

template <typename Predicate>
[[nodiscard]] bool certifiedNowOrImmediatelyBefore(
    size_t snapshotIndex,
    std::span<const network::WreckwaterCertifiedSnapshot> snapshots,
    Predicate predicate) noexcept {
    return predicate(snapshots[snapshotIndex])
        || (snapshotIndex != 0u
            && predicate(snapshots[snapshotIndex - 1u]));
}

[[nodiscard]] bool nonMutatingSubjectsCertified(
    const MatchEvent& event,
    size_t snapshotIndex,
    std::span<const network::WreckwaterCertifiedSnapshot> snapshots)
    noexcept {
    if (event.cargoId != 0u
        && !certifiedNowOrImmediatelyBefore(
            snapshotIndex, snapshots,
            [&event](
                const network::WreckwaterCertifiedSnapshot& snapshot) {
                return snapshotHasCargoIdentity(
                    snapshot, event.cargoId,
                    event.cargoGeneration);
            })) {
        return false;
    }
    const auto skiffCertified =
        [snapshotIndex, snapshots](
            uint32_t id, uint32_t generation) {
            return id == 0u
                || certifiedNowOrImmediatelyBefore(
                    snapshotIndex, snapshots,
                    [id, generation](
                        const network::WreckwaterCertifiedSnapshot&
                            snapshot) {
                        return snapshotHasSkiffIdentity(
                            snapshot, id, generation);
                    });
        };
    if (!skiffCertified(
            event.sourceSkiff, event.sourceSkiffGeneration)
        || !skiffCertified(
            event.targetSkiff, event.targetSkiffGeneration)) {
        return false;
    }
    if (event.sourceAttachmentId != 0u
        && !certifiedNowOrImmediatelyBefore(
            snapshotIndex, snapshots,
            [&event](
                const network::WreckwaterCertifiedSnapshot& snapshot) {
                return snapshotHasAttachmentIdentity(
                    snapshot, event.sourceAttachmentId,
                    event.sourceAttachmentGeneration);
            })) {
        return false;
    }
    return true;
}

[[nodiscard]] bool validateCertifiedTransitions(
    std::span<const MatchEvent> events,
    std::span<const network::WreckwaterCertifiedSnapshot> snapshots,
    std::span<const size_t> snapshotEventPrefixes) noexcept {
    if (snapshots.empty()
        || snapshots.size() != snapshotEventPrefixes.size()) {
        return false;
    }
    size_t priorPrefix = 0u;
    CertifiedLogicalState logical =
        logicalStateOf(snapshots.front());
    for (size_t snapshotIndex = 0u;
         snapshotIndex < snapshots.size(); ++snapshotIndex) {
        const size_t prefix = snapshotEventPrefixes[snapshotIndex];
        if (prefix < priorPrefix || prefix > events.size()) return false;
        const network::WreckwaterCertifiedSnapshot& certified =
            snapshots[snapshotIndex];
        if (snapshotIndex != 0u) {
            logical = logicalStateOf(snapshots[snapshotIndex - 1u]);
        }
        for (size_t eventIndex = priorPrefix;
             eventIndex < prefix; ++eventIndex) {
            const MatchEvent& event = events[eventIndex];
            if (event.sourcePhysicsTick != 0u
                && event.sourcePhysicsTick
                    > certified.physicsEvidenceTick) {
                return false;
            }
            if (snapshotIndex == 0u
                && eventChangesLogicalState(event.type)) {
                return false;
            }
            if (!eventChangesLogicalState(event.type)
                && !nonMutatingSubjectsCertified(
                    event, snapshotIndex, snapshots)) {
                return false;
            }
            if (snapshotIndex != 0u
                && !applyCertifiedEvent(logical, event)) {
                return false;
            }
        }
        if (!logicalStateMatches(logical, certified)) return false;
        priorPrefix = prefix;
    }
    return priorPrefix == events.size();
}

[[nodiscard]] bool validPhaseAdvance(
    MatchPhase from, MatchPhase to) noexcept {
    return (from == MatchPhase::Warmup && to == MatchPhase::Live)
        || (from == MatchPhase::Live
            && (to == MatchPhase::Overtime
                || to == MatchPhase::Finished))
        || (from == MatchPhase::Overtime
            && to == MatchPhase::Finished);
}

[[nodiscard]] bool validCertifiedMatchState(
    std::span<const MatchEvent> events,
    std::span<const network::WreckwaterCertifiedSnapshot> snapshots,
    std::span<const size_t> snapshotEventPrefixes) noexcept {
    if (events.empty() || snapshots.empty()
        || snapshots.size() != snapshotEventPrefixes.size()) {
        return false;
    }

    MatchPhase committedPhase = MatchPhase::Warmup;
    uint32_t committedCrewOneScore = 0u;
    uint32_t committedCrewTwoScore = 0u;
    size_t groupBegin = 0u;
    while (groupBegin < events.size()) {
        size_t groupEnd = groupBegin + 1u;
        while (groupEnd < events.size()
            && events[groupEnd].tick == events[groupBegin].tick) {
            ++groupEnd;
        }
        const MatchEvent& groupState = events[groupBegin];
        bool phaseChanged = false;
        bool crewOneScoreChanged = false;
        bool crewTwoScoreChanged = false;
        for (size_t index = groupBegin; index < groupEnd; ++index) {
            const MatchEvent& event = events[index];
            if (event.phase != groupState.phase
                || event.outcome != groupState.outcome
                || event.crewOneScore != groupState.crewOneScore
                || event.crewTwoScore != groupState.crewTwoScore) {
                return false;
            }
            if (event.type == MatchEventType::PhaseChanged) {
                if (phaseChanged) return false;
                phaseChanged = true;
            }
            if (event.type == MatchEventType::ScoreChanged) {
                if (event.crew == CrewId::CrewOne) {
                    crewOneScoreChanged = true;
                } else if (event.crew == CrewId::CrewTwo) {
                    crewTwoScoreChanged = true;
                } else {
                    return false;
                }
            }
            if (event.type == MatchEventType::MatchFinished
                && (index == groupBegin
                    || events[index - 1u].type
                        != MatchEventType::PhaseChanged
                    || event.phase != MatchPhase::Finished)) {
                return false;
            }
        }

        const bool advanced =
            groupState.phase != committedPhase;
        if (advanced
                ? (!phaseChanged
                    || !validPhaseAdvance(
                        committedPhase, groupState.phase))
                : phaseChanged) {
            return false;
        }
        const bool crewOneAdvanced =
            groupState.crewOneScore > committedCrewOneScore;
        const bool crewTwoAdvanced =
            groupState.crewTwoScore > committedCrewTwoScore;
        if (groupState.crewOneScore < committedCrewOneScore
            || groupState.crewTwoScore < committedCrewTwoScore
            || crewOneAdvanced != crewOneScoreChanged
            || crewTwoAdvanced != crewTwoScoreChanged) {
            return false;
        }
        committedPhase = groupState.phase;
        committedCrewOneScore = groupState.crewOneScore;
        committedCrewTwoScore = groupState.crewTwoScore;
        groupBegin = groupEnd;
    }

    for (size_t index = 0u; index < snapshots.size(); ++index) {
        const size_t prefix = snapshotEventPrefixes[index];
        if (prefix == 0u || prefix > events.size()) return false;
        const MatchEvent& state = events[prefix - 1u];
        const network::WreckwaterCertifiedSnapshot& snapshot =
            snapshots[index];
        if (enumWord(snapshot.phase) != enumWord(state.phase)
            || snapshot.crewOneScore != state.crewOneScore
            || snapshot.crewTwoScore != state.crewTwoScore
            || enumWord(snapshot.outcome) != enumWord(state.outcome)
            || snapshot.matchStateHash != state.stateHash) {
            return false;
        }
        network::WreckwaterCrew expectedWinner =
            network::WreckwaterCrew::None;
        if (state.outcome == MatchOutcomeType::CrewVictory) {
            if (state.crewOneScore == state.crewTwoScore) return false;
            expectedWinner =
                state.crewOneScore > state.crewTwoScore
                ? network::WreckwaterCrew::CrewOne
                : network::WreckwaterCrew::CrewTwo;
        } else if (
            state.outcome == MatchOutcomeType::Tie
            && state.crewOneScore != state.crewTwoScore) {
            return false;
        }
        if (snapshot.winner != expectedWinner) return false;
    }
    return true;
}

[[nodiscard]] network::WreckwaterVec3 lerpVector(
    const network::WreckwaterVec3& a,
    const network::WreckwaterVec3& b,
    double alpha) noexcept {
    const auto lerp = [alpha](float lhs, float rhs) {
        const double value = static_cast<double>(lhs)
            + (static_cast<double>(rhs) - static_cast<double>(lhs))
                * alpha;
        return network::canonicalWreckwaterFloat(
            static_cast<float>(value));
    };
    return {
        .x = lerp(a.x, b.x),
        .y = lerp(a.y, b.y),
        .z = lerp(a.z, b.z),
    };
}

[[nodiscard]] bool canonicalPositionFromRelative(
    const std::array<int32_t, 3>& baseSector,
    const network::WreckwaterVec3& baseLocal,
    const std::array<double, 3>& relativeMeters,
    std::array<int32_t, 3>& sector,
    network::WreckwaterVec3& local) noexcept {
    std::array<float*, 3> localComponents{&local.x, &local.y, &local.z};
    const std::array<float, 3> baseComponents{
        baseLocal.x, baseLocal.y, baseLocal.z};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        double value =
            static_cast<double>(baseComponents[axis])
            + relativeMeters[axis];
        if (!std::isfinite(value)) return false;

        const double shiftDouble = std::floor(
            (value
                - static_cast<double>(
                    network::kWreckwaterSectorLocalMinimum))
            / kSectorSizeMeters);
        const int64_t minimumShift =
            -int64_t{network::kWreckwaterMaximumSectorMagnitude}
            - int64_t{baseSector[axis]};
        const int64_t maximumShift =
            int64_t{network::kWreckwaterMaximumSectorMagnitude}
            - int64_t{baseSector[axis]};
        if (!std::isfinite(shiftDouble)
            || shiftDouble < static_cast<double>(minimumShift)
            || shiftDouble > static_cast<double>(maximumShift)) {
            return false;
        }
        const int64_t shift = static_cast<int64_t>(shiftDouble);
        int64_t candidateSector =
            int64_t{baseSector[axis]} + shift;
        value -= static_cast<double>(shift) * kSectorSizeMeters;
        float localValue = network::canonicalWreckwaterFloat(
            static_cast<float>(value));

        if (localValue
            >= network::kWreckwaterSectorLocalMaximum) {
            if (candidateSector
                == network::kWreckwaterMaximumSectorMagnitude) {
                return false;
            }
            ++candidateSector;
            localValue = network::canonicalWreckwaterFloat(
                localValue - static_cast<float>(kSectorSizeMeters));
        } else if (
            localValue < network::kWreckwaterSectorLocalMinimum) {
            if (candidateSector
                == -network::kWreckwaterMaximumSectorMagnitude) {
                return false;
            }
            --candidateSector;
            localValue = network::canonicalWreckwaterFloat(
                localValue + static_cast<float>(kSectorSizeMeters));
        }
        if (!network::isCanonicalWreckwaterFloat(localValue)
            || localValue
                < network::kWreckwaterSectorLocalMinimum
            || localValue
                >= network::kWreckwaterSectorLocalMaximum) {
            return false;
        }
        sector[axis] = static_cast<int32_t>(candidateSector);
        *localComponents[axis] = localValue;
    }
    return true;
}

[[nodiscard]] std::array<double, 3> relativePosition(
    const network::WreckwaterEntityState& origin,
    const network::WreckwaterEntityState& target) noexcept {
    const std::array<float, 3> originLocal{
        origin.localPosition.x,
        origin.localPosition.y,
        origin.localPosition.z,
    };
    const std::array<float, 3> targetLocal{
        target.localPosition.x,
        target.localPosition.y,
        target.localPosition.z,
    };
    std::array<double, 3> result{};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        const int64_t sectorDelta =
            int64_t{target.sector[axis]}
            - int64_t{origin.sector[axis]};
        result[axis] =
            static_cast<double>(sectorDelta) * kSectorSizeMeters
            + static_cast<double>(targetLocal[axis])
            - static_cast<double>(originLocal[axis]);
    }
    return result;
}

[[nodiscard]] network::WreckwaterQuaternion shortestSlerp(
    const network::WreckwaterQuaternion& from,
    const network::WreckwaterQuaternion& to,
    double alpha) noexcept {
    std::array<double, 4> a{
        static_cast<double>(from.x),
        static_cast<double>(from.y),
        static_cast<double>(from.z),
        static_cast<double>(from.w),
    };
    std::array<double, 4> b{
        static_cast<double>(to.x),
        static_cast<double>(to.y),
        static_cast<double>(to.z),
        static_cast<double>(to.w),
    };
    double dot = 0.0;
    for (size_t index = 0u; index < a.size(); ++index) {
        dot += a[index] * b[index];
    }
    if (dot < 0.0) {
        for (double& component : b) component = -component;
        dot = -dot;
    }
    dot = std::clamp(dot, 0.0, 1.0);

    std::array<double, 4> value{};
    if (dot > 0.9995) {
        for (size_t index = 0u; index < value.size(); ++index) {
            value[index] = a[index] + (b[index] - a[index]) * alpha;
        }
    } else {
        const double theta = std::acos(dot);
        const double sine = std::sin(theta);
        const double fromWeight =
            std::sin((1.0 - alpha) * theta) / sine;
        const double toWeight = std::sin(alpha * theta) / sine;
        for (size_t index = 0u; index < value.size(); ++index) {
            value[index] =
                a[index] * fromWeight + b[index] * toWeight;
        }
    }
    network::WreckwaterQuaternion result{
        .x = static_cast<float>(value[0]),
        .y = static_cast<float>(value[1]),
        .z = static_cast<float>(value[2]),
        .w = static_cast<float>(value[3]),
    };
    if (!network::canonicalizeWreckwaterQuaternion(result)) return from;
    return result;
}

[[nodiscard]] bool interpolatePose(
    const network::WreckwaterEntityState& older,
    const network::WreckwaterEntityState& newer,
    double alpha,
    double intervalTicks,
    WreckwaterReplayVisualPose& pose) noexcept {
    const std::array<double, 3> endpoint =
        relativePosition(older, newer);
    const double seconds =
        intervalTicks
        / static_cast<double>(kWreckwaterReplayTickRateHz);
    const double alphaSquared = alpha * alpha;
    const double alphaCubed = alphaSquared * alpha;
    const double h10 =
        alphaCubed - 2.0 * alphaSquared + alpha;
    const double h01 =
        -2.0 * alphaCubed + 3.0 * alphaSquared;
    const double h11 = alphaCubed - alphaSquared;
    const std::array<float, 3> olderVelocity{
        older.linearVelocity.x,
        older.linearVelocity.y,
        older.linearVelocity.z,
    };
    const std::array<float, 3> newerVelocity{
        newer.linearVelocity.x,
        newer.linearVelocity.y,
        newer.linearVelocity.z,
    };
    std::array<double, 3> relative{};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        double olderTangent =
            static_cast<double>(olderVelocity[axis]) * seconds;
        double newerTangent =
            static_cast<double>(newerVelocity[axis]) * seconds;
        const double delta = endpoint[axis];
        if (delta == 0.0) {
            olderTangent = 0.0;
            newerTangent = 0.0;
        } else {
            double olderSlope = olderTangent / delta;
            double newerSlope = newerTangent / delta;
            if (olderSlope < 0.0) olderSlope = 0.0;
            if (newerSlope < 0.0) newerSlope = 0.0;
            const double slopeLengthSquared =
                olderSlope * olderSlope
                + newerSlope * newerSlope;
            if (slopeLengthSquared > 9.0) {
                const double scale =
                    3.0 / std::sqrt(slopeLengthSquared);
                olderSlope *= scale;
                newerSlope *= scale;
            }
            olderTangent = olderSlope * delta;
            newerTangent = newerSlope * delta;
        }
        relative[axis] =
            h10 * olderTangent + h01 * delta
            + h11 * newerTangent;
    }
    if (!canonicalPositionFromRelative(
            older.sector, older.localPosition, relative,
            pose.sector, pose.localPosition)) {
        for (size_t axis = 0u; axis < relative.size(); ++axis) {
            relative[axis] = alpha * endpoint[axis];
        }
        if (!canonicalPositionFromRelative(
                older.sector, older.localPosition, relative,
                pose.sector, pose.localPosition)) {
            return false;
        }
    }
    pose.orientation =
        shortestSlerp(older.orientation, newer.orientation, alpha);
    pose.linearVelocity =
        lerpVector(older.linearVelocity, newer.linearVelocity, alpha);
    pose.angularVelocity =
        lerpVector(older.angularVelocity, newer.angularVelocity, alpha);
    return true;
}

[[nodiscard]] WreckwaterReplayAuthoritativeFrame authoritativeFrame(
    const network::WreckwaterCertifiedSnapshot& snapshot) noexcept {
    return {
        .schemaVersion = snapshot.schemaVersion,
        .flags = snapshot.flags,
        .sessionId = snapshot.sessionId,
        .matchId = snapshot.matchId,
        .worldId = snapshot.worldId,
        .worldEpoch = snapshot.worldEpoch,
        .authorityEpoch = snapshot.authorityEpoch,
        .snapshotSequence = snapshot.snapshotSequence,
        .applicationTick = snapshot.applicationTick,
        .physicsEvidenceTick = snapshot.physicsEvidenceTick,
        .phase = snapshot.phase,
        .crewOneScore = snapshot.crewOneScore,
        .crewTwoScore = snapshot.crewTwoScore,
        .outcome = snapshot.outcome,
        .winner = snapshot.winner,
        .matchStateHash = snapshot.matchStateHash,
        .eventStreamHash = snapshot.eventStreamHash,
        .serializedByteHash = snapshot.serializedByteHash,
    };
}

[[nodiscard]] uint64_t timeValue(
    WreckwaterReplayTime time) noexcept {
    return time.wholeTick * uint64_t{kWreckwaterReplaySubticksPerTick}
        + uint64_t{time.subTick};
}

[[nodiscard]] WreckwaterReplayTime timeFromValue(
    uint64_t value) noexcept {
    return {
        .wholeTick =
            value / uint64_t{kWreckwaterReplaySubticksPerTick},
        .subTick = static_cast<uint16_t>(
            value % uint64_t{kWreckwaterReplaySubticksPerTick}),
    };
}

[[nodiscard]] uint32_t saturatedAdd(
    uint32_t lhs, uint32_t rhs) noexcept {
    return rhs > std::numeric_limits<uint32_t>::max() - lhs
        ? std::numeric_limits<uint32_t>::max() : lhs + rhs;
}

[[nodiscard]] uint32_t baseCameraScore(
    const network::WreckwaterEntityState& entity) noexcept {
    if (entity.kind == network::WreckwaterEntityKind::Cargo) {
        switch (entity.cargo.disposition) {
            case network::WreckwaterCargoDisposition::Free:
                return 300u;
            case network::WreckwaterCargoDisposition::Towed:
                return 450u;
            case network::WreckwaterCargoDisposition::Banked:
                return 150u;
            case network::WreckwaterCargoDisposition::Lost:
                return 600u;
            case network::WreckwaterCargoDisposition::NotApplicable:
                return 0u;
        }
    }
    switch (entity.skiff.disposition) {
        case network::WreckwaterSkiffDisposition::Active:
            return 250u;
        case network::WreckwaterSkiffDisposition::Sunk:
            return 700u;
        case network::WreckwaterSkiffDisposition::NotApplicable:
            return 0u;
    }
    return 0u;
}

[[nodiscard]] uint32_t eventCameraWeight(
    MatchEventType type) noexcept {
    switch (type) {
        case MatchEventType::CargoTowAttached: return 7'000u;
        case MatchEventType::CargoTowCut: return 10'000u;
        case MatchEventType::CargoStolen: return 14'000u;
        case MatchEventType::CargoBanked: return 18'000u;
        case MatchEventType::ScoreChanged: return 11'000u;
        case MatchEventType::AttachmentBroken: return 12'000u;
        case MatchEventType::CargoLost: return 13'000u;
        case MatchEventType::SkiffSunk: return 16'000u;
        case MatchEventType::SkiffRespawned: return 6'000u;
        case MatchEventType::PhaseChanged: return 2'500u;
        case MatchEventType::MatchFinished: return 20'000u;
        case MatchEventType::AuthorityFault: return 20'000u;
        case MatchEventType::PlayerRegistered:
        case MatchEventType::PlayerConnected:
        case MatchEventType::PlayerReconnected:
        case MatchEventType::PlayerDisconnected:
        case MatchEventType::MatchStarted:
        case MatchEventType::CommandRejected:
        case MatchEventType::WorldEventRejected:
            return 0u;
    }
    return 0u;
}

[[nodiscard]] WreckwaterWreckCamShot shotForEvent(
    MatchEventType type, bool hasReason) noexcept {
    if (!hasReason) return WreckwaterWreckCamShot::Establishing;
    switch (type) {
        case MatchEventType::CargoTowAttached:
            return WreckwaterWreckCamShot::Objective;
        case MatchEventType::CargoStolen:
            return WreckwaterWreckCamShot::Duel;
        case MatchEventType::CargoTowCut:
        case MatchEventType::AttachmentBroken:
        case MatchEventType::CargoLost:
        case MatchEventType::SkiffSunk:
            return WreckwaterWreckCamShot::Impact;
        case MatchEventType::CargoBanked:
        case MatchEventType::ScoreChanged:
        case MatchEventType::MatchFinished:
        case MatchEventType::AuthorityFault:
            return WreckwaterWreckCamShot::Finish;
        case MatchEventType::SkiffRespawned:
            return WreckwaterWreckCamShot::Follow;
        case MatchEventType::PhaseChanged:
        case MatchEventType::MatchStarted:
            return WreckwaterWreckCamShot::Establishing;
        case MatchEventType::PlayerRegistered:
        case MatchEventType::PlayerConnected:
        case MatchEventType::PlayerReconnected:
        case MatchEventType::PlayerDisconnected:
        case MatchEventType::CommandRejected:
        case MatchEventType::WorldEventRejected:
            return WreckwaterWreckCamShot::Follow;
    }
    return WreckwaterWreckCamShot::Follow;
}

} // namespace

const char* wreckwaterReplayPlaybackErrorName(
    WreckwaterReplayPlaybackError error) noexcept {
    switch (error) {
        case WreckwaterReplayPlaybackError::None: return "none";
        case WreckwaterReplayPlaybackError::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterReplayPlaybackError::AlreadyInitialized:
            return "already initialized";
        case WreckwaterReplayPlaybackError::NotInitialized:
            return "not initialized";
        case WreckwaterReplayPlaybackError::NotFinalized:
            return "not finalized";
        case WreckwaterReplayPlaybackError::ArchiveDecodeFailure:
            return "archive decode failure";
        case WreckwaterReplayPlaybackError::AllocationFailed:
            return "allocation failed";
        case WreckwaterReplayPlaybackError::RecordCapacity:
            return "record capacity";
        case WreckwaterReplayPlaybackError::PayloadCapacity:
            return "payload capacity";
        case WreckwaterReplayPlaybackError::SnapshotCapacity:
            return "snapshot capacity";
        case WreckwaterReplayPlaybackError::EventCapacity:
            return "event capacity";
        case WreckwaterReplayPlaybackError::EntityCapacity:
            return "entity capacity";
        case WreckwaterReplayPlaybackError::LifetimeCapacity:
            return "lifetime capacity";
        case WreckwaterReplayPlaybackError::InvalidRecord:
            return "invalid record";
        case WreckwaterReplayPlaybackError::InvalidEvent:
            return "invalid event";
        case WreckwaterReplayPlaybackError::InvalidSnapshot:
            return "invalid snapshot";
        case WreckwaterReplayPlaybackError::InvalidIdentity:
            return "invalid identity";
        case WreckwaterReplayPlaybackError::ContentMismatch:
            return "content mismatch";
        case WreckwaterReplayPlaybackError::NonMonotonicRecord:
            return "nonmonotonic record";
        case WreckwaterReplayPlaybackError::NonMonotonicSnapshot:
            return "nonmonotonic snapshot";
        case WreckwaterReplayPlaybackError::GenerationAlias:
            return "generation alias";
        case WreckwaterReplayPlaybackError::PayloadHashMismatch:
            return "payload hash mismatch";
        case WreckwaterReplayPlaybackError::ReplayHashMismatch:
            return "replay hash mismatch";
        case WreckwaterReplayPlaybackError::InvalidTime:
            return "invalid time";
        case WreckwaterReplayPlaybackError::PositionOverflow:
            return "position overflow";
    }
    return "unknown";
}

WreckwaterReplayPlaybackLoadResult WreckwaterReplayPlayback::initialize(
    std::span<const std::byte> archiveBytes,
    const WreckwaterReplayPlaybackConfig& config) {
    if (initialized_) {
        return {
            .error =
                WreckwaterReplayPlaybackError::AlreadyInitialized};
    }
    if (!validPlaybackConfig(config)) {
        return {
            .error =
                WreckwaterReplayPlaybackError::InvalidConfiguration};
    }
#if defined(__cpp_exceptions)
    try {
#endif
        WreckwaterReplayReadResult decoded =
            WreckwaterReplayCodec::decode(
                archiveBytes, config.readLimits);
        if (!decoded) {
            return {
                .error =
                    WreckwaterReplayPlaybackError::ArchiveDecodeFailure,
                .archiveError = decoded.error,
                .failingRecord = decoded.failingRecord,
            };
        }
        return initializeDecoded(*decoded.archive, config);
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        return {
            .error = WreckwaterReplayPlaybackError::AllocationFailed};
    }
#endif
}

WreckwaterReplayPlaybackLoadResult WreckwaterReplayPlayback::initialize(
    const WreckwaterReplayRecorder& recorder,
    const WreckwaterReplayPlaybackConfig& config) {
    if (initialized_) {
        return {
            .error =
                WreckwaterReplayPlaybackError::AlreadyInitialized};
    }
    if (!recorder.initialized()) {
        return {
            .error =
                WreckwaterReplayPlaybackError::ArchiveDecodeFailure,
            .archiveError = WreckwaterReplayError::NotInitialized,
        };
    }
    if (!recorder.finalized()) {
        return {
            .error = WreckwaterReplayPlaybackError::NotFinalized,
            .archiveError = WreckwaterReplayError::NotFinalized,
        };
    }
#if defined(__cpp_exceptions)
    try {
#endif
        const WreckwaterReplayWriteResult encoded =
            WreckwaterReplayCodec::encode(recorder);
        if (!encoded) {
            return {
                .error =
                    WreckwaterReplayPlaybackError::ArchiveDecodeFailure,
                .archiveError = encoded.error,
            };
        }
        return initialize(encoded.bytes, config);
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        return {
            .error = WreckwaterReplayPlaybackError::AllocationFailed};
    }
#endif
}

WreckwaterReplayPlaybackLoadResult WreckwaterReplayPlayback::initialize(
    const WreckwaterReplayArchive& archive,
    const WreckwaterReplayPlaybackConfig& config) {
    if (initialized_) {
        return {
            .error =
                WreckwaterReplayPlaybackError::AlreadyInitialized};
    }
    if (!validPlaybackConfig(config)) {
        return {
            .error =
                WreckwaterReplayPlaybackError::InvalidConfiguration};
    }
#if defined(__cpp_exceptions)
    try {
#endif
        return initializeDecoded(archive, config);
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        return {
            .error = WreckwaterReplayPlaybackError::AllocationFailed};
    }
#endif
}

WreckwaterReplayPlaybackLoadResult
WreckwaterReplayPlayback::initializeDecoded(
    const WreckwaterReplayArchive& archive,
    const WreckwaterReplayPlaybackConfig& config) {
    WreckwaterReplayPlaybackLoadResult result;
    if (!validReplayConfig(archive.config)) {
        result.error =
            WreckwaterReplayPlaybackError::InvalidConfiguration;
        return result;
    }
    if (config.expectedContentHash != 0u
        && archive.config.contentHash
            != config.expectedContentHash) {
        result.error =
            WreckwaterReplayPlaybackError::ContentMismatch;
        return result;
    }
    if (archive.records.empty() || archive.payload.empty()) {
        result.error = WreckwaterReplayPlaybackError::NotFinalized;
        return result;
    }
    if (archive.records.size() > config.readLimits.maximumRecords
        || archive.records.size() > archive.config.maximumRecords) {
        result.error = WreckwaterReplayPlaybackError::RecordCapacity;
        return result;
    }
    if (archive.payload.size()
            > config.readLimits.maximumPayloadBytes
        || archive.payload.size()
            > archive.config.maximumPayloadBytes) {
        result.error = WreckwaterReplayPlaybackError::PayloadCapacity;
        return result;
    }
    const size_t archiveBudget =
        config.readLimits.maximumArchiveBytes
        - kWreckwaterReplayArchiveHeaderBytes;
    if (archive.payload.size() > archiveBudget
        || archive.records.size()
            > (archiveBudget - archive.payload.size())
                / kWreckwaterReplayRecordHeaderBytes) {
        result.error = WreckwaterReplayPlaybackError::PayloadCapacity;
        return result;
    }
    if (archive.summary.recordCount != archive.records.size()
        || archive.summary.payloadBytes != archive.payload.size()
        || archive.summary.replayHash == 0u) {
        result.error = WreckwaterReplayPlaybackError::InvalidRecord;
        return result;
    }

    size_t snapshotCount = 0u;
    size_t eventCount = 0u;
    for (const WreckwaterReplayRecord& record : archive.records) {
        if (record.type
            == WreckwaterReplayRecordType::CertifiedSnapshot) {
            ++snapshotCount;
        } else if (
            record.type == WreckwaterReplayRecordType::MatchEvent) {
            ++eventCount;
        } else {
            result.error = WreckwaterReplayPlaybackError::InvalidRecord;
            return result;
        }
    }
    if (snapshotCount == 0u || eventCount == 0u) {
        result.error = WreckwaterReplayPlaybackError::NotFinalized;
        return result;
    }
    if (snapshotCount > config.maximumSnapshots) {
        result.error =
            WreckwaterReplayPlaybackError::SnapshotCapacity;
        return result;
    }
    if (eventCount > config.maximumEvents) {
        result.error = WreckwaterReplayPlaybackError::EventCapacity;
        return result;
    }

    std::vector<network::WreckwaterCertifiedSnapshot> snapshots;
    std::vector<MatchEvent> events;
    std::vector<size_t> snapshotEventPrefixes;
    snapshots.reserve(snapshotCount);
    events.reserve(eventCount);
    snapshotEventPrefixes.reserve(snapshotCount);
    NetworkLifetimeMap networkLifetimes;
    LogicalLifetimeMap logicalLifetimes;
    AttachmentLifetimeMap attachmentLifetimes;
    std::map<PlayerId, PlayerConnectionLifetime> playerLifetimes;
    std::map<SeatLifetimeKey, PlayerId> seatLifetimes;
    std::map<uint64_t, ConnectionLifetimeOwner> connectionLifetimes;
    std::map<uint32_t, PlayerId> characterHandleLifetimes;
    std::map<std::pair<PlayerId, uint64_t>, CommandSourceLifetime>
        commandSources;
    std::map<std::pair<uint32_t, uint64_t>, WorldSourceLifetime>
        worldSources;
    size_t trackedLifetimeCount = 0u;

    uint64_t rollingHash = initialReplayHash(archive.config);
    uint32_t eventStreamRollingHash =
        wreckwaterInitialMatchEventStreamRollingHash(
            archive.config.matchConfigurationHash);
    uint64_t nextEventSequence = 1u;
    uint64_t nextSnapshotSequence = 1u;
    uint64_t lastApplicationTick = 0u;
    uint64_t firstApplicationTick = 0u;
    uint64_t lastSnapshotEvidenceTick = 0u;
    uint32_t lastSnapshotPhase = 0u;
    uint32_t lastCrewOneScore = 0u;
    uint32_t lastCrewTwoScore = 0u;
    size_t expectedPayloadOffset = 0u;
    uint64_t eventDensityTick = 0u;
    uint32_t eventsAtDensityTick = 0u;
    uint32_t lastEventPhase = 0u;
    uint32_t lastEventCrewOneScore = 0u;
    uint32_t lastEventCrewTwoScore = 0u;
    bool hasRecord = false;
    bool hasSnapshot = false;
    bool hasEvent = false;
    bool seenMatchStarted = false;
    bool seenMatchFinished = false;
    uint64_t matchFinishedTick = 0u;
    bool snapshotSeenAtApplicationTick = false;

    for (size_t index = 0u; index < archive.records.size(); ++index) {
        result.failingRecord = index;
        const WreckwaterReplayRecord& record = archive.records[index];
        if (!validRecordType(record.type)
            || record.flags != 0u
            || record.payloadSchemaVersion == 0u
            || record.sequence == 0u
            || record.payloadBytes == 0u
            || record.applicationTick
                > network::kWreckwaterMaximumApplicationTick
            || record.physicsEvidenceTick
                > network::kWreckwaterMaximumApplicationTick
            || record.payloadOffset != expectedPayloadOffset
            || record.payloadOffset > archive.payload.size()
            || record.payloadBytes
                > archive.payload.size() - record.payloadOffset) {
            result.error =
                WreckwaterReplayPlaybackError::InvalidRecord;
            return result;
        }
        if (hasRecord && record.applicationTick < lastApplicationTick) {
            result.error =
                WreckwaterReplayPlaybackError::NonMonotonicRecord;
            return result;
        }
        if (!hasRecord
            || record.applicationTick > lastApplicationTick) {
            snapshotSeenAtApplicationTick = false;
        } else if (
            record.type == WreckwaterReplayRecordType::MatchEvent
            && snapshotSeenAtApplicationTick) {
            result.error =
                WreckwaterReplayPlaybackError::NonMonotonicRecord;
            return result;
        }
        const uint64_t expectedSequence =
            record.type == WreckwaterReplayRecordType::MatchEvent
            ? nextEventSequence : nextSnapshotSequence;
        if (record.sequence != expectedSequence) {
            result.error =
                WreckwaterReplayPlaybackError::NonMonotonicRecord;
            return result;
        }

        const std::span<const std::byte> payload =
            archive.recordPayload(index);
        if (payload.size() != record.payloadBytes) {
            result.error =
                WreckwaterReplayPlaybackError::InvalidRecord;
            return result;
        }
        if (record.payloadHash
            != network::wreckwaterSerializedByteHash(payload)) {
            result.error =
                WreckwaterReplayPlaybackError::PayloadHashMismatch;
            return result;
        }
        const uint64_t expectedRollingHash =
            recordRollingHash(rollingHash, record, payload);
        if (record.rollingHash != expectedRollingHash) {
            result.error =
                WreckwaterReplayPlaybackError::ReplayHashMismatch;
            return result;
        }

        if (record.type == WreckwaterReplayRecordType::MatchEvent) {
            if (record.payloadSchemaVersion
                    != kWreckwaterMatchSchemaVersion
                || record.payloadBytes
                    != kWreckwaterReplayMatchEventBytes) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidEvent;
                return result;
            }
            MatchEvent event;
            if (!decodeEvent(payload, event)
                || !validEvent(event, archive.config)
                || !validEventContract(event)
                || event.eventSequence != record.sequence
                || event.tick != record.applicationTick
                || event.sourcePhysicsTick
                    != record.physicsEvidenceTick) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidEvent;
                return result;
            }
            if (hasEvent
                && (enumWord(event.phase) < lastEventPhase
                    || event.crewOneScore
                        < lastEventCrewOneScore
                    || event.crewTwoScore
                        < lastEventCrewTwoScore)) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidEvent;
                return result;
            }
            if (seenMatchFinished
                && (event.tick != matchFinishedTick
                    || (event.type
                            != MatchEventType::PlayerConnected
                        && event.type
                            != MatchEventType::PlayerReconnected
                        && event.type
                            != MatchEventType::PlayerDisconnected
                        && event.type
                            != MatchEventType::CommandRejected
                        && event.type
                            != MatchEventType::WorldEventRejected))) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidEvent;
                return result;
            }
            const WreckwaterReplayPlaybackError eventLifetimeError =
                validateEventLifetime(
                    event, config.maximumTrackedLifetimes,
                    trackedLifetimeCount, seenMatchStarted,
                    playerLifetimes, seatLifetimes,
                    connectionLifetimes, commandSources,
                    worldSources);
            if (eventLifetimeError
                != WreckwaterReplayPlaybackError::None) {
                result.error = eventLifetimeError;
                return result;
            }
            if (event.type == MatchEventType::MatchStarted) {
                if (seenMatchStarted
                    || playerLifetimes.size()
                        != 2u * kWreckwaterPlayersPerCrew
                    || seatLifetimes.size()
                        != 2u * kWreckwaterPlayersPerCrew
                    || !std::all_of(
                        playerLifetimes.begin(),
                        playerLifetimes.end(),
                        [](const auto& entry) {
                            return entry.second.registered
                                && entry.second.connected;
                        })) {
                    result.error =
                        WreckwaterReplayPlaybackError::InvalidEvent;
                    return result;
                }
                seenMatchStarted = true;
            } else if (
                event.type == MatchEventType::PhaseChanged
                || event.type == MatchEventType::CommandRejected
                || event.type == MatchEventType::CargoTowAttached
                || event.type == MatchEventType::CargoTowCut
                || event.type == MatchEventType::CargoStolen
                || event.type == MatchEventType::CargoBanked
                || event.type == MatchEventType::ScoreChanged
                || event.type == MatchEventType::MatchFinished
                || event.type == MatchEventType::WorldEventRejected
                || event.type == MatchEventType::AttachmentBroken
                || event.type == MatchEventType::CargoLost
                || event.type == MatchEventType::SkiffSunk
                || event.type == MatchEventType::SkiffRespawned
                || event.type == MatchEventType::AuthorityFault) {
                if (!seenMatchStarted) {
                    result.error =
                        WreckwaterReplayPlaybackError::InvalidEvent;
                    return result;
                }
            }
            if (event.type == MatchEventType::MatchFinished) {
                if (seenMatchFinished) {
                    result.error =
                        WreckwaterReplayPlaybackError::InvalidEvent;
                    return result;
                }
                seenMatchFinished = true;
                matchFinishedTick = event.tick;
            }
            if (!hasEvent || event.tick != eventDensityTick) {
                eventDensityTick = event.tick;
                eventsAtDensityTick = 0u;
            }
            if (eventsAtDensityTick
                >= config.maximumEventsPerTick) {
                result.error =
                    WreckwaterReplayPlaybackError::EventCapacity;
                return result;
            }
            ++eventsAtDensityTick;
            lastEventPhase = enumWord(event.phase);
            lastEventCrewOneScore = event.crewOneScore;
            lastEventCrewTwoScore = event.crewTwoScore;
            hasEvent = true;
            eventStreamRollingHash =
                wreckwaterAppendMatchEventStreamRollingHash(
                    eventStreamRollingHash, event);
            events.push_back(event);
            nextEventSequence =
                record.sequence
                    == std::numeric_limits<uint64_t>::max()
                ? 0u : record.sequence + 1u;
        } else {
            if (record.payloadSchemaVersion
                != network::kWreckwaterWireSchemaVersion) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidSnapshot;
                return result;
            }
            const network::WreckwaterSnapshotReadResult decoded =
                network::WreckwaterSnapshotCodec::decode(payload);
            if (!decoded) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidSnapshot;
                result.snapshotError = decoded.error;
                return result;
            }
            network::WreckwaterCertifiedSnapshot snapshot =
                *decoded.snapshot;
            result.failingSnapshot = snapshots.size();
            if (!snapshotIdentityMatches(snapshot, archive.config)) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidIdentity;
                return result;
            }
            if (snapshot.snapshotSequence != record.sequence
                || snapshot.applicationTick != record.applicationTick
                || snapshot.physicsEvidenceTick
                    != record.physicsEvidenceTick) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidSnapshot;
                return result;
            }
            if (snapshot.entities.size() > config.maximumEntities) {
                result.error =
                    WreckwaterReplayPlaybackError::EntityCapacity;
                return result;
            }
            if (nextEventSequence == 0u
                || nextEventSequence - 1u
                    > std::numeric_limits<uint32_t>::max()
                || snapshot.eventStreamHash
                    != wreckwaterFinalizeMatchEventStreamHash(
                        eventStreamRollingHash,
                        archive.config.matchConfigurationHash,
                        snapshot.matchStateHash,
                        nextEventSequence,
                        static_cast<uint32_t>(
                            nextEventSequence - 1u))) {
                result.error =
                    WreckwaterReplayPlaybackError::InvalidSnapshot;
                return result;
            }
            if (hasEvent
                && (enumWord(snapshot.phase) < lastEventPhase
                    || snapshot.crewOneScore
                        < lastEventCrewOneScore
                    || snapshot.crewTwoScore
                        < lastEventCrewTwoScore)) {
                result.error =
                    WreckwaterReplayPlaybackError::
                        NonMonotonicSnapshot;
                return result;
            }
            if (hasSnapshot
                && (snapshot.physicsEvidenceTick
                        <= lastSnapshotEvidenceTick
                    || enumWord(snapshot.phase) < lastSnapshotPhase
                    || snapshot.crewOneScore < lastCrewOneScore
                    || snapshot.crewTwoScore < lastCrewTwoScore)) {
                result.error =
                    WreckwaterReplayPlaybackError::
                        NonMonotonicSnapshot;
                return result;
            }
            if (snapshot.phase
                    == network::WreckwaterPhase::Finished
                && index + 1u != archive.records.size()) {
                result.error =
                    WreckwaterReplayPlaybackError::NotFinalized;
                return result;
            }
            const WreckwaterReplayPlaybackError characterError =
                validateCertifiedCharacters(
                    snapshot, config.maximumTrackedLifetimes,
                    trackedLifetimeCount, playerLifetimes,
                    characterHandleLifetimes);
            if (characterError
                != WreckwaterReplayPlaybackError::None) {
                result.error = characterError;
                return result;
            }
            const WreckwaterReplayPlaybackError lifetimeError =
                validateLifetimes(
                    snapshot, snapshots.size(),
                    config.maximumTrackedLifetimes,
                    trackedLifetimeCount,
                    networkLifetimes, logicalLifetimes,
                    attachmentLifetimes);
            if (lifetimeError
                != WreckwaterReplayPlaybackError::None) {
                result.error = lifetimeError;
                return result;
            }
            lastSnapshotEvidenceTick =
                snapshot.physicsEvidenceTick;
            lastSnapshotPhase = enumWord(snapshot.phase);
            lastCrewOneScore = snapshot.crewOneScore;
            lastCrewTwoScore = snapshot.crewTwoScore;
            hasSnapshot = true;
            snapshots.push_back(std::move(snapshot));
            snapshotEventPrefixes.push_back(events.size());
            snapshotSeenAtApplicationTick = true;
            nextSnapshotSequence =
                record.sequence
                    == std::numeric_limits<uint64_t>::max()
                ? 0u : record.sequence + 1u;
        }

        if (!hasRecord) firstApplicationTick = record.applicationTick;
        hasRecord = true;
        lastApplicationTick = record.applicationTick;
        expectedPayloadOffset += record.payloadBytes;
        rollingHash = expectedRollingHash;
    }

    if (expectedPayloadOffset != archive.payload.size()) {
        result.error = WreckwaterReplayPlaybackError::InvalidRecord;
        return result;
    }
    if (!seenMatchFinished
        || snapshotEventPrefixes.empty()
        || snapshotEventPrefixes.back() != events.size()) {
        result.error = WreckwaterReplayPlaybackError::NotFinalized;
        return result;
    }
    size_t cameraWindowBegin = 0u;
    for (size_t end = 0u; end < events.size(); ++end) {
        while (cameraWindowBegin < end
            && events[end].tick
                    - events[cameraWindowBegin].tick
                > config.cameraInterestTicks) {
            ++cameraWindowBegin;
        }
        if (end - cameraWindowBegin + 1u
            > config.maximumCameraEvents) {
            result.error =
                WreckwaterReplayPlaybackError::EventCapacity;
            return result;
        }
    }
    if (!std::all_of(
            commandSources.begin(), commandSources.end(),
            [](const auto& entry) {
                return validCommandSourceBundle(entry.second);
            })
        || !std::all_of(
            worldSources.begin(), worldSources.end(),
            [](const auto& entry) {
                return validWorldSourceBundle(entry.second);
            })) {
        result.error = WreckwaterReplayPlaybackError::InvalidEvent;
        return result;
    }
    if (!validCertifiedMatchState(
            events, snapshots, snapshotEventPrefixes)) {
        result.error =
            WreckwaterReplayPlaybackError::InvalidSnapshot;
        return result;
    }
    if (!validateCertifiedTransitions(
            events, snapshots, snapshotEventPrefixes)) {
        result.error =
            WreckwaterReplayPlaybackError::GenerationAlias;
        return result;
    }
    const network::WreckwaterCertifiedSnapshot& terminal =
        snapshots.back();
    if (archive.records.back().type
            != WreckwaterReplayRecordType::CertifiedSnapshot
        || terminal.phase != network::WreckwaterPhase::Finished
        || terminal.physicsEvidenceTick != terminal.applicationTick
        || archive.summary.firstApplicationTick != firstApplicationTick
        || archive.summary.lastApplicationTick != lastApplicationTick
        || archive.summary.finalMatchStateHash
            != terminal.matchStateHash
        || archive.summary.finalEventStreamHash
            != terminal.eventStreamHash) {
        result.error = WreckwaterReplayPlaybackError::NotFinalized;
        return result;
    }
    if (archive.summary.replayHash
        != finalReplayHash(rollingHash, archive.summary)) {
        result.error =
            WreckwaterReplayPlaybackError::ReplayHashMismatch;
        return result;
    }

    config_ = config;
    replayConfig_ = archive.config;
    replaySummary_ = archive.summary;
    snapshots_ = std::move(snapshots);
    events_ = std::move(events);
    snapshotEventPrefixes_ =
        std::move(snapshotEventPrefixes);
    initialized_ = true;
    WreckwaterReplayPlaybackFrame initialFrame;
    size_t initialVisibleEvents = 0u;
    const WreckwaterReplayPlaybackError initialError = buildFrame(
        firstTime(), initialFrame, initialVisibleEvents);
    if (initialError != WreckwaterReplayPlaybackError::None) {
        initialized_ = false;
        snapshots_.clear();
        events_.clear();
        snapshotEventPrefixes_.clear();
        result.error = initialError;
        return result;
    }
    frame_ = initialFrame;
    visibleEventCount_ = initialVisibleEvents;
    result.failingRecord = archive.records.size();
    result.failingSnapshot = snapshots_.size();
    return result;
}

WreckwaterReplayTime WreckwaterReplayPlayback::firstTime()
    const noexcept {
    return snapshots_.empty()
        ? WreckwaterReplayTime{}
        : WreckwaterReplayTime{
            .wholeTick = snapshots_.front().physicsEvidenceTick,
            .subTick = 0u,
        };
}

WreckwaterReplayTime WreckwaterReplayPlayback::lastTime()
    const noexcept {
    return snapshots_.empty()
        ? WreckwaterReplayTime{}
        : WreckwaterReplayTime{
            .wholeTick = snapshots_.back().physicsEvidenceTick,
            .subTick = 0u,
        };
}

const network::WreckwaterCertifiedSnapshot*
WreckwaterReplayPlayback::exactSnapshot() const noexcept {
    return frame_.exactSnapshot
            && frame_.exactSnapshotIndex < snapshots_.size()
        ? &snapshots_[frame_.exactSnapshotIndex] : nullptr;
}

WreckwaterReplayPlaybackStorageState
WreckwaterReplayPlayback::storageState() const noexcept {
    return {
        .snapshots = snapshots_.data(),
        .events = events_.data(),
        .snapshotEventPrefixes = snapshotEventPrefixes_.data(),
        .snapshotCapacity = snapshots_.capacity(),
        .eventCapacity = events_.capacity(),
        .snapshotEventPrefixCapacity =
            snapshotEventPrefixes_.capacity(),
    };
}

WreckwaterReplayPlaybackError WreckwaterReplayPlayback::buildFrame(
    WreckwaterReplayTime requested,
    WreckwaterReplayPlaybackFrame& output,
    size_t& visibleEvents) const noexcept {
    if (!initialized_ || snapshots_.empty()) {
        return WreckwaterReplayPlaybackError::NotInitialized;
    }
    if (requested.wholeTick
        > network::kWreckwaterMaximumApplicationTick) {
        return WreckwaterReplayPlaybackError::InvalidTime;
    }

    output = {};
    output.requestedTime = requested;
    const uint64_t requestedValue = timeValue(requested);
    const uint64_t firstValue = timeValue(firstTime());
    const uint64_t lastValue = timeValue(lastTime());
    const uint64_t evaluatedValue =
        std::clamp(requestedValue, firstValue, lastValue);
    output.evaluatedTime = timeFromValue(evaluatedValue);
    output.clampedToStart = requestedValue < firstValue;
    output.clampedToEnd = requestedValue > lastValue;

    const auto upper = std::upper_bound(
        snapshots_.begin(), snapshots_.end(), evaluatedValue,
        [](uint64_t value,
           const network::WreckwaterCertifiedSnapshot& snapshot) {
            const uint64_t snapshotValue =
                snapshot.physicsEvidenceTick
                * uint64_t{kWreckwaterReplaySubticksPerTick};
            return value < snapshotValue;
        });
    size_t olderIndex =
        upper == snapshots_.begin()
        ? 0u
        : static_cast<size_t>(
            std::distance(snapshots_.begin(), upper) - 1);
    const uint64_t olderValue =
        snapshots_[olderIndex].physicsEvidenceTick
        * uint64_t{kWreckwaterReplaySubticksPerTick};
    const bool exact = evaluatedValue == olderValue;
    const size_t newerIndex =
        exact || olderIndex + 1u >= snapshots_.size()
        ? kWreckwaterReplayNoSnapshot : olderIndex + 1u;

    output.exactSnapshot = exact;
    output.exactSnapshotIndex =
        exact ? olderIndex : kWreckwaterReplayNoSnapshot;
    output.olderSnapshotIndex = olderIndex;
    output.newerSnapshotIndex = newerIndex;
    const network::WreckwaterCertifiedSnapshot& authoritative =
        snapshots_[olderIndex];
    output.authoritative = authoritativeFrame(authoritative);
    output.entityCount =
        static_cast<uint32_t>(authoritative.entities.size());

    double alpha = 0.0;
    double intervalTicks = 0.0;
    if (!exact) {
        if (newerIndex == kWreckwaterReplayNoSnapshot) {
            return WreckwaterReplayPlaybackError::InvalidTime;
        }
        const uint64_t newerValue =
            snapshots_[newerIndex].physicsEvidenceTick
            * uint64_t{kWreckwaterReplaySubticksPerTick};
        if (newerValue <= olderValue
            || evaluatedValue <= olderValue
            || evaluatedValue >= newerValue) {
            return WreckwaterReplayPlaybackError::InvalidTime;
        }
        alpha =
            static_cast<double>(evaluatedValue - olderValue)
            / static_cast<double>(newerValue - olderValue);
        intervalTicks = static_cast<double>(
            snapshots_[newerIndex].physicsEvidenceTick
            - authoritative.physicsEvidenceTick);
    }

    for (uint32_t index = 0u; index < output.entityCount; ++index) {
        const network::WreckwaterEntityState& entity =
            authoritative.entities[index];
        WreckwaterReplayPlaybackEntity& sampled =
            output.entities[index];
        sampled.netEntityId = entity.netEntityId;
        sampled.netGeneration = entity.netGeneration;
        sampled.motion = WreckwaterReplayVisualMotion::Exact;
        sampled.visualPose = poseOf(entity);
        sampled.authoritativeState = entity;
        if (exact) continue;

        const network::WreckwaterEntityState* newerEntity =
            findByNetId(snapshots_[newerIndex], entity.netEntityId);
        if (newerEntity == nullptr
            || !interpolationCompatible(entity, *newerEntity)) {
            sampled.motion =
                WreckwaterReplayVisualMotion::HeldTopology;
            continue;
        }
        if (!interpolatePose(
                entity, *newerEntity, alpha, intervalTicks,
                sampled.visualPose)) {
            return WreckwaterReplayPlaybackError::PositionOverflow;
        }
        sampled.motion =
            WreckwaterReplayVisualMotion::Interpolated;
    }

    if (olderIndex >= snapshotEventPrefixes_.size()) {
        return WreckwaterReplayPlaybackError::InvalidRecord;
    }
    visibleEvents = snapshotEventPrefixes_[olderIndex];
    return WreckwaterReplayPlaybackError::None;
}

WreckwaterReplayPlaybackError WreckwaterReplayPlayback::seek(
    WreckwaterReplayTime requested) noexcept {
    WreckwaterReplayPlaybackFrame candidate;
    size_t candidateVisibleEvents = 0u;
    const WreckwaterReplayPlaybackError error =
        buildFrame(requested, candidate, candidateVisibleEvents);
    if (error != WreckwaterReplayPlaybackError::None) return error;
    frame_ = candidate;
    visibleEventCount_ = candidateVisibleEvents;
    return WreckwaterReplayPlaybackError::None;
}

WreckwaterReplayStepResult WreckwaterReplayPlayback::stepSubticks(
    int64_t deltaSubticks) noexcept {
    WreckwaterReplayStepResult result;
    if (!initialized_) {
        result.error = WreckwaterReplayPlaybackError::NotInitialized;
        return result;
    }
    const uint64_t oldValue = timeValue(frame_.evaluatedTime);
    const size_t oldVisibleEvents = visibleEventCount_;
    const uint64_t firstValue = timeValue(firstTime());
    const uint64_t lastValue = timeValue(lastTime());
    uint64_t requestedValue = oldValue;
    if (deltaSubticks > 0) {
        const uint64_t delta = static_cast<uint64_t>(deltaSubticks);
        if (delta > lastValue - oldValue) {
            requestedValue = lastValue;
            result.clamped = true;
        } else {
            requestedValue = oldValue + delta;
        }
    } else if (deltaSubticks < 0) {
        const uint64_t magnitude =
            static_cast<uint64_t>(-(deltaSubticks + 1)) + 1u;
        if (magnitude > oldValue - firstValue) {
            requestedValue = firstValue;
            result.clamped = true;
        } else {
            requestedValue = oldValue - magnitude;
        }
    }

    result.error = seek(timeFromValue(requestedValue));
    if (result.error != WreckwaterReplayPlaybackError::None) return result;
    if (requestedValue > oldValue) {
        result.direction = WreckwaterReplayStepDirection::Forward;
        result.eventBegin = oldVisibleEvents;
        result.eventEnd = visibleEventCount_;
    } else if (requestedValue < oldValue) {
        result.direction = WreckwaterReplayStepDirection::Reverse;
        result.eventBegin = visibleEventCount_;
        result.eventEnd = oldVisibleEvents;
    } else {
        result.eventBegin = visibleEventCount_;
        result.eventEnd = visibleEventCount_;
    }
    return result;
}

WreckwaterWreckCamSelection
WreckwaterReplayPlayback::wreckCamSelection() const noexcept {
    WreckwaterWreckCamSelection result;
    result.time = frame_.evaluatedTime;
    if (!initialized_ || frame_.entityCount == 0u) return result;

    result.candidateCount = frame_.entityCount;
    for (uint32_t index = 0u; index < frame_.entityCount; ++index) {
        const WreckwaterReplayPlaybackEntity& entity =
            frame_.entities[index];
        result.candidates[index] = {
            .netEntityId = entity.netEntityId,
            .netGeneration = entity.netGeneration,
            .score = baseCameraScore(entity.authoritativeState),
        };
    }

    const auto addContribution =
        [&result](
            uint32_t index, uint32_t contribution,
            const MatchEvent& event) noexcept {
            if (index >= result.candidateCount || contribution == 0u) {
                return;
            }
            WreckwaterWreckCamCandidate& candidate =
                result.candidates[index];
            candidate.score =
                saturatedAdd(candidate.score, contribution);
            if (contribution > candidate.reasonContribution
                || (contribution == candidate.reasonContribution
                    && event.eventSequence
                        > candidate.reasonEventSequence)) {
                candidate.reason = event.type;
                candidate.reasonEventSequence = event.eventSequence;
                candidate.reasonContribution = contribution;
            }
        };

    const auto findCargo =
        [this](uint32_t id, uint32_t generation) noexcept {
            if (id == 0u || generation == 0u) {
                return frame_.entityCount;
            }
            for (uint32_t index = 0u;
                 index < frame_.entityCount; ++index) {
                const network::WreckwaterEntityState& entity =
                    frame_.entities[index].authoritativeState;
                if (entity.kind
                        == network::WreckwaterEntityKind::Cargo
                    && entity.cargo.cargoId == id
                    && entity.cargo.generation == generation) {
                    return index;
                }
            }
            return frame_.entityCount;
        };
    const auto findSkiff =
        [this](uint32_t id, uint32_t generation) noexcept {
            if (id == 0u || generation == 0u) {
                return frame_.entityCount;
            }
            for (uint32_t index = 0u;
                 index < frame_.entityCount; ++index) {
                const network::WreckwaterEntityState& entity =
                    frame_.entities[index].authoritativeState;
                if (entity.kind
                        == network::WreckwaterEntityKind::Skiff
                    && entity.skiff.skiffId == id
                    && entity.skiff.generation == generation) {
                    return index;
                }
            }
            return frame_.entityCount;
        };

    const uint64_t currentTick =
        frame_.authoritative.applicationTick;
    const uint64_t window = config_.cameraInterestTicks;
    const uint64_t firstInterestingTick =
        currentTick > window ? currentTick - window : 0u;
    const auto firstEvent = std::lower_bound(
        events_.begin(),
        events_.begin()
            + static_cast<std::ptrdiff_t>(visibleEventCount_),
        firstInterestingTick,
        [](const MatchEvent& event, uint64_t tick) {
            return event.tick < tick;
        });
    for (auto eventPosition = firstEvent;
         eventPosition
            != events_.begin()
                + static_cast<std::ptrdiff_t>(visibleEventCount_);
         ++eventPosition) {
        const MatchEvent& event = *eventPosition;
        const uint32_t baseWeight = eventCameraWeight(event.type);
        if (baseWeight == 0u || event.tick > currentTick) continue;
        const uint64_t age = currentTick - event.tick;
        if (age > window) continue;
        const uint64_t numerator = window - age + 1u;
        const uint64_t denominator = window + 1u;
        const uint32_t contribution = static_cast<uint32_t>(
            (uint64_t{baseWeight} * numerator) / denominator);
        const uint32_t half = contribution / 2u;
        const uint32_t threeQuarters =
            contribution - contribution / 4u;

        switch (event.type) {
            case MatchEventType::CargoTowAttached:
                addContribution(
                    findCargo(event.cargoId, event.cargoGeneration),
                    contribution, event);
                addContribution(
                    findSkiff(
                        event.targetSkiff,
                        event.targetSkiffGeneration),
                    half, event);
                break;
            case MatchEventType::CargoTowCut:
            case MatchEventType::AttachmentBroken:
            case MatchEventType::CargoLost:
                addContribution(
                    findCargo(event.cargoId, event.cargoGeneration),
                    contribution, event);
                addContribution(
                    findSkiff(
                        event.sourceSkiff,
                        event.sourceSkiffGeneration),
                    threeQuarters, event);
                break;
            case MatchEventType::CargoStolen:
                addContribution(
                    findCargo(event.cargoId, event.cargoGeneration),
                    contribution, event);
                addContribution(
                    findSkiff(
                        event.sourceSkiff,
                        event.sourceSkiffGeneration),
                    threeQuarters, event);
                addContribution(
                    findSkiff(
                        event.targetSkiff,
                        event.targetSkiffGeneration),
                    threeQuarters, event);
                break;
            case MatchEventType::CargoBanked:
            case MatchEventType::ScoreChanged:
                addContribution(
                    findCargo(event.cargoId, event.cargoGeneration),
                    contribution, event);
                addContribution(
                    findSkiff(
                        event.sourceSkiff,
                        event.sourceSkiffGeneration),
                    half, event);
                break;
            case MatchEventType::SkiffSunk:
                addContribution(
                    findSkiff(
                        event.sourceSkiff,
                        event.sourceSkiffGeneration),
                    contribution, event);
                break;
            case MatchEventType::SkiffRespawned:
                addContribution(
                    findSkiff(
                        event.targetSkiff,
                        event.targetSkiffGeneration),
                    contribution, event);
                break;
            case MatchEventType::MatchFinished:
                for (uint32_t index = 0u;
                     index < frame_.entityCount; ++index) {
                    const network::WreckwaterEntityState& entity =
                        frame_.entities[index].authoritativeState;
                    const bool winningSkiff =
                        entity.kind
                            == network::WreckwaterEntityKind::Skiff
                        && enumWord(entity.crew)
                            == enumWord(event.crew)
                        && event.crew != CrewId::None;
                    addContribution(
                        index,
                        winningSkiff ? contribution : half,
                        event);
                }
                break;
            case MatchEventType::PhaseChanged:
            case MatchEventType::AuthorityFault:
                for (uint32_t index = 0u;
                     index < frame_.entityCount; ++index) {
                    addContribution(index, half, event);
                }
                break;
            case MatchEventType::PlayerRegistered:
            case MatchEventType::PlayerConnected:
            case MatchEventType::PlayerReconnected:
            case MatchEventType::PlayerDisconnected:
            case MatchEventType::MatchStarted:
            case MatchEventType::CommandRejected:
            case MatchEventType::WorldEventRejected:
                break;
        }
    }

    const auto better =
        [&result](uint32_t lhs, uint32_t rhs) noexcept {
            const WreckwaterWreckCamCandidate& a =
                result.candidates[lhs];
            const WreckwaterWreckCamCandidate& b =
                result.candidates[rhs];
            return std::tuple{
                a.score,
                a.reasonEventSequence,
                std::numeric_limits<network::NetEntityId>::max()
                    - a.netEntityId,
                std::numeric_limits<uint32_t>::max()
                    - a.netGeneration}
                > std::tuple{
                    b.score,
                    b.reasonEventSequence,
                    std::numeric_limits<network::NetEntityId>::max()
                        - b.netEntityId,
                    std::numeric_limits<uint32_t>::max()
                        - b.netGeneration};
        };
    uint32_t primary = 0u;
    for (uint32_t index = 1u;
         index < result.candidateCount; ++index) {
        if (better(index, primary)) primary = index;
    }
    uint32_t secondary = result.candidateCount;
    for (uint32_t index = 0u;
         index < result.candidateCount; ++index) {
        if (index == primary) continue;
        if (secondary == result.candidateCount
            || better(index, secondary)) {
            secondary = index;
        }
    }

    const WreckwaterWreckCamCandidate& primaryCandidate =
        result.candidates[primary];
    result.primaryNetEntityId = primaryCandidate.netEntityId;
    result.primaryNetGeneration = primaryCandidate.netGeneration;
    result.primaryScore = primaryCandidate.score;
    result.reason = primaryCandidate.reason;
    result.reasonEventSequence =
        primaryCandidate.reasonEventSequence;
    result.shot = shotForEvent(
        primaryCandidate.reason,
        primaryCandidate.reasonEventSequence != 0u);
    result.valid = true;
    if (secondary != result.candidateCount) {
        const WreckwaterWreckCamCandidate& secondaryCandidate =
            result.candidates[secondary];
        result.secondaryNetEntityId =
            secondaryCandidate.netEntityId;
        result.secondaryNetGeneration =
            secondaryCandidate.netGeneration;
        result.secondaryScore = secondaryCandidate.score;
        result.hasSecondary = true;
    }
    return result;
}

} // namespace voxy::game
