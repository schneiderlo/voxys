#pragma once

#include "game/construction/build_refit.hpp"
#include <memory>

namespace voxy::game::expedition {

namespace construction = voxy::game::construction;
using construction::AuthorityEpoch;
using construction::BuildModel;
using construction::BuildSnapshot;
using construction::ContentKey;
using construction::DurableId;
using construction::GridTransform;
using construction::PartCatalog;
using construction::RequestSequence;
using construction::ResourceAmounts;
using construction::SimulationTick;
using construction::TopologyRevision;
using construction::WorldNamespace;

struct SessionRevisionTag;
using SessionRevision = construction::Counter<SessionRevisionTag, true>;

// DATA-05 journal and recovery counters have distinct domains. Frontiers admit
// zero; identities and generations do not. No counter is a simulation clock.
struct RequestFrontierTag;
struct AdmissionGenerationTag;
struct AdmissionFrontierTag;
struct HistoryEntryIdTag;
struct HistoryGenerationTag;
struct JournalSequenceTag;
struct JournalFrontierTag;
struct JournalWriterGenerationTag;
using RequestFrontier = construction::Counter<RequestFrontierTag, true>;
using AdmissionGeneration = construction::Counter<AdmissionGenerationTag, false>;
using AdmissionFrontier = construction::Counter<AdmissionFrontierTag, true>;
using HistoryEntryId = construction::Counter<HistoryEntryIdTag, false>;
using HistoryGeneration = construction::Counter<HistoryGenerationTag, false>;
using JournalSequence = construction::Counter<JournalSequenceTag, false>;
using JournalFrontier = construction::Counter<JournalFrontierTag, true>;
using JournalWriterGeneration = construction::Counter<JournalWriterGenerationTag, false>;

struct CallerContext {
    DurableId participant{};
    DurableId sessionToken{};
    [[nodiscard]] bool operator==(const CallerContext&) const = default;
};

enum class CargoRecoveryRule : uint8_t { ReturnToSite, PreserveUnique };
// Trusted bootstrap content, not player input. Live cargo mechanics and the
// shipping cargo content schema are later work; this slice admits loose cargo.
struct CargoDefinition {
    ContentKey key{};
    double massKg = 0.0;
    double displacedVolumeCubicMetres = 0.0;
    ResourceAmounts value{};
    CargoRecoveryRule recovery = CargoRecoveryRule::ReturnToSite;
    [[nodiscard]] bool operator==(const CargoDefinition&) const = default;
};
struct CargoRecord {
    DurableId id{};
    ContentKey definition{};
    DurableId owner{};
    construction::MetresPosition position{};
    construction::CanonicalQuaternion orientation{};
    std::optional<DurableId> job{};
    [[nodiscard]] bool operator==(const CargoRecord&) const = default;
};
enum class JobPhase : uint8_t { Available, Accepted, Completed };
struct JobRecord {
    DurableId id{};
    uint64_t generation = 1;
    JobPhase phase = JobPhase::Available;
    DurableId acceptedBy{}; // Zero only while Available.
    [[nodiscard]] bool operator==(const JobRecord&) const = default;
};

struct BuildTarget {
    DurableId build{};
    TopologyRevision expectedRevision{};
    [[nodiscard]] bool operator==(const BuildTarget&) const = default;
};
struct CreateBuild { [[nodiscard]] bool operator==(const CreateBuild&) const = default; };
struct AddPart {
    BuildTarget target{};
    ContentKey definition{};
    GridTransform placement{};
    [[nodiscard]] bool operator==(const AddPart&) const = default;
};
struct MovePart {
    BuildTarget target{};
    DurableId part{};
    GridTransform placement{};
    [[nodiscard]] bool operator==(const MovePart&) const = default;
};
struct RemovePart {
    BuildTarget target{};
    DurableId part{};
    [[nodiscard]] bool operator==(const RemovePart&) const = default;
};
struct AcceptJob {
    DurableId job{};
    [[nodiscard]] bool operator==(const AcceptJob&) const = default;
};
struct Undo {
    BuildTarget target{};
    HistoryEntryId entry{};
    HistoryGeneration expectedHistoryGeneration{};
    [[nodiscard]] bool operator==(const Undo&) const = default;
};
struct Redo {
    BuildTarget target{};
    HistoryEntryId entry{};
    HistoryGeneration expectedHistoryGeneration{};
    [[nodiscard]] bool operator==(const Redo&) const = default;
};
struct DeliverCargo {
    DurableId cargo{};
    [[nodiscard]] bool operator==(const DeliverCargo&) const = default;
};
inline constexpr size_t kMaximumStarterKits=8;
struct StarterKit {
    DurableId build{},entitlement{};
    // Trusted content policy, never a player-supplied replacement design.
    std::shared_ptr<const construction::BuildRefitRequest> design{};
    // Recipe-ordinal bindings for the current grant, including removed loans.
    // Replacement changes this whole set atomically; old IDs are never revived.
    std::array<DurableId,32> partIds{};
    uint8_t partCount=0;
    [[nodiscard]] bool operator==(const StarterKit& other) const noexcept {
        return build==other.build&&entitlement==other.entitlement&&partCount==other.partCount&&partIds==other.partIds&&bool(design)==bool(other.design)
            &&(!design||*design==*other.design);
    }
};
[[nodiscard]] inline size_t starterKitPayloadBytes(std::span<const StarterKit> kits) noexcept {
    size_t bytes=0;for(const auto& kit:kits){if(kit.design)bytes+=kit.design->retainedBytes();}return bytes;
}

struct RefitBuild {
    BuildTarget target{};
    std::shared_ptr<const construction::BuildRefitRequest> design{};
    [[nodiscard]] bool operator==(const RefitBuild& other) const noexcept {
        return target==other.target && bool(design)==bool(other.design) && (!design || *design==*other.design);
    }
};
struct RebuildStarter {
    BuildTarget target{};
    [[nodiscard]] bool operator==(const RebuildStarter&) const = default;
};
struct CutWeld {
    BuildTarget target{};
    DurableId connection{};
    [[nodiscard]] bool operator==(const CutWeld&) const = default;
};
using Intent = std::variant<CreateBuild, AddPart, MovePart, RemovePart, AcceptJob, Undo, Redo, DeliverCargo, RefitBuild, RebuildStarter, CutWeld>;
[[nodiscard]] inline bool isRefitIntent(const Intent& intent) noexcept {
    return std::holds_alternative<RefitBuild>(intent)||std::holds_alternative<RebuildStarter>(intent);
}
[[nodiscard]] inline const construction::BuildRefitRequest* refitDesign(const Intent& intent,std::span<const StarterKit> kits) noexcept {
    if(const auto* refit=std::get_if<RefitBuild>(&intent))return refit->design.get();
    if(const auto* rebuild=std::get_if<RebuildStarter>(&intent))
        for(const auto& kit:kits)if(kit.build==rebuild->target.build)return kit.design.get();
    return nullptr;
}
struct Command {
    AuthorityEpoch epoch{1};
    RequestSequence sequence{1};
    SessionRevision expectedRevision{};
    Intent intent{};
    [[nodiscard]] bool operator==(const Command&) const = default;
};

enum class SessionError : uint8_t {
    None, InvalidBootstrap, Capacity, InvalidIdentity, WrongCaller, WrongToken,
    WrongEpoch, SequenceGap, SequenceExhausted, AlreadyProcessed, RequestConflict,
    Busy, StaleRevision, RevisionExhausted, UnknownBuild, UnknownPart, UnknownJob,
    NotOwner, LeaseDenied, InsufficientResources, ResourceOverflow, IdExhausted,
    InvalidBuild, ConnectedPart, JobUnavailable, AdapterRejected, Canceled,
    NoPending, UnsupportedFeature, JournalCapacity, CandidateCapacity, Closed,
    HistoryUnavailable, HistoryConflict, HistoryBusy, HistoryCapacity, HistoryExhausted,
    UnknownCargo, CargoUnavailable, DeliveryNotReady,
};
struct SessionIssue {
    SessionError error = SessionError::None;
    construction::BuildIssue build{};
    [[nodiscard]] explicit operator bool() const noexcept { return error != SessionError::None; }
};
enum class ReceiptState : uint8_t { Rejected, PendingPreparation, Committed };
// Real durable acknowledgment is unavailable until the SAVE storage gates.
enum class ReceiptDurability : uint8_t { Volatile };
struct Receipt {
    RequestSequence sequence{};
    ReceiptState state = ReceiptState::Rejected;
    SessionIssue issue{};
    bool admitted = false;
    RequestSequence expectedSequence{};
    SessionRevision revision{};
    std::optional<TopologyRevision> buildRevision{};
    std::optional<DurableId> object{}; // Populated only at successful commit.
    ReceiptDurability durability = ReceiptDurability::Volatile;
    std::optional<JournalSequence> journal{}; // Admission/terminal observation, never storage proof.
};

struct RequestKey {
    CallerContext caller{};
    AuthorityEpoch epoch{};
    AdmissionGeneration admissionGeneration{};
    RequestSequence sequence{};
    [[nodiscard]] bool operator==(const RequestKey&) const = default;
};
struct AdmissionState {
    AdmissionGeneration generation{1};
    AdmissionFrontier retiredThrough{};
    RequestFrontier admittedThrough{};
    RequestFrontier processedThrough{};
    bool open = true;
    [[nodiscard]] bool operator==(const AdmissionState&) const = default;
};
struct AllocatorMarkers {
    uint64_t issuedThrough = 0;
    uint64_t reservedThrough = 0;
    [[nodiscard]] bool operator==(const AllocatorMarkers&) const = default;
};

// The logical journal uses fixed slots and bounded immutable owned payloads. SAVE-01
// supplies its canonical bytes/digests; native struct bytes are never a format.
// Diagnostic strings/pointers and backend handles are intentionally excluded.
struct BuildHeader {
    DurableId id{};
    TopologyRevision revision{};
    DurableId owner{};
    std::optional<construction::EditLease> editLease{};
    bool materialized = true;
    [[nodiscard]] bool operator==(const BuildHeader&) const = default;
};
// Only the transferred stock images are retained, including their configuration
// before withdrawal. This makes reverse checkpoint validation lossless.
struct PartStorageTransition {
    std::vector<construction::PartInstance> deposited{},withdrawn{};
    std::optional<StarterKit> starterBefore{};
    [[nodiscard]] bool operator==(const PartStorageTransition&) const = default;
    [[nodiscard]] size_t retainedBytes() const noexcept {
        return sizeof(*this)+(deposited.size()+withdrawn.size())*sizeof(construction::PartInstance)
            +(starterBefore?starterKitPayloadBytes({&*starterBefore,1}):0);
    }
};
inline constexpr size_t kMaximumStorageTransitionBytes=16*1024;
struct WeldCutTransition {
    construction::Connection before{};
    [[nodiscard]] construction::Connection after() const noexcept {
        auto result=before;result.enabled=false;result.damage=construction::kFullHealth;return result;
    }
    [[nodiscard]] bool operator==(const WeldCutTransition& other) const noexcept {
        return construction::sameConnection(before,other.before);
    }
};
struct BuildTransition {
    std::optional<BuildHeader> before{};
    std::optional<BuildHeader> after{};
    std::optional<construction::PartInstance> beforePart{};
    std::optional<construction::PartInstance> afterPart{};
    std::shared_ptr<const construction::BuildRefitDelta> refit{};
    bool refitForward = true;
    std::shared_ptr<const PartStorageTransition> storage{};
    std::shared_ptr<const WeldCutTransition> cut{};
    [[nodiscard]] bool operator==(const BuildTransition& other) const noexcept {
        return before==other.before && after==other.after && beforePart==other.beforePart && afterPart==other.afterPart
            && refitForward==other.refitForward && bool(refit)==bool(other.refit) && (!refit || *refit==*other.refit)
            && bool(storage)==bool(other.storage)&&(!storage||*storage==*other.storage)
            && bool(cut)==bool(other.cut)&&(!cut||*cut==*other.cut);
    }
};
// Visits actual transaction direction, including inverses. Legacy one-part
// edits and connected edits share history/escrow accounting without allocations.
template<class F> bool visitPartChanges(const BuildTransition& edit,F&& visit) {
    if(edit.refit) {
        for(const auto& change:edit.refit->parts())
            if(!visit(edit.refitForward?change.before:change.after,edit.refitForward?change.after:change.before))return false;
        return true;
    }
    return (!edit.beforePart && !edit.afterPart) || visit(edit.beforePart,edit.afterPart);
}
[[nodiscard]] inline bool referencesPart(const BuildTransition& edit,DurableId id) noexcept {
    if(edit.refit)for(const auto& change:edit.refit->welds()) {
        const auto& weld=change.before?*change.before:*change.after;
        if(weld.a.part==id || weld.b.part==id)return true;
    }
    return !visitPartChanges(edit,[&](const auto& before,const auto& after){
        return (!before || before->id!=id) && (!after || after->id!=id);
    });
}
[[nodiscard]] inline size_t removedPartCount(const BuildTransition& edit) noexcept {
    size_t count=0;
    visitPartChanges(edit,[&](const auto& before,const auto& after){count+=before&&!after;return true;});
    return count;
}
[[nodiscard]] inline size_t ownedExtraBytes(const Command& command) noexcept {
    const auto* refit=std::get_if<RefitBuild>(&command.intent);
    return refit&&refit->design?refit->design->retainedBytes():0;
}
[[nodiscard]] inline size_t ownedExtraBytes(const BuildTransition& edit) noexcept {
    return (edit.refit?edit.refit->retainedBytes():0)+(edit.storage?edit.storage->retainedBytes():0)
        +(edit.cut?sizeof(WeldCutTransition):0);
}
struct JobTransition {
    JobRecord before{};
    JobRecord after{};
    [[nodiscard]] bool operator==(const JobTransition&) const = default;
};
struct CargoDeliveryTransition {
    CargoRecord cargo{}; // Removed exactly once from loose cargo.
    JobRecord before{},after{};
    ResourceAmounts reward{};
    [[nodiscard]] bool operator==(const CargoDeliveryTransition&) const = default;
};
using ObjectTransition = std::variant<std::monostate, BuildTransition, JobTransition, CargoDeliveryTransition>;
enum class HistoryAction : uint8_t { None, Record, Undo, Redo, Clear, Service };
struct HistoryTransition {
    HistoryGeneration before{1};
    HistoryGeneration after{1};
    HistoryAction action = HistoryAction::None;
    std::optional<HistoryEntryId> entry{};
    // IDs of history entries removed by this decision, not new object IDs.
    std::array<HistoryEntryId, 32> evicted{};
    uint8_t evictedCount = 0;
    [[nodiscard]] bool operator==(const HistoryTransition&) const = default;
};
struct DecisionOutcome {
    ReceiptState state = ReceiptState::Rejected;
    SessionError error = SessionError::None;
    construction::BuildError buildError = construction::BuildError::None;
    DurableId issueObject{};
    SessionRevision revision{};
    std::optional<TopologyRevision> buildRevision{};
    std::optional<DurableId> object{};
    [[nodiscard]] bool operator==(const DecisionOutcome&) const = default;
};
struct AssignedIdRange {
    DurableId first{};
    uint32_t count=0;
    [[nodiscard]] bool operator==(const AssignedIdRange&) const = default;
};
[[nodiscard]] bool validAssignedRange(AssignedIdRange,WorldNamespace,uint64_t issuedThrough) noexcept;
struct RequestAdmissionRecord {
    RequestKey key{};
    Command command{};
    RequestFrontier admittedBefore{};
    RequestFrontier processedThrough{};
    std::optional<DurableId> assignedObject{};
    AllocatorMarkers allocator{};
    AssignedIdRange assignedRange{}; // Refit parts/welds only; includes burned preparation IDs.
    [[nodiscard]] bool operator==(const RequestAdmissionRecord&) const = default;
};
struct RequestDecisionRecord {
    RequestKey key{};
    Command command{};
    SessionRevision beforeRevision{};
    SessionRevision afterRevision{};
    RequestFrontier processedBefore{};
    ResourceAmounts balanceBefore{};
    ResourceAmounts balanceAfter{};
    ObjectTransition objects{};
    HistoryTransition history{};
    AllocatorMarkers allocator{};
    DecisionOutcome outcome{};
    [[nodiscard]] bool operator==(const RequestDecisionRecord&) const = default;
};
struct AllocatorLeaseRecord {
    uint64_t issuedThrough = 0;
    uint64_t reservedBefore = 0;
    uint64_t reservedAfter = 0;
    [[nodiscard]] bool operator==(const AllocatorLeaseRecord&) const = default;
};
struct JournalIdentity {
    WorldNamespace world{};
    AuthorityEpoch epoch{};
    JournalWriterGeneration writerGeneration{1};
    [[nodiscard]] bool operator==(const JournalIdentity&) const = default;
};
struct AdmissionOpenedRecord {
    CallerContext caller{};
    AdmissionGeneration generation{1};
    AdmissionFrontier retiredThrough{};
    AllocatorMarkers allocator{};
    [[nodiscard]] bool operator==(const AdmissionOpenedRecord&) const = default;
};
struct AdmissionClosedRecord {
    CallerContext caller{};
    AdmissionGeneration generation{1};
    RequestFrontier processedThrough{};
    [[nodiscard]] bool operator==(const AdmissionClosedRecord&) const = default;
};
// Link to the exact finalized parent writer. Completed decisions stay in that
// bounded retired image/journal; the new token's request sequence starts at 1.
struct RecoveryOrigin {
    JournalIdentity parent{};
    JournalFrontier closedThrough{};
    CallerContext caller{};
    AdmissionGeneration generation{1};
    RequestFrontier processedThrough{};
    AllocatorMarkers allocator{};
    HistoryGeneration historyGeneration{1};
    [[nodiscard]] bool operator==(const RecoveryOrigin&) const = default;
};
enum class RecoveryLeasePolicy : uint8_t { ReleaseLocalParticipantLeases };
struct RecoveryOpenedRecord {
    RecoveryOrigin origin{};
    AdmissionOpenedRecord admission{};
    SessionRevision revision{};
    ResourceAmounts balance{};
    RecoveryLeasePolicy leases = RecoveryLeasePolicy::ReleaseLocalParticipantLeases;
    [[nodiscard]] bool operator==(const RecoveryOpenedRecord&) const = default;
};
struct EntitlementHistoryDelta {
    HistoryGeneration before{1}, after{1};
    std::array<HistoryEntryId, 32> invalidatedEntries{};
    std::array<DurableId, 64> retiredParts{};
    std::array<DurableId, 8> retiredBuilds{};
    uint8_t entryCount = 0, partCount = 0, buildCount = 0;
    [[nodiscard]] bool operator==(const EntitlementHistoryDelta&) const = default;
};
// Trusted lifecycle record, never a player command. Active geometry must have
// been removed through its ordinary authority/adapter path before retirement.
struct EntitlementRetiredRecord {
    CallerContext caller{};
    AdmissionGeneration admission{1};
    DurableId entitlement{};
    SessionRevision beforeRevision{}, afterRevision{};
    ResourceAmounts balance{}; // No debit, refund, or grant.
    RequestFrontier admittedThrough{}, processedThrough{};
    AllocatorMarkers allocator{};
    EntitlementHistoryDelta history{};
    [[nodiscard]] bool operator==(const EntitlementRetiredRecord&) const = default;
};
struct StarterKitRegisteredRecord {
    CallerContext caller{};
    AdmissionGeneration admission{1};
    SessionRevision beforeRevision{},afterRevision{};
    StarterKit kit{};
    ResourceAmounts balance{};
    RequestFrontier admittedThrough{},processedThrough{};
    AllocatorMarkers allocator{};
    [[nodiscard]] bool operator==(const StarterKitRegisteredRecord&) const = default;
};
using JournalPayload = std::variant<RequestAdmissionRecord, RequestDecisionRecord,
    AllocatorLeaseRecord, AdmissionOpenedRecord, AdmissionClosedRecord, RecoveryOpenedRecord, EntitlementRetiredRecord, StarterKitRegisteredRecord>;
inline constexpr uint32_t kLogicalJournalSchema = 1;
struct JournalRecord {
    uint32_t schema = kLogicalJournalSchema;
    WorldNamespace world{};
    AuthorityEpoch epoch{};
    JournalSequence sequence{}; // Zero only before append assigns it.
    SimulationTick tick{};
    JournalPayload payload{};
    // std::variant's nontrivial copy assignment lacks a conditional noexcept
    // guarantee in libstdc++. All alternatives contain only value records and
    // immutable shared ownership; this copy cannot allocate or throw.
    JournalRecord() noexcept = default;
    JournalRecord(uint32_t version,WorldNamespace namespaceId,AuthorityEpoch authority,
                  JournalSequence index,SimulationTick simulation,JournalPayload value) noexcept
        :schema(version),world(namespaceId),epoch(authority),sequence(index),tick(simulation),payload(std::move(value)) {}
    JournalRecord(const JournalRecord&) noexcept = default;
    JournalRecord(JournalRecord&&) noexcept = default;
    JournalRecord& operator=(const JournalRecord&) noexcept = default;
    JournalRecord& operator=(JournalRecord&&) noexcept = default;
    [[nodiscard]] bool operator==(const JournalRecord&) const = default;
};
// Checks one already-assigned logical record without appending or allocating.
// It is schema validation, not contiguous replay or persistence authentication.
[[nodiscard]] bool validAssignedJournalRecord(const JournalRecord&) noexcept;

inline constexpr uint32_t kMaximumJournalRecords = 64;
inline constexpr size_t kMaximumJournalPayloadBytes = 1024 * 1024;
inline constexpr size_t kMaximumJournalRecordBytes = 32 * 1024;
// Slot bytes plus ownedExtraBytes are charged per retained reference (even if
// shared elsewhere). This is a conservative memory budget, never SAVE bytes.
inline constexpr size_t kJournalRecordStorageBytes = sizeof(JournalRecord);
static_assert(kJournalRecordStorageBytes <= kMaximumJournalRecordBytes);
[[nodiscard]] size_t ownedExtraBytes(const JournalRecord&) noexcept;
[[nodiscard]] inline size_t journalRecordBytes(const JournalRecord& record) noexcept {
    return kJournalRecordStorageBytes+ownedExtraBytes(record);
}

struct JournalLimits {
    uint32_t records = kMaximumJournalRecords;
    size_t payloadBytes = kMaximumJournalPayloadBytes;
};
struct JournalSeed {
    JournalFrontier appendedThrough{};
    uint64_t lastReservationGeneration = 0;
};
struct JournalReservation {
    JournalIdentity identity{};
    uint64_t generation = 0;
    uint32_t slot = 0;
    [[nodiscard]] bool operator==(const JournalReservation&) const = default;
};
struct JournalDemand {
    uint32_t records = 0;
    size_t payloadBytes = 0;
};
enum class JournalError : uint8_t {
    None, InvalidIdentity, InvalidLimits, InvalidDemand, Capacity,
    SequenceExhausted, ReservationExhausted, InvalidReservation, InvalidRecord,
    PrefixMismatch,
};
struct JournalReservationResult {
    JournalReservation reservation{};
    JournalError error = JournalError::None;
    [[nodiscard]] explicit operator bool() const noexcept { return error == JournalError::None; }
};

// Trusted single-writer, fixed-slot, payload-budgeted volatile outbox. No overwrite policy.
// A replacement writer for the same world/epoch MUST advance writerGeneration.
// Readers receive copies; reserve/append/release are never browser intents.
class JournalOutbox {
public:
    [[nodiscard]] static std::unique_ptr<JournalOutbox> create(
        JournalIdentity, JournalLimits, JournalSeed, JournalError&);
    JournalOutbox(const JournalOutbox&) = delete;
    JournalOutbox& operator=(const JournalOutbox&) = delete;
    [[nodiscard]] JournalReservationResult reserve(JournalDemand) noexcept;
    [[nodiscard]] bool discard(JournalReservation) noexcept;
    [[nodiscard]] bool canAppend(JournalReservation, const JournalRecord&) const noexcept;
    // Precheck canAppend before canonical publication. With the same record,
    // reservation and no re-entry this succeeds without allocation or throwing.
    [[nodiscard]] std::optional<JournalSequence> appendReserved(
        JournalReservation, const JournalRecord&) noexcept;
    [[nodiscard]] size_t copyPrefix(std::span<JournalRecord>) const noexcept;
    // Exact typed record comparison for the DATA-05 RAM crash model ONLY.
    // It releases transport capacity, never marks a receipt durable. SAVE's
    // authenticated storage capability must establish its own byte/prefix proof.
    [[nodiscard]] JournalError releaseModelWrittenPrefix(std::span<const JournalRecord>) noexcept;
    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] size_t reservedRecords() const noexcept { return reservedRecords_; }
    [[nodiscard]] size_t reservedBytes() const noexcept { return reservedBytes_; }
    [[nodiscard]] size_t retainedBytes() const noexcept { return retainedBytes_; }
    [[nodiscard]] JournalFrontier appendedThrough() const noexcept { return appendedThrough_; }
    [[nodiscard]] JournalFrontier modelReleasedThrough() const noexcept { return modelReleasedThrough_; }
    [[nodiscard]] JournalIdentity identity() const noexcept { return identity_; }
private:
    struct ReservationState { uint64_t generation = 0; uint32_t records = 0; size_t bytes = 0; };
    JournalOutbox(JournalIdentity, JournalLimits, JournalSeed) noexcept;
    [[nodiscard]] const ReservationState* find(JournalReservation) const noexcept;
    JournalIdentity identity_{};
    JournalLimits limits_{};
    JournalFrontier appendedThrough_{};
    JournalFrontier modelReleasedThrough_{};
    uint64_t reservationGeneration_ = 0;
    std::array<JournalRecord, kMaximumJournalRecords> records_{};
    std::array<ReservationState, kMaximumJournalRecords> reservations_{};
    size_t begin_ = 0, count_ = 0, reservedRecords_ = 0, reservedBytes_ = 0, retainedBytes_ = 0;
};

} // namespace voxy::game::expedition
