#include "game/expedition/session_transactions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>

namespace voxy::game::expedition {
size_t ownedExtraBytes(const JournalRecord& record) noexcept {
    return std::visit([](const auto& value)->size_t {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,RequestAdmissionRecord>)return ownedExtraBytes(value.command);
        else if constexpr(std::is_same_v<T,RequestDecisionRecord>) {
            const auto* edit=std::get_if<BuildTransition>(&value.objects);
            return ownedExtraBytes(value.command)+(edit?ownedExtraBytes(*edit):0);
        } else if constexpr(std::is_same_v<T,StarterKitRegisteredRecord>)return starterKitPayloadBytes({&value.kit,1});
        else return 0;
    },record.payload);
}
bool validAssignedRange(AssignedIdRange range,WorldNamespace world,uint64_t issued) noexcept {
    if(!range.count)return range.first==DurableId{};
    return construction::isValid(range.first) && range.first.world==world && range.first.counter<=issued
        && range.count<=construction::kMaximumBuildParts+construction::kMaximumBuildConnections
        && range.count-1<=issued-range.first.counter;
}
namespace {
constexpr auto kMaximum = std::numeric_limits<uint64_t>::max();
bool validId(DurableId id, WorldNamespace world) noexcept {
    return construction::isValid(id) && id.world == world;
}
bool validCaller(CallerContext caller, WorldNamespace world) noexcept {
    return validId(caller.participant, world) && validId(caller.sessionToken, world)
        && caller.participant != caller.sessionToken;
}
bool validAllocator(AllocatorMarkers value, CallerContext caller) noexcept {
    return value.issuedThrough <= value.reservedThrough
        && caller.participant.counter <= value.issuedThrough
        && caller.sessionToken.counter <= value.issuedThrough;
}
bool validKey(const RequestKey& key, const Command& command, const JournalRecord& record) noexcept {
    return validCaller(key.caller, record.world) && key.epoch == record.epoch
        && key.admissionGeneration.valid() && key.sequence.valid()
        && command.epoch == key.epoch && command.sequence == key.sequence;
}
bool increments(uint64_t before, uint64_t after) noexcept {
    return before != kMaximum && after == before + 1;
}
bool validHistory(const HistoryTransition& history) noexcept {
    if (!history.before.valid() || !history.after.valid() || history.evictedCount > history.evicted.size()) return false;
    for (size_t i = 0; i < history.evicted.size(); ++i) {
        if (i >= history.evictedCount) {
            if (history.evicted[i].value() != 0) return false;
        } else {
            // Entries are created at the incremented generation (first is 2).
            if (history.evicted[i].value()<2 || history.evicted[i].value()>history.before.value()) return false;
            for (size_t j = 0; j < i; ++j) if (history.evicted[i] == history.evicted[j]) return false;
        }
    }
    switch (history.action) {
    case HistoryAction::None:
        return history.before == history.after && !history.entry && history.evictedCount == 0;
    case HistoryAction::Clear:
    case HistoryAction::Service:
        return increments(history.before.value(), history.after.value()) && !history.entry;
    case HistoryAction::Record:
    case HistoryAction::Undo:
    case HistoryAction::Redo:
        return increments(history.before.value(), history.after.value()) && history.entry && history.entry->valid();
    }
    return false;
}
bool validHeader(const BuildHeader& header, WorldNamespace world, DurableId owner) noexcept {
    return validId(header.id, world) && header.owner == owner
        && (!header.editLease || (header.editLease->holder == owner
            && header.editLease->epoch.valid() && header.editLease->expiresAfter.value() != 0));
}
bool validTransition(const ObjectTransition& transition, const RequestDecisionRecord& decision,
                     WorldNamespace world) noexcept {
    if (const auto* build = std::get_if<BuildTransition>(&transition)) {
        if (!build->after || !validHeader(*build->after, world, decision.key.caller.participant)) return false;
        if (build->before) {
            if (!validHeader(*build->before, world, decision.key.caller.participant)
                || build->before->id != build->after->id
                || !increments(build->before->revision.value(), build->after->revision.value())) return false;
        } else if (build->after->revision.value() != 0 || !build->after->materialized || build->beforePart) return false;
        const auto validPart = [&](const construction::PartInstance& part) {
            return validId(part.id, world) && part.id != build->after->id
                && part.owningBuild == build->after->id;
        };
        if ((build->beforePart && (!build->before || !build->before->materialized || !validPart(*build->beforePart)))
            || (build->afterPart && (!build->after->materialized || !validPart(*build->afterPart)))) return false;
        if (build->beforePart && build->afterPart
            && (build->beforePart->id != build->afterPart->id
                || build->beforePart->definition != build->afterPart->definition
                || build->beforePart->provenance != build->afterPart->provenance)) return false;
        const auto* cut=std::get_if<CutWeld>(&decision.command.intent);
        if(bool(build->cut)!=bool(cut)||(bool(build->storage)||bool(build->cut))!=(decision.history.action==HistoryAction::Service))return false;
        if(build->cut) {
            const auto& link=build->cut->before;
            if(!build->before||!build->before->materialized||!build->after->materialized
                ||build->before->owner!=build->after->owner||build->before->editLease!=build->after->editLease
                ||build->beforePart||build->afterPart||build->refit||build->storage||!build->refitForward
                ||cut->target.build!=build->after->id||cut->target.expectedRevision!=build->before->revision
                ||cut->connection!=link.id||decision.outcome.object!=link.id||decision.balanceBefore!=decision.balanceAfter
                ||!validId(link.id,world)||link.id.counter>decision.allocator.issuedThrough
                ||link.kind!=construction::ConnectionKind::Weld||!link.enabled||link.damage>construction::kFullHealth)return false;
        }
        if(build->storage) {
            const auto& storage=*build->storage;
            if(!isRefitIntent(decision.command.intent)||!build->refit||!build->refitForward
                ||storage.deposited.size()>32||storage.withdrawn.size()>32||storage.retainedBytes()>kMaximumStorageTransitionBytes
                ||bool(storage.starterBefore)!=std::holds_alternative<RebuildStarter>(decision.command.intent)
                ||(!storage.starterBefore&&(storage.withdrawn.empty()||!storage.deposited.empty())))return false;
            for(const auto* list:{&storage.deposited,&storage.withdrawn})
                for(size_t i=0;i<list->size();++i) {
                    const auto& part=(*list)[i];
                    if(!validPart(part)||part.id.counter>decision.allocator.issuedThrough||part.provenance!=construction::PartProvenance{}
                        ||(i&&!((*list)[i-1].id<part.id)))return false;
                }
        }
        if(build->refit) {
            if(!build->before || !build->before->materialized || !build->after->materialized
                || build->beforePart || build->afterPart || build->refit->build()!=build->after->id
                || build->before->owner!=build->after->owner || build->before->editLease!=build->after->editLease
                || build->refitForward==std::holds_alternative<Undo>(decision.command.intent))return false;
            if(!visitPartChanges(*build,[&](const auto& before,const auto& after) {
                return (!before || (validPart(*before)&&before->id.counter<=decision.allocator.issuedThrough))
                    && (!after || (validPart(*after)&&after->id.counter<=decision.allocator.issuedThrough));
            }))return false;
            for(const auto& change:build->refit->welds()) {
                if(bool(change.before)==bool(change.after))return false;
                const auto& weld=change.before?*change.before:*change.after;
                if(!validId(weld.id,world) || weld.id.counter>decision.allocator.issuedThrough
                    || weld.kind!=construction::ConnectionKind::Weld)return false;
            }
        } else if(!build->refitForward)return false;
        return decision.outcome.buildRevision == build->after->revision;
    }
    if (const auto* job = std::get_if<JobTransition>(&transition)) {
        return validId(job->before.id, world) && job->before.id == job->after.id
            && job->before.generation != 0 && job->before.generation == job->after.generation
            && job->before.phase == JobPhase::Available && job->before.acceptedBy == DurableId{}
            && job->after.phase == JobPhase::Accepted && job->after.acceptedBy == decision.key.caller.participant
            && !decision.outcome.buildRevision;
    }
    if(const auto* delivery=std::get_if<CargoDeliveryTransition>(&transition)) {
        const auto& c=delivery->cargo;const auto& before=delivery->before;const auto& after=delivery->after;
        const auto* intent=std::get_if<DeliverCargo>(&decision.command.intent);
        return intent && intent->cargo==c.id && validId(c.id,world) && c.owner==decision.key.caller.participant
            && c.job==before.id && validId(before.id,world) && before.generation && before.generation==after.generation
            && before.id==after.id && before.phase==JobPhase::Accepted && after.phase==JobPhase::Completed
            && before.acceptedBy==c.owner && after.acceptedBy==c.owner
            && decision.outcome.object==c.id && !decision.outcome.buildRevision
            && construction::isValid(c.definition.id) && c.definition.version && construction::isCanonical(c.orientation)
            && std::isfinite(c.position.x) && std::isfinite(c.position.y) && std::isfinite(c.position.z)
            && delivery->reward.salvageMaterial<=kMaximum-decision.balanceBefore.salvageMaterial
            && delivery->reward.specialMachinery<=kMaximum-decision.balanceBefore.specialMachinery
            && decision.balanceAfter.salvageMaterial==decision.balanceBefore.salvageMaterial+delivery->reward.salvageMaterial
            && decision.balanceAfter.specialMachinery==decision.balanceBefore.specialMachinery+delivery->reward.specialMachinery
            && decision.history.action==HistoryAction::None;
    }
    return false; // A current committed command always changes one identified object.
}
bool validRecord(const JournalRecord& record) noexcept {
    if (record.schema != kLogicalJournalSchema || !construction::isValid(record.world)
        || !record.epoch.valid() || record.sequence.value() != 0 || journalRecordBytes(record)>kMaximumJournalRecordBytes) return false;
    return std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, RequestAdmissionRecord>) {
            return validKey(value.key, value.command, record)
                && increments(value.admittedBefore.value(), value.key.sequence.value())
                && value.processedThrough <= value.admittedBefore
                && value.admittedBefore.value() - value.processedThrough.value() < 2
                && validAllocator(value.allocator, value.key.caller)
                && validAssignedRange(value.assignedRange,record.world,value.allocator.issuedThrough)
                && (!value.assignedRange.count || (isRefitIntent(value.command.intent) && !value.assignedObject))
                && (!isRefitIntent(value.command.intent) || !value.assignedObject)
                && (!std::holds_alternative<CutWeld>(value.command.intent) || !value.assignedObject)
                && (!value.assignedObject || (validId(*value.assignedObject, record.world)
                    && value.assignedObject->counter <= value.allocator.issuedThrough
                    && *value.assignedObject != value.key.caller.participant
                    && *value.assignedObject != value.key.caller.sessionToken));
        } else if constexpr (std::is_same_v<T, RequestDecisionRecord>) {
            if (!validKey(value.key, value.command, record)
                || !increments(value.processedBefore.value(), value.key.sequence.value())
                || !validAllocator(value.allocator, value.key.caller) || !validHistory(value.history)
                || value.outcome.revision != value.afterRevision) return false;
            if (value.outcome.state == ReceiptState::Rejected) {
                return value.beforeRevision == value.afterRevision && value.balanceBefore == value.balanceAfter
                    && value.objects.index() == 0 && value.history.action == HistoryAction::None
                    && value.outcome.error != SessionError::None
                    && value.outcome.error <= SessionError::DeliveryNotReady
                    && value.outcome.buildError <= construction::BuildError::NonCanonicalOrder
                    && (value.outcome.issueObject == DurableId{} || validId(value.outcome.issueObject, record.world))
                    && !value.outcome.object && !value.outcome.buildRevision;
            }
            return value.outcome.state == ReceiptState::Committed && value.outcome.error == SessionError::None
                && value.outcome.buildError == construction::BuildError::None && value.outcome.issueObject == DurableId{}
                && increments(value.beforeRevision.value(), value.afterRevision.value())
                && value.outcome.object && validId(*value.outcome.object, record.world)
                && value.outcome.object->counter <= value.allocator.issuedThrough
                && validTransition(value.objects, value, record.world);
        } else if constexpr (std::is_same_v<T, AllocatorLeaseRecord>) {
            return value.issuedThrough <= value.reservedBefore && value.reservedBefore < value.reservedAfter
                && value.reservedAfter - value.reservedBefore <= 64;
        } else if constexpr (std::is_same_v<T, AdmissionOpenedRecord>) {
            return validCaller(value.caller, record.world) && value.generation.valid()
                && value.retiredThrough.value() < value.generation.value() && validAllocator(value.allocator, value.caller);
        } else if constexpr (std::is_same_v<T, AdmissionClosedRecord>) {
            return validCaller(value.caller, record.world) && value.generation.valid();
        } else if constexpr (std::is_same_v<T, StarterKitRegisteredRecord>) {
            const auto& kit=value.kit;
            if(!validCaller(value.caller,record.world)||!value.admission.valid()||!validAllocator(value.allocator,value.caller)
                ||!increments(value.beforeRevision.value(),value.afterRevision.value())
                ||value.admittedThrough!=value.processedThrough||!kit.design||kit.design->parts().empty()
                ||kit.design->parts().size()>32||kit.design->welds().size()>64||kit.partCount!=kit.design->parts().size()
                ||!validId(kit.build,record.world)||!validId(kit.entitlement,record.world)
                ||kit.build.counter>value.allocator.issuedThrough||kit.entitlement.counter>value.allocator.issuedThrough
                ||kit.build==kit.entitlement||kit.build==value.caller.participant||kit.build==value.caller.sessionToken
                ||kit.entitlement==value.caller.participant||kit.entitlement==value.caller.sessionToken)return false;
            for(size_t i=0;i<kit.partIds.size();++i){
                const auto id=kit.partIds[i];
                if(i>=kit.partCount){if(id!=DurableId{})return false;continue;}
                if(!validId(id,record.world)||id.counter>value.allocator.issuedThrough||id==kit.build||id==kit.entitlement
                    ||id==value.caller.participant||id==value.caller.sessionToken||kit.design->parts()[i].source!=DurableId{})return false;
                for(size_t prior=0;prior<i;++prior)if(kit.partIds[prior]==id)return false;
            }
            return true;
        } else if constexpr (std::is_same_v<T, EntitlementRetiredRecord>) {
            if (!validCaller(value.caller, record.world) || !value.admission.valid()
                || !validAllocator(value.allocator, value.caller) || !validId(value.entitlement, record.world)
                || value.entitlement.counter > value.allocator.issuedThrough
                || value.entitlement == value.caller.participant || value.entitlement == value.caller.sessionToken
                || !increments(value.beforeRevision.value(), value.afterRevision.value())
                || !value.history.before.valid() || !increments(value.history.before.value(), value.history.after.value())
                || value.processedThrough > value.admittedThrough
                || value.admittedThrough.value() - value.processedThrough.value() > 2) return false;
            const auto list = [](const auto& values, size_t count, const auto& valid) {
                if (count > values.size()) return false;
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i < count) {
                        if (!valid(values[i]) || (i != 0 && !(values[i - 1] < values[i]))) return false;
                    } else if (values[i] != std::decay_t<decltype(values[i])>{}) return false;
                }
                return true;
            };
            const auto retired = [&](DurableId id) {
                return validId(id, record.world) && id.counter <= value.allocator.issuedThrough
                    && id != value.entitlement && id != value.caller.participant && id != value.caller.sessionToken;
            };
            if (!list(value.history.invalidatedEntries, value.history.entryCount,
                    [&](HistoryEntryId id) { return id.valid() && id.value() <= value.history.before.value(); })
                || !list(value.history.retiredParts, value.history.partCount, retired)
                || !list(value.history.retiredBuilds, value.history.buildCount, retired)) return false;
            for (size_t i = 0; i < value.history.partCount; ++i)
                for (size_t j = 0; j < value.history.buildCount; ++j)
                    if (value.history.retiredParts[i] == value.history.retiredBuilds[j]) return false;
            return true;
        } else {
            const auto& origin = value.origin;
            const auto& opened = value.admission;
            if (origin.parent.world != record.world || !origin.parent.epoch.valid()
                || !origin.parent.writerGeneration.valid() || origin.parent.writerGeneration.value() == kMaximum
                || origin.closedThrough.value() == 0 || origin.closedThrough.value() < origin.processedThrough.value()
                || !origin.historyGeneration.valid()
                || !origin.generation.valid() || !validCaller(origin.caller, record.world)
                || !validAllocator(origin.allocator, origin.caller) || origin.allocator.reservedThrough == kMaximum
                || !increments(origin.parent.epoch.value(), record.epoch.value())
                || !increments(origin.generation.value(), opened.generation.value())
                || opened.retiredThrough.value() != origin.generation.value()
                || !validCaller(opened.caller, record.world) || opened.caller.participant != origin.caller.participant
                || opened.caller.sessionToken.counter != origin.allocator.reservedThrough + 1
                || opened.allocator.issuedThrough != opened.caller.sessionToken.counter
                || value.leases != RecoveryLeasePolicy::ReleaseLocalParticipantLeases) return false;
            const auto amount = std::min(uint64_t{64}, kMaximum - origin.allocator.reservedThrough);
            return opened.allocator.reservedThrough == origin.allocator.reservedThrough + amount;
        }
    }, record.payload);
}
} // namespace

bool validAssignedJournalRecord(const JournalRecord& record) noexcept {
    if (!record.sequence.valid()) return false;
    auto unassigned = record;
    unassigned.sequence = {};
    return validRecord(unassigned);
}

static_assert(std::is_nothrow_copy_assignable_v<RefitBuild>);
static_assert(std::is_nothrow_copy_constructible_v<RefitBuild>);
static_assert(std::is_nothrow_copy_assignable_v<BuildTransition>);
static_assert(std::is_nothrow_move_constructible_v<JournalPayload>);
static_assert(std::is_nothrow_copy_assignable_v<JournalRecord>);
static_assert(std::is_nothrow_default_constructible_v<JournalRecord>);

JournalOutbox::JournalOutbox(JournalIdentity identity, JournalLimits limits, JournalSeed seed) noexcept
    : identity_(identity), limits_(limits), appendedThrough_(seed.appendedThrough),
      modelReleasedThrough_(seed.appendedThrough), reservationGeneration_(seed.lastReservationGeneration) {}
std::unique_ptr<JournalOutbox> JournalOutbox::create(
    JournalIdentity identity, JournalLimits limits, JournalSeed seed, JournalError& issue) {
    if (!construction::isValid(identity.world) || !identity.epoch.valid() || !identity.writerGeneration.valid()) {
        issue = JournalError::InvalidIdentity;
        return nullptr;
    }
    if (limits.records == 0 || limits.records > kMaximumJournalRecords
        || limits.payloadBytes < kJournalRecordStorageBytes || limits.payloadBytes > kMaximumJournalPayloadBytes) {
        issue = JournalError::InvalidLimits;
        return nullptr;
    }
    try {
        auto result = std::unique_ptr<JournalOutbox>(new JournalOutbox(identity, limits, seed));
        issue = JournalError::None;
        return result;
    } catch (const std::bad_alloc&) {
        issue = JournalError::Capacity;
        return nullptr;
    }
}
JournalReservationResult JournalOutbox::reserve(JournalDemand demand) noexcept {
    if (demand.records == 0 || demand.records > kMaximumJournalRecords
        || demand.payloadBytes < static_cast<size_t>(demand.records) * kJournalRecordStorageBytes
        || demand.payloadBytes > kMaximumJournalPayloadBytes) return {{}, JournalError::InvalidDemand};
    if (reservationGeneration_ == kMaximum) return {{}, JournalError::ReservationExhausted};
    const uint64_t unassigned = kMaximum - appendedThrough_.value();
    if (reservedRecords_ > unassigned || demand.records > unassigned - reservedRecords_)
        return {{}, JournalError::SequenceExhausted};
    if (demand.records > limits_.records - count_ - reservedRecords_
        || demand.payloadBytes > limits_.payloadBytes - retainedBytes() - reservedBytes_)
        return {{}, JournalError::Capacity};
    for (size_t i = 0; i < reservations_.size(); ++i) {
        auto& slot = reservations_[i];
        if (slot.records != 0) continue;
        slot = {++reservationGeneration_, demand.records, demand.payloadBytes};
        reservedRecords_ += demand.records;
        reservedBytes_ += demand.payloadBytes;
        return {{identity_, slot.generation, static_cast<uint32_t>(i)}, JournalError::None};
    }
    return {{}, JournalError::Capacity};
}
const JournalOutbox::ReservationState* JournalOutbox::find(JournalReservation reservation) const noexcept {
    if (reservation.identity != identity_ || reservation.slot >= reservations_.size() || reservation.generation == 0) return nullptr;
    const auto& slot = reservations_[reservation.slot];
    return slot.records != 0 && slot.generation == reservation.generation ? &slot : nullptr;
}
bool JournalOutbox::discard(JournalReservation reservation) noexcept {
    if (!find(reservation)) return false;
    auto& slot = reservations_[reservation.slot];
    reservedRecords_ -= slot.records;
    reservedBytes_ -= slot.bytes;
    slot = {};
    return true;
}
bool JournalOutbox::canAppend(JournalReservation reservation, const JournalRecord& record) const noexcept {
    const auto* slot = find(reservation);
    return slot && slot->bytes >= journalRecordBytes(record)+(slot->records-1)*kJournalRecordStorageBytes && record.world == identity_.world
        && record.epoch == identity_.epoch && appendedThrough_.value() != kMaximum && validRecord(record);
}
std::optional<JournalSequence> JournalOutbox::appendReserved(
    JournalReservation reservation, const JournalRecord& record) noexcept {
    if (!canAppend(reservation, record)) return std::nullopt;
    auto& slot = reservations_[reservation.slot];
    const JournalSequence assigned{appendedThrough_.value() + 1};
    auto& destination = records_[(begin_ + count_) % records_.size()];
    destination = record;
    destination.sequence = assigned;
    ++count_;
    --slot.records;
    --reservedRecords_;
    const auto bytes=journalRecordBytes(record);
    slot.bytes -= bytes;
    reservedBytes_ -= bytes;
    retainedBytes_ += bytes;
    appendedThrough_ = JournalFrontier{assigned.value()};
    if (slot.records == 0) {
        reservedBytes_ -= slot.bytes; // Release deliberate over-reservation only at the final append.
        slot = {};
    }
    return assigned;
}
size_t JournalOutbox::copyPrefix(std::span<JournalRecord> output) const noexcept {
    const size_t count = std::min(output.size(), count_);
    for (size_t i = 0; i < count; ++i) output[i] = records_[(begin_ + i) % records_.size()];
    return count;
}
JournalError JournalOutbox::releaseModelWrittenPrefix(std::span<const JournalRecord> prefix) noexcept {
    if (prefix.empty() || prefix.size() > count_) return JournalError::PrefixMismatch;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (prefix[i] != records_[(begin_ + i) % records_.size()]) return JournalError::PrefixMismatch;
    const auto through = prefix.back().sequence;
    for (size_t i = 0; i < prefix.size(); ++i) {
        auto& record=records_[(begin_+i)%records_.size()];
        retainedBytes_-=journalRecordBytes(record);record={};
    }
    begin_ = (begin_ + prefix.size()) % records_.size();
    count_ -= prefix.size();
    modelReleasedThrough_ = JournalFrontier{through.value()};
    return JournalError::None;
}

} // namespace voxy::game::expedition
