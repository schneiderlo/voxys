#pragma once

#include "game/wreckwater_match.hpp"
#include "physics/physics_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace voxy::physics {
class PhysicsWorld;
}

namespace voxy::game {

inline constexpr size_t kWreckwaterLiveMaximumEntities = 16u;
inline constexpr size_t kWreckwaterLiveMaximumTows = 8u;
inline constexpr size_t kWreckwaterCertifiedEvidenceFrameCount = 32u;
inline constexpr size_t kWreckwaterCommittedReceiptCount = 32u;

enum class WreckwaterEntityKind : uint32_t {
    Player = 1u,
    Skiff = 2u,
    Cargo = 3u,
};

// This is the only body identity that may leave the live-world adapter.
// BodyHandle remains private to registration, evidence ingestion, and physics
// event translation.
struct WreckwaterEntityKey {
    WreckwaterEntityKind kind = WreckwaterEntityKind::Player;
    uint64_t id = 0u;
    uint32_t generation = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterEntityKey&) const = default;
};

struct WreckwaterEvidenceBody {
    WreckwaterEntityKey entity{};
    physics::WorldPosition position{};
    bool available = true;
};

struct WreckwaterEvidenceFrameView {
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint64_t physicsTick = 0u;
    std::span<const WreckwaterEvidenceBody> bodies{};
};

struct WreckwaterTowBinding {
    AttachmentId attachmentId = 0u;
    uint32_t attachmentGeneration = 0u;
    CargoId cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterTowBinding&) const = default;
};

enum class WreckwaterBindingResult : uint32_t {
    Bound = 0u,
    Rebound,
    AlreadyBound,
    NotInitialized,
    Invalid,
    StaleGeneration,
    DuplicateLogicalEntity,
    DuplicatePhysicsHandle,
    EntityInUse,
    CapacityExceeded,
};

enum class WreckwaterLiveApplyFailure : uint32_t {
    None = 0u,
    InvalidBatch,
    ReceiptConflict,
    NonMonotonicApplicationTick,
    NonCanonicalIntentOrder,
    InvalidIntent,
    MissingEvidenceFrame,
    EvidenceMismatch,
    MissingSourceTow,
    SourceTowMismatch,
    DestroyCapacityExceeded,
    TowCountUnderflow,
    CargoAlreadyTowed,
    TowCapacityExceeded,
    AttachmentIdentityExhausted,
    CreateCapacityExceeded,
    UnboundAttachmentEndpoint,
    PrepareRejected,
    PreparedTargetMismatch,
    PreparedCreateCountMismatch,
    InvalidCreatedAttachment,
    DuplicateCreatedAttachment,
    CreatedAttachmentCollision,
    CommitRejected,
    CommittedTargetMismatch,
    CommittedCountMismatch,
};

[[nodiscard]] const char* wreckwaterLiveApplyFailureName(
    WreckwaterLiveApplyFailure failure) noexcept;

// Stable post-mortem data for the most recent atomic apply attempt. The
// adapter records the exact failed stage without logging gameplay payloads.
struct WreckwaterLiveApplyDiagnostic {
    WreckwaterLiveApplyFailure failure =
        WreckwaterLiveApplyFailure::None;
    uint32_t intentIndex = std::numeric_limits<uint32_t>::max();
    uint32_t intentCount = 0u;
    uint64_t applicationTick = 0u;
    uint64_t physicsEvidenceTick = 0u;
    physics::PhysicsMutationStatus preparedStatus =
        physics::PhysicsMutationStatus::Unsupported;
    uint64_t preparedTargetTick = 0u;
    uint32_t preparedCreatedCount = 0u;
    physics::PhysicsMutationStatus committedStatus =
        physics::PhysicsMutationStatus::Unsupported;
    uint64_t committedTargetTick = 0u;
    uint32_t requestedDestroyCount = 0u;
    uint32_t requestedCreateCount = 0u;
    uint32_t committedBodyCommandCount = 0u;
    uint32_t committedDestroyCount = 0u;
    uint32_t committedCreateCount = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterLiveApplyDiagnostic&) const = default;
};

// Injectable seam used by deterministic tests and by the real PhysicsWorld
// wrapper below. A prepared token is single-consume.
class IWreckwaterPhysicsTransactions {
public:
    virtual ~IWreckwaterPhysicsTransactions() = default;

    [[nodiscard]] virtual physics::PreparedPhysicsMutation prepare(
        const physics::PhysicsMutationBatch& batch) noexcept = 0;
    [[nodiscard]] virtual physics::PhysicsMutationResult commit(
        const physics::PreparedPhysicsMutation& prepared) noexcept = 0;
    [[nodiscard]] virtual bool discard(
        const physics::PreparedPhysicsMutation& prepared) noexcept = 0;
};

class PhysicsWorldWreckwaterTransactions final
    : public IWreckwaterPhysicsTransactions {
public:
    PhysicsWorldWreckwaterTransactions() = default;
    explicit PhysicsWorldWreckwaterTransactions(
        physics::PhysicsWorld& world) noexcept;

    void reset(physics::PhysicsWorld& world) noexcept;

    [[nodiscard]] physics::PreparedPhysicsMutation prepare(
        const physics::PhysicsMutationBatch& batch) noexcept override;
    [[nodiscard]] physics::PhysicsMutationResult commit(
        const physics::PreparedPhysicsMutation& prepared) noexcept override;
    [[nodiscard]] bool discard(
        const physics::PreparedPhysicsMutation& prepared) noexcept override;

private:
    physics::PhysicsWorld* world_ = nullptr;
};

// Production float-authority bridge. The match owns rules and ordering; this
// adapter owns logical-to-GPU identity and one atomic GPU mutation batch.
//
// Known contract limitation: MatchActionQuery carries playerId but no
// independent player physics generation. evaluate() therefore resolves the
// authenticated player ID to the adapter's current private binding. It does
// not misuse connectionGeneration as a physics generation.
class WreckwaterLiveWorld final : public IWreckwaterWorldAuthority {
public:
    struct Config {
        uint64_t matchId = 1u;
        uint64_t worldId = 1u;
        uint32_t worldEpoch = 1u;
        uint32_t authorityEpoch = 1u;
        float maximumInteractionDistance = 14.0f;
        std::array<physics::WorldPosition, kWreckwaterCrewCount>
            extractionCenters{};
        float extractionRadius = 18.0f;
        glm::vec3 skiffLocalAnchor{0.0f, 0.5f, -1.5f};
        glm::vec3 cargoLocalAnchor{0.0f, 0.0f, 0.0f};
        float towTargetLength = 7.0f;
        float towMinimumLength = 2.0f;
        float towMaximumLength = 18.0f;
        float towMotorSpeed = 0.0f;
        float towMaximumForce = 120'000.0f;
        float towBreakForce = 180'000.0f;
    };

    WreckwaterLiveWorld() = default;
    ~WreckwaterLiveWorld() override = default;
    WreckwaterLiveWorld(const WreckwaterLiveWorld&) = delete;
    WreckwaterLiveWorld& operator=(const WreckwaterLiveWorld&) = delete;
    WreckwaterLiveWorld(WreckwaterLiveWorld&&) = delete;
    WreckwaterLiveWorld& operator=(WreckwaterLiveWorld&&) = delete;

    [[nodiscard]] bool initialize(
        const Config& config,
        IWreckwaterPhysicsTransactions& transactions) noexcept;
    [[nodiscard]] bool initialize(
        const Config& config, physics::PhysicsWorld& world) noexcept;

    // Mounted Player roles may alias their Skiff's exact body handle.
    // Skiff/Cargo bodies remain unique. Reverse lookup deterministically
    // returns the non-Player logical entity for an aliased handle.
    [[nodiscard]] WreckwaterBindingResult bindEntity(
        WreckwaterEntityKey entity,
        physics::BodyHandle body) noexcept;
    [[nodiscard]] bool unbindEntity(
        WreckwaterEntityKey entity) noexcept;

    // Certification is immutable and monotonic for one world epoch. Every
    // currently bound entity must occur exactly once. The 33rd accepted frame
    // evicts the oldest; exact tick lookup never substitutes a nearby frame.
    [[nodiscard]] bool certifyEvidenceFrame(
        const WreckwaterEvidenceFrameView& frame) noexcept;

    [[nodiscard]] std::optional<WreckwaterEntityKey> logicalEntity(
        physics::BodyHandle body) const noexcept;
    [[nodiscard]] std::optional<WreckwaterTowBinding> logicalTow(
        physics::AttachmentHandle attachment) const noexcept;
    [[nodiscard]] std::optional<WreckwaterTowBinding> towBinding(
        AttachmentId attachmentId,
        uint32_t attachmentGeneration) const noexcept;

    [[nodiscard]] size_t entityCount() const noexcept {
        return entityCount_;
    }
    [[nodiscard]] size_t towCount() const noexcept {
        return towCount_;
    }
    [[nodiscard]] size_t evidenceFrameCount() const noexcept {
        return evidenceCount_;
    }
    [[nodiscard]] size_t committedReceiptCount() const noexcept {
        return receiptCount_;
    }
    [[nodiscard]] const WreckwaterLiveApplyDiagnostic&
    lastApplyDiagnostic() const noexcept {
        return lastApplyDiagnostic_;
    }

    [[nodiscard]] WorldValidationResult evaluate(
        const MatchActionQuery& query) const noexcept override;
    [[nodiscard]] bool applyAtomically(
        std::span<const MatchWorldIntent> intents,
        std::span<MatchWorldIntentAck> acknowledgements)
        noexcept override;

private:
    struct EntitySlot {
        WreckwaterEntityKey entity{};
        physics::BodyHandle body{};
        bool occupied = false;
    };

    struct TowSlot {
        WreckwaterTowBinding logical{};
        physics::AttachmentHandle attachment{};
        bool occupied = false;
    };

    struct EvidenceFrame {
        uint64_t worldId = 0u;
        uint32_t worldEpoch = 0u;
        uint64_t physicsTick = 0u;
        std::array<
            WreckwaterEvidenceBody,
            kWreckwaterLiveMaximumEntities> bodies{};
        size_t bodyCount = 0u;
        bool occupied = false;
    };

    struct ReceiptKey {
        uint64_t matchId = 0u;
        uint64_t worldId = 0u;
        uint32_t worldEpoch = 0u;
        uint32_t authorityEpoch = 0u;
        uint64_t applicationTick = 0u;

        [[nodiscard]] bool operator==(
            const ReceiptKey&) const = default;
    };

    struct CommittedReceipt {
        ReceiptKey key{};
        uint64_t payloadHash = 0u;
        std::array<
            MatchWorldIntent,
            kWreckwaterMaximumIntentsPerTick> intents{};
        std::array<
            MatchWorldIntentAck,
            kWreckwaterMaximumIntentsPerTick> acknowledgements{};
        size_t intentCount = 0u;
        bool occupied = false;
    };

    struct PendingCreate {
        size_t towSlot = 0u;
        size_t intentIndex = 0u;
    };

    [[nodiscard]] static bool validEntityKey(
        WreckwaterEntityKey entity) noexcept;
    [[nodiscard]] static uint64_t intentBatchHash(
        std::span<const MatchWorldIntent> intents) noexcept;
    [[nodiscard]] static bool canonicalIntentOrder(
        std::span<const MatchWorldIntent> intents) noexcept;
    [[nodiscard]] static ReceiptKey receiptKey(
        const MatchWorldIntent& intent) noexcept;

    [[nodiscard]] size_t entityIndex(
        WreckwaterEntityKind kind, uint64_t id) const noexcept;
    [[nodiscard]] size_t exactEntityIndex(
        WreckwaterEntityKey entity) const noexcept;
    [[nodiscard]] size_t bodyIndex(
        physics::BodyHandle body) const noexcept;
    [[nodiscard]] bool compatibleBodyBinding(
        WreckwaterEntityKey entity, physics::BodyHandle body,
        size_t ignoredEntity) const noexcept;
    [[nodiscard]] size_t freeEntityIndex() const noexcept;
    [[nodiscard]] bool entityIsInUse(
        WreckwaterEntityKey entity) const noexcept;
    [[nodiscard]] size_t exactTowIndex(
        AttachmentId attachmentId, uint32_t generation) const noexcept;
    [[nodiscard]] static size_t exactTowIndex(
        const std::array<TowSlot, kWreckwaterLiveMaximumTows>& tows,
        AttachmentId attachmentId, uint32_t generation) noexcept;
    [[nodiscard]] static size_t freeTowIndex(
        const std::array<TowSlot, kWreckwaterLiveMaximumTows>& tows)
        noexcept;

    [[nodiscard]] const EvidenceFrame* evidenceFrame(
        uint64_t worldId, uint32_t worldEpoch,
        uint64_t physicsTick) const noexcept;
    [[nodiscard]] static const WreckwaterEvidenceBody* evidenceBody(
        const EvidenceFrame& frame,
        WreckwaterEntityKey entity) noexcept;
    [[nodiscard]] const EntitySlot* currentEntity(
        WreckwaterEntityKind kind, uint64_t id) const noexcept;
    [[nodiscard]] const EntitySlot* exactEntity(
        WreckwaterEntityKey entity) const noexcept;
    [[nodiscard]] WorldValidationResult validateEvidence(
        const MatchActionQuery& query,
        const EvidenceFrame& frame) const noexcept;
    [[nodiscard]] bool validateIntentEvidence(
        const MatchWorldIntent& intent,
        const EvidenceFrame& frame) const noexcept;
    [[nodiscard]] bool validIntent(
        const MatchWorldIntent& intent) const noexcept;
    [[nodiscard]] physics::DistanceAttachmentDesc attachmentDesc(
        WreckwaterEntityKey skiff,
        WreckwaterEntityKey cargo) const noexcept;

    [[nodiscard]] const CommittedReceipt* receipt(
        ReceiptKey key) const noexcept;
    void rememberReceipt(
        ReceiptKey key, uint64_t payloadHash,
        std::span<const MatchWorldIntent> intents,
        std::span<const MatchWorldIntentAck> acknowledgements) noexcept;

    Config config_{};
    std::array<EntitySlot, kWreckwaterLiveMaximumEntities> entities_{};
    std::array<TowSlot, kWreckwaterLiveMaximumTows> tows_{};
    std::array<
        EvidenceFrame, kWreckwaterCertifiedEvidenceFrameCount> evidence_{};
    std::array<
        CommittedReceipt, kWreckwaterCommittedReceiptCount> receipts_{};
    PhysicsWorldWreckwaterTransactions realTransactions_{};
    IWreckwaterPhysicsTransactions* transactions_ = nullptr;
    size_t entityCount_ = 0u;
    size_t towCount_ = 0u;
    size_t evidenceBegin_ = 0u;
    size_t evidenceCount_ = 0u;
    size_t receiptBegin_ = 0u;
    size_t receiptCount_ = 0u;
    AttachmentId nextAttachmentId_ = 1u;
    uint64_t lastCommittedApplicationTick_ = 0u;
    WreckwaterLiveApplyDiagnostic lastApplyDiagnostic_{};
    bool initialized_ = false;
};

} // namespace voxy::game
