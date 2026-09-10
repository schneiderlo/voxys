#pragma once

#include "game/expedition/session_events.hpp"

namespace voxy::game::expedition {

inline constexpr size_t kMaximumReceiptOwnedBytes=256*1024;

struct SessionLimits {
    uint32_t builds = 8;
    uint32_t totalParts = 256;
    uint32_t cargo = 64;
    uint32_t jobs = 32;
    uint32_t receipts = 64;
    uint32_t pendingCommands = 2;
    uint32_t journalRecords = kMaximumJournalRecords;
    size_t journalBytes = kMaximumJournalPayloadBytes;
    size_t candidateBytes = 512 * 1024;
    uint32_t historyEntries = 32;
    size_t historyBytes = 256 * 1024;
    uint32_t dormantParts = 64;
    uint32_t dormantBuilds = 8;
};
struct SessionBootstrap {
    WorldNamespace world{};
    CallerContext caller{};
    AuthorityEpoch epoch{1};
    uint64_t lastIssuedId = 0;
    SessionRevision revision{};
    SimulationTick tick{};
    ResourceAmounts inventory{};
    SessionLimits limits{};
    bool workshopEnabled = false; // Static cove never grants a workshop implicitly.
    std::vector<BuildSnapshot> builds{};
    std::vector<DurableId> starterEntitlements{};
    std::vector<StarterKit> starterKits{};
    std::vector<construction::PartInstance> storedParts{};
    std::vector<CargoDefinition> cargoDefinitions{};
    std::vector<CargoRecord> cargo{};
    std::vector<JobRecord> jobs{};
};

struct PreparationTicket {
    AuthorityEpoch epoch{};
    uint64_t generation = 0;
    [[nodiscard]] bool operator==(const PreparationTicket&) const = default;
};
struct PreparationRequest {
    PreparationTicket ticket{};
    SessionRevision baseRevision{};
    SessionRevision targetRevision{};
    // Borrowed only during begin(). An asynchronous adapter copies what it needs.
    const BuildSnapshot* changedBuild = nullptr;
    uint32_t totalParts = 0;
    uint32_t totalConnections = 0;
    // An empty-build undo removes a build, which cannot be represented by a
    // null changedBuild alone (AcceptJob also has no changed build).
    std::optional<DurableId> removedBuild{};
    const CargoDeliveryTransition* delivery = nullptr; // Borrowed during begin only.
    const StarterKit* starterKit = nullptr; // Candidate grant, borrowed during begin only.
    const PartStorageTransition* storage = nullptr; // Ownership service, borrowed during begin only.
    const CutWeld* cut = nullptr; // Tool/reach and multi-root admission required; borrowed during begin only.
};
enum class PreparationState : uint8_t { Pending, Ready, Rejected };
struct PreparationResult {
    PreparationTicket ticket{};
    PreparationState state = PreparationState::Pending;
};
// Exclusive to this session. The catalog and adapter must outlive GameSession.
// Do not reuse an adapter for another session until every prior active/pending/
// retiring resource is gone: ticket generations are scoped to this lifetime.
// Preparation never mutates accepted physics. Activation is a preallocated,
// non-throwing publication; discard retains in-flight resources until safe.
class PreparationAdapter {
public:
    virtual ~PreparationAdapter() = default;
    [[nodiscard]] virtual PreparationResult begin(const PreparationRequest&) = 0;
    [[nodiscard]] virtual PreparationResult poll(PreparationTicket) noexcept = 0;
    [[nodiscard]] virtual bool canActivate(PreparationTicket) const noexcept = 0;
    virtual void activate(PreparationTicket, SimulationTick) noexcept = 0;
    virtual void discard(PreparationTicket) noexcept = 0;
    // Real execution separates queuing changes for a future tick from publishing
    // accepted canonical state after that tick's validated GPU evidence arrives.
    // Legacy/fake adapters cannot opt into this boundary accidentally.
    [[nodiscard]] virtual PreparationState stage(PreparationTicket, SimulationTick) noexcept { return PreparationState::Rejected; }
    [[nodiscard]] virtual bool executionComplete(PreparationTicket, SimulationTick) const noexcept { return false; }
};

struct SessionSnapshot {
    SessionRevision revision{};
    SimulationTick tick{};
    ResourceAmounts inventory{};
    std::vector<BuildSnapshot> builds{};
    std::vector<construction::PartInstance> storedParts{};
    std::vector<CargoDefinition> cargoDefinitions{};
    std::vector<CargoRecord> cargo{};
    std::vector<JobRecord> jobs{};
};

struct HistoryChoice {
    HistoryEntryId entry{};
    BuildTarget target{}; // Current revision, including dormant build high-water.
    [[nodiscard]] bool operator==(const HistoryChoice&) const = default;
};
struct HistorySummary {
    HistoryGeneration generation{1};
    uint32_t entries = 0, applied = 0, dormantParts = 0, dormantBuilds = 0;
    size_t payloadBytes = 0;
    std::optional<HistoryChoice> undo{}, redo{};
    [[nodiscard]] bool operator==(const HistorySummary&) const = default;
};

enum class EntitlementRetirementError : uint8_t {
    None, Closed, InvalidIdentity, Inactive, ActiveLoans, StaleRevision,
    RevisionExhausted, HistoryExhausted, JournalCapacity, JournalFault, Busy,
};
struct EntitlementRetirementReceipt {
    EntitlementRetirementError error = EntitlementRetirementError::None;
    SessionRevision revision{};
    std::optional<JournalSequence> journal{};
    ReceiptDurability durability = ReceiptDurability::Volatile;
};

enum class ObservationIngress : uint8_t {
    Authentication, Ordering, ConflictingRetry, Capacity, Closed, Other, ExactRetry,
};
struct ObservationDiagnostics {
    std::array<SaturatingEventCount, 7> ingress{};
    bool publicationLost = false; // Current incarnation; also see lane invalid/exhausted status.
    [[nodiscard]] bool operator==(const ObservationDiagnostics&) const = default;
};
struct ObservedReceipt {
    ReceiptChangedEvent receipt{};
    SessionRevision revision{};
    std::optional<EventBuildReference> build{};
    [[nodiscard]] bool operator==(const ObservedReceipt&) const = default;
};
inline constexpr size_t kMaximumEventBaselineBytes = 8 * 1024 * 1024;
struct EventBaseline {
    EventStreamIdentity stream{};
    DurableId participant{}; // No admission token, request intent or private escrow.
    SessionSnapshot state{};
    std::vector<DurableId> starterEntitlements{};
    bool workshopEnabled = false;
    AdmissionState admission{};
    HistorySummary history{};
    std::array<ObservedReceipt, 66> receipts{}; // Ordered retained terminal interval then ≤2 pending.
    size_t receiptCount = 0;
    std::array<EventCursor, 3> cursors{};
    std::array<EventLaneStats, 3> lanes{};
    ObservationDiagnostics diagnostics{};
    size_t ownedBytes = 0; // Fixed object + copied vector elements, not serialized storage.
};

class GameSession {
public:
    [[nodiscard]] static std::unique_ptr<GameSession> create(
        const SessionBootstrap&, EventStreamIncarnation, const PartCatalog&, PreparationAdapter&, SessionIssue&);
    // Read-only canonical validation. Does not create an event stream, journal,
    // live authority capability or backend preparation. May allocate temporary models.
    [[nodiscard]] static SessionIssue validateInitialState(const SessionBootstrap&, const PartCatalog&);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    [[nodiscard]] Receipt submit(CallerContext, const Command&);
    // Authenticated control of one's existing request, not a second spend/undo.
    [[nodiscard]] Receipt cancel(CallerContext, RequestSequence);
    [[nodiscard]] Receipt receipt(CallerContext, RequestSequence) const;
    // Trusted host lifecycle only. Requires no active part using this exact
    // entitlement; remove such geometry through the normal adapter path first.
    // Revokes affected undo/escrow, invalidates prepared work, and never pays or
    // grants parts. An inactive retry makes no further change or journal record.
    [[nodiscard]] EntitlementRetirementReceipt retireStarterEntitlement(
        DurableId entitlement, SessionRevision expectedRevision) noexcept;
    // Trusted content registration, never a player grant or fresh bootstrap.
    // Binds the original recipe/loan IDs in legacy worlds; exact retries are
    // no-ops. A conflicting policy, stale revision or active transaction refuses.
    // This remains volatile until the storage host publishes its checkpoint.
    [[nodiscard]] SessionIssue registerStarterKit(const StarterKit&,SessionRevision) noexcept;
    // Borrowed until the next session operation/publication.
    [[nodiscard]] const StarterKit* starterKit(DurableId build) const noexcept;
    // Trusted scene teardown; bypasses the economic queue, never reopens.
    void closeAdmission() noexcept;
    [[nodiscard]] bool admissionOpen() const noexcept { return admissionOpen_; }
    void pollPreparation(); // Does not advance time or publish accepted state.
    // Trusted fake 60 Hz boundary driver only; never called by a UI status timer.
    // Real submitted/completed physics scheduling belongs to SIM-04/06.
    [[nodiscard]] bool advanceOneTick();
    // Trusted simulation owner only. UI cannot supply incarnation/tick evidence.
    // Bind once; then stage a ready transaction strictly after submitted work,
    // and confirm only from the owned physics frontier and adapter's required
    // body/event observations. The fake clock is disabled after binding.
    [[nodiscard]] bool bindExecution(uint64_t incarnation, SimulationTick baseTick) noexcept;
    [[nodiscard]] bool stageExecution(uint64_t incarnation, SimulationTick nextTick);
    [[nodiscard]] bool confirmExecution(uint64_t incarnation, SimulationTick completed);
    [[nodiscard]] bool executionInFlight() const noexcept { return executionTick_.has_value(); }
    [[nodiscard]] SessionSnapshot snapshot() const;
    [[nodiscard]] uint64_t lastIssuedId() const noexcept;
    [[nodiscard]] uint64_t processedSequence() const noexcept { return processedSequence_; }
    [[nodiscard]] size_t retainedReceiptCount() const noexcept { return receipts_.size(); }
    [[nodiscard]] bool hasPending() const noexcept { return pendingCount_ != 0; }
    [[nodiscard]] size_t pendingCount() const noexcept { return pendingCount_; }
    [[nodiscard]] AdmissionState admissionState() const noexcept {
        return {admissionGeneration_, admissionOpen_ ? retiredAdmissionThrough_ : AdmissionFrontier{admissionGeneration_.value()},
            RequestFrontier{admittedSequence_}, RequestFrontier{processedSequence_}, admissionOpen_};
    }
    [[nodiscard]] AllocatorMarkers allocatorMarkers() const noexcept { return {ids_.lastIssued(), reservedThroughId_}; }
    [[nodiscard]] const JournalOutbox& journal() const noexcept { return *journal_; }
    [[nodiscard]] bool journalFaulted() const noexcept { return journalFault_; }
    // Copies only bounded choices/counts; never exposes inverse object images.
    [[nodiscard]] HistorySummary history() const noexcept;
    [[nodiscard]] SessionEventReader events() const noexcept { return events_->reader(); }
    [[nodiscard]] ObservationDiagnostics observationDiagnostics() const noexcept { return observationDiagnostics_; }
    // Synchronous owner boundary; copies state and cursors together, with no
    // callbacks/suspension. Failure leaves source and all reader cursors untouched.
    [[nodiscard]] std::unique_ptr<EventBaseline> eventBaseline(SessionIssue&,
        size_t maximumOwnedBytes = kMaximumEventBaselineBytes) const;
    // Trusted owner supplies a fresh public identity. Stable reader views remain
    // valid, but old cursors reject and require a new baseline. Never resets state.
    [[nodiscard]] bool restartEventStream(EventStreamIncarnation, SessionIssue&) noexcept;

private:
    friend class SessionRecovery;
    struct State;
    struct Pending;
    struct Retained { Command command{}; Receipt receipt{}; };
    GameSession(const SessionBootstrap&, const PartCatalog&, PreparationAdapter&);
    [[nodiscard]] static std::unique_ptr<GameSession> createBase(
        const SessionBootstrap&, const PartCatalog&, PreparationAdapter&, SessionIssue&);
    [[nodiscard]] bool initializeJournal(JournalIdentity, std::span<const JournalRecord>, SessionIssue&);
    [[nodiscard]] SessionIssue validateBootstrap(const SessionBootstrap&);
    [[nodiscard]] SessionError authenticate(CallerContext) const noexcept;
    [[nodiscard]] RequestSequence expectedSequence() const noexcept;
    [[nodiscard]] Receipt rejected(RequestSequence, SessionError) const noexcept;
    [[nodiscard]] SessionIssue prepare(const Command&, Pending&);
    [[nodiscard]] SessionIssue checkTarget(const BuildTarget&, const State&, SimulationTick, bool allowDormant = false) const;
    [[nodiscard]] SessionIssue prepareCompensation(const Command&, Pending&);
    [[nodiscard]] SessionIssue recordHistory(Pending&);
    [[nodiscard]] SessionIssue evictHistoryFront(Pending&);
    [[nodiscard]] SessionIssue reserveDebit(Pending&, ResourceAmounts) const;
    void retain(const Command&, Receipt) noexcept;
    void rejectPending(Pending&, SessionIssue) noexcept;
    void drainRejected() noexcept;
    void releasePreparation(Pending&) noexcept;
    void finishFront(Receipt) noexcept;
    [[nodiscard]] std::optional<DurableId> allocateId(Pending&);
    [[nodiscard]] JournalRecord decisionRecord(const Pending&, const Receipt&) const noexcept;
    [[nodiscard]] SessionIssue revalidate(const Pending&, SimulationTick) const;
    [[nodiscard]] EventDraft receiptEvent(const Command&, const Receipt&) const noexcept;
    [[nodiscard]] EventDraft stateEvent(const RequestDecisionRecord&) const noexcept;
    void publishEvent(EventDraft) noexcept;
    void noteIngress(SessionError) noexcept;

    const PartCatalog& catalog_;
    PreparationAdapter& adapter_;
    WorldNamespace world_{};
    CallerContext caller_{};
    AuthorityEpoch epoch_{};
    SessionLimits limits_{};
    bool workshopEnabled_ = false;
    bool admissionOpen_ = true;
    AdmissionGeneration admissionGeneration_{1};
    AdmissionFrontier retiredAdmissionThrough_{};
    std::optional<RecoveryOrigin> recoveryOrigin_{};
    construction::IdAllocator ids_;
    SimulationTick tick_{};
    uint64_t executionIncarnation_=0;
    std::optional<SimulationTick> executionTick_{};
    uint64_t processedSequence_ = 0;
    uint64_t admittedSequence_ = 0;
    uint64_t reservedThroughId_ = 0;
    std::unique_ptr<JournalOutbox> journal_;
    std::unique_ptr<SessionEventHub> events_;
    ObservationDiagnostics observationDiagnostics_{};
    JournalReservation closureReservation_{};
    bool journalFault_ = false;
    uint64_t preparationGeneration_ = 0;
    std::unique_ptr<State> state_;
    std::array<std::unique_ptr<Pending>, 2> pending_{};
    size_t pendingCount_ = 0;
    std::vector<Retained> receipts_{};
};

} // namespace voxy::game::expedition
