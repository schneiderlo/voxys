#include "game/expedition/session_events.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace voxy::game::expedition;
constexpr auto kMax = std::numeric_limits<uint64_t>::max();

EventStreamIdentity identity() {
    EventStreamIdentity result;
    for (size_t i = 0; i < 16; ++i) {
        result.world.bytes[i] = static_cast<uint8_t>(i + 1);
        result.incarnation.bytes[i] = static_cast<uint8_t>(0xa1 + i);
    }
    result.epoch = AuthorityEpoch{0x0020000000000001ULL};
    return result;
}
DurableId object(uint64_t counter) { return {identity().world, counter}; }
EventDraft stateDraft(uint64_t revision = 1) {
    StateCommittedEvent state;
    state.cause = PublicRequestReference{object(1), RequestSequence{revision}};
    state.changes = changeBit(EventChange::Builds) | changeBit(EventChange::Inventory);
    state.primary = object(3); state.balance = {9000, 40};
    state.journal = EventJournalReference{JournalWriterGeneration{1}, JournalSequence{revision}};
    return {SimulationTick{revision}, SessionRevision{revision}, EventBuildReference{object(3), TopologyRevision{revision}}, state};
}
EventDraft diagnosticDraft(uint64_t tick = 1) {
    return {SimulationTick{tick}, SessionRevision{}, {},
        DiagnosticSummaryEvent{EventDiagnosticCode::OwnerBoundaryCost, 4, SimulationTick{0}, SimulationTick{tick}, 2.5, 8.0, false}};
}
EventCursor cursor(EventLane lane = EventLane::Domain, uint64_t through = 0) {
    return {identity().incarnation, lane, EventFrontier{through}};
}
EventRecord record(EventPayload payload) {
    EventRecord result;
    result.header = {identity(), EventSequence{0xfedcba9876543210ULL}, SimulationTick{17}, SessionRevision{5},
        {}, kEventEnvelopeVersion, eventKind(payload), kEventPayloadVersion,
        eventPayloadBytes(eventKind(payload)), std::holds_alternative<DiagnosticSummaryEvent>(payload) ? EventLane::Telemetry : EventLane::Domain};
    result.payload = payload;
    return result;
}
std::array<EventRecord, 4> codecFixtures() {
    ReceiptChangedEvent receipt;
    receipt.request = {object(1), RequestSequence{0x0020000000000001ULL}};
    receipt.outcome = EventOutcome::Committed; receipt.expectedSequence = RequestSequence{0x0020000000000002ULL};
    receipt.object = object(11); receipt.journal = EventJournalReference{JournalWriterGeneration{7}, JournalSequence{kMax}};
    auto r = record(receipt); r.header.build = EventBuildReference{object(3), TopologyRevision{9}};
    StateCommittedEvent state;
    state.changes = changeBit(EventChange::History) | changeBit(EventChange::Entitlements);
    state.primary = object(20); state.balance = {kMax, 0x0020000000000001ULL};
    state.journal = EventJournalReference{JournalWriterGeneration{7}, JournalSequence{kMax}};
    return {r, record(state), record(AdmissionClosedEvent{EventCloseReason::JournalFault, {}}),
        record(DiagnosticSummaryEvent{EventDiagnosticCode::OwnerBoundaryCost, 4, SimulationTick{12}, SimulationTick{17}, 2.5, 8, false})};
}
std::vector<std::byte> fromHex(std::string_view value) {
    std::vector<std::byte> out;
    auto digit = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
    for (size_t i = 0; i < value.size(); i += 2)
        out.push_back(static_cast<std::byte>(digit(value[i]) * 16 + digit(value[i + 1])));
    return out;
}
// Frozen independent Python struct.pack fixtures; no native codec generated them.
constexpr std::array<std::string_view, 4> kGolden{
    "565345560100010001003800010101000102030405060708090a0b0c0d0e0f10a1a2a3a4a5a6a7a8a9aaabacadaeafb001000000000020001032547698badcfe110000000000000005000000000000000300000000000000090000000000000001000000000000000100000000002000030103000000000002000000000020000b000000000000000700000000000000ffffffffffffffff",
    "565345560100020001004000010100000102030405060708090a0b0c0d0e0f10a1a2a3a4a5a6a7a8a9aaabacadaeafb001000000000020001032547698badcfe11000000000000000500000000000000000000000000000000000000000000000000000000000000000000000000000006001800010000001400000000000000ffffffffffffffff01000000000020000700000000000000ffffffffffffffff",
    "565345560100030001001800010100000102030405060708090a0b0c0d0e0f10a1a2a3a4a5a6a7a8a9aaabacadaeafb001000000000020001032547698badcfe1100000000000000050000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000",
    "565345560100000301003000030100000102030405060708090a0b0c0d0e0f10a1a2a3a4a5a6a7a8a9aaabacadaeafb001000000000020001032547698badcfe1100000000000000050000000000000000000000000000000000000000000000030000000000000004000000000000000c00000000000000110000000000000000000000000004400000000000002040",
};

TEST(SessionEvents, OverwriteRequiresExplicitGapRecoveryAndDoesNotAdvanceOtherReaders) {
    detail::EventRing<3> ring(identity(), EventLane::Domain);
    auto fast = cursor(); const auto stalled = cursor();
    std::array<EventRecord, 2> output{};
    for (uint64_t n = 1; n <= 301; ++n) {
        ASSERT_EQ(ring.publish(stateDraft(n)).status, EventPublishStatus::Published);
        const auto read = ring.read(fast, output);
        ASSERT_EQ(read.status, EventReadStatus::Read); ASSERT_EQ(read.count, 1u);
        EXPECT_EQ(output[0].header.sequence.value(), n); EXPECT_EQ(read.next.lastConsumed.value(), n);
        fast = read.next;
    }
    const auto before = output;
    const auto gap = ring.read(stalled, output);
    EXPECT_EQ(gap.status, EventReadStatus::Gap); EXPECT_EQ(gap.count, 0u);
    EXPECT_EQ(gap.next, stalled); EXPECT_EQ(output, before);
    EXPECT_EQ(gap.stats.oldest.value(), 299u); EXPECT_EQ(gap.stats.newest.value(), 301u);
    EXPECT_EQ(gap.stats.overwritten.value, 298u); EXPECT_EQ(gap.stats.occupancy, 3u);
    EXPECT_EQ(gap.stats.highWater, 3u); EXPECT_EQ(gap.stats.capacity, 3u);
    auto read = ring.read(cursor(EventLane::Domain, 298), output);
    ASSERT_EQ(read.count, 2u); EXPECT_EQ(output[0].header.sequence.value(), 299u);
    EXPECT_EQ(output[1].header.sequence.value(), 300u);
    read = ring.read(read.next, output); EXPECT_EQ(read.count, 1u); EXPECT_EQ(output[0].header.sequence.value(), 301u);
    EXPECT_EQ(ring.read(read.next, output).count, 0u); EXPECT_EQ(stalled.lastConsumed.value(), 0u);
}
TEST(SessionEvents, InvalidAndEmptyReadsNeverAcknowledgeOrPartlyOverwriteOutput) {
    detail::EventRing<3> ring(identity(), EventLane::Domain);
    ASSERT_EQ(ring.publish(stateDraft()).status, EventPublishStatus::Published);
    std::array<EventRecord, 2> output{}; output[0] = codecFixtures()[0]; const auto before = output;
    auto wrong = cursor(); wrong.incarnation.bytes[0] ^= 1;
    const std::array<std::pair<EventCursor, EventReadStatus>, 4> cases{{
        {wrong, EventReadStatus::WrongIncarnation}, {cursor(EventLane::Telemetry), EventReadStatus::WrongLane},
        {cursor(EventLane::Domain, 2), EventReadStatus::FutureCursor},
        {{identity().incarnation, EventLane::Domain, {}, 2}, EventReadStatus::UnsupportedVersion},
    }};
    for (const auto& [value, expected] : cases) {
        auto read = ring.read(value, output); EXPECT_EQ(read.status, expected);
        EXPECT_EQ(read.count, 0u); EXPECT_EQ(read.next, value); EXPECT_EQ(output, before);
    }
    const auto empty = ring.read(cursor(), {});
    EXPECT_EQ(empty.status, EventReadStatus::NeedCapacity); EXPECT_EQ(empty.next, cursor());
    EXPECT_EQ(ring.stats().publishedThrough.value(), 1u);
}
TEST(SessionEvents, SingleSlotRingAndMultipleIndependentObserversRemainBounded) {
    detail::EventRing<1> ring(identity(), EventLane::Domain);
    std::array<EventRecord, 1> a{}, b{};
    ASSERT_EQ(ring.publish(stateDraft(1)).status, EventPublishStatus::Published);
    auto readA = ring.read(cursor(), a); auto readB = ring.read(cursor(), b);
    ASSERT_EQ(readA.count, 1u); EXPECT_EQ(a, b);
    std::get<StateCommittedEvent>(a[0].payload).balance = {kMax, kMax};
    readB = ring.read(cursor(), b); EXPECT_EQ(std::get<StateCommittedEvent>(b[0].payload).balance.salvageMaterial, 9000u);
    ASSERT_EQ(ring.publish(stateDraft(2)).status, EventPublishStatus::Published);
    EXPECT_EQ(ring.read(cursor(), a).status, EventReadStatus::Gap);
    EXPECT_EQ(ring.read(readA.next, a).count, 1u); EXPECT_EQ(ring.read(readB.next, b).count, 1u);
    EXPECT_EQ(a, b); EXPECT_EQ(ring.stats().overwritten.value, 1u);
}
TEST(SessionEvents, TelemetryFloodAndBadIngressCannotEvictDomainHistory) {
    detail::EventRing<3> domain(identity(), EventLane::Domain);
    detail::EventRing<3> telemetry(identity(), EventLane::Telemetry);
    ASSERT_EQ(domain.publish(stateDraft()).status, EventPublishStatus::Published);
    const auto authorityLane = domain.stats();
    for (uint64_t n = 1; n <= 10000; ++n) {
        ASSERT_EQ(telemetry.publish(diagnosticDraft(n)).status, EventPublishStatus::Published);
        ASSERT_EQ(telemetry.publish(stateDraft(n)).status, EventPublishStatus::InvalidInput);
    }
    EXPECT_EQ(domain.stats(), authorityLane); EXPECT_EQ(telemetry.stats().overwritten.value, 9997u);
    EXPECT_EQ(telemetry.stats().invalidInput.value, 10000u);
    std::array<EventRecord, 3> records{};
    const auto read = domain.read(cursor(), records); EXPECT_EQ(read.count, 1u);
    EXPECT_EQ(std::get<StateCommittedEvent>(records[0].payload).balance, (ResourceAmounts{9000, 40}));
}
TEST(SessionEvents, DomainPublicationRejectsRegressedBoundariesButAllowsSameRevisionReceipts) {
    detail::EventRing<3> ring(identity(), EventLane::Domain);
    ASSERT_EQ(ring.publish(stateDraft(10)).status, EventPublishStatus::Published);
    auto draft = stateDraft(9);
    EXPECT_EQ(ring.publish(draft).validation, EventValidation::RegressedBoundary);
    draft.revision = SessionRevision{11}; EXPECT_EQ(ring.publish(draft).validation, EventValidation::RegressedBoundary);
    draft.tick = SimulationTick{11}; draft.revision = SessionRevision{9};
    EXPECT_EQ(ring.publish(draft).validation, EventValidation::RegressedBoundary);
    draft = stateDraft(10); EXPECT_EQ(ring.publish(draft).status, EventPublishStatus::Published);
    EXPECT_EQ(ring.stats().newest.value(), 2u); EXPECT_EQ(ring.stats().invalidInput.value, 3u);
    detail::EventRing<2> telemetry(identity(), EventLane::Telemetry);
    EXPECT_EQ(telemetry.publish(diagnosticDraft(10)).status, EventPublishStatus::Published);
    EXPECT_EQ(telemetry.publish(diagnosticDraft(3)).status, EventPublishStatus::Published);
}
TEST(SessionEvents, LastSequencePublishesOnceAndExhaustionNeverWraps) {
    detail::EventRing<3> ring(identity(), EventLane::Domain, EventFrontier{kMax - 2});
    ASSERT_EQ(ring.publish(stateDraft(1)).sequence->value(), kMax - 1);
    ASSERT_EQ(ring.publish(stateDraft(2)).sequence->value(), kMax);
    const auto before = ring.stats(); ASSERT_TRUE(before.exhausted);
    EXPECT_EQ(ring.publish(stateDraft(3)).status, EventPublishStatus::Exhausted); EXPECT_EQ(ring.stats(), before);
    std::array<EventRecord, 3> output{};
    auto read = ring.read(cursor(EventLane::Domain, kMax - 2), output);
    ASSERT_EQ(read.count, 2u); EXPECT_EQ(read.next.lastConsumed.value(), kMax);
    EXPECT_EQ(ring.read(read.next, output).count, 0u); EXPECT_EQ(ring.read(cursor(), output).status, EventReadStatus::Gap);
    auto replacement = identity(); replacement.incarnation.bytes[0] ^= 1;
    detail::EventRing<3> fresh(replacement, EventLane::Domain);
    EXPECT_EQ(fresh.publish(stateDraft(3)).sequence->value(), 1u);
    EXPECT_EQ(fresh.read(read.next, output).status, EventReadStatus::WrongIncarnation);
}
TEST(SessionEvents, DiagnosticCountersSaturateWithoutRecursivePublication) {
    SaturatingEventCount count{kMax - 1, false}; count.increment();
    EXPECT_EQ(count.value, kMax); EXPECT_FALSE(count.saturated);
    for (int n = 0; n < 20; ++n) count.increment();
    EXPECT_EQ(count.value, kMax); EXPECT_TRUE(count.saturated);
    auto diagnostic = diagnosticDraft(); auto& value = std::get<DiagnosticSummaryEvent>(diagnostic.payload);
    value.count = kMax; value.countSaturated = true;
    detail::EventRing<1> ring(identity(), EventLane::Telemetry);
    EXPECT_EQ(ring.publish(diagnostic).status, EventPublishStatus::Published);
    value.count = kMax - 1; EXPECT_EQ(ring.publish(diagnostic).status, EventPublishStatus::InvalidInput);
    EXPECT_EQ(ring.stats().newest.value(), 1u);
}
TEST(SessionEvents, PublicHubStartsEmptyAndReaderExposesNoWriterOrMutableRecords) {
    auto hub = SessionEventHub::create(identity()); ASSERT_NE(hub, nullptr);
    auto reader = hub->reader(); EXPECT_EQ(reader.identity(), identity());
    for (auto lane : {EventLane::Domain, EventLane::Presentation, EventLane::Telemetry}) {
        const auto stats = reader.stats(lane); ASSERT_TRUE(stats); EXPECT_EQ(stats->occupancy, 0u);
        EXPECT_EQ(stats->capacity, lane == EventLane::Telemetry ? 1024u : 256u);
        std::array<EventRecord, 1> records{}; EXPECT_EQ(reader.read(cursor(lane), records).count, 0u);
    }
    EXPECT_FALSE(reader.stats(static_cast<EventLane>(9)));
    EXPECT_EQ(reader.read(cursor(static_cast<EventLane>(9)), {}).status, EventReadStatus::WrongLane);
    EXPECT_EQ(SessionEventHub::create({}), nullptr);
    auto invalid = identity(); invalid.incarnation.bytes = invalid.world.bytes;
    EXPECT_EQ(SessionEventHub::create(invalid), nullptr);
    EXPECT_LE(sizeof(EventRecord), 256u); EXPECT_LE(SessionEventHub::slotBytes(), 384u * 1024u);
}
template<class T> concept CanPublish = requires(T value, EventDraft draft) { value.publish(EventLane::Domain, draft); };
static_assert(!CanPublish<SessionEventReader> && !CanPublish<SessionEventHub>);
static_assert(!std::is_default_constructible_v<SessionEventReader>);

TEST(SessionEventCodec, FourKindsMatchIndependentGoldenBytesIncludingWideCounters) {
    const auto fixtures = codecFixtures();
    for (size_t i = 0; i < fixtures.size(); ++i) {
        const auto golden = fromHex(kGolden[i]);
        std::array<std::byte, 170> output; output.fill(std::byte{0x5a});
        const auto result = encodeEvent(fixtures[i], output);
        ASSERT_EQ(result.error, EventCodecError::None); ASSERT_EQ(result.bytes, golden.size());
        EXPECT_TRUE(std::equal(golden.begin(), golden.end(), output.begin()));
        EXPECT_EQ(output[result.bytes], std::byte{0x5a});
        auto decoded = decodeEvent(golden); ASSERT_EQ(decoded.error, EventCodecError::None);
        ASSERT_TRUE(decoded.record); EXPECT_EQ(*decoded.record, fixtures[i]);
    }
}
TEST(SessionEventCodec, AllTruncationsAndTrailingBytesRejectWithoutPartialDecode) {
    for (const auto& hex : kGolden) {
        auto wire = fromHex(hex);
        for (size_t size = 0; size < wire.size(); ++size) {
            const auto result = decodeEvent(std::span<const std::byte>{wire}.first(size));
            EXPECT_EQ(result.error, EventCodecError::Truncated); EXPECT_FALSE(result.record);
        }
        wire.push_back(std::byte{0}); const auto result = decodeEvent(wire);
        EXPECT_EQ(result.error, EventCodecError::TrailingBytes); EXPECT_FALSE(result.record);
    }
}
TEST(SessionEventCodec, InvalidOrShortEncodingPreservesEntireDestination) {
    auto fixtures = codecFixtures();
    std::array<std::byte, kMaximumEventWireBytes> output; output.fill(std::byte{0x42}); const auto before = output;
    for (auto fixture : fixtures) {
        const auto shortSize = kEventWireHeaderBytes + fixture.header.payloadBytes - 1;
        EXPECT_EQ(encodeEvent(fixture, std::span<std::byte>{output}.first(shortSize)).error, EventCodecError::NeedCapacity);
        EXPECT_EQ(output, before); fixture.header.sequence = EventSequence{};
        EXPECT_EQ(encodeEvent(fixture, output).error, EventCodecError::InvalidRecord); EXPECT_EQ(output, before);
    }
}
TEST(SessionEventCodec, VersionIdentityKindLaneAndTokenlessReceiptValidationAreExplicit) {
    const auto original = codecFixtures()[0];
    auto value = original; value.header.envelopeVersion = 2;
    EXPECT_EQ(validateEvent(value), EventValidation::UnsupportedVersion);
    value = original; value.header.payloadVersion = 0; EXPECT_EQ(validateEvent(value), EventValidation::UnsupportedVersion);
    value = original; value.header.kind = EventKind::ForceSample; EXPECT_EQ(validateEvent(value), EventValidation::UnsupportedKind);
    value = original; value.header.lane = EventLane::Telemetry; EXPECT_EQ(validateEvent(value), EventValidation::WrongLane);
    value = original; value.header.tickBasis = EventTickBasis::CompletedSimulation; EXPECT_EQ(validateEvent(value), EventValidation::WrongTickBasis);
    value = original; value.header.stream.epoch = AuthorityEpoch{}; EXPECT_EQ(validateEvent(value), EventValidation::InvalidIdentity);
    value = original; value.header.build->build.world.bytes[0] ^= 1; EXPECT_EQ(validateEvent(value), EventValidation::InvalidIdentity);
    value = original; std::get<ReceiptChangedEvent>(value.payload).request.participant.counter = 0;
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<ReceiptChangedEvent>(value.payload).error = static_cast<EventSessionCode>(2);
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<ReceiptChangedEvent>(value.payload).buildError = static_cast<EventBuildCode>(1);
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<ReceiptChangedEvent>(value.payload).durability = static_cast<EventDurability>(2);
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<ReceiptChangedEvent>(value.payload).expectedSequence = RequestSequence{};
    EXPECT_EQ(validateEvent(value), EventValidation::Valid);
    auto pending = std::get<ReceiptChangedEvent>(value.payload); pending.outcome = EventOutcome::Pending; pending.object.reset();
    value.payload = pending; EXPECT_EQ(validateEvent(value), EventValidation::Valid);
    pending.journal.reset(); value.payload = pending; EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
}
TEST(SessionEventCodec, ReservedAndAbsentFieldsHaveExactlyOneEncoding) {
    // Offsets independently follow the documented wire table, not native layout.
    const std::array<std::pair<size_t, size_t>, 14> edits{{
        {0,15}, {0,14}, {0,114}, {0,115}, {1,14}, {1,80}, {1,88},
        {1,96}, {1,104}, {1,117}, {1,119}, {2,98}, {2,104}, {3,100},
    }};
    for (const auto& [fixture, offset] : edits) {
        auto wire = fromHex(kGolden[fixture]); wire[offset] |= std::byte{0x80};
        const auto decoded = decodeEvent(wire);
        EXPECT_EQ(decoded.error, EventCodecError::NonCanonical) << fixture << ':' << offset;
        EXPECT_FALSE(decoded.record);
    }
    auto wire = fromHex(kGolden[0]); wire[0] = std::byte{0}; EXPECT_EQ(decodeEvent(wire).error, EventCodecError::InvalidMagic);
    wire = fromHex(kGolden[0]); wire[4] = std::byte{2}; EXPECT_EQ(decodeEvent(wire).validation, EventValidation::UnsupportedVersion);
    wire = fromHex(kGolden[0]); wire[6] = std::byte{0}; wire[7] = std::byte{1};
    EXPECT_EQ(decodeEvent(wire).validation, EventValidation::UnsupportedKind);
    wire = fromHex(kGolden[0]); wire[10] = std::byte{55}; EXPECT_EQ(decodeEvent(wire).validation, EventValidation::InvalidPayload);
}
TEST(SessionEventCodec, DiagnosticsRejectNonfiniteNegativeUnmeasuredAndUncertifiedSamples) {
    auto original = codecFixtures()[3];
    for (double bad : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), -1.0, -0.0}) {
        auto value = original; std::get<DiagnosticSummaryEvent>(value.payload).meanMicroseconds = bad;
        EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
        value = original; std::get<DiagnosticSummaryEvent>(value.payload).maximumMicroseconds = bad;
        EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    }
    auto value = original; std::get<DiagnosticSummaryEvent>(value.payload).count = 0;
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<DiagnosticSummaryEvent>(value.payload).intervalEnd = SimulationTick{18};
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<DiagnosticSummaryEvent>(value.payload).code = EventDiagnosticCode::IngressRejected;
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
    value = original; std::get<DiagnosticSummaryEvent>(value.payload).meanMicroseconds = 9;
    EXPECT_EQ(validateEvent(value), EventValidation::InvalidPayload);
}
TEST(SessionEventCodec, ErrorAdaptersUseStableCodesAndRejectUnknownSourceEnums) {
    EXPECT_EQ(eventCode(SessionError::HistoryExhausted), EventSessionCode::HistoryExhausted);
    EXPECT_EQ(eventCode(construction::BuildError::MisalignedWeld), EventBuildCode::MisalignedWeld);
    EXPECT_EQ(static_cast<uint16_t>(*eventCode(SessionError::Capacity)), 0x1002);
    EXPECT_EQ(static_cast<uint16_t>(*eventCode(construction::BuildError::Capacity)), 0x2001);
    EXPECT_FALSE(eventCode(static_cast<SessionError>(255)));
    EXPECT_FALSE(eventCode(static_cast<construction::BuildError>(255)));
}
} // namespace
