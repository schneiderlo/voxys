#pragma once

#include "game/expedition/session_transactions.hpp"
#include <algorithm>
#include <type_traits>

namespace voxy::game::expedition {

struct EventSequenceTag;
struct EventFrontierTag;
using EventSequence = construction::Counter<EventSequenceTag, false>;
using EventFrontier = construction::Counter<EventFrontierTag, true>;
struct EventStreamIncarnation {
    std::array<uint8_t, 16> bytes{};
    [[nodiscard]] bool operator==(const EventStreamIncarnation&) const = default;
};
struct EventStreamIdentity {
    WorldNamespace world{};
    AuthorityEpoch epoch{};
    // Fresh public identity from the composition root, never an admission token.
    EventStreamIncarnation incarnation{};
    [[nodiscard]] bool operator==(const EventStreamIdentity&) const = default;
};
[[nodiscard]] bool isValid(EventStreamIdentity) noexcept;

enum class EventLane : uint8_t { Domain = 1, Presentation = 2, Telemetry = 3 };
enum class EventTickBasis : uint8_t { LogicalBoundary = 1, CompletedSimulation = 2 };
enum class EventKind : uint16_t {
    ReceiptChanged = 0x0001, StateCommitted = 0x0002, AdmissionClosed = 0x0003,
    // Reserved kinds have no accepted payload or producer until their mechanic exists.
    ForceSample = 0x0100, AttachmentChanged = 0x0101, DamageApplied = 0x0102,
    CargoChanged = 0x0103, RewardBanked = 0x0104, PresentationCue = 0x0200,
    DiagnosticSummary = 0x0300,
};
enum class EventOutcome : uint8_t { Pending = 1, Rejected = 2, Committed = 3 };
enum class EventDurability : uint8_t { Volatile = 1 }; // SAVE must define any stronger guarantee.

// Stable event error codes and explicit adapters are declared below. They never
// encode the implicit ordinal values of SessionError/BuildError.
enum class EventSessionCode : uint16_t {
    None = 0x0000,
    InvalidBootstrap = 0x1001,
    Capacity = 0x1002,
    InvalidIdentity = 0x1003,
    WrongCaller = 0x1004,
    WrongToken = 0x1005,
    WrongEpoch = 0x1006,
    SequenceGap = 0x1007,
    SequenceExhausted = 0x1008,
    AlreadyProcessed = 0x1009,
    RequestConflict = 0x100a,
    Busy = 0x100b,
    StaleRevision = 0x100c,
    RevisionExhausted = 0x100d,
    UnknownBuild = 0x100e,
    UnknownPart = 0x100f,
    UnknownJob = 0x1010,
    NotOwner = 0x1011,
    LeaseDenied = 0x1012,
    InsufficientResources = 0x1013,
    ResourceOverflow = 0x1014,
    IdExhausted = 0x1015,
    InvalidBuild = 0x1016,
    ConnectedPart = 0x1017,
    JobUnavailable = 0x1018,
    AdapterRejected = 0x1019,
    Canceled = 0x101a,
    NoPending = 0x101b,
    UnsupportedFeature = 0x101c,
    JournalCapacity = 0x101d,
    CandidateCapacity = 0x101e,
    Closed = 0x101f,
    HistoryUnavailable = 0x1020,
    HistoryConflict = 0x1021,
    HistoryBusy = 0x1022,
    HistoryCapacity = 0x1023,
    HistoryExhausted = 0x1024,
    UnknownCargo = 0x1025, CargoUnavailable = 0x1026, DeliveryNotReady = 0x1027,
};
[[nodiscard]] std::optional<EventSessionCode> eventCode(SessionError) noexcept;
enum class EventBuildCode : uint16_t {
    None = 0x0000,
    Capacity = 0x2001,
    InvalidId = 0x2002,
    DuplicateId = 0x2003,
    WrongWorld = 0x2004,
    WrongBuild = 0x2005,
    UnknownDefinition = 0x2006,
    UnknownDefinitionVersion = 0x2007,
    InvalidPlacement = 0x2008,
    InvalidHealth = 0x2009,
    InvalidSettings = 0x200a,
    InvalidProvenance = 0x200b,
    InvalidLease = 0x200c,
    UnknownPart = 0x200d,
    UnknownSocket = 0x200e,
    SamePartConnection = 0x200f,
    IncompatibleSocket = 0x2010,
    InvalidConnection = 0x2011,
    DuplicateConnection = 0x2012,
    SocketCapacity = 0x2013,
    MisalignedWeld = 0x2014,
    SolidOverlap = 0x2015,
    ClearanceBlocked = 0x2016,
    StaleRevision = 0x2017,
    RevisionExhausted = 0x2018,
    ImmutableIdentity = 0x2019,
    InvalidEncoding = 0x201a,
    UnsupportedSchema = 0x201b,
    NonCanonicalOrder = 0x201c,
};
[[nodiscard]] std::optional<EventBuildCode> eventCode(construction::BuildError) noexcept;

struct PublicRequestReference {
    DurableId participant{};
    RequestSequence sequence{};
    [[nodiscard]] bool operator==(const PublicRequestReference&) const = default;
};
struct EventJournalReference {
    JournalWriterGeneration writer{};
    JournalSequence sequence{}; // World/epoch are in the envelope.
    [[nodiscard]] bool operator==(const EventJournalReference&) const = default;
};
struct EventBuildReference {
    DurableId build{};
    TopologyRevision revision{};
    [[nodiscard]] bool operator==(const EventBuildReference&) const = default;
};
struct ReceiptChangedEvent {
    PublicRequestReference request{};
    EventOutcome outcome = EventOutcome::Pending;
    EventSessionCode error = EventSessionCode::None;
    EventBuildCode buildError = EventBuildCode::None;
    RequestSequence expectedSequence{}; // Zero means exhausted, never sequence zero admission.
    std::optional<DurableId> object{};
    std::optional<EventJournalReference> journal{};
    EventDurability durability = EventDurability::Volatile;
    [[nodiscard]] bool operator==(const ReceiptChangedEvent&) const = default;
};
enum class EventChange : uint16_t {
    Builds = 0x0001, Inventory = 0x0002, Jobs = 0x0004,
    History = 0x0008, Entitlements = 0x0010, Cargo = 0x0020,
};
[[nodiscard]] constexpr uint16_t changeBit(EventChange change) noexcept { return static_cast<uint16_t>(change); }
struct StateCommittedEvent {
    std::optional<PublicRequestReference> cause{};
    uint16_t changes = 0;
    EventDurability durability = EventDurability::Volatile;
    std::optional<DurableId> primary{};
    ResourceAmounts balance{}; // Absolute state, never an instruction to grant resources.
    std::optional<EventJournalReference> journal{};
    [[nodiscard]] bool operator==(const StateCommittedEvent&) const = default;
};
enum class EventCloseReason : uint16_t { HostClosed = 1, JournalFault = 2 };
struct AdmissionClosedEvent {
    EventCloseReason reason = EventCloseReason::HostClosed;
    std::optional<EventJournalReference> journal{};
    [[nodiscard]] bool operator==(const AdmissionClosedEvent&) const = default;
};
enum class EventDiagnosticCode : uint16_t {
    IngressRejected = 1, ObserverOverwrites = 2, OwnerBoundaryCost = 3,
};
struct DiagnosticSummaryEvent {
    EventDiagnosticCode code = EventDiagnosticCode::IngressRejected;
    uint64_t count = 0;
    SimulationTick intervalBegin{}, intervalEnd{};
    // Cost: mean and maximum microseconds over count measured boundaries.
    // Count-only codes require both values zero. No force/impulse interpretation.
    double meanMicroseconds = 0.0, maximumMicroseconds = 0.0;
    bool countSaturated = false;
    [[nodiscard]] bool operator==(const DiagnosticSummaryEvent&) const = default;
};
using EventPayload = std::variant<ReceiptChangedEvent, StateCommittedEvent,
    AdmissionClosedEvent, DiagnosticSummaryEvent>;
inline constexpr uint16_t kEventEnvelopeVersion = 1;
inline constexpr uint16_t kEventPayloadVersion = 1;
struct EventHeader {
    EventStreamIdentity stream{};
    EventSequence sequence{};
    SimulationTick tick{};
    SessionRevision revision{};
    std::optional<EventBuildReference> build{};
    uint16_t envelopeVersion = kEventEnvelopeVersion;
    EventKind kind = EventKind::ReceiptChanged;
    uint16_t payloadVersion = kEventPayloadVersion;
    uint16_t payloadBytes = 56;
    EventLane lane = EventLane::Domain;
    EventTickBasis tickBasis = EventTickBasis::LogicalBoundary;
    [[nodiscard]] bool operator==(const EventHeader&) const = default;
};
struct EventRecord {
    EventHeader header{};
    EventPayload payload{};
    [[nodiscard]] bool operator==(const EventRecord&) const = default;
};
static_assert(std::is_trivially_copyable_v<EventRecord>);
static_assert(std::is_nothrow_copy_assignable_v<EventRecord>);
static_assert(sizeof(EventRecord) <= 256, "Revise DATA-06's memory budget before increasing record size");
struct EventDraft {
    SimulationTick tick{};
    SessionRevision revision{};
    std::optional<EventBuildReference> build{};
    EventPayload payload{};
    EventTickBasis tickBasis = EventTickBasis::LogicalBoundary;
};
[[nodiscard]] EventKind eventKind(const EventPayload&) noexcept;
[[nodiscard]] uint16_t eventPayloadBytes(EventKind) noexcept;
enum class EventValidation : uint8_t {
    Valid, UnsupportedVersion, InvalidIdentity, InvalidSequence, UnsupportedKind,
    WrongLane, WrongTickBasis, InvalidPayload, RegressedBoundary,
};
[[nodiscard]] EventValidation validateEvent(const EventRecord&) noexcept;

// Canonical little-endian bytes; never native union/optional/padding bytes.
inline constexpr size_t kEventWireHeaderBytes = 96;
inline constexpr size_t kMaximumEventWireBytes = 160;
enum class EventCodecError : uint8_t {
    None, NeedCapacity, Truncated, TrailingBytes, InvalidMagic, NonCanonical, InvalidRecord,
};
struct EventEncodeResult {
    EventCodecError error = EventCodecError::None;
    EventValidation validation = EventValidation::Valid;
    size_t bytes = 0;
};
struct EventDecodeResult {
    EventCodecError error = EventCodecError::None;
    EventValidation validation = EventValidation::Valid;
    std::optional<EventRecord> record{};
};
// Error leaves the caller's output untouched. Successful encoding writes bytes
// only through result.bytes; decoding consumes exactly one record.
[[nodiscard]] EventEncodeResult encodeEvent(const EventRecord&, std::span<std::byte>) noexcept;
[[nodiscard]] EventDecodeResult decodeEvent(std::span<const std::byte>) noexcept;

struct EventCursor {
    EventStreamIncarnation incarnation{};
    EventLane lane = EventLane::Domain;
    EventFrontier lastConsumed{};
    uint16_t envelopeVersion = kEventEnvelopeVersion;
    [[nodiscard]] bool operator==(const EventCursor&) const = default;
};
struct SaturatingEventCount {
    uint64_t value = 0;
    bool saturated = false;
    void increment() noexcept {
        if (value == std::numeric_limits<uint64_t>::max()) saturated = true;
        else ++value;
    }
    [[nodiscard]] bool operator==(const SaturatingEventCount&) const = default;
};
struct EventLaneStats {
    EventSequence oldest{}, newest{}; // Both zero when there are no retained records.
    EventFrontier publishedThrough{};
    uint32_t capacity = 0, occupancy = 0, highWater = 0;
    SaturatingEventCount overwritten{}, invalidInput{};
    bool exhausted = false;
    [[nodiscard]] bool operator==(const EventLaneStats&) const = default;
};
enum class EventPublishStatus : uint8_t { Published, InvalidInput, Exhausted };
struct EventPublishResult {
    EventPublishStatus status = EventPublishStatus::InvalidInput;
    EventValidation validation = EventValidation::Valid;
    std::optional<EventSequence> sequence{};
};
enum class EventReadStatus : uint8_t {
    Read, NeedCapacity, WrongIncarnation, WrongLane, UnsupportedVersion, FutureCursor, Gap,
};
struct EventReadResult {
    EventReadStatus status = EventReadStatus::Read;
    EventCursor next{};
    EventLaneStats stats{};
    size_t count = 0;
};

namespace detail {
// Single-owner bounded transport primitive. This is not an authority writer.
// Nonzero initial frontier is for synthetic arithmetic fixtures, not resumed
// GameSession history. New production hubs always use the zero default.
template <size_t Capacity>
class EventRing {
    static_assert(Capacity > 0 && Capacity <= 1024);
public:
    explicit EventRing(EventStreamIdentity identity, EventLane lane, EventFrontier initial = {}) noexcept
        : identity_(identity), lane_(lane), origin_(initial) {
        stats_.capacity = static_cast<uint32_t>(Capacity);
        stats_.publishedThrough = initial;
        stats_.exhausted = initial.value() == std::numeric_limits<uint64_t>::max();
    }
    [[nodiscard]] EventLaneStats stats() const noexcept { return stats_; }
    // Owner boundary only. Readers cannot expose stale slots because occupancy
    // is zero and every old cursor has the retired incarnation.
    void restart(EventStreamIdentity identity) noexcept {
        identity_ = identity; origin_ = {}; begin_ = 0; stats_ = {};
        stats_.capacity = static_cast<uint32_t>(Capacity);
    }
    [[nodiscard]] EventPublishResult publish(const EventDraft& draft) noexcept {
        EventRecord record;
        record.header = {identity_, EventSequence{1}, draft.tick, draft.revision, draft.build,
            kEventEnvelopeVersion, eventKind(draft.payload), kEventPayloadVersion,
            eventPayloadBytes(eventKind(draft.payload)), lane_, draft.tickBasis};
        record.payload = draft.payload;
        auto validation = validateEvent(record);
        if (validation == EventValidation::Valid && lane_ == EventLane::Domain && stats_.occupancy != 0) {
            const auto& last = records_[(begin_ + stats_.occupancy - 1) % Capacity].header;
            if (draft.revision < last.revision || draft.tick < last.tick)
                validation = EventValidation::RegressedBoundary;
        }
        if (validation != EventValidation::Valid) {
            stats_.invalidInput.increment();
            return {EventPublishStatus::InvalidInput, validation, {}};
        }
        if (stats_.exhausted) return {EventPublishStatus::Exhausted, EventValidation::Valid, {}};
        const EventSequence sequence{stats_.publishedThrough.value() + 1};
        record.header.sequence = sequence;
        if (stats_.occupancy == Capacity) {
            records_[begin_] = record;
            begin_ = (begin_ + 1) % Capacity;
            stats_.overwritten.increment();
        } else {
            records_[(begin_ + stats_.occupancy) % Capacity] = record;
            ++stats_.occupancy;
            stats_.highWater = stats_.occupancy;
        }
        stats_.oldest = records_[begin_].header.sequence;
        stats_.newest = sequence;
        stats_.publishedThrough = EventFrontier{sequence.value()};
        stats_.exhausted = sequence.value() == std::numeric_limits<uint64_t>::max();
        return {EventPublishStatus::Published, EventValidation::Valid, sequence};
    }
    [[nodiscard]] EventReadResult read(EventCursor cursor, std::span<EventRecord> output) const noexcept {
        EventReadResult result{EventReadStatus::Read, cursor, stats_, 0};
        // Validation failures never copy data or propose an advanced cursor.
        if (cursor.envelopeVersion != kEventEnvelopeVersion) result.status = EventReadStatus::UnsupportedVersion;
        else if (cursor.incarnation != identity_.incarnation) result.status = EventReadStatus::WrongIncarnation;
        else if (cursor.lane != lane_) result.status = EventReadStatus::WrongLane;
        else if (cursor.lastConsumed > stats_.publishedThrough) result.status = EventReadStatus::FutureCursor;
        else if (output.empty()) result.status = EventReadStatus::NeedCapacity;
        else if (cursor.lastConsumed < origin_ || (stats_.oldest.valid()
            && cursor.lastConsumed.value() < stats_.oldest.value() - 1)) result.status = EventReadStatus::Gap;
        if (result.status != EventReadStatus::Read) return result;
        const auto available = stats_.publishedThrough.value() - cursor.lastConsumed.value();
        result.count = std::min<size_t>(output.size(), static_cast<uint32_t>(available)); // Gap validation bounds available by Capacity.
        if (result.count == 0) return result;
        const size_t offset = static_cast<uint32_t>(cursor.lastConsumed.value() + 1 - stats_.oldest.value());
        for (size_t i = 0; i < result.count; ++i) output[i] = records_[(begin_ + offset + i) % Capacity];
        result.next.lastConsumed = EventFrontier{output[result.count - 1].header.sequence.value()};
        return result;
    }
private:
    EventStreamIdentity identity_{};
    EventLane lane_{};
    EventFrontier origin_{};
    EventLaneStats stats_{};
    size_t begin_ = 0;
    std::array<EventRecord, Capacity> records_{};
};
} // namespace detail

class SessionEventHub;
class SessionEventReader {
public:
    // Borrowed, owner-thread-only view; it must not outlive the hub/session.
    [[nodiscard]] EventStreamIdentity identity() const noexcept;
    [[nodiscard]] std::optional<EventLaneStats> stats(EventLane) const noexcept;
    [[nodiscard]] EventReadResult read(EventCursor, std::span<EventRecord>) const noexcept;
private:
    friend class SessionEventHub;
    explicit SessionEventReader(const SessionEventHub& hub) noexcept : hub_(&hub) {}
    const SessionEventHub* hub_;
};
class SessionEventHub {
public:
    [[nodiscard]] static std::unique_ptr<SessionEventHub> create(EventStreamIdentity);
    SessionEventHub(const SessionEventHub&) = delete;
    SessionEventHub& operator=(const SessionEventHub&) = delete;
    [[nodiscard]] SessionEventReader reader() const noexcept { return SessionEventReader{*this}; }
    [[nodiscard]] static constexpr size_t slotBytes() noexcept { return 1536 * sizeof(EventRecord); }
private:
    friend class GameSession; // Only authority owns the publication capability.
    friend class SessionEventReader;
    explicit SessionEventHub(EventStreamIdentity identity) noexcept
        : identity_(identity), domain_(identity, EventLane::Domain),
          presentation_(identity, EventLane::Presentation), telemetry_(identity, EventLane::Telemetry) {}
    [[nodiscard]] EventPublishResult publish(EventLane, const EventDraft&) noexcept;
    void restart(EventStreamIdentity identity) noexcept {
        identity_ = identity;
        domain_.restart(identity); presentation_.restart(identity); telemetry_.restart(identity);
    }
    EventStreamIdentity identity_{};
    detail::EventRing<256> domain_;
    detail::EventRing<256> presentation_;
    detail::EventRing<1024> telemetry_;
};
static_assert(SessionEventHub::slotBytes() <= 384 * 1024);

} // namespace voxy::game::expedition
