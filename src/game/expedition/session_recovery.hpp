#pragma once

#include "game/expedition/game_session.hpp"
#include <utility>

namespace voxy::game::expedition {

// Trusted composition/storage surface, never a browser intent. The composition
// root supplies identity from its verified content manifest, not from a client.
// ASSET-07/SAVE define production manifest/byte encoding. These are owned logical
// records; native struct bytes and caller-provided hashes are not storage proof.
struct RecoveryContentIdentity {
    ContentKey manifest{};
    std::array<std::byte, 32> manifestDigest{};
    uint32_t profile = 1; // Initial local volatile workshop profile only.
    uint32_t profileVersion = 1;
    [[nodiscard]] bool operator==(const RecoveryContentIdentity&) const = default;
};
struct RecoveryHistoryEntry {
    HistoryEntryId id{};
    CallerContext caller{};
    AdmissionGeneration admission{1};
    BuildTransition edit{};
    ResourceAmounts debit{}, credit{};
    [[nodiscard]] bool operator==(const RecoveryHistoryEntry&) const = default;
};
// Included solely to validate journal deltas after checkpoint coverage. Restore
// clears it before admitting a fresh token; it cannot grant saved undo.
struct RecoveryHistory {
    HistoryGeneration generation{1};
    std::array<RecoveryHistoryEntry, 32> entries{};
    std::array<construction::PartInstance, 64> parts{};
    std::array<BuildHeader, 8> builds{};
    size_t count = 0, applied = 0, partCount = 0, buildCount = 0;
};
[[nodiscard]] inline size_t ownedExtraBytes(const RecoveryHistory& history) noexcept {
    size_t bytes=0;
    for(size_t i=0;i<history.count;++i)bytes+=ownedExtraBytes(history.entries[i].edit);
    return bytes;
}
[[nodiscard]] inline size_t historyPayloadBytes(const RecoveryHistory& history) noexcept {
    return history.count*sizeof(RecoveryHistoryEntry)+history.partCount*sizeof(construction::PartInstance)
        +history.buildCount*sizeof(BuildHeader)+ownedExtraBytes(history);
}
namespace detail {
// Bounded private projection shared by authority and exact-delta validation.
// Input/output must be distinct; this cannot install history into a session.
[[nodiscard]] bool projectEntitlementRetirement(const RecoveryHistory&, DurableId,
    RecoveryHistory&, EntitlementHistoryDelta&) noexcept;
}
enum class RecoveryPendingState : uint8_t {
    Preparing, Ready, RejectReady, CancelReady,
    // A journal admission does not attest preparation progress or its failure.
    // Used only by replay until the exact terminal decision is encountered.
    JournalAdmitted,
};
struct RecoveryPending {
    Command command{};
    RecoveryPendingState state = RecoveryPendingState::Preparing;
    // Assigned logical identity, including canceled proposals. No adapter
    // ticket, candidate model, resident resources or debit reservation survives.
    std::optional<DurableId> assignedObject{};
    construction::BuildError buildError = construction::BuildError::None;
    SessionError error = SessionError::None;
    DurableId issueObject{};
    JournalSequence admissionJournal{};
    RequestSequence observedExpectedSequence{};
    AssignedIdRange assignedRange{};
    [[nodiscard]] bool operator==(const RecoveryPending&) const = default;
};
struct RecoveryReceipt {
    Command command{};
    DecisionOutcome outcome{};
    RequestSequence observedExpectedSequence{};
    JournalSequence decisionJournal{};
    ReceiptDurability durability = ReceiptDurability::Volatile;
    [[nodiscard]] bool operator==(const RecoveryReceipt&) const = default;
};
inline constexpr uint32_t kLogicalRecoverySchema = 1;
inline constexpr size_t kMaximumLogicalRecoveryBytes = 2 * 1024 * 1024;
struct LogicalRecoveryCheckpoint {
    uint32_t schema = kLogicalRecoverySchema;
    RecoveryContentIdentity content{};
    SessionBootstrap accepted{};
    AdmissionState admission{};
    AllocatorMarkers allocator{};
    JournalIdentity journalIdentity{};
    std::optional<RecoveryOrigin> origin{};
    JournalFrontier coveredThrough{};
    JournalFrontier modelReleasedThrough{};
    bool historyClearedOnRestore = true;
    ReceiptDurability durability = ReceiptDurability::Volatile;
    RecoveryHistory history{};
    std::array<RecoveryPending, 2> pending{};
    size_t pendingCount = 0;
    std::array<RecoveryReceipt, 64> receipts{};
    size_t receiptCount = 0;
    // Exact still-retained transport prefix, already covered by accepted state
    // and the pending interval. Replay starts strictly AFTER coveredThrough.
    std::array<JournalRecord, kMaximumJournalRecords> retainedJournal{};
    size_t retainedJournalCount = 0;
    size_t ownedBytes = 0; // Fixed object + copied vector elements, not SAVE bytes.
};
// Counts every retained reference conservatively, including shared command and
// delta payloads. Caller validates fixed-array counts before invoking this.
[[nodiscard]] inline size_t ownedExtraBytes(const LogicalRecoveryCheckpoint& image) noexcept {
    size_t bytes=ownedExtraBytes(image.history)+starterKitPayloadBytes(image.accepted.starterKits);
    for(size_t i=0;i<image.pendingCount;++i)bytes+=ownedExtraBytes(image.pending[i].command);
    for(size_t i=0;i<image.receiptCount;++i)bytes+=ownedExtraBytes(image.receipts[i].command);
    for(size_t i=0;i<image.retainedJournalCount;++i)bytes+=ownedExtraBytes(image.retainedJournal[i]);
    return bytes;
}
enum class RecoveryError : uint8_t {
    None, InvalidContentIdentity, Capacity, JournalFault, InconsistentSession,
    UnsupportedSchema, IdentityMismatch, InvalidWorld, InvalidAdmission,
    InvalidRequest, InvalidHistory, InvalidJournal, InvalidCoverage, CounterExhausted, MissingCheckpoint,
};
struct RecoveryIssue {
    RecoveryError error = RecoveryError::None;
    size_t record = 0;
    [[nodiscard]] explicit operator bool() const noexcept { return error != RecoveryError::None; }
};

struct ExpectedRecoveryIdentity {
    WorldNamespace world{};
    RecoveryContentIdentity content{};
};
// This is validated storage data, not an activated GameSession. It owns its
// snapshot; changing the caller's input after admission cannot alter it.
class ValidatedRecoveryCheckpoint {
public:
    [[nodiscard]] const LogicalRecoveryCheckpoint& snapshot() const noexcept { return *image_; }
private:
    friend class SessionRecovery;
    ValidatedRecoveryCheckpoint(std::unique_ptr<LogicalRecoveryCheckpoint> image, const PartCatalog& catalog)
        : image_(std::move(image)), catalog_(catalog) {}
    std::unique_ptr<LogicalRecoveryCheckpoint> image_;
    const PartCatalog& catalog_; // Immutable and must outlive this object.
};

// Trusted host result. Persist the finalized parent and fresh initial image as
// one recoverable lineage before claiming durability. This CPU factory does not
// activate scene physics; a fresh adapter lifetime must be provided by the host.
struct RecoveredGameSession {
    std::unique_ptr<GameSession> session{};
    std::unique_ptr<ValidatedRecoveryCheckpoint> retired{};
    std::unique_ptr<ValidatedRecoveryCheckpoint> initial{};
    std::array<JournalRecord, 3> retirementRecords{};
    size_t retirementRecordCount = 0;
};

class SessionRecovery {
public:
    // Structural/domain admission only. The trusted storage layer must verify
    // integrity/authenticity separately; expected identity is never client input.
    [[nodiscard]] static std::unique_ptr<ValidatedRecoveryCheckpoint> admit(
        const LogicalRecoveryCheckpoint&, ExpectedRecoveryIdentity,
        const PartCatalog&, RecoveryIssue&);
    // Applies at most 64 strictly post-coverage records to a private copy.
    // Wrong writer, overlap, holes or invalid pre-state reject the entire batch.
    // No command submission, allocation of IDs, adapter calls or live authority.
    // Already-covered transport detail may be compacted in the returned image;
    // this is not a storage acknowledgment. Input is unchanged on every failure.
    [[nodiscard]] static std::unique_ptr<ValidatedRecoveryCheckpoint> replay(
        const ValidatedRecoveryCheckpoint&, JournalIdentity,
        std::span<const JournalRecord>, RecoveryIssue&);
    // Exclusive world ownership: the old live session/backend must already be
    // retired. Consumes the validated input only after complete success. Failed
    // allocation/validation leaves it reusable; no live session is partly reset.
    // The storage host must select the latest lineage, never reopen an old save
    // concurrently, and persist the transition before promising crash safety.
    [[nodiscard]] static std::unique_ptr<RecoveredGameSession> restore(
        std::unique_ptr<ValidatedRecoveryCheckpoint>&, EventStreamIncarnation, PreparationAdapter&, RecoveryIssue&);
    // Owning thread only, between authority operations; no concurrent callbacks.
    // Failed allocation/preflight leaves authority, history and transport intact.
    [[nodiscard]] static std::unique_ptr<LogicalRecoveryCheckpoint> capture(
        const GameSession&, RecoveryContentIdentity, RecoveryIssue&,
        size_t maximumOwnedBytes = kMaximumLogicalRecoveryBytes);
    // Controllable RAM crash-model seam only. Exact typed-prefix matching frees
    // transport slots; it never marks a receipt durable or changes authority.
    [[nodiscard]] static JournalError releaseModelWrittenPrefix(
        GameSession&, std::span<const JournalRecord>) noexcept;
private:
    [[nodiscard]] static std::unique_ptr<GameSession> instantiate(
        const LogicalRecoveryCheckpoint&, EventStreamIncarnation, const PartCatalog&, PreparationAdapter&, RecoveryIssue&);
};

} // namespace voxy::game::expedition
