// Isolated replacement-allocation probe, deliberately excluded from combined
// repository tests. It checks actual new/new[]/aligned-new traffic in this TU
// and linked journal implementation, not a fake allocator supplied to the API.
#include "game/expedition/session_recovery.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
bool probeAllocation = false;
size_t failAt = 0;
size_t attemptedAllocations = 0;
bool injectedFailure = false;
void checkAllocation() {
    if (probeAllocation && attemptedAllocations++ == failAt) { injectedFailure = true; throw std::bad_alloc{}; }
}
void* allocate(size_t size) {
    checkAllocation();
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc{};
}
void* alignedAllocate(size_t size, size_t alignment) {
    checkAllocation();
    if (size == 0) size = 1;
    const size_t remainder = size % alignment;
    if (remainder != 0) {
        if (size > std::numeric_limits<size_t>::max() - (alignment - remainder)) throw std::bad_alloc{};
        size += alignment - remainder;
    }
    if (void* result = std::aligned_alloc(alignment, size)) return result;
    throw std::bad_alloc{};
}
}
void* operator new(size_t size) { return allocate(size); }
void* operator new[](size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t) noexcept { std::free(pointer); }
void* operator new(size_t size, std::align_val_t align) { return alignedAllocate(size, static_cast<size_t>(align)); }
void* operator new[](size_t size, std::align_val_t align) { return alignedAllocate(size, static_cast<size_t>(align)); }
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { std::free(pointer); }

int main() {
    using namespace voxy::game::construction;
    using namespace voxy::game::expedition;
    class Adapter final : public PreparationAdapter {
    public:
        PreparationResult begin(const PreparationRequest& request) override { return {request.ticket, PreparationState::Ready}; }
        PreparationResult poll(PreparationTicket ticket) noexcept override { return {ticket, PreparationState::Ready}; }
        bool canActivate(PreparationTicket) const noexcept override { return true; }
        void activate(PreparationTicket, SimulationTick) noexcept override {}
        void discard(PreparationTicket) noexcept override {}
    } adapter;
    const WorldNamespace world{{'c','h','e','c','k','p','o','i','n','t','-','f','a','u','l','t'}};
    const auto id = [&](uint64_t counter) { return DurableId{world, counter}; };
    CatalogIssue catalogIssue;
    auto catalog = PartCatalog::create(makeStarterCatalogDraft(), catalogIssue);
    if (!catalog) return 1;
    SessionBootstrap boot; boot.world = world; boot.caller = {id(1), id(2)};
    boot.lastIssuedId = 10; boot.workshopEnabled = true; boot.inventory = {10000, 10000};
    boot.starterEntitlements = {id(5)};
    BuildSnapshot build; build.id = id(3); build.owner = id(1);
    PartInstance engine; engine.id = id(4); engine.owningBuild = build.id;
    engine.definition = starterPartKey(StarterPart::Engine);
    engine.settings = defaultModuleSettings(*catalog->lookup(engine.definition).definition);
    engine.provenance = {PartOrigin::StarterLoan, id(5)}; engine.health = 3141;
    PartInstance plate; plate.id = id(8); plate.owningBuild = build.id;
    plate.definition = starterPartKey(StarterPart::Plate); plate.placement.translation.x = 1000;
    build.parts = {engine, plate}; boot.builds = {build};
    boot.jobs = {{id(6), 1, JobPhase::Available, {}}};
    boot.cargoDefinitions = {{{id(1000), 1}, 500, .3, {50, 1}, CargoRecoveryRule::PreserveUnique}};
    boot.cargo = {{id(7), {id(1000), 1}, id(1), {10, 2, -30}, {}, id(6)}};
    SessionIssue sessionIssue;
    auto session = GameSession::create(boot, EventStreamIncarnation{{1}}, *catalog, adapter, sessionIssue);
    if (!session) return 2;
    const Command remove{AuthorityEpoch{1}, RequestSequence{1}, {}, RemovePart{{build.id, {}}, engine.id}};
    if (session->submit(boot.caller, remove).state != ReceiptState::PendingPreparation || !session->advanceOneTick()) return 3;
    const Command first{AuthorityEpoch{1}, RequestSequence{2}, SessionRevision{1},
        AddPart{{build.id, TopologyRevision{1}}, starterPartKey(StarterPart::Plate), {{2000, 0, 0}, {}}}};
    auto second = first; second.sequence = RequestSequence{3};
    std::get<AddPart>(second.intent).placement.translation.x = 3000;
    if (session->submit(boot.caller, first).state != ReceiptState::PendingPreparation
        || session->submit(boot.caller, second).state != ReceiptState::PendingPreparation) return 4;
    const auto before = session->snapshot();
    RecoveryContentIdentity content; content.manifest = {id(9000), 1}; content.manifestDigest.fill(std::byte{0x51});
    RecoveryIssue issue;
    const auto source = SessionRecovery::capture(*session, content, issue);
    if (!source) return 5;
    auto admitted = SessionRecovery::admit(*source, {world, content}, *catalog, issue);
    if (!admitted) return 6;
    session.reset();
    Adapter fresh;
    size_t failures = 0;
    bool passed = true, succeeded = false, reachedUninjected = false;
    size_t successes = 0, recoveredFallbacks = 0;
    for (size_t index = 0; index < 4096; ++index) {
        // A successful optional-allocation fallback consumes its capability.
        // Re-admit the exact source outside the injection window, then continue
        // through every later allocation instead of stopping at that fallback.
        if (!admitted) admitted = SessionRecovery::admit(*source, {world, content}, *catalog, issue);
        if (!admitted) return 8;
        const auto* original = admitted.get();
        attemptedAllocations = 0; failAt = index; injectedFailure = false; probeAllocation = true;
        auto result = SessionRecovery::restore(admitted, EventStreamIncarnation{{2}}, fresh, issue);
        probeAllocation = false;
        if (result) {
            succeeded = true;
            const auto& accepted = result->initial->snapshot().accepted;
            const auto& retired = result->retired->snapshot();
            passed = passed && !issue && !admitted && (injectedFailure ? attemptedAllocations > index : attemptedAllocations == index)
                && accepted.inventory == before.inventory && accepted.starterEntitlements == boot.starterEntitlements
                && accepted.builds[0].parts == before.builds[0].parts && accepted.jobs == before.jobs && accepted.cargo == before.cargo
                && accepted.caller.sessionToken.counter == source->allocator.reservedThrough + 1
                && accepted.epoch == AuthorityEpoch{2} && retired.pendingCount == 0 && retired.history.partCount == 0
                && retired.admission.processedThrough == RequestFrontier{3} && result->retirementRecordCount == 3
                && result->session->snapshot().inventory == before.inventory
                && result->initial->snapshot().durability == ReceiptDurability::Volatile
                && result->session->events().identity().incarnation == EventStreamIncarnation{{2}}
                && result->session->events().stats(EventLane::Domain)->occupancy == 0;
            ++successes;
            if (!injectedFailure) { reachedUninjected = true; break; }
            ++recoveredFallbacks;
            continue;
        }
        ++failures;
        passed = passed && admitted.get() == original;
        if (!admitted || issue.error != RecoveryError::Capacity || attemptedAllocations != index + 1) {
            std::printf("bad_failure index=%zu attempts=%zu error=%u\n",
                index, attemptedAllocations, static_cast<unsigned>(issue.error));
            passed = false; break;
        }
        const auto& image = admitted->snapshot();
        passed = passed && image.accepted.revision == before.revision && image.accepted.tick == before.tick
            && image.accepted.inventory == before.inventory && image.accepted.jobs == before.jobs
            && image.accepted.cargo == before.cargo && image.accepted.builds[0].parts == before.builds[0].parts
            && image.history.parts[0] == engine && image.history.partCount == 1
            && image.pendingCount == 2 && image.pending == source->pending
            && image.allocator == source->allocator && image.coveredThrough == source->coveredThrough;
    }
    auto exhausted = *source;
    exhausted.retainedJournal = {}; exhausted.retainedJournalCount = 0;
    exhausted.modelReleasedThrough = exhausted.coveredThrough;
    exhausted.allocator.reservedThrough = std::numeric_limits<uint64_t>::max();
    auto retryable = SessionRecovery::admit(exhausted, {world, content}, *catalog, issue);
    if (!retryable) return 7;
    attemptedAllocations = 0; failAt = 0; probeAllocation = true;
    auto rejected = SessionRecovery::restore(retryable, EventStreamIncarnation{{2}}, fresh, issue);
    probeAllocation = false;
    passed = passed && succeeded && reachedUninjected && failures > 6 && !rejected && retryable
        && issue.error == RecoveryError::CounterExhausted && attemptedAllocations == 0;
    std::printf("restore_failures=%zu successful_fallbacks=%zu successful_restores=%zu eventual_uninjected_success=%s preflight_allocation_attempts=%zu authority_preserved=%s\n",
        failures, recoveredFallbacks, successes, reachedUninjected ? "true" : "false", attemptedAllocations, passed ? "true" : "false");
    return passed ? 0 : 1;
}
