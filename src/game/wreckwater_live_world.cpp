#include "game/wreckwater_live_world.hpp"

#include "physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace voxy::game {
namespace {

constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
constexpr uint64_t kFnvOffset64 = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnvPrime64 = 1'099'511'628'211ull;

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool validCrew(CrewId crew) noexcept {
    return crew == CrewId::CrewOne || crew == CrewId::CrewTwo;
}

[[nodiscard]] bool validCommandType(
    MatchCommandType type) noexcept {
    switch (type) {
        case MatchCommandType::TowCargo:
        case MatchCommandType::CutTow:
        case MatchCommandType::StealCargo:
        case MatchCommandType::BankCargo:
            return true;
    }
    return false;
}

[[nodiscard]] uint32_t intentPriority(
    MatchWorldIntentType type) noexcept {
    switch (type) {
        case MatchWorldIntentType::BankCargo:
            return 0u;
        case MatchWorldIntentType::CutTow:
            return 1u;
        case MatchWorldIntentType::TransferTow:
            return 2u;
        case MatchWorldIntentType::AttachTow:
            return 3u;
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] bool intentLess(
    const MatchWorldIntent& lhs,
    const MatchWorldIntent& rhs) noexcept {
    if (lhs.source != rhs.source) {
        return lhs.source
            == MatchWorldIntentSource::AuthoritativeWorldEvent;
    }
    if (lhs.source
        == MatchWorldIntentSource::AuthoritativeWorldEvent) {
        return std::tuple{
            lhs.applicationTick, lhs.physicsEvidenceTick,
            lhs.sourceSequence, intentPriority(lhs.type),
            lhs.cargoId, lhs.cargoGeneration,
            lhs.sourceSkiff, lhs.sourceSkiffGeneration,
            lhs.sourceAttachmentId,
            lhs.sourceAttachmentGeneration}
            < std::tuple{
                rhs.applicationTick, rhs.physicsEvidenceTick,
                rhs.sourceSequence, intentPriority(rhs.type),
                rhs.cargoId, rhs.cargoGeneration,
                rhs.sourceSkiff, rhs.sourceSkiffGeneration,
                rhs.sourceAttachmentId,
                rhs.sourceAttachmentGeneration};
    }
    return std::tuple{
        lhs.applicationTick, lhs.cargoId, intentPriority(lhs.type),
        lhs.playerId, lhs.sourceSequence, lhs.physicsEvidenceTick,
        lhs.connectionGeneration, lhs.connectionId,
        lhs.cargoGeneration, lhs.priorCargoRevision,
        lhs.actorSkiff, lhs.actorSkiffGeneration,
        lhs.sourceSkiff, lhs.sourceSkiffGeneration,
        lhs.targetSkiff, lhs.targetSkiffGeneration,
        lhs.sourceAttachmentId, lhs.sourceAttachmentGeneration}
        < std::tuple{
            rhs.applicationTick, rhs.cargoId, intentPriority(rhs.type),
            rhs.playerId, rhs.sourceSequence, rhs.physicsEvidenceTick,
            rhs.connectionGeneration, rhs.connectionId,
            rhs.cargoGeneration, rhs.priorCargoRevision,
            rhs.actorSkiff, rhs.actorSkiffGeneration,
            rhs.sourceSkiff, rhs.sourceSkiffGeneration,
            rhs.targetSkiff, rhs.targetSkiffGeneration,
            rhs.sourceAttachmentId, rhs.sourceAttachmentGeneration};
}

[[nodiscard]] double distanceSquared(
    const physics::WorldPosition& lhs,
    const physics::WorldPosition& rhs) noexcept {
    const glm::dvec3 delta =
        physics::worldPositionToAbsolute(lhs)
        - physics::worldPositionToAbsolute(rhs);
    return glm::dot(delta, delta);
}

void hashU32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kFnvPrime64;
    }
}

void hashU64(uint64_t& hash, uint64_t value) noexcept {
    hashU32(hash, static_cast<uint32_t>(value));
    hashU32(hash, static_cast<uint32_t>(value >> 32u));
}

[[nodiscard]] MatchWorldIntentAck acknowledgementFor(
    const MatchWorldIntent& intent) noexcept {
    MatchWorldIntentAck ack{};
    ack.schemaVersion = intent.schemaVersion;
    ack.type = intent.type;
    ack.source = intent.source;
    ack.matchId = intent.matchId;
    ack.worldId = intent.worldId;
    ack.worldEpoch = intent.worldEpoch;
    ack.authorityEpoch = intent.authorityEpoch;
    ack.applicationTick = intent.applicationTick;
    ack.physicsEvidenceTick = intent.physicsEvidenceTick;
    ack.sourceStreamId = intent.sourceStreamId;
    ack.sourceSequence = intent.sourceSequence;
    ack.playerId = intent.playerId;
    ack.connectionId = intent.connectionId;
    ack.connectionGeneration = intent.connectionGeneration;
    ack.crew = intent.crew;
    ack.actorSkiff = intent.actorSkiff;
    ack.actorSkiffGeneration = intent.actorSkiffGeneration;
    ack.cargoId = intent.cargoId;
    ack.cargoGeneration = intent.cargoGeneration;
    ack.priorCargoRevision = intent.priorCargoRevision;
    ack.resultingCargoRevision = intent.resultingCargoRevision;
    ack.sourceSkiff = intent.sourceSkiff;
    ack.sourceSkiffGeneration = intent.sourceSkiffGeneration;
    ack.targetSkiff = intent.targetSkiff;
    ack.targetSkiffGeneration = intent.targetSkiffGeneration;
    ack.sourceAttachmentId = intent.sourceAttachmentId;
    ack.sourceAttachmentGeneration =
        intent.sourceAttachmentGeneration;
    return ack;
}

} // namespace

const char* wreckwaterLiveApplyFailureName(
    WreckwaterLiveApplyFailure failure) noexcept {
    switch (failure) {
        case WreckwaterLiveApplyFailure::None:
            return "none";
        case WreckwaterLiveApplyFailure::InvalidBatch:
            return "invalid_batch";
        case WreckwaterLiveApplyFailure::ReceiptConflict:
            return "receipt_conflict";
        case WreckwaterLiveApplyFailure::NonMonotonicApplicationTick:
            return "non_monotonic_application_tick";
        case WreckwaterLiveApplyFailure::NonCanonicalIntentOrder:
            return "non_canonical_intent_order";
        case WreckwaterLiveApplyFailure::InvalidIntent:
            return "invalid_intent";
        case WreckwaterLiveApplyFailure::MissingEvidenceFrame:
            return "missing_evidence_frame";
        case WreckwaterLiveApplyFailure::EvidenceMismatch:
            return "evidence_mismatch";
        case WreckwaterLiveApplyFailure::MissingSourceTow:
            return "missing_source_tow";
        case WreckwaterLiveApplyFailure::SourceTowMismatch:
            return "source_tow_mismatch";
        case WreckwaterLiveApplyFailure::DestroyCapacityExceeded:
            return "destroy_capacity_exceeded";
        case WreckwaterLiveApplyFailure::TowCountUnderflow:
            return "tow_count_underflow";
        case WreckwaterLiveApplyFailure::CargoAlreadyTowed:
            return "cargo_already_towed";
        case WreckwaterLiveApplyFailure::TowCapacityExceeded:
            return "tow_capacity_exceeded";
        case WreckwaterLiveApplyFailure::AttachmentIdentityExhausted:
            return "attachment_identity_exhausted";
        case WreckwaterLiveApplyFailure::CreateCapacityExceeded:
            return "create_capacity_exceeded";
        case WreckwaterLiveApplyFailure::UnboundAttachmentEndpoint:
            return "unbound_attachment_endpoint";
        case WreckwaterLiveApplyFailure::PrepareRejected:
            return "prepare_rejected";
        case WreckwaterLiveApplyFailure::PreparedTargetMismatch:
            return "prepared_target_mismatch";
        case WreckwaterLiveApplyFailure::PreparedCreateCountMismatch:
            return "prepared_create_count_mismatch";
        case WreckwaterLiveApplyFailure::InvalidCreatedAttachment:
            return "invalid_created_attachment";
        case WreckwaterLiveApplyFailure::DuplicateCreatedAttachment:
            return "duplicate_created_attachment";
        case WreckwaterLiveApplyFailure::CreatedAttachmentCollision:
            return "created_attachment_collision";
        case WreckwaterLiveApplyFailure::CommitRejected:
            return "commit_rejected";
        case WreckwaterLiveApplyFailure::CommittedTargetMismatch:
            return "committed_target_mismatch";
        case WreckwaterLiveApplyFailure::CommittedCountMismatch:
            return "committed_count_mismatch";
    }
    return "unknown";
}

PhysicsWorldWreckwaterTransactions::
PhysicsWorldWreckwaterTransactions(
    physics::PhysicsWorld& world) noexcept
    : world_(&world) {}

void PhysicsWorldWreckwaterTransactions::reset(
    physics::PhysicsWorld& world) noexcept {
    world_ = &world;
}

physics::PreparedPhysicsMutation
PhysicsWorldWreckwaterTransactions::prepare(
    const physics::PhysicsMutationBatch& batch) noexcept {
    return world_ != nullptr
        ? world_->prepareMutationBatch(batch)
        : physics::PreparedPhysicsMutation{};
}

physics::PhysicsMutationResult
PhysicsWorldWreckwaterTransactions::commit(
    const physics::PreparedPhysicsMutation& prepared) noexcept {
    return world_ != nullptr
        ? world_->commitPrepared(prepared)
        : physics::PhysicsMutationResult{};
}

bool PhysicsWorldWreckwaterTransactions::discard(
    const physics::PreparedPhysicsMutation& prepared) noexcept {
    return world_ != nullptr && world_->discardPrepared(prepared);
}

bool WreckwaterLiveWorld::initialize(
    const Config& config,
    IWreckwaterPhysicsTransactions& transactions) noexcept {
    if (initialized_
        || config.matchId == 0u || config.worldId == 0u
        || config.worldEpoch == 0u || config.authorityEpoch == 0u
        || !std::isfinite(config.maximumInteractionDistance)
        || config.maximumInteractionDistance <= 0.0f
        || !std::isfinite(config.extractionRadius)
        || config.extractionRadius <= 0.0f
        || !finiteVector(config.skiffLocalAnchor)
        || !finiteVector(config.cargoLocalAnchor)
        || !std::isfinite(config.towTargetLength)
        || !std::isfinite(config.towMinimumLength)
        || !std::isfinite(config.towMaximumLength)
        || !std::isfinite(config.towMotorSpeed)
        || !std::isfinite(config.towMaximumForce)
        || !std::isfinite(config.towBreakForce)
        || config.towMinimumLength < 0.0f
        || config.towTargetLength < config.towMinimumLength
        || config.towMaximumLength < config.towTargetLength
        || config.towMaximumForce < 0.0f
        || config.towBreakForce < 0.0f) {
        return false;
    }
    for (const physics::WorldPosition& center
         : config.extractionCenters) {
        if (!physics::isValidWorldPosition(center)) return false;
    }

    config_ = config;
    transactions_ = &transactions;
    entities_ = {};
    tows_ = {};
    evidence_ = {};
    receipts_ = {};
    entityCount_ = 0u;
    towCount_ = 0u;
    evidenceBegin_ = 0u;
    evidenceCount_ = 0u;
    receiptBegin_ = 0u;
    receiptCount_ = 0u;
    nextAttachmentId_ = 1u;
    lastCommittedApplicationTick_ = 0u;
    lastApplyDiagnostic_ = {};
    initialized_ = true;
    return true;
}

bool WreckwaterLiveWorld::initialize(
    const Config& config, physics::PhysicsWorld& world) noexcept {
    const physics::BackendCapabilities capabilities =
        world.capabilities();
    if (!world.isInitialized()
        || world.backendType() != physics::BackendType::WebGpuSoft
        || !capabilities.gpuResidentState
        || !capabilities.distanceAttachments
        || !capabilities.atomicMutationBatches) {
        return false;
    }
    realTransactions_.reset(world);
    return initialize(config, realTransactions_);
}

bool WreckwaterLiveWorld::validEntityKey(
    WreckwaterEntityKey entity) noexcept {
    if (entity.id == 0u || entity.generation == 0u) return false;
    switch (entity.kind) {
        case WreckwaterEntityKind::Player:
            return true;
        case WreckwaterEntityKind::Skiff:
        case WreckwaterEntityKind::Cargo:
            return entity.id
                <= std::numeric_limits<uint32_t>::max();
    }
    return false;
}

size_t WreckwaterLiveWorld::entityIndex(
    WreckwaterEntityKind kind, uint64_t id) const noexcept {
    for (size_t index = 0u; index < entities_.size(); ++index) {
        if (entities_[index].occupied
            && entities_[index].entity.kind == kind
            && entities_[index].entity.id == id) {
            return index;
        }
    }
    return kInvalidIndex;
}

size_t WreckwaterLiveWorld::exactEntityIndex(
    WreckwaterEntityKey entity) const noexcept {
    const size_t index = entityIndex(entity.kind, entity.id);
    return index != kInvalidIndex
            && entities_[index].entity.generation == entity.generation
        ? index : kInvalidIndex;
}

size_t WreckwaterLiveWorld::bodyIndex(
    physics::BodyHandle body) const noexcept {
    // Physics events should resolve to the simulated object, never an
    // arbitrary mounted crew role that aliases its body.
    for (size_t index = 0u; index < entities_.size(); ++index) {
        if (entities_[index].occupied
            && entities_[index].body == body
            && entities_[index].entity.kind
                != WreckwaterEntityKind::Player) {
            return index;
        }
    }
    for (size_t index = 0u; index < entities_.size(); ++index) {
        if (entities_[index].occupied
            && entities_[index].body == body) {
            return index;
        }
    }
    return kInvalidIndex;
}

bool WreckwaterLiveWorld::compatibleBodyBinding(
    WreckwaterEntityKey entity, physics::BodyHandle body,
    size_t ignoredEntity) const noexcept {
    for (size_t index = 0u; index < entities_.size(); ++index) {
        if (index == ignoredEntity || !entities_[index].occupied
            || entities_[index].body.index != body.index) {
            continue;
        }
        const EntitySlot& existing = entities_[index];
        if (existing.body != body) return false;
        if (entity.kind == WreckwaterEntityKind::Player) {
            if (existing.entity.kind == WreckwaterEntityKind::Cargo) {
                return false;
            }
        } else if (entity.kind == WreckwaterEntityKind::Skiff) {
            if (existing.entity.kind != WreckwaterEntityKind::Player) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

size_t WreckwaterLiveWorld::freeEntityIndex() const noexcept {
    for (size_t index = 0u; index < entities_.size(); ++index) {
        if (!entities_[index].occupied) return index;
    }
    return kInvalidIndex;
}

bool WreckwaterLiveWorld::entityIsInUse(
    WreckwaterEntityKey entity) const noexcept {
    for (const TowSlot& slot : tows_) {
        if (!slot.occupied) continue;
        if ((entity.kind == WreckwaterEntityKind::Cargo
                && slot.logical.cargoId == entity.id
                && slot.logical.cargoGeneration
                    == entity.generation)
            || (entity.kind == WreckwaterEntityKind::Skiff
                && slot.logical.skiffId == entity.id
                && slot.logical.skiffGeneration
                    == entity.generation)) {
            return true;
        }
    }
    return false;
}

WreckwaterBindingResult WreckwaterLiveWorld::bindEntity(
    WreckwaterEntityKey entity,
    physics::BodyHandle body) noexcept {
    if (!initialized_) {
        return WreckwaterBindingResult::NotInitialized;
    }
    if (!validEntityKey(entity)
        || !body.valid() || body.generation == 0u) {
        return WreckwaterBindingResult::Invalid;
    }

    const size_t logicalIndex =
        entityIndex(entity.kind, entity.id);
    if (logicalIndex != kInvalidIndex) {
        EntitySlot& current = entities_[logicalIndex];
        if (entity.generation < current.entity.generation) {
            return WreckwaterBindingResult::StaleGeneration;
        }
        if (entity.generation == current.entity.generation) {
            return current.body == body
                ? WreckwaterBindingResult::AlreadyBound
                : WreckwaterBindingResult::DuplicateLogicalEntity;
        }
        if (entityIsInUse(current.entity)) {
            return WreckwaterBindingResult::EntityInUse;
        }
        if (!compatibleBodyBinding(entity, body, logicalIndex)) {
            return WreckwaterBindingResult::DuplicatePhysicsHandle;
        }
        current.entity = entity;
        current.body = body;
        return WreckwaterBindingResult::Rebound;
    }

    if (!compatibleBodyBinding(entity, body, kInvalidIndex)) {
        return WreckwaterBindingResult::DuplicatePhysicsHandle;
    }
    const size_t freeIndex = freeEntityIndex();
    if (freeIndex == kInvalidIndex) {
        return WreckwaterBindingResult::CapacityExceeded;
    }
    entities_[freeIndex] = {
        .entity = entity,
        .body = body,
        .occupied = true,
    };
    ++entityCount_;
    return WreckwaterBindingResult::Bound;
}

bool WreckwaterLiveWorld::unbindEntity(
    WreckwaterEntityKey entity) noexcept {
    if (!initialized_) return false;
    const size_t index = exactEntityIndex(entity);
    if (index == kInvalidIndex || entityIsInUse(entity)) return false;
    entities_[index] = {};
    if (entityCount_ != 0u) --entityCount_;
    return true;
}

std::optional<WreckwaterEntityKey>
WreckwaterLiveWorld::logicalEntity(
    physics::BodyHandle body) const noexcept {
    const size_t index = bodyIndex(body);
    if (index == kInvalidIndex) return std::nullopt;
    return entities_[index].entity;
}

size_t WreckwaterLiveWorld::exactTowIndex(
    AttachmentId attachmentId, uint32_t generation) const noexcept {
    return exactTowIndex(tows_, attachmentId, generation);
}

size_t WreckwaterLiveWorld::exactTowIndex(
    const std::array<TowSlot, kWreckwaterLiveMaximumTows>& tows,
    AttachmentId attachmentId, uint32_t generation) noexcept {
    for (size_t index = 0u; index < tows.size(); ++index) {
        if (tows[index].occupied
            && tows[index].logical.attachmentId == attachmentId
            && tows[index].logical.attachmentGeneration
                == generation) {
            return index;
        }
    }
    return kInvalidIndex;
}

size_t WreckwaterLiveWorld::freeTowIndex(
    const std::array<TowSlot, kWreckwaterLiveMaximumTows>& tows)
    noexcept {
    for (size_t index = 0u; index < tows.size(); ++index) {
        if (!tows[index].occupied) return index;
    }
    return kInvalidIndex;
}

std::optional<WreckwaterTowBinding>
WreckwaterLiveWorld::logicalTow(
    physics::AttachmentHandle attachment) const noexcept {
    if (!attachment.valid()) return std::nullopt;
    for (const TowSlot& slot : tows_) {
        if (slot.occupied && slot.attachment == attachment) {
            return slot.logical;
        }
    }
    return std::nullopt;
}

std::optional<WreckwaterTowBinding>
WreckwaterLiveWorld::towBinding(
    AttachmentId attachmentId,
    uint32_t attachmentGeneration) const noexcept {
    const size_t index = exactTowIndex(
        attachmentId, attachmentGeneration);
    if (index == kInvalidIndex) return std::nullopt;
    return tows_[index].logical;
}

bool WreckwaterLiveWorld::certifyEvidenceFrame(
    const WreckwaterEvidenceFrameView& frame) noexcept {
    if (!initialized_
        || frame.worldId != config_.worldId
        || frame.worldEpoch != config_.worldEpoch
        || frame.physicsTick == 0u
        || frame.bodies.size() != entityCount_
        || frame.bodies.size()
            > kWreckwaterLiveMaximumEntities) {
        return false;
    }
    if (evidenceCount_ != 0u) {
        const size_t newest =
            (evidenceBegin_ + evidenceCount_ - 1u)
            % evidence_.size();
        if (frame.physicsTick <= evidence_[newest].physicsTick) {
            return false;
        }
    }

    std::array<bool, kWreckwaterLiveMaximumEntities> seen{};
    std::array<
        const WreckwaterEvidenceBody*,
        kWreckwaterLiveMaximumEntities> samples{};
    for (const WreckwaterEvidenceBody& body : frame.bodies) {
        if (!validEntityKey(body.entity)
            || !physics::isValidWorldPosition(body.position)) {
            return false;
        }
        const size_t entity = exactEntityIndex(body.entity);
        if (entity == kInvalidIndex || seen[entity]) return false;
        seen[entity] = true;
        samples[entity] = &body;
    }
    for (size_t index = 0u; index < entities_.size(); ++index) {
        if (entities_[index].occupied && !seen[index]) return false;
    }
    for (size_t lhs = 0u; lhs < entities_.size(); ++lhs) {
        if (!entities_[lhs].occupied) continue;
        for (size_t rhs = lhs + 1u; rhs < entities_.size(); ++rhs) {
            if (!entities_[rhs].occupied
                || entities_[lhs].body != entities_[rhs].body) {
                continue;
            }
            if (samples[lhs]->position != samples[rhs]->position
                || samples[lhs]->available
                    != samples[rhs]->available) {
                return false;
            }
        }
    }

    size_t destination = 0u;
    if (evidenceCount_ < evidence_.size()) {
        destination =
            (evidenceBegin_ + evidenceCount_) % evidence_.size();
        ++evidenceCount_;
    } else {
        destination = evidenceBegin_;
        evidenceBegin_ =
            (evidenceBegin_ + 1u) % evidence_.size();
    }
    EvidenceFrame& stored = evidence_[destination];
    stored = {};
    stored.worldId = frame.worldId;
    stored.worldEpoch = frame.worldEpoch;
    stored.physicsTick = frame.physicsTick;
    stored.bodyCount = frame.bodies.size();
    stored.occupied = true;
    std::copy(
        frame.bodies.begin(), frame.bodies.end(),
        stored.bodies.begin());
    return true;
}

const WreckwaterLiveWorld::EvidenceFrame*
WreckwaterLiveWorld::evidenceFrame(
    uint64_t worldId, uint32_t worldEpoch,
    uint64_t physicsTick) const noexcept {
    for (size_t offset = 0u; offset < evidenceCount_; ++offset) {
        const EvidenceFrame& frame =
            evidence_[(evidenceBegin_ + offset) % evidence_.size()];
        if (frame.occupied && frame.worldId == worldId
            && frame.worldEpoch == worldEpoch
            && frame.physicsTick == physicsTick) {
            return &frame;
        }
    }
    return nullptr;
}

const WreckwaterEvidenceBody*
WreckwaterLiveWorld::evidenceBody(
    const EvidenceFrame& frame,
    WreckwaterEntityKey entity) noexcept {
    for (size_t index = 0u; index < frame.bodyCount; ++index) {
        if (frame.bodies[index].entity == entity) {
            return &frame.bodies[index];
        }
    }
    return nullptr;
}

const WreckwaterLiveWorld::EntitySlot*
WreckwaterLiveWorld::currentEntity(
    WreckwaterEntityKind kind, uint64_t id) const noexcept {
    const size_t index = entityIndex(kind, id);
    return index == kInvalidIndex ? nullptr : &entities_[index];
}

const WreckwaterLiveWorld::EntitySlot*
WreckwaterLiveWorld::exactEntity(
    WreckwaterEntityKey entity) const noexcept {
    const size_t index = exactEntityIndex(entity);
    return index == kInvalidIndex ? nullptr : &entities_[index];
}

WorldValidationResult WreckwaterLiveWorld::validateEvidence(
    const MatchActionQuery& query,
    const EvidenceFrame& frame) const noexcept {
    const EntitySlot* actor = currentEntity(
        WreckwaterEntityKind::Player, query.command.playerId);
    if (actor == nullptr) {
        return WorldValidationResult::ActorUnavailable;
    }
    const WreckwaterEntityKey skiffKey{
        WreckwaterEntityKind::Skiff,
        query.actorSkiff.skiffId,
        query.actorSkiff.generation,
    };
    const EntitySlot* skiff = exactEntity(skiffKey);
    if (skiff == nullptr) {
        return WorldValidationResult::SkiffUnavailable;
    }
    if (actor->body != skiff->body) {
        return WorldValidationResult::ActorUnavailable;
    }
    const WreckwaterEntityKey cargoKey{
        WreckwaterEntityKind::Cargo,
        query.cargo.cargoId,
        query.cargo.generation,
    };
    const EntitySlot* cargo = exactEntity(cargoKey);
    if (cargo == nullptr) return WorldValidationResult::Rejected;

    const WreckwaterEvidenceBody* actorBody =
        evidenceBody(frame, actor->entity);
    const WreckwaterEvidenceBody* skiffBody =
        evidenceBody(frame, skiff->entity);
    const WreckwaterEvidenceBody* cargoBody =
        evidenceBody(frame, cargo->entity);
    if (actorBody == nullptr || !actorBody->available) {
        return WorldValidationResult::ActorUnavailable;
    }
    if (skiffBody == nullptr || !skiffBody->available) {
        return WorldValidationResult::SkiffUnavailable;
    }
    if (cargoBody == nullptr || !cargoBody->available) {
        return WorldValidationResult::Rejected;
    }

    if (query.command.type == MatchCommandType::TowCargo) {
        if (query.cargo.disposition != CargoDisposition::Free) {
            return WorldValidationResult::Rejected;
        }
        for (const TowSlot& tow : tows_) {
            if (tow.occupied
                && tow.logical.cargoId == query.cargo.cargoId
                && tow.logical.cargoGeneration
                    == query.cargo.generation) {
                return WorldValidationResult::Rejected;
            }
        }
    } else {
        if (query.cargo.disposition != CargoDisposition::Towed
            || query.cargo.towingSkiff == 0u
            || query.cargo.towingSkiffGeneration == 0u
            || query.cargo.towAttachmentId == 0u
            || query.cargo.towAttachmentGeneration == 0u) {
            return WorldValidationResult::Rejected;
        }
        const size_t towIndex = exactTowIndex(
            query.cargo.towAttachmentId,
            query.cargo.towAttachmentGeneration);
        if (towIndex == kInvalidIndex) {
            return WorldValidationResult::Rejected;
        }
        const TowSlot& tow = tows_[towIndex];
        if (tow.logical.cargoId != query.cargo.cargoId
            || tow.logical.cargoGeneration
                != query.cargo.generation
            || tow.logical.skiffId != query.cargo.towingSkiff
            || tow.logical.skiffGeneration
                != query.cargo.towingSkiffGeneration) {
            return WorldValidationResult::Rejected;
        }
        const EntitySlot* sourceSkiff = exactEntity({
            WreckwaterEntityKind::Skiff,
            query.cargo.towingSkiff,
            query.cargo.towingSkiffGeneration,
        });
        if (sourceSkiff == nullptr) {
            return WorldValidationResult::SkiffUnavailable;
        }
        const WreckwaterEvidenceBody* sourceSkiffBody =
            evidenceBody(frame, sourceSkiff->entity);
        if (sourceSkiffBody == nullptr
            || !sourceSkiffBody->available) {
            return WorldValidationResult::SkiffUnavailable;
        }
    }

    const double maximumDistance =
        static_cast<double>(config_.maximumInteractionDistance);
    if (distanceSquared(skiffBody->position, cargoBody->position)
        > maximumDistance * maximumDistance) {
        return WorldValidationResult::OutOfRange;
    }
    if (query.command.type == MatchCommandType::BankCargo) {
        const size_t crewIndex =
            query.actorCrew == CrewId::CrewOne ? 0u : 1u;
        const double radius =
            static_cast<double>(config_.extractionRadius);
        if (distanceSquared(
                cargoBody->position,
                config_.extractionCenters[crewIndex])
            > radius * radius) {
            return WorldValidationResult::OutsideExtraction;
        }
    }
    return WorldValidationResult::Allowed;
}

WorldValidationResult WreckwaterLiveWorld::evaluate(
    const MatchActionQuery& query) const noexcept {
    if (!initialized_
        || query.command.schemaVersion
            != kWreckwaterMatchSchemaVersion
        || !validCommandType(query.command.type)
        || query.command.matchId != config_.matchId
        || query.command.worldId != config_.worldId
        || query.command.worldEpoch != config_.worldEpoch
        || query.command.authorityEpoch != config_.authorityEpoch
        || query.applicationTick == 0u
        || query.physicsEvidenceTick == 0u
        || query.physicsEvidenceTick
            != query.command.physicsEvidenceTick
        || query.command.tick != query.applicationTick
        || query.command.playerId == 0u
        || !validCrew(query.actorCrew)
        || query.actorSkiff.owner != query.actorCrew
        || query.actorSkiff.skiffId != query.command.skiffId
        || query.actorSkiff.generation
            != query.command.skiffGeneration
        || query.actorSkiff.disposition
            != SkiffDisposition::Active
        || query.cargo.cargoId != query.command.cargoId
        || query.cargo.generation
            != query.command.cargoGeneration
        || query.cargo.revision
            != query.command.observedCargoRevision) {
        return WorldValidationResult::Rejected;
    }
    const EvidenceFrame* frame = evidenceFrame(
        query.command.worldId, query.command.worldEpoch,
        query.physicsEvidenceTick);
    if (frame == nullptr) {
        return WorldValidationResult::PhysicsEvidenceUnavailable;
    }
    return validateEvidence(query, *frame);
}

bool WreckwaterLiveWorld::validIntent(
    const MatchWorldIntent& intent) const noexcept {
    if (intent.schemaVersion != kWreckwaterMatchSchemaVersion
        || intent.matchId != config_.matchId
        || intent.worldId != config_.worldId
        || intent.worldEpoch != config_.worldEpoch
        || intent.authorityEpoch != config_.authorityEpoch
        || intent.applicationTick == 0u
        || intent.physicsEvidenceTick == 0u
        || intent.physicsEvidenceTick > intent.applicationTick
        || intent.sourceSequence == 0u
        || !validCrew(intent.crew)
        || intent.cargoId == 0u
        || intent.cargoGeneration == 0u
        || intent.priorCargoRevision == 0u
        || intent.priorCargoRevision
            == std::numeric_limits<uint32_t>::max()
        || intent.resultingCargoRevision
            != intent.priorCargoRevision + 1u
        || intent.resultingAttachmentId != 0u
        || intent.resultingAttachmentGeneration != 0u) {
        return false;
    }
    switch (intent.source) {
        case MatchWorldIntentSource::PlayerCommand:
            if (intent.sourceStreamId != 0u
                || intent.playerId == 0u
                || intent.connectionId == 0u
                || intent.connectionGeneration == 0u
                || intent.actorSkiff == 0u
                || intent.actorSkiffGeneration == 0u) {
                return false;
            }
            break;
        case MatchWorldIntentSource::AuthoritativeWorldEvent:
            if (intent.type != MatchWorldIntentType::CutTow
                || intent.sourceStreamId == 0u
                || intent.playerId != 0u
                || intent.connectionId != 0u
                || intent.connectionGeneration != 0u
                || intent.actorSkiff != 0u
                || intent.actorSkiffGeneration != 0u) {
                return false;
            }
            break;
        default:
            return false;
    }

    switch (intent.type) {
        case MatchWorldIntentType::AttachTow:
            return intent.sourceSkiff == 0u
                && intent.sourceSkiffGeneration == 0u
                && intent.sourceAttachmentId == 0u
                && intent.sourceAttachmentGeneration == 0u
                && intent.targetSkiff != 0u
                && intent.targetSkiffGeneration != 0u
                && intent.actorSkiff == intent.targetSkiff
                && intent.actorSkiffGeneration
                    == intent.targetSkiffGeneration;
        case MatchWorldIntentType::CutTow:
            return intent.sourceSkiff != 0u
                && intent.sourceSkiffGeneration != 0u
                && intent.sourceAttachmentId != 0u
                && intent.sourceAttachmentGeneration != 0u
                && intent.targetSkiff == 0u
                && intent.targetSkiffGeneration == 0u;
        case MatchWorldIntentType::BankCargo:
            return intent.sourceSkiff != 0u
                && intent.sourceSkiffGeneration != 0u
                && intent.sourceAttachmentId != 0u
                && intent.sourceAttachmentGeneration != 0u
                && intent.targetSkiff == 0u
                && intent.targetSkiffGeneration == 0u
                && intent.actorSkiff == intent.sourceSkiff
                && intent.actorSkiffGeneration
                    == intent.sourceSkiffGeneration;
        case MatchWorldIntentType::TransferTow:
            return intent.sourceSkiff != 0u
                && intent.sourceSkiffGeneration != 0u
                && intent.sourceAttachmentId != 0u
                && intent.sourceAttachmentGeneration != 0u
                && intent.targetSkiff != 0u
                && intent.targetSkiffGeneration != 0u
                && intent.actorSkiff == intent.targetSkiff
                && intent.actorSkiffGeneration
                    == intent.targetSkiffGeneration
                && (intent.sourceSkiff != intent.targetSkiff
                    || intent.sourceSkiffGeneration
                        != intent.targetSkiffGeneration);
    }
    return false;
}

bool WreckwaterLiveWorld::validateIntentEvidence(
    const MatchWorldIntent& intent,
    const EvidenceFrame& frame) const noexcept {
    const WreckwaterEntityKey cargoKey{
        WreckwaterEntityKind::Cargo,
        intent.cargoId,
        intent.cargoGeneration,
    };
    const EntitySlot* cargo = exactEntity(cargoKey);
    if (cargo == nullptr) return false;
    const WreckwaterEvidenceBody* cargoBody =
        evidenceBody(frame, cargo->entity);
    if (cargoBody == nullptr) return false;

    // Loss/break/sink cleanup is identity retirement, not a new physical
    // interaction. The exact certified frame and exact logical identities
    // must exist, but a body may correctly be unavailable in the frame that
    // caused the authoritative event.
    if (intent.source
        == MatchWorldIntentSource::AuthoritativeWorldEvent) {
        const EntitySlot* sourceSkiff = exactEntity({
            WreckwaterEntityKind::Skiff,
            intent.sourceSkiff,
            intent.sourceSkiffGeneration,
        });
        return sourceSkiff != nullptr
            && evidenceBody(frame, sourceSkiff->entity) != nullptr;
    }
    if (!cargoBody->available) return false;

    const EntitySlot* skiff = exactEntity({
        WreckwaterEntityKind::Skiff,
        intent.actorSkiff,
        intent.actorSkiffGeneration,
    });
    if (skiff == nullptr) return false;
    const WreckwaterEvidenceBody* skiffBody =
        evidenceBody(frame, skiff->entity);
    if (skiffBody == nullptr || !skiffBody->available) return false;

    if (intent.type == MatchWorldIntentType::TransferTow) {
        const EntitySlot* sourceSkiff = exactEntity({
            WreckwaterEntityKind::Skiff,
            intent.sourceSkiff,
            intent.sourceSkiffGeneration,
        });
        if (sourceSkiff == nullptr) return false;
        const WreckwaterEvidenceBody* sourceBody =
            evidenceBody(frame, sourceSkiff->entity);
        if (sourceBody == nullptr || !sourceBody->available) {
            return false;
        }
    }

    if (intent.source
        == MatchWorldIntentSource::PlayerCommand) {
        const EntitySlot* actor = currentEntity(
            WreckwaterEntityKind::Player, intent.playerId);
        if (actor == nullptr || actor->body != skiff->body) {
            return false;
        }
        const WreckwaterEvidenceBody* actorBody =
            evidenceBody(frame, actor->entity);
        if (actorBody == nullptr || !actorBody->available) {
            return false;
        }
        const double maximumDistance =
            static_cast<double>(config_.maximumInteractionDistance);
        if (distanceSquared(
                skiffBody->position, cargoBody->position)
            > maximumDistance * maximumDistance) {
            return false;
        }
    }

    if (intent.type == MatchWorldIntentType::BankCargo) {
        const size_t crewIndex =
            intent.crew == CrewId::CrewOne ? 0u : 1u;
        const double radius =
            static_cast<double>(config_.extractionRadius);
        if (distanceSquared(
                cargoBody->position,
                config_.extractionCenters[crewIndex])
            > radius * radius) {
            return false;
        }
    }
    return true;
}

physics::DistanceAttachmentDesc
WreckwaterLiveWorld::attachmentDesc(
    WreckwaterEntityKey skiff,
    WreckwaterEntityKey cargo) const noexcept {
    physics::DistanceAttachmentDesc desc{};
    const EntitySlot* skiffSlot = exactEntity(skiff);
    const EntitySlot* cargoSlot = exactEntity(cargo);
    if (skiffSlot == nullptr || cargoSlot == nullptr) return desc;
    desc.bodyA = skiffSlot->body;
    desc.bodyB = cargoSlot->body;
    desc.localAnchorA = config_.skiffLocalAnchor;
    desc.localAnchorB = config_.cargoLocalAnchor;
    desc.targetLength = config_.towTargetLength;
    desc.minimumLength = config_.towMinimumLength;
    desc.maximumLength = config_.towMaximumLength;
    desc.motorSpeed = config_.towMotorSpeed;
    desc.maximumForce = config_.towMaximumForce;
    desc.breakForce = config_.towBreakForce;
    return desc;
}

uint64_t WreckwaterLiveWorld::intentBatchHash(
    std::span<const MatchWorldIntent> intents) noexcept {
    uint64_t hash = kFnvOffset64;
    hashU64(hash, intents.size());
    for (const MatchWorldIntent& intent : intents) {
        hashU32(hash, intent.schemaVersion);
        hashU32(hash, static_cast<uint32_t>(intent.type));
        hashU32(hash, static_cast<uint32_t>(intent.source));
        hashU64(hash, intent.matchId);
        hashU64(hash, intent.worldId);
        hashU32(hash, intent.worldEpoch);
        hashU32(hash, intent.authorityEpoch);
        hashU64(hash, intent.applicationTick);
        hashU64(hash, intent.physicsEvidenceTick);
        hashU32(hash, intent.sourceStreamId);
        hashU64(hash, intent.sourceSequence);
        hashU64(hash, intent.playerId);
        hashU64(hash, intent.connectionId);
        hashU32(hash, intent.connectionGeneration);
        hashU32(hash, static_cast<uint32_t>(intent.crew));
        hashU32(hash, intent.actorSkiff);
        hashU32(hash, intent.actorSkiffGeneration);
        hashU32(hash, intent.cargoId);
        hashU32(hash, intent.cargoGeneration);
        hashU32(hash, intent.priorCargoRevision);
        hashU32(hash, intent.resultingCargoRevision);
        hashU32(hash, intent.sourceSkiff);
        hashU32(hash, intent.sourceSkiffGeneration);
        hashU32(hash, intent.targetSkiff);
        hashU32(hash, intent.targetSkiffGeneration);
        hashU64(hash, intent.sourceAttachmentId);
        hashU32(hash, intent.sourceAttachmentGeneration);
        hashU64(hash, intent.resultingAttachmentId);
        hashU32(hash, intent.resultingAttachmentGeneration);
    }
    return hash;
}

bool WreckwaterLiveWorld::canonicalIntentOrder(
    std::span<const MatchWorldIntent> intents) noexcept {
    for (size_t index = 1u; index < intents.size(); ++index) {
        if (intentLess(intents[index], intents[index - 1u])) {
            return false;
        }
    }
    for (size_t lhs = 0u; lhs < intents.size(); ++lhs) {
        for (size_t rhs = lhs + 1u; rhs < intents.size(); ++rhs) {
            const MatchWorldIntent& a = intents[lhs];
            const MatchWorldIntent& b = intents[rhs];
            if (a.source == b.source
                && a.sourceStreamId == b.sourceStreamId
                && a.sourceSequence == b.sourceSequence
                && a.playerId == b.playerId) {
                return false;
            }
        }
    }
    return true;
}

WreckwaterLiveWorld::ReceiptKey
WreckwaterLiveWorld::receiptKey(
    const MatchWorldIntent& intent) noexcept {
    return {
        .matchId = intent.matchId,
        .worldId = intent.worldId,
        .worldEpoch = intent.worldEpoch,
        .authorityEpoch = intent.authorityEpoch,
        .applicationTick = intent.applicationTick,
    };
}

const WreckwaterLiveWorld::CommittedReceipt*
WreckwaterLiveWorld::receipt(ReceiptKey key) const noexcept {
    for (size_t offset = 0u; offset < receiptCount_; ++offset) {
        const CommittedReceipt& candidate =
            receipts_[(receiptBegin_ + offset) % receipts_.size()];
        if (candidate.occupied && candidate.key == key) {
            return &candidate;
        }
    }
    return nullptr;
}

void WreckwaterLiveWorld::rememberReceipt(
    ReceiptKey key, uint64_t payloadHash,
    std::span<const MatchWorldIntent> intents,
    std::span<const MatchWorldIntentAck> acknowledgements) noexcept {
    size_t destination = 0u;
    if (receiptCount_ < receipts_.size()) {
        destination =
            (receiptBegin_ + receiptCount_) % receipts_.size();
        ++receiptCount_;
    } else {
        destination = receiptBegin_;
        receiptBegin_ =
            (receiptBegin_ + 1u) % receipts_.size();
    }
    CommittedReceipt& stored = receipts_[destination];
    stored = {};
    stored.key = key;
    stored.payloadHash = payloadHash;
    stored.intentCount = intents.size();
    stored.occupied = true;
    std::copy(intents.begin(), intents.end(), stored.intents.begin());
    std::memcpy(
        stored.acknowledgements.data(), acknowledgements.data(),
        acknowledgements.size() * sizeof(MatchWorldIntentAck));
}

bool WreckwaterLiveWorld::applyAtomically(
    std::span<const MatchWorldIntent> intents,
    std::span<MatchWorldIntentAck> acknowledgements) noexcept {
    lastApplyDiagnostic_ = {};
    lastApplyDiagnostic_.intentCount = static_cast<uint32_t>(
        std::min(
            intents.size(),
            static_cast<size_t>(
                std::numeric_limits<uint32_t>::max())));
    if (!intents.empty()) {
        lastApplyDiagnostic_.applicationTick =
            intents.front().applicationTick;
        lastApplyDiagnostic_.physicsEvidenceTick =
            intents.front().physicsEvidenceTick;
    }
    const auto fail =
        [this](
            WreckwaterLiveApplyFailure failure,
            size_t intentIndex = kInvalidIndex) noexcept {
            lastApplyDiagnostic_.failure = failure;
            if (intentIndex
                <= std::numeric_limits<uint32_t>::max()) {
                lastApplyDiagnostic_.intentIndex =
                    static_cast<uint32_t>(intentIndex);
            }
            return false;
        };
    if (!initialized_ || transactions_ == nullptr
        || intents.empty()
        || intents.size() > kWreckwaterMaximumIntentsPerTick
        || acknowledgements.size() != intents.size()) {
        return fail(WreckwaterLiveApplyFailure::InvalidBatch);
    }

    const ReceiptKey key = receiptKey(intents.front());
    const uint64_t payloadHash = intentBatchHash(intents);
    if (const CommittedReceipt* prior = receipt(key);
        prior != nullptr) {
        bool samePayload = prior->payloadHash == payloadHash
            && prior->intentCount == intents.size();
        for (size_t index = 0u;
             samePayload && index < intents.size(); ++index) {
            samePayload =
                prior->intents[index] == intents[index];
        }
        if (!samePayload) {
            return fail(
                WreckwaterLiveApplyFailure::ReceiptConflict);
        }
        std::memcpy(
            acknowledgements.data(),
            prior->acknowledgements.data(),
            intents.size() * sizeof(MatchWorldIntentAck));
        return true;
    }
    if (key.applicationTick <= lastCommittedApplicationTick_) {
        return fail(
            WreckwaterLiveApplyFailure::
                NonMonotonicApplicationTick);
    }
    if (!canonicalIntentOrder(intents)) {
        return fail(
            WreckwaterLiveApplyFailure::NonCanonicalIntentOrder);
    }

    std::array<
        MatchWorldIntentAck,
        kWreckwaterMaximumIntentsPerTick> tentativeAcks{};
    std::array<
        physics::AttachmentHandle,
        kWreckwaterMaximumIntentsPerTick> destroys{};
    std::array<
        physics::DistanceAttachmentDesc,
        kWreckwaterMaximumIntentsPerTick> creates{};
    std::array<
        PendingCreate,
        kWreckwaterMaximumIntentsPerTick> pendingCreates{};
    auto candidateTows = tows_;
    size_t candidateTowCount = towCount_;
    AttachmentId candidateNextAttachmentId = nextAttachmentId_;
    size_t destroyCount = 0u;
    size_t createCount = 0u;
    physics::PreparedPhysicsMutation prepared{};
    bool preparedReturned = false;
    const auto discardPrepared =
        [this, &prepared, &preparedReturned]() noexcept {
            if (!preparedReturned) return;
            (void)transactions_->discard(prepared);
            preparedReturned = false;
        };

    for (size_t index = 0u; index < intents.size(); ++index) {
        const MatchWorldIntent& intent = intents[index];
        if (!validIntent(intent)
            || receiptKey(intent) != key) {
            return fail(
                WreckwaterLiveApplyFailure::InvalidIntent, index);
        }
        const EvidenceFrame* frame = evidenceFrame(
            intent.worldId, intent.worldEpoch,
            intent.physicsEvidenceTick);
        if (frame == nullptr) {
            return fail(
                WreckwaterLiveApplyFailure::MissingEvidenceFrame,
                index);
        }
        if (!validateIntentEvidence(intent, *frame)) {
            return fail(
                WreckwaterLiveApplyFailure::EvidenceMismatch,
                index);
        }

        MatchWorldIntentAck ack = acknowledgementFor(intent);
        const bool destroysTow =
            intent.type == MatchWorldIntentType::CutTow
            || intent.type == MatchWorldIntentType::TransferTow
            || intent.type == MatchWorldIntentType::BankCargo;
        if (destroysTow) {
            const size_t sourceIndex = exactTowIndex(
                candidateTows, intent.sourceAttachmentId,
                intent.sourceAttachmentGeneration);
            if (sourceIndex == kInvalidIndex) {
                return fail(
                    WreckwaterLiveApplyFailure::MissingSourceTow,
                    index);
            }
            const TowSlot& source = candidateTows[sourceIndex];
            if (source.logical.cargoId != intent.cargoId
                || source.logical.cargoGeneration
                    != intent.cargoGeneration
                || source.logical.skiffId != intent.sourceSkiff
                || source.logical.skiffGeneration
                    != intent.sourceSkiffGeneration) {
                return fail(
                    WreckwaterLiveApplyFailure::SourceTowMismatch,
                    index);
            }
            if (destroyCount >= destroys.size()) {
                return fail(
                    WreckwaterLiveApplyFailure::
                        DestroyCapacityExceeded,
                    index);
            }
            destroys[destroyCount++] = source.attachment;
            candidateTows[sourceIndex] = {};
            if (candidateTowCount == 0u) {
                return fail(
                    WreckwaterLiveApplyFailure::TowCountUnderflow,
                    index);
            }
            --candidateTowCount;
        }

        const bool createsTow =
            intent.type == MatchWorldIntentType::AttachTow
            || intent.type == MatchWorldIntentType::TransferTow;
        if (createsTow) {
            for (const TowSlot& slot : candidateTows) {
                if (slot.occupied
                    && slot.logical.cargoId == intent.cargoId
                    && slot.logical.cargoGeneration
                        == intent.cargoGeneration) {
                    return fail(
                        WreckwaterLiveApplyFailure::
                            CargoAlreadyTowed,
                        index);
                }
            }
            const size_t destination =
                freeTowIndex(candidateTows);
            if (destination == kInvalidIndex) {
                return fail(
                    WreckwaterLiveApplyFailure::TowCapacityExceeded,
                    index);
            }
            if (candidateNextAttachmentId == 0u
                || candidateNextAttachmentId
                    == std::numeric_limits<AttachmentId>::max()) {
                return fail(
                    WreckwaterLiveApplyFailure::
                        AttachmentIdentityExhausted,
                    index);
            }
            if (createCount >= creates.size()) {
                return fail(
                    WreckwaterLiveApplyFailure::
                        CreateCapacityExceeded,
                    index);
            }
            const WreckwaterTowBinding logical{
                .attachmentId = candidateNextAttachmentId,
                .attachmentGeneration = 1u,
                .cargoId = intent.cargoId,
                .cargoGeneration = intent.cargoGeneration,
                .skiffId = intent.targetSkiff,
                .skiffGeneration =
                    intent.targetSkiffGeneration,
            };
            ++candidateNextAttachmentId;
            candidateTows[destination] = {
                .logical = logical,
                .attachment = {},
                .occupied = true,
            };
            ++candidateTowCount;
            creates[createCount] = attachmentDesc(
                {
                    WreckwaterEntityKind::Skiff,
                    intent.targetSkiff,
                    intent.targetSkiffGeneration,
                },
                {
                    WreckwaterEntityKind::Cargo,
                    intent.cargoId,
                    intent.cargoGeneration,
                });
            if (!creates[createCount].bodyA.valid()
                || !creates[createCount].bodyB.valid()) {
                return fail(
                    WreckwaterLiveApplyFailure::
                        UnboundAttachmentEndpoint,
                    index);
            }
            pendingCreates[createCount] = {
                .towSlot = destination,
                .intentIndex = index,
            };
            ack.newAttachmentId = logical.attachmentId;
            ack.newAttachmentGeneration =
                logical.attachmentGeneration;
            ++createCount;
        }
        tentativeAcks[index] = ack;
    }

    const physics::PhysicsMutationBatch batch{
        .bodyCommands = {},
        .attachmentDestroys =
            std::span(destroys.data(), destroyCount),
        .attachmentCreates =
            std::span(creates.data(), createCount),
    };
    lastApplyDiagnostic_.requestedDestroyCount =
        static_cast<uint32_t>(destroyCount);
    lastApplyDiagnostic_.requestedCreateCount =
        static_cast<uint32_t>(createCount);
    prepared = transactions_->prepare(batch);
    preparedReturned = true;
    lastApplyDiagnostic_.preparedStatus = prepared.status;
    lastApplyDiagnostic_.preparedTargetTick = prepared.targetTick;
    lastApplyDiagnostic_.preparedCreatedCount =
        static_cast<uint32_t>(std::min(
            prepared.createdAttachments.size(),
            static_cast<size_t>(
                std::numeric_limits<uint32_t>::max())));
    if (!prepared.ready()) {
        discardPrepared();
        return fail(
            WreckwaterLiveApplyFailure::PrepareRejected);
    }
    if (prepared.targetTick != key.applicationTick) {
        discardPrepared();
        return fail(
            WreckwaterLiveApplyFailure::PreparedTargetMismatch);
    }
    if (prepared.createdAttachments.size() != createCount) {
        discardPrepared();
        return fail(
            WreckwaterLiveApplyFailure::
                PreparedCreateCountMismatch);
    }
    for (size_t index = 0u; index < createCount; ++index) {
        const physics::AttachmentHandle handle =
            prepared.createdAttachments[index];
        if (!handle.valid() || handle.generation == 0u) {
            discardPrepared();
                return fail(
                    WreckwaterLiveApplyFailure::
                        InvalidCreatedAttachment,
                    pendingCreates[index].intentIndex);
        }
        for (size_t other = 0u; other < index; ++other) {
            if (prepared.createdAttachments[other].index
                == handle.index) {
                discardPrepared();
                return fail(
                    WreckwaterLiveApplyFailure::
                        DuplicateCreatedAttachment,
                    pendingCreates[index].intentIndex);
            }
        }
        for (const TowSlot& active : tows_) {
            if (!active.occupied
                || active.attachment.index != handle.index) {
                continue;
            }
            const bool activeWasDestroyed = std::find(
                destroys.begin(),
                destroys.begin()
                    + static_cast<std::ptrdiff_t>(destroyCount),
                active.attachment)
                != destroys.begin()
                    + static_cast<std::ptrdiff_t>(destroyCount);
            if (!activeWasDestroyed
                || active.attachment.generation
                    == handle.generation) {
                discardPrepared();
                return fail(
                    WreckwaterLiveApplyFailure::
                        CreatedAttachmentCollision,
                    pendingCreates[index].intentIndex);
            }
        }
        candidateTows[pendingCreates[index].towSlot].attachment =
            handle;
    }

    const physics::PhysicsMutationResult committed =
        transactions_->commit(prepared);
    lastApplyDiagnostic_.committedStatus = committed.status;
    lastApplyDiagnostic_.committedTargetTick =
        committed.targetTick;
    lastApplyDiagnostic_.committedBodyCommandCount =
        committed.bodyCommandCount;
    lastApplyDiagnostic_.committedDestroyCount =
        committed.destroyedAttachmentCount;
    lastApplyDiagnostic_.committedCreateCount =
        committed.createdAttachmentCount;
    if (committed.status
        == physics::PhysicsMutationStatus::Committed) {
        preparedReturned = false;
    }
    if (!committed) {
        discardPrepared();
        return fail(WreckwaterLiveApplyFailure::CommitRejected);
    }
    if (committed.targetTick != prepared.targetTick) {
        discardPrepared();
        return fail(
            WreckwaterLiveApplyFailure::CommittedTargetMismatch);
    }
    if (committed.bodyCommandCount != 0u
        || committed.destroyedAttachmentCount != destroyCount
        || committed.createdAttachmentCount != createCount) {
        discardPrepared();
        return fail(
            WreckwaterLiveApplyFailure::CommittedCountMismatch);
    }

    tows_ = candidateTows;
    towCount_ = candidateTowCount;
    nextAttachmentId_ = candidateNextAttachmentId;
    lastCommittedApplicationTick_ = key.applicationTick;
    rememberReceipt(
        key, payloadHash, intents,
        std::span(
            tentativeAcks.data(), intents.size()));
    std::memcpy(
        acknowledgements.data(), tentativeAcks.data(),
        intents.size() * sizeof(MatchWorldIntentAck));
    return true;
}

} // namespace voxy::game
