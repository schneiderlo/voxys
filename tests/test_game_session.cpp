#include "game/expedition/game_session.hpp"
#include "game/expedition/session_recovery.hpp"
#include "game/expedition/session_save.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace voxy::game::construction;
using namespace voxy::game::expedition;

constexpr WorldNamespace kWorld{{'s','e','s','s','i','o','n','-','f','i','x','t','u','r','e','1'}};
DurableId id(uint64_t value) { return {kWorld, value}; }
EventStreamIncarnation fixtureIncarnation() {
    // Deterministic test-only source; each newly constructed live hub is fresh.
    static uint64_t serial = 0;
    EventStreamIncarnation value{{'o','b','s','e','r','v','e','r'}};
    ++serial;
    for (size_t i = 0; i < 8; ++i) value.bytes[8 + i] = static_cast<uint8_t>(serial >> (8 * i));
    return value;
}

const PartCatalog& catalog() {
    static const auto result = [] {
        CatalogIssue issue;
        auto value = PartCatalog::create(makeStarterCatalogDraft(), issue);
        if (!value) throw std::runtime_error("invalid starter catalog");
        return std::move(*value);
    }();
    return result;
}

// Deliberately fake backend: slots model reservation/retirement lifetime, not
// bodies, mass or a real compiler's resource estimate. No GPU calls occur.
class FakeAdapter final : public PreparationAdapter {
public:
    struct Slot { PreparationTicket ticket{}; bool used = false, active = false, retiring = false; };
    std::array<Slot, 4> slots{};
    bool ready = true, rejectBegin = false, rejectPoll = false, allowActivation = true;
    bool stalePoll = false, deferDiscard = false, throwAfterReserve = false, unexpectedThrow = false;
    std::optional<PreparationTicket> forcedPoll{};
    uint32_t partCapacity = 256, begins = 0, activations = 0, discards = 0;
    SimulationTick activatedTick{},stagedTick{};
    bool stageAllowed=true,executionObserved=false;
    PreparationState stage(PreparationTicket,SimulationTick tick) noexcept override {
        if(!stageAllowed)return PreparationState::Rejected;
        stagedTick=tick;return PreparationState::Ready;
    }
    bool executionComplete(PreparationTicket,SimulationTick tick) const noexcept override {
        return executionObserved && tick==stagedTick;
    }
    std::optional<DurableId> lastRemovedBuild{};
    std::optional<CutWeld> lastCut{};
    PreparationResult begin(const PreparationRequest& request) override {
        ++begins;
        lastRemovedBuild = request.removedBuild;
        lastCut=request.cut?std::optional{*request.cut}:std::nullopt;
        if (rejectBegin || request.totalParts > partCapacity)
            return {request.ticket, PreparationState::Rejected};
        auto slot = std::find_if(slots.begin(), slots.end(), [](const auto& value) { return !value.used; });
        if (slot == slots.end()) return {request.ticket, PreparationState::Rejected};
        *slot = {request.ticket, true, false, false};
        if (throwAfterReserve) throw std::bad_alloc{};
        if (unexpectedThrow) throw std::runtime_error("injected adapter fault");
        return {request.ticket, ready ? PreparationState::Ready : PreparationState::Pending};
    }
    PreparationResult poll(PreparationTicket ticket) noexcept override {
        if (forcedPoll) return {*forcedPoll, PreparationState::Ready};
        if (stalePoll) return {{ticket.epoch, ticket.generation - 1}, PreparationState::Ready};
        return {ticket, rejectPoll ? PreparationState::Rejected
            : ready ? PreparationState::Ready : PreparationState::Pending};
    }
    bool canActivate(PreparationTicket ticket) const noexcept override {
        return allowActivation && std::any_of(slots.begin(), slots.end(), [&](const auto& value) {
            return value.used && value.ticket == ticket && !value.retiring;
        });
    }
    void activate(PreparationTicket ticket, SimulationTick tick) noexcept override {
        ++activations;
        activatedTick = tick;
        for (auto& slot : slots) {
            if (slot.active) { slot.active = false; slot.retiring = true; }
            if (slot.used && slot.ticket == ticket) slot.active = true;
        }
    }
    void discard(PreparationTicket ticket) noexcept override {
        ++discards;
        for (auto& slot : slots) if (slot.used && slot.ticket == ticket) {
            if (deferDiscard) slot.retiring = true;
            else slot = {};
        }
    }
    void completeRetirement() { for (auto& slot : slots) if (slot.retiring) slot = {}; }
    size_t resident() const {
        return static_cast<size_t>(std::count_if(slots.begin(), slots.end(), [](const auto& slot) { return slot.used; }));
    }
};

SessionBootstrap bootstrap() {
    SessionBootstrap boot;
    boot.world = kWorld; boot.caller = {id(1), id(2)}; boot.lastIssuedId = 100;
    boot.inventory = {10000, 10000}; boot.workshopEnabled = true;
    BuildSnapshot build; build.id = id(3); build.owner = id(1);
    boot.builds.push_back(build);
    boot.jobs.push_back({id(5), 7, JobPhase::Available, {}});
    boot.cargoDefinitions.push_back({{id(1000), 1}, 500, .3, {50, 1}, CargoRecoveryRule::PreserveUnique});
    boot.cargo.push_back({id(6), {id(1000), 1}, id(1), {10, 2, -30}, {}, id(5)});
    return boot;
}
std::unique_ptr<GameSession> create(FakeAdapter& adapter, SessionBootstrap boot = bootstrap()) {
    SessionIssue issue;
    auto session = GameSession::create(boot, fixtureIncarnation(), catalog(), adapter, issue);
    if (!session) throw std::runtime_error("invalid session fixture: " + std::to_string(static_cast<int>(issue.error)));
    return session;
}
Command add(uint64_t sequence = 1, uint64_t sessionRevision = 0, uint64_t buildRevision = 0,
            GridPosition position = {137, 29, -211}) {
    return {AuthorityEpoch{1}, RequestSequence{sequence}, SessionRevision{sessionRevision},
        AddPart{{id(3), TopologyRevision{buildRevision}}, starterPartKey(StarterPart::Plate), {position, CubeRotation{4}}}};
}
Receipt commit(GameSession& session, FakeAdapter& adapter, Command command) {
    const auto submitted = session.submit(bootstrap().caller, command);
    EXPECT_EQ(submitted.state, ReceiptState::PendingPreparation) << static_cast<int>(submitted.issue.error);
    EXPECT_TRUE(session.advanceOneTick());
    const auto result = session.receipt(bootstrap().caller, command.sequence);
    EXPECT_EQ(result.state, ReceiptState::Committed) << static_cast<int>(result.issue.error);
    adapter.completeRetirement();
    return result;
}
std::vector<std::byte> encode(const BuildSnapshot& build) {
    std::vector<std::byte> result;
    EXPECT_FALSE(encodeBuild(build, catalog(), result));
    return result;
}
void expectWorldEqual(const SessionSnapshot& a, const SessionSnapshot& b) {
    // The trusted fake clock and receipt/allocator bookkeeping are separate.
    EXPECT_EQ(a.revision, b.revision);
    EXPECT_EQ(a.inventory, b.inventory);EXPECT_EQ(a.storedParts,b.storedParts);
    EXPECT_EQ(a.cargoDefinitions, b.cargoDefinitions);
    EXPECT_EQ(a.cargo, b.cargo);
    EXPECT_EQ(a.jobs, b.jobs);
    ASSERT_EQ(a.builds.size(), b.builds.size());
    for (size_t i = 0; i < a.builds.size(); ++i) EXPECT_EQ(encode(a.builds[i]), encode(b.builds[i]));
}
PartInstance seededPart(uint64_t counter, GridPosition position = {}) {
    PartInstance result;
    result.id = id(counter); result.owningBuild = id(3);
    result.definition = starterPartKey(StarterPart::Beam);
    result.placement.translation = position;
    result.settings = defaultModuleSettings(*catalog().lookup(result.definition).definition);
    return result;
}

TEST(GameSession, BuysMovesAndDismantlesUsingCatalogEconomyAndStableIdentity) {
    FakeAdapter adapter; auto session = create(adapter);
    const auto initial = session->snapshot();
    const auto bought = commit(*session, adapter, add());
    ASSERT_TRUE(bought.object); EXPECT_EQ(*bought.object, id(101));
    auto after = session->snapshot();
    const auto definition = catalog().lookup(starterPartKey(StarterPart::Plate)).definition;
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(after.inventory.salvageMaterial, initial.inventory.salvageMaterial - definition->cost.salvageMaterial);
    EXPECT_EQ(after.inventory.specialMachinery, initial.inventory.specialMachinery - definition->cost.specialMachinery);
    const auto original = after.builds[0].parts[0];
    EXPECT_EQ(original.provenance.origin, PartOrigin::Paid);
    EXPECT_EQ(original.placement.rotation.value, 4);
    const GridTransform placement{{-333, 117, 509}, CubeRotation{18}};
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1},
        MovePart{{id(3), TopologyRevision{1}}, *bought.object, placement}});
    after = session->snapshot();
    auto moved = after.builds[0].parts[0];
    EXPECT_EQ(moved.placement, placement);
    moved.placement = original.placement; EXPECT_EQ(moved, original);
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{3}, SessionRevision{2},
        RemovePart{{id(3), TopologyRevision{2}}, *bought.object}});
    after = session->snapshot();
    EXPECT_TRUE(after.builds[0].parts.empty());
    EXPECT_EQ(after.builds[0].revision, TopologyRevision{3});
    EXPECT_EQ(after.revision, SessionRevision{3});
    EXPECT_EQ(after.inventory.salvageMaterial,
        initial.inventory.salvageMaterial - definition->cost.salvageMaterial + definition->salvageYield.salvageMaterial);
    EXPECT_EQ(after.cargo, initial.cargo); EXPECT_EQ(after.jobs, initial.jobs);
}

TEST(GameSession, EmptyBuildCreationIsOwnedAndCostsNoParts) {
    FakeAdapter adapter; auto session = create(adapter);
    const auto before = session->snapshot();
    const auto result = commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, SessionRevision{}, CreateBuild{}});
    ASSERT_TRUE(result.object); EXPECT_EQ(*result.object, id(101));
    const auto after = session->snapshot();
    ASSERT_EQ(after.builds.size(), 2u); EXPECT_EQ(after.builds[1].owner, id(1));
    EXPECT_EQ(after.builds[1].revision, TopologyRevision{}); EXPECT_TRUE(after.builds[1].parts.empty());
    EXPECT_EQ(after.inventory, before.inventory);
}

TEST(GameSession, DuplicatePendingCommittedAndConflictingRequestsDoNotSpendAgain) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    const auto before = session->snapshot(); const auto command = add();
    EXPECT_EQ(session->submit(bootstrap().caller, command).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->submit(bootstrap().caller, command).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(adapter.begins, 1u); expectWorldEqual(before, session->snapshot());
    auto conflict = command; std::get<AddPart>(conflict.intent).placement.translation.x += 1;
    EXPECT_EQ(session->submit(bootstrap().caller, conflict).issue.error, SessionError::RequestConflict);
    EXPECT_EQ(session->submit(bootstrap().caller, add(2)).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->submit(bootstrap().caller, add(3)).issue.error, SessionError::Busy);
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    const auto accepted = session->snapshot();
    EXPECT_EQ(session->submit(bootstrap().caller, command).state, ReceiptState::Committed);
    EXPECT_EQ(session->submit(bootstrap().caller, conflict).issue.error, SessionError::RequestConflict);
    expectWorldEqual(accepted, session->snapshot()); EXPECT_EQ(adapter.activations, 1u);
}

TEST(GameSession, CancelBurnsIdsAndStaleCompletionCannotPublishNextPreparation) {
    FakeAdapter adapter; adapter.ready = false; adapter.deferDiscard = true; auto session = create(adapter);
    const auto before = session->snapshot();
    EXPECT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->lastIssuedId(), 101u);
    EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{1}).issue.error, SessionError::Canceled);
    EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{1}).issue.error, SessionError::Canceled);
    EXPECT_EQ(adapter.discards, 1u); EXPECT_EQ(adapter.resident(), 1u);
    expectWorldEqual(before, session->snapshot());
    EXPECT_EQ(session->submit(bootstrap().caller, add(2)).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->lastIssuedId(), 102u);
    adapter.stalePoll = true; adapter.ready = true;
    ASSERT_TRUE(session->advanceOneTick()); expectWorldEqual(before, session->snapshot());
    adapter.stalePoll = false; ASSERT_TRUE(session->advanceOneTick());
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{2}).object, id(102));
    EXPECT_EQ(adapter.resident(), 2u); adapter.completeRetirement(); EXPECT_EQ(adapter.resident(), 1u);
    EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{2}).state, ReceiptState::Committed);
}

TEST(GameSession, ReceiptEvictionKeepsContiguousHighWaterAndNeverReexecutes) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.receipts = 2; auto session = create(adapter, boot);
    for (uint64_t sequence = 1; sequence <= 6; ++sequence) {
        auto command = add(sequence); command.expectedRevision = SessionRevision{99};
        EXPECT_EQ(session->submit(boot.caller, command).issue.error, SessionError::StaleRevision);
    }
    EXPECT_EQ(session->retainedReceiptCount(), 2u); EXPECT_EQ(session->processedSequence(), 6u);
    EXPECT_EQ(session->submit(boot.caller, add()).issue.error, SessionError::AlreadyProcessed);
    EXPECT_EQ(session->submit(boot.caller, add(8)).issue.error, SessionError::SequenceGap);
    EXPECT_EQ(session->processedSequence(), 6u); EXPECT_EQ(adapter.begins, 0u);
    EXPECT_EQ(session->submit(boot.caller, add(7)).state, ReceiptState::PendingPreparation);
}

TEST(GameSession, CallerTokenEpochAndSequenceAreValidatedBeforeAdmission) {
    FakeAdapter adapter; auto session = create(adapter); const auto command = add();
    EXPECT_EQ(session->submit({id(77), id(2)}, command).issue.error, SessionError::WrongCaller);
    EXPECT_EQ(session->submit({id(1), id(77)}, command).issue.error, SessionError::WrongToken);
    auto wrong = command; wrong.epoch = AuthorityEpoch{2};
    EXPECT_EQ(session->submit(bootstrap().caller, wrong).issue.error, SessionError::WrongEpoch);
    wrong = command; wrong.sequence = RequestSequence{};
    EXPECT_EQ(session->submit(bootstrap().caller, wrong).issue.error, SessionError::InvalidIdentity);
    EXPECT_EQ(session->submit(bootstrap().caller, add(2)).expectedSequence, RequestSequence{1});
    EXPECT_EQ(session->processedSequence(), 0u); EXPECT_EQ(session->lastIssuedId(), 100u);
    EXPECT_EQ(session->submit(bootstrap().caller, command).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->cancel({id(77), id(2)}, RequestSequence{1}).issue.error, SessionError::WrongCaller);
    EXPECT_TRUE(session->hasPending());
}

TEST(GameSession, InsufficientFundsInvalidGeometryAndCapacityPreserveWorld) {
    for (int failure = 0; failure < 4; ++failure) {
        FakeAdapter adapter; auto boot = bootstrap(); auto command = add();
        if (failure == 0) boot.inventory = {};
        if (failure == 1) std::get<AddPart>(command.intent).placement.rotation = CubeRotation{24};
        if (failure == 2) adapter.partCapacity = 0;
        if (failure == 3) {
            boot.builds[0].parts.push_back(seededPart(10));
            std::get<AddPart>(command.intent).placement = {};
            std::get<AddPart>(command.intent).definition = starterPartKey(StarterPart::Beam);
        }
        auto session = create(adapter, boot); const auto before = session->snapshot();
        const auto result = session->submit(boot.caller, command);
        EXPECT_EQ(result.state, ReceiptState::Rejected);
        EXPECT_NE(result.issue.error, SessionError::None);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.activations, 0u);
        EXPECT_FALSE(session->hasPending()); EXPECT_EQ(adapter.resident(), 0u);
    }
}

TEST(GameSession, DelayedAndFinalReadinessFailuresDiscardExactlyOnce) {
    for (int failure = 0; failure < 3; ++failure) {
        FakeAdapter adapter; adapter.ready = failure != 0;
        adapter.rejectPoll = failure == 0; adapter.allowActivation = failure != 1;
        adapter.throwAfterReserve = failure == 2;
        auto session = create(adapter); const auto before = session->snapshot();
        const auto result = session->submit(bootstrap().caller, add());
        if (failure == 2) EXPECT_EQ(result.issue.error, SessionError::Capacity);
        else ASSERT_TRUE(session->advanceOneTick());
        EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).state, ReceiptState::Rejected);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.discards, 1u);
        EXPECT_EQ(adapter.activations, 0u); EXPECT_EQ(adapter.resident(), 0u);
    }
}

TEST(GameSession, LeaseOwnershipEpochAndExclusiveExpiryAreRecheckedAtBoundary) {
    for (int failure = 0; failure < 3; ++failure) {
        FakeAdapter adapter; auto boot = bootstrap();
        boot.builds[0].editLease = EditLease{id(1), AuthorityEpoch{1}, SimulationTick{10}};
        if (failure == 0) boot.builds[0].editLease->epoch = AuthorityEpoch{2};
        if (failure == 1) {
            boot.tick = SimulationTick{1};
            boot.builds[0].editLease->expiresAfter = SimulationTick{1};
        }
        if (failure == 2) boot.builds[0].editLease->expiresAfter = SimulationTick{1};
        auto session = create(adapter, boot); const auto before = session->snapshot();
        const auto result = session->submit(boot.caller, add());
        if (failure == 2) { EXPECT_EQ(result.state, ReceiptState::PendingPreparation); ASSERT_TRUE(session->advanceOneTick()); }
        EXPECT_EQ(session->receipt(boot.caller, RequestSequence{1}).issue.error, SessionError::LeaseDenied);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.activations, 0u);
    }
}

TEST(GameSession, FutureExecutionRejectsLeaseExpiryBeforeStagingPhysicalWork) {
    FakeAdapter adapter; auto boot=bootstrap();
    boot.builds[0].editLease=EditLease{boot.caller.participant,AuthorityEpoch{1},SimulationTick{7}};
    auto session=create(adapter,boot);const auto before=session->snapshot();
    ASSERT_TRUE(session->bindExecution(71,SimulationTick{}));
    ASSERT_EQ(session->submit(boot.caller,add()).state,ReceiptState::PendingPreparation);
    EXPECT_FALSE(session->stageExecution(71,SimulationTick{7}));
    EXPECT_EQ(session->receipt(boot.caller,RequestSequence{1}).issue.error,SessionError::LeaseDenied);
    EXPECT_FALSE(session->executionInFlight()); EXPECT_EQ(adapter.activations,0u);
    expectWorldEqual(before,session->snapshot());
}

TEST(GameSession, StaleBuildOrSessionRevisionsRejectBeforeIssuingIds) {
    for (int failure = 0; failure < 2; ++failure) {
        FakeAdapter adapter; auto boot = bootstrap(); auto command = add();
        if (failure == 0) std::get<AddPart>(command.intent).target.expectedRevision = TopologyRevision{1};
        if (failure == 1) command.expectedRevision = SessionRevision{1};
        auto session = create(adapter, boot); const auto before = session->snapshot();
        const auto result = session->submit(boot.caller, command);
        EXPECT_EQ(result.issue.error, SessionError::StaleRevision);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->lastIssuedId(), 100u);
    }
}

TEST(GameSession, LoanDismantlingPaysZeroAndConnectedRemovalIsExplicitlyRejected) {
    FakeAdapter adapter; auto boot = bootstrap();
    auto loan = seededPart(10); loan.provenance = {PartOrigin::StarterLoan, id(20)};
    boot.starterEntitlements = {id(20)}; boot.builds[0].parts = {loan};
    auto session = create(adapter, boot); const auto before = session->snapshot();
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, SessionRevision{},
        RemovePart{{id(3), TopologyRevision{}}, id(10)}});
    EXPECT_EQ(session->snapshot().inventory, before.inventory);
    boot.builds[0].parts.push_back(seededPart(11, {0, kBrickBodyTicks, 0}));
    Connection weld; weld.id = id(30); weld.a = {id(10), SocketId{1}}; weld.b = {id(11), SocketId{2}};
    weld.strength = {1000, 1000, 1000, 1000}; boot.builds[0].connections = {weld};
    FakeAdapter secondAdapter; auto connected = create(secondAdapter, boot);
    const auto connectedBefore = connected->snapshot();
    const auto result = connected->submit(boot.caller, {AuthorityEpoch{1}, RequestSequence{1}, SessionRevision{},
        RemovePart{{id(3), TopologyRevision{}}, id(10)}});
    EXPECT_EQ(result.issue.error, SessionError::ConnectedPart);
    expectWorldEqual(connectedBefore, connected->snapshot());
}

TEST(GameSession, TrustedJobAcceptanceDoesNotRewardOrChangeCargo) {
    FakeAdapter adapter; auto session = create(adapter); const auto before = session->snapshot();
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, SessionRevision{}, AcceptJob{id(5)}});
    auto after = session->snapshot(); EXPECT_EQ(after.jobs[0].phase, JobPhase::Accepted);
    EXPECT_EQ(after.jobs[0].acceptedBy, id(1)); EXPECT_EQ(after.inventory, before.inventory);
    EXPECT_EQ(after.cargo, before.cargo); EXPECT_EQ(encode(after.builds[0]), encode(before.builds[0]));
    const auto again = session->submit(bootstrap().caller,
        {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1}, AcceptJob{id(5)}});
    EXPECT_EQ(again.issue.error, SessionError::JobUnavailable); expectWorldEqual(after, session->snapshot());
    after.jobs.clear(); after.cargo.clear(); after.inventory = {};
    EXPECT_EQ(session->snapshot().jobs.size(), 1u); EXPECT_EQ(session->snapshot().cargo.size(), 1u);
    EXPECT_EQ(session->snapshot().inventory, before.inventory);
}

TEST(GameSession, PreviewBootstrapDoesNotGrantWorkshopInventoryOrJobs) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.workshopEnabled = false; boot.inventory = {};
    boot.builds.clear(); boot.jobs.clear(); boot.cargo.clear(); boot.cargoDefinitions.clear();
    auto session = create(adapter, boot); const auto before = session->snapshot();
    EXPECT_EQ(session->submit(boot.caller, add()).issue.error, SessionError::UnsupportedFeature);
    EXPECT_EQ(session->submit(boot.caller,
        {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{}, AcceptJob{id(5)}}).issue.error, SessionError::UnknownJob);
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.begins, 0u);
}

TEST(GameSession, IdRevisionTickAndCreditOverflowNeverWrap) {
    for (int failure = 0; failure < 5; ++failure) {
        FakeAdapter adapter; auto boot = bootstrap(); auto command = add();
        constexpr auto maximum = std::numeric_limits<uint64_t>::max();
        if (failure == 0) boot.lastIssuedId = maximum;
        if (failure == 1) { boot.revision = SessionRevision{maximum}; command.expectedRevision = boot.revision; }
        if (failure == 2) {
            boot.builds[0].revision = TopologyRevision{maximum};
            std::get<AddPart>(command.intent).target.expectedRevision = boot.builds[0].revision;
        }
        if (failure == 3) boot.tick = SimulationTick{maximum};
        if (failure == 4) {
            boot.inventory = {maximum, maximum}; boot.builds[0].parts = {seededPart(10)};
            command.intent = RemovePart{{id(3), TopologyRevision{}}, id(10)};
        }
        auto session = create(adapter, boot); const auto before = session->snapshot();
        if (failure == 3) EXPECT_FALSE(session->advanceOneTick());
        else EXPECT_EQ(session->submit(boot.caller, command).state, ReceiptState::Rejected);
        expectWorldEqual(before, session->snapshot());
    }
}

TEST(GameSession, BootstrapRejectsDuplicateCrossWorldInvalidContentAndUnbackedLoan) {
    for (int failure = 0; failure < 10; ++failure) {
        FakeAdapter adapter; auto boot = bootstrap();
        if (failure == 0) boot.cargo[0].id = boot.jobs[0].id;
        if (failure == 1) boot.cargo[0].owner.world.bytes[0] = 'x';
        if (failure == 2) boot.lastIssuedId = 4;
        if (failure == 3) boot.cargo[0].definition.version = 2;
        if (failure == 4) boot.cargoDefinitions[0].massKg = std::numeric_limits<double>::quiet_NaN();
        if (failure == 5) boot.cargo[0].job = id(99);
        if (failure == 6) {
            auto part = seededPart(10); part.provenance = {PartOrigin::StarterLoan, id(20)};
            boot.builds[0].parts = {part};
        }
        if (failure == 7) boot.caller.sessionToken = boot.caller.participant;
        if (failure == 8) boot.limits.receipts = 0;
        if (failure == 9) boot.cargo[0].orientation.w = 2;
        SessionIssue issue; const auto session = GameSession::create(boot, fixtureIncarnation(), catalog(), adapter, issue);
        EXPECT_FALSE(session) << failure; EXPECT_TRUE(issue) << failure; EXPECT_EQ(adapter.begins, 0u);
    }
}

TEST(GameSession, TotalPartAndBuildLimitsAreCheckedBeforeIssuingIds) {
    for (bool buildLimit : {false, true}) {
        FakeAdapter adapter; auto boot = bootstrap(); boot.limits.builds = 1; boot.limits.totalParts = 1;
        boot.builds[0].parts = {seededPart(10)}; auto session = create(adapter, boot);
        const auto before = session->snapshot(); auto command = add();
        if (buildLimit) command.intent = CreateBuild{};
        EXPECT_EQ(session->submit(boot.caller, command).issue.error, SessionError::Capacity);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->lastIssuedId(), 100u);
    }
}

TEST(GameSession, PollingCannotAdvanceClockOrCommitAndDestructorCancelsPending) {
    FakeAdapter adapter;
    {
        auto session = create(adapter); const auto before = session->snapshot();
        EXPECT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
        for (int i = 0; i < 100; ++i) session->pollPreparation();
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->snapshot().tick, SimulationTick{});
        EXPECT_EQ(adapter.activations, 0u);
    }
    EXPECT_EQ(adapter.discards, 1u); EXPECT_EQ(adapter.resident(), 0u);
}

TEST(GameSession, RetiringReservationsRemainBoundedAndBlockNewPreparations) {
    FakeAdapter adapter; adapter.ready = false; adapter.deferDiscard = true; auto session = create(adapter);
    const auto before = session->snapshot();
    for (uint64_t sequence = 1; sequence <= 4; ++sequence) {
        EXPECT_EQ(session->submit(bootstrap().caller, add(sequence)).state, ReceiptState::PendingPreparation);
        EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{sequence}).issue.error, SessionError::Canceled);
    }
    EXPECT_EQ(adapter.resident(), 4u);
    EXPECT_EQ(session->submit(bootstrap().caller, add(5)).issue.error, SessionError::AdapterRejected);
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.resident(), 4u);
    adapter.completeRetirement(); adapter.ready = true;
    const auto accepted = commit(*session, adapter, add(6));
    EXPECT_EQ(accepted.object, id(106)); EXPECT_EQ(adapter.resident(), 1u);
}

TEST(GameSession, CloseAdmissionCancelsOnceAndCannotBeBypassedByRetryOrBusyQueue) {
    FakeAdapter adapter; adapter.ready = false;
    {
        auto session = create(adapter); const auto before = session->snapshot();
        ASSERT_TRUE(session->admissionOpen());
        EXPECT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
        session->closeAdmission(); session->closeAdmission();
        EXPECT_FALSE(session->admissionOpen()); EXPECT_FALSE(session->hasPending());
        EXPECT_EQ(adapter.discards, 1u);
        EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).issue.error, SessionError::Canceled);
        EXPECT_EQ(session->submit(bootstrap().caller, add()).issue.error, SessionError::Closed);
        EXPECT_EQ(session->submit(bootstrap().caller, add(2)).issue.error, SessionError::Closed);
        EXPECT_EQ(session->processedSequence(), 1u); EXPECT_FALSE(session->advanceOneTick());
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.activations, 0u);
    }
    EXPECT_EQ(adapter.discards, 1u);
    auto boot = bootstrap(); boot.caller.sessionToken = id(99);
    auto replacement = create(adapter, boot);
    EXPECT_EQ(replacement->submit(bootstrap().caller, add()).issue.error, SessionError::WrongToken);
}

TEST(GameSession, UnexpectedAdapterExceptionReleasesReservationAndNeverPublishes) {
    FakeAdapter adapter; adapter.unexpectedThrow = true; auto session = create(adapter);
    const auto before = session->snapshot();
    EXPECT_THROW(static_cast<void>(session->submit(bootstrap().caller, add())), std::runtime_error);
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(adapter.discards, 1u);
    EXPECT_EQ(adapter.resident(), 0u); EXPECT_FALSE(session->hasPending());
    EXPECT_EQ(session->processedSequence(), 0u); EXPECT_EQ(session->lastIssuedId(), 101u);
    adapter.unexpectedThrow = false;
    EXPECT_EQ(commit(*session, adapter, add()).object, id(102));
}

TEST(GameSession, BootstrapParticipantReferencesRejectRoleAliasesAndUnknownIds) {
    for (const auto badParticipant : {id(2), id(77)}) {
        for (int field = 0; field < 4; ++field) {
            FakeAdapter adapter; auto boot = bootstrap();
            if (field == 0) boot.builds[0].owner = badParticipant;
            if (field == 1) boot.builds[0].editLease = EditLease{badParticipant, AuthorityEpoch{1}, SimulationTick{10}};
            if (field == 2) boot.cargo[0].owner = badParticipant;
            if (field == 3) { boot.jobs[0].phase = JobPhase::Accepted; boot.jobs[0].acceptedBy = badParticipant; }
            SessionIssue issue;
            EXPECT_FALSE(GameSession::create(boot, fixtureIncarnation(), catalog(), adapter, issue)) << field << '/' << badParticipant.counter;
            EXPECT_TRUE(issue); EXPECT_EQ(adapter.begins, 0u);
        }
    }
    FakeAdapter adapter; auto boot = bootstrap();
    boot.builds[0].editLease = EditLease{boot.caller.participant, AuthorityEpoch{1}, SimulationTick{10}};
    boot.jobs[0].phase = JobPhase::Accepted; boot.jobs[0].acceptedBy = boot.caller.participant;
    EXPECT_NE(create(adapter, boot), nullptr);
}
TEST(GameSessionTransactions, TwoPreparedCandidatesCannotOverwriteNewerAcceptedWorld) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    const auto original = session->snapshot();
    ASSERT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    auto second = add(2); std::get<AddPart>(second.intent).placement.translation.x += 500;
    ASSERT_EQ(session->submit(bootstrap().caller, second).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->admissionState().admittedThrough, RequestFrontier{2});
    EXPECT_EQ(session->admissionState().processedThrough, RequestFrontier{0});
    expectWorldEqual(original, session->snapshot());
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).state, ReceiptState::Committed);
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{2}).issue.error, SessionError::StaleRevision);
    const auto accepted = session->snapshot();
    ASSERT_EQ(accepted.builds[0].parts.size(), 1);
    EXPECT_EQ(accepted.builds[0].parts[0].id, id(101));
    EXPECT_EQ(accepted.builds[0].parts[0].placement, std::get<AddPart>(add().intent).placement);
    EXPECT_EQ(accepted.revision, SessionRevision{1});
    EXPECT_EQ(session->processedSequence(), 2);
    EXPECT_EQ(session->lastIssuedId(), 102);
    EXPECT_EQ(adapter.activations, 1); EXPECT_EQ(adapter.discards, 1);
    EXPECT_FALSE(session->journalFaulted());
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).durability, ReceiptDurability::Volatile);
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).journal, JournalSequence{4});
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{2}).journal, JournalSequence{5});
    std::array<JournalRecord, 64> journal{};
    const auto count = session->journal().copyPrefix(journal);
    ASSERT_EQ(count, 5); // Lease, admission 1, admission 2, decision 1, decision 2.
    for (size_t i = 0; i < count; ++i) EXPECT_EQ(journal[i].sequence.value(), i + 1);
    EXPECT_EQ(std::get<RequestDecisionRecord>(journal[3].payload).outcome.state, ReceiptState::Committed);
    EXPECT_EQ(std::get<RequestDecisionRecord>(journal[4].payload).outcome.error, SessionError::StaleRevision);
}
TEST(GameSessionTransactions, CanceledFrontAllowsSecondCandidateAfterFreshChecks) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    const auto before = session->snapshot();
    ASSERT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(bootstrap().caller, add(2)).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{1}).issue.error, SessionError::Canceled);
    EXPECT_EQ(session->processedSequence(), 1);
    EXPECT_EQ(session->pendingCount(), 1);
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{2}).object, id(102));
    EXPECT_EQ(session->snapshot().revision, SessionRevision{1});
    const auto cost = catalog().lookup(starterPartKey(StarterPart::Plate)).definition->cost;
    EXPECT_EQ(session->snapshot().inventory.salvageMaterial, before.inventory.salvageMaterial - cost.salvageMaterial);
    EXPECT_EQ(adapter.discards, 1); EXPECT_EQ(adapter.activations, 1);
}
TEST(GameSessionTransactions, LaterCancellationKeepsItsOrderedTombstoneAndDecisionCapacity) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    ASSERT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(bootstrap().caller, add(2)).state, ReceiptState::PendingPreparation);
    const auto reservedBefore = session->journal().reservedRecords();
    EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{2}).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->journal().reservedRecords(), reservedBefore);
    EXPECT_EQ(session->pendingCount(), 2);
    EXPECT_EQ(session->processedSequence(), 0);
    auto altered = add(2); std::get<AddPart>(altered.intent).placement.translation.x += 1;
    EXPECT_EQ(session->submit(bootstrap().caller, altered).issue.error, SessionError::RequestConflict);
    EXPECT_EQ(session->submit(bootstrap().caller, add(3)).issue.error, SessionError::Busy);
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).state, ReceiptState::Committed);
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{2}).issue.error, SessionError::Canceled);
    EXPECT_EQ(session->processedSequence(), 2);
    EXPECT_EQ(adapter.activations, 1); EXPECT_EQ(adapter.discards, 1);
    EXPECT_EQ(session->journal().reservedRecords(), 1); // Trusted closure only.
}
TEST(GameSessionTransactions, PendingDebitsAndCreditsNeverBecomeSpendableTwice) {
    const auto cost = catalog().lookup(starterPartKey(StarterPart::Plate)).definition->cost;
    FakeAdapter adapter; adapter.ready = false; auto boot = bootstrap(); boot.inventory = cost;
    auto session = create(adapter, boot);
    ASSERT_EQ(session->submit(boot.caller, add()).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(boot.caller, add(2)).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->lastIssuedId(), 101); // Rejected second debit cannot mint an ID.
    EXPECT_EQ(adapter.begins, 1);
    EXPECT_EQ(session->cancel(boot.caller, RequestSequence{1}).issue.error, SessionError::Canceled);
    EXPECT_EQ(session->receipt(boot.caller, RequestSequence{2}).issue.error, SessionError::InsufficientResources);
    EXPECT_EQ(session->snapshot().inventory, cost);
    FakeAdapter credits; credits.ready = false; boot = bootstrap(); boot.inventory = {};
    boot.builds[0].parts = {seededPart(10)};
    auto creditSession = create(credits, boot);
    Command remove{AuthorityEpoch{1}, RequestSequence{1}, {}, RemovePart{{id(3), {}}, id(10)}};
    ASSERT_EQ(creditSession->submit(boot.caller, remove).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(creditSession->submit(boot.caller, add(2)).state, ReceiptState::PendingPreparation);
    credits.ready = true; ASSERT_TRUE(creditSession->advanceOneTick());
    EXPECT_EQ(creditSession->receipt(boot.caller, RequestSequence{2}).issue.error, SessionError::InsufficientResources);
    EXPECT_TRUE(creditSession->snapshot().builds[0].parts.empty());
    EXPECT_EQ(credits.begins, 1);
}
TEST(GameSessionTransactions, FullJournalCannotBlockReservedClosureOrSpendBeforeAdmission) {
    FakeAdapter adapter; adapter.ready = false; auto boot = bootstrap(); boot.limits.journalRecords = 4;
    auto session = create(adapter, boot); const auto before = session->snapshot();
    ASSERT_EQ(session->submit(boot.caller, add()).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->submit(boot.caller, add(2)).issue.error, SessionError::JournalCapacity);
    EXPECT_EQ(session->admissionState().admittedThrough, RequestFrontier{1});
    EXPECT_EQ(session->lastIssuedId(), 101);
    session->closeAdmission();
    EXPECT_EQ(session->processedSequence(), 1); EXPECT_FALSE(session->hasPending());
    EXPECT_FALSE(session->journalFaulted());
    EXPECT_EQ(session->journal().size(), 4);
    EXPECT_EQ(session->journal().reservedRecords(), 0);
    EXPECT_EQ(adapter.discards, 1); expectWorldEqual(before, session->snapshot());
    std::array<JournalRecord, 4> copied{};
    ASSERT_EQ(session->journal().copyPrefix(copied), 4);
    EXPECT_TRUE(std::holds_alternative<AdmissionClosedRecord>(copied.back().payload));
}
TEST(GameSessionTransactions, ThrowingSecondPreparationReleasesOnlyItsOwnReservations) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    ASSERT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    adapter.unexpectedThrow = true;
    EXPECT_THROW(static_cast<void>(session->submit(bootstrap().caller, add(2))), std::runtime_error);
    EXPECT_EQ(session->pendingCount(), 1);
    EXPECT_EQ(session->admissionState().admittedThrough, RequestFrontier{1});
    EXPECT_EQ(session->admissionState().processedThrough, RequestFrontier{0});
    EXPECT_EQ(adapter.discards, 1); EXPECT_EQ(session->lastIssuedId(), 102);
    adapter.unexpectedThrow = false; adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    EXPECT_EQ(session->receipt(bootstrap().caller, RequestSequence{1}).object, id(101));
    EXPECT_EQ(adapter.activations, 1); EXPECT_FALSE(session->journalFaulted());
}
TEST(GameSessionTransactions, CandidateAndAggregateActiveLimitsRejectBeforeNewIdentity) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.candidateBytes = 1;
    auto bounded = create(adapter, boot);
    EXPECT_EQ(bounded->submit(boot.caller, add()).issue.error, SessionError::CandidateCapacity);
    EXPECT_EQ(bounded->lastIssuedId(), 100); EXPECT_EQ(adapter.begins, 0);
    FakeAdapter parts; parts.ready = false; boot = bootstrap(); boot.limits.totalParts = 1;
    auto session = create(parts, boot);
    ASSERT_EQ(session->submit(boot.caller, add()).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(boot.caller, add(2)).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->lastIssuedId(), 101);
    EXPECT_EQ(parts.begins, 1);
    session->closeAdmission();
    EXPECT_EQ(session->processedSequence(), 2);
    EXPECT_FALSE(session->journalFaulted());
}

Command inverse(const GameSession& session, bool undo = true) {
    const auto history = session.history();
    const auto choice = undo ? history.undo : history.redo;
    if (!choice) throw std::runtime_error("missing history fixture choice");
    Command command{AuthorityEpoch{1}, RequestSequence{session.admissionState().admittedThrough.value() + 1},
        session.snapshot().revision, {}};
    if (undo) command.intent = Undo{choice->target, choice->entry, history.generation};
    else command.intent = Redo{choice->target, choice->entry, history.generation};
    return command;
}
Command removeCurrent(const GameSession& session, DurableId part) {
    const auto snapshot = session.snapshot();
    return {AuthorityEpoch{1}, RequestSequence{session.admissionState().admittedThrough.value() + 1}, snapshot.revision,
        RemovePart{{snapshot.builds[0].id, snapshot.builds[0].revision}, part}};
}
TEST(GameSessionHistory, PurchaseCyclesReverseExactCostAndKeepOneIdentity) {
    FakeAdapter adapter; auto session = create(adapter);
    const auto initial = session->snapshot().inventory;
    commit(*session, adapter, add());
    const auto paid = session->snapshot();
    const auto part = paid.builds[0].parts[0];
    for (int cycle = 0; cycle < 5; ++cycle) {
        const auto undo = inverse(*session);
        commit(*session, adapter, undo);
        EXPECT_EQ(session->snapshot().inventory, initial);
        EXPECT_TRUE(session->snapshot().builds[0].parts.empty());
        EXPECT_EQ(session->history().dormantParts, 1);
        EXPECT_EQ(session->submit(bootstrap().caller, undo).state, ReceiptState::Committed);
        commit(*session, adapter, inverse(*session, false));
        EXPECT_EQ(session->snapshot().inventory, paid.inventory);
        ASSERT_EQ(session->snapshot().builds[0].parts.size(), 1);
        EXPECT_EQ(session->snapshot().builds[0].parts[0], part);
        EXPECT_EQ(session->history().dormantParts, 0);
        EXPECT_EQ(session->lastIssuedId(), 101);
    }
    EXPECT_EQ(session->snapshot().revision, SessionRevision{11});
    EXPECT_EQ(session->snapshot().builds[0].revision, TopologyRevision{11});
    EXPECT_EQ(session->history().entries, 1);
    EXPECT_FALSE(session->journalFaulted());
}
TEST(GameSessionHistory, DismantleCyclesChargeYieldAndPreserveFullPartImage) {
    for (bool loan : {false, true}) {
        FakeAdapter adapter; auto boot = bootstrap();
        auto part = seededPart(10); part.definition = starterPartKey(StarterPart::Engine);
        part.settings = defaultModuleSettings(*catalog().lookup(part.definition).definition);
        part.health = 2317; part.paint = {17, 22, 90, 255};
        part.settings.enabled = false; part.settings.controlChannel = 7;
        if (loan) { part.provenance = {PartOrigin::StarterLoan, id(20)}; boot.starterEntitlements = {id(20)}; }
        boot.builds[0].parts = {part}; auto session = create(adapter, boot);
        commit(*session, adapter, removeCurrent(*session, part.id));
        const auto dismantled = session->snapshot().inventory;
        const auto yield = loan ? ResourceAmounts{} : catalog().lookup(part.definition).definition->salvageYield;
        EXPECT_EQ(dismantled.salvageMaterial, boot.inventory.salvageMaterial + yield.salvageMaterial);
        EXPECT_EQ(dismantled.specialMachinery, boot.inventory.specialMachinery + yield.specialMachinery);
        for (int cycle = 0; cycle < 3; ++cycle) {
            commit(*session, adapter, inverse(*session));
            EXPECT_EQ(session->snapshot().inventory, boot.inventory);
            ASSERT_EQ(session->snapshot().builds[0].parts.size(), 1);
            EXPECT_EQ(session->snapshot().builds[0].parts[0], part);
            commit(*session, adapter, inverse(*session, false));
            EXPECT_EQ(session->snapshot().inventory, dismantled);
            EXPECT_TRUE(session->snapshot().builds[0].parts.empty());
        }
        EXPECT_EQ(session->lastIssuedId(), boot.lastIssuedId);
        EXPECT_FALSE(session->journalFaulted());
    }
}
TEST(GameSessionHistory, MultipleUndoRedoPreservesReferencesAndUnrelatedJobs) {
    FakeAdapter adapter; auto session = create(adapter);
    commit(*session, adapter, add());
    const auto original = session->snapshot().builds[0].parts[0];
    const GridTransform moved{{-333, 117, 509}, CubeRotation{18}};
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1},
        MovePart{{id(3), TopologyRevision{1}}, original.id, moved}});
    commit(*session, adapter, removeCurrent(*session, original.id));
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{4}, SessionRevision{3}, AcceptJob{id(5)}});
    const auto removed = session->snapshot();
    const auto historyBeforeJob = session->history();
    EXPECT_EQ(historyBeforeJob.entries, 3);
    commit(*session, adapter, inverse(*session));
    EXPECT_EQ(session->snapshot().builds[0].parts[0].placement, moved);
    commit(*session, adapter, inverse(*session));
    EXPECT_EQ(session->snapshot().builds[0].parts[0], original);
    commit(*session, adapter, inverse(*session));
    EXPECT_TRUE(session->snapshot().builds[0].parts.empty());
    EXPECT_EQ(session->snapshot().inventory, bootstrap().inventory);
    for (int i = 0; i < 3; ++i) commit(*session, adapter, inverse(*session, false));
    const auto after = session->snapshot();
    EXPECT_TRUE(after.builds[0].parts.empty()); EXPECT_EQ(after.inventory, removed.inventory);
    EXPECT_EQ(after.jobs, removed.jobs); EXPECT_EQ(after.cargo, removed.cargo);
    EXPECT_EQ(after.builds[0].revision, TopologyRevision{9});
    EXPECT_EQ(after.revision, SessionRevision{10}); EXPECT_EQ(session->lastIssuedId(), 101);
}
TEST(GameSessionHistory, EmptyBuildRestoresSameIdWithMonotonicDormantRevision) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.builds.clear(); auto session = create(adapter, boot);
    const auto created = commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, {}, CreateBuild{}});
    ASSERT_TRUE(created.object);
    auto buy = add(2, 1); std::get<AddPart>(buy.intent).target.build = *created.object;
    commit(*session, adapter, buy);
    const auto paid = session->snapshot();
    commit(*session, adapter, inverse(*session));
    commit(*session, adapter, inverse(*session));
    EXPECT_EQ(adapter.lastRemovedBuild, created.object);
    EXPECT_TRUE(session->snapshot().builds.empty());
    EXPECT_EQ(session->history().dormantParts, 1); EXPECT_EQ(session->history().dormantBuilds, 1);
    EXPECT_EQ(session->history().redo->target.expectedRevision, TopologyRevision{3});
    commit(*session, adapter, inverse(*session, false));
    EXPECT_FALSE(adapter.lastRemovedBuild);
    EXPECT_EQ(session->snapshot().builds[0].revision, TopologyRevision{4});
    commit(*session, adapter, inverse(*session, false));
    EXPECT_EQ(session->snapshot().builds[0].parts, paid.builds[0].parts);
    EXPECT_EQ(session->snapshot().inventory, paid.inventory);
    EXPECT_EQ(session->snapshot().builds[0].revision, TopologyRevision{5});
    EXPECT_EQ(session->lastIssuedId(), 102); EXPECT_FALSE(session->journalFaulted());
}
TEST(GameSessionHistory, FailedAndCanceledNewEditsPreserveRedoUntilCommit) {
    FakeAdapter adapter; auto session = create(adapter);
    commit(*session, adapter, add()); commit(*session, adapter, inverse(*session));
    const auto before = session->snapshot(); const auto history = session->history();
    adapter.ready = false;
    EXPECT_EQ(session->submit(bootstrap().caller, add(3, 2, 2)).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->history(), history);
    EXPECT_EQ(session->cancel(bootstrap().caller, RequestSequence{3}).issue.error, SessionError::Canceled);
    auto invalid = add(4, 2, 2); std::get<AddPart>(invalid.intent).placement.rotation = CubeRotation{24};
    EXPECT_EQ(session->submit(bootstrap().caller, invalid).issue.error, SessionError::InvalidBuild);
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
    adapter.ready = true; const auto newPart = commit(*session, adapter, add(5, 2, 2));
    EXPECT_FALSE(session->history().redo); EXPECT_EQ(session->history().dormantParts, 0);
    EXPECT_NE(newPart.object, id(101)); EXPECT_EQ(session->history().entries, 1);
    const auto current = session->history();
    auto forged = inverse(*session); std::get<Undo>(forged.intent).entry = history.redo->entry;
    EXPECT_EQ(session->submit(bootstrap().caller, forged).issue.error, SessionError::HistoryConflict);
    EXPECT_EQ(session->history(), current);
}
TEST(GameSessionHistory, SameInverseReservationCannotRestoreTwiceAndCancelReleasesIt) {
    FakeAdapter adapter; auto session = create(adapter);
    commit(*session, adapter, add()); commit(*session, adapter, inverse(*session));
    const auto before = session->snapshot(); const auto history = session->history();
    adapter.ready = false;
    const auto first = inverse(*session, false);
    ASSERT_EQ(session->submit(bootstrap().caller, first).state, ReceiptState::PendingPreparation);
    const auto second = inverse(*session, false);
    ASSERT_EQ(session->submit(bootstrap().caller, second).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->pendingCount(), 2);
    EXPECT_EQ(session->cancel(bootstrap().caller, first.sequence).issue.error, SessionError::Canceled);
    EXPECT_EQ(session->receipt(bootstrap().caller, second.sequence).issue.error, SessionError::HistoryBusy);
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
    adapter.ready = true; commit(*session, adapter, inverse(*session, false));
    EXPECT_EQ(session->snapshot().builds[0].parts[0].id, id(101));
    EXPECT_EQ(session->lastIssuedId(), 101);
}
TEST(GameSessionHistory, EvictionCannotStealAnEarlierInverseReservationOrViceVersa) {
    for (bool inverseFirst : {true, false}) {
        FakeAdapter adapter; auto boot = bootstrap(); boot.limits.historyEntries = 1;
        auto session = create(adapter, boot); commit(*session, adapter, add());
        const auto history = session->history(); adapter.ready = false;
        auto undo = inverse(*session); auto edit = add(2, 1, 1, {1000, 0, 0});
        if (inverseFirst) edit.sequence = RequestSequence{3}; else undo.sequence = RequestSequence{3};
        const auto first = inverseFirst ? undo : edit;
        const auto second = inverseFirst ? edit : undo;
        ASSERT_EQ(session->submit(boot.caller, first).state, ReceiptState::PendingPreparation);
        ASSERT_EQ(session->submit(boot.caller, second).state, ReceiptState::PendingPreparation);
        EXPECT_EQ(session->history(), history);
        static_cast<void>(session->cancel(boot.caller, first.sequence));
        EXPECT_EQ(session->receipt(boot.caller, second.sequence).issue.error, SessionError::HistoryBusy);
        EXPECT_EQ(session->history(), history); EXPECT_FALSE(session->journalFaulted());
    }
}
TEST(GameSessionHistory, StaleSecondInverseCannotEraseAnAcceptedJob) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    adapter.ready = false;
    ASSERT_EQ(session->submit(bootstrap().caller,
        {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1}, AcceptJob{id(5)}}).state, ReceiptState::PendingPreparation);
    const auto undo = inverse(*session);
    ASSERT_EQ(session->submit(bootstrap().caller, undo).state, ReceiptState::PendingPreparation);
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick()); adapter.completeRetirement();
    EXPECT_EQ(session->receipt(bootstrap().caller, undo.sequence).issue.error, SessionError::StaleRevision);
    EXPECT_EQ(session->snapshot().jobs[0].phase, JobPhase::Accepted);
    EXPECT_EQ(session->history().applied, 1); EXPECT_EQ(session->snapshot().builds[0].parts.size(), 1);
    commit(*session, adapter, inverse(*session));
    EXPECT_EQ(session->snapshot().jobs[0].phase, JobPhase::Accepted);
}
TEST(GameSessionHistory, HistoryEvictionKeepsDormantObjectReferencedByNewerEntry) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.historyEntries = 2;
    auto session = create(adapter, boot); commit(*session, adapter, add());
    const auto original = session->snapshot().builds[0].parts[0];
    commit(*session, adapter, removeCurrent(*session, original.id));
    commit(*session, adapter, add(3, 2, 2, {1000, 0, 0})); // Evicts original purchase only.
    EXPECT_EQ(session->history().entries, 2); EXPECT_EQ(session->history().dormantParts, 1);
    commit(*session, adapter, inverse(*session)); commit(*session, adapter, inverse(*session));
    ASSERT_EQ(session->snapshot().builds[0].parts.size(), 1);
    EXPECT_EQ(session->snapshot().builds[0].parts[0], original);
    EXPECT_EQ(session->history().dormantParts, 1); EXPECT_EQ(session->lastIssuedId(), 102);
}
TEST(GameSessionHistory, CountAndByteLimitsStageEvictionOrRejectWithoutChangingHistory) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.historyEntries = 1;
    auto session = create(adapter, boot); commit(*session, adapter, add());
    const auto oneEntryBytes = session->history().payloadBytes;
    EXPECT_GT(oneEntryBytes, 0);
    commit(*session, adapter, add(2, 1, 1, {1000, 0, 0}));
    EXPECT_EQ(session->history().entries, 1);
    EXPECT_EQ(session->history().payloadBytes, oneEntryBytes);
    FakeAdapter exact; boot = bootstrap(); boot.limits.historyBytes = oneEntryBytes;
    auto bounded = create(exact, boot); commit(*bounded, exact, add());
    const auto before = bounded->snapshot(); const auto history = bounded->history();
    EXPECT_EQ(bounded->submit(boot.caller, inverse(*bounded)).issue.error, SessionError::HistoryCapacity);
    expectWorldEqual(before, bounded->snapshot()); EXPECT_EQ(bounded->history(), history);
    // Recording a newer edit may evict to the exact payload bound.
    commit(*bounded, exact, add(3, 1, 1, {1000, 0, 0}));
    EXPECT_EQ(bounded->history().entries, 1); EXPECT_EQ(bounded->history().payloadBytes, oneEntryBytes);
    FakeAdapter tiny; boot.limits.historyBytes = oneEntryBytes - 1;
    auto rejected = create(tiny, boot);
    EXPECT_EQ(rejected->submit(boot.caller, add()).issue.error, SessionError::HistoryCapacity);
    EXPECT_TRUE(rejected->snapshot().builds[0].parts.empty()); EXPECT_EQ(rejected->history().entries, 0);
    EXPECT_EQ(tiny.begins, 0);
}
TEST(GameSessionHistory, DormantLimitsAndActivationFailuresKeepCompensationAvailable) {
    for (int failure = 0; failure < 5; ++failure) {
        FakeAdapter adapter; auto boot = bootstrap();
        if (failure == 0) boot.limits.dormantParts = 0;
        if (failure == 1) { boot.limits.dormantBuilds = 0; boot.builds.clear(); }
        auto session = create(adapter, boot);
        if (failure == 1) commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, {}, CreateBuild{}});
        else commit(*session, adapter, add());
        const auto before = session->snapshot(); const auto history = session->history();
        if (failure == 2) adapter.allowActivation = false;
        if (failure == 3) adapter.throwAfterReserve = true;
        if (failure == 4) adapter.unexpectedThrow = true;
        const auto command = inverse(*session);
        if (failure == 4) EXPECT_THROW(static_cast<void>(session->submit(boot.caller, command)), std::runtime_error);
        else {
            static_cast<void>(session->submit(boot.caller, command));
            ASSERT_TRUE(session->advanceOneTick());
            EXPECT_EQ(session->receipt(boot.caller, command.sequence).state, ReceiptState::Rejected);
        }
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
        EXPECT_EQ(session->lastIssuedId(), 101); EXPECT_FALSE(session->journalFaulted());
    }
}
TEST(GameSessionHistory, InverseDebitCannotConsumeAnotherPreparationsReservedFunds) {
    for (bool inverseFirst : {false, true}) {
        FakeAdapter adapter; auto boot = bootstrap(); boot.inventory = {};
        auto part = seededPart(10); part.definition = starterPartKey(StarterPart::Engine);
        part.settings = defaultModuleSettings(*catalog().lookup(part.definition).definition);
        boot.builds[0].parts = {part};
        auto session = create(adapter, boot); commit(*session, adapter, removeCurrent(*session, part.id));
        const auto before = session->snapshot(); const auto history = session->history();
        // Engine yield is 18; a Plate costs 10. Either individual debit fits,
        // but restoring the Engine and buying the Plate together do not.
        adapter.ready = false;
        auto undo = inverse(*session); auto buy = add(2, 1, 1, {1000, 0, 0});
        if (inverseFirst) buy.sequence = RequestSequence{3}; else undo.sequence = RequestSequence{3};
        const auto first = inverseFirst ? undo : buy;
        const auto second = inverseFirst ? buy : undo;
        ASSERT_EQ(session->submit(boot.caller, first).state, ReceiptState::PendingPreparation);
        ASSERT_EQ(session->submit(boot.caller, second).state, ReceiptState::PendingPreparation);
        static_cast<void>(session->cancel(boot.caller, first.sequence));
        EXPECT_EQ(session->receipt(boot.caller, second.sequence).issue.error, SessionError::InsufficientResources);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
        adapter.ready = true; commit(*session, adapter, inverse(*session));
        EXPECT_EQ(session->snapshot().inventory, ResourceAmounts{});
        EXPECT_EQ(session->snapshot().builds[0].parts[0], part);
        EXPECT_FALSE(session->journalFaulted());
    }
}
TEST(GameSessionHistory, ForgedEntryGenerationTargetAndDirectionNeverSupplyInverseAuthority) {
    for (int wrong = 0; wrong < 5; ++wrong) {
        FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
        auto command = inverse(*session); auto& undo = std::get<Undo>(command.intent);
        if (wrong == 0) undo.entry = HistoryEntryId{999};
        if (wrong == 1) undo.expectedHistoryGeneration = HistoryGeneration{999};
        if (wrong == 2) undo.target.build = id(99);
        if (wrong == 3) undo.target.expectedRevision = TopologyRevision{};
        if (wrong == 4) command.intent = Redo{undo.target, undo.entry, undo.expectedHistoryGeneration};
        const auto before = session->snapshot(); const auto history = session->history();
        EXPECT_EQ(session->submit(bootstrap().caller, command).state, ReceiptState::Rejected);
        expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
        EXPECT_EQ(session->lastIssuedId(), 101); EXPECT_FALSE(session->journalFaulted());
    }
}
TEST(GameSessionHistory, ClosureRetiresEscrowWithoutPayingAgainAndJournalRecordsExactActions) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    commit(*session, adapter, inverse(*session));
    std::array<JournalRecord, 64> journal{};
    const auto count = session->journal().copyPrefix(journal);
    ASSERT_EQ(count, 5);
    const auto& purchase = std::get<RequestDecisionRecord>(journal[2].payload);
    const auto& undo = std::get<RequestDecisionRecord>(journal[4].payload);
    EXPECT_EQ(purchase.history.action, HistoryAction::Record);
    EXPECT_EQ(undo.history.action, HistoryAction::Undo);
    EXPECT_EQ(purchase.history.entry, undo.history.entry);
    EXPECT_EQ(purchase.history.after, undo.history.before);
    EXPECT_EQ(purchase.balanceBefore, undo.balanceAfter);
    EXPECT_EQ(purchase.balanceAfter, undo.balanceBefore);
    const auto before = session->snapshot(); const auto generation = session->history().generation;
    session->closeAdmission(); session->closeAdmission();
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history().entries, 0);
    EXPECT_EQ(session->history().dormantParts, 0); EXPECT_FALSE(session->history().redo);
    EXPECT_GT(session->history().generation, generation); EXPECT_EQ(session->lastIssuedId(), 101);
    EXPECT_FALSE(session->journalFaulted());
}

RecoveryContentIdentity recoveryContent() {
    // Synthetic verified-manifest identity for these isolated authority tests.
    RecoveryContentIdentity result; result.manifest = {id(9000), 3};
    result.manifestDigest.fill(std::byte{0x5a}); return result;
}
std::unique_ptr<LogicalRecoveryCheckpoint> capture(const GameSession& session) {
    RecoveryIssue issue;
    auto result = SessionRecovery::capture(session, recoveryContent(), issue);
    if (!result) throw std::runtime_error("failed checkpoint fixture: " + std::to_string(static_cast<int>(issue.error)));
    const auto admitted = SessionRecovery::admit(*result, {kWorld, recoveryContent()}, catalog(), issue);
    if (!admitted) throw std::runtime_error("failed checkpoint admission: " + std::to_string(static_cast<int>(issue.error)));
    // Every valid recovery fixture also crosses the portable byte boundary.
    SaveCodecIssue codec; std::vector<std::byte> bytes, repeated;
    if (!SessionSaveCodec::encodeCheckpoint(*admitted, bytes, codec))
        throw std::runtime_error("checkpoint encoding failed");
    auto decoded = SessionSaveCodec::decodeCheckpoint(bytes, {kWorld, recoveryContent()}, catalog(), codec);
    if (!decoded) throw std::runtime_error("checkpoint decoding failed: " + std::to_string(static_cast<int>(codec.error))
        + " at " + std::to_string(codec.offset) + " recovery " + std::to_string(static_cast<int>(codec.recovery.error)));
    EXPECT_TRUE(SessionSaveCodec::encodeCheckpoint(*decoded, repeated, codec)); EXPECT_EQ(bytes, repeated);
    return std::make_unique<LogicalRecoveryCheckpoint>(decoded->snapshot());
}
TEST(GameSessionDelivery, PublishesOnlyAfterFutureExecutionAndExactRetryNeverPaysTwice) {
    FakeAdapter adapter;auto boot=bootstrap();boot.jobs[0].phase=JobPhase::Accepted;boot.jobs[0].acceptedBy=boot.caller.participant;
    auto session=create(adapter,boot);const auto before=capture(*session);
    ASSERT_TRUE(session->bindExecution(71,SimulationTick{}));EXPECT_FALSE(session->bindExecution(72,SimulationTick{}));
    EXPECT_FALSE(session->advanceOneTick());
    const Command deliver{AuthorityEpoch{1},RequestSequence{1},SessionRevision{},DeliverCargo{id(6)}};
    ASSERT_EQ(session->submit(boot.caller,deliver).state,ReceiptState::PendingPreparation);
    EXPECT_FALSE(session->stageExecution(72,SimulationTick{7}));
    ASSERT_TRUE(session->stageExecution(71,SimulationTick{7}));
    EXPECT_EQ(adapter.activations,0u);EXPECT_EQ(session->snapshot().inventory,boot.inventory);
    EXPECT_EQ(session->cancel(boot.caller,RequestSequence{1}).issue.error,SessionError::Busy);
    RecoveryIssue issue;EXPECT_FALSE(SessionRecovery::capture(*session,recoveryContent(),issue));
    ASSERT_TRUE(session->confirmExecution(71,SimulationTick{6}));
    EXPECT_FALSE(session->confirmExecution(71,SimulationTick{7}));
    EXPECT_EQ(session->snapshot().cargo.size(),1u);EXPECT_EQ(adapter.activations,0u);
    adapter.executionObserved=true;ASSERT_TRUE(session->confirmExecution(71,SimulationTick{7}));
    EXPECT_EQ(adapter.activatedTick,SimulationTick{7});EXPECT_EQ(adapter.activations,1u);
    EXPECT_FALSE(session->confirmExecution(72,SimulationTick{8}));
    const auto delivered=session->snapshot();EXPECT_TRUE(delivered.cargo.empty());
    EXPECT_EQ(delivered.jobs[0].phase,JobPhase::Completed);
    EXPECT_EQ(delivered.inventory.salvageMaterial,boot.inventory.salvageMaterial+50);
    EXPECT_EQ(delivered.inventory.specialMachinery,boot.inventory.specialMachinery+1);
    EXPECT_EQ(session->submit(boot.caller,deliver).state,ReceiptState::Committed);
    EXPECT_EQ(adapter.activations,1u);EXPECT_EQ(session->snapshot().inventory,delivered.inventory);
    auto after=capture(*session);ASSERT_EQ(after->retainedJournalCount,2u);
    auto admitted=SessionRecovery::admit(*before,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(admitted);
    const auto replayed=SessionRecovery::replay(*admitted,after->journalIdentity,
        std::span(after->retainedJournal).subspan(0,2),issue);
    ASSERT_TRUE(replayed)<<static_cast<int>(issue.error);
    EXPECT_EQ(replayed->snapshot().accepted.inventory,delivered.inventory);EXPECT_TRUE(replayed->snapshot().accepted.cargo.empty());
    auto forged=*after;auto& decision=std::get<RequestDecisionRecord>(forged.retainedJournal[1].payload);
    std::get<CargoDeliveryTransition>(decision.objects).reward.salvageMaterial++;
    EXPECT_FALSE(SessionRecovery::admit(forged,{kWorld,recoveryContent()},catalog(),issue));
    Command duplicate=deliver;duplicate.sequence=RequestSequence{2};duplicate.expectedRevision=delivered.revision;
    EXPECT_EQ(session->submit(boot.caller,duplicate).issue.error,SessionError::UnknownCargo);
    EXPECT_EQ(session->snapshot().inventory,delivered.inventory);EXPECT_EQ(adapter.activations,1u);
    ASSERT_TRUE(capture(*session));
}

TEST(GameSessionDelivery, RefusalAndCancellationPreserveCargoJobAndBalance) {
    for(int fault=0;fault<4;++fault) {
        FakeAdapter adapter;auto boot=bootstrap();
        if(fault!=0) {boot.jobs[0].phase=JobPhase::Accepted;boot.jobs[0].acceptedBy=boot.caller.participant;}
        if(fault==1) adapter.rejectBegin=true;
        if(fault==2) adapter.stageAllowed=false;
        auto session=create(adapter,boot);ASSERT_TRUE(session->bindExecution(51,SimulationTick{}));
        const Command command{AuthorityEpoch{1},RequestSequence{1},SessionRevision{},DeliverCargo{id(6)}};
        (void)session->submit(boot.caller,command);
        if(fault==2) {EXPECT_FALSE(session->stageExecution(51,SimulationTick{1}));}
        if(fault==3) {EXPECT_EQ(session->cancel(boot.caller,RequestSequence{1}).issue.error,SessionError::Canceled);}
        const auto after=session->snapshot();EXPECT_EQ(after.inventory,boot.inventory);EXPECT_EQ(after.cargo,boot.cargo);
        EXPECT_EQ(after.jobs,boot.jobs);EXPECT_EQ(adapter.activations,0u);ASSERT_TRUE(capture(*session));
    }
}

TEST(GameSessionDelivery, SecondHaulAfterByteRecoveryPaysOnlyItsOwnCargoAndRetainsTheFirstReceipt) {
    FakeAdapter adapter;auto boot=bootstrap();boot.jobs[0].phase=JobPhase::Accepted;boot.jobs[0].acceptedBy=boot.caller.participant;
    const CargoDefinition crate{{id(1001),1},700,.1536,{100,0},CargoRecoveryRule::PreserveUnique};
    boot.cargoDefinitions.push_back(crate);boot.jobs.push_back({id(7)});
    boot.cargo.push_back({id(8),crate.key,boot.caller.participant,{-30,-4,-70},{},id(7)});
    auto session=create(adapter,boot);ASSERT_TRUE(session->bindExecution(71,SimulationTick{}));
    const Command first{AuthorityEpoch{1},RequestSequence{1},SessionRevision{},DeliverCargo{id(6)}};
    ASSERT_EQ(session->submit(boot.caller,first).state,ReceiptState::PendingPreparation);
    ASSERT_TRUE(session->stageExecution(71,SimulationTick{1}));adapter.executionObserved=true;
    ASSERT_TRUE(session->confirmExecution(71,SimulationTick{1}));
    const auto firstBalance=session->snapshot().inventory;
    ASSERT_EQ(session->snapshot().cargo.size(),1u);EXPECT_EQ(session->snapshot().cargo[0],boot.cargo[1]);
    // Real session/checkpoint codecs and new writer/token; the adapter models
    // confirmed physical execution only. This is not a GPU or disk-I/O test.
    const auto image=capture(*session);RecoveryIssue issue;
    auto checkpoint=SessionRecovery::admit(*image,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(checkpoint);
    session.reset();FakeAdapter fresh;
    auto resumed=SessionRecovery::restore(checkpoint,fixtureIncarnation(),fresh,issue);ASSERT_TRUE(resumed)<<int(issue.error);
    auto& live=*resumed->session;const auto caller=resumed->initial->snapshot().accepted.caller;
    const auto epoch=resumed->initial->snapshot().accepted.epoch;
    ASSERT_TRUE(live.bindExecution(72,live.snapshot().tick));
    EXPECT_EQ(live.submit(boot.caller,first).issue.error,SessionError::WrongToken);
    const Command accept{epoch,RequestSequence{1},live.snapshot().revision,AcceptJob{id(7)}};
    ASSERT_EQ(live.submit(caller,accept).state,ReceiptState::PendingPreparation);
    ASSERT_TRUE(live.stageExecution(72,SimulationTick{2}));fresh.executionObserved=true;
    ASSERT_TRUE(live.confirmExecution(72,SimulationTick{2}));fresh.completeRetirement();
    const Command second{epoch,RequestSequence{2},live.snapshot().revision,DeliverCargo{id(8)}};
    ASSERT_EQ(live.submit(caller,second).state,ReceiptState::PendingPreparation);
    ASSERT_TRUE(live.stageExecution(72,SimulationTick{3}));
    EXPECT_EQ(live.snapshot().inventory,firstBalance);
    ASSERT_TRUE(live.confirmExecution(72,SimulationTick{3}));
    const auto paid=live.snapshot();EXPECT_TRUE(paid.cargo.empty());ASSERT_EQ(paid.jobs.size(),2u);
    for(const auto& job:paid.jobs)EXPECT_EQ(job.phase,JobPhase::Completed);
    EXPECT_EQ(paid.inventory.salvageMaterial,boot.inventory.salvageMaterial+150);
    EXPECT_EQ(paid.inventory.specialMachinery,boot.inventory.specialMachinery+1);
    EXPECT_EQ(live.submit(caller,second).state,ReceiptState::Committed);
    for(uint64_t cargo:{uint64_t{6},uint64_t{8}}) {
        const Command again{epoch,RequestSequence{live.admissionState().admittedThrough.value()+1},paid.revision,DeliverCargo{id(cargo)}};
        EXPECT_EQ(live.submit(caller,again).issue.error,SessionError::UnknownCargo);
    }
    EXPECT_EQ(live.snapshot().inventory,paid.inventory);
    const auto final=capture(live);EXPECT_EQ(final->accepted.jobs,paid.jobs);EXPECT_EQ(final->accepted.inventory,paid.inventory);
}

TEST(GameSessionCheckpoint, CopiesAcceptedWorldHistoryAndCoverageWithoutPublishingAnything) {
    FakeAdapter adapter; auto session = create(adapter);
    commit(*session, adapter, add());
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1}, AcceptJob{id(5)}});
    const auto before = session->snapshot(); const auto history = session->history();
    const auto begins = adapter.begins, activations = adapter.activations;
    auto image = capture(*session);
    EXPECT_EQ(image->schema, kLogicalRecoverySchema); EXPECT_EQ(image->content, recoveryContent());
    EXPECT_EQ(image->accepted.world, kWorld); EXPECT_EQ(image->accepted.caller, bootstrap().caller);
    EXPECT_EQ(image->accepted.revision, before.revision); EXPECT_EQ(image->accepted.tick, before.tick);
    EXPECT_EQ(image->accepted.inventory, before.inventory);
    EXPECT_EQ(image->accepted.cargo, before.cargo); EXPECT_EQ(image->accepted.jobs, before.jobs);
    EXPECT_EQ(encode(image->accepted.builds[0]), encode(before.builds[0]));
    EXPECT_EQ(image->history.generation, history.generation); EXPECT_EQ(image->history.count, 1);
    EXPECT_EQ(image->history.entries[0].edit.afterPart, before.builds[0].parts[0]);
    EXPECT_EQ(image->history.entries[0].debit, catalog().lookup(before.builds[0].parts[0].definition).definition->cost);
    EXPECT_EQ(image->coveredThrough, session->journal().appendedThrough());
    EXPECT_EQ(image->retainedJournalCount, 5); EXPECT_EQ(image->pendingCount, 0);
    EXPECT_EQ(image->receiptCount, 2); EXPECT_TRUE(image->historyClearedOnRestore);
    EXPECT_EQ(image->durability, ReceiptDurability::Volatile);
    EXPECT_LE(image->ownedBytes, kMaximumLogicalRecoveryBytes);
    // The trusted storage worker receives owned copies, not writable authority.
    image->accepted.inventory = {}; image->accepted.builds[0].parts.clear();
    image->history.entries[0].debit = {};
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
    EXPECT_EQ(session->snapshot().tick, before.tick);
    EXPECT_EQ(adapter.begins, begins); EXPECT_EQ(adapter.activations, activations);
}
TEST(GameSessionCheckpoint, RetainsCanceledSecondSlotAndFullIntentBehindWaitingFront) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    const auto first = add(); const auto second = add(2, 0, 0, {1000, 0, 0});
    ASSERT_EQ(session->submit(bootstrap().caller, first).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(bootstrap().caller, second).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(session->cancel(bootstrap().caller, second.sequence).state, ReceiptState::PendingPreparation);
    auto image = capture(*session);
    EXPECT_EQ(image->admission.processedThrough, RequestFrontier{});
    EXPECT_EQ(image->admission.admittedThrough, RequestFrontier{2});
    ASSERT_EQ(image->pendingCount, 2);
    EXPECT_EQ(image->pending[0].command, first); EXPECT_EQ(image->pending[1].command, second);
    EXPECT_EQ(image->pending[0].state, RecoveryPendingState::Preparing);
    EXPECT_EQ(image->pending[1].state, RecoveryPendingState::CancelReady);
    EXPECT_EQ(image->pending[1].error, SessionError::Canceled);
    EXPECT_EQ(image->pending[0].assignedObject, id(101)); EXPECT_EQ(image->pending[1].assignedObject, id(102));
    EXPECT_EQ(image->pending[0].admissionJournal, JournalSequence{2});
    EXPECT_EQ(image->pending[1].admissionJournal, JournalSequence{3});
    EXPECT_EQ(image->allocator.issuedThrough, 102); EXPECT_EQ(image->allocator.reservedThrough, 164);
    EXPECT_TRUE(image->accepted.builds[0].parts.empty()); EXPECT_EQ(image->receiptCount, 0);
    EXPECT_EQ(adapter.discards, 1); EXPECT_EQ(adapter.activations, 0);
    // Resolving the live source cannot alter the earlier saved proposal interval.
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    EXPECT_EQ(image->admission.processedThrough, RequestFrontier{});
    auto after = capture(*session); EXPECT_EQ(after->pendingCount, 0); EXPECT_EQ(after->receiptCount, 2);
    EXPECT_EQ(after->receipts[1].outcome.error, SessionError::Canceled);
}
TEST(GameSessionCheckpoint, DistinguishesReadyAndRejectedPreparationsWithoutInventingTerminalReceipts) {
    FakeAdapter adapter; auto session = create(adapter);
    ASSERT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    auto bad = add(2); bad.expectedRevision = SessionRevision{99};
    ASSERT_EQ(session->submit(bootstrap().caller, bad).state, ReceiptState::PendingPreparation);
    const auto image = capture(*session);
    EXPECT_EQ(image->pending[0].state, RecoveryPendingState::Ready);
    EXPECT_EQ(image->pending[1].state, RecoveryPendingState::RejectReady);
    EXPECT_EQ(image->pending[1].error, SessionError::StaleRevision);
    EXPECT_EQ(image->pending[1].command, bad);
    EXPECT_FALSE(image->pending[1].assignedObject); EXPECT_EQ(image->receiptCount, 0);
    EXPECT_EQ(image->admission.processedThrough, RequestFrontier{});
}
TEST(GameSessionCheckpoint, CapturesDormantPartAndBuildForLaterDeltaValidationOnly) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.builds.clear(); auto session = create(adapter, boot);
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, {}, CreateBuild{}});
    auto buy = add(2, 1); std::get<AddPart>(buy.intent).target.build = id(101);
    commit(*session, adapter, buy); const auto part = session->snapshot().builds[0].parts[0];
    commit(*session, adapter, inverse(*session)); commit(*session, adapter, inverse(*session));
    const auto image = capture(*session);
    EXPECT_TRUE(image->accepted.builds.empty()); EXPECT_EQ(image->history.count, 2);
    EXPECT_EQ(image->history.applied, 0); ASSERT_EQ(image->history.partCount, 1);
    EXPECT_EQ(image->history.parts[0], part); ASSERT_EQ(image->history.buildCount, 1);
    EXPECT_EQ(image->history.builds[0].id, id(101));
    EXPECT_EQ(image->history.builds[0].revision, TopologyRevision{3});
    EXPECT_FALSE(image->history.builds[0].materialized); EXPECT_TRUE(image->historyClearedOnRestore);
    EXPECT_EQ(image->allocator.issuedThrough, 102); EXPECT_EQ(image->accepted.lastIssuedId, 102);
}
TEST(GameSessionCheckpoint, ExactRamPrefixReleaseKeepsMarkersAndCannotAcknowledgeAnotherWorld) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.journalRecords = 5; boot.limits.receipts = 1;
    auto session = create(adapter, boot); commit(*session, adapter, add());
    const auto before = session->snapshot(); const auto image = capture(*session);
    ASSERT_EQ(image->retainedJournalCount, 3);
    auto bad = image->retainedJournal; bad[0].world.bytes[0] ^= 1;
    EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session, std::span(bad).first(3)), JournalError::PrefixMismatch);
    EXPECT_EQ(session->journal().size(), 3);
    bad = image->retainedJournal;
    std::get<RequestDecisionRecord>(bad[2].payload).balanceAfter.salvageMaterial += 1;
    EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session, std::span(bad).first(3)), JournalError::PrefixMismatch);
    const auto prefix = std::span(image->retainedJournal).first(3);
    EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session, prefix), JournalError::None);
    EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session, prefix), JournalError::PrefixMismatch);
    expectWorldEqual(before, session->snapshot());
    EXPECT_EQ(session->receipt(boot.caller, RequestSequence{1}).durability, ReceiptDurability::Volatile);
    auto after = capture(*session); EXPECT_EQ(after->retainedJournalCount, 0);
    EXPECT_EQ(after->coveredThrough, JournalFrontier{3}); EXPECT_EQ(after->modelReleasedThrough, JournalFrontier{3});
    commit(*session, adapter, inverse(*session));
    after = capture(*session); EXPECT_EQ(after->receiptCount, 1);
    EXPECT_EQ(after->receipts[0].command.sequence, RequestSequence{2});
    EXPECT_EQ(after->admission.processedThrough, RequestFrontier{2});
    EXPECT_EQ(session->submit(boot.caller, add()).issue.error, SessionError::AlreadyProcessed);
    EXPECT_EQ(after->retainedJournal[0].sequence, JournalSequence{4});
}
TEST(GameSessionCheckpoint, MemoryAndContentPreflightRefuseWithoutChangingAuthorityOrTransport) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    const auto image = capture(*session); const auto before = session->snapshot();
    const auto history = session->history(); RecoveryIssue issue;
    EXPECT_FALSE(SessionRecovery::capture(*session, recoveryContent(), issue, image->ownedBytes - 1));
    EXPECT_EQ(issue.error, RecoveryError::Capacity);
    EXPECT_NE(SessionRecovery::capture(*session, recoveryContent(), issue, image->ownedBytes), nullptr);
    for (const size_t size : {size_t{0}, kMaximumLogicalRecoveryBytes + 1}) {
        EXPECT_FALSE(SessionRecovery::capture(*session, recoveryContent(), issue, size));
        EXPECT_EQ(issue.error, RecoveryError::Capacity);
    }
    for (int wrong = 0; wrong < 5; ++wrong) {
        auto content = recoveryContent();
        if (wrong == 0) content.manifest.id = {};
        if (wrong == 1) content.manifest.version = 0;
        if (wrong == 2) content.manifestDigest.fill(std::byte{});
        if (wrong == 3) content.profile = 2;
        if (wrong == 4) content.profileVersion = 2;
        EXPECT_FALSE(SessionRecovery::capture(*session, content, issue));
        EXPECT_EQ(issue.error, RecoveryError::InvalidContentIdentity);
    }
    expectWorldEqual(before, session->snapshot()); EXPECT_EQ(session->history(), history);
    EXPECT_EQ(session->journal().size(), image->retainedJournalCount); EXPECT_EQ(session->snapshot().tick, before.tick);
}
TEST(GameSessionCheckpoint, ClosedAdmissionExportsCanceledDecisionsAndRetiredHistoryWithoutRefund) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    commit(*session, adapter, inverse(*session));
    adapter.ready = false;
    ASSERT_EQ(session->submit(bootstrap().caller, inverse(*session, false)).state, ReceiptState::PendingPreparation);
    const auto balance = session->snapshot().inventory;
    session->closeAdmission(); const auto image = capture(*session);
    EXPECT_FALSE(image->admission.open); EXPECT_EQ(image->admission.retiredThrough, AdmissionFrontier{1});
    EXPECT_EQ(image->admission.processedThrough, RequestFrontier{3}); EXPECT_EQ(image->pendingCount, 0);
    EXPECT_EQ(image->history.count, 0); EXPECT_EQ(image->history.partCount, 0);
    EXPECT_EQ(image->accepted.inventory, balance); EXPECT_EQ(image->allocator.issuedThrough, 101);
    ASSERT_EQ(image->receiptCount, 3); EXPECT_EQ(image->receipts[2].outcome.error, SessionError::Canceled);
    EXPECT_TRUE(std::holds_alternative<AdmissionClosedRecord>(image->retainedJournal[image->retainedJournalCount - 1].payload));
}
TEST(GameSessionCheckpoint, FullHistoryAndReceiptEvictionStayBoundedAcrossJournalTransportCycles) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.receipts = 2;
    auto session = create(adapter, boot);
    const auto release = [&] {
        const auto image = capture(*session);
        EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session,
            std::span(image->retainedJournal).first(image->retainedJournalCount)), JournalError::None);
    };
    for (uint64_t i = 0; i < 40; ++i) {
        commit(*session, adapter, add(i + 1, i, i, {static_cast<int32_t>(i * 300), 29, -211}));
        const auto image = capture(*session);
        EXPECT_EQ(image->history.count, std::min(uint64_t{32}, i + 1));
        EXPECT_EQ(image->receiptCount, std::min(uint64_t{2}, i + 1));
        EXPECT_LE(image->ownedBytes, kMaximumLogicalRecoveryBytes);
        release();
    }
    for (int i = 0; i < 32; ++i) { commit(*session, adapter, inverse(*session)); release(); }
    const auto image = capture(*session);
    EXPECT_EQ(image->history.count, 32); EXPECT_EQ(image->history.applied, 0);
    EXPECT_EQ(image->history.partCount, 32); EXPECT_FALSE(session->history().undo);
    ASSERT_EQ(image->accepted.builds[0].parts.size(), 8);
    EXPECT_EQ(image->allocator.issuedThrough, 140); EXPECT_EQ(image->admission.processedThrough, RequestFrontier{72});
    EXPECT_EQ(image->receipts[0].command.sequence, RequestSequence{71});
    EXPECT_EQ(image->receipts[1].command.sequence, RequestSequence{72});
    EXPECT_EQ(session->submit(boot.caller, add()).issue.error, SessionError::AlreadyProcessed);
    const auto cost = catalog().lookup(starterPartKey(StarterPart::Plate)).definition->cost;
    EXPECT_EQ(image->accepted.inventory.salvageMaterial, boot.inventory.salvageMaterial - 8 * cost.salvageMaterial);
    EXPECT_EQ(image->retainedJournalCount, 0); EXPECT_EQ(image->coveredThrough, image->modelReleasedThrough);
    EXPECT_FALSE(session->journalFaulted());
}

std::unique_ptr<ValidatedRecoveryCheckpoint> admit(const LogicalRecoveryCheckpoint& image, RecoveryIssue& issue) {
    return SessionRecovery::admit(image, {kWorld, recoveryContent()}, catalog(), issue);
}
TEST(GameSessionRecoveryAdmission, OwnsValidatedInputAndRequiresIndependentWorldContentAndSchema) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    auto source = capture(*session); RecoveryIssue issue;
    auto accepted = admit(*source, issue); ASSERT_NE(accepted, nullptr); EXPECT_FALSE(issue);
    source->accepted.inventory = {};
    EXPECT_EQ(accepted->snapshot().accepted.inventory, session->snapshot().inventory);
    for (int fault = 0; fault < 6; ++fault) {
        auto bad = accepted->snapshot();
        if (fault == 0) bad.schema = 2;
        if (fault == 1) bad.historyClearedOnRestore = false;
        if (fault == 2) bad.content.manifest.version += 1;
        if (fault == 3) bad.content.manifestDigest[4] ^= std::byte{1};
        if (fault == 4) bad.accepted.world.bytes[0] ^= 1;
        if (fault == 5) bad.durability = static_cast<ReceiptDurability>(1);
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue);
    }
    auto expected = ExpectedRecoveryIdentity{kWorld, recoveryContent()}; expected.content.manifestDigest.fill(std::byte{});
    EXPECT_FALSE(SessionRecovery::admit(accepted->snapshot(), expected, catalog(), issue));
    EXPECT_EQ(accepted->snapshot().accepted.builds[0].parts.size(), 1);
}
TEST(GameSessionRecoveryAdmission, RejectsOversizedCountsAndInconsistentOwnedByteAccountingBeforeReads) {
    FakeAdapter adapter; auto session = create(adapter); const auto source = capture(*session);
    for (int fault = 0; fault < 12; ++fault) {
        auto bad = *source; RecoveryIssue issue;
        if (fault == 0) bad.pendingCount = 3;
        if (fault == 1) bad.receiptCount = 65;
        if (fault == 2) bad.retainedJournalCount = 65;
        if (fault == 3) bad.history.count = 33;
        if (fault == 4) bad.history.applied = 1;
        if (fault == 5) bad.history.partCount = 65;
        if (fault == 6) bad.history.buildCount = 9;
        if (fault == 7) --bad.ownedBytes;
        if (fault == 8) bad.ownedBytes = std::numeric_limits<size_t>::max();
        if (fault == 9) bad.accepted.builds.resize(33);
        if (fault == 10) bad.accepted.builds[0].parts.resize(257);
        if (fault == 11) bad.accepted.builds[0].connections.resize(1025);
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_EQ(issue.error, RecoveryError::Capacity) << fault;
    }
}
TEST(GameSessionRecoveryAdmission, RejectsFrontierHolesRetiredAdmissionsAndLostPendingIdentities) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    ASSERT_EQ(session->submit(bootstrap().caller, add()).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(bootstrap().caller, add(2, 0, 0, {1000, 0, 0})).state, ReceiptState::PendingPreparation);
    static_cast<void>(session->cancel(bootstrap().caller, RequestSequence{2}));
    const auto source = capture(*session);
    for (int fault = 0; fault < 15; ++fault) {
        auto bad = *source; RecoveryIssue issue;
        if (fault == 0) bad.admission.processedThrough = RequestFrontier{3};
        if (fault == 1) bad.pendingCount = 1;
        if (fault == 2) bad.pending[1].command.sequence = RequestSequence{3};
        if (fault == 3) bad.admission.retiredThrough = AdmissionFrontier{1};
        if (fault == 4) bad.admission.open = false;
        if (fault == 5) bad.admission.generation = {};
        if (fault == 6) bad.pending[0].command.epoch = AuthorityEpoch{2};
        if (fault == 7) bad.pending[0].assignedObject.reset();
        if (fault == 8) bad.pending[1].assignedObject = bad.pending[0].assignedObject;
        if (fault == 9) bad.pending[0].assignedObject = id(1);
        if (fault == 10) bad.pending[1].state = RecoveryPendingState::Ready;
        if (fault == 11) bad.pending[1].error = SessionError::None;
        if (fault == 12) bad.pending[0].admissionJournal = JournalSequence{999};
        if (fault == 13) bad.allocator.reservedThrough = 101;
        if (fault == 14) bad.accepted.lastIssuedId = 101;
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue) << fault;
    }
    EXPECT_EQ(session->pendingCount(), 2); EXPECT_EQ(session->processedSequence(), 0);
}
TEST(GameSessionRecoveryAdmission, RejectsRoleAliasesNoncanonicalBuildsAndMissingLoanEntitlements) {
    FakeAdapter adapter; auto boot = bootstrap();
    auto loan = seededPart(10); loan.provenance = {PartOrigin::StarterLoan, id(20)};
    boot.starterEntitlements = {id(20)}; boot.builds[0].parts = {loan, seededPart(11, {1000, 0, 0})};
    auto session = create(adapter, boot); const auto source = capture(*session);
    for (int fault = 0; fault < 8; ++fault) {
        auto bad = *source; RecoveryIssue issue;
        if (fault == 0) bad.accepted.cargo[0].id = bad.accepted.jobs[0].id;
        if (fault == 1) bad.accepted.builds[0].parts[0].id = boot.caller.sessionToken;
        if (fault == 2) bad.accepted.builds[0].parts[0].provenance.starterEntitlement = id(77);
        if (fault == 3) std::swap(bad.accepted.builds[0].parts[0], bad.accepted.builds[0].parts[1]);
        if (fault == 4) bad.accepted.builds[0].parts[1].placement = loan.placement;
        if (fault == 5) bad.accepted.builds[0].owner = id(77);
        if (fault == 6) bad.accepted.builds[0].parts[0].definition.version = 999;
        if (fault == 7) bad.accepted.builds[0].parts[0].placement.rotation.value = 24;
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue) << fault;
    }
}
TEST(GameSessionRecoveryAdmission, RejectsForgedHistoryAmountsImagesCursorAndDormantOwnership) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    const auto part = session->snapshot().builds[0].parts[0];
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1},
        MovePart{{id(3), TopologyRevision{1}}, part.id, {{1000, 0, 0}, {}}}});
    commit(*session, adapter, removeCurrent(*session, part.id));
    const auto source = capture(*session);
    for (int fault = 0; fault < 13; ++fault) {
        auto bad = *source; RecoveryIssue issue;
        if (fault == 0) bad.history.entries[0].debit.salvageMaterial += 1;
        if (fault == 1) bad.history.entries[2].credit.salvageMaterial += 1;
        if (fault == 2) bad.history.entries[1].edit.beforePart->health -= 1;
        if (fault == 3) bad.history.parts[0].placement.translation.x += 1;
        if (fault == 4) bad.history.parts[0].id = bootstrap().caller.participant;
        if (fault == 5) bad.history.applied = 1;
        if (fault == 6) bad.history.entries[1].id = bad.history.entries[0].id;
        if (fault == 7) bad.history.entries[1].caller.sessionToken = id(99);
        if (fault == 8) bad.history.entries[2].admission = AdmissionGeneration{2};
        if (fault == 9) bad.history.entries[1].edit.after->revision = TopologyRevision{99};
        if (fault == 10) bad.history.entries[1].edit.afterPart->paint[0] = 17;
        if (fault == 11) bad.history.entries[3] = bad.history.entries[0];
        if (fault == 12) bad.history.parts[0].owningBuild = id(99);
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue) << fault;
    }
}
TEST(GameSessionRecoveryAdmission, RejectsJournalCorruptionAndContradictoryCoveredState) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    const auto source = capture(*session);
    ASSERT_EQ(source->retainedJournalCount, 3);
    for (int fault = 0; fault < 14; ++fault) {
        auto bad = *source; RecoveryIssue issue;
        auto& decision = std::get<RequestDecisionRecord>(bad.retainedJournal[2].payload);
        if (fault == 0) bad.retainedJournal[1].sequence = JournalSequence{9};
        if (fault == 1) bad.retainedJournal[2].world.bytes[0] ^= 1;
        if (fault == 2) bad.retainedJournal[2].epoch = AuthorityEpoch{2};
        if (fault == 3) bad.coveredThrough = JournalFrontier{4};
        if (fault == 4) bad.modelReleasedThrough = JournalFrontier{1};
        if (fault == 5) bad.accepted.inventory.salvageMaterial += 1;
        if (fault == 6) decision.balanceAfter.salvageMaterial += 1;
        if (fault == 7) std::get<BuildTransition>(decision.objects).afterPart->placement.translation.x += 1;
        if (fault == 8) decision.history.action = HistoryAction::None;
        if (fault == 9) decision.key.caller.sessionToken = id(99);
        if (fault == 10) decision.outcome.object = id(1);
        if (fault == 11) bad.retainedJournal[1].tick = SimulationTick{99};
        if (fault == 12) bad.allocator.reservedThrough += 64;
        if (fault == 13) bad.retainedJournal[3] = bad.retainedJournal[2];
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue) << fault;
    }
}
TEST(GameSessionRecoveryAdmission, RejectsChangedReceiptIntentOutcomeAndSequenceAfterTransportRelease) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.receipts = 1; auto session = create(adapter, boot);
    commit(*session, adapter, add()); commit(*session, adapter, inverse(*session));
    const auto beforeRelease = capture(*session);
    ASSERT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session,
        std::span(beforeRelease->retainedJournal).first(beforeRelease->retainedJournalCount)), JournalError::None);
    const auto source = capture(*session);
    for (int fault = 0; fault < 8; ++fault) {
        auto bad = *source; RecoveryIssue issue;
        if (fault == 0) bad.receipts[0].command.sequence = RequestSequence{1};
        if (fault == 1) bad.receipts[0].command.epoch = AuthorityEpoch{2};
        if (fault == 2) bad.receipts[0].outcome.object = id(1);
        if (fault == 3) bad.receipts[0].outcome.buildRevision = TopologyRevision{1};
        if (fault == 4) bad.receipts[0].outcome.state = ReceiptState::PendingPreparation;
        if (fault == 5) bad.receipts[0].observedExpectedSequence = RequestSequence{2};
        if (fault == 6) bad.receipts[0].decisionJournal = JournalSequence{99};
        if (fault == 7) bad.receipts[1] = bad.receipts[0];
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue) << fault;
    }
    RecoveryIssue issue; EXPECT_NE(admit(*source, issue), nullptr);
}

std::vector<JournalRecord> portableJournal(JournalIdentity identity, std::span<const JournalRecord> records) {
    SaveCodecIssue issue; std::vector<std::byte> bytes, repeated; std::vector<JournalRecord> decoded;
    if (!SessionSaveCodec::encodeJournal(identity, records, bytes, issue)
        || !SessionSaveCodec::decodeJournal(bytes, identity, decoded, issue))
        throw std::runtime_error("journal byte fixture: " + std::to_string(static_cast<int>(issue.error)));
    EXPECT_TRUE(SessionSaveCodec::encodeJournal(identity, decoded, repeated, issue)); EXPECT_EQ(bytes, repeated);
    EXPECT_TRUE(std::equal(records.begin(), records.end(), decoded.begin(), decoded.end()));
    return decoded;
}
std::vector<JournalRecord> journalAfter(const GameSession& session, JournalFrontier frontier = {}) {
    std::array<JournalRecord, kMaximumJournalRecords> retained{};
    const auto count = session.journal().copyPrefix(retained);
    std::vector<JournalRecord> result;
    for (size_t i = 0; i < count; ++i) if (retained[i].sequence.value() > frontier.value()) result.push_back(retained[i]);
    return portableJournal(session.journal().identity(), result);
}
void expectRecoveredState(const LogicalRecoveryCheckpoint& actual, const LogicalRecoveryCheckpoint& expected) {
    EXPECT_EQ(actual.content, expected.content);
    EXPECT_EQ(actual.accepted.world, expected.accepted.world);
    EXPECT_EQ(actual.accepted.caller, expected.accepted.caller);
    EXPECT_EQ(actual.accepted.epoch, expected.accepted.epoch);
    EXPECT_EQ(actual.accepted.lastIssuedId, expected.accepted.lastIssuedId);
    EXPECT_EQ(actual.accepted.revision, expected.accepted.revision);
    EXPECT_EQ(actual.accepted.tick, expected.accepted.tick);
    EXPECT_EQ(actual.accepted.inventory, expected.accepted.inventory);
    EXPECT_EQ(actual.accepted.starterEntitlements, expected.accepted.starterEntitlements);
    EXPECT_EQ(actual.accepted.starterKits,expected.accepted.starterKits);EXPECT_EQ(actual.accepted.storedParts,expected.accepted.storedParts);
    EXPECT_EQ(actual.accepted.cargoDefinitions, expected.accepted.cargoDefinitions);
    EXPECT_EQ(actual.accepted.cargo, expected.accepted.cargo);
    EXPECT_EQ(actual.accepted.jobs, expected.accepted.jobs);
    ASSERT_EQ(actual.accepted.builds.size(), expected.accepted.builds.size());
    for (size_t i = 0; i < actual.accepted.builds.size(); ++i)
        EXPECT_EQ(encode(actual.accepted.builds[i]), encode(expected.accepted.builds[i]));
    EXPECT_EQ(actual.admission, expected.admission);
    EXPECT_EQ(actual.allocator, expected.allocator);
    EXPECT_EQ(actual.history.generation, expected.history.generation);
    EXPECT_EQ(actual.history.count, expected.history.count);
    EXPECT_EQ(actual.history.applied, expected.history.applied);
    EXPECT_EQ(actual.history.entries, expected.history.entries);
    EXPECT_EQ(actual.history.partCount, expected.history.partCount);
    EXPECT_EQ(actual.history.parts, expected.history.parts);
    EXPECT_EQ(actual.history.buildCount, expected.history.buildCount);
    EXPECT_EQ(actual.history.builds, expected.history.builds);
    EXPECT_EQ(actual.pendingCount, expected.pendingCount);
    EXPECT_EQ(actual.receiptCount, expected.receiptCount);
    EXPECT_EQ(actual.receipts, expected.receipts);
    EXPECT_EQ(actual.coveredThrough, expected.coveredThrough);
    EXPECT_EQ(actual.journalIdentity, expected.journalIdentity);
    EXPECT_EQ(actual.origin, expected.origin);
    EXPECT_EQ(actual.ownedBytes, expected.ownedBytes);
}
TEST(GameSessionRecoveryReplay, EveryJournalSplitPreservesExactCompensationJobsAndBuildHighWater) {
    FakeAdapter adapter; auto session = create(adapter);
    auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    ASSERT_NE(base, nullptr);
    commit(*session, adapter, add());
    const auto part = session->snapshot().builds[0].parts[0];
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1},
        MovePart{{id(3), TopologyRevision{1}}, part.id, {{1000, 0, 0}, {}}}});
    commit(*session, adapter, inverse(*session));
    commit(*session, adapter, inverse(*session));
    commit(*session, adapter, inverse(*session, false));
    commit(*session, adapter, removeCurrent(*session, part.id));
    commit(*session, adapter, inverse(*session));
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{8}, SessionRevision{7}, AcceptJob{id(5)}});
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{9}, SessionRevision{8}, CreateBuild{}});
    commit(*session, adapter, inverse(*session));
    commit(*session, adapter, inverse(*session, false));
    const auto expected = capture(*session); const auto records = journalAfter(*session);
    const auto begins = adapter.begins, activations = adapter.activations;
    for (size_t split = 0; split <= records.size(); ++split) {
        const auto first = SessionRecovery::replay(*base, source->journalIdentity, std::span(records).first(split), issue);
        ASSERT_NE(first, nullptr) << split << ": " << static_cast<int>(issue.error) << " record " << issue.record;
        const auto result = SessionRecovery::replay(*first, source->journalIdentity, std::span(records).subspan(split), issue);
        ASSERT_NE(result, nullptr) << split << ": " << static_cast<int>(issue.error) << " record " << issue.record;
        expectRecoveredState(result->snapshot(), *expected);
        EXPECT_FALSE(SessionRecovery::replay(*result, source->journalIdentity, records, issue));
        EXPECT_EQ(issue.error, RecoveryError::InvalidCoverage);
    }
    EXPECT_EQ(base->snapshot().accepted.revision, SessionRevision{});
    EXPECT_EQ(adapter.begins, begins); EXPECT_EQ(adapter.activations, activations);
}
TEST(GameSessionRecoveryReplay, CapturedPendingIntervalKeepsCanceledSecondSlotAndOrderedReceipts) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    static_cast<void>(session->submit(bootstrap().caller, add()));
    static_cast<void>(session->submit(bootstrap().caller, add(2, 0, 0, {1000, 0, 0})));
    static_cast<void>(session->cancel(bootstrap().caller, RequestSequence{2}));
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    adapter.ready = true; ASSERT_TRUE(session->advanceOneTick());
    const auto records = journalAfter(*session, source->coveredThrough);
    ASSERT_EQ(records.size(), 2);
    const auto result = SessionRecovery::replay(*base, source->journalIdentity, records, issue);
    ASSERT_NE(result, nullptr) << static_cast<int>(issue.error);
    expectRecoveredState(result->snapshot(), *capture(*session));
    EXPECT_EQ(result->snapshot().receipts[0].observedExpectedSequence, RequestSequence{3});
    EXPECT_EQ(result->snapshot().receipts[1].outcome.error, SessionError::Canceled);
    EXPECT_EQ(base->snapshot().pending[1].state, RecoveryPendingState::CancelReady);
    auto forged = records;
    std::get<RequestDecisionRecord>(forged[1].payload).outcome.error = SessionError::AdapterRejected;
    EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, forged, issue));
    EXPECT_EQ(issue.record, 1);
}
TEST(GameSessionRecoveryReplay, AdmissionWithoutAssignedIdDoesNotInventPreparationOrSpend) {
    FakeAdapter adapter; auto session = create(adapter);
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    const auto denied = session->submit(bootstrap().caller, add(1, 99));
    ASSERT_EQ(denied.issue.error, SessionError::StaleRevision);
    const auto records = journalAfter(*session); ASSERT_EQ(records.size(), 2);
    const auto cut = SessionRecovery::replay(*base, source->journalIdentity, std::span(records).first(1), issue);
    ASSERT_NE(cut, nullptr) << static_cast<int>(issue.error);
    ASSERT_EQ(cut->snapshot().pendingCount, 1);
    EXPECT_EQ(cut->snapshot().pending[0].state, RecoveryPendingState::JournalAdmitted);
    EXPECT_FALSE(cut->snapshot().pending[0].assignedObject);
    EXPECT_EQ(cut->snapshot().accepted.inventory, source->accepted.inventory);
    const auto result = SessionRecovery::replay(*cut, source->journalIdentity, std::span(records).subspan(1), issue);
    ASSERT_NE(result, nullptr) << static_cast<int>(issue.error);
    expectRecoveredState(result->snapshot(), *capture(*session));
    EXPECT_EQ(adapter.begins, 0);
}
TEST(GameSessionRecoveryReplay, BurnedUnadmittedIdsRemainCoveredByTheRecordedReservationHorizon) {
    FakeAdapter adapter; auto session = create(adapter);
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    adapter.unexpectedThrow = true;
    EXPECT_THROW(static_cast<void>(session->submit(bootstrap().caller, add())), std::runtime_error);
    ASSERT_EQ(session->lastIssuedId(), 101); ASSERT_EQ(session->pendingCount(), 0);
    const auto lease = journalAfter(*session); ASSERT_EQ(lease.size(), 1);
    const auto crash = SessionRecovery::replay(*base, source->journalIdentity, lease, issue);
    ASSERT_NE(crash, nullptr) << static_cast<int>(issue.error);
    EXPECT_EQ(crash->snapshot().allocator.issuedThrough, 100);
    EXPECT_EQ(crash->snapshot().allocator.reservedThrough, 164);
    adapter.unexpectedThrow = false; commit(*session, adapter, add());
    const auto result = SessionRecovery::replay(*crash, source->journalIdentity,
        journalAfter(*session, crash->snapshot().coveredThrough), issue);
    ASSERT_NE(result, nullptr) << static_cast<int>(issue.error);
    expectRecoveredState(result->snapshot(), *capture(*session));
    EXPECT_EQ(result->snapshot().accepted.builds[0].parts[0].id, id(102));
}
TEST(GameSessionRecoveryReplay, RejectsLateForgedDeltasAtomicallyAgainstPriorHistoryAndEconomics) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    commit(*session, adapter, inverse(*session));
    const auto records = journalAfter(*session, source->coveredThrough); ASSERT_EQ(records.size(), 2);
    for (int fault = 0; fault < 15; ++fault) {
        auto bad = records;
        auto& decision = std::get<RequestDecisionRecord>(bad[1].payload);
        auto& edit = std::get<BuildTransition>(decision.objects);
        if (fault == 0) decision.balanceBefore.salvageMaterial += 1;
        if (fault == 1) decision.balanceAfter.salvageMaterial -= 1;
        if (fault == 2) decision.beforeRevision = SessionRevision{0};
        if (fault == 3) edit.beforePart->placement.translation.x += 1;
        if (fault == 4) edit.beforePart->health -= 1;
        if (fault == 5) decision.history.entry = HistoryEntryId{99};
        if (fault == 6) decision.history.evicted[0] = HistoryEntryId{2}, decision.history.evictedCount = 1;
        if (fault == 7) decision.history.action = HistoryAction::Redo;
        if (fault == 8) decision.history.before = HistoryGeneration{99};
        if (fault == 9) edit.before->revision = TopologyRevision{0};
        if (fault == 10) decision.command.sequence = RequestSequence{1};
        if (fault == 11) decision.allocator.reservedThrough += 64;
        if (fault == 12) decision.allocator.issuedThrough -= 1;
        if (fault == 13) decision.key.caller.sessionToken = id(99);
        if (fault == 14) bad[1].tick = SimulationTick{};
        EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, bad, issue)) << fault;
        EXPECT_TRUE(issue) << fault;
        expectRecoveredState(base->snapshot(), *source);
    }
    EXPECT_NE(SessionRecovery::replay(*base, source->journalIdentity, records, issue), nullptr);
}
TEST(GameSessionRecoveryReplay, RejectsWrongWriterHolesOverlapAndOversizedBatchesBeforePublication) {
    FakeAdapter adapter; auto session = create(adapter);
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    commit(*session, adapter, add()); const auto records = journalAfter(*session);
    for (int fault = 0; fault < 3; ++fault) {
        auto identity = source->journalIdentity;
        if (fault == 0) identity.world.bytes[0] ^= 1;
        if (fault == 1) identity.epoch = AuthorityEpoch{2};
        if (fault == 2) identity.writerGeneration = JournalWriterGeneration{2};
        EXPECT_FALSE(SessionRecovery::replay(*base, identity, records, issue));
        EXPECT_EQ(issue.error, RecoveryError::IdentityMismatch);
    }
    EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, std::span(records).subspan(1), issue));
    EXPECT_EQ(issue.error, RecoveryError::InvalidCoverage);
    std::array<JournalRecord, 65> oversized{};
    EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, oversized, issue));
    EXPECT_EQ(issue.error, RecoveryError::Capacity);
    auto alias = records;
    std::get<RequestAdmissionRecord>(alias[1].payload).assignedObject = id(20);
    EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, alias, issue));
    EXPECT_EQ(issue.record, 1);
    expectRecoveredState(base->snapshot(), *source);
}
TEST(GameSessionRecoveryReplay, HistoryEvictionRedoDeletionAndTransportCompactionStayBounded) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.historyEntries = 3;
    boot.limits.dormantParts = 3; boot.limits.receipts = 2; boot.limits.journalRecords = 7;
    auto session = create(adapter, boot);
    const auto source = capture(*session); RecoveryIssue issue; auto recovered = admit(*source, issue);
    const auto replayAndDrain = [&] {
        const auto records = journalAfter(*session, recovered->snapshot().coveredThrough);
        auto next = SessionRecovery::replay(*recovered, source->journalIdentity, records, issue);
        if (!next) throw std::runtime_error("replay failed: " + std::to_string(static_cast<int>(issue.error)));
        const auto current = capture(*session); expectRecoveredState(next->snapshot(), *current);
        const auto retained = journalAfter(*session);
        EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session, retained), JournalError::None);
        recovered = std::move(next);
    };
    for (uint64_t i = 0; i < 10; ++i) {
        commit(*session, adapter, add(i + 1, i, i, {static_cast<int32_t>(i * 300), 0, 0}));
        replayAndDrain();
    }
    for (int i = 0; i < 3; ++i) { commit(*session, adapter, inverse(*session)); replayAndDrain(); }
    commit(*session, adapter, add(14, 13, 13, {4000, 0, 0})); replayAndDrain();
    EXPECT_EQ(recovered->snapshot().history.count, 1);
    EXPECT_EQ(recovered->snapshot().history.partCount, 0);
    EXPECT_EQ(recovered->snapshot().receiptCount, 2);
    EXPECT_EQ(recovered->snapshot().retainedJournalCount, 7);
    EXPECT_GT(recovered->snapshot().modelReleasedThrough.value(), 0);
}
TEST(GameSessionRecoveryReplay, ClosureCancelsUnresolvedWorkAndRetiresHistoryWithoutPayout) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    commit(*session, adapter, inverse(*session));
    adapter.ready = false;
    static_cast<void>(session->submit(bootstrap().caller, add(3, 2, 2)));
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    session->closeAdmission(); const auto records = journalAfter(*session, source->coveredThrough);
    const auto result = SessionRecovery::replay(*base, source->journalIdentity, records, issue);
    ASSERT_NE(result, nullptr) << static_cast<int>(issue.error);
    expectRecoveredState(result->snapshot(), *capture(*session));
    EXPECT_EQ(result->snapshot().accepted.inventory, source->accepted.inventory);
    EXPECT_EQ(result->snapshot().history.partCount, 0);
    EXPECT_FALSE(result->snapshot().admission.open);
    ASSERT_EQ(records.size(), 2);
    std::array skipped{records.at(1)}; skipped[0].sequence = records.at(0).sequence;
    EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, skipped, issue));
}

TEST(GameSessionRecoveryReplay, PreservesDamagedLoanImageAndExactEntitlementAcrossEverySplit) {
    FakeAdapter adapter; auto boot = bootstrap();
    auto loan = seededPart(10); loan.provenance = {PartOrigin::StarterLoan, id(20)};
    loan.health = 3141; loan.paint[0] = 37;
    boot.starterEntitlements = {id(20)}; boot.builds[0].parts = {loan};
    auto session = create(adapter, boot); const auto source = capture(*session);
    RecoveryIssue issue; const auto base = admit(*source, issue);
    commit(*session, adapter, removeCurrent(*session, loan.id));
    commit(*session, adapter, inverse(*session));
    commit(*session, adapter, inverse(*session, false));
    const auto records = journalAfter(*session); const auto expected = capture(*session);
    for (size_t split = 0; split <= records.size(); ++split) {
        const auto prefix = SessionRecovery::replay(*base, source->journalIdentity, std::span(records).first(split), issue);
        ASSERT_NE(prefix, nullptr) << split << ": " << static_cast<int>(issue.error);
        const auto result = SessionRecovery::replay(*prefix, source->journalIdentity, std::span(records).subspan(split), issue);
        ASSERT_NE(result, nullptr) << split << ": " << static_cast<int>(issue.error);
        expectRecoveredState(result->snapshot(), *expected);
        EXPECT_EQ(result->snapshot().history.parts[0], loan);
        EXPECT_EQ(result->snapshot().accepted.inventory, boot.inventory);
    }
}
TEST(GameSessionRecoveryReplay, ExhaustedJournalFrontierNeverWrapsToZero) {
    FakeAdapter adapter; auto session = create(adapter); auto source = capture(*session);
    // A trusted compacted checkpoint can have arbitrarily old covered records.
    const auto maximum = std::numeric_limits<uint64_t>::max();
    source->coveredThrough = source->modelReleasedThrough = JournalFrontier{maximum - 1};
    RecoveryIssue issue; const auto base = admit(*source, issue); ASSERT_NE(base, nullptr);
    static_cast<void>(session->submit(bootstrap().caller,
        {AuthorityEpoch{1}, RequestSequence{1}, {}, AcceptJob{id(99)}}));
    auto records = journalAfter(*session); ASSERT_EQ(records.size(), 2);
    records[0].sequence = JournalSequence{maximum};
    const auto result = SessionRecovery::replay(*base, source->journalIdentity, std::span(records).first(1), issue);
    ASSERT_NE(result, nullptr) << static_cast<int>(issue.error);
    EXPECT_EQ(result->snapshot().coveredThrough.value(), maximum);
    records[1].sequence = {};
    EXPECT_FALSE(SessionRecovery::replay(*result, source->journalIdentity, std::span(records).subspan(1), issue));
    EXPECT_EQ(issue.error, RecoveryError::InvalidCoverage);
    EXPECT_EQ(result->snapshot().pendingCount, 1);
}

TEST(GameSessionRecoveryReplay, GlobalActivePartLimitAppliesAcrossBuildsBeforeMaterialization) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.totalParts = 2;
    auto occupied = boot.builds[0]; occupied.id = id(9);
    auto part = seededPart(10); part.owningBuild = occupied.id; occupied.parts = {part};
    boot.builds.push_back(occupied);
    auto session = create(adapter, boot); auto source = capture(*session);
    // Same valid accepted world, but a tighter trusted runtime capacity.
    source->accepted.limits.totalParts = 1;
    RecoveryIssue issue; const auto base = admit(*source, issue); ASSERT_NE(base, nullptr);
    commit(*session, adapter, add()); const auto records = journalAfter(*session);
    EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, records, issue));
    EXPECT_TRUE(issue); EXPECT_EQ(issue.record, records.size() - 1);
    EXPECT_TRUE(base->snapshot().accepted.builds[0].parts.empty());
    EXPECT_EQ(base->snapshot().accepted.builds[1].parts[0], part);
    EXPECT_EQ(base->snapshot().accepted.inventory, boot.inventory);
}

Receipt commitAs(GameSession& session, FakeAdapter& adapter, CallerContext caller, Command command) {
    const auto submitted = session.submit(caller, command);
    EXPECT_EQ(submitted.state, ReceiptState::PendingPreparation) << static_cast<int>(submitted.issue.error);
    EXPECT_TRUE(session.advanceOneTick());
    const auto result = session.receipt(caller, command.sequence);
    EXPECT_EQ(result.state, ReceiptState::Committed) << static_cast<int>(result.issue.error);
    adapter.completeRetirement();
    return result;
}
TEST(GameSessionRestore, FreshAuthorityPreservesEconomicsSkipsReservedIdsAndRejectsOldRequests) {
    FakeAdapter oldAdapter; auto session = create(oldAdapter);
    const auto bought = commit(*session, oldAdapter, add());
    commit(*session, oldAdapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1}, AcceptJob{id(5)}});
    const auto source = capture(*session); RecoveryIssue issue; auto input = admit(*source, issue);
    session.reset(); FakeAdapter adapter;
    auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue);
    ASSERT_NE(recovered, nullptr) << static_cast<int>(issue.error); EXPECT_FALSE(issue); EXPECT_FALSE(input);
    auto& game = *recovered->session; const auto& initial = recovered->initial->snapshot();
    expectRecoveredState(*capture(game), initial);
    EXPECT_EQ(initial.accepted.inventory, source->accepted.inventory);
    EXPECT_EQ(initial.accepted.jobs, source->accepted.jobs); EXPECT_EQ(initial.accepted.cargo, source->accepted.cargo);
    EXPECT_EQ(initial.accepted.builds[0].parts, source->accepted.builds[0].parts);
    EXPECT_EQ(initial.accepted.epoch, AuthorityEpoch{2});
    EXPECT_EQ(initial.accepted.caller.sessionToken, id(165));
    EXPECT_EQ(initial.allocator, (AllocatorMarkers{165, 228}));
    EXPECT_EQ(initial.admission, (AdmissionState{AdmissionGeneration{2}, AdmissionFrontier{1}, {}, {}, true}));
    EXPECT_EQ(initial.journalIdentity.writerGeneration, JournalWriterGeneration{2});
    EXPECT_EQ(game.history().entries, 0); EXPECT_EQ(game.history().generation.value(), source->history.generation.value() + 1);
    EXPECT_EQ(adapter.begins, 0); EXPECT_EQ(adapter.activations, 0); EXPECT_EQ(adapter.discards, 0);
    const auto old = source->accepted.caller, fresh = initial.accepted.caller;
    EXPECT_EQ(game.submit(old, add()).issue.error, SessionError::WrongToken);
    EXPECT_EQ(game.receipt(old, RequestSequence{1}).issue.error, SessionError::WrongToken);
    EXPECT_EQ(game.cancel(old, RequestSequence{1}).issue.error, SessionError::WrongToken);
    EXPECT_EQ(game.submit(fresh, add()).issue.error, SessionError::WrongEpoch);
    EXPECT_EQ(game.submit({fresh.participant, id(3)}, add()).issue.error, SessionError::WrongToken);
    EXPECT_EQ(game.processedSequence(), 0);
    const auto result = commitAs(game, adapter, fresh,
        {AuthorityEpoch{2}, RequestSequence{1}, SessionRevision{2},
            AddPart{{id(3), TopologyRevision{1}}, starterPartKey(StarterPart::Plate), {{1000, 0, 0}, {}}}});
    EXPECT_EQ(result.object, id(166)); EXPECT_NE(result.object, bought.object);
    EXPECT_EQ(game.admissionState().generation, AdmissionGeneration{2});
    const auto after = capture(game);
    EXPECT_EQ(after->history.entries[0].admission, AdmissionGeneration{2});
    const auto records = journalAfter(game, initial.coveredThrough);
    const auto replayed = SessionRecovery::replay(*recovered->initial, initial.journalIdentity, records, issue);
    ASSERT_NE(replayed, nullptr) << static_cast<int>(issue.error);
    expectRecoveredState(replayed->snapshot(), *after);
    EXPECT_EQ(recovered->retired->snapshot().receipts[0].outcome.object, bought.object);
    EXPECT_EQ(recovered->retired->snapshot().admission.processedThrough, RequestFrontier{2});
    EXPECT_FALSE(SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue));
    EXPECT_EQ(issue.error, RecoveryError::MissingCheckpoint);
}
TEST(GameSessionRestore, FinalizesTwoPendingSlotsInOrderAndClearsEscrowWithoutPayout) {
    FakeAdapter oldAdapter; auto session = create(oldAdapter); commit(*session, oldAdapter, add());
    commit(*session, oldAdapter, inverse(*session));
    oldAdapter.ready = false;
    static_cast<void>(session->submit(bootstrap().caller, add(3, 2, 2)));
    static_cast<void>(session->submit(bootstrap().caller, add(4, 2, 2, {1000, 0, 0})));
    static_cast<void>(session->cancel(bootstrap().caller, RequestSequence{4}));
    const auto source = capture(*session); RecoveryIssue issue; auto input = admit(*source, issue);
    session.reset(); FakeAdapter adapter;
    const auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue);
    ASSERT_NE(recovered, nullptr) << static_cast<int>(issue.error);
    ASSERT_EQ(recovered->retirementRecordCount, 3);
    for (size_t i = 0; i < 2; ++i) {
        const auto& record = recovered->retirementRecords[i];
        const auto& decision = std::get<RequestDecisionRecord>(record.payload);
        EXPECT_EQ(decision.key.sequence.value(), i + 3);
        EXPECT_EQ(decision.outcome.error, SessionError::Canceled);
        EXPECT_EQ(decision.beforeRevision, decision.afterRevision);
        EXPECT_EQ(decision.balanceBefore, decision.balanceAfter);
        EXPECT_EQ(record.epoch, AuthorityEpoch{1});
    }
    EXPECT_TRUE(std::holds_alternative<AdmissionClosedRecord>(recovered->retirementRecords[2].payload));
    const auto& retired = recovered->retired->snapshot();
    EXPECT_FALSE(retired.admission.open); EXPECT_EQ(retired.pendingCount, 0);
    EXPECT_EQ(retired.admission.processedThrough, RequestFrontier{4});
    EXPECT_EQ(retired.history.partCount, 0); EXPECT_EQ(retired.history.count, 0);
    EXPECT_EQ(retired.accepted.inventory, source->accepted.inventory);
    EXPECT_EQ(recovered->session->snapshot().inventory, source->accepted.inventory);
    EXPECT_EQ(recovered->session->lastIssuedId(), source->allocator.reservedThrough + 1);
    EXPECT_EQ(recovered->session->retainedReceiptCount(), 0);
    EXPECT_EQ(adapter.begins, 0); EXPECT_EQ(adapter.activations, 0);
}
TEST(GameSessionRestore, KnownRejectionKeepsItsReasonDuringRecoveryClosure) {
    FakeAdapter oldAdapter; oldAdapter.ready = false; auto session = create(oldAdapter);
    static_cast<void>(session->submit(bootstrap().caller, add()));
    static_cast<void>(session->submit(bootstrap().caller, add(2, 99)));
    const auto source = capture(*session); ASSERT_EQ(source->pending[1].state, RecoveryPendingState::RejectReady);
    RecoveryIssue issue; auto input = admit(*source, issue); session.reset(); FakeAdapter adapter;
    const auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue);
    ASSERT_NE(recovered, nullptr) << static_cast<int>(issue.error);
    const auto& retired = recovered->retired->snapshot();
    EXPECT_EQ(retired.receipts[0].outcome.error, SessionError::Canceled);
    EXPECT_EQ(retired.receipts[1].outcome.error, SessionError::StaleRevision);
    EXPECT_EQ(retired.accepted.inventory, source->accepted.inventory);
}
TEST(GameSessionRestore, EveryRetirementCrashCutConvergesToTheSameNewOpening) {
    FakeAdapter oldAdapter; oldAdapter.ready = false; auto session = create(oldAdapter);
    static_cast<void>(session->submit(bootstrap().caller, add()));
    static_cast<void>(session->submit(bootstrap().caller, add(2, 0, 0, {1000, 0, 0})));
    const auto source = capture(*session); RecoveryIssue issue; auto base = admit(*source, issue); session.reset();
    auto input = SessionRecovery::replay(*base, source->journalIdentity, {}, issue); FakeAdapter referenceAdapter;
    const auto reference = SessionRecovery::restore(input, fixtureIncarnation(), referenceAdapter, issue);
    ASSERT_NE(reference, nullptr) << static_cast<int>(issue.error);
    reference->session.reset();
    for (size_t split = 0; split <= reference->retirementRecordCount; ++split) {
        auto cut = SessionRecovery::replay(*base, source->journalIdentity,
            std::span(reference->retirementRecords).first(split), issue);
        ASSERT_NE(cut, nullptr) << split;
        FakeAdapter adapter;
        const auto result = SessionRecovery::restore(cut, fixtureIncarnation(), adapter, issue);
        ASSERT_NE(result, nullptr) << split << ": " << static_cast<int>(issue.error);
        EXPECT_FALSE(cut); expectRecoveredState(result->initial->snapshot(), reference->initial->snapshot());
        EXPECT_EQ(result->initial->snapshot().retainedJournal[0], reference->initial->snapshot().retainedJournal[0]);
        expectRecoveredState(result->retired->snapshot(), reference->retired->snapshot());
        EXPECT_EQ(result->retirementRecordCount, reference->retirementRecordCount - split);
        EXPECT_EQ(adapter.begins, 0);
    }
}
TEST(GameSessionRestore, RepeatedRecoveryAdvancesEpochTokenAdmissionAndWriterWithoutCreatingValue) {
    auto adapter = std::make_unique<FakeAdapter>(); auto session = create(*adapter);
    const auto inventory = session->snapshot().inventory;
    uint64_t reserved = session->allocatorMarkers().reservedThrough;
    for (uint64_t epoch = 2; epoch <= 7; ++epoch) {
        const auto source = capture(*session); RecoveryIssue issue; auto input = admit(*source, issue);
        session.reset(); adapter = std::make_unique<FakeAdapter>();
        auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), *adapter, issue);
        ASSERT_NE(recovered, nullptr) << epoch << ": " << static_cast<int>(issue.error);
        const auto& initial = recovered->initial->snapshot();
        EXPECT_EQ(initial.accepted.epoch.value(), epoch);
        EXPECT_EQ(initial.admission.generation.value(), epoch);
        EXPECT_EQ(initial.admission.retiredThrough.value(), epoch - 1);
        EXPECT_EQ(initial.journalIdentity.writerGeneration.value(), epoch);
        EXPECT_EQ(initial.accepted.caller.sessionToken.counter, reserved + 1);
        EXPECT_EQ(initial.allocator.reservedThrough, reserved + 64);
        EXPECT_EQ(initial.accepted.inventory, inventory);
        EXPECT_EQ(initial.admission.admittedThrough.value(), 0);
        ASSERT_TRUE(initial.origin); EXPECT_EQ(initial.origin->parent, source->journalIdentity);
        EXPECT_EQ(initial.origin->closedThrough, recovered->retired->snapshot().coveredThrough);
        reserved = initial.allocator.reservedThrough;
        session = std::move(recovered->session);
        expectRecoveredState(*capture(*session), initial);
    }
}
TEST(GameSessionRestore, ReleasesOldLocalEditLeasesAndRejectsOldEpochCompletionWithMatchingGeneration) {
    FakeAdapter oldAdapter; auto boot = bootstrap();
    boot.builds[0].editLease = EditLease{boot.caller.participant, AuthorityEpoch{1}, SimulationTick{100}};
    auto session = create(oldAdapter, boot); const auto source = capture(*session);
    RecoveryIssue issue; auto input = admit(*source, issue); session.reset(); FakeAdapter adapter; adapter.ready = false;
    auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue);
    ASSERT_NE(recovered, nullptr) << static_cast<int>(issue.error);
    const auto& initial = recovered->initial->snapshot(); auto& game = *recovered->session;
    EXPECT_FALSE(initial.accepted.builds[0].editLease);
    EXPECT_EQ(initial.accepted.builds[0].revision, source->accepted.builds[0].revision);
    EXPECT_EQ(recovered->retired->snapshot().accepted.builds[0].editLease, boot.builds[0].editLease);
    const auto caller = initial.accepted.caller;
    auto command = add(); command.epoch = AuthorityEpoch{2};
    ASSERT_EQ(game.submit(caller, command).state, ReceiptState::PendingPreparation);
    adapter.forcedPoll = PreparationTicket{AuthorityEpoch{1}, 1};
    ASSERT_TRUE(game.advanceOneTick());
    EXPECT_EQ(game.processedSequence(), 0); EXPECT_EQ(adapter.activations, 0);
    EXPECT_EQ(game.snapshot().inventory, boot.inventory);
    adapter.forcedPoll.reset(); adapter.ready = true;
    ASSERT_TRUE(game.advanceOneTick());
    EXPECT_EQ(game.receipt(caller, RequestSequence{1}).state, ReceiptState::Committed);
    EXPECT_EQ(adapter.activations, 1);
}
TEST(GameSessionRestore, RejectsForgedOriginOpeningAndPolicyIncludingAfterJournalCompaction) {
    FakeAdapter oldAdapter; auto session = create(oldAdapter); auto source = capture(*session);
    RecoveryIssue issue; auto input = admit(*source, issue); session.reset(); FakeAdapter adapter;
    auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue); ASSERT_NE(recovered, nullptr);
    for (int fault = 0; fault < 16; ++fault) {
        auto bad = recovered->initial->snapshot();
        auto& opening = std::get<RecoveryOpenedRecord>(bad.retainedJournal[0].payload);
        if (fault == 0) bad.origin.reset();
        if (fault == 1) bad.origin->parent.epoch = bad.accepted.epoch;
        if (fault == 2) bad.origin->parent.writerGeneration = bad.journalIdentity.writerGeneration;
        if (fault == 3) bad.origin->closedThrough = {};
        if (fault == 4) bad.origin->caller.participant = id(77);
        if (fault == 5) bad.admission.retiredThrough = {};
        if (fault == 6) opening.admission.caller.sessionToken = id(77);
        if (fault == 7) opening.admission.allocator.reservedThrough += 1;
        if (fault == 8) opening.balance.salvageMaterial += 1;
        if (fault == 9) opening.revision = SessionRevision{99};
        if (fault == 10) opening.leases = static_cast<RecoveryLeasePolicy>(1);
        if (fault == 11) bad.origin->historyGeneration = HistoryGeneration{99};
        if (fault == 12) bad.origin->allocator.reservedThrough -= 1;
        if (fault == 13) bad.origin->generation = AdmissionGeneration{99};
        if (fault == 14) {
            bad.origin->caller.sessionToken = id(3);
            opening.origin = *bad.origin;
        }
        if (fault == 15) bad.accepted.builds[0].editLease =
            EditLease{bad.accepted.caller.participant, AuthorityEpoch{1}, SimulationTick{100}};
        EXPECT_FALSE(admit(bad, issue)) << fault; EXPECT_TRUE(issue) << fault;
    }
    auto compacted = recovered->initial->snapshot();
    compacted.modelReleasedThrough = compacted.coveredThrough;
    compacted.retainedJournalCount = 0; compacted.retainedJournal = {};
    EXPECT_NE(admit(compacted, issue), nullptr);
    compacted.origin->parent.writerGeneration = JournalWriterGeneration{99};
    EXPECT_FALSE(admit(compacted, issue));
}
TEST(GameSessionRestore, CounterExhaustionAndInsufficientControlCapacityPreserveInput) {
    FakeAdapter oldAdapter; auto session = create(oldAdapter); auto source = capture(*session);
    RecoveryIssue issue; auto input = admit(*source, issue); session.reset(); FakeAdapter adapter;
    auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue); ASSERT_NE(recovered, nullptr);
    recovered->session.reset();
    const auto maximum = std::numeric_limits<uint64_t>::max();
    for (int fault = 0; fault < 5; ++fault) {
        auto image = recovered->initial->snapshot();
        image.retainedJournal = {}; image.retainedJournalCount = 0; image.modelReleasedThrough = image.coveredThrough;
        if (fault == 0) {
            image.accepted.epoch = image.journalIdentity.epoch = AuthorityEpoch{maximum};
            image.origin->parent.epoch = AuthorityEpoch{maximum - 1};
        }
        if (fault == 1) {
            image.admission.generation = AdmissionGeneration{maximum};
            image.admission.retiredThrough = AdmissionFrontier{maximum - 1};
            image.origin->generation = AdmissionGeneration{maximum - 1};
        }
        if (fault == 2) {
            image.journalIdentity.writerGeneration = JournalWriterGeneration{maximum};
            image.origin->parent.writerGeneration = JournalWriterGeneration{maximum - 1};
        }
        if (fault == 3) image.allocator.reservedThrough = maximum;
        if (fault == 4) image.coveredThrough = image.modelReleasedThrough = JournalFrontier{maximum};
        auto exhausted = admit(image, issue); ASSERT_NE(exhausted, nullptr) << fault << ": " << static_cast<int>(issue.error);
        const auto* same = exhausted.get(); FakeAdapter fresh;
        EXPECT_FALSE(SessionRecovery::restore(exhausted, fixtureIncarnation(), fresh, issue)) << fault;
        EXPECT_EQ(issue.error, RecoveryError::CounterExhausted) << fault;
        EXPECT_EQ(exhausted.get(), same); EXPECT_EQ(fresh.begins, 0);
    }
    auto tight = *source; tight.accepted.limits.journalBytes = kJournalRecordStorageBytes;
    auto retryable = admit(tight, issue); ASSERT_NE(retryable, nullptr); FakeAdapter fresh;
    EXPECT_FALSE(SessionRecovery::restore(retryable, fixtureIncarnation(), fresh, issue));
    EXPECT_EQ(issue.error, RecoveryError::Capacity); ASSERT_NE(retryable, nullptr);
    expectRecoveredState(retryable->snapshot(), tight);
    EXPECT_EQ(fresh.begins, 0); EXPECT_EQ(fresh.activations, 0);
}
TEST(GameSessionRestore, ExhaustedHistoryDoesNotWrapOrRestoreAnOldUndoChoice) {
    FakeAdapter oldAdapter; auto session = create(oldAdapter); auto source = capture(*session);
    source->history.generation = HistoryGeneration{std::numeric_limits<uint64_t>::max()};
    RecoveryIssue issue; auto input = admit(*source, issue); session.reset(); FakeAdapter adapter;
    auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), adapter, issue); ASSERT_NE(recovered, nullptr);
    const auto& initial = recovered->initial->snapshot(); auto& game = *recovered->session;
    EXPECT_EQ(game.history().generation, source->history.generation); EXPECT_FALSE(game.history().undo);
    auto command = add(); command.epoch = initial.accepted.epoch;
    EXPECT_EQ(game.submit(initial.accepted.caller, command).issue.error, SessionError::HistoryExhausted);
    EXPECT_EQ(game.snapshot().inventory, source->accepted.inventory);
    EXPECT_TRUE(game.snapshot().builds[0].parts.empty()); EXPECT_EQ(adapter.activations, 0);
}

SessionBootstrap loanBootstrap() {
    auto boot = bootstrap();
    auto loan = seededPart(10); loan.provenance = {PartOrigin::StarterLoan, id(20)};
    loan.health = 2718; loan.paint[0] = 37;
    boot.starterEntitlements = {id(20)};
    auto paid = seededPart(11, {1000, 0, 0}); paid.health = 4321; paid.paint[2] = 19;
    boot.builds[0].parts = {loan, paid};
    return boot;
}
TEST(GameSessionEntitlement, ActiveLoansMustUseNormalRemovalAndFailedPreflightPreservesPreparation) {
    FakeAdapter adapter; adapter.ready = false; auto boot = loanBootstrap(); auto session = create(adapter, boot);
    static_cast<void>(session->submit(boot.caller, add(1, 0, 0, {2000, 0, 0})));
    const auto before = capture(*session); const auto discards = adapter.discards;
    EXPECT_EQ(session->retireStarterEntitlement(id(20), {}).error, EntitlementRetirementError::ActiveLoans);
    EXPECT_EQ(session->retireStarterEntitlement(id(20), SessionRevision{99}).error, EntitlementRetirementError::StaleRevision);
    EXPECT_EQ(session->retireStarterEntitlement(id(99), {}).error, EntitlementRetirementError::Inactive);
    EXPECT_EQ(session->retireStarterEntitlement(id(999), {}).error, EntitlementRetirementError::InvalidIdentity);
    EXPECT_EQ(session->retireStarterEntitlement({}, {}).error, EntitlementRetirementError::InvalidIdentity);
    auto foreign = id(20); foreign.world.bytes[0] ^= 1;
    EXPECT_EQ(session->retireStarterEntitlement(foreign, {}).error, EntitlementRetirementError::InvalidIdentity);
    expectRecoveredState(*capture(*session), *before); EXPECT_EQ(adapter.discards, discards);
    EXPECT_EQ(session->pendingCount(), 1);
    static_cast<void>(session->cancel(boot.caller, RequestSequence{1}));
    adapter.ready = true; commit(*session, adapter, removeCurrent(*session, id(10)));
    const auto paid = session->snapshot().builds[0].parts;
    const auto result = session->retireStarterEntitlement(id(20), session->snapshot().revision);
    EXPECT_EQ(result.error, EntitlementRetirementError::None); ASSERT_TRUE(result.journal);
    EXPECT_EQ(session->snapshot().builds[0].parts, paid);
    EXPECT_EQ(session->snapshot().inventory, boot.inventory);
    EXPECT_EQ(adapter.activations, 1); // Only normal loan removal activates geometry.
}
TEST(GameSessionEntitlement, SelectiveInvalidationPreservesPaidEscrowAndExactCompensation) {
    FakeAdapter adapter; auto boot = loanBootstrap(); auto session = create(adapter, boot);
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, {},
        MovePart{{id(3), {}}, id(10), {{-1000, 0, 0}, {}}}});
    commit(*session, adapter, removeCurrent(*session, id(10)));
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{3}, SessionRevision{2},
        MovePart{{id(3), TopologyRevision{2}}, id(11), {{2000, 0, 0}, {}}}});
    const auto paid = session->snapshot().builds[0].parts[0];
    commit(*session, adapter, removeCurrent(*session, id(11)));
    const auto before = capture(*session); ASSERT_EQ(before->history.partCount, 2);
    const auto result = session->retireStarterEntitlement(id(20), SessionRevision{4});
    ASSERT_EQ(result.error, EntitlementRetirementError::None);
    const auto after = capture(*session);
    EXPECT_TRUE(after->accepted.starterEntitlements.empty());
    EXPECT_EQ(after->history.count, 2); EXPECT_EQ(after->history.applied, 2);
    ASSERT_EQ(after->history.partCount, 1); EXPECT_EQ(after->history.parts[0], paid);
    EXPECT_EQ(after->accepted.inventory, before->accepted.inventory);
    EXPECT_EQ(after->accepted.builds[0].revision, before->accepted.builds[0].revision);
    EXPECT_EQ(after->admission, before->admission); EXPECT_EQ(after->allocator, before->allocator);
    const auto records = journalAfter(*session, before->coveredThrough); ASSERT_EQ(records.size(), 1);
    const auto& retired = std::get<EntitlementRetiredRecord>(records[0].payload);
    EXPECT_EQ(retired.history.entryCount, 2); EXPECT_EQ(retired.history.partCount, 1);
    EXPECT_EQ(retired.history.retiredParts[0], id(10)); EXPECT_EQ(retired.history.buildCount, 0);
    commit(*session, adapter, inverse(*session));
    EXPECT_EQ(session->snapshot().builds[0].parts[0], paid);
    EXPECT_EQ(session->snapshot().inventory, boot.inventory); // Exact paid yield is charged once.
    commit(*session, adapter, inverse(*session));
    EXPECT_EQ(session->snapshot().builds[0].parts[0], boot.builds[0].parts[1]);
    EXPECT_FALSE(session->history().undo);
}
TEST(GameSessionEntitlement, RetainsUnrelatedRedoAcrossRevocationAndMakesRetriesHarmless) {
    FakeAdapter adapter; auto session = create(adapter, loanBootstrap());
    commit(*session, adapter, removeCurrent(*session, id(10)));
    commit(*session, adapter, removeCurrent(*session, id(11)));
    commit(*session, adapter, inverse(*session));
    const auto redo = session->history().redo; ASSERT_TRUE(redo);
    const auto result = session->retireStarterEntitlement(id(20), SessionRevision{3});
    ASSERT_EQ(result.error, EntitlementRetirementError::None);
    EXPECT_EQ(session->history().redo, redo);
    const auto after = capture(*session);
    const auto retry = session->retireStarterEntitlement(id(20), SessionRevision{3});
    EXPECT_EQ(retry.error, EntitlementRetirementError::Inactive); EXPECT_FALSE(retry.journal);
    expectRecoveredState(*capture(*session), *after);
    commit(*session, adapter, inverse(*session, false));
    EXPECT_TRUE(session->snapshot().builds[0].parts.empty());
    EXPECT_EQ(session->history().dormantParts, 1); // Paid object only.
    session->closeAdmission();
    EXPECT_EQ(session->retireStarterEntitlement(id(20), session->snapshot().revision).error, EntitlementRetirementError::Closed);
}
TEST(GameSessionEntitlement, InvalidatesPendingInverseAndPaidCandidateWithOrderedReservedDecisions) {
    FakeAdapter adapter; auto boot = loanBootstrap(); auto session = create(adapter, boot);
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{1}, {},
        MovePart{{id(3), {}}, id(11), {{2000, 0, 0}, {}}}});
    commit(*session, adapter, removeCurrent(*session, id(10)));
    adapter.ready = false; adapter.deferDiscard = true;
    const auto undo = inverse(*session);
    ASSERT_EQ(session->submit(boot.caller, undo).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(boot.caller, add(4, 2, 2, {3000, 0, 0})).state, ReceiptState::PendingPreparation);
    const auto before = capture(*session); const auto resident = adapter.resident();
    const auto activations = adapter.activations, discards = adapter.discards;
    const auto result = session->retireStarterEntitlement(id(20), SessionRevision{2});
    ASSERT_EQ(result.error, EntitlementRetirementError::None);
    const auto after = capture(*session);
    EXPECT_EQ(session->pendingCount(), 0); EXPECT_EQ(session->processedSequence(), 4);
    EXPECT_EQ(session->receipt(boot.caller, RequestSequence{3}).issue.error, SessionError::HistoryConflict);
    EXPECT_EQ(session->receipt(boot.caller, RequestSequence{4}).issue.error, SessionError::StaleRevision);
    EXPECT_EQ(adapter.activations, activations); EXPECT_EQ(adapter.discards, discards + 2);
    EXPECT_EQ(adapter.resident(), resident); // Logical release is not backend fence completion.
    EXPECT_EQ(after->accepted.inventory, before->accepted.inventory);
    EXPECT_EQ(after->accepted.builds[0].parts, before->accepted.builds[0].parts);
    EXPECT_EQ(after->history.count, 1); EXPECT_EQ(after->history.partCount, 0);
    const auto records = journalAfter(*session, before->coveredThrough); ASSERT_EQ(records.size(), 3);
    RecoveryIssue issue; const auto base = admit(*before, issue); ASSERT_NE(base, nullptr);
    for (size_t split = 0; split <= records.size(); ++split) {
        const auto prefix = SessionRecovery::replay(*base, before->journalIdentity, std::span(records).first(split), issue);
        ASSERT_NE(prefix, nullptr) << split << ": " << static_cast<int>(issue.error);
        const auto restored = SessionRecovery::replay(*prefix, before->journalIdentity, std::span(records).subspan(split), issue);
        ASSERT_NE(restored, nullptr) << split << ": " << static_cast<int>(issue.error);
        expectRecoveredState(restored->snapshot(), *after);
    }
    adapter.ready = true; session->pollPreparation();
    EXPECT_EQ(adapter.activations, activations); EXPECT_EQ(session->snapshot().revision, SessionRevision{3});
    EXPECT_EQ(session->submit(boot.caller, undo).issue.error, SessionError::HistoryConflict);
}
TEST(GameSessionEntitlement, EveryJournalCutAndFreshRecoveryKeepRetiredLoansAbsent) {
    FakeAdapter adapter; auto boot = loanBootstrap(); auto session = create(adapter, boot);
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    commit(*session, adapter, removeCurrent(*session, id(10)));
    const auto oldUndo = inverse(*session);
    ASSERT_EQ(session->retireStarterEntitlement(id(20), SessionRevision{1}).error, EntitlementRetirementError::None);
    const auto expected = capture(*session); const auto records = journalAfter(*session);
    for (size_t split = 0; split <= records.size(); ++split) {
        const auto prefix = SessionRecovery::replay(*base, source->journalIdentity, std::span(records).first(split), issue);
        ASSERT_NE(prefix, nullptr) << split << ": " << static_cast<int>(issue.error);
        const auto result = SessionRecovery::replay(*prefix, source->journalIdentity, std::span(records).subspan(split), issue);
        ASSERT_NE(result, nullptr) << split << ": " << static_cast<int>(issue.error);
        expectRecoveredState(result->snapshot(), *expected);
    }
    auto input = admit(*expected, issue); session.reset(); FakeAdapter fresh;
    const auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), fresh, issue); ASSERT_NE(recovered, nullptr);
    auto stale = oldUndo; stale.epoch = recovered->initial->snapshot().accepted.epoch;
    stale.expectedRevision = recovered->session->snapshot().revision; stale.sequence = RequestSequence{1};
    EXPECT_EQ(recovered->session->submit(recovered->initial->snapshot().accepted.caller, stale).issue.error,
        SessionError::HistoryConflict);
    const auto snapshot = capture(*recovered->session);
    EXPECT_TRUE(snapshot->accepted.starterEntitlements.empty()); EXPECT_EQ(snapshot->history.partCount, 0);
    EXPECT_EQ(snapshot->accepted.builds[0].parts, expected->accepted.builds[0].parts);
    EXPECT_EQ(snapshot->accepted.inventory, boot.inventory);
}
TEST(GameSessionEntitlement, RejectsForgedRetirementListsBalancesMarkersAndReintroducedLoanState) {
    FakeAdapter adapter; auto session = create(adapter, loanBootstrap());
    commit(*session, adapter, removeCurrent(*session, id(10)));
    const auto source = capture(*session); RecoveryIssue issue; const auto base = admit(*source, issue);
    ASSERT_EQ(session->retireStarterEntitlement(id(20), SessionRevision{1}).error, EntitlementRetirementError::None);
    const auto records = journalAfter(*session, source->coveredThrough); ASSERT_EQ(records.size(), 1);
    for (int fault = 0; fault < 13; ++fault) {
        auto bad = records; auto& record = std::get<EntitlementRetiredRecord>(bad[0].payload);
        if (fault == 0) record.history.retiredParts[0] = id(11);
        if (fault == 1) record.history.invalidatedEntries[0] = HistoryEntryId{99};
        if (fault == 2) record.history.entryCount = 0, record.history.invalidatedEntries = {};
        if (fault == 3) record.history.partCount = 0, record.history.retiredParts = {};
        if (fault == 4) record.history.partCount = 65;
        if (fault == 5) record.balance.salvageMaterial += 1;
        if (fault == 6) record.beforeRevision = SessionRevision{};
        if (fault == 7) record.afterRevision = SessionRevision{99};
        if (fault == 8) record.admittedThrough = RequestFrontier{99};
        if (fault == 9) record.caller.sessionToken = id(77);
        if (fault == 10) record.entitlement = id(21);
        if (fault == 11) record.allocator.reservedThrough += 64;
        if (fault == 12) record.history.retiredParts[1] = id(99);
        EXPECT_FALSE(SessionRecovery::replay(*base, source->journalIdentity, bad, issue)) << fault;
        expectRecoveredState(base->snapshot(), *source);
    }
    const auto current = capture(*session);
    auto revived = *current; revived.accepted.starterEntitlements = {id(20)};
    revived.ownedBytes += sizeof(DurableId);
    EXPECT_FALSE(admit(revived, issue));
    revived = *current; revived.history = source->history;
    EXPECT_FALSE(admit(revived, issue));
}
TEST(GameSessionEntitlement, FullOutboxPreservesInverseUntilExactModelPrefixIsReleased) {
    FakeAdapter adapter; auto boot = loanBootstrap(); boot.limits.journalRecords = 4;
    auto session = create(adapter, boot);
    commit(*session, adapter, removeCurrent(*session, id(10))); // 2 retained + 1 reserved closure.
    const auto initial = capture(*session);
    ASSERT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session, journalAfter(*session)), JournalError::None);
    // Admit a delayed inverse: 1 retained admission + 1 terminal + 1 closure.
    adapter.ready = false; static_cast<void>(session->submit(boot.caller, inverse(*session)));
    // A normal system retirement needs its own slot; lowering bytes is not a live API.
    // The fourth slot succeeds and its already-reserved terminal decision drains.
    ASSERT_EQ(session->retireStarterEntitlement(id(20), SessionRevision{1}).error, EntitlementRetirementError::None);
    EXPECT_EQ(session->processedSequence(), 2); EXPECT_EQ(session->snapshot().inventory, initial->accepted.inventory);
    EXPECT_FALSE(session->journalFaulted());

    FakeAdapter otherAdapter; auto tight = loanBootstrap(); tight.limits.journalRecords = 3;
    auto other = create(otherAdapter, tight);
    commit(*other, otherAdapter, removeCurrent(*other, id(10)));
    const auto before = capture(*other);
    EXPECT_EQ(other->retireStarterEntitlement(id(20), SessionRevision{1}).error, EntitlementRetirementError::JournalCapacity);
    expectRecoveredState(*capture(*other), *before); EXPECT_TRUE(other->history().undo);
    ASSERT_EQ(SessionRecovery::releaseModelWrittenPrefix(*other, journalAfter(*other)), JournalError::None);
    EXPECT_EQ(other->retireStarterEntitlement(id(20), SessionRevision{1}).error, EntitlementRetirementError::None);
    EXPECT_EQ(other->snapshot().inventory, tight.inventory);
}
TEST(GameSessionEntitlement, ExhaustedRevisionOrHistoryCannotPartlyRevokeEntitlement) {
    for (const bool revision : {false, true}) {
        FakeAdapter adapter; auto boot = bootstrap(); boot.starterEntitlements = {id(20)};
        if (revision) boot.revision = SessionRevision{std::numeric_limits<uint64_t>::max()};
        auto session = create(adapter, boot);
        if (!revision) {
            auto image = capture(*session); image->history.generation = HistoryGeneration{std::numeric_limits<uint64_t>::max()};
            RecoveryIssue issue; auto input = admit(*image, issue); session.reset();
            // The first adapter had no live or pending resources; a separate lifetime is still supplied.
            FakeAdapter fresh;
            auto recovered = SessionRecovery::restore(input, fixtureIncarnation(), fresh, issue); ASSERT_NE(recovered, nullptr);
            const auto before = capture(*recovered->session);
            EXPECT_EQ(recovered->session->retireStarterEntitlement(id(20), {}).error, EntitlementRetirementError::HistoryExhausted);
            expectRecoveredState(*capture(*recovered->session), *before);
        } else {
            const auto before = capture(*session);
            EXPECT_EQ(session->retireStarterEntitlement(id(20), boot.revision).error, EntitlementRetirementError::RevisionExhausted);
            expectRecoveredState(*capture(*session), *before);
        }
    }
}
TEST(GameSessionEntitlement, TrustedReplacementBootstrapUsesFreshLoanIdsAndNeverRevivesOldUndo) {
    // Validation fixture for the later rescue composition root, not a shipping
    // refill API. Every old loan is normally removed and its right retired before
    // a new trusted bootstrap is admitted. No live snapshot setter is used.
    auto boot = loanBootstrap(); auto adapter = std::make_unique<FakeAdapter>(); auto session = create(*adapter, boot);
    const auto initialInventory = boot.inventory; const auto paid = boot.builds[0].parts[1];
    DurableId entitlement = id(20), loan = id(10);
    for (int cycle = 0; cycle < 3; ++cycle) {
        const auto source = capture(*session); auto caller = source->accepted.caller;
        const auto before = session->snapshot();
        commitAs(*session, *adapter, caller,
            {source->accepted.epoch, RequestSequence{session->processedSequence() + 1}, before.revision,
                RemovePart{{id(3), before.builds[0].revision}, loan}});
        const auto choice = session->history().undo; ASSERT_TRUE(choice);
        const auto oldGeneration = session->history().generation;
        ASSERT_EQ(session->retireStarterEntitlement(entitlement, session->snapshot().revision).error, EntitlementRetirementError::None);
        const auto retired = capture(*session);
        boot = retired->accepted;
        IdAllocator replacements(boot.world, retired->allocator.reservedThrough);
        const auto newToken = replacements.allocate(), newEntitlement = replacements.allocate(), newLoan = replacements.allocate();
        ASSERT_TRUE(newToken && newEntitlement && newLoan);
        EXPECT_GT(newLoan->counter, loan.counter);
        boot.caller.sessionToken = *newToken; boot.epoch = *next(boot.epoch);
        boot.lastIssuedId = replacements.lastIssued(); boot.starterEntitlements = {*newEntitlement};
        auto replacement = seededPart(newLoan->counter);
        replacement.provenance = {PartOrigin::StarterLoan, *newEntitlement};
        boot.builds[0].parts.push_back(replacement);
        boot.builds[0].revision = *next(boot.builds[0].revision); boot.revision = *next(boot.revision);
        session.reset(); adapter = std::make_unique<FakeAdapter>(); session = create(*adapter, boot);
        EXPECT_EQ(session->submit(caller, add()).issue.error, SessionError::WrongToken);
        const auto denied = session->submit(boot.caller,
            {boot.epoch, RequestSequence{1}, boot.revision,
                Undo{{id(3), boot.builds[0].revision}, choice->entry, oldGeneration}});
        EXPECT_EQ(denied.issue.error, SessionError::HistoryConflict);
        EXPECT_EQ(session->snapshot().inventory, initialInventory);
        const auto current = capture(*session); ASSERT_EQ(current->accepted.builds[0].parts.size(), 2);
        EXPECT_EQ(current->accepted.builds[0].parts[0], paid);
        EXPECT_EQ(current->accepted.builds[0].parts[1].provenance.origin, PartOrigin::StarterLoan);
        EXPECT_EQ(current->accepted.builds[0].parts[1].provenance.starterEntitlement, *newEntitlement);
        entitlement = *newEntitlement; loan = *newLoan;
    }
}
std::unique_ptr<EventBaseline> observe(const GameSession& session) {
    SessionIssue issue;
    auto result = session.eventBaseline(issue);
    if (!result || issue) throw std::runtime_error("observation baseline failed");
    EXPECT_EQ(result->stream, session.events().identity());
    EXPECT_FALSE(result->diagnostics.publicationLost);
    for (const auto& lane : result->lanes) EXPECT_EQ(lane.invalidInput.value, 0u);
    return result;
}
std::vector<EventRecord> readEvents(const GameSession& session, EventCursor& cursor) {
    std::array<EventRecord, 256> output{};
    const auto read = session.events().read(cursor, output);
    if (read.status != EventReadStatus::Read) throw std::runtime_error("unexpected observer gap/error");
    std::vector<EventRecord> result(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(read.count));
    cursor = read.next;
    for (const auto& record : result) {
        EXPECT_EQ(validateEvent(record), EventValidation::Valid);
        std::array<std::byte, kMaximumEventWireBytes> bytes{};
        const auto encoded = encodeEvent(record, bytes); EXPECT_EQ(encoded.error, EventCodecError::None);
        const auto decoded = decodeEvent(std::span(bytes).first(encoded.bytes));
        EXPECT_EQ(decoded.record, record);
    }
    return result;
}
void drainModelJournal(GameSession& session) {
    std::array<JournalRecord, kMaximumJournalRecords> records{};
    const auto count = session.journal().copyPrefix(records);
    EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(session, std::span(records).first(count)), JournalError::None);
}

TEST(GameSessionObservation, BootstrapIsAQuietOwnedBaselineWithExplicitPublicIdentity) {
    FakeAdapter adapter; auto boot = loanBootstrap(); auto session = create(adapter, boot);
    const auto before = session->snapshot(); auto baseline = observe(*session);
    EXPECT_EQ(baseline->participant, boot.caller.participant); EXPECT_TRUE(baseline->workshopEnabled);
    EXPECT_EQ(baseline->starterEntitlements, boot.starterEntitlements);
    EXPECT_EQ(baseline->receiptCount, 0u); EXPECT_EQ(baseline->lanes[0].publishedThrough.value(), 0u);
    EXPECT_EQ(baseline->admission, session->admissionState()); EXPECT_EQ(baseline->history, session->history());
    expectWorldEqual(baseline->state, before);
    baseline->state.inventory = {}; baseline->state.builds[0].parts.clear();
    baseline->starterEntitlements.clear(); baseline->admission.open = false;
    expectWorldEqual(session->snapshot(), before); EXPECT_EQ(observe(*session)->starterEntitlements, boot.starterEntitlements);
    EXPECT_FALSE(GameSession::validateInitialState(boot, catalog()));
    SessionIssue issue;
    EXPECT_FALSE(GameSession::create(boot, {}, catalog(), adapter, issue)); EXPECT_EQ(issue.error, SessionError::InvalidIdentity);
    EXPECT_FALSE(GameSession::create(boot, EventStreamIncarnation{boot.world.bytes}, catalog(), adapter, issue));
    EXPECT_EQ(adapter.begins, 0u);
}
TEST(GameSessionObservation, RealAddMoveRemoveAndJobPublishExactChangesAndBalancesOnce) {
    FakeAdapter adapter; auto session = create(adapter); auto cursor = observe(*session)->cursors[0];
    const auto command = add(); const auto pending = session->submit(bootstrap().caller, command);
    ASSERT_EQ(pending.state, ReceiptState::PendingPreparation);
    auto notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 1u);
    const auto& admission = std::get<ReceiptChangedEvent>(notices[0].payload);
    EXPECT_EQ(admission.outcome, EventOutcome::Pending); EXPECT_EQ(admission.journal->sequence, *pending.journal);
    EXPECT_EQ(session->submit(bootstrap().caller, command).state, ReceiptState::PendingPreparation);
    EXPECT_TRUE(readEvents(*session, cursor).empty()); ASSERT_TRUE(session->advanceOneTick()); adapter.completeRetirement();
    const auto accepted = session->receipt(bootstrap().caller, RequestSequence{1}); ASSERT_TRUE(accepted.object);
    notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 2u);
    const auto& state = std::get<StateCommittedEvent>(notices[0].payload);
    EXPECT_EQ(state.balance, session->snapshot().inventory); EXPECT_EQ(state.primary, accepted.object);
    EXPECT_EQ(state.changes, changeBit(EventChange::Builds) | changeBit(EventChange::Inventory) | changeBit(EventChange::History));
    EXPECT_EQ(state.journal->sequence, *accepted.journal);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[1].payload).outcome, EventOutcome::Committed);
    EXPECT_EQ(notices[0].header.revision, SessionRevision{1}); EXPECT_EQ(notices[1].header.build->revision, TopologyRevision{1});
    static_cast<void>(session->submit(bootstrap().caller, command)); static_cast<void>(session->cancel(bootstrap().caller, RequestSequence{1}));
    EXPECT_TRUE(readEvents(*session, cursor).empty());
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1},
        MovePart{{id(3), TopologyRevision{1}}, *accepted.object, {{1000, 0, 0}, {}}}});
    notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 3u);
    EXPECT_EQ(std::get<StateCommittedEvent>(notices[1].payload).changes, changeBit(EventChange::Builds) | changeBit(EventChange::History));
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{3}, SessionRevision{2},
        RemovePart{{id(3), TopologyRevision{2}}, *accepted.object}});
    notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 3u);
    EXPECT_EQ(std::get<StateCommittedEvent>(notices[1].payload).balance, session->snapshot().inventory);
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{4}, SessionRevision{3}, AcceptJob{id(5)}});
    notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 3u);
    EXPECT_EQ(std::get<StateCommittedEvent>(notices[1].payload).changes, changeBit(EventChange::Jobs));
    EXPECT_FALSE(notices[1].header.build); EXPECT_EQ(std::get<StateCommittedEvent>(notices[1].payload).primary, id(5));
    const auto baseline = observe(*session); EXPECT_EQ(baseline->receiptCount, 4u);
    EXPECT_EQ(baseline->diagnostics.ingress[static_cast<size_t>(ObservationIngress::ExactRetry)].value, 2u);
    EXPECT_EQ(baseline->lanes[1].occupancy, 0u); EXPECT_EQ(baseline->lanes[2].occupancy, 0u);
}
TEST(GameSessionObservation, IngressSpamAndOutOfOrderCancellationDoNotInventTransitions) {
    FakeAdapter adapter; adapter.ready = false; auto session = create(adapter);
    auto cursor = observe(*session)->cursors[0]; const auto caller = bootstrap().caller;
    ASSERT_EQ(session->submit(caller, add(1)).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(caller, add(2, 0, 0, {1000, 0, 0})).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(readEvents(*session, cursor).size(), 2u);
    for (int n = 0; n < 100; ++n) {
        EXPECT_EQ(session->submit({id(1), id(999)}, add(3)).issue.error, SessionError::WrongToken);
        EXPECT_EQ(session->submit(caller, add(4)).issue.error, SessionError::SequenceGap);
        EXPECT_EQ(session->submit(caller, add(3)).issue.error, SessionError::Busy);
        EXPECT_EQ(session->submit(caller, add(1, 0, 0, {2000, 0, 0})).issue.error, SessionError::RequestConflict);
        EXPECT_EQ(session->submit(caller, add(1)).state, ReceiptState::PendingPreparation);
    }
    EXPECT_TRUE(readEvents(*session, cursor).empty());
    EXPECT_EQ(session->cancel(caller, RequestSequence{2}).state, ReceiptState::PendingPreparation);
    auto pending = observe(*session); ASSERT_EQ(pending->receiptCount, 2u);
    EXPECT_EQ(pending->receipts[1].receipt.outcome, EventOutcome::Pending);
    EXPECT_TRUE(readEvents(*session, cursor).empty());
    EXPECT_EQ(session->cancel(caller, RequestSequence{1}).issue.error, SessionError::Canceled);
    auto notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        const auto& receipt = std::get<ReceiptChangedEvent>(notices[i].payload);
        EXPECT_EQ(receipt.request.sequence.value(), i + 1); EXPECT_EQ(receipt.outcome, EventOutcome::Rejected);
        EXPECT_EQ(receipt.error, EventSessionCode::Canceled); EXPECT_EQ(notices[i].header.revision.value(), 0u);
    }
    session->closeAdmission(); session->closeAdmission(); notices = readEvents(*session, cursor);
    ASSERT_EQ(notices.size(), 1u); EXPECT_EQ(notices[0].header.kind, EventKind::AdmissionClosed);
    const auto baseline = observe(*session); EXPECT_FALSE(baseline->admission.open); EXPECT_EQ(baseline->admission.processedThrough.value(), 2u);
    for (auto category : {ObservationIngress::Authentication, ObservationIngress::Ordering, ObservationIngress::Capacity,
        ObservationIngress::ConflictingRetry, ObservationIngress::ExactRetry})
        EXPECT_EQ(baseline->diagnostics.ingress[static_cast<size_t>(category)].value, 100u);
    EXPECT_EQ(baseline->state.inventory, bootstrap().inventory); EXPECT_EQ(adapter.activations, 0u);
}
TEST(GameSessionObservation, ImmediateRejectionAndStalePreparationReportOnlyAcceptedFacts) {
    FakeAdapter adapter; auto session = create(adapter); auto cursor = observe(*session)->cursors[0];
    auto bad = add(); std::get<AddPart>(bad.intent).target.build = id(999);
    const auto rejected = session->submit(bootstrap().caller, bad); EXPECT_EQ(rejected.issue.error, SessionError::UnknownBuild);
    auto notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 2u);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[0].payload).outcome, EventOutcome::Pending);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[1].payload).error, EventSessionCode::UnknownBuild);
    ASSERT_EQ(session->submit(bootstrap().caller, add(2)).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(bootstrap().caller, add(3, 0, 0, {1000, 0, 0})).state, ReceiptState::PendingPreparation);
    ASSERT_TRUE(session->advanceOneTick()); notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 5u);
    EXPECT_EQ(notices[2].header.kind, EventKind::StateCommitted);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[4].payload).error, EventSessionCode::StaleRevision);
    EXPECT_EQ(notices[4].header.revision.value(), 1u);
    const auto baseline = observe(*session); EXPECT_EQ(baseline->receiptCount, 3u);
    EXPECT_EQ(baseline->state.builds[0].parts.size(), 1u); EXPECT_EQ(adapter.activations, 1u);
}
TEST(GameSessionObservation, CompensationAndDormantBuildReactivationKeepEventRevisionsMonotonic) {
    FakeAdapter adapter; auto session = create(adapter); auto cursor = observe(*session)->cursors[0];
    const auto added = commit(*session, adapter, add()); ASSERT_TRUE(added.object);
    commit(*session, adapter, inverse(*session)); commit(*session, adapter, inverse(*session, false));
    commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{4}, SessionRevision{3}, RemovePart{{id(3), TopologyRevision{3}}, *added.object}});
    commit(*session, adapter, inverse(*session));
    const auto created = commit(*session, adapter, {AuthorityEpoch{1}, RequestSequence{6}, SessionRevision{5}, CreateBuild{}});
    ASSERT_TRUE(created.object); commit(*session, adapter, inverse(*session));
    EXPECT_EQ(session->snapshot().builds.size(), 1u);
    commit(*session, adapter, inverse(*session, false));
    const auto notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 24u);
    for (size_t i = 0; i < 8; ++i) {
        const auto& state = notices[i * 3 + 1]; const auto& receipt = notices[i * 3 + 2];
        EXPECT_EQ(state.header.kind, EventKind::StateCommitted); EXPECT_EQ(state.header.revision.value(), i + 1);
        EXPECT_EQ(receipt.header.revision, state.header.revision);
        EXPECT_NE(std::get<StateCommittedEvent>(state.payload).changes & changeBit(EventChange::History), 0);
    }
    EXPECT_EQ(notices[19].header.build->build, *created.object);
    EXPECT_EQ(notices[22].header.build->build, *created.object);
    EXPECT_GT(notices[22].header.build->revision, notices[19].header.build->revision);
    EXPECT_EQ(observe(*session)->state.builds.size(), 2u);
}
TEST(GameSessionObservation, EntitlementRetirementPublishesBeforeItsPendingInvalidations) {
    FakeAdapter adapter; auto boot = loanBootstrap(); auto session = create(adapter, boot);
    commit(*session, adapter, {boot.epoch, RequestSequence{1}, {}, RemovePart{{id(3), {}}, id(10)}});
    auto cursor = observe(*session)->cursors[0];
    ASSERT_EQ(session->submit(boot.caller, inverse(*session)).state, ReceiptState::PendingPreparation);
    ASSERT_EQ(session->submit(boot.caller, {boot.epoch, RequestSequence{3}, SessionRevision{1},
        MovePart{{id(3), TopologyRevision{1}}, id(11), {{2000, 0, 0}, {}}}}).state, ReceiptState::PendingPreparation);
    EXPECT_EQ(readEvents(*session, cursor).size(), 2u); const auto balance = session->snapshot().inventory;
    const auto result = session->retireStarterEntitlement(id(20), SessionRevision{1});
    ASSERT_EQ(result.error, EntitlementRetirementError::None);
    const auto notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 3u);
    const auto& state = std::get<StateCommittedEvent>(notices[0].payload);
    EXPECT_FALSE(state.cause); EXPECT_EQ(state.primary, id(20)); EXPECT_EQ(state.balance, balance);
    EXPECT_EQ(state.changes, changeBit(EventChange::History) | changeBit(EventChange::Entitlements));
    EXPECT_EQ(state.journal->sequence, result.journal);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[1].payload).error, EventSessionCode::HistoryConflict);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[2].payload).error, EventSessionCode::StaleRevision);
    for (const auto& notice : notices) EXPECT_EQ(notice.header.revision.value(), 2u);
    EXPECT_EQ(session->retireStarterEntitlement(id(20), SessionRevision{1}).error, EntitlementRetirementError::Inactive);
    EXPECT_TRUE(readEvents(*session, cursor).empty());
    const auto baseline = observe(*session); EXPECT_TRUE(baseline->starterEntitlements.empty());
    ASSERT_EQ(baseline->state.builds[0].parts.size(), 1u); EXPECT_EQ(baseline->state.builds[0].parts[0].id, id(11));
}
TEST(GameSessionObservation, DomainOverrunAndReceiptEvictionRecoverFromOneBaselineWithoutSpendingAgain) {
    FakeAdapter adapter; auto boot = bootstrap(); boot.limits.receipts = 2; boot.builds[0].parts = {seededPart(10)};
    auto session = create(adapter, boot); const auto stalled = observe(*session)->cursors[0];
    for (uint64_t n = 1; n <= 100; ++n) {
        commit(*session, adapter, {boot.epoch, RequestSequence{n}, SessionRevision{n - 1},
            MovePart{{id(3), TopologyRevision{n - 1}}, id(10), {{static_cast<int32_t>(n * 50), 0, 0}, {}}}});
        if (n % 10 == 0) drainModelJournal(*session);
    }
    std::array<EventRecord, 3> output{}; const auto sentinel = output;
    const auto gap = session->events().read(stalled, output);
    EXPECT_EQ(gap.status, EventReadStatus::Gap); EXPECT_EQ(gap.next, stalled); EXPECT_EQ(output, sentinel);
    EXPECT_EQ(gap.stats.overwritten.value, 44u); EXPECT_EQ(gap.stats.oldest.value(), 45u);
    const auto baseline = observe(*session); auto cursor = baseline->cursors[0];
    EXPECT_EQ(cursor.lastConsumed.value(), 300u); EXPECT_EQ(baseline->receiptCount, 2u);
    EXPECT_EQ(baseline->receipts[0].receipt.request.sequence.value(), 99u);
    EXPECT_EQ(baseline->receipts[1].receipt.request.sequence.value(), 100u);
    EXPECT_EQ(baseline->state.inventory, boot.inventory); EXPECT_EQ(baseline->admission.processedThrough.value(), 100u);
    EXPECT_EQ(session->submit(boot.caller, {boot.epoch, RequestSequence{1}, {},
        MovePart{{id(3), {}}, id(10), {{50, 0, 0}, {}}}}).issue.error, SessionError::AlreadyProcessed);
    EXPECT_TRUE(readEvents(*session, cursor).empty());
    commit(*session, adapter, {boot.epoch, RequestSequence{101}, SessionRevision{100},
        MovePart{{id(3), TopologyRevision{100}}, id(10), {{5050, 0, 0}, {}}}});
    const auto tail = readEvents(*session, cursor); ASSERT_EQ(tail.size(), 3u);
    EXPECT_EQ(tail[0].header.sequence.value(), 301u); EXPECT_EQ(tail[2].header.sequence.value(), 303u);
    EXPECT_EQ(std::get<StateCommittedEvent>(tail[1].payload).balance, boot.inventory);
    EXPECT_EQ(baseline->state.builds[0].parts[0].placement.translation.x, 5000);
    EXPECT_EQ(session->snapshot().builds[0].parts[0].placement.translation.x, 5050);
}
TEST(GameSessionObservation, BaselineFailureAndInPlaceStreamRestartPreserveAuthorityAndReaderLifetime) {
    FakeAdapter adapter; auto session = create(adapter); commit(*session, adapter, add());
    auto baseline = observe(*session); const auto before = session->snapshot(); const auto journal = session->journal().size();
    auto reader = session->events(); const auto oldCursor = baseline->cursors[0]; SessionIssue issue;
    EXPECT_FALSE(session->eventBaseline(issue, baseline->ownedBytes - 1)); EXPECT_EQ(issue.error, SessionError::Capacity);
    EXPECT_FALSE(session->eventBaseline(issue, kMaximumEventBaselineBytes + 1));
    EXPECT_TRUE(session->eventBaseline(issue, baseline->ownedBytes));
    EXPECT_EQ(observe(*session)->cursors, baseline->cursors); expectWorldEqual(session->snapshot(), before);
    EXPECT_FALSE(session->restartEventStream(baseline->stream.incarnation, issue));
    EXPECT_FALSE(session->restartEventStream({}, issue));
    const auto next = fixtureIncarnation(); ASSERT_TRUE(session->restartEventStream(next, issue));
    EXPECT_EQ(reader.identity().incarnation, next); EXPECT_EQ(session->journal().size(), journal);
    std::array<EventRecord, 2> output{}; EXPECT_EQ(reader.read(oldCursor, output).status, EventReadStatus::WrongIncarnation);
    auto refreshed = observe(*session); expectWorldEqual(refreshed->state, before);
    EXPECT_EQ(refreshed->cursors[0].lastConsumed.value(), 0u); EXPECT_EQ(refreshed->receiptCount, 1u);
    commit(*session, adapter, inverse(*session)); auto cursor = refreshed->cursors[0];
    const auto notices = readEvents(*session, cursor); ASSERT_EQ(notices.size(), 3u);
    EXPECT_EQ(notices[0].header.sequence.value(), 1u); EXPECT_EQ(notices[0].header.stream.incarnation, next);
    EXPECT_EQ(std::get<ReceiptChangedEvent>(notices[0].payload).request.sequence.value(), 2u);
}
TEST(GameSessionObservation, FreshRecoveryHasAQuietNewStreamAndKeepsFailedInputReusable) {
    FakeAdapter oldAdapter; auto session = create(oldAdapter); commit(*session, oldAdapter, add());
    const auto oldStream = session->events().identity(); const auto oldCursor = observe(*session)->cursors[0];
    session->closeAdmission(); const auto image = capture(*session); RecoveryIssue issue;
    auto input = SessionRecovery::admit(*image, {kWorld, recoveryContent()}, catalog(), issue); ASSERT_NE(input, nullptr);
    const auto* prior = input.get(); FakeAdapter fresh;
    EXPECT_FALSE(SessionRecovery::restore(input, {}, fresh, issue)); EXPECT_EQ(issue.error, RecoveryError::IdentityMismatch);
    EXPECT_EQ(input.get(), prior); const auto incarnation = fixtureIncarnation();
    const auto recovered = SessionRecovery::restore(input, incarnation, fresh, issue); ASSERT_NE(recovered, nullptr);
    EXPECT_FALSE(input); auto baseline = observe(*recovered->session); auto cursor = baseline->cursors[0];
    EXPECT_EQ(baseline->stream.incarnation, incarnation); EXPECT_NE(baseline->stream.incarnation, oldStream.incarnation);
    EXPECT_GT(baseline->stream.epoch, oldStream.epoch); EXPECT_EQ(baseline->receiptCount, 0u);
    EXPECT_EQ(baseline->lanes[0].publishedThrough.value(), 0u); EXPECT_EQ(baseline->state.inventory, image->accepted.inventory);
    std::array<EventRecord, 1> output{};
    EXPECT_EQ(recovered->session->events().read(oldCursor, output).status, EventReadStatus::WrongIncarnation);
    EXPECT_EQ(recovered->session->submit(bootstrap().caller, add(2, 1, 1)).issue.error, SessionError::WrongToken);
    EXPECT_TRUE(readEvents(*recovered->session, cursor).empty());
    const auto& boot = recovered->initial->snapshot().accepted;
    auto command = add(1, boot.revision.value(), boot.builds[0].revision.value(), {1000, 0, 0}); command.epoch = boot.epoch;
    ASSERT_EQ(recovered->session->submit(boot.caller, command).state, ReceiptState::PendingPreparation);
    ASSERT_TRUE(recovered->session->advanceOneTick());
    const auto notices = readEvents(*recovered->session, cursor); ASSERT_EQ(notices.size(), 3u);
    for (const auto& notice : notices) EXPECT_EQ(notice.header.stream.epoch, boot.epoch);
    EXPECT_EQ(notices[0].header.sequence.value(), 1u); EXPECT_EQ(fresh.activations, 1u);
}

SessionBootstrap weldedBootstrap(bool loanTop=false) {
    auto boot=bootstrap();
    boot.builds[0].parts={seededPart(10),seededPart(11,{0,kBrickBodyTicks,0})};
    Connection weld;weld.id=id(20);weld.a={id(10),SocketId{1}};weld.b={id(11),SocketId{2}};
    weld.strength={1000,1000,1000,1000};boot.builds[0].connections={weld};
    if(loanTop) {
        boot.starterEntitlements={id(30)};
        boot.builds[0].parts.at(1).provenance={PartOrigin::StarterLoan,id(30)};
    }
    return boot;
}
std::shared_ptr<const BuildRefitRequest> refitRequest(const BuildSnapshot& build) {
    std::vector<RefitPart> parts;std::vector<RefitWeld> welds;
    for(size_t i=0;i<build.parts.size();++i) {
        const auto& part=build.parts[i];
        parts.push_back({part.id,{static_cast<uint32_t>(i+1),part.definition,part.placement,part.paint,part.settings}});
    }
    const auto ordinal=[&](DurableId part) {
        return static_cast<uint32_t>(std::find_if(build.parts.begin(),build.parts.end(),[&](const auto& p){return p.id==part;})-build.parts.begin()+1);
    };
    for(const auto& weld:build.connections)welds.push_back({{ordinal(weld.a.part),weld.a.socket},{ordinal(weld.b.part),weld.b.socket}});
    BuildIssue issue;auto result=BuildRefitRequest::create(parts,welds,issue);
    if(!result)throw std::runtime_error("refit fixture: "+std::string(issue.field));
    return result;
}
Command refitCommand(const GameSession& session,std::shared_ptr<const BuildRefitRequest> design) {
    const auto state=session.snapshot();
    return {AuthorityEpoch{1},RequestSequence{session.admissionState().admittedThrough.value()+1},state.revision,
        RefitBuild{{state.builds[0].id,state.builds[0].revision},std::move(design)}};
}

std::shared_ptr<const BuildRefitRequest> starterRecipe(const BuildSnapshot& build) {
    const auto owned=refitRequest(build);std::vector<RefitPart> parts(owned->parts().begin(),owned->parts().end());
    for(auto& part:parts)part.source={};
    BuildIssue issue;return BuildRefitRequest::create(parts,owned->welds(),issue);
}

TEST(StarterRecoveryPlan, RebuildRetiresLoanInstancesAndStoresExactPaidConditionWithoutRefund) {
    auto before=weldedBootstrap(true).builds[0];before.parts[0].health=4200;before.parts[0].paint={7,8,9,255};
    BuildIssue issue;const auto model=BuildModel::create(before,catalog(),issue);ASSERT_TRUE(model);
    const auto kit=starterRecipe(before);ASSERT_TRUE(kit);uint64_t next=1000;
    const auto plan=prepareBuildRefit(*model,*kit,catalog(),[&]{return id(++next);},issue,{{},id(30)});
    ASSERT_TRUE(plan)<<issue.field;EXPECT_EQ(plan->debit,ResourceAmounts{});EXPECT_EQ(plan->credit,ResourceAmounts{});
    ASSERT_EQ(plan->storedPartsAfter.size(),1);EXPECT_EQ(plan->storedPartsAfter[0],before.parts[0]);
    ASSERT_EQ(plan->after.parts.size(),2);EXPECT_EQ(plan->createdIds.size(),3);
    for(const auto& p:plan->after.parts){EXPECT_GT(p.id.counter,1000);EXPECT_EQ(p.provenance,(PartProvenance{PartOrigin::StarterLoan,id(30)}));}
    EXPECT_EQ(model->snapshot().parts,before.parts);EXPECT_TRUE(plan->consumedStoredParts.empty());
    auto applied=before;ASSERT_FALSE(plan->delta->apply(applied,catalog()));EXPECT_EQ(applied.parts,plan->after.parts);
}

TEST(StarterRecoveryPlan, RepeatedRebuildAndStockReuseCannotDuplicatePaidValue) {
    const auto before=weldedBootstrap(true).builds[0];const auto kit=starterRecipe(before);BuildIssue issue;
    auto model=BuildModel::create(before,catalog(),issue);ASSERT_TRUE(model);uint64_t next=1000;
    auto plan=prepareBuildRefit(*model,*kit,catalog(),[&]{return id(++next);},issue,{{},id(30)});ASSERT_TRUE(plan)<<issue.field;
    for(int cycle=0;cycle<4;cycle++){
        model=BuildModel::create(plan->after,catalog(),issue);ASSERT_TRUE(model);
        plan=prepareBuildRefit(*model,*kit,catalog(),[&]{return id(++next);},issue,{plan->storedPartsAfter,id(30)});
        ASSERT_TRUE(plan)<<issue.field;ASSERT_EQ(plan->storedPartsAfter.size(),1);EXPECT_EQ(plan->storedPartsAfter[0],before.parts[0]);
        EXPECT_EQ(plan->debit,ResourceAmounts{});EXPECT_EQ(plan->credit,ResourceAmounts{});
    }
    auto design=plan->after;design.parts[1]=plan->storedPartsAfter[0];design.parts[1].placement.translation={0,kBrickBodyTicks,0};
    design.connections[0].a.part=design.parts[0].id;design.connections[0].b.part=design.parts[1].id;
    model=BuildModel::create(plan->after,catalog(),issue);ASSERT_TRUE(model);
    auto reuse=prepareBuildRefit(*model,*refitRequest(design),catalog(),[&]{return id(++next);},issue,{plan->storedPartsAfter,{}});
    ASSERT_TRUE(reuse)<<issue.field;EXPECT_TRUE(reuse->storedPartsAfter.empty());EXPECT_EQ(reuse->consumedStoredParts,(std::vector<DurableId>{id(10)}));
    EXPECT_EQ(reuse->debit,ResourceAmounts{});EXPECT_EQ(reuse->credit,ResourceAmounts{});
    model=BuildModel::create(reuse->after,catalog(),issue);ASSERT_TRUE(model);
    design=reuse->after;std::erase_if(design.parts,[](const auto& p){return p.id==id(10);});design.connections.clear();
    auto dismantled=prepareBuildRefit(*model,*refitRequest(design),catalog(),[&]{return id(++next);},issue,{reuse->storedPartsAfter,{}});
    ASSERT_TRUE(dismantled)<<issue.field;EXPECT_EQ(dismantled->credit,catalog().lookup(before.parts[0].definition).definition->salvageYield);
    model=BuildModel::create(dismantled->after,catalog(),issue);ASSERT_TRUE(model);
    auto again=prepareBuildRefit(*model,*kit,catalog(),[&]{return id(++next);},issue,{dismantled->storedPartsAfter,id(30)});
    ASSERT_TRUE(again)<<issue.field;EXPECT_TRUE(again->storedPartsAfter.empty());EXPECT_EQ(again->credit,ResourceAmounts{});
}

TEST(StarterRecoveryPlan, CapacityAndInvalidProvenanceRefuseBeforeIssuingIds) {
    const auto before=weldedBootstrap(true).builds[0];const auto kit=starterRecipe(before);BuildIssue issue;
    auto model=BuildModel::create(before,catalog(),issue);ASSERT_TRUE(model);unsigned allocations=0;
    const auto allocate=[&]{++allocations;return id(1000+allocations);};
    std::vector<PartInstance> stock(64,before.parts[0]);
    EXPECT_FALSE(prepareBuildRefit(*model,*kit,catalog(),allocate,issue,{stock,id(30)}));EXPECT_EQ(allocations,0);
    stock.resize(1);stock[0].id=id(400);stock[0].provenance={PartOrigin::StarterLoan,id(30)};
    EXPECT_FALSE(prepareBuildRefit(*model,*kit,catalog(),allocate,issue,{stock,id(30)}));EXPECT_EQ(allocations,0);
    EXPECT_FALSE(prepareBuildRefit(*model,*kit,catalog(),allocate,issue,{{},id(31)}));EXPECT_EQ(allocations,0);
    EXPECT_FALSE(prepareBuildRefit(*model,*refitRequest(before),catalog(),allocate,issue,{{},id(30)}));EXPECT_EQ(allocations,0);
}

TEST(StarterRecoveryPlan, StoredPaidIdsCannotBeReissuedOrClaimedByAnotherBuild) {
    const auto before=weldedBootstrap(true).builds[0];BuildIssue issue;
    const auto model=BuildModel::create(before,catalog(),issue);ASSERT_TRUE(model);
    auto stored=before.parts[0];stored.id=id(400);
    EXPECT_FALSE(prepareBuildRefit(*model,*starterRecipe(before),catalog(),[&]{return stored.id;},issue,{{&stored,1},id(30)}));
    EXPECT_EQ(issue.error,BuildError::InvalidId);
    stored.owningBuild=id(401);auto design=before;design.parts[0].id=stored.id;design.connections[0].a.part=stored.id;
    uint64_t next=1000;EXPECT_FALSE(prepareBuildRefit(*model,*refitRequest(design),catalog(),[&]{return id(++next);},issue,{{&stored,1},{}}));
    EXPECT_EQ(issue.error,BuildError::UnknownPart);
}
void expectSameGeometry(BuildSnapshot a,BuildSnapshot b) {a.revision={};b.revision={};EXPECT_EQ(encode(a),encode(b));}
void expectReplay(const LogicalRecoveryCheckpoint& before,const LogicalRecoveryCheckpoint& after) {
    RecoveryIssue issue;
    auto base=SessionRecovery::admit(before,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(base)<<static_cast<int>(issue.error);
    std::vector<JournalRecord> records;
    for(size_t i=0;i<after.retainedJournalCount;++i)
        if(after.retainedJournal[i].sequence.value()>before.coveredThrough.value())records.push_back(after.retainedJournal[i]);
    const auto replayed=SessionRecovery::replay(*base,after.journalIdentity,portableJournal(after.journalIdentity,records),issue);
    ASSERT_TRUE(replayed)<<static_cast<int>(issue.error)<<" at "<<issue.record;
    EXPECT_EQ(replayed->snapshot().accepted.inventory,after.accepted.inventory);
    EXPECT_EQ(replayed->snapshot().allocator,after.allocator);
    EXPECT_EQ(encode(replayed->snapshot().accepted.builds[0]),encode(after.accepted.builds[0]));
    EXPECT_EQ(replayed->snapshot().history.count,after.history.count);
    EXPECT_EQ(replayed->snapshot().accepted.storedParts,after.accepted.storedParts);
    EXPECT_EQ(replayed->snapshot().accepted.starterKits,after.accepted.starterKits);
}

SessionBootstrap storedStarterBootstrap() {
    auto boot=weldedBootstrap(true);for(auto& part:boot.builds[0].parts)part.provenance={PartOrigin::StarterLoan,id(30)};
    boot.starterKits={{boot.builds[0].id,id(30),starterRecipe(boot.builds[0]),{id(10),id(11)},2}};
    auto stored=seededPart(50);stored.health=4200;stored.paint={7,8,9,255};boot.storedParts={stored};return boot;
}

TEST(StarterRecoveryOwnership, VersionTwoRoundTripAndNewWriterPreserveExactStockAndGrantBindings) {
    auto boot=storedStarterBootstrap();FakeAdapter adapter;auto session=create(adapter,boot);ASSERT_TRUE(session);
    EXPECT_EQ(session->snapshot().storedParts,boot.storedParts);auto image=capture(*session);
    RecoveryIssue recovery;auto validated=SessionRecovery::admit(*image,{kWorld,recoveryContent()},catalog(),recovery);ASSERT_TRUE(validated);
    std::vector<std::byte> bytes;SaveCodecIssue issue;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*validated,bytes,issue));
    EXPECT_EQ(bytes[4],std::byte{2});auto decoded=SessionSaveCodec::decodeCheckpoint(bytes,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(decoded)<<static_cast<int>(issue.error);
    EXPECT_EQ(decoded->snapshot().accepted.starterKits,boot.starterKits);EXPECT_EQ(decoded->snapshot().accepted.storedParts,boot.storedParts);
    session.reset();FakeAdapter fresh;auto restored=SessionRecovery::restore(decoded,fixtureIncarnation(),fresh,recovery);ASSERT_TRUE(restored)<<static_cast<int>(recovery.error);
    EXPECT_EQ(restored->session->snapshot().storedParts,boot.storedParts);
    EXPECT_EQ(restored->initial->snapshot().accepted.starterKits,boot.starterKits);
    EXPECT_EQ(restored->session->snapshot().inventory,boot.inventory);
}

TEST(StarterRecoveryOwnership, RejectsForgedStockAndGrantAliases) {
    const auto valid=storedStarterBootstrap();ASSERT_FALSE(GameSession::validateInitialState(valid,catalog()));
    for(int fault=0;fault<9;++fault){
        auto boot=valid;
        if(fault==0)boot.storedParts[0].provenance={PartOrigin::StarterLoan,id(30)};
        if(fault==1)boot.storedParts[0].id=id(10);
        if(fault==2)boot.storedParts[0].owningBuild=id(99);
        if(fault==3)boot.storedParts[0].health=10001;
        if(fault==4)boot.starterKits[0].partIds[0]=boot.storedParts[0].id;
        if(fault==5)boot.starterKits[0].partIds[0]=id(11);
        if(fault==6)boot.starterKits[0].partCount=1;
        if(fault==7)boot.starterKits[0].entitlement=id(31);
        if(fault==8)boot.starterKits[0].design=refitRequest(boot.builds[0]);
        EXPECT_TRUE(GameSession::validateInitialState(boot,catalog()))<<fault;
    }
}

TEST(StarterRecoveryOwnership, StoredPartsCountAgainstWorldCapacity) {
    auto boot=storedStarterBootstrap();boot.limits.totalParts=3;FakeAdapter adapter;auto session=create(adapter,boot);ASSERT_TRUE(session);
    const auto result=session->submit(boot.caller,add(1,0,0,{1000,0,0}));EXPECT_EQ(result.issue.error,SessionError::Capacity);
    EXPECT_EQ(session->snapshot().storedParts,boot.storedParts);EXPECT_EQ(session->lastIssuedId(),boot.lastIssuedId);
}

TEST(StarterRecoveryOwnership, RetiringEntitlementRemovesItsKitButKeepsPaidStorageInExactReplay) {
    auto boot=storedStarterBootstrap();FakeAdapter adapter;auto session=create(adapter,boot);ASSERT_TRUE(session);
    const auto before=capture(*session);auto empty=boot.builds[0];empty.parts.clear();empty.connections.clear();
    commit(*session,adapter,refitCommand(*session,refitRequest(empty)));
    const auto retired=session->retireStarterEntitlement(id(30),session->snapshot().revision);ASSERT_EQ(retired.error,EntitlementRetirementError::None);
    const auto after=capture(*session);EXPECT_TRUE(after->accepted.starterKits.empty());EXPECT_EQ(after->accepted.storedParts,boot.storedParts);
    expectReplay(*before,*after);EXPECT_EQ(after->accepted.inventory,boot.inventory);
}

TEST(StarterRecoveryRegistration, LegacyWorldBindsKitWithoutNewObjectsOrInventoryAndReplaysExactly) {
    auto boot=storedStarterBootstrap();const auto kit=boot.starterKits[0];boot.starterKits.clear();boot.storedParts.clear();
    FakeAdapter adapter;auto session=create(adapter,boot);ASSERT_TRUE(session);const auto before=capture(*session);
    ASSERT_FALSE(session->registerStarterKit(kit,boot.revision));ASSERT_NE(session->starterKit(kit.build),nullptr);
    EXPECT_EQ(*session->starterKit(kit.build),kit);EXPECT_EQ(session->lastIssuedId(),boot.lastIssuedId);
    EXPECT_EQ(session->snapshot().inventory,boot.inventory);EXPECT_EQ(adapter.activations,0);
    const auto frontier=session->journal().appendedThrough();ASSERT_FALSE(session->registerStarterKit(kit,boot.revision));
    EXPECT_EQ(session->journal().appendedThrough(),frontier);EXPECT_EQ(session->snapshot().revision,SessionRevision{1});
    const auto after=capture(*session);expectReplay(*before,*after);
    RecoveryIssue recovery;auto input=SessionRecovery::admit(*after,{kWorld,recoveryContent()},catalog(),recovery);ASSERT_TRUE(input);
    std::vector<std::byte> bytes;SaveCodecIssue issue;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*input,bytes,issue));EXPECT_EQ(bytes[4],std::byte{2});
    auto decoded=SessionSaveCodec::decodeCheckpoint(bytes,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(decoded);
    session.reset();FakeAdapter fresh;auto loaded=SessionRecovery::restore(decoded,fixtureIncarnation(),fresh,recovery);ASSERT_TRUE(loaded);
    ASSERT_NE(loaded->session->starterKit(kit.build),nullptr);EXPECT_EQ(*loaded->session->starterKit(kit.build),kit);
}

TEST(StarterRecoveryRegistration, CapacityBusyStaleAndWrongEntitlementLeaveNoPartialPolicy) {
    for(int fault=0;fault<4;++fault){
        auto boot=storedStarterBootstrap();auto kit=boot.starterKits[0];boot.starterKits.clear();
        if(fault==0){boot.limits.journalRecords=2;boot.limits.journalBytes=2*kJournalRecordStorageBytes;}
        FakeAdapter adapter;auto session=create(adapter,boot);ASSERT_TRUE(session);auto expected=boot.revision;
        if(fault==1){adapter.ready=false;EXPECT_TRUE(session->submit(boot.caller,add(1,0,0,{1000,0,0})).admitted);}
        if(fault==2)expected=SessionRevision{1};
        if(fault==3)kit.entitlement=id(31);
        const auto state=session->snapshot();const auto allocator=session->allocatorMarkers();const auto frontier=session->journal().appendedThrough();
        EXPECT_TRUE(session->registerStarterKit(kit,expected))<<fault;EXPECT_EQ(session->starterKit(kit.build),nullptr);
        expectWorldEqual(session->snapshot(),state);EXPECT_EQ(session->allocatorMarkers(),allocator);EXPECT_EQ(session->journal().appendedThrough(),frontier);
    }
}

TEST(StarterRecoveryRegistration, RetiredRegistrationCannotResurrectAReplacementEntitlement) {
    auto boot=storedStarterBootstrap();const auto kit=boot.starterKits[0];boot.starterKits.clear();
    FakeAdapter adapter;auto session=create(adapter,boot);ASSERT_TRUE(session);const auto before=capture(*session);
    ASSERT_FALSE(session->registerStarterKit(kit,boot.revision));auto empty=boot.builds[0];empty.parts.clear();empty.connections.clear();
    commit(*session,adapter,refitCommand(*session,refitRequest(empty)));
    ASSERT_EQ(session->retireStarterEntitlement(kit.entitlement,session->snapshot().revision).error,EntitlementRetirementError::None);
    const auto after=capture(*session);expectReplay(*before,*after);EXPECT_EQ(session->starterKit(kit.build),nullptr);
    EXPECT_TRUE(session->registerStarterKit(kit,session->snapshot().revision));EXPECT_EQ(session->snapshot().storedParts,boot.storedParts);
}


SessionBootstrap fittedStarterBootstrap() {
    auto boot=storedStarterBootstrap();auto paid=boot.storedParts[0];paid.placement.translation={2000,0,0};
    boot.builds[0].parts.push_back(paid);boot.storedParts.clear();boot.inventory={};return boot;
}
Command rebuildCommand(const GameSession& session) {
    const auto state=session.snapshot();
    return {session.journal().identity().epoch,RequestSequence{session.admissionState().admittedThrough.value()+1},state.revision,
        RebuildStarter{{state.builds[0].id,state.builds[0].revision}}};
}
TEST(StarterRecoveryService, RepeatedRebuildStoresExactPaidPartAndWithdrawsItWithoutAnotherPurchase) {
    const auto boot=fittedStarterBootstrap();FakeAdapter adapter;auto session=create(adapter,boot);
    const auto original=capture(*session);const auto paid=boot.builds[0].parts.back();
    commit(*session,adapter,{boot.epoch,RequestSequence{1},{},MovePart{{id(3),{}},paid.id,paid.placement}});
    ASSERT_TRUE(session->history().undo);const auto oldUndo=*session->history().undo;
    std::vector<DurableId> retired{ id(10),id(11) };
    for(int repeat=0;repeat<3;++repeat) {
        const auto request=rebuildCommand(*session);const auto receipt=commit(*session,adapter,request);
        const auto state=session->snapshot();
        EXPECT_EQ(state.inventory,ResourceAmounts{});EXPECT_EQ(state.jobs,boot.jobs);EXPECT_EQ(state.cargo,boot.cargo);
        ASSERT_EQ(state.builds[0].parts.size(),2);ASSERT_EQ(state.storedParts.size(),1);EXPECT_EQ(state.storedParts[0],paid);
        EXPECT_FALSE(session->history().undo);EXPECT_FALSE(session->history().redo);EXPECT_EQ(session->history().dormantParts,0u);
        ASSERT_NE(session->starterKit(id(3)),nullptr);EXPECT_EQ(session->starterKit(id(3))->entitlement,id(30));
        for(size_t i=0;i<2;++i) {
            const auto& part=state.builds[0].parts[i];EXPECT_EQ(part.provenance,(PartProvenance{PartOrigin::StarterLoan,id(30)}));
            EXPECT_EQ(session->starterKit(id(3))->partIds[i],part.id);
            EXPECT_EQ(std::find(retired.begin(),retired.end(),part.id),retired.end());retired.push_back(part.id);
        }
        const auto issued=session->lastIssuedId();EXPECT_EQ(session->submit(boot.caller,request).journal,receipt.journal);
        EXPECT_EQ(session->lastIssuedId(),issued);expectReplay(*original,*capture(*session));
    }
    const auto beforeWithdrawal=capture(*session);auto draft=session->snapshot().builds[0];draft.parts.push_back(paid);
    commit(*session,adapter,refitCommand(*session,refitRequest(draft)));
    EXPECT_TRUE(session->snapshot().storedParts.empty());EXPECT_EQ(session->snapshot().inventory,ResourceAmounts{});
    EXPECT_EQ(session->snapshot().builds[0].parts.front(),paid);EXPECT_FALSE(session->history().undo);
    expectReplay(*beforeWithdrawal,*capture(*session));expectReplay(*original,*capture(*session));
    auto rejected=rebuildCommand(*session);rejected.intent=Undo{{id(3),session->snapshot().builds[0].revision},oldUndo.entry,session->history().generation};
    EXPECT_EQ(session->submit(boot.caller,rejected).issue.error,SessionError::HistoryUnavailable);
    draft=session->snapshot().builds[0];std::erase_if(draft.parts,[&](const auto& part){return part.id==paid.id;});
    commit(*session,adapter,refitCommand(*session,refitRequest(draft)));
    const auto yield=catalog().lookup(paid.definition).definition->salvageYield;
    EXPECT_EQ(session->snapshot().inventory,yield);
    commit(*session,adapter,rebuildCommand(*session));EXPECT_EQ(session->snapshot().inventory,yield);
    EXPECT_TRUE(session->snapshot().storedParts.empty());expectReplay(*original,*capture(*session));
}
TEST(StarterRecoveryService, AdapterRejectionCancellationAndCapacityPreserveAcceptedOwnership) {
    for(int fault=0;fault<4;++fault) {
        auto boot=fittedStarterBootstrap();if(fault==3){boot.builds[0].parts.erase(boot.builds[0].parts.begin());boot.builds[0].connections.clear();boot.limits.totalParts=2;}
        FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);const auto worldBefore=session->snapshot();const auto kit=*session->starterKit(id(3));
        if(fault==0)adapter.rejectBegin=true;
        if(fault==1)adapter.allowActivation=false;
        const auto request=rebuildCommand(*session);const auto submitted=session->submit(boot.caller,request);
        if(fault==2){EXPECT_EQ(session->cancel(boot.caller,request.sequence).issue.error,SessionError::Canceled);}
        else if(submitted.state==ReceiptState::PendingPreparation){EXPECT_TRUE(session->advanceOneTick());}
        EXPECT_EQ(session->receipt(boot.caller,request.sequence).state,ReceiptState::Rejected)<<fault;
        expectWorldEqual(session->snapshot(),worldBefore);
        EXPECT_EQ(*session->starterKit(id(3)),kit);EXPECT_EQ(session->snapshot().storedParts,boot.storedParts);
        EXPECT_EQ(encode(session->snapshot().builds[0]),encode(boot.builds[0]));EXPECT_EQ(session->snapshot().inventory,boot.inventory);
        EXPECT_EQ(adapter.activations,0u);expectReplay(*before,*capture(*session));
    }
}
TEST(StarterRecoveryService, NewWriterRestoresStoredConditionAndCanRebuildAgainWithoutDuplicatingStock) {
    auto boot=fittedStarterBootstrap();FakeAdapter adapter;auto session=create(adapter,boot);
    commit(*session,adapter,rebuildCommand(*session));const auto saved=capture(*session);
    RecoveryIssue recovery;auto input=SessionRecovery::admit(*saved,{kWorld,recoveryContent()},catalog(),recovery);ASSERT_TRUE(input)<<static_cast<int>(recovery.error);
    std::vector<std::byte> bytes;SaveCodecIssue issue;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*input,bytes,issue));EXPECT_EQ(bytes[4],std::byte{2});
    auto decoded=SessionSaveCodec::decodeCheckpoint(bytes,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(decoded)<<static_cast<int>(issue.recovery.error);
    session.reset();FakeAdapter fresh;auto restored=SessionRecovery::restore(decoded,fixtureIncarnation(),fresh,recovery);ASSERT_TRUE(restored);
    auto& loaded=*restored->session;const auto caller=restored->initial->snapshot().accepted.caller;
    const auto initial=capture(loaded);const auto request=rebuildCommand(loaded);
    ASSERT_EQ(loaded.submit(caller,request).state,ReceiptState::PendingPreparation);ASSERT_TRUE(loaded.advanceOneTick());
    ASSERT_EQ(loaded.receipt(caller,request.sequence).state,ReceiptState::Committed);
    EXPECT_EQ(loaded.snapshot().storedParts,saved->accepted.storedParts);EXPECT_EQ(loaded.snapshot().inventory,ResourceAmounts{});
    expectReplay(*initial,*capture(loaded));
}

TEST(StarterRecoveryService, CoveredAndReplayedTransfersRejectForgedStockAndGrantImages) {
    auto boot=fittedStarterBootstrap();FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);
    commit(*session,adapter,rebuildCommand(*session));const auto after=capture(*session);
    RecoveryIssue issue;auto initial=SessionRecovery::admit(*before,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(initial);
    for(int fault=0;fault<4;++fault) {
        auto bad=*after;RequestDecisionRecord* decision=nullptr;
        for(size_t i=0;i<bad.retainedJournalCount;++i)
            if(auto* candidate=std::get_if<RequestDecisionRecord>(&bad.retainedJournal[i].payload))decision=candidate;
        ASSERT_NE(decision,nullptr);auto& edit=std::get<BuildTransition>(decision->objects);
        auto storage=std::make_shared<PartStorageTransition>(*edit.storage);
        if(fault==0)--storage->deposited[0].health;
        if(fault==1)storage->deposited[0].paint[0]^=1;
        if(fault==2)storage->starterBefore->partIds[0]=id(49);
        if(fault==3)++decision->balanceAfter.salvageMaterial;
        edit.storage=storage;
        EXPECT_FALSE(SessionRecovery::admit(bad,{kWorld,recoveryContent()},catalog(),issue))<<fault;
        std::vector<JournalRecord> tail;
        for(size_t i=0;i<bad.retainedJournalCount;++i)if(bad.retainedJournal[i].sequence.value()>before->coveredThrough.value())tail.push_back(bad.retainedJournal[i]);
        EXPECT_FALSE(SessionRecovery::replay(*initial,bad.journalIdentity,tail,issue))<<fault;
    }
    EXPECT_EQ(session->snapshot().storedParts,after->accepted.storedParts);
    EXPECT_EQ(encode(session->snapshot().builds[0]),encode(after->accepted.builds[0]));
}
TEST(GameSessionRefit, MovesConnectedPartsTogetherWithOwnedValueRetryAndExactUndo) {
    static_assert(!std::is_copy_constructible_v<BuildRefitRequest>);
    static_assert(!std::is_copy_assignable_v<BuildRefitDelta>);
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap());const auto before=capture(*session);
    auto draft=before->accepted.builds[0];for(auto& part:draft.parts)part.placement.translation.x+=50;
    auto request=refitRequest(draft);auto same=refitRequest(draft);ASSERT_NE(request.get(),same.get());EXPECT_EQ(*request,*same);
    auto command=refitCommand(*session,request);adapter.ready=false;
    ASSERT_EQ(session->submit(bootstrap().caller,command).state,ReceiptState::PendingPreparation);
    const auto pending=capture(*session);expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);
    auto duplicate=command;std::get<RefitBuild>(duplicate.intent).design=same;
    EXPECT_EQ(session->submit(bootstrap().caller,duplicate).state,ReceiptState::PendingPreparation);EXPECT_EQ(adapter.begins,1u);
    draft.parts[0].paint[0]=128;std::get<RefitBuild>(duplicate.intent).design=refitRequest(draft);
    EXPECT_EQ(session->submit(bootstrap().caller,duplicate).issue.error,SessionError::RequestConflict);
    adapter.ready=true;ASSERT_TRUE(session->advanceOneTick());adapter.completeRetirement();
    ASSERT_EQ(session->receipt(bootstrap().caller,command.sequence).state,ReceiptState::Committed);
    const auto after=capture(*session);expectReplay(*before,*after);expectReplay(*pending,*after);
    EXPECT_EQ(after->allocator.issuedThrough,100u);EXPECT_EQ(after->accepted.inventory,before->accepted.inventory);
    EXPECT_EQ(after->accepted.builds[0].connections[0].id,id(20));
    commit(*session,adapter,inverse(*session));expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);
    commit(*session,adapter,inverse(*session,false));expectSameGeometry(session->snapshot().builds[0],after->accepted.builds[0]);
    expectReplay(*before,*capture(*session));
}

TEST(GameSessionRefit, RemovesConnectedLoanWithoutRefundAndRestoresOriginalWeld) {
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap(true));const auto before=capture(*session);
    auto draft=before->accepted.builds[0];draft.parts.pop_back();draft.connections.clear();
    commit(*session,adapter,refitCommand(*session,refitRequest(draft)));
    EXPECT_EQ(session->snapshot().inventory,before->accepted.inventory);EXPECT_EQ(session->history().dormantParts,1u);
    EXPECT_TRUE(session->snapshot().builds[0].connections.empty());const auto removed=capture(*session);expectReplay(*before,*removed);
    commit(*session,adapter,inverse(*session));expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);
    EXPECT_EQ(session->history().dormantParts,0u);EXPECT_EQ(session->lastIssuedId(),100u);
    expectReplay(*before,*capture(*session));
    commit(*session,adapter,inverse(*session,false));const auto current=session->snapshot();
    EXPECT_EQ(session->retireStarterEntitlement(id(30),current.revision).error,EntitlementRetirementError::None);
    EXPECT_FALSE(session->history().undo);ASSERT_TRUE(capture(*session));
}

TEST(GameSessionRefit, ReplacesPaidPartAtomicallyAndReplaysExactIdsAndNetEconomy) {
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap());const auto before=capture(*session);
    const auto request=refitRequest(before->accepted.builds[0]);
    std::vector<RefitPart> parts(request->parts().begin(),request->parts().end());parts.at(1).source={};parts.at(1).design.paint={80,120,200,255};
    BuildIssue issue;const auto replacement=BuildRefitRequest::create(parts,request->welds(),issue);ASSERT_TRUE(replacement);
    const auto command=refitCommand(*session,replacement);const auto receipt=commit(*session,adapter,command);EXPECT_EQ(receipt.object,id(3));
    const auto after=capture(*session);EXPECT_EQ(after->allocator.issuedThrough,102u);
    EXPECT_EQ(after->accepted.builds[0].parts.back().id,id(101));EXPECT_EQ(after->accepted.builds[0].connections[0].id,id(102));
    const auto* definition=catalog().lookup(parts.at(1).design.definition).definition;ASSERT_TRUE(definition);
    EXPECT_EQ(after->accepted.inventory.salvageMaterial,before->accepted.inventory.salvageMaterial-definition->cost.salvageMaterial+definition->salvageYield.salvageMaterial);
    EXPECT_EQ(session->history().dormantParts,1u);expectReplay(*before,*after);
    commit(*session,adapter,inverse(*session));expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);
    EXPECT_EQ(session->snapshot().inventory,before->accepted.inventory);
    commit(*session,adapter,inverse(*session,false));expectSameGeometry(session->snapshot().builds[0],after->accepted.builds[0]);
    EXPECT_EQ(session->lastIssuedId(),102u);expectReplay(*before,*capture(*session));
    auto forged=*after;
    auto wrong=parts;wrong.at(1).design.paint[0]=9;
    const auto forgedRequest=BuildRefitRequest::create(wrong,request->welds(),issue);ASSERT_TRUE(forgedRequest);
    for(size_t i=0;i<forged.retainedJournalCount;++i)std::visit([&](auto& record) {
        if constexpr(requires{record.command;})std::get<RefitBuild>(record.command.intent).design=forgedRequest;
    },forged.retainedJournal[i].payload);
    std::get<RefitBuild>(forged.receipts[0].command.intent).design=forgedRequest;
    RecoveryIssue recovery;EXPECT_FALSE(SessionRecovery::admit(forged,{kWorld,recoveryContent()},catalog(),recovery));
    EXPECT_EQ(recovery.error,RecoveryError::InvalidJournal);
}

TEST(GameSessionRefit, InvalidConnectionAndAdapterFailurePreserveLiveBuild) {
    for(bool backendFailure:{false,true}) {
        FakeAdapter adapter;auto session=create(adapter,weldedBootstrap());const auto before=capture(*session);
        auto draft=before->accepted.builds[0];draft.parts.back().placement.translation.x+=50;
        if(backendFailure)draft.parts.front().placement.translation.x+=50;
        adapter.rejectBegin=backendFailure;
        const auto result=session->submit(bootstrap().caller,refitCommand(*session,refitRequest(draft)));
        EXPECT_EQ(result.state,ReceiptState::Rejected);
        EXPECT_EQ(result.issue.error,backendFailure?SessionError::AdapterRejected:SessionError::InvalidBuild);
        expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);
        EXPECT_EQ(session->snapshot().inventory,before->accepted.inventory);EXPECT_EQ(session->history().entries,0u);
        EXPECT_FALSE(session->journalFaulted());expectReplay(*before,*capture(*session));
    }
}

TEST(GameSessionRefit, LargeRejectedRefitBurnsContiguousIdsAcrossLeaseRecords) {
    FakeAdapter adapter;auto session=create(adapter);const auto before=capture(*session);
    std::vector<RefitPart> parts;
    for(uint32_t i=0;i<80;++i) {
        const auto part=seededPart(10,{static_cast<int32_t>(i)*400,0,0});
        parts.push_back({{},{i+1,part.definition,part.placement,part.paint,part.settings}});
    }
    BuildIssue issue;const auto request=BuildRefitRequest::create(parts,{},issue);ASSERT_TRUE(request)<<issue.field;
    const auto result=session->submit(bootstrap().caller,refitCommand(*session,request));
    EXPECT_EQ(result.state,ReceiptState::Rejected);EXPECT_EQ(session->lastIssuedId(),180u);
    EXPECT_FALSE(session->journalFaulted());EXPECT_EQ(adapter.begins,0u);
    const auto after=capture(*session);ASSERT_EQ(after->retainedJournalCount,4u);
    EXPECT_TRUE(std::holds_alternative<AllocatorLeaseRecord>(after->retainedJournal[0].payload));
    EXPECT_TRUE(std::holds_alternative<AllocatorLeaseRecord>(after->retainedJournal[1].payload));
    expectReplay(*before,*after);
}

TEST(GameSessionRefit, BoundsSharedPayloadsAndRejectsHistoryThatCannotUndoRemoval) {
    auto boot=weldedBootstrap();boot.limits.dormantParts=0;
    FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);
    auto draft=before->accepted.builds[0];draft.parts.pop_back();draft.connections.clear();
    EXPECT_EQ(session->submit(boot.caller,refitCommand(*session,refitRequest(draft))).issue.error,SessionError::HistoryCapacity);
    expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);EXPECT_EQ(adapter.begins,0u);
    auto normal=create(adapter,weldedBootstrap());draft=normal->snapshot().builds[0];for(auto& p:draft.parts)p.placement.translation.z+=50;
    commit(*normal,adapter,refitCommand(*normal,refitRequest(draft)));const auto image=capture(*normal);
    EXPECT_GT(ownedExtraBytes(*image),0u);RecoveryIssue recovery;
    EXPECT_FALSE(SessionRecovery::capture(*normal,recoveryContent(),recovery,image->ownedBytes-1));
    EXPECT_TRUE(SessionRecovery::capture(*normal,recoveryContent(),recovery,image->ownedBytes));
    auto forged=*image;--forged.ownedBytes;
    EXPECT_FALSE(SessionRecovery::admit(forged,{kWorld,recoveryContent()},catalog(),recovery));EXPECT_EQ(recovery.error,RecoveryError::Capacity);
    EXPECT_EQ(SessionRecovery::releaseModelWrittenPrefix(*normal,std::span(image->retainedJournal).first(image->retainedJournalCount)),JournalError::None);
    EXPECT_EQ(normal->journal().retainedBytes(),0u);ASSERT_TRUE(capture(*normal));
}
TEST(GameSessionRefit, RetiringLoanInvalidatesWeldOnlyHistoryThatReferencesItsEndpoint) {
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap(true));
    auto disconnected=session->snapshot().builds[0];disconnected.connections.clear();
    commit(*session,adapter,refitCommand(*session,refitRequest(disconnected)));
    commit(*session,adapter,removeCurrent(*session,id(11)));
    EXPECT_EQ(session->history().entries,2u);ASSERT_TRUE(capture(*session));
    EXPECT_EQ(session->retireStarterEntitlement(id(30),session->snapshot().revision).error,EntitlementRetirementError::None);
    EXPECT_EQ(session->history().entries,0u);EXPECT_EQ(session->history().dormantParts,0u);ASSERT_TRUE(capture(*session));
}

TEST(GameSessionRefit, PublishesOnlyAfterConfirmedFutureExecutionAndRestoresPendingAsCanceled) {
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap());const auto before=capture(*session);
    auto draft=before->accepted.builds[0];for(auto& part:draft.parts)part.placement.translation.z+=50;
    const auto request=refitCommand(*session,refitRequest(draft));
    ASSERT_TRUE(session->bindExecution(71,SimulationTick{}));
    ASSERT_EQ(session->submit(bootstrap().caller,request).state,ReceiptState::PendingPreparation);
    auto pending=capture(*session);RecoveryIssue issue;
    auto admitted=SessionRecovery::admit(*pending,{kWorld,recoveryContent()},catalog(),issue);ASSERT_TRUE(admitted);
    FakeAdapter recoveredAdapter;
    const auto recovered=SessionRecovery::restore(admitted,fixtureIncarnation(),recoveredAdapter,issue);ASSERT_TRUE(recovered)<<static_cast<int>(issue.error);
    expectSameGeometry(recovered->session->snapshot().builds[0],before->accepted.builds[0]);
    EXPECT_FALSE(session->advanceOneTick());ASSERT_TRUE(session->stageExecution(71,SimulationTick{7}));
    EXPECT_EQ(session->cancel(bootstrap().caller,request.sequence).issue.error,SessionError::Busy);
    EXPECT_FALSE(session->confirmExecution(71,SimulationTick{7}));
    expectSameGeometry(session->snapshot().builds[0],before->accepted.builds[0]);EXPECT_EQ(adapter.activations,0u);
    adapter.executionObserved=true;ASSERT_TRUE(session->confirmExecution(71,SimulationTick{7}));
    expectSameGeometry(session->snapshot().builds[0],draft);EXPECT_EQ(adapter.activations,1u);
    expectReplay(*before,*capture(*session));
}

std::vector<std::byte> savedCheckpoint(const GameSession& session) {
    const auto source=capture(session); RecoveryIssue recovery; const auto valid=admit(*source,recovery);
    if(!valid)throw std::runtime_error("invalid save fixture");
    std::vector<std::byte> bytes; SaveCodecIssue issue;
    if(!SessionSaveCodec::encodeCheckpoint(*valid,bytes,issue))throw std::runtime_error("failed save fixture");
    return bytes;
}
void rehashSave(std::vector<std::byte>& bytes) {
    const auto hash=voxy::core::sha256(std::span(bytes).first(bytes.size()-32));
    std::copy(hash.bytes.begin(),hash.bytes.end(),bytes.end()-32);
}
void patchSave(std::vector<std::byte>& bytes,size_t offset,uint64_t value,size_t width) {
    for(size_t i=0;i<width;++i)bytes.at(offset+i)=static_cast<std::byte>((value>>(8*i))&255);
    rehashSave(bytes);
}
TEST(GameSessionSave, FrozenNativeAndWasmBytesKeepLargeIdsAndNegativeGridCoordinates) {
    FakeAdapter adapter; auto boot=bootstrap(); boot.lastIssuedId=(uint64_t{1}<<53)+17;
    boot.inventory={uint64_t{1}<<54,10000}; auto session=create(adapter,boot);
    const auto paid=commit(*session,adapter,add()); ASSERT_TRUE(paid.object); EXPECT_GT(paid.object->counter,uint64_t{1}<<53);
    const auto bytes=savedCheckpoint(*session); SaveCodecIssue issue;
    const auto decoded=SessionSaveCodec::decodeCheckpoint(bytes,{kWorld,recoveryContent()},catalog(),issue); ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->snapshot().accepted.lastIssuedId,paid.object->counter);
    EXPECT_EQ(decoded->snapshot().accepted.builds.at(0).parts.at(0).placement.translation,(GridPosition{137,29,-211}));
    EXPECT_EQ(decoded->snapshot().accepted.inventory,session->snapshot().inventory);
    EXPECT_EQ(bytes.size(),2439u);
    EXPECT_EQ(voxy::core::sha256Hex(voxy::core::sha256(bytes)),"962e2c472045fdebac6f218cf53a2e0b0e175871fe871e85c5259d3f629c5265");
    std::cout<<"SAVE_GOLDEN large-checkpoint "<<bytes.size()<<" "<<voxy::core::sha256Hex(voxy::core::sha256(bytes))<<"\n";
    std::vector<std::byte> empty; ASSERT_TRUE(SessionSaveCodec::encodeJournal(session->journal().identity(),{},empty,issue));
    EXPECT_EQ(empty.size(),76u);
    EXPECT_EQ(voxy::core::sha256Hex(voxy::core::sha256(empty)),"d6fa134cbb6410c3ec1e99c512a26e7ceeddeab65c49998b1a054639f5273076");
    std::cout<<"SAVE_GOLDEN empty-journal "<<empty.size()<<" "<<voxy::core::sha256Hex(voxy::core::sha256(empty))<<"\n";
}
TEST(GameSessionSave, DamagedOrTruncatedCheckpointsAndWrongHostIdentityNeverDecode) {
    FakeAdapter adapter; auto session=create(adapter); const auto bytes=savedCheckpoint(*session); SaveCodecIssue issue;
    for(size_t cut=0;cut<bytes.size();++cut) {
        EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(std::span(bytes).first(cut),{kWorld,recoveryContent()},catalog(),issue))<<cut;
    }
    for(size_t at=0;at<bytes.size();++at) {
        auto bad=bytes; bad[at]^=std::byte{1};
        EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(bad,{kWorld,recoveryContent()},catalog(),issue))<<at;
    }
    auto content=recoveryContent(); content.manifestDigest[0]^=std::byte{1};
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(bytes,{kWorld,content},catalog(),issue)); EXPECT_EQ(issue.error,SaveCodecError::InvalidState);
    auto world=kWorld;world.bytes[0]^=1;
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(bytes,{world,recoveryContent()},catalog(),issue)); EXPECT_EQ(issue.error,SaveCodecError::InvalidState);
    auto future=bytes;patchSave(future,4,kSessionSaveSchema+1,4);
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(future,{kWorld,recoveryContent()},catalog(),issue)); EXPECT_EQ(issue.error,SaveCodecError::UnsupportedSchema);
    auto trailing=bytes;trailing.insert(trailing.end()-32,std::byte{});rehashSave(trailing);
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(trailing,{kWorld,recoveryContent()},catalog(),issue)); EXPECT_EQ(issue.error,SaveCodecError::InvalidEncoding);
    std::vector<std::byte> oversized(kMaximumSessionSaveBytes+1);
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(oversized,{kWorld,recoveryContent()},catalog(),issue)); EXPECT_EQ(issue.error,SaveCodecError::Capacity);
    EXPECT_EQ(adapter.begins,0u);EXPECT_EQ(adapter.activations,0u);
}
TEST(GameSessionSave, ChecksummedForgedCountsBooleansIdsAndNonfiniteValuesAreRejected) {
    FakeAdapter adapter;auto session=create(adapter);const auto bytes=savedCheckpoint(*session);SaveCodecIssue issue;
    // Frozen schema offsets for the initial one-empty-build fixture. Rehashing
    // bypasses integrity deliberately to exercise the independent parser/domain checks.
    constexpr size_t workshop=256,buildCount=257,buildId=261,buildOwner=293;
    ASSERT_EQ(bytes.at(workshop),std::byte{1});ASSERT_EQ(bytes.at(buildCount),std::byte{1});
    for(const auto& [offset,value,width]:std::array<std::array<uint64_t,3>,4>{{
        {workshop,2,1},{buildCount,33,4},{buildId+16,1,8},{buildOwner+16,3,8}}}) {
        auto bad=bytes;patchSave(bad,static_cast<size_t>(offset),value,static_cast<size_t>(width));
        EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(bad,{kWorld,recoveryContent()},catalog(),issue))<<offset;
    }
    auto duplicate=bytes;
    const std::vector<std::byte> build(bytes.begin()+buildId,bytes.begin()+buildId+65);
    duplicate.insert(duplicate.begin()+buildId+65,build.begin(),build.end());patchSave(duplicate,buildCount,2,4);
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(duplicate,{kWorld,recoveryContent()},catalog(),issue));EXPECT_EQ(issue.error,SaveCodecError::InvalidState);
    // Locate the unique initial cargo position's IEEE-754 triple from schema.
    std::vector<std::byte> position;
    for(double value:{10.,2.,-30.}) {
        const auto bits=std::bit_cast<uint64_t>(value);
        for(size_t i=0;i<8;++i)position.push_back(static_cast<std::byte>((bits>>(8*i))&255));
    }
    const auto found=std::search(bytes.begin(),bytes.end(),position.begin(),position.end());ASSERT_NE(found,bytes.end());
    const auto offset=static_cast<size_t>(found-bytes.begin());
    auto nan=bytes;patchSave(nan,offset,0x7ff8000000000000ULL,8);
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(nan,{kWorld,recoveryContent()},catalog(),issue));EXPECT_EQ(issue.error,SaveCodecError::InvalidEncoding);
    auto negativeZero=bytes;patchSave(negativeZero,offset+24,0x80000000,4); // Quaternion x, normally +0.
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(negativeZero,{kWorld,recoveryContent()},catalog(),issue));EXPECT_EQ(issue.error,SaveCodecError::InvalidState);
}
TEST(GameSessionSave, JournalsRejectBadTagsCountsWritersAndPartialBatchesWithoutReplacingOutput) {
    FakeAdapter adapter;auto session=create(adapter);commit(*session,adapter,add());
    const auto identity=session->journal().identity();const auto records=journalAfter(*session);
    SaveCodecIssue issue;std::vector<std::byte> bytes;ASSERT_TRUE(SessionSaveCodec::encodeJournal(identity,records,bytes,issue));
    auto output=records;
    for(size_t cut=0;cut<bytes.size();++cut) {
        EXPECT_FALSE(SessionSaveCodec::decodeJournal(std::span(bytes).first(cut),identity,output,issue))<<cut;
        EXPECT_EQ(output,records);
    }
    // Header(8), writer(32), count(4), record header(44), payload tag(1).
    for(const auto& [offset,value]:std::array<std::array<uint64_t,2>,3>{{{40,65},{88,255},{4,2}}}) {
        auto bad=bytes;patchSave(bad,static_cast<size_t>(offset),value,offset==88?1:4);
        EXPECT_FALSE(SessionSaveCodec::decodeJournal(bad,identity,output,issue));EXPECT_EQ(output,records);
    }
    auto wrong=identity;wrong.writerGeneration=JournalWriterGeneration{2};
    EXPECT_FALSE(SessionSaveCodec::decodeJournal(bytes,wrong,output,issue));EXPECT_EQ(issue.error,SaveCodecError::InvalidJournal);EXPECT_EQ(output,records);
    auto invalid=records;invalid.at(1).sequence=invalid.at(0).sequence;
    auto preserved=bytes;EXPECT_FALSE(SessionSaveCodec::encodeJournal(identity,invalid,preserved,issue));EXPECT_EQ(preserved,bytes);
    invalid=records;invalid.at(0).tick=SimulationTick{999};
    EXPECT_FALSE(SessionSaveCodec::encodeJournal(identity,invalid,preserved,issue));EXPECT_EQ(preserved,bytes);
}
TEST(GameSessionSave, RefitChangesAndMalformedRejectedSettingsSurvivePortableReplay) {
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap());const auto before=capture(*session);
    auto draft=before->accepted.builds.at(0);for(auto& part:draft.parts)part.placement.translation.z-=50;
    draft.parts.at(0).paint={11,22,33,255};draft.parts.at(1).settings.enabled=false;
    commit(*session,adapter,refitCommand(*session,refitRequest(draft)));expectReplay(*before,*capture(*session));
    commit(*session,adapter,inverse(*session));commit(*session,adapter,inverse(*session,false));
    draft=session->snapshot().builds.at(0);draft.parts.at(0).settings.kind=static_cast<SettingsKind>(255);
    const auto malformed=refitCommand(*session,refitRequest(draft));
    EXPECT_EQ(session->submit(bootstrap().caller,malformed).state,ReceiptState::Rejected);
    const auto after=capture(*session);ASSERT_EQ(after->receiptCount,4u);
    EXPECT_EQ(after->receipts.at(3).command,malformed);expectReplay(*before,*after);
    EXPECT_EQ(savedCheckpoint(*session).size(),8156u);
    EXPECT_EQ(voxy::core::sha256Hex(voxy::core::sha256(savedCheckpoint(*session))),"60d7019f96108c91d97da4e291a55278c228835eb6fe22c266628aacf30a0ecc");
    std::cout<<"SAVE_GOLDEN refit-checkpoint "<<savedCheckpoint(*session).size()<<" "
        <<voxy::core::sha256Hex(voxy::core::sha256(savedCheckpoint(*session)))<<"\n";
}
TEST(GameSessionSave, EveryEncodedJournalSplitKeepsPurchaseAndBankingExactlyOnce) {
    FakeAdapter adapter;auto boot=bootstrap();auto session=create(adapter,boot);const auto initialBytes=savedCheckpoint(*session);
    commit(*session,adapter,add());
    commit(*session,adapter,{AuthorityEpoch{1},RequestSequence{2},SessionRevision{1},AcceptJob{id(5)}});
    const Command deliver{AuthorityEpoch{1},RequestSequence{3},SessionRevision{2},DeliverCargo{id(6)}};
    ASSERT_TRUE(session->bindExecution(51,session->snapshot().tick));
    ASSERT_EQ(session->submit(boot.caller,deliver).state,ReceiptState::PendingPreparation);
    ASSERT_TRUE(session->stageExecution(51,SimulationTick{10}));adapter.executionObserved=true;
    ASSERT_TRUE(session->confirmExecution(51,SimulationTick{10}));
    const auto expected=capture(*session);const auto records=journalAfter(*session);SaveCodecIssue codec;RecoveryIssue issue;
    for(size_t split=0;split<=records.size();++split) {
        const auto initial=SessionSaveCodec::decodeCheckpoint(initialBytes,{kWorld,recoveryContent()},catalog(),codec);ASSERT_TRUE(initial);
        const auto prefix=portableJournal(expected->journalIdentity,std::span(records).first(split));
        const auto partial=SessionRecovery::replay(*initial,expected->journalIdentity,prefix,issue);ASSERT_TRUE(partial)<<split;
        std::vector<std::byte> middle;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*partial,middle,codec));
        const auto resumed=SessionSaveCodec::decodeCheckpoint(middle,{kWorld,recoveryContent()},catalog(),codec);ASSERT_TRUE(resumed)<<split;
        const auto suffix=portableJournal(expected->journalIdentity,std::span(records).subspan(split));
        const auto complete=SessionRecovery::replay(*resumed,expected->journalIdentity,suffix,issue);ASSERT_TRUE(complete)<<split;
        expectRecoveredState(complete->snapshot(),*expected);
        EXPECT_FALSE(SessionRecovery::replay(*complete,expected->journalIdentity,records,issue));EXPECT_EQ(issue.error,RecoveryError::InvalidCoverage);
    }
}
TEST(GameSessionSave, CompactedByteReloadRetiresOldRequestsWithoutDuplicatingRewards) {
    FakeAdapter adapter;auto boot=bootstrap();boot.jobs.at(0).phase=JobPhase::Accepted;boot.jobs.at(0).acceptedBy=boot.caller.participant;
    auto session=create(adapter,boot);const Command delivery{AuthorityEpoch{1},RequestSequence{1},SessionRevision{},DeliverCargo{id(6)}};
    ASSERT_TRUE(session->bindExecution(77,SimulationTick{}));
    ASSERT_EQ(session->submit(boot.caller,delivery).state,ReceiptState::PendingPreparation);
    ASSERT_TRUE(session->stageExecution(77,SimulationTick{1}));adapter.executionObserved=true;ASSERT_TRUE(session->confirmExecution(77,SimulationTick{1}));
    const auto paid=session->snapshot().inventory;const auto records=journalAfter(*session);
    // This explicitly models compaction acknowledgment in RAM, not fsync/IDB.
    ASSERT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session,records),JournalError::None);
    const auto bytes=savedCheckpoint(*session);SaveCodecIssue codec;
    auto decoded=SessionSaveCodec::decodeCheckpoint(bytes,{kWorld,recoveryContent()},catalog(),codec);ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->snapshot().retainedJournalCount,0u);EXPECT_GT(decoded->snapshot().modelReleasedThrough.value(),0u);
    session.reset();FakeAdapter fresh;RecoveryIssue issue;
    const auto restored=SessionRecovery::restore(decoded,fixtureIncarnation(),fresh,issue);ASSERT_TRUE(restored);
    EXPECT_EQ(restored->session->snapshot().inventory,paid);EXPECT_TRUE(restored->session->snapshot().cargo.empty());
    EXPECT_EQ(restored->session->submit(boot.caller,delivery).issue.error,SessionError::WrongToken);
    const auto caller=restored->initial->snapshot().accepted.caller;auto retry=delivery;
    retry.epoch=restored->initial->snapshot().accepted.epoch;retry.expectedRevision=restored->session->snapshot().revision;
    EXPECT_EQ(restored->session->submit(caller,retry).issue.error,SessionError::UnknownCargo);
    EXPECT_EQ(restored->session->snapshot().inventory,paid);EXPECT_EQ(fresh.activations,0u);
    for(const auto* checkpoint:{restored->retired.get(),restored->initial.get()}) {
        std::vector<std::byte> encoded;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*checkpoint,encoded,codec));
        EXPECT_TRUE(SessionSaveCodec::decodeCheckpoint(encoded,{kWorld,recoveryContent()},catalog(),codec));
    }
    EXPECT_EQ(portableJournal(restored->retired->snapshot().journalIdentity,
        std::span(restored->retirementRecords).first(restored->retirementRecordCount)).size(),restored->retirementRecordCount);
}

Command cutCommand(const GameSession& session,DurableId connection=id(20)) {
    const auto state=session.snapshot();
    return {session.journal().identity().epoch,RequestSequence{session.admissionState().admittedThrough.value()+1},state.revision,
        CutWeld{{id(3),state.builds[0].revision},connection}};
}

TEST(GameSessionCut, FieldCutWaitsForExecutionAndPreservesEveryOwnedValueOnExactRetry) {
    auto boot=storedStarterBootstrap();boot.workshopEnabled=false;
    boot.builds[0].parts[0].health=4321;boot.builds[0].parts[0].paint={21,43,65,255};
    boot.builds[0].connections[0].damage=2700;
    FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);
    const auto request=cutCommand(*session);ASSERT_TRUE(session->bindExecution(71,SimulationTick{}));
    ASSERT_EQ(session->submit(boot.caller,request).state,ReceiptState::PendingPreparation);
    ASSERT_TRUE(adapter.lastCut);EXPECT_EQ(*adapter.lastCut,std::get<CutWeld>(request.intent));
    const auto pending=capture(*session);EXPECT_EQ(pending->allocator,before->allocator);
    EXPECT_FALSE(pending->pending[0].assignedObject);EXPECT_EQ(pending->pending[0].assignedRange.count,0u);
    EXPECT_EQ(session->submit(boot.caller,request).state,ReceiptState::PendingPreparation);EXPECT_EQ(adapter.begins,1u);
    auto conflict=request;std::get<CutWeld>(conflict.intent).connection=id(21);
    EXPECT_EQ(session->submit(boot.caller,conflict).issue.error,SessionError::RequestConflict);
    ASSERT_TRUE(session->stageExecution(71,SimulationTick{7}));
    EXPECT_FALSE(session->confirmExecution(71,SimulationTick{7}));EXPECT_EQ(adapter.activations,0u);
    EXPECT_EQ(encode(session->snapshot().builds[0]),encode(boot.builds[0]));
    EXPECT_EQ(session->cancel(boot.caller,request.sequence).issue.error,SessionError::Busy);
    adapter.executionObserved=true;ASSERT_TRUE(session->confirmExecution(71,SimulationTick{7}));
    const auto after=capture(*session);auto expected=boot.builds[0];
    expected.connections[0].enabled=false;expected.connections[0].damage=kFullHealth;expected.revision=TopologyRevision{1};
    EXPECT_EQ(encode(after->accepted.builds[0]),encode(expected));EXPECT_EQ(after->accepted.revision,SessionRevision{1});
    EXPECT_EQ(after->accepted.inventory,boot.inventory);EXPECT_EQ(after->accepted.storedParts,boot.storedParts);
    EXPECT_EQ(after->accepted.starterKits,boot.starterKits);EXPECT_EQ(after->allocator,before->allocator);
    EXPECT_EQ(after->accepted.jobs,boot.jobs);EXPECT_EQ(after->accepted.cargo,boot.cargo);
    EXPECT_EQ(session->submit(boot.caller,request).state,ReceiptState::Committed);
    EXPECT_EQ(session->receipt(boot.caller,request.sequence).object,id(20));EXPECT_EQ(adapter.activations,1u);
    EXPECT_FALSE(session->history().undo);EXPECT_FALSE(session->history().redo);
    expectReplay(*before,*after);expectReplay(*pending,*after);
    EXPECT_EQ(session->submit(boot.caller,cutCommand(*session)).issue.error,SessionError::InvalidBuild);
    EXPECT_EQ(adapter.activations,1u);EXPECT_EQ(session->lastIssuedId(),boot.lastIssuedId);
    expectReplay(*after,*capture(*session));
}

TEST(GameSessionCut, RetiresOnlyTargetHistoryAndKeepsOtherBuildUndoUsable) {
    auto boot=weldedBootstrap(true);auto second=boot.builds[0];second.id=id(4);second.connections.clear();
    second.parts={seededPart(12,{700,0,0})};second.parts[0].owningBuild=second.id;boot.builds.push_back(second);
    FakeAdapter adapter;auto session=create(adapter,boot);const auto initial=capture(*session);
    auto shifted=boot.builds[0];for(auto& part:shifted.parts)part.placement.translation.z+=50;
    commit(*session,adapter,refitCommand(*session,refitRequest(shifted)));const auto targetUndo=*session->history().undo;
    commit(*session,adapter,{boot.epoch,RequestSequence{2},SessionRevision{1},
        MovePart{{id(4),{}},id(12),{{800,0,0},{}}}});
    ASSERT_EQ(session->history().entries,2u);const auto otherUndo=*session->history().undo;
    commit(*session,adapter,cutCommand(*session));
    ASSERT_EQ(session->history().entries,1u);ASSERT_TRUE(session->history().undo);
    EXPECT_EQ(session->history().undo->entry,otherUndo.entry);
    const auto cut=capture(*session);expectReplay(*initial,*cut);
    auto old=cutCommand(*session);old.intent=Undo{{id(3),session->snapshot().builds[0].revision},targetUndo.entry,session->history().generation};
    EXPECT_EQ(session->submit(boot.caller,old).issue.error,SessionError::HistoryConflict);
    commit(*session,adapter,inverse(*session));
    expectSameGeometry(session->snapshot().builds[1],second);
    EXPECT_FALSE(session->snapshot().builds[0].connections[0].enabled);
    expectReplay(*initial,*capture(*session));
}

TEST(GameSessionCut, AdmissionCancellationAndBackendFailuresKeepOwnershipAndAllocatorUntouched) {
    for(int fault=0;fault<11;++fault) {
        SCOPED_TRACE(fault);auto boot=weldedBootstrap(true);boot.workshopEnabled=false;
        if(fault==1)boot.builds[0].connections[0].enabled=false;
        if(fault==5)boot.builds[0].editLease=EditLease{id(1),AuthorityEpoch{1},SimulationTick{1}};
        FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);
        const auto worldBefore=session->snapshot();auto request=cutCommand(*session);
        if(fault==0)std::get<CutWeld>(request.intent).connection=id(19);
        if(fault==3)std::get<CutWeld>(request.intent).target.expectedRevision=TopologyRevision{1};
        if(fault==4)request.expectedRevision=SessionRevision{1};
        if(fault==6)adapter.rejectBegin=true;
        if(fault==7)adapter.allowActivation=false;
        if(fault==9){adapter.ready=false;adapter.rejectPoll=true;}
        if(fault==10)adapter.throwAfterReserve=true;
        const auto submitted=session->submit(fault==2?CallerContext{id(40),boot.caller.sessionToken}:boot.caller,request);
        if(fault==8){EXPECT_EQ(session->cancel(boot.caller,request.sequence).issue.error,SessionError::Canceled);}
        else if(submitted.state==ReceiptState::PendingPreparation){EXPECT_TRUE(session->advanceOneTick());}
        EXPECT_EQ(session->receipt(boot.caller,request.sequence).state,ReceiptState::Rejected);
        expectWorldEqual(session->snapshot(),worldBefore);EXPECT_EQ(session->allocatorMarkers(),before->allocator);
        EXPECT_EQ(adapter.activations,0u);EXPECT_EQ(adapter.resident(),0u);EXPECT_FALSE(session->journalFaulted());
        expectReplay(*before,*capture(*session));
    }
}

TEST(GameSessionCut, FutureCutChecksLeaseBeforeStagingAndPendingCrashRestoresUncutBoat) {
    for(bool expires:{false,true}) {
        auto boot=storedStarterBootstrap();boot.workshopEnabled=false;
        boot.builds[0].editLease=EditLease{id(1),AuthorityEpoch{1},SimulationTick{expires?7u:8u}};
        FakeAdapter adapter;auto session=create(adapter,boot);const auto request=cutCommand(*session);
        ASSERT_TRUE(session->bindExecution(71,SimulationTick{}));
        ASSERT_EQ(session->submit(boot.caller,request).state,ReceiptState::PendingPreparation);
        const auto pending=capture(*session);RecoveryIssue recovery;auto input=admit(*pending,recovery);ASSERT_TRUE(input);
        FakeAdapter fresh;auto restored=SessionRecovery::restore(input,fixtureIncarnation(),fresh,recovery);ASSERT_TRUE(restored);
        auto unleased=boot.builds[0];unleased.editLease.reset();
        EXPECT_EQ(encode(restored->session->snapshot().builds[0]),encode(unleased));
        EXPECT_EQ(restored->session->snapshot().storedParts,boot.storedParts);EXPECT_EQ(fresh.begins,0u);
        ASSERT_EQ(restored->retired->snapshot().receiptCount,1u);
        EXPECT_EQ(restored->retired->snapshot().receipts[0].outcome.error,SessionError::Canceled);
        ASSERT_TRUE(capture(*restored->session));
        EXPECT_EQ(session->stageExecution(71,SimulationTick{7}),!expires);
        if(expires){EXPECT_EQ(adapter.stagedTick,SimulationTick{});EXPECT_EQ(session->receipt(boot.caller,request.sequence).issue.error,SessionError::LeaseDenied);}
        else {adapter.executionObserved=true;ASSERT_TRUE(session->confirmExecution(71,SimulationTick{7}));}
        EXPECT_EQ(adapter.activations,expires?0u:1u);ASSERT_TRUE(capture(*session));
    }
}

TEST(GameSessionCut, CoveredAndReplayedCutsRejectForgedBondsBalancesAndUndoHistory) {
    FakeAdapter adapter;auto session=create(adapter,weldedBootstrap(true));const auto before=capture(*session);
    commit(*session,adapter,cutCommand(*session));const auto after=capture(*session);
    RecoveryIssue recovery;const auto initial=admit(*before,recovery);ASSERT_TRUE(initial);
    for(int fault=0;fault<13;++fault) {
        SCOPED_TRACE(fault);auto bad=*after;RequestDecisionRecord* decision=nullptr;
        for(size_t i=0;i<bad.retainedJournalCount;++i)
            if(auto* candidate=std::get_if<RequestDecisionRecord>(&bad.retainedJournal[i].payload))decision=candidate;
        ASSERT_NE(decision,nullptr);auto& edit=std::get<BuildTransition>(decision->objects);
        auto cut=std::make_shared<WeldCutTransition>(*edit.cut);
        if(fault==0)cut->before.id=id(19);
        if(fault==1)cut->before.a.part=id(11);
        if(fault==2)cut->before.enabled=false;
        if(fault==3)cut->before.damage=kFullHealth+1;
        if(fault==4)cut->before.strength.tensionNewtons+=1;
        if(fault==5)++decision->balanceAfter.salvageMaterial;
        if(fault==6){decision->history.action=HistoryAction::Record;decision->history.entry=HistoryEntryId{decision->history.after.value()};}
        if(fault==7)edit.afterPart=after->accepted.builds[0].parts[0];
        if(fault==8)edit.refitForward=false;
        if(fault==9){decision->history.evictedCount=1;decision->history.evicted[0]=HistoryEntryId{1};}
        if(fault==10)cut->before.a.socket=SocketId{9};
        if(fault==11)std::get<RequestAdmissionRecord>(bad.retainedJournal[0].payload).assignedObject=id(19);
        if(fault==12)std::get<RequestAdmissionRecord>(bad.retainedJournal[0].payload).assignedRange={id(100),1};
        edit.cut=cut;
        EXPECT_FALSE(SessionRecovery::admit(bad,{kWorld,recoveryContent()},catalog(),recovery));
        EXPECT_FALSE(SessionRecovery::replay(*initial,bad.journalIdentity,
            std::span(bad.retainedJournal).first(bad.retainedJournalCount),recovery));
    }
    // A plausible former damage value cannot be authenticated by a checksum.
    // Replay against the independently held predecessor must match it exactly.
    auto records=journalAfter(*session);auto& edit=std::get<BuildTransition>(std::get<RequestDecisionRecord>(records.back().payload).objects);
    auto cut=std::make_shared<WeldCutTransition>(*edit.cut);cut->before.damage=42;edit.cut=cut;
    EXPECT_FALSE(SessionRecovery::replay(*initial,after->journalIdentity,records,recovery));
    EXPECT_EQ(encode(session->snapshot().builds[0]),encode(after->accepted.builds[0]));
}

TEST(GameSessionCut, EveryJournalBoundaryRoundTripsVersionThreeAndFreshWriterKeepsFragments) {
    FakeAdapter adapter;auto session=create(adapter,storedStarterBootstrap());const auto before=capture(*session);
    const auto originalBytes=savedCheckpoint(*session);EXPECT_EQ(originalBytes[4],std::byte{2});
    commit(*session,adapter,cutCommand(*session));const auto after=capture(*session);const auto bytes=savedCheckpoint(*session);
    EXPECT_EQ(bytes[4],std::byte{3});EXPECT_EQ(bytes.size(),2674u);
    EXPECT_EQ(voxy::core::sha256Hex(voxy::core::sha256(bytes)),"49704f0e7cfdc07f0d3015eccabd20d8ac7773b77724973996dafc0647d56e17");
    std::cout<<"SAVE_GOLDEN cut-checkpoint "<<bytes.size()<<" "
        <<voxy::core::sha256Hex(voxy::core::sha256(bytes))<<"\n";
    const auto records=journalAfter(*session);ASSERT_EQ(records.size(),2u);SaveCodecIssue codec;RecoveryIssue recovery;
    std::vector<std::byte> journal;ASSERT_TRUE(SessionSaveCodec::encodeJournal(after->journalIdentity,records,journal,codec));
    EXPECT_EQ(journal[4],std::byte{3});EXPECT_EQ(journal.size(),957u);
    EXPECT_EQ(voxy::core::sha256Hex(voxy::core::sha256(journal)),"100155e57d6ae7b491e269af2cf0277bf39abc9271e41e2cf3785089e0689452");
    std::cout<<"SAVE_GOLDEN cut-journal "<<journal.size()<<" "
        <<voxy::core::sha256Hex(voxy::core::sha256(journal))<<"\n";
    for(size_t split=0;split<=records.size();++split) {
        SCOPED_TRACE(split);const auto base=SessionSaveCodec::decodeCheckpoint(originalBytes,{kWorld,recoveryContent()},catalog(),codec);ASSERT_TRUE(base);
        const auto prefix=portableJournal(after->journalIdentity,std::span(records).first(split));
        const auto partial=SessionRecovery::replay(*base,after->journalIdentity,prefix,recovery);ASSERT_TRUE(partial);
        std::vector<std::byte> checkpoint;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*partial,checkpoint,codec));
        const auto decoded=SessionSaveCodec::decodeCheckpoint(checkpoint,{kWorld,recoveryContent()},catalog(),codec);ASSERT_TRUE(decoded);
        const auto suffix=portableJournal(after->journalIdentity,std::span(records).subspan(split));
        const auto complete=SessionRecovery::replay(*decoded,after->journalIdentity,suffix,recovery);ASSERT_TRUE(complete);
        expectRecoveredState(complete->snapshot(),*after);
    }
    auto downgraded=bytes;patchSave(downgraded,4,2,4);rehashSave(downgraded);
    EXPECT_FALSE(SessionSaveCodec::decodeCheckpoint(downgraded,{kWorld,recoveryContent()},catalog(),codec));
    ASSERT_EQ(SessionRecovery::releaseModelWrittenPrefix(*session,records),JournalError::None);
    auto saved=SessionSaveCodec::decodeCheckpoint(savedCheckpoint(*session),{kWorld,recoveryContent()},catalog(),codec);ASSERT_TRUE(saved);
    session.reset();FakeAdapter fresh;auto restored=SessionRecovery::restore(saved,fixtureIncarnation(),fresh,recovery);ASSERT_TRUE(restored);
    EXPECT_EQ(encode(restored->session->snapshot().builds[0]),encode(after->accepted.builds[0]));
    EXPECT_EQ(*restored->session->starterKit(id(3)),after->accepted.starterKits[0]);
    EXPECT_EQ(restored->session->snapshot().storedParts,after->accepted.storedParts);EXPECT_EQ(fresh.begins,0u);
    EXPECT_FALSE(restored->session->history().undo);ASSERT_TRUE(capture(*restored->session));
}

TEST(GameSessionCut, ReservesEntireCutPayloadBeforeAnyPreparationOrJournalMutation) {
    for(bool exact:{false,true}) {
        auto boot=weldedBootstrap();boot.limits.journalRecords=3; // Two cut records plus reserved authority closure.
        boot.limits.journalBytes=3*kJournalRecordStorageBytes+sizeof(WeldCutTransition)-(exact?0u:1u);
        FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);
        const auto request=cutCommand(*session);
        if(exact) {
            commit(*session,adapter,request);EXPECT_EQ(adapter.activations,1u);const auto after=capture(*session);
            EXPECT_EQ(session->journal().retainedBytes(),2*kJournalRecordStorageBytes+sizeof(WeldCutTransition));expectReplay(*before,*after);
            RecoveryIssue recovery;
            EXPECT_FALSE(SessionRecovery::capture(*session,recoveryContent(),recovery,after->ownedBytes-1));
            EXPECT_TRUE(SessionRecovery::capture(*session,recoveryContent(),recovery,after->ownedBytes));
        } else {
            EXPECT_EQ(session->submit(boot.caller,request).issue.error,SessionError::JournalCapacity);
            EXPECT_EQ(adapter.begins,0u);EXPECT_EQ(session->journal().retainedBytes(),0u);
            EXPECT_EQ(session->allocatorMarkers(),before->allocator);expectRecoveredState(*capture(*session),*before);
        }
    }
}

TEST(GameSessionCut, StarterRecoveryAfterCutCollectsPaidPartsOnceAndRetiresEveryOldLoan) {
    auto boot=storedStarterBootstrap();boot.storedParts.clear();boot.inventory={};
    auto paid=seededPart(9,{0,-kBrickBodyTicks,0});paid.health=3456;paid.paint={10,20,30,255};
    boot.builds[0].parts.insert(boot.builds[0].parts.begin(),paid);
    Connection lower=boot.builds[0].connections[0];lower.id=id(19);lower.a.part=paid.id;lower.b.part=id(10);
    boot.builds[0].connections.insert(boot.builds[0].connections.begin(),lower);
    FakeAdapter adapter;auto session=create(adapter,boot);const auto before=capture(*session);
    commit(*session,adapter,cutCommand(*session,id(19)));commit(*session,adapter,cutCommand(*session,id(20)));
    ASSERT_EQ(session->snapshot().builds[0].parts.size(),3u);
    commit(*session,adapter,rebuildCommand(*session));auto state=session->snapshot();
    EXPECT_EQ(state.inventory,ResourceAmounts{});ASSERT_EQ(state.storedParts.size(),1u);EXPECT_EQ(state.storedParts[0],paid);
    ASSERT_EQ(state.builds[0].parts.size(),2u);ASSERT_EQ(state.builds[0].connections.size(),1u);EXPECT_TRUE(state.builds[0].connections[0].enabled);
    for(const auto& part:state.builds[0].parts){EXPECT_NE(part.id,id(10));EXPECT_NE(part.id,id(11));EXPECT_EQ(part.provenance,(PartProvenance{PartOrigin::StarterLoan,id(30)}));}
    commit(*session,adapter,rebuildCommand(*session));EXPECT_EQ(session->snapshot().storedParts,state.storedParts);
    EXPECT_EQ(session->snapshot().inventory,ResourceAmounts{});expectReplay(*before,*capture(*session));
}

} // namespace
