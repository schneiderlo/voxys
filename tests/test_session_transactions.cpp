#include "game/expedition/session_transactions.hpp"
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace voxy::game::construction;
using namespace voxy::game::expedition;
constexpr WorldNamespace kWorld{{'j','o','u','r','n','a','l','-','f','i','x','t','u','r','e','1'}};
DurableId id(uint64_t value) { return {kWorld, value}; }
JournalIdentity identity() { return {kWorld, AuthorityEpoch{4}, JournalWriterGeneration{1}}; }
JournalRecord record() {
    return {kLogicalJournalSchema, kWorld, AuthorityEpoch{4}, {}, SimulationTick{17},
        AdmissionOpenedRecord{{id(1), id(2)}, AdmissionGeneration{1}, {}, {2, 64}}};
}
JournalRecord admission(uint64_t sequence = 1) {
    RequestAdmissionRecord request;
    request.key = {{id(1), id(2)}, AuthorityEpoch{4}, AdmissionGeneration{1}, RequestSequence{sequence}};
    request.command = {AuthorityEpoch{4}, RequestSequence{sequence}, {}, CreateBuild{}};
    request.admittedBefore = RequestFrontier{sequence - 1};
    request.assignedObject = id(3);
    request.allocator = {3, 64};
    auto result = record(); result.payload = request;
    return result;
}
std::unique_ptr<JournalOutbox> outbox(uint32_t capacity = 64, JournalSeed seed = {}, size_t byteLimit = kMaximumJournalPayloadBytes) {
    JournalError issue;
    auto result = JournalOutbox::create(identity(), {capacity, byteLimit}, seed, issue);
    if (!result) throw std::runtime_error("invalid outbox fixture");
    return result;
}
JournalDemand demand(uint32_t records) { return {records, records * kJournalRecordStorageBytes}; }
JournalReservation reserve(JournalOutbox& box, uint32_t count = 1) {
    const auto result = box.reserve(demand(count));
    if (!result) throw std::runtime_error("outbox reservation failed");
    return result.reservation;
}
TEST(SessionJournal, StrongCountersAndBoundedOwnedRecords) {
    static_assert(!std::is_same_v<RequestFrontier, RequestSequence>);
    static_assert(!std::is_same_v<JournalSequence, RequestSequence>);
    static_assert(!std::is_same_v<AdmissionGeneration, HistoryGeneration>);
    static_assert(std::is_nothrow_copy_assignable_v<JournalRecord>);
    EXPECT_TRUE(RequestFrontier{}.valid());
    EXPECT_FALSE(RequestSequence{}.valid());
    EXPECT_FALSE(JournalSequence{}.valid());
    EXPECT_LE(sizeof(JournalRecord), kMaximumJournalRecordBytes);
    EXPECT_LE(sizeof(JournalOutbox), kMaximumJournalPayloadBytes + size_t{8192});
}
TEST(SessionJournal, InvalidFactoryBoundsAndIdentityReject) {
    JournalError issue;
    auto bad = identity(); bad.writerGeneration = {};
    EXPECT_FALSE(JournalOutbox::create(bad, {}, {}, issue));
    EXPECT_EQ(issue, JournalError::InvalidIdentity);
    for (const auto limits : std::array<JournalLimits, 4>{{{0, 4096}, {65, 4096}, {1, 0}, {1, kMaximumJournalPayloadBytes + 1}}}) {
        EXPECT_FALSE(JournalOutbox::create(identity(), limits, {}, issue));
        EXPECT_EQ(issue, JournalError::InvalidLimits);
    }
}
TEST(SessionJournal, AggregateReservationsIncludeUnappendedClosure) {
    auto box = outbox(5);
    const auto closure = reserve(*box);
    const auto first = reserve(*box, 2);
    const auto second = reserve(*box, 2);
    EXPECT_EQ(box->reservedRecords(), 5);
    EXPECT_EQ(box->reserve(demand(1)).error, JournalError::Capacity);
    EXPECT_EQ(box->size(), 0);
    ASSERT_TRUE(box->appendReserved(first, admission(1)));
    ASSERT_TRUE(box->appendReserved(second, admission(2)));
    EXPECT_EQ(box->reservedRecords(), 3);
    EXPECT_EQ(box->size(), 2);
    EXPECT_EQ(box->reserve(demand(1)).error, JournalError::Capacity);
    // A cancellation cannot release its terminal reservation until the ordered
    // decision is appended; the outbox supports partial reservation consumption.
    EXPECT_TRUE(box->canAppend(first, record()));
    EXPECT_TRUE(box->canAppend(second, record()));
    EXPECT_TRUE(box->canAppend(closure, record()));
}
TEST(SessionJournal, SequenceFollowsAppendOrderNotReservationOrder) {
    auto box = outbox();
    const auto earlier = reserve(*box, 2);
    const auto later = reserve(*box);
    EXPECT_EQ(box->appendReserved(later, record()), JournalSequence{1});
    EXPECT_EQ(box->appendReserved(earlier, record()), JournalSequence{2});
    EXPECT_EQ(box->appendReserved(earlier, record()), JournalSequence{3});
    EXPECT_FALSE(box->appendReserved(earlier, record()));
    EXPECT_EQ(box->reservedRecords(), 0);
    EXPECT_EQ(box->appendedThrough(), JournalFrontier{3});
}
TEST(SessionJournal, ByteDemandAndReleaseAreCheckedSeparately) {
    auto box = outbox(8, {}, 3 * kJournalRecordStorageBytes);
    EXPECT_EQ(box->reserve({2, kJournalRecordStorageBytes}).error, JournalError::InvalidDemand);
    EXPECT_EQ(box->reserve({0, 0}).error, JournalError::InvalidDemand);
    const auto result = box->reserve({2, 3 * kJournalRecordStorageBytes});
    ASSERT_TRUE(result);
    EXPECT_EQ(box->reserve(demand(1)).error, JournalError::Capacity);
    ASSERT_TRUE(box->appendReserved(result.reservation, record()));
    EXPECT_EQ(box->reservedBytes(), 2 * kJournalRecordStorageBytes);
    ASSERT_TRUE(box->appendReserved(result.reservation, record()));
    EXPECT_EQ(box->reservedBytes(), 0);
    EXPECT_EQ(box->retainedBytes(), 2 * kJournalRecordStorageBytes);
    EXPECT_TRUE(box->reserve(demand(1)));
}
TEST(SessionJournal, DiscardIsIdempotentAndStaleTicketsCannotDiscardReplacement) {
    auto box = outbox(1);
    const auto old = reserve(*box);
    EXPECT_TRUE(box->discard(old));
    EXPECT_FALSE(box->discard(old));
    const auto current = reserve(*box);
    EXPECT_EQ(old.slot, current.slot);
    EXPECT_NE(old.generation, current.generation);
    EXPECT_FALSE(box->discard(old));
    EXPECT_FALSE(box->appendReserved(old, record()));
    auto foreign = current; foreign.identity.writerGeneration = JournalWriterGeneration{2};
    EXPECT_FALSE(box->discard(foreign));
    foreign = current; foreign.identity.epoch = AuthorityEpoch{3};
    EXPECT_FALSE(box->appendReserved(foreign, record()));
    foreign = current; foreign.identity.world.bytes[0] = 'x';
    EXPECT_FALSE(box->discard(foreign));
    EXPECT_EQ(box->reservedRecords(), 1);
    EXPECT_TRUE(box->appendReserved(current, record()));
}
TEST(SessionJournal, MalformedAppendPreservesReservationAndSequence) {
    auto box = outbox(); const auto ticket = reserve(*box);
    auto bad = record(); bad.schema = 2;
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad = record(); bad.sequence = JournalSequence{7};
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad = record(); bad.epoch = AuthorityEpoch{3};
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad = record(); std::get<AdmissionOpenedRecord>(bad.payload).caller.sessionToken = id(1);
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad = admission(); std::get<RequestAdmissionRecord>(bad.payload).processedThrough = RequestFrontier{2};
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad = admission(); std::get<RequestAdmissionRecord>(bad.payload).assignedObject = id(2);
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad = record(); bad.payload = AllocatorLeaseRecord{10, 9, 73};
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    bad.payload = AllocatorLeaseRecord{9, 9, 74};
    EXPECT_FALSE(box->appendReserved(ticket, bad));
    EXPECT_EQ(box->reservedRecords(), 1);
    EXPECT_EQ(box->appendedThrough(), JournalFrontier{0});
    EXPECT_TRUE(box->appendReserved(ticket, record()));
}
TEST(SessionJournal, ExactPrefixProofRejectsChangedIntentWorldAndResult) {
    auto box = outbox(); const auto ticket = reserve(*box, 2);
    ASSERT_TRUE(box->appendReserved(ticket, admission()));
    ASSERT_TRUE(box->appendReserved(ticket, admission(2)));
    std::array<JournalRecord, 2> copied{};
    ASSERT_EQ(box->copyPrefix(copied), 2);
    auto changed = copied;
    std::get<RequestAdmissionRecord>(changed[0].payload).command.intent = AcceptJob{id(19)};
    EXPECT_EQ(box->releaseModelWrittenPrefix(changed), JournalError::PrefixMismatch);
    changed = copied; changed[1].world.bytes[0] = 'x';
    EXPECT_EQ(box->releaseModelWrittenPrefix(changed), JournalError::PrefixMismatch);
    changed = copied; std::get<RequestAdmissionRecord>(changed[1].payload).assignedObject = id(4);
    EXPECT_EQ(box->releaseModelWrittenPrefix(changed), JournalError::PrefixMismatch);
    EXPECT_EQ(box->releaseModelWrittenPrefix(std::span<const JournalRecord>{copied}.subspan(1)), JournalError::PrefixMismatch);
    EXPECT_EQ(box->releaseModelWrittenPrefix({}), JournalError::PrefixMismatch);
    EXPECT_EQ(box->size(), 2);
    EXPECT_EQ(box->modelReleasedThrough(), JournalFrontier{0});
    EXPECT_EQ(box->releaseModelWrittenPrefix(std::span<const JournalRecord>{copied}.first(1)), JournalError::None);
    EXPECT_EQ(box->modelReleasedThrough(), JournalFrontier{1});
    EXPECT_EQ(box->releaseModelWrittenPrefix(copied), JournalError::PrefixMismatch);
    EXPECT_EQ(box->releaseModelWrittenPrefix(std::span<const JournalRecord>{copied}.subspan(1)), JournalError::None);
    EXPECT_EQ(box->size(), 0);
    EXPECT_EQ(box->appendedThrough(), JournalFrontier{2});
}
TEST(SessionJournal, RepeatedStorageWrapKeepsAllPrefixBytesAndOrder) {
    auto box = outbox(3);
    std::array<JournalRecord, 3> copied{};
    for (uint64_t batch = 0; batch < 100; ++batch) {
        const auto ticket = reserve(*box, 3);
        for (uint64_t i = 0; i < 3; ++i) {
            auto nextRecord = record(); nextRecord.tick = SimulationTick{batch * 3 + i};
            ASSERT_EQ(box->appendReserved(ticket, nextRecord), JournalSequence{batch * 3 + i + 1});
        }
        ASSERT_EQ(box->copyPrefix(copied), 3);
        for (size_t i = 0; i < copied.size(); ++i) {
            EXPECT_EQ(copied[i].tick.value(), batch * 3 + i);
            EXPECT_EQ(copied[i].sequence.value(), batch * 3 + i + 1);
        }
        ASSERT_EQ(box->releaseModelWrittenPrefix(copied), JournalError::None);
    }
    EXPECT_EQ(box->appendedThrough(), JournalFrontier{300});
}
TEST(SessionJournal, SequenceHeadroomIncludesOtherReservationsAndNeverWraps) {
    constexpr auto maximum = std::numeric_limits<uint64_t>::max();
    auto box = outbox(4, {JournalFrontier{maximum - 2}, 0});
    const auto first = reserve(*box); const auto second = reserve(*box);
    EXPECT_EQ(box->reserve(demand(1)).error, JournalError::SequenceExhausted);
    EXPECT_EQ(box->appendReserved(second, record()), JournalSequence{maximum - 1});
    EXPECT_EQ(box->appendReserved(first, record()), JournalSequence{maximum});
    std::array<JournalRecord, 2> copied{};
    ASSERT_EQ(box->copyPrefix(copied), 2);
    ASSERT_EQ(box->releaseModelWrittenPrefix(copied), JournalError::None);
    EXPECT_EQ(box->reserve(demand(1)).error, JournalError::SequenceExhausted);
    auto reservationExhausted = outbox(1, {{}, maximum});
    EXPECT_EQ(reservationExhausted->reserve(demand(1)).error, JournalError::ReservationExhausted);
}
TEST(SessionJournal, TerminalOutcomeCannotHidePartialEconomicPublication) {
    auto box = outbox(); const auto ticket = reserve(*box);
    auto candidate = record();
    RequestDecisionRecord decision;
    decision.key = {{id(1), id(2)}, AuthorityEpoch{4}, AdmissionGeneration{1}, RequestSequence{1}};
    decision.command = {AuthorityEpoch{4}, RequestSequence{1}, {}, CreateBuild{}};
    decision.allocator = {3, 64};
    decision.outcome.error = SessionError::Canceled;
    decision.balanceBefore = {100, 2}; decision.balanceAfter = {99, 2};
    candidate.payload = decision;
    EXPECT_FALSE(box->appendReserved(ticket, candidate));
    decision.balanceAfter = decision.balanceBefore;
    candidate.payload = decision;
    ASSERT_TRUE(box->appendReserved(ticket, candidate));
    std::array<JournalRecord, 1> copied{};
    ASSERT_EQ(box->copyPrefix(copied), 1);
    EXPECT_EQ(std::get<RequestDecisionRecord>(copied[0].payload).outcome.error, SessionError::Canceled);
    // A RAM prefix release does not modify the accepted decision or grant a
    // durability status; such a status is deliberately absent from the outbox.
    EXPECT_EQ(box->releaseModelWrittenPrefix(copied), JournalError::None);
    EXPECT_EQ(std::get<RequestDecisionRecord>(copied[0].payload).outcome.error, SessionError::Canceled);
}
TEST(SessionJournal, CommittedPartDeltaPreservesAsymmetricStateAndRejectsBrokenRevision) {
    auto box = outbox(); const auto ticket = reserve(*box);
    auto candidate = record();
    RequestDecisionRecord decision;
    decision.key = {{id(1), id(2)}, AuthorityEpoch{4}, AdmissionGeneration{1}, RequestSequence{1}};
    decision.command = {AuthorityEpoch{4}, RequestSequence{1}, {},
        AddPart{{id(3), TopologyRevision{8}}, {id(1000), 2}, {{137, 29, -211}, CubeRotation{4}}}};
    decision.afterRevision = SessionRevision{1};
    decision.balanceBefore = {90, 7}; decision.balanceAfter = {65, 4};
    decision.allocator = {4, 64};
    BuildTransition changed;
    changed.before = BuildHeader{id(3), TopologyRevision{8}, id(1), {}, true};
    changed.after = BuildHeader{id(3), TopologyRevision{9}, id(1), {}, true};
    PartInstance part; part.id = id(4); part.owningBuild = id(3); part.definition = {id(1000), 2};
    part.placement = {{137, 29, -211}, CubeRotation{4}};
    part.paint = {11, 193, 47, 231}; part.health = 8321;
    changed.afterPart = part;
    decision.objects = changed;
    decision.outcome = {ReceiptState::Committed, SessionError::None, BuildError::None, {},
        SessionRevision{1}, TopologyRevision{9}, id(4)};
    decision.history.after = HistoryGeneration{2};
    decision.history.action = HistoryAction::Record;
    decision.history.entry = HistoryEntryId{19};
    candidate.payload = decision;
    auto malformed = candidate;
    std::get<BuildTransition>(std::get<RequestDecisionRecord>(malformed.payload).objects).after->revision = TopologyRevision{8};
    EXPECT_FALSE(box->appendReserved(ticket, malformed));
    malformed = candidate;
    std::get<RequestDecisionRecord>(malformed.payload).history.evicted[31] = HistoryEntryId{9};
    EXPECT_FALSE(box->appendReserved(ticket, malformed));
    ASSERT_TRUE(box->appendReserved(ticket, candidate));
    std::array<JournalRecord, 1> copied{};
    ASSERT_EQ(box->copyPrefix(copied), 1);
    const auto& saved = std::get<RequestDecisionRecord>(copied[0].payload);
    EXPECT_EQ(saved.balanceBefore, decision.balanceBefore);
    EXPECT_EQ(saved.balanceAfter, decision.balanceAfter);
    EXPECT_EQ(std::get<BuildTransition>(saved.objects).afterPart, part);
    auto changedProof = copied;
    std::get<BuildTransition>(std::get<RequestDecisionRecord>(changedProof[0].payload).objects).afterPart->paint[1] ^= 1;
    EXPECT_EQ(box->releaseModelWrittenPrefix(changedProof), JournalError::PrefixMismatch);
    EXPECT_EQ(box->releaseModelWrittenPrefix(copied), JournalError::None);
}
} // namespace
