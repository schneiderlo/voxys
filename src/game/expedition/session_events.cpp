#include "game/expedition/session_events.hpp"

#include <bit>
#include <cmath>
#include <new>

namespace voxy::game::expedition {

std::optional<EventSessionCode> eventCode(SessionError code) noexcept {
    switch (code) {
    case SessionError::None: return EventSessionCode::None;
    case SessionError::InvalidBootstrap: return EventSessionCode::InvalidBootstrap;
    case SessionError::Capacity: return EventSessionCode::Capacity;
    case SessionError::InvalidIdentity: return EventSessionCode::InvalidIdentity;
    case SessionError::WrongCaller: return EventSessionCode::WrongCaller;
    case SessionError::WrongToken: return EventSessionCode::WrongToken;
    case SessionError::WrongEpoch: return EventSessionCode::WrongEpoch;
    case SessionError::SequenceGap: return EventSessionCode::SequenceGap;
    case SessionError::SequenceExhausted: return EventSessionCode::SequenceExhausted;
    case SessionError::AlreadyProcessed: return EventSessionCode::AlreadyProcessed;
    case SessionError::RequestConflict: return EventSessionCode::RequestConflict;
    case SessionError::Busy: return EventSessionCode::Busy;
    case SessionError::StaleRevision: return EventSessionCode::StaleRevision;
    case SessionError::RevisionExhausted: return EventSessionCode::RevisionExhausted;
    case SessionError::UnknownBuild: return EventSessionCode::UnknownBuild;
    case SessionError::UnknownPart: return EventSessionCode::UnknownPart;
    case SessionError::UnknownJob: return EventSessionCode::UnknownJob;
    case SessionError::NotOwner: return EventSessionCode::NotOwner;
    case SessionError::LeaseDenied: return EventSessionCode::LeaseDenied;
    case SessionError::InsufficientResources: return EventSessionCode::InsufficientResources;
    case SessionError::ResourceOverflow: return EventSessionCode::ResourceOverflow;
    case SessionError::IdExhausted: return EventSessionCode::IdExhausted;
    case SessionError::InvalidBuild: return EventSessionCode::InvalidBuild;
    case SessionError::ConnectedPart: return EventSessionCode::ConnectedPart;
    case SessionError::JobUnavailable: return EventSessionCode::JobUnavailable;
    case SessionError::AdapterRejected: return EventSessionCode::AdapterRejected;
    case SessionError::Canceled: return EventSessionCode::Canceled;
    case SessionError::NoPending: return EventSessionCode::NoPending;
    case SessionError::UnsupportedFeature: return EventSessionCode::UnsupportedFeature;
    case SessionError::JournalCapacity: return EventSessionCode::JournalCapacity;
    case SessionError::CandidateCapacity: return EventSessionCode::CandidateCapacity;
    case SessionError::Closed: return EventSessionCode::Closed;
    case SessionError::HistoryUnavailable: return EventSessionCode::HistoryUnavailable;
    case SessionError::HistoryConflict: return EventSessionCode::HistoryConflict;
    case SessionError::HistoryBusy: return EventSessionCode::HistoryBusy;
    case SessionError::HistoryCapacity: return EventSessionCode::HistoryCapacity;
    case SessionError::HistoryExhausted: return EventSessionCode::HistoryExhausted;
    case SessionError::UnknownCargo: return EventSessionCode::UnknownCargo;
    case SessionError::CargoUnavailable: return EventSessionCode::CargoUnavailable;
    case SessionError::DeliveryNotReady: return EventSessionCode::DeliveryNotReady;
    }
    return std::nullopt;
}
namespace {
bool validCode(EventSessionCode code) noexcept {
    switch (code) {
    case EventSessionCode::None:
    case EventSessionCode::InvalidBootstrap:
    case EventSessionCode::Capacity:
    case EventSessionCode::InvalidIdentity:
    case EventSessionCode::WrongCaller:
    case EventSessionCode::WrongToken:
    case EventSessionCode::WrongEpoch:
    case EventSessionCode::SequenceGap:
    case EventSessionCode::SequenceExhausted:
    case EventSessionCode::AlreadyProcessed:
    case EventSessionCode::RequestConflict:
    case EventSessionCode::Busy:
    case EventSessionCode::StaleRevision:
    case EventSessionCode::RevisionExhausted:
    case EventSessionCode::UnknownBuild:
    case EventSessionCode::UnknownPart:
    case EventSessionCode::UnknownJob:
    case EventSessionCode::NotOwner:
    case EventSessionCode::LeaseDenied:
    case EventSessionCode::InsufficientResources:
    case EventSessionCode::ResourceOverflow:
    case EventSessionCode::IdExhausted:
    case EventSessionCode::InvalidBuild:
    case EventSessionCode::ConnectedPart:
    case EventSessionCode::JobUnavailable:
    case EventSessionCode::AdapterRejected:
    case EventSessionCode::Canceled:
    case EventSessionCode::NoPending:
    case EventSessionCode::UnsupportedFeature:
    case EventSessionCode::JournalCapacity:
    case EventSessionCode::CandidateCapacity:
    case EventSessionCode::Closed:
    case EventSessionCode::HistoryUnavailable:
    case EventSessionCode::HistoryConflict:
    case EventSessionCode::HistoryBusy:
    case EventSessionCode::HistoryCapacity:
    case EventSessionCode::UnknownCargo: case EventSessionCode::CargoUnavailable: case EventSessionCode::DeliveryNotReady:
    case EventSessionCode::HistoryExhausted:
        return true;
    }
    return false;
}
} // namespace
std::optional<EventBuildCode> eventCode(construction::BuildError code) noexcept {
    switch (code) {
    case construction::BuildError::None: return EventBuildCode::None;
    case construction::BuildError::Capacity: return EventBuildCode::Capacity;
    case construction::BuildError::InvalidId: return EventBuildCode::InvalidId;
    case construction::BuildError::DuplicateId: return EventBuildCode::DuplicateId;
    case construction::BuildError::WrongWorld: return EventBuildCode::WrongWorld;
    case construction::BuildError::WrongBuild: return EventBuildCode::WrongBuild;
    case construction::BuildError::UnknownDefinition: return EventBuildCode::UnknownDefinition;
    case construction::BuildError::UnknownDefinitionVersion: return EventBuildCode::UnknownDefinitionVersion;
    case construction::BuildError::InvalidPlacement: return EventBuildCode::InvalidPlacement;
    case construction::BuildError::InvalidHealth: return EventBuildCode::InvalidHealth;
    case construction::BuildError::InvalidSettings: return EventBuildCode::InvalidSettings;
    case construction::BuildError::InvalidProvenance: return EventBuildCode::InvalidProvenance;
    case construction::BuildError::InvalidLease: return EventBuildCode::InvalidLease;
    case construction::BuildError::UnknownPart: return EventBuildCode::UnknownPart;
    case construction::BuildError::UnknownSocket: return EventBuildCode::UnknownSocket;
    case construction::BuildError::SamePartConnection: return EventBuildCode::SamePartConnection;
    case construction::BuildError::IncompatibleSocket: return EventBuildCode::IncompatibleSocket;
    case construction::BuildError::InvalidConnection: return EventBuildCode::InvalidConnection;
    case construction::BuildError::DuplicateConnection: return EventBuildCode::DuplicateConnection;
    case construction::BuildError::SocketCapacity: return EventBuildCode::SocketCapacity;
    case construction::BuildError::MisalignedWeld: return EventBuildCode::MisalignedWeld;
    case construction::BuildError::SolidOverlap: return EventBuildCode::SolidOverlap;
    case construction::BuildError::ClearanceBlocked: return EventBuildCode::ClearanceBlocked;
    case construction::BuildError::StaleRevision: return EventBuildCode::StaleRevision;
    case construction::BuildError::RevisionExhausted: return EventBuildCode::RevisionExhausted;
    case construction::BuildError::ImmutableIdentity: return EventBuildCode::ImmutableIdentity;
    case construction::BuildError::InvalidEncoding: return EventBuildCode::InvalidEncoding;
    case construction::BuildError::UnsupportedSchema: return EventBuildCode::UnsupportedSchema;
    case construction::BuildError::NonCanonicalOrder: return EventBuildCode::NonCanonicalOrder;
    }
    return std::nullopt;
}
namespace {
bool validCode(EventBuildCode code) noexcept {
    switch (code) {
    case EventBuildCode::None:
    case EventBuildCode::Capacity:
    case EventBuildCode::InvalidId:
    case EventBuildCode::DuplicateId:
    case EventBuildCode::WrongWorld:
    case EventBuildCode::WrongBuild:
    case EventBuildCode::UnknownDefinition:
    case EventBuildCode::UnknownDefinitionVersion:
    case EventBuildCode::InvalidPlacement:
    case EventBuildCode::InvalidHealth:
    case EventBuildCode::InvalidSettings:
    case EventBuildCode::InvalidProvenance:
    case EventBuildCode::InvalidLease:
    case EventBuildCode::UnknownPart:
    case EventBuildCode::UnknownSocket:
    case EventBuildCode::SamePartConnection:
    case EventBuildCode::IncompatibleSocket:
    case EventBuildCode::InvalidConnection:
    case EventBuildCode::DuplicateConnection:
    case EventBuildCode::SocketCapacity:
    case EventBuildCode::MisalignedWeld:
    case EventBuildCode::SolidOverlap:
    case EventBuildCode::ClearanceBlocked:
    case EventBuildCode::StaleRevision:
    case EventBuildCode::RevisionExhausted:
    case EventBuildCode::ImmutableIdentity:
    case EventBuildCode::InvalidEncoding:
    case EventBuildCode::UnsupportedSchema:
    case EventBuildCode::NonCanonicalOrder:
        return true;
    }
    return false;
}
} // namespace


bool isValid(EventStreamIdentity identity) noexcept {
    return construction::isValid(identity.world) && identity.epoch.valid()
        && identity.incarnation.bytes != std::array<uint8_t, 16>{}
        && identity.incarnation.bytes != identity.world.bytes;
}
EventKind eventKind(const EventPayload& payload) noexcept {
    if (std::holds_alternative<ReceiptChangedEvent>(payload)) return EventKind::ReceiptChanged;
    if (std::holds_alternative<StateCommittedEvent>(payload)) return EventKind::StateCommitted;
    if (std::holds_alternative<AdmissionClosedEvent>(payload)) return EventKind::AdmissionClosed;
    return EventKind::DiagnosticSummary;
}
uint16_t eventPayloadBytes(EventKind kind) noexcept {
    switch (kind) {
    case EventKind::ReceiptChanged: return 56;
    case EventKind::StateCommitted: return 64;
    case EventKind::AdmissionClosed: return 24;
    case EventKind::DiagnosticSummary: return 48;
    default: return 0;
    }
}
EventValidation validateEvent(const EventRecord& record) noexcept {
    const auto& h = record.header;
    if (h.envelopeVersion != kEventEnvelopeVersion || h.payloadVersion != kEventPayloadVersion)
        return EventValidation::UnsupportedVersion;
    if (!isValid(h.stream)) return EventValidation::InvalidIdentity;
    if (!h.sequence.valid()) return EventValidation::InvalidSequence;
    if (eventPayloadBytes(h.kind) == 0) return EventValidation::UnsupportedKind;
    if (h.kind != eventKind(record.payload) || h.payloadBytes != eventPayloadBytes(h.kind))
        return EventValidation::InvalidPayload;
    if (h.lane != (h.kind == EventKind::DiagnosticSummary ? EventLane::Telemetry : EventLane::Domain))
        return EventValidation::WrongLane;
    // No implemented payload carries certified completed-physics evidence yet.
    if (h.tickBasis != EventTickBasis::LogicalBoundary) return EventValidation::WrongTickBasis;
    const auto id = [&](DurableId object) { return construction::isValid(object) && object.world == h.stream.world; };
    const auto request = [&](PublicRequestReference value) { return id(value.participant) && value.sequence.valid(); };
    const auto journal = [](const std::optional<EventJournalReference>& value) {
        return !value || (value->writer.valid() && value->sequence.valid());
    };
    if (h.build && !id(h.build->build)) return EventValidation::InvalidIdentity;
    bool valid = false;
    if (const auto* receipt = std::get_if<ReceiptChangedEvent>(&record.payload)) {
        valid = request(receipt->request) && validCode(receipt->error) && validCode(receipt->buildError)
            && journal(receipt->journal) && receipt->durability == EventDurability::Volatile
            && (!receipt->object || id(*receipt->object))
            && (!receipt->expectedSequence.valid() || receipt->expectedSequence > receipt->request.sequence);
        switch (receipt->outcome) {
        case EventOutcome::Pending:
            valid &= receipt->error == EventSessionCode::None && receipt->buildError == EventBuildCode::None
                && !receipt->object && receipt->journal.has_value();
            break;
        case EventOutcome::Rejected:
            valid &= receipt->error != EventSessionCode::None && !receipt->object;
            break;
        case EventOutcome::Committed:
            valid &= receipt->error == EventSessionCode::None && receipt->buildError == EventBuildCode::None;
            break;
        default: valid = false; break;
        }
    } else if (const auto* state = std::get_if<StateCommittedEvent>(&record.payload)) {
        valid = (!state->cause || request(*state->cause)) && state->changes != 0
            && (state->changes & ~uint16_t{0x003f}) == 0 && (!state->primary || id(*state->primary))
            && journal(state->journal) && state->durability == EventDurability::Volatile;
    } else if (const auto* closed = std::get_if<AdmissionClosedEvent>(&record.payload)) {
        valid = (closed->reason == EventCloseReason::HostClosed || closed->reason == EventCloseReason::JournalFault)
            && journal(closed->journal) && !h.build;
    } else if (const auto* diagnostic = std::get_if<DiagnosticSummaryEvent>(&record.payload)) {
        valid = !h.build && diagnostic->intervalBegin <= diagnostic->intervalEnd && diagnostic->intervalEnd <= h.tick
            && (!diagnostic->countSaturated || diagnostic->count == std::numeric_limits<uint64_t>::max())
            && std::isfinite(diagnostic->meanMicroseconds) && std::isfinite(diagnostic->maximumMicroseconds)
            && !std::signbit(diagnostic->meanMicroseconds) && !std::signbit(diagnostic->maximumMicroseconds)
            && diagnostic->maximumMicroseconds >= diagnostic->meanMicroseconds;
        switch (diagnostic->code) {
        case EventDiagnosticCode::IngressRejected:
        case EventDiagnosticCode::ObserverOverwrites:
            valid &= diagnostic->meanMicroseconds == 0 && diagnostic->maximumMicroseconds == 0;
            break;
        case EventDiagnosticCode::OwnerBoundaryCost:
            valid &= diagnostic->count != 0 || diagnostic->maximumMicroseconds == 0;
            break;
        default: valid = false; break;
        }
    }
    return valid ? EventValidation::Valid : EventValidation::InvalidPayload;
}

namespace {
// Used only after complete fixed-size bounds checks, on stack-owned output.
struct WireWriter {
    std::span<std::byte> bytes;
    size_t position = 0;
    void integer(uint64_t value, size_t width) noexcept {
        for (size_t i = 0; i < width; ++i) bytes[position++] = static_cast<std::byte>((value >> (8 * i)) & 255);
    }
    void word(uint16_t value) noexcept { integer(value, 2); }
    void wide(uint64_t value) noexcept { integer(value, 8); }
    void flag(uint8_t value) noexcept { integer(value, 1); }
    void idBytes(const std::array<uint8_t, 16>& value) noexcept { for (auto byte : value) flag(byte); }
    void journal(const std::optional<EventJournalReference>& value) noexcept {
        wide(value ? value->writer.value() : 0); wide(value ? value->sequence.value() : 0);
    }
};
struct WireReader {
    std::span<const std::byte> bytes;
    size_t position = 0;
    [[nodiscard]] uint64_t integer(size_t width) noexcept {
        uint64_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= static_cast<uint64_t>(bytes[position++]) << (8 * i);
        return value;
    }
    [[nodiscard]] uint16_t word() noexcept { return static_cast<uint16_t>(integer(2)); }
    [[nodiscard]] uint64_t wide() noexcept { return integer(8); }
    [[nodiscard]] uint8_t flag() noexcept { return static_cast<uint8_t>(integer(1)); }
    [[nodiscard]] std::array<uint8_t, 16> idBytes() noexcept {
        std::array<uint8_t, 16> value{}; for (auto& byte : value) byte = flag(); return value;
    }
    [[nodiscard]] std::optional<EventJournalReference> journal(bool present) noexcept {
        const auto writer = wide(); const auto sequence = wide();
        if (!present) return std::nullopt;
        return EventJournalReference{JournalWriterGeneration{writer}, JournalSequence{sequence}};
    }
};
constexpr uint32_t kEventMagic = 0x56455356; // ASCII VSEV.
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
} // namespace

EventEncodeResult encodeEvent(const EventRecord& record, std::span<std::byte> output) noexcept {
    const auto validation = validateEvent(record);
    if (validation != EventValidation::Valid) return {EventCodecError::InvalidRecord, validation, 0};
    const size_t size = kEventWireHeaderBytes + record.header.payloadBytes;
    if (output.size() < size) return {EventCodecError::NeedCapacity, EventValidation::Valid, 0};
    std::array<std::byte, kMaximumEventWireBytes> encoded{};
    WireWriter w{encoded};
    const auto& h = record.header;
    w.integer(kEventMagic, 4); w.word(h.envelopeVersion); w.word(static_cast<uint16_t>(h.kind));
    w.word(h.payloadVersion); w.word(h.payloadBytes); w.flag(static_cast<uint8_t>(h.lane));
    w.flag(static_cast<uint8_t>(h.tickBasis)); w.flag(h.build ? 1 : 0); w.flag(0);
    w.idBytes(h.stream.world.bytes); w.idBytes(h.stream.incarnation.bytes);
    w.wide(h.stream.epoch.value()); w.wide(h.sequence.value()); w.wide(h.tick.value()); w.wide(h.revision.value());
    w.wide(h.build ? h.build->build.counter : 0); w.wide(h.build ? h.build->revision.value() : 0);
    if (const auto* receipt = std::get_if<ReceiptChangedEvent>(&record.payload)) {
        w.wide(receipt->request.participant.counter); w.wide(receipt->request.sequence.value());
        w.flag(static_cast<uint8_t>(receipt->outcome)); w.flag(static_cast<uint8_t>(receipt->durability));
        w.word(static_cast<uint16_t>((receipt->object ? 1 : 0) | (receipt->journal ? 2 : 0)));
        w.word(static_cast<uint16_t>(receipt->error)); w.word(static_cast<uint16_t>(receipt->buildError));
        w.wide(receipt->expectedSequence.value()); w.wide(receipt->object ? receipt->object->counter : 0);
        w.journal(receipt->journal);
    } else if (const auto* state = std::get_if<StateCommittedEvent>(&record.payload)) {
        w.wide(state->cause ? state->cause->participant.counter : 0); w.wide(state->cause ? state->cause->sequence.value() : 0);
        w.word(static_cast<uint16_t>((state->cause ? 1 : 0) | (state->primary ? 2 : 0) | (state->journal ? 4 : 0)));
        w.word(state->changes); w.flag(static_cast<uint8_t>(state->durability)); w.integer(0, 3);
        w.wide(state->primary ? state->primary->counter : 0);
        w.wide(state->balance.salvageMaterial); w.wide(state->balance.specialMachinery); w.journal(state->journal);
    } else if (const auto* closed = std::get_if<AdmissionClosedEvent>(&record.payload)) {
        w.word(static_cast<uint16_t>(closed->reason)); w.word(closed->journal ? 1 : 0); w.integer(0, 4);
        w.journal(closed->journal);
    } else if (const auto* diagnostic = std::get_if<DiagnosticSummaryEvent>(&record.payload)) {
        w.word(static_cast<uint16_t>(diagnostic->code)); w.word(diagnostic->countSaturated ? 1 : 0); w.integer(0, 4);
        w.wide(diagnostic->count); w.wide(diagnostic->intervalBegin.value()); w.wide(diagnostic->intervalEnd.value());
        w.wide(std::bit_cast<uint64_t>(diagnostic->meanMicroseconds)); w.wide(std::bit_cast<uint64_t>(diagnostic->maximumMicroseconds));
    }
    std::copy_n(encoded.begin(), size, output.begin());
    return {EventCodecError::None, EventValidation::Valid, size};
}
EventDecodeResult decodeEvent(std::span<const std::byte> input) noexcept {
    if (input.size() < kEventWireHeaderBytes) return {EventCodecError::Truncated, EventValidation::Valid, {}};
    WireReader r{input};
    if (r.integer(4) != kEventMagic) return {EventCodecError::InvalidMagic, EventValidation::Valid, {}};
    EventRecord record;
    auto& h = record.header;
    h.envelopeVersion = r.word(); h.kind = static_cast<EventKind>(r.word());
    h.payloadVersion = r.word(); h.payloadBytes = r.word();
    const auto invalid = [](EventValidation why) { return EventDecodeResult{EventCodecError::InvalidRecord, why, {}}; };
    if (h.envelopeVersion != kEventEnvelopeVersion || h.payloadVersion != kEventPayloadVersion)
        return invalid(EventValidation::UnsupportedVersion);
    if (eventPayloadBytes(h.kind) == 0) return invalid(EventValidation::UnsupportedKind);
    if (h.payloadBytes != eventPayloadBytes(h.kind)) return invalid(EventValidation::InvalidPayload);
    const size_t size = kEventWireHeaderBytes + h.payloadBytes;
    if (input.size() < size) return {EventCodecError::Truncated, EventValidation::Valid, {}};
    if (input.size() > size) return {EventCodecError::TrailingBytes, EventValidation::Valid, {}};
    h.lane = static_cast<EventLane>(r.flag()); h.tickBasis = static_cast<EventTickBasis>(r.flag());
    const auto buildFlag = r.flag(); static_cast<void>(r.flag());
    h.stream.world.bytes = r.idBytes(); h.stream.incarnation.bytes = r.idBytes();
    h.stream.epoch = AuthorityEpoch{r.wide()}; h.sequence = EventSequence{r.wide()};
    h.tick = SimulationTick{r.wide()}; h.revision = SessionRevision{r.wide()};
    const auto build = r.wide(); const auto revision = r.wide();
    if (buildFlag & 1) h.build = EventBuildReference{{h.stream.world, build}, TopologyRevision{revision}};
    if (h.kind == EventKind::ReceiptChanged) {
        ReceiptChangedEvent receipt;
        receipt.request.participant = {h.stream.world, r.wide()}; receipt.request.sequence = RequestSequence{r.wide()};
        receipt.outcome = static_cast<EventOutcome>(r.flag()); receipt.durability = static_cast<EventDurability>(r.flag());
        const auto flags = r.word(); receipt.error = static_cast<EventSessionCode>(r.word()); receipt.buildError = static_cast<EventBuildCode>(r.word());
        receipt.expectedSequence = RequestSequence{r.wide()}; const auto object = r.wide();
        if (flags & 1) receipt.object = DurableId{h.stream.world, object};
        receipt.journal = r.journal((flags & 2) != 0); record.payload = receipt;
    } else if (h.kind == EventKind::StateCommitted) {
        StateCommittedEvent state;
        const auto participant = r.wide(); const auto sequence = r.wide(); const auto flags = r.word();
        if (flags & 1) state.cause = PublicRequestReference{{h.stream.world, participant}, RequestSequence{sequence}};
        state.changes = r.word(); state.durability = static_cast<EventDurability>(r.flag()); static_cast<void>(r.integer(3));
        const auto primary = r.wide(); if (flags & 2) state.primary = DurableId{h.stream.world, primary};
        state.balance = {r.wide(), r.wide()}; state.journal = r.journal((flags & 4) != 0); record.payload = state;
    } else if (h.kind == EventKind::AdmissionClosed) {
        AdmissionClosedEvent closed; closed.reason = static_cast<EventCloseReason>(r.word());
        const auto flags = r.word(); static_cast<void>(r.integer(4)); closed.journal = r.journal((flags & 1) != 0); record.payload = closed;
    } else {
        DiagnosticSummaryEvent diagnostic; diagnostic.code = static_cast<EventDiagnosticCode>(r.word());
        const auto flags = r.word(); static_cast<void>(r.integer(4)); diagnostic.countSaturated = (flags & 1) != 0;
        diagnostic.count = r.wide(); diagnostic.intervalBegin = SimulationTick{r.wide()}; diagnostic.intervalEnd = SimulationTick{r.wide()};
        diagnostic.meanMicroseconds = std::bit_cast<double>(r.wide()); diagnostic.maximumMicroseconds = std::bit_cast<double>(r.wide());
        record.payload = diagnostic;
    }
    const auto validation = validateEvent(record);
    if (validation != EventValidation::Valid) return invalid(validation);
    // Also rejects unknown flag bits, nonzero reserved/absent fields and all
    // alternate encodings. Re-encoding is stack-only and bounded by 160 bytes.
    std::array<std::byte, kMaximumEventWireBytes> canonical{};
    const auto encoded = encodeEvent(record, canonical);
    if (encoded.error != EventCodecError::None || !std::equal(input.begin(), input.end(), canonical.begin()))
        return {EventCodecError::NonCanonical, EventValidation::Valid, {}};
    return {EventCodecError::None, EventValidation::Valid, record};
}

std::unique_ptr<SessionEventHub> SessionEventHub::create(EventStreamIdentity identity) {
    if (!isValid(identity)) return nullptr;
    try { return std::unique_ptr<SessionEventHub>(new SessionEventHub(identity)); }
    catch (const std::bad_alloc&) { return nullptr; }
}
EventPublishResult SessionEventHub::publish(EventLane lane, const EventDraft& draft) noexcept {
    switch (lane) {
    case EventLane::Domain: return domain_.publish(draft);
    case EventLane::Presentation: return presentation_.publish(draft);
    case EventLane::Telemetry: return telemetry_.publish(draft);
    }
    return {EventPublishStatus::InvalidInput, EventValidation::WrongLane, {}};
}
EventStreamIdentity SessionEventReader::identity() const noexcept { return hub_->identity_; }
std::optional<EventLaneStats> SessionEventReader::stats(EventLane lane) const noexcept {
    switch (lane) {
    case EventLane::Domain: return hub_->domain_.stats();
    case EventLane::Presentation: return hub_->presentation_.stats();
    case EventLane::Telemetry: return hub_->telemetry_.stats();
    }
    return std::nullopt;
}
EventReadResult SessionEventReader::read(EventCursor cursor, std::span<EventRecord> output) const noexcept {
    switch (cursor.lane) {
    case EventLane::Domain: return hub_->domain_.read(cursor, output);
    case EventLane::Presentation: return hub_->presentation_.read(cursor, output);
    case EventLane::Telemetry: return hub_->telemetry_.read(cursor, output);
    }
    return {EventReadStatus::WrongLane, cursor, {}, 0};
}

} // namespace voxy::game::expedition
