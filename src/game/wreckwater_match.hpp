#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::game {

inline constexpr uint32_t kWreckwaterMatchSchemaVersion = 2u;
inline constexpr uint32_t kWreckwaterPlayerCount = 4u;
inline constexpr uint32_t kWreckwaterCrewCount = 2u;
inline constexpr uint32_t kWreckwaterPlayersPerCrew = 2u;
inline constexpr uint32_t kWreckwaterSkiffCount = 2u;
inline constexpr uint32_t kWreckwaterCargoCount = 1u;
inline constexpr uint32_t kWreckwaterTickRateHz = 60u;
inline constexpr uint32_t kWreckwaterMaximumCommandsPerPlayerTick = 8u;
inline constexpr uint32_t kWreckwaterMaximumWorldEventsPerTick = 16u;
inline constexpr uint32_t kWreckwaterMaximumIntentsPerTick =
    kWreckwaterPlayerCount * kWreckwaterMaximumCommandsPerPlayerTick
    + kWreckwaterMaximumWorldEventsPerTick;
inline constexpr uint32_t kWreckwaterMaximumPendingCommands = 4'096u;
inline constexpr uint32_t kWreckwaterMaximumPendingWorldEvents = 4'096u;
inline constexpr uint32_t kWreckwaterMaximumRecordedEvents = 524'288u;
inline constexpr uint32_t kWreckwaterMaximumMatchTicks =
    24u * 60u * 60u * kWreckwaterTickRateHz;

using PlayerId = uint64_t;
using ConnectionId = uint64_t;
using CargoId = uint32_t;
using SkiffId = uint32_t;
using AttachmentId = uint64_t;

enum class CrewId : uint32_t {
    None = 0u,
    CrewOne = 1u,
    CrewTwo = 2u,
};

enum class MatchPhase : uint32_t {
    Warmup = 0u,
    Live = 1u,
    Overtime = 2u,
    Finished = 3u,
};

enum class MatchOutcomeType : uint32_t {
    Undecided = 0u,
    CrewVictory = 1u,
    Tie = 2u,
};

struct MatchOutcome {
    MatchOutcomeType type = MatchOutcomeType::Undecided;
    CrewId winner = CrewId::None;

    [[nodiscard]] bool operator==(const MatchOutcome&) const = default;
};

enum class CargoDisposition : uint32_t {
    Free = 0u,
    Towed = 1u,
    Banked = 2u,
    Lost = 3u,
};

enum class SkiffDisposition : uint32_t {
    Active = 0u,
    Sunk = 1u,
};

enum class MatchCommandType : uint32_t {
    TowCargo = 1u,
    CutTow = 2u,
    StealCargo = 3u,
    BankCargo = 4u,
};

enum class CommandRejectReason : uint32_t {
    None = 0u,
    NotInitialized = 1u,
    MatchNotActive = 2u,
    Malformed = 3u,
    ForeignMatch = 4u,
    StaleWorldEpoch = 5u,
    StaleAuthorityEpoch = 6u,
    UnknownPlayer = 7u,
    PlayerDisconnected = 8u,
    StaleConnection = 9u,
    UnknownCargo = 10u,
    WrongSkiff = 11u,
    StaleSkiffGeneration = 12u,
    TickOutOfWindow = 13u,
    ReplayedSequence = 14u,
    SequenceTooOld = 15u,
    SequenceJumpTooLarge = 16u,
    PerTickLimit = 17u,
    PendingCapacity = 18u,
    ConflictLost = 19u,
    StaleCargoRevision = 20u,
    InvalidCargoState = 21u,
    ActorUnavailable = 22u,
    SkiffUnavailable = 23u,
    OutOfRange = 24u,
    OutsideExtraction = 25u,
    WorldRejected = 26u,
    PhysicsEvidenceInFuture = 27u,
    PhysicsEvidenceTooOld = 28u,
    StaleCargoGeneration = 29u,
    PhysicsEvidenceUnavailable = 30u,
    AuthorityFaulted = 31u,
};

struct MatchCommand {
    uint32_t schemaVersion = kWreckwaterMatchSchemaVersion;
    MatchCommandType type = MatchCommandType::TowCargo;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint64_t tick = 0u;
    // Stamped by the authoritative ingress path, not trusted client data.
    // The world adapter must evaluate this exact retained physics snapshot.
    uint64_t physicsEvidenceTick = 0u;
    uint64_t sequence = 0u;
    PlayerId playerId = 0u;
    ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    CargoId cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t observedCargoRevision = 0u;
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    std::array<uint32_t, 2> reserved{};

    [[nodiscard]] bool operator==(const MatchCommand&) const = default;
};

// Server conflict order is bank, cut, steal, then tow. Equal-priority
// requests use stable player ID and command sequence, never arrival order.
[[nodiscard]] bool wreckwaterCommandLess(
    const MatchCommand& lhs, const MatchCommand& rhs) noexcept;

struct CommandSubmitResult {
    bool accepted = false;
    CommandRejectReason reason = CommandRejectReason::None;

    [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

enum class AuthoritativeWorldEventType : uint32_t {
    AttachmentBroken = 1u,
    CargoLost = 2u,
    SkiffSunk = 3u,
    SkiffRespawned = 4u,
};

enum class WorldEventRejectReason : uint32_t {
    None = 0u,
    NotInitialized = 1u,
    MatchNotActive = 2u,
    Malformed = 3u,
    ForeignMatch = 4u,
    StaleWorldEpoch = 5u,
    StaleAuthorityEpoch = 6u,
    ForeignStream = 7u,
    TickOutOfWindow = 8u,
    SourceTickInFuture = 9u,
    SourceTickTooOld = 10u,
    ReplayedSequence = 11u,
    SequenceTooOld = 12u,
    SequenceJumpTooLarge = 13u,
    PerTickLimit = 14u,
    PendingCapacity = 15u,
    UnknownCargo = 16u,
    UnknownSkiff = 17u,
    StaleCargoGeneration = 18u,
    StaleCargoRevision = 19u,
    StaleSkiffGeneration = 20u,
    StaleAttachment = 21u,
    InvalidState = 22u,
    AuthorityFaulted = 23u,
};

// GPU readback is asynchronous. sourcePhysicsTick identifies when the
// physical event occurred. applicationTick identifies the later canonical
// match tick where it is applied. This core does not roll back to sourceTick.
struct AuthoritativeWorldEvent {
    uint32_t schemaVersion = kWreckwaterMatchSchemaVersion;
    AuthoritativeWorldEventType type =
        AuthoritativeWorldEventType::AttachmentBroken;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint32_t streamId = 0u;
    uint64_t sourcePhysicsTick = 0u;
    uint64_t applicationTick = 0u;
    uint64_t sequence = 0u;
    CargoId cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t cargoRevision = 0u;
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    uint32_t nextSkiffGeneration = 0u;
    AttachmentId attachmentId = 0u;
    uint32_t attachmentGeneration = 0u;
    std::array<uint32_t, 2> reserved{};

    [[nodiscard]] bool operator==(
        const AuthoritativeWorldEvent&) const = default;
};

[[nodiscard]] bool wreckwaterWorldEventLess(
    const AuthoritativeWorldEvent& lhs,
    const AuthoritativeWorldEvent& rhs) noexcept;

struct WorldEventSubmitResult {
    bool accepted = false;
    WorldEventRejectReason reason = WorldEventRejectReason::None;

    [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

struct PlayerRegistration {
    PlayerId playerId = 0u;
    CrewId crew = CrewId::None;
    uint32_t seat = 0u;
};

enum class RegistrationResult : uint32_t {
    Registered = 0u,
    NotInitialized = 1u,
    RosterLocked = 2u,
    Invalid = 3u,
    DuplicatePlayer = 4u,
    SeatOccupied = 5u,
    AlreadyRegistered = 6u,
    EventCapacity = 7u,
};

enum class ConnectionResult : uint32_t {
    Connected = 0u,
    Reconnected = 1u,
    AlreadyConnected = 2u,
    NotInitialized = 3u,
    UnknownPlayer = 4u,
    InvalidConnection = 5u,
    ConnectionInUse = 6u,
    MustDisconnectFirst = 7u,
    TransitionLimit = 8u,
    EventCapacity = 9u,
    AuthorityFaulted = 10u,
};

struct PlayerState {
    PlayerId playerId = 0u;
    CrewId crew = CrewId::None;
    uint32_t seat = 0u;
    ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    uint32_t connectionEventCount = 0u;
    bool occupied = false;
    bool connected = false;
    bool everConnected = false;

    [[nodiscard]] bool operator==(const PlayerState&) const = default;
};

struct CrewState {
    CrewId crew = CrewId::None;
    SkiffId skiffId = 0u;
    uint32_t score = 0u;

    [[nodiscard]] bool operator==(const CrewState&) const = default;
};

struct SkiffState {
    SkiffId skiffId = 0u;
    CrewId owner = CrewId::None;
    uint32_t generation = 1u;
    SkiffDisposition disposition = SkiffDisposition::Active;

    [[nodiscard]] bool operator==(const SkiffState&) const = default;
};

struct CargoState {
    CargoId cargoId = 0u;
    uint32_t generation = 1u;
    uint32_t revision = 1u;
    CargoDisposition disposition = CargoDisposition::Free;
    CrewId owner = CrewId::None;
    SkiffId towingSkiff = 0u;
    uint32_t towingSkiffGeneration = 0u;
    AttachmentId towAttachmentId = 0u;
    uint32_t towAttachmentGeneration = 0u;

    [[nodiscard]] bool operator==(const CargoState&) const = default;
};

enum class WorldValidationResult : uint32_t {
    Allowed = 0u,
    ActorUnavailable = 1u,
    SkiffUnavailable = 2u,
    OutOfRange = 3u,
    OutsideExtraction = 4u,
    Rejected = 5u,
    PhysicsEvidenceUnavailable = 6u,
};

struct MatchActionQuery {
    MatchCommand command{};
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    CrewId actorCrew = CrewId::None;
    CargoState cargo{};
    SkiffState actorSkiff{};
};

enum class MatchWorldIntentType : uint32_t {
    AttachTow = 1u,
    CutTow = 2u,
    TransferTow = 3u,
    BankCargo = 4u,
};

enum class MatchWorldIntentSource : uint32_t {
    PlayerCommand = 1u,
    AuthoritativeWorldEvent = 2u,
};

struct MatchWorldIntent {
    uint32_t schemaVersion = kWreckwaterMatchSchemaVersion;
    MatchWorldIntentType type = MatchWorldIntentType::AttachTow;
    MatchWorldIntentSource source =
        MatchWorldIntentSource::PlayerCommand;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    uint32_t sourceStreamId = 0u;
    uint64_t sourceSequence = 0u;
    PlayerId playerId = 0u;
    ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    CrewId crew = CrewId::None;
    // The authenticated player's own skiff. This is deliberately distinct
    // from sourceSkiff, which is the cargo's current towing skiff and may
    // belong to the opposing crew during CutTow/TransferTow.
    SkiffId actorSkiff = 0u;
    uint32_t actorSkiffGeneration = 0u;
    CargoId cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t priorCargoRevision = 0u;
    uint32_t resultingCargoRevision = 0u;
    SkiffId sourceSkiff = 0u;
    uint32_t sourceSkiffGeneration = 0u;
    SkiffId targetSkiff = 0u;
    uint32_t targetSkiffGeneration = 0u;
    AttachmentId sourceAttachmentId = 0u;
    uint32_t sourceAttachmentGeneration = 0u;
    AttachmentId resultingAttachmentId = 0u;
    uint32_t resultingAttachmentGeneration = 0u;

    [[nodiscard]] bool operator==(const MatchWorldIntent&) const = default;
};

struct MatchWorldIntentAck {
    uint32_t schemaVersion = kWreckwaterMatchSchemaVersion;
    MatchWorldIntentType type = MatchWorldIntentType::AttachTow;
    MatchWorldIntentSource source =
        MatchWorldIntentSource::PlayerCommand;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    uint32_t sourceStreamId = 0u;
    uint64_t sourceSequence = 0u;
    PlayerId playerId = 0u;
    ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    CrewId crew = CrewId::None;
    SkiffId actorSkiff = 0u;
    uint32_t actorSkiffGeneration = 0u;
    CargoId cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t priorCargoRevision = 0u;
    uint32_t resultingCargoRevision = 0u;
    SkiffId sourceSkiff = 0u;
    uint32_t sourceSkiffGeneration = 0u;
    SkiffId targetSkiff = 0u;
    uint32_t targetSkiffGeneration = 0u;
    AttachmentId sourceAttachmentId = 0u;
    uint32_t sourceAttachmentGeneration = 0u;
    AttachmentId newAttachmentId = 0u;
    uint32_t newAttachmentGeneration = 0u;

    [[nodiscard]] bool operator==(
        const MatchWorldIntentAck&) const = default;
};

// This core deliberately does not duplicate fracture, flooding, water, or
// attachment simulation. An adapter may read gameplay::WreckwaterSlice for
// deterministic fixtures, or the live GPU physics world for production.
//
// applyAtomically is a transaction boundary. False means no intent changed
// world state and the exact batch may be retried. True means every intent
// changed world state and every echoed identity/ack field is valid.
// TransferTow must atomically remove the source attachment and create the
// target attachment. Calls must be idempotent by the full intent identity.
// Returning true with an invalid acknowledgement is an adapter contract
// violation and places the match in a permanent AuthorityFault fail-stop.
class IWreckwaterWorldAuthority {
public:
    virtual ~IWreckwaterWorldAuthority() = default;
    [[nodiscard]] virtual WorldValidationResult evaluate(
        const MatchActionQuery& query) const noexcept = 0;
    [[nodiscard]] virtual bool applyAtomically(
        std::span<const MatchWorldIntent> intents,
        std::span<MatchWorldIntentAck> acknowledgements) noexcept = 0;
};

enum class MatchEventType : uint32_t {
    PlayerRegistered = 1u,
    PlayerConnected = 2u,
    PlayerReconnected = 3u,
    PlayerDisconnected = 4u,
    MatchStarted = 5u,
    PhaseChanged = 6u,
    CommandRejected = 7u,
    CargoTowAttached = 8u,
    CargoTowCut = 9u,
    CargoStolen = 10u,
    CargoBanked = 11u,
    ScoreChanged = 12u,
    MatchFinished = 13u,
    WorldEventRejected = 14u,
    AttachmentBroken = 15u,
    CargoLost = 16u,
    SkiffSunk = 17u,
    SkiffRespawned = 18u,
    AuthorityFault = 19u,
};

enum class MatchFaultReason : uint32_t {
    None = 0u,
    InvalidWorldAcknowledgement = 1u,
    CommittedInvariantViolation = 2u,
};

enum class MatchEventSource : uint32_t {
    MatchSystem = 0u,
    PlayerCommand = 1u,
    AuthoritativeWorldEvent = 2u,
};

struct MatchEvent {
    uint32_t schemaVersion = kWreckwaterMatchSchemaVersion;
    uint64_t eventSequence = 0u;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint64_t tick = 0u;
    uint64_t sourcePhysicsTick = 0u;
    uint64_t commandTick = 0u;
    uint64_t sourceSequence = 0u;
    MatchEventSource source = MatchEventSource::MatchSystem;
    uint32_t sourceStreamId = 0u;
    MatchCommandType commandType = MatchCommandType::TowCargo;
    MatchEventType type = MatchEventType::MatchStarted;
    AuthoritativeWorldEventType worldEventType =
        AuthoritativeWorldEventType::AttachmentBroken;
    MatchPhase phase = MatchPhase::Warmup;
    MatchOutcomeType outcome = MatchOutcomeType::Undecided;
    MatchFaultReason faultReason = MatchFaultReason::None;
    CommandRejectReason commandRejection = CommandRejectReason::None;
    WorldEventRejectReason worldEventRejection =
        WorldEventRejectReason::None;
    PlayerId playerId = 0u;
    uint32_t seat = 0u;
    ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    CrewId crew = CrewId::None;
    CargoId cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t observedCargoRevision = 0u;
    uint32_t cargoRevision = 0u;
    SkiffId sourceSkiff = 0u;
    uint32_t sourceSkiffGeneration = 0u;
    SkiffId targetSkiff = 0u;
    uint32_t targetSkiffGeneration = 0u;
    AttachmentId attachmentId = 0u;
    uint32_t attachmentGeneration = 0u;
    AttachmentId sourceAttachmentId = 0u;
    uint32_t sourceAttachmentGeneration = 0u;
    AttachmentId resultingAttachmentId = 0u;
    uint32_t resultingAttachmentGeneration = 0u;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    uint32_t stateHash = 0u;

    [[nodiscard]] bool operator==(const MatchEvent&) const = default;
};

// These helpers are the single canonical implementation of the authoritative
// event-stream fingerprint. Replay recording/playback uses the same functions
// as the live match instead of maintaining a second, drifting hash contract.
[[nodiscard]] uint32_t wreckwaterInitialMatchEventStreamRollingHash(
    uint32_t configurationHash) noexcept;
[[nodiscard]] uint32_t wreckwaterAppendMatchEventStreamRollingHash(
    uint32_t rollingHash, const MatchEvent& event) noexcept;
[[nodiscard]] uint32_t wreckwaterFinalizeMatchEventStreamHash(
    uint32_t rollingHash,
    uint32_t configurationHash,
    uint32_t stateHash,
    uint64_t nextEventSequence,
    uint32_t eventCount) noexcept;

struct WreckwaterMatchTelemetry {
    uint64_t registeredPlayers = 0u;
    uint64_t connectionTransitions = 0u;
    uint64_t reconnects = 0u;
    uint64_t queuedCommands = 0u;
    uint64_t appliedCommands = 0u;
    uint64_t rejectedCommands = 0u;
    uint64_t malformedCommands = 0u;
    uint64_t replayedCommands = 0u;
    uint64_t conflictRejections = 0u;
    uint64_t worldRejections = 0u;
    uint64_t queuedWorldEvents = 0u;
    uint64_t appliedWorldEvents = 0u;
    uint64_t rejectedWorldEvents = 0u;
    uint64_t replayedWorldEvents = 0u;
    uint64_t transactionAborts = 0u;
    uint64_t invalidWorldAcks = 0u;
    uint64_t eventCapacityFailures = 0u;
    uint32_t pendingHighWater = 0u;
    uint32_t pendingWorldEventHighWater = 0u;
    uint32_t eventsHighWater = 0u;
};

struct WreckwaterStorageState {
    const void* pendingCommands = nullptr;
    const void* pendingWorldEvents = nullptr;
    const void* purgedCommandsScratch = nullptr;
    const void* tentativeEvents = nullptr;
    const void* recordedEvents = nullptr;
    size_t pendingCommandCapacity = 0u;
    size_t pendingWorldEventCapacity = 0u;
    size_t purgedCommandCapacity = 0u;
    size_t tentativeEventCapacity = 0u;
    size_t recordedEventCapacity = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterStorageState&) const = default;
};

// Single-threaded authoritative rules core. initialize() owns and reserves
// every dynamic buffer; accepted runtime operations never grow those buffers.
// Physics remains external and crosses only the explicit transaction seam.
class WreckwaterMatch {
public:
    struct Config {
        uint64_t matchId = 1u;
        uint64_t worldId = 1u;
        uint32_t worldEpoch = 1u;
        uint32_t authorityEpoch = 1u;
        uint32_t worldEventStreamId = 1u;
        uint32_t tickRateHz = kWreckwaterTickRateHz;
        uint32_t warmupTicks = 3u * kWreckwaterTickRateHz;
        uint32_t liveTicks = 4u * 60u * kWreckwaterTickRateHz;
        uint32_t overtimeTicks = 30u * kWreckwaterTickRateHz;
        uint32_t inputFutureWindow = 8u;
        uint32_t worldEventFutureWindow = 8u;
        uint32_t maximumWorldEventLagTicks = 64u;
        uint32_t maximumActionEvidenceLagTicks = 64u;
        uint32_t maximumCommandsPerPlayerTick = 2u;
        uint32_t maximumWorldEventsPerTick = 2u;
        uint32_t maximumSequenceAdvance = 1'024u;
        uint32_t maximumWorldEventSequenceAdvance = 1'024u;
        uint32_t maximumConnectionEventsPerPlayer = 32u;
        uint32_t pendingCommandCapacity = 64u;
        uint32_t pendingWorldEventCapacity = 16u;
        uint32_t eventCapacity = 196'608u;
        uint32_t bankScore = 1'000u;
        CargoId reactorCargoId = 1u;
        uint32_t reactorCargoGeneration = 1u;
        SkiffId crewOneSkiffId = 1u;
        SkiffId crewTwoSkiffId = 2u;
    };

    WreckwaterMatch() = default;
    ~WreckwaterMatch() = default;
    WreckwaterMatch(const WreckwaterMatch&) = delete;
    WreckwaterMatch& operator=(const WreckwaterMatch&) = delete;
    WreckwaterMatch(WreckwaterMatch&&) = delete;
    WreckwaterMatch& operator=(WreckwaterMatch&&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool initialize(const Config& config);

    [[nodiscard]] RegistrationResult registerPlayer(
        const PlayerRegistration& registration);
    [[nodiscard]] ConnectionResult connectPlayer(
        PlayerId playerId, ConnectionId connectionId);
    [[nodiscard]] bool disconnectPlayer(
        PlayerId playerId, ConnectionId connectionId);
    [[nodiscard]] bool start();

    [[nodiscard]] CommandSubmitResult submitCommand(
        const MatchCommand& command);
    [[nodiscard]] WorldEventSubmitResult submitWorldEvent(
        const AuthoritativeWorldEvent& event);
    [[nodiscard]] bool step(IWreckwaterWorldAuthority& world);

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool rosterComplete() const noexcept;
    [[nodiscard]] uint64_t currentTick() const noexcept {
        return currentTick_;
    }
    [[nodiscard]] MatchPhase phase() const noexcept { return phase_; }
    [[nodiscard]] MatchOutcome outcome() const noexcept { return outcome_; }
    [[nodiscard]] MatchFaultReason fault() const noexcept { return fault_; }
    [[nodiscard]] bool faulted() const noexcept {
        return fault_ != MatchFaultReason::None;
    }
    [[nodiscard]] uint32_t remainingPhaseTicks() const noexcept;
    [[nodiscard]] uint32_t score(CrewId crew) const noexcept;
    [[nodiscard]] uint32_t stateHash() const noexcept { return stateHash_; }
    [[nodiscard]] uint32_t eventStreamHash() const noexcept;
    [[nodiscard]] uint32_t configurationHash() const noexcept;
    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] const WreckwaterMatchTelemetry& telemetry() const noexcept {
        return telemetry_;
    }
    [[nodiscard]] WreckwaterStorageState storageState() const noexcept;

    [[nodiscard]] std::span<const PlayerState> players() const noexcept {
        return players_;
    }
    [[nodiscard]] std::span<const CrewState> crews() const noexcept {
        return crews_;
    }
    [[nodiscard]] std::span<const SkiffState> skiffs() const noexcept {
        return skiffs_;
    }
    [[nodiscard]] std::span<const CargoState> cargo() const noexcept {
        return cargo_;
    }
    [[nodiscard]] std::span<const MatchEvent> events() const noexcept {
        return events_;
    }
    [[nodiscard]] std::span<const MatchWorldIntent>
    intentsThisTick() const noexcept {
        return std::span<const MatchWorldIntent>(
            publishedIntents_.data(), publishedIntentCount_);
    }

    [[nodiscard]] const PlayerState* player(
        PlayerId playerId) const noexcept;
    [[nodiscard]] const CargoState* cargo(CargoId cargoId) const noexcept;
    [[nodiscard]] const SkiffState* skiff(SkiffId skiffId) const noexcept;
    [[nodiscard]] std::optional<uint64_t> nextCommandSequence(
        PlayerId playerId) const noexcept;
    [[nodiscard]] std::optional<uint64_t>
    nextWorldEventSequence() const noexcept;

private:
    enum class SequenceObservation : uint32_t {
        Accepted = 0u,
        Duplicate = 1u,
        TooOld = 2u,
        JumpTooLarge = 3u,
    };

    struct SequenceWindow {
        uint64_t latest = 0u;
        uint64_t bits = 0u;
        bool initialized = false;

        [[nodiscard]] SequenceObservation observe(
            uint64_t sequence, uint32_t maximumAdvance) noexcept;
        void reset() noexcept;
    };

    struct StepTransaction {
        uint64_t tick = 0u;
        std::array<CrewState, kWreckwaterCrewCount> crews{};
        std::array<SkiffState, kWreckwaterSkiffCount> skiffs{};
        std::array<CargoState, kWreckwaterCargoCount> cargo{};
        MatchPhase phase = MatchPhase::Warmup;
        MatchOutcome outcome{};
        uint64_t appliedCommands = 0u;
        uint64_t rejectedCommands = 0u;
        uint64_t conflictRejections = 0u;
        uint64_t worldRejections = 0u;
        uint64_t appliedWorldEvents = 0u;
        uint64_t rejectedWorldEvents = 0u;
    };

    [[nodiscard]] static bool validConfig(const Config& config) noexcept;
    [[nodiscard]] static bool validCrew(CrewId crew) noexcept;
    [[nodiscard]] static bool validCommandType(
        MatchCommandType type) noexcept;
    [[nodiscard]] static bool validWorldEventType(
        AuthoritativeWorldEventType type) noexcept;
    [[nodiscard]] static uint32_t playerSlot(
        CrewId crew, uint32_t seat) noexcept;
    [[nodiscard]] size_t playerIndex(PlayerId playerId) const noexcept;
    [[nodiscard]] size_t cargoIndex(CargoId cargoId) const noexcept;
    [[nodiscard]] size_t skiffIndex(SkiffId skiffId) const noexcept;
    [[nodiscard]] CommandRejectReason validateSubmission(
        const MatchCommand& command, size_t& outPlayerIndex) const noexcept;
    [[nodiscard]] WorldEventRejectReason validateWorldEventSubmission(
        const AuthoritativeWorldEvent& event) const noexcept;
    [[nodiscard]] CommandRejectReason validateGameState(
        const StepTransaction& transaction, const MatchCommand& command,
        size_t playerIndex, size_t cargoIndex) const noexcept;
    [[nodiscard]] CommandRejectReason worldRejection(
        WorldValidationResult result) const noexcept;
    void applyCommand(
        StepTransaction& transaction, const MatchCommand& command,
        size_t playerIndex, size_t cargoIndex,
        const IWreckwaterWorldAuthority& world,
        std::array<bool, kWreckwaterCargoCount>& cargoMutated);
    void applyWorldEvent(
        StepTransaction& transaction,
        const AuthoritativeWorldEvent& event);
    void rejectStepCommand(
        StepTransaction& transaction, const MatchCommand& command,
        CrewId crew, CommandRejectReason reason);
    void rejectStepWorldEvent(
        StepTransaction& transaction,
        const AuthoritativeWorldEvent& event,
        WorldEventRejectReason reason);
    void appendTentativeEvent(
        const StepTransaction& transaction, MatchEvent event);
    void appendTentativeIntent(const MatchWorldIntent& intent) noexcept;
    [[nodiscard]] bool validateAndApplyAcks(
        StepTransaction& transaction) noexcept;
    [[nodiscard]] bool validCommittedInvariants(
        const StepTransaction& transaction) const noexcept;
    void enterFault(
        MatchFaultReason reason, const MatchWorldIntent* intent);
    void publishEvent(MatchEvent event);
    void updateStateHash() noexcept;
    void transitionPhase(
        StepTransaction& transaction, MatchPhase phase);
    void finishMatch(StepTransaction& transaction);
    void clearTentative() noexcept;
    void clearPublishedIntents() noexcept;
    void purgePendingCommands(
        PlayerId playerId, uint32_t connectionGeneration);
    [[nodiscard]] bool hasEventCapacity(size_t count) const noexcept;
    void commitTelemetry(const StepTransaction& transaction) noexcept;

    Config config_{};
    std::array<PlayerState, kWreckwaterPlayerCount> players_{};
    std::array<SequenceWindow, kWreckwaterPlayerCount> commandWindows_{};
    SequenceWindow worldEventWindow_{};
    std::array<CrewState, kWreckwaterCrewCount> crews_{};
    std::array<SkiffState, kWreckwaterSkiffCount> skiffs_{};
    std::array<CargoState, kWreckwaterCargoCount> cargo_{};
    std::vector<MatchCommand> pendingCommands_;
    std::vector<AuthoritativeWorldEvent> pendingWorldEvents_;
    std::vector<MatchCommand> purgedCommandsScratch_;
    std::vector<MatchEvent> tentativeEvents_;
    std::vector<MatchEvent> events_;
    std::array<MatchWorldIntent, kWreckwaterMaximumIntentsPerTick>
        tentativeIntents_{};
    std::array<MatchWorldIntentAck, kWreckwaterMaximumIntentsPerTick>
        tentativeAcks_{};
    size_t tentativeIntentCount_ = 0u;
    std::array<MatchWorldIntent, kWreckwaterMaximumIntentsPerTick>
        publishedIntents_{};
    size_t publishedIntentCount_ = 0u;
    uint64_t currentTick_ = 0u;
    uint64_t nextEventSequence_ = 1u;
    uint32_t warmupEndTick_ = 0u;
    uint32_t liveEndTick_ = 0u;
    uint32_t overtimeEndTick_ = 0u;
    uint32_t stateHash_ = 0u;
    uint32_t eventStreamRollingHash_ = 2'166'136'261u;
    MatchPhase phase_ = MatchPhase::Warmup;
    MatchOutcome outcome_{};
    MatchFaultReason fault_ = MatchFaultReason::None;
    WreckwaterMatchTelemetry telemetry_{};
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace voxy::game
