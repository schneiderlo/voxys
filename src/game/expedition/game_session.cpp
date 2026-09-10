#include "game/expedition/game_session.hpp"
#include "game/expedition/session_recovery.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace voxy::game::expedition {
namespace {
SessionIssue fail(SessionError error) { return {error, {}}; }
bool validLimits(SessionLimits limits) {
    return limits.builds > 0 && limits.builds <= 32
        && limits.totalParts > 0 && limits.totalParts <= construction::kMaximumBuildParts
        && limits.cargo > 0 && limits.cargo <= 256
        && limits.jobs > 0 && limits.jobs <= 128
        && limits.receipts > 0 && limits.receipts <= 64
        && limits.pendingCommands > 0 && limits.pendingCommands <= 2
        && limits.journalRecords >= 2 && limits.journalRecords <= kMaximumJournalRecords
        && limits.journalBytes >= kJournalRecordStorageBytes && limits.journalBytes <= kMaximumJournalPayloadBytes
        && limits.candidateBytes > 0 && limits.candidateBytes <= 512 * 1024
        && limits.historyEntries > 0 && limits.historyEntries <= 32
        && limits.historyBytes > 0 && limits.historyBytes <= 256 * 1024
        && limits.dormantParts <= 64 && limits.dormantBuilds <= 8;
}
bool canPay(ResourceAmounts balance, ResourceAmounts price) {
    return balance.salvageMaterial >= price.salvageMaterial
        && balance.specialMachinery >= price.specialMachinery;
}
std::optional<ResourceAmounts> credited(ResourceAmounts balance, ResourceAmounts amount) {
    constexpr auto maximum = std::numeric_limits<uint64_t>::max();
    if (amount.salvageMaterial > maximum - balance.salvageMaterial
        || amount.specialMachinery > maximum - balance.specialMachinery) return std::nullopt;
    return ResourceAmounts{balance.salvageMaterial + amount.salvageMaterial,
                           balance.specialMachinery + amount.specialMachinery};
}
const BuildTarget* targetOf(const Intent& intent) {
    return std::visit([](const auto& value) -> const BuildTarget* {
        if constexpr (requires { value.target; }) return &value.target;
        else return nullptr;
    }, intent);
}
bool isCompensation(const Intent& intent) noexcept {
    return std::holds_alternative<Undo>(intent) || std::holds_alternative<Redo>(intent);
}
BuildHeader headerOf(const BuildSnapshot& build) noexcept {
    return {build.id, build.revision, build.owner, build.editLease, true};
}
// Private changed records, never a whole-world undo snapshot. Each dormant ID
// owns one current object image; history images are references to that object.
using HistoryEntry = RecoveryHistoryEntry;
template<class T, size_t N>
void eraseFixed(std::array<T, N>& values, size_t& count, size_t index) noexcept {
    for (size_t i = index + 1; i < count; ++i) values[i - 1] = values[i];
    values[--count] = {};
}
struct WorkshopHistory : RecoveryHistory {
    size_t payloadBytes() const noexcept {
        return historyPayloadBytes(*this);
    }
    void prune() noexcept {
        for (size_t i = 0; i < partCount;) {
            bool referenced = false;
            for (size_t j = 0; j < count; ++j) {
                const auto& edit = entries[j].edit;
                referenced |= referencesPart(edit,parts[i].id);
            }
            if (!referenced) eraseFixed(parts, partCount, i); else ++i;
        }
        for (size_t i = 0; i < buildCount;) {
            bool referenced = false;
            for (size_t j = 0; j < count; ++j) referenced |= entries[j].edit.after->id == builds[i].id;
            if (!referenced) eraseFixed(builds, buildCount, i); else ++i;
        }
    }
};
auto findBuild(auto& builds, DurableId id) {
    return std::find_if(builds.begin(), builds.end(), [id](const auto& build) {
        return build.snapshot().id == id;
    });
}
uint32_t partCount(const std::vector<BuildModel>& builds) {
    uint32_t count = 0;
    for (const auto& build : builds) count += static_cast<uint32_t>(build.snapshot().parts.size());
    return count;
}
uint32_t connectionCount(const std::vector<BuildModel>& builds) {
    uint32_t count = 0;
    for (const auto& build : builds) count += static_cast<uint32_t>(build.snapshot().connections.size());
    return count;
}
} // namespace

struct GameSession::State {
    SessionRevision revision{};
    ResourceAmounts inventory{};
    std::vector<BuildModel> builds{};
    std::vector<DurableId> entitlements{};
    std::vector<StarterKit> starterKits{};
    std::vector<construction::PartInstance> storedParts{};
    std::vector<CargoDefinition> cargoDefinitions{};
    std::vector<CargoRecord> cargo{};
    std::vector<JobRecord> jobs{};
    WorkshopHistory history{};
};
struct GameSession::Pending {
    Command command{};
    Receipt receipt{};
    std::unique_ptr<State> candidate{};
    PreparationTicket ticket{};
    PreparationState preparation = PreparationState::Pending;
    bool adapterStarted = false;
    bool rejected = false;
    SessionIssue finalIssue{};
    JournalReservation journalReservation{};
    ResourceAmounts reservedDebit{};
    uint32_t reservedParts = 0, reservedBuilds = 0;
    ObjectTransition transition{};
    HistoryTransition historyTransition{};
    std::optional<HistoryEntryId> reservedHistory{};
    std::optional<DurableId> removedBuild{};
    std::optional<DurableId> changedBuild{};
    std::optional<DurableId> committedObject{};
    AssignedIdRange assignedRange{};
};

GameSession::GameSession(const SessionBootstrap& boot, const PartCatalog& catalog, PreparationAdapter& adapter)
    : catalog_(catalog), adapter_(adapter), world_(boot.world), caller_(boot.caller),
      epoch_(boot.epoch), limits_(boot.limits), workshopEnabled_(boot.workshopEnabled),
      ids_(boot.world, boot.lastIssuedId), tick_(boot.tick), reservedThroughId_(boot.lastIssuedId),
      state_(std::make_unique<State>()) {}

std::unique_ptr<GameSession> GameSession::createBase(
    const SessionBootstrap& boot, const PartCatalog& catalog, PreparationAdapter& adapter, SessionIssue& issue) {
    try {
        auto session = std::unique_ptr<GameSession>(new GameSession(boot, catalog, adapter));
        issue = session->validateBootstrap(boot);
        if (issue) return nullptr;
        session->receipts_.reserve(boot.limits.receipts);
        return session;
    } catch (const std::bad_alloc&) {
        issue = fail(SessionError::Capacity);
        return nullptr;
    }
}
bool GameSession::initializeJournal(JournalIdentity identity, std::span<const JournalRecord> initial, SessionIssue& issue) {
    JournalError journalIssue;
    journal_ = JournalOutbox::create(identity, {limits_.journalRecords, limits_.journalBytes}, {}, journalIssue);
    if (!journal_) { issue = fail(SessionError::JournalCapacity); return false; }
    const auto closure = journal_->reserve({1, kJournalRecordStorageBytes});
    if (!closure) { issue = fail(SessionError::JournalCapacity); return false; }
    closureReservation_ = closure.reservation;
    for (const auto& assigned : initial) {
        const auto slot = journal_->reserve({1, journalRecordBytes(assigned)});
        if (!slot) { issue = fail(SessionError::JournalCapacity); return false; }
        auto record = assigned; record.sequence = {};
        const auto sequence = journal_->appendReserved(slot.reservation, record);
        static_cast<void>(journal_->discard(slot.reservation));
        if (sequence != assigned.sequence) { issue = fail(SessionError::JournalCapacity); return false; }
    }
    return true;
}
std::unique_ptr<GameSession> GameSession::create(
    const SessionBootstrap& boot, EventStreamIncarnation incarnation,
    const PartCatalog& catalog, PreparationAdapter& adapter, SessionIssue& issue) {
    const EventStreamIdentity stream{boot.world, boot.epoch, incarnation};
    if (!isValid(stream)) { issue = fail(SessionError::InvalidIdentity); return nullptr; }
    auto session = createBase(boot, catalog, adapter, issue);
    if (!session || !session->initializeJournal({boot.world, boot.epoch, JournalWriterGeneration{1}}, {}, issue)) return nullptr;
    session->events_ = SessionEventHub::create(stream);
    if (!session->events_) { issue = fail(SessionError::Capacity); return nullptr; }
    return session;
}

SessionIssue GameSession::validateInitialState(const SessionBootstrap& boot, const PartCatalog& catalog) {
    class NoPreparation final : public PreparationAdapter {
    public:
        PreparationResult begin(const PreparationRequest& request) override { return {request.ticket, PreparationState::Rejected}; }
        PreparationResult poll(PreparationTicket ticket) noexcept override { return {ticket, PreparationState::Rejected}; }
        bool canActivate(PreparationTicket) const noexcept override { return false; }
        void activate(PreparationTicket, SimulationTick) noexcept override { std::terminate(); }
        void discard(PreparationTicket) noexcept override {}
    } adapter;
    SessionIssue issue;
    const auto checked = createBase(boot, catalog, adapter, issue);
    return issue;
}

SessionIssue GameSession::validateBootstrap(const SessionBootstrap& boot) {
    if (!construction::isValid(world_) || !epoch_.valid() || !validLimits(limits_))
        return fail(SessionError::InvalidBootstrap);
    if (boot.builds.size() > limits_.builds || boot.cargo.size() > limits_.cargo
        || boot.jobs.size() > limits_.jobs || boot.starterEntitlements.size() > 256
        || boot.cargoDefinitions.size() > 256 || boot.starterKits.size()>kMaximumStarterKits
        || boot.storedParts.size()>construction::kMaximumStoredParts) return fail(SessionError::Capacity);
    const auto validReference = [&](DurableId id) {
        return construction::isValid(id) && id.world == world_ && id.counter <= boot.lastIssuedId;
    };
    if (!validReference(caller_.participant) || !validReference(caller_.sessionToken))
        return fail(SessionError::InvalidIdentity);
    std::vector<DurableId> issued{caller_.participant, caller_.sessionToken};
    const auto object = [&](DurableId id) {
        if (!validReference(id)) return false;
        issued.push_back(id);
        return true;
    };
    for (const auto entitlement : boot.starterEntitlements)
        if (!object(entitlement)) return fail(SessionError::InvalidIdentity);
    state_->builds.reserve(boot.builds.size());
    for (const auto& draft : boot.builds) {
        if (!object(draft.id) || draft.owner != caller_.participant
            || (draft.editLease && draft.editLease->holder != caller_.participant))
            return fail(SessionError::InvalidIdentity);
        construction::BuildIssue buildIssue;
        auto model = BuildModel::create(draft, catalog_, buildIssue);
        if (!model) return {SessionError::InvalidBuild, buildIssue};
        for (const auto& part : draft.parts) {
            if (!object(part.id)) return fail(SessionError::InvalidIdentity);
            if (part.provenance.origin == construction::PartOrigin::StarterLoan
                && std::find(boot.starterEntitlements.begin(), boot.starterEntitlements.end(),
                    part.provenance.starterEntitlement) == boot.starterEntitlements.end())
                return fail(SessionError::InvalidIdentity);
        }
        for (const auto& connection : draft.connections)
            if (!object(connection.id)) return fail(SessionError::InvalidIdentity);
        state_->builds.push_back(std::move(*model));
        if (partCount(state_->builds) > limits_.totalParts) return fail(SessionError::Capacity);
    }
    if(partCount(state_->builds)+boot.storedParts.size()>limits_.totalParts)return fail(SessionError::Capacity);
    for(const auto& part:boot.storedParts){
        const auto owner=findBuild(state_->builds,part.owningBuild);
        if(owner==state_->builds.end()||!object(part.id)||part.provenance!=construction::PartProvenance{})
            return fail(SessionError::InvalidIdentity);
        auto stock=owner->snapshot();stock.parts={part};stock.connections.clear();
        construction::BuildIssue issue;
        if(!BuildModel::create(stock,catalog_,issue))return {SessionError::InvalidBuild,issue};
    }
    for(size_t index=0;index<boot.starterKits.size();++index){
        const auto& kit=boot.starterKits[index];const auto build=findBuild(state_->builds,kit.build);
        if(build==state_->builds.end()||!kit.design||kit.design->parts().empty()||kit.design->parts().size()>32
            ||kit.design->welds().size()>64||kit.partCount!=kit.design->parts().size()||!validReference(kit.entitlement)
            ||std::find(boot.starterEntitlements.begin(),boot.starterEntitlements.end(),kit.entitlement)==boot.starterEntitlements.end())
            return fail(SessionError::InvalidBootstrap);
        for(size_t previous=0;previous<index;++previous)
            if(boot.starterKits[previous].build==kit.build||boot.starterKits[previous].entitlement==kit.entitlement)
                return fail(SessionError::InvalidBootstrap);
        for(size_t unused=kit.partCount;unused<kit.partIds.size();++unused)
            if(kit.partIds[unused]!=DurableId{})return fail(SessionError::InvalidIdentity);
        for(size_t ordinal=0;ordinal<kit.partCount;++ordinal){
            const auto granted=kit.partIds[ordinal];
            if(!validReference(granted)||granted==caller_.participant||granted==caller_.sessionToken
                ||std::find(boot.starterEntitlements.begin(),boot.starterEntitlements.end(),granted)!=boot.starterEntitlements.end())
                return fail(SessionError::InvalidIdentity);
            for(size_t prior=0;prior<ordinal;++prior)if(kit.partIds[prior]==granted)return fail(SessionError::InvalidIdentity);
            for(size_t prior=0;prior<index;++prior)
                if(std::find(boot.starterKits[prior].partIds.begin(),boot.starterKits[prior].partIds.begin()+boot.starterKits[prior].partCount,granted)!=boot.starterKits[prior].partIds.begin()+boot.starterKits[prior].partCount)
                    return fail(SessionError::InvalidIdentity);
            for(const auto& owned:state_->builds){
                if(owned.snapshot().id==granted)return fail(SessionError::InvalidIdentity);
                for(const auto& connection:owned.snapshot().connections)if(connection.id==granted)return fail(SessionError::InvalidIdentity);
                for(const auto& part:owned.snapshot().parts)if(part.id==granted){
                    if(part.owningBuild!=kit.build||part.provenance!=construction::PartProvenance{construction::PartOrigin::StarterLoan,kit.entitlement}
                        ||part.definition!=kit.design->parts()[ordinal].design.definition)return fail(SessionError::InvalidIdentity);
                }
            }
            if(std::any_of(boot.storedParts.begin(),boot.storedParts.end(),[&](const auto& part){return part.id==granted;})
                ||std::any_of(boot.cargo.begin(),boot.cargo.end(),[&](const auto& cargo){return cargo.id==granted;})
                ||std::any_of(boot.jobs.begin(),boot.jobs.end(),[&](const auto& job){return job.id==granted;}))return fail(SessionError::InvalidIdentity);
        }
        // Validate the immutable recipe with temporary IDs only. This empty
        // model never enters accepted state or consumes the world's allocator.
        BuildSnapshot empty;empty.id=kit.build;empty.owner=build->snapshot().owner;
        construction::BuildIssue issue;const auto model=BuildModel::create(empty,catalog_,issue);
        if(!model)return {SessionError::InvalidBuild,issue};
        uint64_t temporary=0;
        const auto allocate=[&]()->std::optional<DurableId>{
            DurableId id;do{id={world_,++temporary};}while(id==empty.id||id==empty.owner||id==kit.entitlement);return id;
        };
        if(!construction::prepareBuildRefit(*model,*kit.design,catalog_,allocate,issue,{{},kit.entitlement}))
            return {SessionError::InvalidBuild,issue};
        for(const auto& owned:state_->builds)for(const auto& part:owned.snapshot().parts){
            if(part.provenance.origin!=construction::PartOrigin::StarterLoan||part.provenance.starterEntitlement!=kit.entitlement)continue;
            if(part.owningBuild!=kit.build||std::find(kit.partIds.begin(),kit.partIds.begin()+kit.partCount,part.id)==kit.partIds.begin()+kit.partCount)return fail(SessionError::InvalidIdentity);
            const auto quota=std::count_if(kit.design->parts().begin(),kit.design->parts().end(),[&](const auto& p){return p.design.definition==part.definition;});
            const auto active=std::count_if(owned.snapshot().parts.begin(),owned.snapshot().parts.end(),[&](const auto& p){
                return p.provenance==part.provenance&&p.definition==part.definition;});
            if(active>quota)return fail(SessionError::InvalidIdentity);
        }
    }
    for (size_t i = 0; i < boot.cargoDefinitions.size(); ++i) {
        const auto& definition = boot.cargoDefinitions[i];
        if (!construction::isValid(definition.key.id) || definition.key.version == 0
            || !std::isfinite(definition.massKg) || definition.massKg <= 0
            || !std::isfinite(definition.displacedVolumeCubicMetres) || definition.displacedVolumeCubicMetres < 0
            || (definition.recovery != CargoRecoveryRule::ReturnToSite
                && definition.recovery != CargoRecoveryRule::PreserveUnique))
            return fail(SessionError::InvalidBootstrap);
        for (size_t j = 0; j < i; ++j)
            if (boot.cargoDefinitions[j].key == definition.key) return fail(SessionError::InvalidBootstrap);
    }
    for (const auto& job : boot.jobs) {
        if (!object(job.id) || job.generation == 0
            || (job.phase == JobPhase::Available && job.acceptedBy != DurableId{})
            || (job.phase != JobPhase::Available && job.acceptedBy != caller_.participant)
            || (job.phase != JobPhase::Available && job.phase != JobPhase::Accepted && job.phase != JobPhase::Completed))
            return fail(SessionError::InvalidBootstrap);
    }
    for (const auto& cargo : boot.cargo) {
        if (!object(cargo.id) || cargo.owner != caller_.participant
            || !std::isfinite(cargo.position.x) || !std::isfinite(cargo.position.y) || !std::isfinite(cargo.position.z)
            || !construction::isCanonical(cargo.orientation)) return fail(SessionError::InvalidBootstrap);
        if (std::none_of(boot.cargoDefinitions.begin(), boot.cargoDefinitions.end(),
            [&](const auto& definition) { return definition.key == cargo.definition; }))
            return fail(SessionError::InvalidBootstrap);
        if (cargo.job && std::none_of(boot.jobs.begin(), boot.jobs.end(),
            [&](const auto& job) { return job.id == *cargo.job && job.phase!=JobPhase::Completed; })) return fail(SessionError::InvalidBootstrap);
    }
    std::sort(issued.begin(), issued.end());
    if (std::adjacent_find(issued.begin(), issued.end()) != issued.end()) return fail(SessionError::InvalidIdentity);
    std::sort(state_->builds.begin(), state_->builds.end(), [](const auto& a, const auto& b) {
        return a.snapshot().id < b.snapshot().id;
    });
    state_->revision = boot.revision;
    state_->inventory = boot.inventory;
    state_->entitlements = boot.starterEntitlements;
    state_->starterKits=boot.starterKits;state_->storedParts=boot.storedParts;
    std::sort(state_->storedParts.begin(),state_->storedParts.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    state_->cargoDefinitions = boot.cargoDefinitions;
    state_->cargo = boot.cargo;
    state_->jobs = boot.jobs;
    std::sort(state_->cargo.begin(), state_->cargo.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(state_->jobs.begin(), state_->jobs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return {};
}

GameSession::~GameSession() { closeAdmission(); }
uint64_t GameSession::lastIssuedId() const noexcept { return ids_.lastIssued(); }
SessionError GameSession::authenticate(CallerContext caller) const noexcept {
    if (caller.participant != caller_.participant) return SessionError::WrongCaller;
    if (caller.sessionToken != caller_.sessionToken) return SessionError::WrongToken;
    return SessionError::None;
}
RequestSequence GameSession::expectedSequence() const noexcept {
    return admittedSequence_ == std::numeric_limits<uint64_t>::max()
        ? RequestSequence{} : RequestSequence{admittedSequence_ + 1};
}
Receipt GameSession::rejected(RequestSequence sequence, SessionError error) const noexcept {
    return {sequence, ReceiptState::Rejected, fail(error), false, expectedSequence(), state_->revision, {}, {}};
}
void GameSession::retain(const Command& command, Receipt result) noexcept {
    processedSequence_ = command.sequence.value();
    result.expectedSequence = expectedSequence();
    size_t bytes=ownedExtraBytes(command);
    for(const auto& retained:receipts_)bytes+=ownedExtraBytes(retained.command);
    while(!receipts_.empty() && (receipts_.size()>=limits_.receipts || bytes>kMaximumReceiptOwnedBytes)) {
        bytes-=ownedExtraBytes(receipts_.front().command);receipts_.erase(receipts_.begin());
    }
    receipts_.push_back({command, result}); // Capacity reserved at construction.
}
Receipt GameSession::receipt(CallerContext caller, RequestSequence sequence) const {
    if (const auto error = authenticate(caller); error != SessionError::None) return rejected(sequence, error);
    if (!sequence.valid()) return rejected(sequence, SessionError::InvalidIdentity);
    for (size_t i = 0; i < pendingCount_; ++i)
        if (pending_[i]->command.sequence == sequence) return pending_[i]->receipt;
    for (const auto& retained : receipts_) if (retained.command.sequence == sequence) return retained.receipt;
    return rejected(sequence, sequence.value() <= processedSequence_ ? SessionError::AlreadyProcessed : SessionError::NoPending);
}

SessionIssue GameSession::checkTarget(const BuildTarget& target, const State& state, SimulationTick atTick, bool allowDormant) const {
    const auto build = findBuild(state.builds, target.build);
    std::optional<BuildHeader> header;
    if (build != state.builds.end()) header = headerOf(build->snapshot());
    else if (allowDormant) {
        for (size_t i = 0; i < state.history.buildCount; ++i)
            if (state.history.builds[i].id == target.build) header = state.history.builds[i];
    }
    if (!header) return fail(SessionError::UnknownBuild);
    const auto& snapshot = *header;
    if (snapshot.owner != caller_.participant) return fail(SessionError::NotOwner);
    if (snapshot.revision != target.expectedRevision) return fail(SessionError::StaleRevision);
    if (snapshot.editLease && (snapshot.editLease->holder != caller_.participant
        || snapshot.editLease->epoch != epoch_ || atTick >= snapshot.editLease->expiresAfter))
        return fail(SessionError::LeaseDenied);
    return {};
}

SessionIssue GameSession::prepare(const Command& command, Pending& pending) {
    if (command.expectedRevision != state_->revision) return fail(SessionError::StaleRevision);
    const auto revision = construction::next(state_->revision);
    if (!revision) return fail(SessionError::RevisionExhausted);
    if (!std::holds_alternative<AcceptJob>(command.intent) && !std::holds_alternative<DeliverCargo>(command.intent)
        && !std::holds_alternative<CutWeld>(command.intent) && !workshopEnabled_)
        return fail(SessionError::UnsupportedFeature);
    if (const auto* target = targetOf(command.intent)) {
        if (const auto issue = checkTarget(*target, *state_, tick_, isCompensation(command.intent))) return issue;
        if (!construction::next(target->expectedRevision)) return fail(SessionError::RevisionExhausted);
    }
    size_t candidateBytes = sizeof(State) + ownedExtraBytes(state_->history) + ownedExtraBytes(command) + state_->cargo.size() * sizeof(CargoRecord)
        + state_->cargoDefinitions.size() * sizeof(CargoDefinition) + state_->jobs.size() * sizeof(JobRecord)
        + state_->entitlements.size() * sizeof(DurableId)
        +state_->storedParts.size()*sizeof(construction::PartInstance)+state_->starterKits.size()*sizeof(StarterKit)
        +starterKitPayloadBytes(state_->starterKits);
    for (const auto& value : state_->builds) candidateBytes += construction::kBuildHeaderBytes
        + value.snapshot().parts.size() * construction::kBuildPartBytes
        + value.snapshot().connections.size() * construction::kBuildConnectionBytes;
    size_t addedBytes = std::holds_alternative<AddPart>(command.intent) ? construction::kBuildPartBytes
        : std::holds_alternative<CreateBuild>(command.intent) ? construction::kBuildHeaderBytes
        : isCompensation(command.intent) ? construction::kBuildHeaderBytes + construction::kBuildPartBytes : 0;
    if(const auto* design=refitDesign(command.intent,state_->starterKits))
        addedBytes+=design->parts().size()*construction::kBuildPartBytes
            +design->welds().size()*construction::kBuildConnectionBytes+construction::kMaximumRefitDeltaBytes
            +kMaximumStorageTransitionBytes;
    if(isCompensation(command.intent))addedBytes+=construction::kMaximumRefitDeltaBytes;
    if(std::holds_alternative<CutWeld>(command.intent))addedBytes+=sizeof(WeldCutTransition);
    if (candidateBytes > limits_.candidateBytes || addedBytes > limits_.candidateBytes - candidateBytes)
        return fail(SessionError::CandidateCapacity);
    pending.candidate = std::make_unique<State>(*state_);
    auto& candidate = *pending.candidate;
    candidate.revision = *revision;
    if (isCompensation(command.intent)) return prepareCompensation(command, pending);
    if (std::holds_alternative<CreateBuild>(command.intent)) {
        size_t reservedBuilds = 0;
        for (size_t i = 0; i < pendingCount_; ++i) reservedBuilds += pending_[i]->reservedBuilds;
        if (candidate.builds.size() + reservedBuilds >= limits_.builds) return fail(SessionError::Capacity);
        const auto id = allocateId(pending);
        if (!id) return fail(SessionError::IdExhausted);
        BuildSnapshot draft;
        draft.id = *id;
        draft.owner = caller_.participant;
        construction::BuildIssue issue;
        auto model = BuildModel::create(draft, catalog_, issue);
        if (!model) return {SessionError::InvalidBuild, issue};
        candidate.builds.push_back(std::move(*model));
        pending.changedBuild = *id;
        pending.committedObject = *id;
        pending.reservedBuilds = 1;
        pending.transition = BuildTransition{{}, BuildHeader{draft.id, draft.revision, draft.owner, draft.editLease, true}, {}, {}};
        return {};
    }
    if (const auto* accept = std::get_if<AcceptJob>(&command.intent)) {
        const auto job = std::find_if(candidate.jobs.begin(), candidate.jobs.end(),
            [&](const auto& value) { return value.id == accept->job; });
        if (job == candidate.jobs.end()) return fail(SessionError::UnknownJob);
        if (job->phase != JobPhase::Available) return fail(SessionError::JobUnavailable);
        const auto before = *job;
        job->phase = JobPhase::Accepted;
        job->acceptedBy = caller_.participant;
        pending.committedObject = job->id;
        pending.transition = JobTransition{before, *job};
        return {};
    }
    if(const auto* deliver=std::get_if<DeliverCargo>(&command.intent)) {
        const auto cargo=std::find_if(candidate.cargo.begin(),candidate.cargo.end(),[&](const auto& c){return c.id==deliver->cargo;});
        if(cargo==candidate.cargo.end()) return fail(SessionError::UnknownCargo);
        if(cargo->owner!=caller_.participant || !cargo->job
            || std::count_if(candidate.cargo.begin(),candidate.cargo.end(),[&](const auto& c){return c.job==cargo->job;})!=1)
            return fail(SessionError::CargoUnavailable);
        const auto job=std::find_if(candidate.jobs.begin(),candidate.jobs.end(),[&](const auto& j){return j.id==*cargo->job;});
        if(job==candidate.jobs.end() || job->phase!=JobPhase::Accepted || job->acceptedBy!=caller_.participant)
            return fail(SessionError::CargoUnavailable);
        const auto definition=std::find_if(candidate.cargoDefinitions.begin(),candidate.cargoDefinitions.end(),
            [&](const auto& d){return d.key==cargo->definition;});
        if(definition==candidate.cargoDefinitions.end()) return fail(SessionError::CargoUnavailable);
        const auto balance=credited(candidate.inventory,definition->value);
        if(!balance) return fail(SessionError::ResourceOverflow);
        CargoDeliveryTransition transition{*cargo,*job,*job,definition->value};
        transition.after.phase=JobPhase::Completed;
        candidate.inventory=*balance;*job=transition.after;
        pending.committedObject=cargo->id;pending.transition=transition;
        candidate.cargo.erase(cargo);
        return {};
    }
    const auto* target = targetOf(command.intent);
    if (!target) return fail(SessionError::UnsupportedFeature);
    const auto build = findBuild(candidate.builds, target->build);
    if (build == candidate.builds.end()) return fail(SessionError::UnknownBuild);
    auto draft = build->snapshot();
    BuildTransition transition;
    transition.before = BuildHeader{draft.id, draft.revision, draft.owner, draft.editLease, true};
    if(const auto* cut=std::get_if<CutWeld>(&command.intent)) {
        const auto before=std::find_if(draft.connections.begin(),draft.connections.end(),[&](const auto& c){return c.id==cut->connection;});
        if(before==draft.connections.end())return {SessionError::InvalidBuild,{construction::BuildError::InvalidConnection,cut->connection,"cut.unknown"}};
        transition.cut=std::make_shared<const WeldCutTransition>(WeldCutTransition{*before});
        const std::array cuts{cut->connection};
        if(const auto issue=build->cutWelds(target->expectedRevision,cuts,catalog_))return {SessionError::InvalidBuild,issue};
        const auto& after=build->snapshot();
        transition.after=BuildHeader{after.id,after.revision,after.owner,after.editLease,true};
        pending.changedBuild=after.id;pending.committedObject=cut->connection;pending.transition=std::move(transition);
        return {};
    }
    if(isRefitIntent(command.intent)) {
        const auto* design=refitDesign(command.intent,candidate.starterKits);
        if(!design)return fail(SessionError::InvalidBuild);
        const bool rebuild=std::holds_alternative<RebuildStarter>(command.intent);
        const auto kit=std::find_if(candidate.starterKits.begin(),candidate.starterKits.end(),[&](const auto& k){return k.build==target->build;});
        const auto entitlement=rebuild?std::optional{kit->entitlement}:std::nullopt;
        uint32_t held=0;
        for(size_t i=0;i<pendingCount_;++i)held+=pending_[i]->reservedParts;
        const auto total=partCount(candidate.builds)-draft.parts.size()+design->parts().size();
        size_t storageAfter=candidate.storedParts.size();
        if(rebuild)storageAfter+=static_cast<size_t>(std::count_if(draft.parts.begin(),draft.parts.end(),
            [](const auto& p){return p.provenance.origin==construction::PartOrigin::Paid;}));
        else for(const auto& p:candidate.storedParts)
            if(std::any_of(design->parts().begin(),design->parts().end(),[&](const auto& r){return r.source==p.id;}))--storageAfter;
        if(total+storageAfter+held>limits_.totalParts)return fail(SessionError::Capacity);
        // Record leases before supplying ANY IDs. Thus a replay checkpoint at
        // any lease boundary cannot mistake earlier IDs in this request for
        // preexisting objects. All preparation IDs remain one contiguous range.
        const uint64_t maximumIds=static_cast<uint64_t>(std::count_if(design->parts().begin(),design->parts().end(),
            [](const auto& part){return part.source==DurableId{};}))+design->welds().size();
        while(reservedThroughId_-ids_.lastIssued()<maximumIds && reservedThroughId_!=std::numeric_limits<uint64_t>::max()) {
            const auto amount=std::min(uint64_t{64},std::numeric_limits<uint64_t>::max()-reservedThroughId_);
            JournalRecord lease{kLogicalJournalSchema,world_,epoch_,{},tick_,
                AllocatorLeaseRecord{ids_.lastIssued(),reservedThroughId_,reservedThroughId_+amount}};
            if(!journal_->appendReserved(pending.journalReservation,lease)) {journalFault_=true;return fail(SessionError::JournalCapacity);}
            reservedThroughId_+=amount;
        }
        construction::BuildIssue buildIssue;
        const auto plan=construction::prepareBuildRefit(*build,*design,catalog_,[&]{return allocateId(pending);},buildIssue,{candidate.storedParts,entitlement});
        if(!plan)return {buildIssue.field=="refit.newPartId" || buildIssue.field=="refit.newWeldId"
            ?SessionError::IdExhausted:SessionError::InvalidBuild,buildIssue};
        const auto net=[](uint64_t debit,uint64_t credit){return debit>credit?debit-credit:0;};
        const ResourceAmounts debit{net(plan->debit.salvageMaterial,plan->credit.salvageMaterial),
            net(plan->debit.specialMachinery,plan->credit.specialMachinery)};
        const ResourceAmounts credit{net(plan->credit.salvageMaterial,plan->debit.salvageMaterial),
            net(plan->credit.specialMachinery,plan->debit.specialMachinery)};
        if(const auto issue=reserveDebit(pending,debit))return issue;
        const auto balance=credited({candidate.inventory.salvageMaterial-debit.salvageMaterial,
            candidate.inventory.specialMachinery-debit.specialMachinery},credit);
        if(!balance)return fail(SessionError::ResourceOverflow);
        if(rebuild||!plan->consumedStoredParts.empty()) {
            auto storage=std::make_shared<PartStorageTransition>();
            if(rebuild) {
                storage->starterBefore=*kit;
                for(const auto& part:draft.parts)
                    if(part.provenance.origin==construction::PartOrigin::Paid)storage->deposited.push_back(part);
                kit->partIds={};kit->partCount=static_cast<uint8_t>(design->parts().size());
                std::copy_n(plan->createdIds.begin(),kit->partCount,kit->partIds.begin());
            }
            for(const auto& part:candidate.storedParts)
                if(std::find(plan->consumedStoredParts.begin(),plan->consumedStoredParts.end(),part.id)!=plan->consumedStoredParts.end())
                    storage->withdrawn.push_back(part);
            if(storage->retainedBytes()>kMaximumStorageTransitionBytes)return fail(SessionError::CandidateCapacity);
            transition.storage=std::move(storage);
        }
        if(kJournalRecordStorageBytes+ownedExtraBytes(command)+plan->delta->retainedBytes()
            +(transition.storage?transition.storage->retainedBytes():0)>kMaximumJournalRecordBytes)
            return fail(SessionError::JournalCapacity);
        candidate.inventory=*balance;
        const auto ownedBefore=partCount(candidate.builds)+candidate.storedParts.size();
        const auto ownedAfter=total+plan->storedPartsAfter.size();
        pending.reservedParts=ownedAfter>ownedBefore?static_cast<uint32_t>(ownedAfter-ownedBefore):0;
        candidate.storedParts=plan->storedPartsAfter;
        draft=plan->after;draft.revision=target->expectedRevision;
        transition.refit=plan->delta;pending.committedObject=draft.id;
    } else if (const auto* add = std::get_if<AddPart>(&command.intent)) {
        uint32_t reservedParts = 0;
        for (size_t i = 0; i < pendingCount_; ++i) reservedParts += pending_[i]->reservedParts;
        if (partCount(candidate.builds) + candidate.storedParts.size() + reservedParts >= limits_.totalParts) return fail(SessionError::Capacity);
        const auto lookup = catalog_.lookup(add->definition);
        if (!lookup.definition) return fail(SessionError::InvalidBuild);
        if (const auto issue = reserveDebit(pending, lookup.definition->cost)) return issue;
        const auto id = allocateId(pending);
        if (!id) return fail(SessionError::IdExhausted);
        construction::PartInstance part;
        part.id = *id;
        part.definition = add->definition;
        part.placement = add->placement;
        part.owningBuild = draft.id;
        part.settings = construction::defaultModuleSettings(*lookup.definition);
        draft.parts.push_back(part);
        transition.afterPart = part;
        pending.reservedDebit = lookup.definition->cost;
        pending.reservedParts = 1;
        candidate.inventory.salvageMaterial -= lookup.definition->cost.salvageMaterial;
        candidate.inventory.specialMachinery -= lookup.definition->cost.specialMachinery;
        pending.committedObject = *id;
    } else {
        const auto* move = std::get_if<MovePart>(&command.intent);
        const auto* remove = std::get_if<RemovePart>(&command.intent);
        if (!move && !remove) return fail(SessionError::UnsupportedFeature);
        const auto partId = move ? move->part : remove->part;
        const auto part = std::find_if(draft.parts.begin(), draft.parts.end(),
            [partId](const auto& value) { return value.id == partId; });
        if (part == draft.parts.end()) return fail(SessionError::UnknownPart);
        transition.beforePart = *part;
        if (move) { part->placement = move->placement; transition.afterPart = *part; }
        else {
            if (std::any_of(draft.connections.begin(), draft.connections.end(), [&](const auto& connection) {
                return connection.a.part == partId || connection.b.part == partId;
            })) return fail(SessionError::ConnectedPart);
            const auto lookup = catalog_.lookup(part->definition);
            if (!lookup.definition) return fail(SessionError::InvalidBuild);
            const auto yield = part->provenance.origin == construction::PartOrigin::StarterLoan
                ? ResourceAmounts{} : lookup.definition->salvageYield;
            const auto balance = credited(candidate.inventory, yield);
            if (!balance) return fail(SessionError::ResourceOverflow);
            candidate.inventory = *balance;
            draft.parts.erase(part);
        }
        pending.committedObject = partId;
    }
    if (const auto issue = build->replace(target->expectedRevision, draft, catalog_))
        return {SessionError::InvalidBuild, issue};
    pending.changedBuild = draft.id;
    const auto& after = build->snapshot();
    transition.after = BuildHeader{after.id, after.revision, after.owner, after.editLease, true};
    pending.transition = transition;
    return {};
}

SessionIssue GameSession::reserveDebit(Pending& pending, ResourceAmounts debit) const {
    auto available = state_->inventory;
    for (size_t i = 0; i < pendingCount_; ++i) {
        const auto held = pending_[i]->reservedDebit;
        if (!canPay(available, held)) return fail(SessionError::InsufficientResources);
        available.salvageMaterial -= held.salvageMaterial;
        available.specialMachinery -= held.specialMachinery;
    }
    if (!canPay(available, debit)) return fail(SessionError::InsufficientResources);
    pending.reservedDebit = debit;
    return {};
}

SessionIssue GameSession::evictHistoryFront(Pending& pending) {
    auto& history = pending.candidate->history;
    const auto entry = history.entries[0].id;
    for (size_t i = 0; i < pendingCount_; ++i)
        if (pending_[i]->reservedHistory == entry) return fail(SessionError::HistoryBusy);
    auto& change = pending.historyTransition;
    if (change.evictedCount == change.evicted.size()) return fail(SessionError::HistoryCapacity);
    change.evicted[change.evictedCount++] = entry;
    eraseFixed(history.entries, history.count, 0);
    if (history.applied != 0) --history.applied;
    history.prune();
    return {};
}

SessionIssue GameSession::recordHistory(Pending& pending) {
    const auto* edit = std::get_if<BuildTransition>(&pending.transition);
    if (!edit || isCompensation(pending.command.intent)) return {};
    auto& history = pending.candidate->history;
    const auto generation = construction::next(history.generation);
    if (!generation) return fail(SessionError::HistoryExhausted);
    auto& change = pending.historyTransition;
    if(edit->storage||edit->cut) {
        change={history.generation,*generation,HistoryAction::Service,{}};
        for(size_t i=0;i<history.count;) {
            if(history.entries[i].edit.after->id!=edit->after->id){++i;continue;}
            const auto entry=history.entries[i].id;
            for(size_t j=0;j<pendingCount_;++j)
                if(pending_[j]->reservedHistory==entry)return fail(SessionError::HistoryBusy);
            change.evicted[change.evictedCount++]=entry;
            eraseFixed(history.entries,history.count,i);
            if(i<history.applied)--history.applied;
        }
        history.prune();history.generation=*generation;return {};
    }
    change = {history.generation, *generation, HistoryAction::Record, HistoryEntryId{generation->value()}};
    // Removing redo is staged in the candidate. Every canceled/failed edit
    // leaves the accepted cursor and all its retained objects untouched.
    while (history.count > history.applied) {
        const auto entry = history.entries[history.count - 1].id;
        for (size_t i = 0; i < pendingCount_; ++i)
            if (pending_[i]->reservedHistory == entry) return fail(SessionError::HistoryBusy);
        change.evicted[change.evictedCount++] = entry;
        history.entries[--history.count] = {};
    }
    history.prune();
    while (history.count >= limits_.historyEntries)
        if (const auto issue = evictHistoryFront(pending)) return issue;
    const auto before = state_->inventory;
    const auto after = pending.candidate->inventory;
    const ResourceAmounts debit{before.salvageMaterial > after.salvageMaterial ? before.salvageMaterial - after.salvageMaterial : 0,
        before.specialMachinery > after.specialMachinery ? before.specialMachinery - after.specialMachinery : 0};
    const ResourceAmounts credit{after.salvageMaterial > before.salvageMaterial ? after.salvageMaterial - before.salvageMaterial : 0,
        after.specialMachinery > before.specialMachinery ? after.specialMachinery - before.specialMachinery : 0};
    history.entries[history.count++] = {*change.entry, caller_, admissionGeneration_, *edit, debit, credit};
    history.applied = history.count;
    const size_t removedParts=removedPartCount(*edit);
    const size_t extraBytes=removedParts*sizeof(construction::PartInstance);
    while (history.payloadBytes() + extraBytes > limits_.historyBytes
        || history.partCount + removedParts > limits_.dormantParts) {
        // The new operation itself must stay reversible; never silently drop
        // it to make an undersized history configuration appear to work.
        if (history.count <= 1) return fail(SessionError::HistoryCapacity);
        if (const auto issue = evictHistoryFront(pending)) return issue;
    }
    visitPartChanges(*edit,[&](const auto& beforePart,const auto& afterPart) {
        if(beforePart&&!afterPart)history.parts[history.partCount++]=*beforePart;
        return true;
    });
    history.generation = *generation;
    return {};
}

SessionIssue GameSession::prepareCompensation(const Command& command, Pending& pending) {
    const bool undo = std::holds_alternative<Undo>(command.intent);
    const auto* target = targetOf(command.intent);
    const auto entryId = undo ? std::get<Undo>(command.intent).entry : std::get<Redo>(command.intent).entry;
    const auto expected = undo ? std::get<Undo>(command.intent).expectedHistoryGeneration
        : std::get<Redo>(command.intent).expectedHistoryGeneration;
    auto& candidate = *pending.candidate;
    auto& history = candidate.history;
    if (expected != history.generation || !entryId.valid()) return fail(SessionError::HistoryConflict);
    if ((undo && history.applied == 0) || (!undo && history.applied == history.count))
        return fail(SessionError::HistoryUnavailable);
    const auto entry = history.entries[undo ? history.applied - 1 : history.applied];
    if (entry.id != entryId || entry.edit.after->id != target->build
        || entry.caller != caller_ || entry.admission != admissionGeneration_) return fail(SessionError::HistoryConflict);
    for (size_t i = 0; i < pendingCount_; ++i) {
        if (pending_[i]->reservedHistory == entryId) return fail(SessionError::HistoryBusy);
        const auto& change = pending_[i]->historyTransition;
        for (size_t j = 0; j < change.evictedCount; ++j)
            if (change.evicted[j] == entryId) return fail(SessionError::HistoryBusy);
    }
    const auto generation = construction::next(history.generation);
    if (!generation) return fail(SessionError::HistoryExhausted);
    const auto nextRevision = construction::next(target->expectedRevision);
    if (!nextRevision) return fail(SessionError::RevisionExhausted);
    pending.reservedHistory = entryId;
    const auto debit = undo ? entry.credit : entry.debit;
    const auto credit = undo ? entry.debit : entry.credit;
    if (const auto issue = reserveDebit(pending, debit)) return issue;
    candidate.inventory.salvageMaterial -= debit.salvageMaterial;
    candidate.inventory.specialMachinery -= debit.specialMachinery;
    const auto balance = credited(candidate.inventory, credit);
    if (!balance) return fail(SessionError::ResourceOverflow);
    candidate.inventory = *balance;

    const auto build = findBuild(candidate.builds, target->build);
    BuildTransition transition;
    if (build != candidate.builds.end()) transition.before = headerOf(build->snapshot());
    else for (size_t i = 0; i < history.buildCount; ++i)
        if (history.builds[i].id == target->build) transition.before = history.builds[i];
    if (!transition.before || transition.before->owner != entry.edit.after->owner
        || transition.before->editLease != entry.edit.after->editLease) return fail(SessionError::HistoryConflict);
    transition.after = transition.before;
    transition.after->revision = *nextRevision;
    const auto& fromPart = undo ? entry.edit.afterPart : entry.edit.beforePart;
    const auto& toPart = undo ? entry.edit.beforePart : entry.edit.afterPart;
    if (!entry.edit.before) {
        // Only CreateBuild has no before header. Its inverse retains the
        // current empty header; restoration advances that dormant high-water.
        if (undo) {
            if (build == candidate.builds.end() || !build->snapshot().parts.empty()
                || !build->snapshot().connections.empty()
                || std::any_of(candidate.storedParts.begin(),candidate.storedParts.end(),[&](const auto& part){return part.owningBuild==target->build;})
                || std::any_of(candidate.starterKits.begin(),candidate.starterKits.end(),[&](const auto& kit){return kit.build==target->build;})) return fail(SessionError::HistoryConflict);
            if (history.buildCount >= limits_.dormantBuilds) return fail(SessionError::HistoryCapacity);
            transition.after->materialized = false;
            history.builds[history.buildCount++] = *transition.after;
            candidate.builds.erase(build);
            pending.removedBuild = target->build;
        } else {
            if (build != candidate.builds.end() || transition.before->materialized) return fail(SessionError::HistoryConflict);
            size_t held = 0;
            for (size_t i = 0; i < pendingCount_; ++i) held += pending_[i]->reservedBuilds;
            if (candidate.builds.size() + held >= limits_.builds) return fail(SessionError::Capacity);
            BuildSnapshot draft{target->build, *nextRevision, transition.before->owner, transition.before->editLease, {}, {}};
            construction::BuildIssue issue;
            auto restored = BuildModel::create(draft, catalog_, issue);
            if (!restored) return {SessionError::InvalidBuild, issue};
            candidate.builds.push_back(std::move(*restored));
            std::sort(candidate.builds.begin(), candidate.builds.end(), [](const auto& a, const auto& b) {
                return a.snapshot().id < b.snapshot().id;
            });
            for (size_t i = 0; i < history.buildCount; ++i) if (history.builds[i].id == target->build) {
                eraseFixed(history.builds, history.buildCount, i); break;
            }
            transition.after->materialized = true;
            pending.reservedBuilds = 1;
        }
        pending.committedObject = target->build;
    } else if(entry.edit.refit) {
        if(build==candidate.builds.end())return fail(SessionError::HistoryConflict);
        transition.refit=entry.edit.refit;transition.refitForward=!undo;
        auto draft=build->snapshot();
        if(const auto issue=transition.refit->apply(draft,catalog_,!undo))return {SessionError::HistoryConflict,issue};
        uint32_t held=0;
        for(size_t i=0;i<pendingCount_;++i)held+=pending_[i]->reservedParts;
        const size_t currentTotal=partCount(candidate.builds)+candidate.storedParts.size();
        const size_t total=currentTotal-build->snapshot().parts.size()+draft.parts.size();
        if(total+held>limits_.totalParts)return fail(SessionError::Capacity);
        size_t restores=0;
        if(!visitPartChanges(transition,[&](const auto& from,const auto& to) {
            const auto& part=from?*from:*to;
            if(part.provenance.origin==construction::PartOrigin::StarterLoan
                && std::find(candidate.entitlements.begin(),candidate.entitlements.end(),part.provenance.starterEntitlement)==candidate.entitlements.end())return false;
            size_t dormantPart=0;
            while(dormantPart<history.partCount && history.parts[dormantPart].id!=part.id)++dormantPart;
            if(from)return dormantPart==history.partCount;
            ++restores;
            return dormantPart<history.partCount && history.parts[dormantPart]==*to;
        }))return fail(SessionError::HistoryConflict);
        if(history.partCount-restores+removedPartCount(transition)>limits_.dormantParts)return fail(SessionError::HistoryCapacity);
        // Remove all restored escrow images before retaining newly removed ones.
        visitPartChanges(transition,[&](const auto& from,const auto& to) {
            if(!from)for(size_t i=0;i<history.partCount;++i)if(history.parts[i].id==to->id) {
                eraseFixed(history.parts,history.partCount,i);break;
            }
            return true;
        });
        visitPartChanges(transition,[&](const auto& from,const auto& to) {
            if(from&&!to)history.parts[history.partCount++]=*from;
            return true;
        });
        if(const auto issue=build->replace(target->expectedRevision,draft,catalog_))return {SessionError::InvalidBuild,issue};
        pending.reservedParts=total>currentTotal?static_cast<uint32_t>(total-currentTotal):0;
        pending.committedObject=target->build;
    } else {
        if (build == candidate.builds.end() || (!fromPart && !toPart)) return fail(SessionError::HistoryConflict);
        auto draft = build->snapshot();
        const auto partId = fromPart ? fromPart->id : toPart->id;
        const auto part = std::find_if(draft.parts.begin(), draft.parts.end(),
            [partId](const auto& value) { return value.id == partId; });
        if ((fromPart && (part == draft.parts.end() || *part != *fromPart))
            || (!fromPart && part != draft.parts.end())) return fail(SessionError::HistoryConflict);
        const auto& image = toPart ? *toPart : *fromPart;
        if (image.provenance.origin == construction::PartOrigin::StarterLoan
            && std::find(candidate.entitlements.begin(), candidate.entitlements.end(), image.provenance.starterEntitlement)
                == candidate.entitlements.end()) return fail(SessionError::HistoryConflict);
        size_t dormant = 0;
        while (dormant < history.partCount && history.parts[dormant].id != partId) ++dormant;
        if (fromPart && dormant != history.partCount) return fail(SessionError::HistoryConflict);
        if (!fromPart) {
            if (dormant == history.partCount || history.parts[dormant] != *toPart)
                return fail(SessionError::HistoryConflict);
            uint32_t held = 0;
            for (size_t i = 0; i < pendingCount_; ++i) held += pending_[i]->reservedParts;
            if (partCount(candidate.builds) + candidate.storedParts.size() + held >= limits_.totalParts) return fail(SessionError::Capacity);
            draft.parts.push_back(*toPart);
            eraseFixed(history.parts, history.partCount, dormant);
            pending.reservedParts = 1;
        } else if (!toPart) {
            if (std::any_of(draft.connections.begin(), draft.connections.end(), [&](const auto& connection) {
                return connection.a.part == partId || connection.b.part == partId;
            })) return fail(SessionError::ConnectedPart);
            if (history.partCount >= limits_.dormantParts) return fail(SessionError::HistoryCapacity);
            history.parts[history.partCount++] = *fromPart;
            draft.parts.erase(part);
        } else *part = *toPart;
        if (const auto issue = build->replace(target->expectedRevision, draft, catalog_))
            return {SessionError::InvalidBuild, issue};
        transition.beforePart = fromPart;
        transition.afterPart = toPart;
        pending.committedObject = partId;
    }
    if (history.payloadBytes() > limits_.historyBytes) return fail(SessionError::HistoryCapacity);
    history.applied = undo ? history.applied - 1 : history.applied + 1;
    pending.historyTransition = {history.generation, *generation, undo ? HistoryAction::Undo : HistoryAction::Redo, entryId};
    history.generation = *generation;
    pending.changedBuild = target->build;
    pending.transition = transition;
    return {};
}

std::optional<DurableId> GameSession::allocateId(Pending& pending) {
    if (ids_.lastIssued() == std::numeric_limits<uint64_t>::max()) return std::nullopt;
    if (ids_.lastIssued() == reservedThroughId_) {
        const uint64_t amount = std::min(uint64_t{64}, std::numeric_limits<uint64_t>::max() - reservedThroughId_);
        JournalRecord lease{kLogicalJournalSchema, world_, epoch_, {}, tick_,
            AllocatorLeaseRecord{ids_.lastIssued(), reservedThroughId_, reservedThroughId_ + amount}};
        if (!journal_->appendReserved(pending.journalReservation, lease)) {
            journalFault_ = true;
            return std::nullopt;
        }
        reservedThroughId_ += amount;
    }
    const auto id=ids_.allocate();
    if(id && isRefitIntent(pending.command.intent)) {
        if(!pending.assignedRange.count)pending.assignedRange.first=*id;
        ++pending.assignedRange.count;
    }
    return id;
}

Receipt GameSession::submit(CallerContext caller, const Command& command) {
    const auto refuse = [&](SessionError error) { noteIngress(error); return rejected(command.sequence, error); };
    if (const auto error = authenticate(caller); error != SessionError::None) return refuse(error);
    if (!admissionOpen_) return refuse(SessionError::Closed);
    if (journalFault_) return refuse(SessionError::JournalCapacity);
    if (command.epoch != epoch_) return refuse(SessionError::WrongEpoch);
    if (!command.sequence.valid()) return refuse(SessionError::InvalidIdentity);
    for (size_t i = 0; i < pendingCount_; ++i) {
        if (pending_[i]->command.sequence != command.sequence) continue;
        if (pending_[i]->command == command) { noteIngress(SessionError::None); return pending_[i]->receipt; }
        return refuse(SessionError::RequestConflict);
    }
    for (const auto& retained : receipts_) {
        if (retained.command.sequence != command.sequence) continue;
        if (retained.command == command) { noteIngress(SessionError::None); return retained.receipt; }
        return refuse(SessionError::RequestConflict);
    }
    if (command.sequence.value() <= processedSequence_) return refuse(SessionError::AlreadyProcessed);
    if (!expectedSequence().valid()) return refuse(SessionError::SequenceExhausted);
    if (command.sequence != expectedSequence()) return refuse(SessionError::SequenceGap);
    if (pendingCount_ == limits_.pendingCommands) return refuse(SessionError::Busy);
    const bool mayAllocate = std::holds_alternative<CreateBuild>(command.intent) || std::holds_alternative<AddPart>(command.intent);
    uint32_t journalRecords=mayAllocate?3:2;
    size_t extraBytes=2*ownedExtraBytes(command);
    if(const auto* design=refitDesign(command.intent,state_->starterKits)) {
        const auto maximumIds=static_cast<uint64_t>(std::count_if(design->parts().begin(),design->parts().end(),
            [](const auto& part){return part.source==DurableId{};}))+design->welds().size();
        const auto available=reservedThroughId_-ids_.lastIssued();
        journalRecords+=static_cast<uint32_t>(maximumIds>available?(maximumIds-available+63)/64:0);
        extraBytes+=std::min(construction::kMaximumRefitDeltaBytes+kMaximumStorageTransitionBytes,kMaximumJournalRecordBytes-kJournalRecordStorageBytes-ownedExtraBytes(command));
    } else if(isCompensation(command.intent))extraBytes+=construction::kMaximumRefitDeltaBytes;
    else if(std::holds_alternative<CutWeld>(command.intent))extraBytes+=sizeof(WeldCutTransition);
    const auto reservation = journal_->reserve({journalRecords, journalRecords*kJournalRecordStorageBytes+extraBytes});
    if (!reservation) return refuse(SessionError::JournalCapacity);
    std::unique_ptr<Pending> work;
    SessionIssue preparationIssue;
    try {
        work = std::make_unique<Pending>();
        work->command = command;
        work->journalReservation = reservation.reservation;
        work->historyTransition.before = work->historyTransition.after = state_->history.generation;
        preparationIssue = prepare(command, *work);
        if (!preparationIssue) preparationIssue = recordHistory(*work);
        if (!preparationIssue && preparationGeneration_ == std::numeric_limits<uint64_t>::max())
            preparationIssue = fail(SessionError::Capacity);
        if (!preparationIssue) {
            work->ticket = {epoch_, ++preparationGeneration_};
            const BuildSnapshot* changed = nullptr;
            if (work->changedBuild) {
                const auto build = findBuild(work->candidate->builds, *work->changedBuild);
                if (build != work->candidate->builds.end()) changed = &build->snapshot();
            }
            const StarterKit* candidateKit=nullptr;
            if(changed)for(const auto& kit:work->candidate->starterKits)if(kit.build==changed->id)candidateKit=&kit;
            work->adapterStarted = true;
            const auto prepared = adapter_.begin({work->ticket, state_->revision, work->candidate->revision,
                changed, partCount(work->candidate->builds), connectionCount(work->candidate->builds), work->removedBuild,
                std::get_if<CargoDeliveryTransition>(&work->transition),candidateKit,
                std::get_if<BuildTransition>(&work->transition)?std::get<BuildTransition>(work->transition).storage.get():nullptr,
                std::get_if<CutWeld>(&work->command.intent)});
            if (prepared.ticket != work->ticket
                || (prepared.state != PreparationState::Pending && prepared.state != PreparationState::Ready))
                preparationIssue = fail(SessionError::AdapterRejected);
            else work->preparation = prepared.state;
        }
    } catch (const std::bad_alloc&) { preparationIssue = fail(SessionError::Capacity); }
    catch (...) {
        if (work) releasePreparation(*work);
        static_cast<void>(journal_->discard(reservation.reservation));
        throw; // Burned allocator IDs/lease persist; this request was not admitted.
    }
    if (!work) {
        static_cast<void>(journal_->discard(reservation.reservation));
        return refuse(SessionError::Capacity);
    }
    work->receipt = rejected(command.sequence, SessionError::None);
    work->receipt.admitted = true;
    work->receipt.state = ReceiptState::PendingPreparation;
    const RequestKey key{caller_, epoch_, admissionGeneration_, command.sequence};
    JournalRecord admission{kLogicalJournalSchema, world_, epoch_, {}, tick_,
        RequestAdmissionRecord{key, command, RequestFrontier{admittedSequence_}, RequestFrontier{processedSequence_},
            mayAllocate ? work->committedObject : std::nullopt, allocatorMarkers(),work->assignedRange}};
    const auto admissionJournal = journal_->appendReserved(reservation.reservation, admission);
    if (!admissionJournal) {
        releasePreparation(*work);
        static_cast<void>(journal_->discard(reservation.reservation));
        journalFault_ = true;
        return refuse(SessionError::JournalCapacity);
    }
    admittedSequence_ = command.sequence.value();
    work->receipt.expectedSequence = expectedSequence();
    work->receipt.journal = admissionJournal;
    if (preparationIssue) rejectPending(*work, preparationIssue);
    pending_[pendingCount_++] = std::move(work);
    publishEvent(receiptEvent(command, pending_[pendingCount_ - 1]->receipt));
    drainRejected();
    return receipt(caller, command.sequence);
}

void GameSession::releasePreparation(Pending& pending) noexcept {
    if (pending.adapterStarted) {
        adapter_.discard(pending.ticket);
        pending.adapterStarted = false;
    }
    pending.candidate.reset();
    pending.reservedDebit = {};
    pending.reservedParts = 0;
    pending.reservedBuilds = 0;
    pending.reservedHistory.reset();
    pending.historyTransition = {};
}
void GameSession::rejectPending(Pending& pending, SessionIssue issue) noexcept {
    if (pending.rejected) return;
    releasePreparation(pending);
    pending.rejected = true;
    pending.finalIssue = issue;
    // Keep the original intent and terminal journal reservation until this
    // request reaches the front. A canceled later slot is still admitted.
}
JournalRecord GameSession::decisionRecord(const Pending& pending, const Receipt& result) const noexcept {
    RequestDecisionRecord decision;
    decision.key = {caller_, epoch_, admissionGeneration_, pending.command.sequence};
    decision.command = pending.command;
    decision.beforeRevision = state_->revision;
    decision.afterRevision = result.revision;
    decision.processedBefore = RequestFrontier{processedSequence_};
    decision.balanceBefore = state_->inventory;
    decision.balanceAfter = result.state == ReceiptState::Committed ? pending.candidate->inventory : state_->inventory;
    decision.history.before = decision.history.after = state_->history.generation;
    if (result.state == ReceiptState::Committed) {
        decision.objects = pending.transition;
        decision.history = pending.historyTransition;
    }
    decision.allocator = allocatorMarkers();
    decision.outcome = {result.state, result.issue.error, result.issue.build.error, result.issue.build.object,
        result.revision, result.buildRevision, result.object};
    return {kLogicalJournalSchema, world_, epoch_, {}, tick_, decision};
}
void GameSession::finishFront(Receipt result) noexcept {
    auto& work = *pending_[0];
    const auto record = decisionRecord(work, result);
    // Bounded observation values are built before accepted-state publication.
    // Only their journal sequence is filled after the reserved append succeeds.
    result.expectedSequence = expectedSequence();
    auto receiptNotice = receiptEvent(work.command, result);
    std::optional<EventDraft> stateNotice;
    if (result.state == ReceiptState::Committed)
        stateNotice = stateEvent(std::get<RequestDecisionRecord>(record.payload));
    const bool appendable = journal_->canAppend(work.journalReservation, record);
    if (!appendable) {
        // A failed journal invariant must never publish a candidate. Trusted
        // close still tears down and reports memory-only cancellation below.
        journalFault_ = true;
        if (result.state == ReceiptState::Committed) { closeAdmission(); return; }
    }
    if (result.state == ReceiptState::Committed) {
        adapter_.activate(work.ticket, tick_);
        work.adapterStarted = false; // Activation transferred ownership; do not discard it.
        state_.swap(work.candidate);
    }
    if (appendable) {
        result.journal = journal_->appendReserved(work.journalReservation, record);
        if (!result.journal) journalFault_ = true;
    } else result.journal.reset();
    static_cast<void>(journal_->discard(work.journalReservation));
    retain(work.command, result);
    const auto reference = result.journal ? std::optional<EventJournalReference>{
        {journal_->identity().writerGeneration, *result.journal}} : std::nullopt;
    std::get<ReceiptChangedEvent>(receiptNotice.payload).journal = reference;
    if (stateNotice) {
        std::get<StateCommittedEvent>(stateNotice->payload).journal = reference;
        publishEvent(*stateNotice);
    }
    publishEvent(receiptNotice);
    pending_[0].reset();
    for (size_t i = 1; i < pendingCount_; ++i) pending_[i - 1] = std::move(pending_[i]);
    --pendingCount_;
}
void GameSession::drainRejected() noexcept {
    for (size_t drained = 0; drained < 2 && pendingCount_ != 0 && pending_[0]->rejected; ++drained) {
        auto result = pending_[0]->receipt;
        result.state = ReceiptState::Rejected;
        result.issue = pending_[0]->finalIssue;
        result.revision = state_->revision;
        finishFront(result);
    }
}
Receipt GameSession::cancel(CallerContext caller, RequestSequence sequence) {
    if (const auto error = authenticate(caller); error != SessionError::None) {
        noteIngress(error); return rejected(sequence, error);
    }
    for (size_t i = 0; i < pendingCount_; ++i)
        if (pending_[i]->command.sequence == sequence) {
            if(i==0 && executionTick_) {auto result=pending_[i]->receipt;result.issue=fail(SessionError::Busy);return result;}
            rejectPending(*pending_[i], fail(SessionError::Canceled));
        }
    drainRejected();
    const auto result = receipt(caller, sequence);
    if (!result.admitted) noteIngress(result.issue.error);
    return result;
}
const StarterKit* GameSession::starterKit(DurableId build) const noexcept {
    const auto found=std::find_if(state_->starterKits.begin(),state_->starterKits.end(),[&](const auto& kit){return kit.build==build;});
    return found==state_->starterKits.end()?nullptr:&*found;
}
SessionIssue GameSession::registerStarterKit(const StarterKit& kit,SessionRevision expected) noexcept {
    if(!admissionOpen_)return fail(SessionError::Closed);
    if(journalFault_)return fail(SessionError::JournalCapacity);
    if(pendingCount_||executionTick_)return fail(SessionError::Busy);
    if(const auto* found=starterKit(kit.build))return *found==kit?SessionIssue{}:fail(SessionError::InvalidBootstrap);
    if(expected!=state_->revision)return fail(SessionError::StaleRevision);
    if(state_->starterKits.size()>=kMaximumStarterKits)return fail(SessionError::Capacity);
    const auto revision=construction::next(state_->revision);if(!revision)return fail(SessionError::RevisionExhausted);
    try{
        auto kits=state_->starterKits;kits.push_back(kit);
        SessionBootstrap boot;boot.world=world_;boot.caller=caller_;boot.epoch=epoch_;boot.lastIssuedId=ids_.lastIssued();
        boot.revision=state_->revision;boot.tick=tick_;boot.inventory=state_->inventory;boot.limits=limits_;boot.workshopEnabled=workshopEnabled_;
        for(const auto& build:state_->builds)boot.builds.push_back(build.snapshot());
        boot.starterEntitlements=state_->entitlements;boot.starterKits=kits;boot.storedParts=state_->storedParts;
        boot.cargoDefinitions=state_->cargoDefinitions;boot.cargo=state_->cargo;boot.jobs=state_->jobs;
        if(const auto issue=validateInitialState(boot,catalog_))return issue;
        const StarterKitRegisteredRecord registration{caller_,admissionGeneration_,state_->revision,*revision,kit,
            state_->inventory,RequestFrontier{admittedSequence_},RequestFrontier{processedSequence_},allocatorMarkers()};
        const JournalRecord record{kLogicalJournalSchema,world_,epoch_,{},tick_,registration};
        const auto slot=journal_->reserve({1,journalRecordBytes(record)});if(!slot)return fail(SessionError::JournalCapacity);
        if(!journal_->canAppend(slot.reservation,record)){
            static_cast<void>(journal_->discard(slot.reservation));return fail(SessionError::InvalidBootstrap);
        }
        StateCommittedEvent change;change.changes=changeBit(EventChange::Entitlements);change.primary=kit.entitlement;change.balance=state_->inventory;
        EventDraft notice{tick_,*revision,{},change};
        // Candidate allocation/validation is complete. Fixed record/shared
        // recipe publication and the vector swap cannot allocate or re-enter.
        state_->starterKits.swap(kits);state_->revision=*revision;
        const auto sequence=journal_->appendReserved(slot.reservation,record);static_cast<void>(journal_->discard(slot.reservation));
        if(!sequence){journalFault_=true;closeAdmission();return fail(SessionError::JournalCapacity);}
        std::get<StateCommittedEvent>(notice.payload).journal=EventJournalReference{journal_->identity().writerGeneration,*sequence};
        publishEvent(notice);return {};
    }catch(const std::bad_alloc&){return fail(SessionError::Capacity);}
}

EntitlementRetirementReceipt GameSession::retireStarterEntitlement(
    DurableId entitlement, SessionRevision expectedRevision) noexcept {
    const auto reject = [&](EntitlementRetirementError error) {
        return EntitlementRetirementReceipt{error, state_->revision, {}};
    };
    if (!admissionOpen_) return reject(EntitlementRetirementError::Closed);
    if(executionTick_) return reject(EntitlementRetirementError::Busy);
    if (journalFault_) return reject(EntitlementRetirementError::JournalFault);
    if (!construction::isValid(entitlement) || entitlement.world != world_ || entitlement.counter > ids_.lastIssued())
        return reject(EntitlementRetirementError::InvalidIdentity);
    const auto found = std::find(state_->entitlements.begin(), state_->entitlements.end(), entitlement);
    if (found == state_->entitlements.end()) return reject(EntitlementRetirementError::Inactive);
    if (expectedRevision != state_->revision) return reject(EntitlementRetirementError::StaleRevision);
    for (const auto& build : state_->builds) for (const auto& part : build.snapshot().parts)
        if (part.provenance.origin == construction::PartOrigin::StarterLoan && part.provenance.starterEntitlement == entitlement)
            return reject(EntitlementRetirementError::ActiveLoans);
    const auto revision = construction::next(state_->revision);
    if (!revision) return reject(EntitlementRetirementError::RevisionExhausted);
    RecoveryHistory projected;
    EntitlementRetiredRecord retirement;
    if (!detail::projectEntitlementRetirement(state_->history, entitlement, projected, retirement.history))
        return reject(EntitlementRetirementError::HistoryExhausted);
    retirement.caller = caller_; retirement.admission = admissionGeneration_; retirement.entitlement = entitlement;
    retirement.beforeRevision = state_->revision; retirement.afterRevision = *revision;
    retirement.balance = state_->inventory; retirement.allocator = allocatorMarkers();
    retirement.admittedThrough = RequestFrontier{admittedSequence_};
    retirement.processedThrough = RequestFrontier{processedSequence_};
    const auto slot = journal_->reserve({1, kJournalRecordStorageBytes});
    if (!slot) return reject(EntitlementRetirementError::JournalCapacity);
    const JournalRecord record{kLogicalJournalSchema, world_, epoch_, {}, tick_, retirement};
    if (!journal_->canAppend(slot.reservation, record)) {
        static_cast<void>(journal_->discard(slot.reservation)); journalFault_ = true;
        return reject(EntitlementRetirementError::JournalFault);
    }
    StateCommittedEvent retirementNotice;
    retirementNotice.changes = changeBit(EventChange::Entitlements) | changeBit(EventChange::History);
    retirementNotice.primary = entitlement; retirementNotice.balance = state_->inventory;
    EventDraft notice{tick_, *revision, {}, retirementNotice};
    // All authority changes and the exact control record publish without an
    // allocation or backend activation. The owning thread cannot re-enter.
    state_->entitlements.erase(found);
    std::erase_if(state_->starterKits,[&](const auto& kit){return kit.entitlement==entitlement;});
    static_cast<RecoveryHistory&>(state_->history) = projected;
    state_->revision = *revision;
    const auto sequence = journal_->appendReserved(slot.reservation, record);
    static_cast<void>(journal_->discard(slot.reservation));
    if (!sequence) { journalFault_ = true; closeAdmission(); return reject(EntitlementRetirementError::JournalFault); }
    std::get<StateCommittedEvent>(notice.payload).journal = EventJournalReference{journal_->identity().writerGeneration, *sequence};
    publishEvent(notice);
    // Every outstanding candidate copied the old entitlement/history state.
    // Release it now; its reserved terminal slot still guarantees ordered detail.
    for (size_t i = 0; i < pendingCount_; ++i)
        rejectPending(*pending_[i], fail(isCompensation(pending_[i]->command.intent)
            ? SessionError::HistoryConflict : SessionError::StaleRevision));
    drainRejected();
    return {EntitlementRetirementError::None, *revision, sequence};
}

void GameSession::closeAdmission() noexcept {
    if (!admissionOpen_) return;
    admissionOpen_ = false;
    executionTick_.reset();
    for (size_t i = 0; i < pendingCount_; ++i) rejectPending(*pending_[i], fail(SessionError::Canceled));
    drainRejected();
    AdmissionClosedEvent notice;
    if (journal_) {
        const JournalRecord closure{kLogicalJournalSchema, world_, epoch_, {}, tick_,
            AdmissionClosedRecord{caller_, admissionGeneration_, RequestFrontier{processedSequence_}}};
        const auto sequence = journal_->appendReserved(closureReservation_, closure);
        if (!sequence) journalFault_ = true;
        else notice.journal = EventJournalReference{journal_->identity().writerGeneration, *sequence};
        static_cast<void>(journal_->discard(closureReservation_));
    }
    // Session-local history is revoked by token closure, including dormant
    // objects. Active objects/account and the allocator high-water stay intact.
    const auto generation = state_->history.generation;
    state_->history = WorkshopHistory{};
    state_->history.generation = construction::next(generation).value_or(generation);
    notice.reason = journalFault_ ? EventCloseReason::JournalFault : EventCloseReason::HostClosed;
    publishEvent({tick_, state_->revision, {}, notice});
}
void GameSession::pollPreparation() {
    for (size_t i = 0; i < pendingCount_; ++i) {
        auto& work = *pending_[i];
        if (work.rejected || work.preparation == PreparationState::Ready) continue;
        const auto result = adapter_.poll(work.ticket);
        if (result.ticket != work.ticket) continue;
        if (result.state != PreparationState::Pending && result.state != PreparationState::Ready)
            rejectPending(work, fail(SessionError::AdapterRejected));
        else work.preparation = result.state;
    }
    drainRejected(); // Ordered terminal bookkeeping only; never activates a candidate.
}
SessionIssue GameSession::revalidate(const Pending& pending, SimulationTick atTick) const {
    if (pending.command.epoch != epoch_) return fail(SessionError::WrongEpoch);
    if (pending.command.expectedRevision != state_->revision) return fail(SessionError::StaleRevision);
    if (const auto* target = targetOf(pending.command.intent)) {
        if (const auto issue = checkTarget(*target, *state_, atTick, isCompensation(pending.command.intent))) return issue;
    }
    if (pending.historyTransition.before != state_->history.generation) return fail(SessionError::HistoryConflict);
    auto remaining = pending.candidate->inventory;
    for (size_t i = 0; i < pendingCount_; ++i) {
        if (pending_[i].get() == &pending) continue;
        const auto debit = pending_[i]->reservedDebit;
        if (!canPay(remaining, debit)) return fail(SessionError::InsufficientResources);
        remaining.salvageMaterial -= debit.salvageMaterial;
        remaining.specialMachinery -= debit.specialMachinery;
    }
    return {};
}
bool GameSession::advanceOneTick() {
    if (executionIncarnation_ || !admissionOpen_) return false;
    const auto tick = construction::next(tick_);
    if (!tick) return false;
    tick_ = *tick;
    pollPreparation();
    for (size_t drained = 0; drained < 2 && pendingCount_ != 0; ++drained) {
        auto& work = *pending_[0];
        if (work.rejected) { drainRejected(); continue; }
        if (work.preparation != PreparationState::Ready) break;
        if (const auto issue = revalidate(work,tick_)) {
            rejectPending(work, issue); drainRejected(); continue;
        }
        if (!adapter_.canActivate(work.ticket)) {
            rejectPending(work, fail(SessionError::AdapterRejected)); drainRejected(); continue;
        }
        auto result = work.receipt;
        result.state = ReceiptState::Committed;
        result.revision = work.candidate->revision;
        result.object = work.committedObject;
        if (const auto* build = std::get_if<BuildTransition>(&work.transition)) result.buildRevision = build->after->revision;
        finishFront(result);
    }
    return true;
}
bool GameSession::bindExecution(uint64_t incarnation,SimulationTick baseTick) noexcept {
    if(!incarnation || executionIncarnation_ || executionTick_ || pendingCount_ || baseTick!=tick_) return false;
    executionIncarnation_=incarnation;return true;
}
bool GameSession::stageExecution(uint64_t incarnation,SimulationTick nextTick) {
    if(!admissionOpen_ || !incarnation || incarnation!=executionIncarnation_ || executionTick_ || nextTick<=tick_) return false;
    pollPreparation();
    if(!pendingCount_ || pending_[0]->preparation!=PreparationState::Ready) return false;
    auto& work=*pending_[0];
    if(const auto issue=revalidate(work,nextTick)) {rejectPending(work,issue);drainRejected();return false;}
    if(!adapter_.canActivate(work.ticket)) {rejectPending(work,fail(SessionError::AdapterRejected));drainRejected();return false;}
    auto outcome=work.receipt;outcome.state=ReceiptState::Committed;outcome.revision=work.candidate->revision;outcome.object=work.committedObject;
    if(const auto* build=std::get_if<BuildTransition>(&work.transition)) outcome.buildRevision=build->after->revision;
    auto decision=decisionRecord(work,outcome);decision.tick=nextTick;
    if(!journal_->canAppend(work.journalReservation,decision)) {journalFault_=true;closeAdmission();return false;}
    const auto staged=adapter_.stage(work.ticket,nextTick);
    if(staged==PreparationState::Pending) return false;
    if(staged!=PreparationState::Ready) {rejectPending(work,fail(SessionError::DeliveryNotReady));drainRejected();return false;}
    executionTick_=nextTick;return true;
}
bool GameSession::confirmExecution(uint64_t incarnation,SimulationTick completed) {
    if(!admissionOpen_ || !incarnation || incarnation!=executionIncarnation_ || completed<tick_) return false;
    if(executionTick_ && completed>=*executionTick_) {
        if(!pendingCount_ || !adapter_.executionComplete(pending_[0]->ticket,*executionTick_)) return false;
        // stage() already installed physical changes before execution. activate()
        // only publishes their confirmed mapping; it cannot enqueue GPU work.
        tick_=*executionTick_;
        auto& work=*pending_[0];auto result=work.receipt;
        result.state=ReceiptState::Committed;result.revision=work.candidate->revision;result.object=work.committedObject;
        if(const auto* build=std::get_if<BuildTransition>(&work.transition)) result.buildRevision=build->after->revision;
        finishFront(result);executionTick_.reset();
    }
    tick_=completed;return !journalFault_;
}
SessionSnapshot GameSession::snapshot() const {
    SessionSnapshot result;
    result.revision = state_->revision;
    result.tick = tick_;
    result.inventory = state_->inventory;result.storedParts=state_->storedParts;
    result.builds.reserve(state_->builds.size());
    for (const auto& build : state_->builds) result.builds.push_back(build.snapshot());
    result.cargoDefinitions = state_->cargoDefinitions;
    result.cargo = state_->cargo;
    result.jobs = state_->jobs;
    return result;
}

void GameSession::noteIngress(SessionError error) noexcept {
    ObservationIngress bucket = ObservationIngress::Other;
    switch (error) {
    case SessionError::None: bucket = ObservationIngress::ExactRetry; break;
    case SessionError::WrongCaller: case SessionError::WrongToken: case SessionError::WrongEpoch:
    case SessionError::InvalidIdentity: bucket = ObservationIngress::Authentication; break;
    case SessionError::SequenceGap: case SessionError::SequenceExhausted: case SessionError::AlreadyProcessed:
        bucket = ObservationIngress::Ordering; break;
    case SessionError::RequestConflict: bucket = ObservationIngress::ConflictingRetry; break;
    case SessionError::Capacity: case SessionError::JournalCapacity: case SessionError::Busy:
        bucket = ObservationIngress::Capacity; break;
    case SessionError::Closed: bucket = ObservationIngress::Closed; break;
    default: break;
    }
    observationDiagnostics_.ingress[static_cast<size_t>(bucket)].increment();
}
EventDraft GameSession::receiptEvent(const Command& command, const Receipt& result) const noexcept {
    ReceiptChangedEvent notice;
    notice.request = {caller_.participant, result.sequence};
    switch (result.state) {
    case ReceiptState::PendingPreparation: notice.outcome = EventOutcome::Pending; break;
    case ReceiptState::Rejected: notice.outcome = EventOutcome::Rejected; break;
    case ReceiptState::Committed: notice.outcome = EventOutcome::Committed; break;
    }
    // Unknown source errors deliberately fail event validation, remaining
    // observable through publicationLost. Never silently relabel them as success.
    notice.error = eventCode(result.issue.error).value_or(static_cast<EventSessionCode>(0xffff));
    notice.buildError = eventCode(result.issue.build.error).value_or(static_cast<EventBuildCode>(0xffff));
    notice.expectedSequence = result.expectedSequence; notice.object = result.object;
    if (result.journal) notice.journal = EventJournalReference{journal_->identity().writerGeneration, *result.journal};
    EventDraft draft{tick_, result.revision, {}, notice};
    if (result.buildRevision) {
        if (const auto* target = targetOf(command.intent))
            draft.build = EventBuildReference{target->build, *result.buildRevision};
        else if (std::holds_alternative<CreateBuild>(command.intent) && result.object)
            draft.build = EventBuildReference{*result.object, *result.buildRevision};
    }
    return draft;
}
EventDraft GameSession::stateEvent(const RequestDecisionRecord& decision) const noexcept {
    StateCommittedEvent notice;
    notice.cause = PublicRequestReference{caller_.participant, decision.command.sequence};
    notice.balance = decision.balanceAfter; notice.primary = decision.outcome.object;
    if (decision.balanceBefore != decision.balanceAfter) notice.changes |= changeBit(EventChange::Inventory);
    if (decision.history.action != HistoryAction::None) notice.changes |= changeBit(EventChange::History);
    EventDraft draft{tick_, decision.afterRevision, {}, {}};
    if (const auto* build = std::get_if<BuildTransition>(&decision.objects)) {
        notice.changes |= changeBit(EventChange::Builds);
        if (build->after) draft.build = EventBuildReference{build->after->id, build->after->revision};
    } else if (std::holds_alternative<JobTransition>(decision.objects)) notice.changes |= changeBit(EventChange::Jobs);
    else if(std::holds_alternative<CargoDeliveryTransition>(decision.objects)) {
        notice.changes|=changeBit(EventChange::Jobs);notice.changes|=changeBit(EventChange::Cargo);
    }
    draft.payload = notice;
    return draft;
}
void GameSession::publishEvent(EventDraft draft) noexcept {
    // Unpublished bootstrap validation/failing construction has no observers.
    if (!events_) return;
    const auto published = events_->publish(EventLane::Domain, draft);
    if (published.status != EventPublishStatus::Published) observationDiagnostics_.publicationLost = true;
}
bool GameSession::restartEventStream(EventStreamIncarnation incarnation, SessionIssue& issue) noexcept {
    const EventStreamIdentity identity{world_, epoch_, incarnation};
    if (!isValid(identity) || incarnation == events_->reader().identity().incarnation) {
        issue = fail(SessionError::InvalidIdentity); return false;
    }
    events_->restart(identity); // In place, so borrowed reader views remain valid.
    observationDiagnostics_.publicationLost = false;
    issue = {}; return true;
}
std::unique_ptr<EventBaseline> GameSession::eventBaseline(SessionIssue& issue, size_t maximumOwnedBytes) const {
    const auto refuse = [&]() { issue = fail(SessionError::Capacity); return std::unique_ptr<EventBaseline>{}; };
    if (maximumOwnedBytes > kMaximumEventBaselineBytes) return refuse();
    size_t owned = sizeof(EventBaseline);
    const auto charge = [&](size_t count, size_t bytes) {
        if (owned > maximumOwnedBytes || (bytes && count > (maximumOwnedBytes - owned) / bytes)) return false;
        owned += count * bytes; return true;
    };
    if (!charge(state_->storedParts.size(),sizeof(construction::PartInstance))
        || !charge(state_->builds.size(), sizeof(BuildSnapshot)) || !charge(state_->entitlements.size(), sizeof(DurableId))
        || !charge(state_->cargoDefinitions.size(), sizeof(CargoDefinition)) || !charge(state_->cargo.size(), sizeof(CargoRecord))
        || !charge(state_->jobs.size(), sizeof(JobRecord))) return refuse();
    for (const auto& build : state_->builds)
        if (!charge(build.snapshot().parts.size(), sizeof(construction::PartInstance))
            || !charge(build.snapshot().connections.size(), sizeof(construction::Connection))) return refuse();
    try {
        auto result = std::make_unique<EventBaseline>();
        result->state = snapshot();
        result->starterEntitlements = state_->entitlements;
        result->participant = caller_.participant; result->workshopEnabled = workshopEnabled_;
        result->admission = admissionState(); result->history = history();
        const auto copyReceipt = [&](const Command& command, const Receipt& receipt) {
            const auto notice = receiptEvent(command, receipt);
            result->receipts[result->receiptCount++] = {std::get<ReceiptChangedEvent>(notice.payload), notice.revision, notice.build};
        };
        for (const auto& retained : receipts_) copyReceipt(retained.command, retained.receipt);
        // A later canceled/rejected preparation still has a pending public
        // receipt until its ordered terminal decision publishes.
        for (size_t i = 0; i < pendingCount_; ++i) copyReceipt(pending_[i]->command, pending_[i]->receipt);
        const auto reader = events_->reader(); result->stream = reader.identity();
        constexpr std::array<EventLane, 3> lanes{EventLane::Domain, EventLane::Presentation, EventLane::Telemetry};
        for (size_t i = 0; i < lanes.size(); ++i) {
            result->lanes[i] = *reader.stats(lanes[i]);
            result->cursors[i] = {result->stream.incarnation, lanes[i], result->lanes[i].publishedThrough};
        }
        result->diagnostics = observationDiagnostics_; result->ownedBytes = owned;
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(); }
}

HistorySummary GameSession::history() const noexcept {
    const auto& value = state_->history;
    HistorySummary result{value.generation, static_cast<uint32_t>(value.count), static_cast<uint32_t>(value.applied),
        static_cast<uint32_t>(value.partCount), static_cast<uint32_t>(value.buildCount), value.payloadBytes(), {}, {}};
    const auto choice = [&](const HistoryEntry& entry) -> std::optional<HistoryChoice> {
        const auto id = entry.edit.after->id;
        const auto build = findBuild(state_->builds, id);
        if (build != state_->builds.end()) return HistoryChoice{entry.id, {id, build->snapshot().revision}};
        for (size_t i = 0; i < value.buildCount; ++i)
            if (value.builds[i].id == id) return HistoryChoice{entry.id, {id, value.builds[i].revision}};
        return std::nullopt;
    };
    if (admissionOpen_) {
        if (value.applied != 0) result.undo = choice(value.entries[value.applied - 1]);
        if (value.applied < value.count) result.redo = choice(value.entries[value.applied]);
    }
    return result;
}

std::unique_ptr<LogicalRecoveryCheckpoint> SessionRecovery::capture(
    const GameSession& session, RecoveryContentIdentity content, RecoveryIssue& issue, size_t maximumOwnedBytes) {
    const auto reject = [&](RecoveryError error) -> std::unique_ptr<LogicalRecoveryCheckpoint> {
        issue.error = error; return nullptr;
    };
    if (!construction::isValid(content.manifest.id) || content.manifest.version == 0
        || content.profile != 1 || content.profileVersion != 1
        || std::all_of(content.manifestDigest.begin(), content.manifestDigest.end(), [](std::byte b) { return b == std::byte{}; }))
        return reject(RecoveryError::InvalidContentIdentity);
    if (maximumOwnedBytes == 0 || maximumOwnedBytes > kMaximumLogicalRecoveryBytes)
        return reject(RecoveryError::Capacity);
    if (session.journalFault_) return reject(RecoveryError::JournalFault);
    if(session.executionTick_) return reject(RecoveryError::InconsistentSession);
    const auto& state = *session.state_;
    const auto admission = session.admissionState();
    const auto allocator = session.allocatorMarkers();
    const auto& journal = *session.journal_;
    if (admission.processedThrough > admission.admittedThrough
        || admission.admittedThrough.value() - admission.processedThrough.value() != session.pendingCount_
        || session.pendingCount_ > session.limits_.pendingCommands
        || (!admission.open && session.pendingCount_ != 0)
        || allocator.issuedThrough > allocator.reservedThrough
        || journal.modelReleasedThrough() > journal.appendedThrough()
        || journal.appendedThrough().value() - journal.modelReleasedThrough().value() != journal.size()
        || session.receipts_.size() > session.limits_.receipts
        || session.receipts_.size() > admission.processedThrough.value()
        || state.history.applied > state.history.count || state.history.count > session.limits_.historyEntries
        || state.history.partCount > session.limits_.dormantParts || state.history.buildCount > session.limits_.dormantBuilds)
        return reject(RecoveryError::InconsistentSession);
    // Check all count-driven owned storage before allocating/copying. BuildModel
    // acceleration vectors/backend residency are not part of a recovery image.
    size_t owned = sizeof(LogicalRecoveryCheckpoint);
    const auto charge = [&](size_t count, size_t bytes) {
        if (owned > maximumOwnedBytes || (bytes && count > (maximumOwnedBytes - owned) / bytes)) return false;
        owned += count * bytes; return true;
    };
    if (!charge(state.builds.size(), sizeof(BuildSnapshot))
        || !charge(state.entitlements.size(), sizeof(DurableId))
        || !charge(state.storedParts.size(),sizeof(construction::PartInstance))
        || !charge(state.starterKits.size(),sizeof(StarterKit)) || !charge(1,starterKitPayloadBytes(state.starterKits))
        || !charge(state.cargoDefinitions.size(), sizeof(CargoDefinition))
        || !charge(state.cargo.size(), sizeof(CargoRecord)) || !charge(state.jobs.size(), sizeof(JobRecord)))
        return reject(RecoveryError::Capacity);
    for (const auto& build : state.builds)
        if (!charge(build.snapshot().parts.size(), sizeof(construction::PartInstance))
            || !charge(build.snapshot().connections.size(), sizeof(construction::Connection)))
            return reject(RecoveryError::Capacity);
    if(!charge(1,ownedExtraBytes(state.history)))return reject(RecoveryError::Capacity);
    for(size_t i=0;i<session.pendingCount_;++i)
        if(!charge(1,ownedExtraBytes(session.pending_[i]->command)))return reject(RecoveryError::Capacity);
    for(const auto& retained:session.receipts_)
        if(!charge(1,ownedExtraBytes(retained.command)))return reject(RecoveryError::Capacity);
    if(!charge(1,journal.retainedBytes()-journal.size()*kJournalRecordStorageBytes))return reject(RecoveryError::Capacity);
    try {
        auto result = std::make_unique<LogicalRecoveryCheckpoint>();
        result->content = content;
        auto& accepted = result->accepted;
        accepted.world = session.world_; accepted.caller = session.caller_; accepted.epoch = session.epoch_;
        accepted.lastIssuedId = allocator.issuedThrough; accepted.revision = state.revision;
        accepted.tick = session.tick_; accepted.inventory = state.inventory;
        accepted.limits = session.limits_; accepted.workshopEnabled = session.workshopEnabled_;
        accepted.builds.reserve(state.builds.size());
        for (const auto& build : state.builds) accepted.builds.push_back(build.snapshot());
        accepted.starterEntitlements = state.entitlements;accepted.storedParts=state.storedParts;accepted.starterKits=state.starterKits;
        accepted.cargoDefinitions = state.cargoDefinitions; accepted.cargo = state.cargo; accepted.jobs = state.jobs;
        result->admission = admission; result->allocator = allocator;
        result->origin = session.recoveryOrigin_;
        result->journalIdentity = journal.identity(); result->coveredThrough = journal.appendedThrough();
        result->modelReleasedThrough = journal.modelReleasedThrough();
        result->history = state.history;
        result->pendingCount = session.pendingCount_;
        for (size_t i = 0; i < session.pendingCount_; ++i) {
            const auto& work = *session.pending_[i];
            if (work.command.sequence.value() != admission.processedThrough.value() + i + 1
                || !work.receipt.journal || work.receipt.state != ReceiptState::PendingPreparation
                || (work.rejected && work.finalIssue.error == SessionError::None))
                return reject(RecoveryError::InconsistentSession);
            auto& pending = result->pending[i];
            pending.command = work.command;
            pending.state = work.rejected ? (work.finalIssue.error == SessionError::Canceled
                ? RecoveryPendingState::CancelReady : RecoveryPendingState::RejectReady)
                : work.preparation == PreparationState::Ready ? RecoveryPendingState::Ready : RecoveryPendingState::Preparing;
            if (std::holds_alternative<AddPart>(work.command.intent) || std::holds_alternative<CreateBuild>(work.command.intent))
                pending.assignedObject = work.committedObject;
            pending.assignedRange=work.assignedRange;
            pending.buildError = work.finalIssue.build.error; pending.error = work.finalIssue.error;
            pending.issueObject = work.finalIssue.build.object;
            pending.admissionJournal = *work.receipt.journal;
            pending.observedExpectedSequence = work.receipt.expectedSequence;
        }
        result->receiptCount = session.receipts_.size();
        for (size_t i = 0; i < session.receipts_.size(); ++i) {
            const auto& retained = session.receipts_[i];
            const auto& receipt = retained.receipt;
            if (!receipt.journal || receipt.state == ReceiptState::PendingPreparation
                || retained.command.sequence.value() != admission.processedThrough.value() - session.receipts_.size() + i + 1)
                return reject(RecoveryError::InconsistentSession);
            result->receipts[i] = {retained.command,
                {receipt.state, receipt.issue.error, receipt.issue.build.error, receipt.issue.build.object,
                    receipt.revision, receipt.buildRevision, receipt.object},
                receipt.expectedSequence, *receipt.journal, receipt.durability};
        }
        result->retainedJournalCount = journal.copyPrefix(result->retainedJournal);
        result->ownedBytes = owned;
        issue = {};
        return result;
    } catch (const std::bad_alloc&) { return reject(RecoveryError::Capacity); }
}


std::unique_ptr<GameSession> SessionRecovery::instantiate(
    const LogicalRecoveryCheckpoint& image, EventStreamIncarnation incarnation,
    const PartCatalog& catalog, PreparationAdapter& adapter, RecoveryIssue& issue) {
    SessionIssue sessionIssue;
    auto session = GameSession::createBase(image.accepted, catalog, adapter, sessionIssue);
    if (!session) { issue = {RecoveryError::Capacity}; return nullptr; }
    // These are unpublished construction-time values, never a live reset.
    session->admissionGeneration_ = image.admission.generation;
    session->retiredAdmissionThrough_ = image.admission.retiredThrough;
    session->recoveryOrigin_ = image.origin;
    session->reservedThroughId_ = image.allocator.reservedThrough;
    session->state_->history.generation = image.history.generation;
    if (!session->initializeJournal(image.journalIdentity,
        std::span(image.retainedJournal).first(image.retainedJournalCount), sessionIssue)) {
        issue = {RecoveryError::Capacity}; return nullptr;
    }
    session->events_ = SessionEventHub::create({image.accepted.world, image.accepted.epoch, incarnation});
    if (!session->events_) { issue = {RecoveryError::Capacity}; return nullptr; }
    return session;
}

JournalError SessionRecovery::releaseModelWrittenPrefix(GameSession& session, std::span<const JournalRecord> prefix) noexcept {
    return session.journal_->releaseModelWrittenPrefix(prefix);
}

} // namespace voxy::game::expedition
