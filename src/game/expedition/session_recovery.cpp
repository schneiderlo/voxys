#include "game/expedition/session_recovery.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>

namespace voxy::game::expedition {
namespace {
constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
bool validContent(RecoveryContentIdentity value) {
    return construction::isValid(value.manifest.id) && value.manifest.version != 0
        && value.profile == 1 && value.profileVersion == 1
        && std::any_of(value.manifestDigest.begin(), value.manifestDigest.end(), [](std::byte b) { return b != std::byte{}; });
}
bool observedNext(RequestSequence request, RequestSequence expected, RequestFrontier admitted) {
    if (!expected.valid()) return admitted.value() == maximum;
    return expected.value() > request.value()
        && (admitted.value() == maximum || expected.value() <= admitted.value() + 1);
}
bool resources(ResourceAmounts& balance, ResourceAmounts debit, ResourceAmounts credit) {
    if (balance.salvageMaterial < debit.salvageMaterial || balance.specialMachinery < debit.specialMachinery) return false;
    const ResourceAmounts reduced{balance.salvageMaterial - debit.salvageMaterial, balance.specialMachinery - debit.specialMachinery};
    if (credit.salvageMaterial > maximum - reduced.salvageMaterial || credit.specialMachinery > maximum - reduced.specialMachinery) return false;
    balance = {reduced.salvageMaterial + credit.salvageMaterial, reduced.specialMachinery + credit.specialMachinery};
    return true;
}
BuildHeader headerOf(const BuildSnapshot& build) { return {build.id, build.revision, build.owner, build.editLease, true}; }
template<class T> bool ordered(const T& values) {
    for (size_t i = 1; i < values.size(); ++i) if (!(values[i - 1].id < values[i].id)) return false;
    return true;
}
template<class T, size_t N> bool emptyTail(const std::array<T, N>& values, size_t count) {
    return std::all_of(values.begin() + static_cast<std::ptrdiff_t>(count), values.end(), [](const T& value) { return value == T{}; });
}
struct WorkingPart {
    construction::PartInstance value{};
    bool active = false;
    bool operator==(const WorkingPart&) const = default;
};
struct WorkingBuild { BuildHeader header{}; std::vector<construction::Connection> connections{}; };
bool refitEconomy(const construction::BuildRefitDelta& delta,const PartCatalog& catalog,ResourceAmounts& debit,ResourceAmounts& credit,const PartStorageTransition* storage=nullptr) {
    debit={};credit={};
    const auto add=[](ResourceAmounts& total,ResourceAmounts amount) {
        if(amount.salvageMaterial>maximum-total.salvageMaterial || amount.specialMachinery>maximum-total.specialMachinery)return false;
        total.salvageMaterial+=amount.salvageMaterial;total.specialMachinery+=amount.specialMachinery;return true;
    };
    for(const auto& change:delta.parts()) {
        const auto& part=change.before?*change.before:*change.after;
        const auto* definition=catalog.lookup(part.definition).definition;if(!definition)return false;
        if(!change.before) {
            construction::PartInstance paid;
            paid.id=part.id;paid.definition=part.definition;paid.owningBuild=part.owningBuild;
            paid.placement=part.placement;paid.settings=part.settings;paid.paint=part.paint;
            if(storage&&storage->starterBefore)paid.provenance={construction::PartOrigin::StarterLoan,storage->starterBefore->entitlement};
            const auto* withdrawn=storage?&storage->withdrawn:nullptr;
            const auto stored=withdrawn?std::find_if(withdrawn->begin(),withdrawn->end(),[&](const auto& p){return p.id==part.id;}):std::vector<construction::PartInstance>::const_iterator{};
            if(withdrawn&&stored!=withdrawn->end()) {
                paid.health=stored->health;
                if(stored->definition!=paid.definition||stored->owningBuild!=paid.owningBuild||stored->provenance!=paid.provenance)return false;
            }
            if(part!=paid)return false;
            if(!(storage&&storage->starterBefore)&&!(withdrawn&&stored!=withdrawn->end())&&!add(debit,definition->cost))return false;
        } else if(!change.after && !(storage&&storage->starterBefore) && part.provenance.origin!=construction::PartOrigin::StarterLoan && !add(credit,definition->salvageYield))return false;
    }
    const ResourceAmounts offset{std::min(debit.salvageMaterial,credit.salvageMaterial),std::min(debit.specialMachinery,credit.specialMachinery)};
    debit.salvageMaterial-=offset.salvageMaterial;credit.salvageMaterial-=offset.salvageMaterial;
    debit.specialMachinery-=offset.specialMachinery;credit.specialMachinery-=offset.specialMachinery;return true;
}
std::vector<DurableId> refitCreatedIds(const construction::BuildRefitDelta& delta,const PartStorageTransition* storage=nullptr) {
    std::vector<DurableId> ids;
    for(const auto& change:delta.parts())if(!change.before
        &&(!storage||std::none_of(storage->withdrawn.begin(),storage->withdrawn.end(),[&](const auto& p){return p.id==change.after->id;})))ids.push_back(change.after->id);
    for(const auto& change:delta.welds())if(!change.before)ids.push_back(change.after->id);
    std::sort(ids.begin(),ids.end());return ids;
}
StarterKit reboundKit(const StarterKit& before,std::span<const DurableId> created) {
    auto after=before;after.partIds={};
    if(created.size()<after.partCount)return {};
    std::copy_n(created.begin(),after.partCount,after.partIds.begin());return after;
}
bool transferStock(std::vector<construction::PartInstance>& stock,const PartStorageTransition& storage,bool forward) {
    const auto& remove=forward?storage.withdrawn:storage.deposited;
    const auto& add=forward?storage.deposited:storage.withdrawn;
    for(const auto& part:remove) {
        const auto found=std::find_if(stock.begin(),stock.end(),[&](const auto& p){return p.id==part.id;});
        if(found==stock.end()||*found!=part)return false;
        stock.erase(found);
    }
    for(const auto& part:add) {
        if(std::any_of(stock.begin(),stock.end(),[&](const auto& p){return p.id==part.id;}))return false;
        stock.push_back(part);
    }
    std::sort(stock.begin(),stock.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return stock.size()<=construction::kMaximumStoredParts;
}
std::optional<construction::BuildRefitPlan> refitMatches(const BuildSnapshot& before,const Intent& intent,
    const BuildTransition& edit,const PartCatalog& catalog,std::span<const StarterKit> kits,
    std::span<const construction::PartInstance> stock) {
    const auto* design=refitDesign(intent,kits);
    const auto* target=std::visit([](const auto& v)->const BuildTarget*{if constexpr(requires{v.target;})return &v.target;else return nullptr;},intent);
    if(!design||!target||!edit.refit||!edit.refitForward||edit.beforePart||edit.afterPart
        ||before.id!=target->build||before.revision!=target->expectedRevision)return {};
    const bool rebuild=std::holds_alternative<RebuildStarter>(intent);
    const auto kit=std::find_if(kits.begin(),kits.end(),[&](const auto& k){return k.build==before.id;});
    if(rebuild&&(kit==kits.end()||!edit.storage||edit.storage->starterBefore!=*kit))return {};
    if(!rebuild&&edit.storage&&edit.storage->starterBefore)return {};
    if(rebuild) {
        if(kit->partCount!=design->parts().size()||!kit->partCount||kit->partCount>kit->partIds.size()
            ||!emptyTail(kit->partIds,kit->partCount))return {};
        for(size_t i=0;i<kit->partCount;++i) {
            const auto id=kit->partIds[i];
            if(!construction::isValid(id)||id.world!=before.id.world||id==before.id||id==before.owner||id==kit->entitlement
                ||std::find(kit->partIds.begin(),kit->partIds.begin()+static_cast<std::ptrdiff_t>(i),id)!=kit->partIds.begin()+static_cast<std::ptrdiff_t>(i))return {};
        }
        // A covered journal can begin after registration. Reconstructing its
        // predecessor must still bind every then-active loan to that grant.
        for(const auto& part:before.parts)if(part.provenance.origin==construction::PartOrigin::StarterLoan) {
            const auto binding=std::find(kit->partIds.begin(),kit->partIds.begin()+kit->partCount,part.id);
            if(binding==kit->partIds.begin()+kit->partCount||part.provenance.starterEntitlement!=kit->entitlement
                ||part.definition!=design->parts()[static_cast<size_t>(binding-kit->partIds.begin())].design.definition)return {};
        }
    }
    construction::BuildIssue issue;const auto model=BuildModel::create(before,catalog,issue);if(!model)return {};
    const auto ids=refitCreatedIds(*edit.refit,edit.storage.get());
    for(size_t i=1;i<ids.size();++i)
        if(ids[i].world!=ids[0].world||ids[i-1].counter==maximum||ids[i].counter!=ids[i-1].counter+1)return {};
    size_t used=0;
    auto prepared=construction::prepareBuildRefit(*model,*design,catalog,[&]()->std::optional<DurableId>{
        if(used==ids.size())return std::nullopt;
        return ids[used++];
    },issue,{stock,rebuild?std::optional{kit->entitlement}:std::nullopt});
    if(!prepared||used!=ids.size()||*prepared->delta!=*edit.refit||headerOf(prepared->after)!=edit.after
        ||bool(edit.storage)!=(rebuild||!prepared->consumedStoredParts.empty()))return {};
    if(edit.storage) {
        PartStorageTransition expected;
        if(rebuild) {
            expected.starterBefore=*kit;
            for(const auto& p:before.parts)if(p.provenance.origin==construction::PartOrigin::Paid)expected.deposited.push_back(p);
        }
        for(const auto& p:stock)
            if(std::find(prepared->consumedStoredParts.begin(),prepared->consumedStoredParts.end(),p.id)!=prepared->consumedStoredParts.end())expected.withdrawn.push_back(p);
        if(expected!=*edit.storage)return {};
    }
    return prepared;
}
bool cutMatches(const BuildSnapshot& before,const CutWeld& cut,const BuildTransition& edit,const PartCatalog& catalog) {
    if(!edit.cut||!edit.before||!edit.after||edit.refit||edit.storage||edit.beforePart||edit.afterPart
        ||before.id!=cut.target.build||before.revision!=cut.target.expectedRevision
        ||edit.cut->before.id!=cut.connection||headerOf(before)!=edit.before)return false;
    const auto link=std::find_if(before.connections.begin(),before.connections.end(),[&](const auto& c){return c.id==cut.connection;});
    if(link==before.connections.end()||!construction::sameConnection(*link,edit.cut->before))return false;
    construction::BuildIssue issue;auto model=BuildModel::create(before,catalog,issue);if(!model)return false;
    const std::array cuts{cut.connection};
    return !model->cutWelds(cut.target.expectedRevision,cuts,catalog)&&headerOf(model->snapshot())==edit.after;
}
size_t retainedReceiptBytes(const LogicalRecoveryCheckpoint& image) {
    size_t bytes=0;for(size_t i=0;i<image.receiptCount;++i)bytes+=ownedExtraBytes(image.receipts[i].command);return bytes;
}
size_t retainedJournalBytes(const LogicalRecoveryCheckpoint& image) {
    size_t bytes=0;for(size_t i=0;i<image.retainedJournalCount;++i)bytes+=journalRecordBytes(image.retainedJournal[i]);return bytes;
}

class Validator {
public:
    Validator(const LogicalRecoveryCheckpoint& value, ExpectedRecoveryIdentity expected, const PartCatalog& catalog)
        : image(value), boot(value.accepted), expected_(expected), catalog_(catalog) {}
    RecoveryIssue validate() {
        if (image.schema != kLogicalRecoverySchema || !image.historyClearedOnRestore
            || image.durability != ReceiptDurability::Volatile) return {RecoveryError::UnsupportedSchema};
        if (!validContent(expected_.content) || !construction::isValid(expected_.world)) return {RecoveryError::InvalidContentIdentity};
        if (image.content != expected_.content || boot.world != expected_.world) return {RecoveryError::IdentityMismatch};
        if (!bounds()) return {RecoveryError::Capacity};
        if (!markers()) return {RecoveryError::InvalidAdmission};
        if (!world()) return {capacity_ ? RecoveryError::Capacity : RecoveryError::InvalidWorld};
        if (!history()) return {RecoveryError::InvalidHistory};
        if (!requests()) return {RecoveryError::InvalidRequest};
        if (!journal()) return {RecoveryError::InvalidJournal};
        return {};
    }
private:
    bool bounds() const {
        const auto& h = image.history;
        if (boot.starterKits.size()>kMaximumStarterKits || boot.storedParts.size()>construction::kMaximumStoredParts
            || boot.builds.size() > 32 || boot.cargo.size() > 256 || boot.jobs.size() > 128
            || boot.starterEntitlements.size() > 256 || boot.cargoDefinitions.size() > 256
            || image.pendingCount > 2 || image.receiptCount > 64 || image.retainedJournalCount > kMaximumJournalRecords
            || h.count > 32 || h.applied > h.count || h.partCount > 64 || h.buildCount > 8) return false;
        size_t bytes = sizeof(LogicalRecoveryCheckpoint), totalParts = 0;
        const auto charge = [&](size_t count, size_t size) {
            if (bytes > kMaximumLogicalRecoveryBytes || (size && count > (kMaximumLogicalRecoveryBytes - bytes) / size)) return false;
            bytes += count * size; return true;
        };
        if (!charge(boot.starterKits.size(),sizeof(StarterKit)) || !charge(boot.storedParts.size(),sizeof(construction::PartInstance))
            || !charge(boot.builds.size(), sizeof(BuildSnapshot)) || !charge(boot.cargo.size(), sizeof(CargoRecord))
            || !charge(boot.jobs.size(), sizeof(JobRecord)) || !charge(boot.starterEntitlements.size(), sizeof(DurableId))
            || !charge(boot.cargoDefinitions.size(), sizeof(CargoDefinition))) return false;
        for (const auto& build : boot.builds) {
            if (build.parts.size() > construction::kMaximumBuildParts || build.connections.size() > construction::kMaximumBuildConnections)
                return false;
            totalParts += build.parts.size();
            if (totalParts > 256 || !charge(build.parts.size(), sizeof(construction::PartInstance))
                || !charge(build.connections.size(), sizeof(construction::Connection))) return false;
        }
        return charge(1,ownedExtraBytes(image)) && bytes==image.ownedBytes
            && retainedReceiptBytes(image)<=kMaximumReceiptOwnedBytes && retainedJournalBytes(image)<=boot.limits.journalBytes;
    }
    bool markers() const {
        const auto& a = image.admission;
        const auto& j = image.journalIdentity;
        return boot.epoch.valid() && a.generation.valid()
            && (a.open ? a.retiredThrough.value() < a.generation.value() : a.retiredThrough.value() == a.generation.value())
            && a.processedThrough <= a.admittedThrough
            && a.admittedThrough.value() - a.processedThrough.value() == image.pendingCount
            && (a.open || image.pendingCount == 0) && image.pendingCount <= boot.limits.pendingCommands
            && image.receiptCount <= a.processedThrough.value() && image.receiptCount <= boot.limits.receipts
            && image.allocator.issuedThrough == boot.lastIssuedId
            && image.allocator.issuedThrough <= image.allocator.reservedThrough
            && j.world == boot.world && j.epoch == boot.epoch && j.writerGeneration.valid()
            && image.modelReleasedThrough <= image.coveredThrough
            && image.coveredThrough.value() - image.modelReleasedThrough.value() == image.retainedJournalCount
            && image.coveredThrough.value() >= a.admittedThrough.value()
            && image.retainedJournalCount <= boot.limits.journalRecords && lineage();
    }
    bool lineage() const {
        if (!image.origin) return image.admission.generation == AdmissionGeneration{1}
            && image.journalIdentity.writerGeneration == JournalWriterGeneration{1};
        const auto& origin = *image.origin;
        if (origin.allocator.reservedThrough == maximum || image.coveredThrough.value() == 0) return false;
        const uint64_t reserved = origin.allocator.reservedThrough
            + std::min(uint64_t{64}, maximum - origin.allocator.reservedThrough);
        const RecoveryOpenedRecord opening{origin,
            {boot.caller, image.admission.generation, AdmissionFrontier{origin.generation.value()},
                {boot.caller.sessionToken.counter, reserved}}, boot.revision, boot.inventory};
        const JournalRecord record{kLogicalJournalSchema, boot.world, boot.epoch, JournalSequence{1}, boot.tick, opening};
        return validAssignedJournalRecord(record)
            && construction::next(origin.parent.writerGeneration) == image.journalIdentity.writerGeneration
            && image.history.generation >= origin.historyGeneration
            && (!image.admission.open || image.admission.retiredThrough.value() == origin.generation.value())
            && image.allocator.reservedThrough >= reserved;
    }
    bool validId(DurableId id) const {
        return construction::isValid(id) && id.world == boot.world && id.counter <= image.allocator.issuedThrough;
    }
    bool object(DurableId id) {
        if (!validId(id) || std::find(ids.begin(), ids.end(), id) != ids.end()) return false;
        ids.push_back(id); return true;
    }
    bool validHeader(const BuildHeader& header) const {
        return validId(header.id) && header.owner == boot.caller.participant
            && (!header.editLease || (header.editLease->holder == boot.caller.participant
                && header.editLease->epoch.valid() && header.editLease->expiresAfter.value() != 0));
    }
    WorkingBuild* findBuild(DurableId id) {
        const auto it = std::find_if(builds.begin(), builds.end(), [id](const auto& b) { return b.header.id == id; });
        return it == builds.end() ? nullptr : &*it;
    }
    WorkingPart* findPart(DurableId id) {
        const auto it = std::find_if(parts.begin(), parts.end(), [id](const auto& p) { return p.value.id == id; });
        return it == parts.end() ? nullptr : &*it;
    }
    bool validPart(const construction::PartInstance& part) {
        auto* build = findBuild(part.owningBuild);
        if (!validId(part.id) || !build) return false;
        if (part.provenance.origin == construction::PartOrigin::StarterLoan
            && std::find(boot.starterEntitlements.begin(), boot.starterEntitlements.end(), part.provenance.starterEntitlement)
                == boot.starterEntitlements.end()) return false;
        BuildSnapshot draft{build->header.id, build->header.revision, build->header.owner, build->header.editLease, {part}, {}};
        construction::BuildIssue issue;
        return BuildModel::create(draft, catalog_, issue).has_value();
    }
    bool world() {
        // Reuse canonical validation without issuing a live observation stream
        // or allocating a journal merely to inspect storage data.
        const auto issue = GameSession::validateInitialState(boot, catalog_);
        if (issue) {
            capacity_ = issue.error == SessionError::Capacity || issue.error == SessionError::JournalCapacity;
            return false;
        }
        if (!ordered(boot.builds) || !ordered(boot.cargo) || !ordered(boot.jobs) || !ordered(boot.storedParts)) return false;
        if (!object(boot.caller.participant) || !object(boot.caller.sessionToken)) return false;
        // A retired token remains a token identity; it cannot alias a current
        // part/build/job even though it is no longer an admitted caller.
        if (image.origin && !object(image.origin->caller.sessionToken)) return false;
        if(image.origin)for(const auto& kit:boot.starterKits)for(size_t i=0;i<kit.partCount;++i)
            if(kit.partIds[i]==image.origin->caller.sessionToken)return false;
        for (const auto id : boot.starterEntitlements) if (!object(id)) return false;
        for(const auto& part:boot.storedParts)if(!object(part.id))return false;
        for (const auto& value : boot.cargo) if (!object(value.id)) return false;
        for (const auto& value : boot.jobs) if (!object(value.id)) return false;
        for (const auto& build : boot.builds) {
            // Profile 1 has no post-recovery lease issuer. Its opening policy
            // revokes every old lease; an imported image cannot revive one.
            if (image.origin && build.editLease) return false;
            if (!object(build.id) || !ordered(build.parts) || !ordered(build.connections)) return false;
            builds.push_back({headerOf(build), build.connections});
            for (const auto& connection : build.connections) {
                if (!object(connection.id) || connection.b < connection.a) return false;
                historicalWelds.push_back(connection);
            }
            for (const auto& part : build.parts) {
                if (!object(part.id)) return false;
                parts.push_back({part, true});
            }
        }
        for (size_t i = 0; i < image.history.buildCount; ++i) {
            const auto& header = image.history.builds[i];
            if (header.materialized || !validHeader(header) || !object(header.id)) return false;
            builds.push_back({header, {}});
        }
        for (size_t i = 0; i < image.history.partCount; ++i) {
            const auto& part = image.history.parts[i];
            if (!object(part.id) || !validPart(part)) return false;
            parts.push_back({part, false});
        }
        return true;
    }
    bool historyEntry(const RecoveryHistoryEntry& entry) {
        const auto& e = entry.edit;
        if (e.storage || e.cut || !entry.id.valid() || entry.id.value() > image.history.generation.value()
            || entry.caller != boot.caller || entry.admission != image.admission.generation
            || !e.after || !e.after->materialized || !validHeader(*e.after)) return false;
        const auto* build = findBuild(e.after->id);
        if (!build || e.after->revision > build->header.revision || e.after->owner != build->header.owner
            || e.after->editLease != build->header.editLease) return false;
        ResourceAmounts debit{}, credit{};
        if (!e.before) {
            if (e.after->revision.value() != 0 || e.after->editLease || e.beforePart || e.afterPart || e.refit) return false;
        } else {
            if (!validHeader(*e.before) || !e.before->materialized || e.before->id != e.after->id
                || e.before->owner != e.after->owner || e.before->editLease != e.after->editLease
                || construction::next(e.before->revision) != e.after->revision || (!e.beforePart && !e.afterPart && !e.refit)) return false;
            const auto reference = [&](const construction::PartInstance& part) {
                const auto* current = findPart(part.id);
                return current && part.owningBuild == e.after->id && validPart(part)
                    && current->value.owningBuild == part.owningBuild && current->value.definition == part.definition
                    && current->value.provenance == part.provenance;
            };
            if(e.refit) {
                if(!e.refitForward || e.beforePart || e.afterPart || e.refit->build()!=e.after->id
                    || !visitPartChanges(e,[&](const auto& before,const auto& after){return (!before||reference(*before))&&(!after||reference(*after));})
                    || !refitEconomy(*e.refit,catalog_,debit,credit))return false;
                for(const auto& change:e.refit->welds()) {
                    const auto& weld=change.before?*change.before:*change.after;
                    if(!validId(weld.id) || weld.kind!=construction::ConnectionKind::Weld || !findPart(weld.a.part) || !findPart(weld.b.part))return false;
                    const auto found=std::find_if(historicalWelds.begin(),historicalWelds.end(),[&](const auto& old){return old.id==weld.id;});
                    if(found==historicalWelds.end()) {
                        if(!object(weld.id))return false;
                        historicalWelds.push_back(weld);
                    } else if(!construction::sameConnection(*found,weld))return false;
                }
                return entry.debit==debit && entry.credit==credit;
            }
            if(!e.refitForward || (e.beforePart && !reference(*e.beforePart)) || (e.afterPart && !reference(*e.afterPart))) return false;
            if (e.beforePart && e.afterPart) {
                auto same = *e.afterPart; same.placement = e.beforePart->placement;
                if (same != *e.beforePart) return false;
            } else if (e.afterPart) {
                const auto* definition = catalog_.lookup(e.afterPart->definition).definition;
                if (!definition) return false;
                construction::PartInstance expected;
                expected.id = e.afterPart->id; expected.definition = e.afterPart->definition;
                expected.owningBuild = e.after->id; expected.placement = e.afterPart->placement;
                expected.settings = construction::defaultModuleSettings(*definition);
                if (*e.afterPart != expected) return false;
                debit = definition->cost;
            } else {
                const auto* definition = catalog_.lookup(e.beforePart->definition).definition;
                if (!definition) return false;
                if (e.beforePart->provenance.origin != construction::PartOrigin::StarterLoan) credit = definition->salvageYield;
            }
        }
        return entry.debit == debit && entry.credit == credit;
    }
    bool historyStep(const RecoveryHistoryEntry& entry, bool forward, ResourceAmounts& balance) {
        const auto& edit = entry.edit;
        auto* build = findBuild(edit.after->id);
        const bool fromActive = forward ? edit.before.has_value() : true;
        const bool toActive = forward ? true : edit.before.has_value();
        if (!build || build->header.materialized != fromActive) return false;
        const auto& from = forward ? edit.beforePart : edit.afterPart;
        const auto& to = forward ? edit.afterPart : edit.beforePart;
        if(edit.refit) {
            BuildSnapshot draft{build->header.id,build->header.revision,build->header.owner,build->header.editLease,{},build->connections};
            for(const auto& part:parts)if(part.active&&part.value.owningBuild==draft.id)draft.parts.push_back(part.value);
            if(edit.refit->apply(draft,catalog_,forward))return false;
            BuildTransition directed=edit;directed.refitForward=forward;
            if(!visitPartChanges(directed,[&](const auto& source,const auto& destination) {
                auto* part=findPart(source?source->id:destination->id);
                if(!part || part->active!=source.has_value() || (source&&part->value!=*source)
                    || (!source&&part->value!=*destination))return false;
                if(destination)part->value=*destination;
                part->active=destination.has_value();return true;
            }))return false;
            build->connections=std::move(draft.connections);
        } else if (from || to) {
            auto* part = findPart(from ? from->id : to->id);
            if (!part || part->active != from.has_value() || (from && part->value != *from)
                || (!from && part->value != *to)) return false;
            if (to) part->value = *to;
            part->active = to.has_value();
        }
        if (!fromActive || !toActive) {
            if (!build->connections.empty() || std::any_of(parts.begin(), parts.end(), [&](const auto& p) {
                return p.active && p.value.owningBuild == build->header.id;
            })) return false;
            build->header.materialized = toActive;
        }
        if (!resources(balance, forward ? entry.debit : entry.credit, forward ? entry.credit : entry.debit)) return false;
        if (toActive) {
            BuildSnapshot draft{build->header.id, build->header.revision, build->header.owner, build->header.editLease, {}, {}};
            for (const auto& p : parts) if (p.active && p.value.owningBuild == draft.id) draft.parts.push_back(p.value);
            draft.connections.assign(build->connections.begin(), build->connections.end());
            construction::BuildIssue issue;
            if (!BuildModel::create(draft, catalog_, issue)) return false;
        }
        const auto activeParts = std::count_if(parts.begin(), parts.end(), [](const auto& p) { return p.active; });
        const auto activeBuilds = std::count_if(builds.begin(), builds.end(), [](const auto& b) { return b.header.materialized; });
        return static_cast<size_t>(activeParts)+boot.storedParts.size() <= boot.limits.totalParts && static_cast<size_t>(activeBuilds) <= boot.limits.builds;
    }
    bool history() {
        const auto& h = image.history;
        if (!h.generation.valid() || h.count > boot.limits.historyEntries || h.partCount > boot.limits.dormantParts
            || h.buildCount > boot.limits.dormantBuilds
            || historyPayloadBytes(h) > boot.limits.historyBytes
            || (!image.admission.open && (h.count != 0 || h.partCount != 0 || h.buildCount != 0))
            || !emptyTail(h.entries, h.count) || !emptyTail(h.parts, h.partCount) || !emptyTail(h.builds, h.buildCount)) return false;
        for (size_t i = 0; i < h.count; ++i)
            if (!historyEntry(h.entries[i]) || (i != 0 && h.entries[i - 1].id >= h.entries[i].id)) return false;
        for (size_t i = 0; i < h.partCount; ++i) {
            bool found = false;
            for (size_t j = 0; j < h.count; ++j) {
                const auto& e = h.entries[j].edit;
                found |= referencesPart(e,h.parts[i].id);
            }
            if (!found) return false;
        }
        for (size_t i = 0; i < h.buildCount; ++i) {
            bool found = false;
            for (size_t j = 0; j < h.count; ++j) found |= h.entries[j].edit.after->id == h.builds[i].id;
            if (!found) return false;
        }
        const auto acceptedParts = parts;
        const auto acceptedBuilds = builds;
        auto balance = boot.inventory;
        for (size_t i = h.applied; i > 0; --i) if (!historyStep(h.entries[i - 1], false, balance)) return false;
        for (size_t i = 0; i < h.count; ++i) {
            if (!historyStep(h.entries[i], true, balance)) return false;
            if (i + 1 == h.applied && (balance != boot.inventory || parts != acceptedParts
                || !std::equal(builds.begin(), builds.end(), acceptedBuilds.begin(), [](const auto& a, const auto& b) {
                    return a.header==b.header && a.connections.size()==b.connections.size()
                        && std::equal(a.connections.begin(),a.connections.end(),b.connections.begin(),construction::sameConnection);
                }))) return false;
        }
        parts = acceptedParts; builds = acceptedBuilds;
        return true;
    }
    bool validOutcome(const DecisionOutcome& value) const {
        if (value.revision > boot.revision || value.buildError > construction::BuildError::NonCanonicalOrder
            || (value.issueObject != DurableId{} && !validId(value.issueObject))) return false;
        if (value.state == ReceiptState::Rejected)
            return value.error > SessionError::None && value.error <= SessionError::DeliveryNotReady && !value.object && !value.buildRevision;
        return value.state == ReceiptState::Committed && value.error == SessionError::None
            && value.buildError == construction::BuildError::None && value.issueObject == DurableId{}
            && value.object && validId(*value.object);
    }
    bool requests() {
        if (!emptyTail(image.pending, image.pendingCount) || !emptyTail(image.receipts, image.receiptCount)) return false;
        for (size_t i = 0; i < image.pendingCount; ++i) {
            const auto& p = image.pending[i];
            if (p.command.epoch != boot.epoch || p.command.sequence.value() != image.admission.processedThrough.value() + i + 1
                || !p.admissionJournal.valid() || p.admissionJournal.value() > image.coveredThrough.value()
                || (i != 0 && image.pending[i - 1].admissionJournal >= p.admissionJournal)
                || !observedNext(p.command.sequence, p.observedExpectedSequence, image.admission.admittedThrough)
                || p.state > RecoveryPendingState::JournalAdmitted || p.error > SessionError::DeliveryNotReady
                || p.buildError > construction::BuildError::NonCanonicalOrder
                || (p.issueObject != DurableId{} && !validId(p.issueObject))) return false;
            const bool rejected = p.state == RecoveryPendingState::RejectReady || p.state == RecoveryPendingState::CancelReady;
            if ((!rejected && (p.error != SessionError::None || p.buildError != construction::BuildError::None
                    || p.issueObject != DurableId{}
                    || (p.state != RecoveryPendingState::JournalAdmitted && p.command.expectedRevision > boot.revision)))
                || (rejected && p.error == SessionError::None)
                || ((p.state == RecoveryPendingState::CancelReady) != (p.error == SessionError::Canceled))) return false;
            if (!rejected && p.state != RecoveryPendingState::JournalAdmitted
                && (std::holds_alternative<CreateBuild>(p.command.intent) || std::holds_alternative<AddPart>(p.command.intent))
                && !p.assignedObject) return false;
            if(!validAssignedRange(p.assignedRange,boot.world,image.allocator.issuedThrough)
                || (p.assignedRange.count&&!isRefitIntent(p.command.intent)))return false;
            for(uint32_t j=0;j<p.assignedRange.count;++j)
                if(!object({boot.world,p.assignedRange.first.counter+j}))return false;
            if (p.assignedObject && ((!std::holds_alternative<CreateBuild>(p.command.intent) && !std::holds_alternative<AddPart>(p.command.intent))
                    || !object(*p.assignedObject))) return false;
        }
        for (size_t i = 0; i < image.receiptCount; ++i) {
            const auto& r = image.receipts[i];
            if (r.command.epoch != boot.epoch || r.command.sequence.value() != image.admission.processedThrough.value() - image.receiptCount + i + 1
                || !validOutcome(r.outcome) || !outcomeMatches(r.command, r.outcome)
                || !r.decisionJournal.valid() || r.decisionJournal.value() > image.coveredThrough.value()
                || (i != 0 && image.receipts[i - 1].decisionJournal >= r.decisionJournal)
                || r.durability != ReceiptDurability::Volatile
                || !observedNext(r.command.sequence, r.observedExpectedSequence, image.admission.admittedThrough)) return false;
        }
        return true;
    }
    bool outcomeMatches(const Command& command, const DecisionOutcome& outcome) const {
        if (outcome.state != ReceiptState::Committed) return true;
        if (construction::next(command.expectedRevision) != outcome.revision || !outcome.object
            || *outcome.object == boot.caller.participant || *outcome.object == boot.caller.sessionToken) return false;
        return std::visit([&](const auto& intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, AcceptJob>) return outcome.object == intent.job && !outcome.buildRevision;
            else if constexpr (std::is_same_v<T, DeliverCargo>) return outcome.object == intent.cargo && !outcome.buildRevision;
            else if constexpr (std::is_same_v<T, CreateBuild>) return outcome.buildRevision == TopologyRevision{};
            else {
                if (!validId(intent.target.build) || construction::next(intent.target.expectedRevision) != outcome.buildRevision) return false;
                if constexpr (std::is_same_v<T, MovePart> || std::is_same_v<T, RemovePart>) return outcome.object == intent.part;
                if constexpr (std::is_same_v<T,CutWeld>)return outcome.object==intent.connection;
                if constexpr (std::is_same_v<T,RefitBuild>||std::is_same_v<T,RebuildStarter>)return outcome.object==intent.target.build;
                return true;
            }
        }, command.intent);
    }
    bool decisionMatches(const RequestDecisionRecord& value) const {
        if (!outcomeMatches(value.command, value.outcome)) return false;
        if (value.outcome.state != ReceiptState::Committed) return true;
        const auto* edit = std::get_if<BuildTransition>(&value.objects);
        if (const auto* job = std::get_if<AcceptJob>(&value.command.intent)) {
            const auto* changed = std::get_if<JobTransition>(&value.objects);
            return changed && changed->after.id == job->job && value.history.action == HistoryAction::None
                && value.balanceBefore == value.balanceAfter;
        }
        if(const auto* delivery=std::get_if<DeliverCargo>(&value.command.intent)) {
            const auto* change=std::get_if<CargoDeliveryTransition>(&value.objects);
            if(!change || change->cargo.id!=delivery->cargo || value.history.action!=HistoryAction::None) return false;
            const auto definition=std::find_if(boot.cargoDefinitions.begin(),boot.cargoDefinitions.end(),
                [&](const auto& d){return d.key==change->cargo.definition;});
            return definition!=boot.cargoDefinitions.end() && definition->value==change->reward;
        }
        if (!edit || !edit->after || edit->after->owner != boot.caller.participant) return false;
        if (edit->before && (edit->before->owner != edit->after->owner || edit->before->editLease != edit->after->editLease)) return false;
        if(const auto* cut=std::get_if<CutWeld>(&value.command.intent)) {
            return edit->cut&&edit->before&&edit->before->materialized&&edit->after->materialized
                &&!edit->refit&&!edit->storage&&!edit->beforePart&&!edit->afterPart&&edit->refitForward
                &&cut->target.build==edit->after->id&&cut->target.expectedRevision==edit->before->revision
                &&cut->connection==edit->cut->before.id&&value.outcome.object==cut->connection
                &&value.history.action==HistoryAction::Service&&!value.history.entry&&value.balanceBefore==value.balanceAfter;
        }
        if(edit->cut)return false;
        if(edit->refit) {
            if(!edit->before || !edit->before->materialized || !edit->after->materialized
                || edit->beforePart || edit->afterPart || value.outcome.object!=edit->after->id)return false;
            const bool undo=std::holds_alternative<Undo>(value.command.intent),redo=std::holds_alternative<Redo>(value.command.intent);
            const auto target=std::visit([](const auto& intent)->const BuildTarget* {
                if constexpr(requires{intent.target;})return &intent.target;else return nullptr;
            },value.command.intent);
            if(!target || target->build!=edit->after->id || target->expectedRevision!=edit->before->revision
                || edit->refitForward==undo || value.history.action!=(undo?HistoryAction::Undo:redo?HistoryAction::Redo:edit->storage?HistoryAction::Service:HistoryAction::Record))return false;
            if(!undo&&!redo) {
                if(!isRefitIntent(value.command.intent))return false;
                if(!edit->storage&&(!value.history.entry||value.history.entry->value()!=value.history.after.value()))return false;
                if(edit->storage&&value.history.entry)return false;
            } else if(undo?value.history.entry!=std::get<Undo>(value.command.intent).entry
                    || value.history.before!=std::get<Undo>(value.command.intent).expectedHistoryGeneration
                : value.history.entry!=std::get<Redo>(value.command.intent).entry
                    || value.history.before!=std::get<Redo>(value.command.intent).expectedHistoryGeneration)return false;
            ResourceAmounts debit{},credit{};
            if(!refitEconomy(*edit->refit,catalog_,debit,credit,edit->storage.get()))return false;
            auto balance=value.balanceBefore;
            return resources(balance,undo?credit:debit,undo?debit:credit)&&balance==value.balanceAfter;
        }
        if(isRefitIntent(value.command.intent))return false;
        const auto* from = edit->beforePart ? &*edit->beforePart : nullptr;
        const auto* to = edit->afterPart ? &*edit->afterPart : nullptr;
        if (value.outcome.object != (to ? to->id : from ? from->id : edit->after->id)) return false;
        const auto validateImage = [&](const construction::PartInstance& part) {
            BuildSnapshot draft{edit->after->id, edit->after->revision, edit->after->owner, edit->after->editLease, {part}, {}};
            construction::BuildIssue issue;
            return validId(part.id) && BuildModel::create(draft, catalog_, issue).has_value();
        };
        if ((from && !validateImage(*from)) || (to && !validateImage(*to))) return false;
        if (from && to) { auto same = *to; same.placement = from->placement; if (same != *from) return false; }
        const bool undo = std::holds_alternative<Undo>(value.command.intent);
        const bool redo = std::holds_alternative<Redo>(value.command.intent);
        bool matched = std::visit([&](const auto& intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, AcceptJob> || std::is_same_v<T, DeliverCargo>) return false;
            else if constexpr (std::is_same_v<T, CreateBuild>)
                return !edit->before && !from && !to && !edit->after->editLease;
            else {
                if (!edit->before || edit->before->id != intent.target.build || edit->before->revision != intent.target.expectedRevision) return false;
                if constexpr (std::is_same_v<T, AddPart>)
                    return !from && to && to->definition == intent.definition && to->placement == intent.placement;
                else if constexpr (std::is_same_v<T, MovePart>)
                    return from && to && from->id == intent.part && to->placement == intent.placement;
                else if constexpr (std::is_same_v<T, RemovePart>) return from && !to && from->id == intent.part;
                else if constexpr (std::is_same_v<T,RefitBuild>||std::is_same_v<T,RebuildStarter>||std::is_same_v<T,CutWeld>)return false;
                else return value.history.entry == intent.entry && value.history.before == intent.expectedHistoryGeneration;
            }
        }, value.command.intent);
        if (!matched || value.history.action != (undo ? HistoryAction::Undo : redo ? HistoryAction::Redo : HistoryAction::Record)) return false;
        if (!undo && !redo && (!value.history.entry || value.history.entry->value() != value.history.after.value())) return false;
        if ((from || to) && (!edit->before || !edit->before->materialized || !edit->after->materialized)) return false;
        if (!from && !to && edit->before) {
            if (undo ? (!edit->before->materialized || edit->after->materialized)
                     : (edit->before->materialized || !edit->after->materialized)) return false;
        }
        ResourceAmounts debit{}, credit{};
        if ((from == nullptr) != (to == nullptr)) {
            const auto& part = from ? *from : *to;
            const auto* definition = catalog_.lookup(part.definition).definition;
            if (!definition) return false;
            const bool purchase = (undo && from) || (!undo && to);
            if (purchase) {
                construction::PartInstance expected;
                expected.id = part.id; expected.definition = part.definition; expected.placement = part.placement;
                expected.owningBuild = part.owningBuild; expected.settings = construction::defaultModuleSettings(*definition);
                if (part != expected) return false;
                if (to) debit = definition->cost; else credit = definition->cost;
            } else if (part.provenance.origin != construction::PartOrigin::StarterLoan) {
                if (to) debit = definition->salvageYield; else credit = definition->salvageYield;
            }
        }
        auto balance = value.balanceBefore;
        return resources(balance, debit, credit) && balance == value.balanceAfter;
    }
    bool journal();
    bool reverseBuildJournal();
    const LogicalRecoveryCheckpoint& image;
    const SessionBootstrap& boot;
    ExpectedRecoveryIdentity expected_;
    const PartCatalog& catalog_;
    std::vector<DurableId> ids{};
    std::vector<WorkingPart> parts{};
    std::vector<WorkingBuild> builds{};
    std::vector<construction::Connection> historicalWelds{};
    bool capacity_ = false;
};

bool Validator::journal() {
    if (!emptyTail(image.retainedJournal, image.retainedJournalCount)) return false;
    std::optional<uint64_t> admitted{}, processed{}, issued{}, reserved{};
    std::optional<SessionRevision> revision{};
    std::optional<ResourceAmounts> balance{};
    std::optional<HistoryGeneration> generation{};
    SimulationTick previousTick{};
    const auto allocator = [&](AllocatorMarkers markers) {
        if (markers.issuedThrough > image.allocator.issuedThrough || (issued && markers.issuedThrough < *issued)
            || (reserved && markers.reservedThrough != *reserved)) return false;
        issued = markers.issuedThrough; reserved = markers.reservedThrough; return true;
    };
    for (size_t i = 0; i < image.retainedJournalCount; ++i) {
        const auto& record = image.retainedJournal[i];
        if (!validAssignedJournalRecord(record) || record.world != boot.world || record.epoch != boot.epoch
            || record.sequence.value() != image.modelReleasedThrough.value() + i + 1
            || record.tick < previousTick || record.tick > boot.tick) return false;
        previousTick = record.tick;
        const bool okay = std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AllocatorLeaseRecord>) {
                if ((reserved && value.reservedBefore != *reserved) || (issued && value.issuedThrough < *issued)
                    || value.issuedThrough > image.allocator.issuedThrough) return false;
                reserved = value.reservedAfter; issued = value.issuedThrough; return true;
            } else if constexpr (std::is_same_v<T, AdmissionOpenedRecord>) {
                return !image.origin && i == 0 && record.sequence.value() == 1 && value.caller == boot.caller
                    && value.generation == image.admission.generation
                    && value.retiredThrough.value() < image.admission.generation.value() && allocator(value.allocator);
            } else if constexpr (std::is_same_v<T, RecoveryOpenedRecord>) {
                if (i != 0 || record.sequence.value() != 1 || image.origin != value.origin
                    || value.admission.caller != boot.caller || value.admission.generation != image.admission.generation
                    || !allocator(value.admission.allocator)) return false;
                admitted = processed = 0;
                revision = value.revision; balance = value.balance; generation = value.origin.historyGeneration;
                return true;
            } else if constexpr (std::is_same_v<T, StarterKitRegisteredRecord>) {
                if(value.caller!=boot.caller||value.admission!=image.admission.generation
                    ||(admitted&&*admitted!=value.admittedThrough.value())||(processed&&*processed!=value.processedThrough.value())
                    ||(revision&&*revision!=value.beforeRevision)||(balance&&*balance!=value.balance)||!allocator(value.allocator))return false;
                const auto kit=std::find_if(boot.starterKits.begin(),boot.starterKits.end(),[&](const auto& k){return k.build==value.kit.build;});
                if(kit!=boot.starterKits.end()){
                    auto policy=*kit;policy.partIds=value.kit.partIds;policy.partCount=value.kit.partCount;
                    if(policy!=value.kit)return false;
                }
                else{
                    bool retired=false;
                    for(size_t later=i+1;later<image.retainedJournalCount;++later)
                        if(const auto* event=std::get_if<EntitlementRetiredRecord>(&image.retainedJournal[later].payload))
                            retired|=event->entitlement==value.kit.entitlement;
                    if(!retired)return false;
                }
                admitted=value.admittedThrough.value();processed=value.processedThrough.value();revision=value.afterRevision;balance=value.balance;
                return true;
            } else if constexpr (std::is_same_v<T, EntitlementRetiredRecord>) {
                if (value.caller != boot.caller || value.admission != image.admission.generation
                    || (admitted && *admitted != value.admittedThrough.value())
                    || (processed && *processed != value.processedThrough.value())
                    || (revision && *revision != value.beforeRevision)
                    || (balance && *balance != value.balance)
                    || (generation && *generation != value.history.before) || !allocator(value.allocator)
                    || !object(value.entitlement)) return false;
                for (size_t j = 0; j < value.history.partCount; ++j)
                    if (!object(value.history.retiredParts[j])) return false;
                for (size_t j = 0; j < value.history.buildCount; ++j)
                    if (!object(value.history.retiredBuilds[j])) return false;
                for (size_t j = 0; j < value.history.entryCount; ++j)
                    for (size_t k = 0; k < image.history.count; ++k)
                        if (value.history.invalidatedEntries[j] == image.history.entries[k].id) return false;
                admitted = value.admittedThrough.value(); processed = value.processedThrough.value();
                revision = value.afterRevision; balance = value.balance; generation = value.history.after;
                return true;
            } else if constexpr (std::is_same_v<T, AdmissionClosedRecord>) {
                if (image.admission.open || i + 1 != image.retainedJournalCount || value.caller != boot.caller
                    || value.generation != image.admission.generation || value.processedThrough != image.admission.processedThrough
                    || (processed && *processed != value.processedThrough.value())) return false;
                if (generation) generation = construction::next(*generation).value_or(*generation);
                return true;
            } else {
                const auto previousIssued=issued;
                if (value.key.caller != boot.caller || value.key.admissionGeneration != image.admission.generation
                    || value.key.sequence.value() > image.admission.admittedThrough.value() || !allocator(value.allocator)) return false;
                if constexpr (std::is_same_v<T, RequestAdmissionRecord>) {
                    if (value.assignedRange.count && previousIssued && value.assignedRange.first.counter<=*previousIssued)return false;
                    if ((admitted && *admitted != value.admittedBefore.value())
                        || (processed && *processed != value.processedThrough.value())) return false;
                    admitted = value.key.sequence.value(); processed = value.processedThrough.value();
                    for (size_t j = 0; j < image.pendingCount; ++j) if (image.pending[j].command.sequence == value.command.sequence) {
                        const auto& p = image.pending[j];
                        if (p.command != value.command || p.assignedObject != value.assignedObject || p.assignedRange!=value.assignedRange || p.admissionJournal != record.sequence) return false;
                    }
                    for (size_t j = 0; j < image.receiptCount; ++j)
                        if (image.receipts[j].command.sequence == value.command.sequence && image.receipts[j].command != value.command) return false;
                } else {
                    if ((processed && *processed != value.processedBefore.value())
                        || (admitted && value.key.sequence.value() > *admitted)
                        || value.key.sequence.value() > image.admission.processedThrough.value()
                        || (revision && *revision != value.beforeRevision) || (balance && *balance != value.balanceBefore)
                        || (generation && *generation != value.history.before) || !decisionMatches(value)) return false;
                    processed = value.key.sequence.value(); revision = value.afterRevision;
                    balance = value.balanceAfter; generation = value.history.after;
                    for (size_t j = 0; j < i; ++j) {
                        const auto* admission = std::get_if<RequestAdmissionRecord>(&image.retainedJournal[j].payload);
                        if (!admission || admission->key.sequence != value.key.sequence) continue;
                        if (admission->key != value.key || admission->command != value.command) return false;
                        if(value.outcome.state==ReceiptState::Committed && isRefitIntent(value.command.intent)) {
                            const auto* changed=std::get_if<BuildTransition>(&value.objects);
                            if(!changed || !changed->refit)return false;
                            const auto assigned=refitCreatedIds(*changed->refit,changed->storage.get());
                            if(admission->assignedRange!=AssignedIdRange{assigned.empty()?DurableId{}:assigned.front(),static_cast<uint32_t>(assigned.size())})return false;
                        }
                        if (value.outcome.state == ReceiptState::Committed
                            && (std::holds_alternative<CreateBuild>(value.command.intent)
                                || std::holds_alternative<AddPart>(value.command.intent))
                            && admission->assignedObject != value.outcome.object) return false;
                    }
                    for (size_t j = 0; j < image.receiptCount; ++j) if (image.receipts[j].command.sequence == value.command.sequence) {
                        const auto& r = image.receipts[j];
                        if (r.command != value.command || r.outcome != value.outcome || r.decisionJournal != record.sequence) return false;
                    }
                }
                return true;
            }
        }, record.payload);
        if (!okay) return false;
    }
    if ((admitted && *admitted != image.admission.admittedThrough.value())
        || (processed && *processed != image.admission.processedThrough.value())
        || (reserved && *reserved != image.allocator.reservedThrough)
        || (revision && *revision != boot.revision) || (balance && *balance != boot.inventory)
        || (generation && *generation != image.history.generation)) return false;
    // Each final covered object result must agree with the accepted image.
    // Earlier overwritten part/build images remain historical, not extra objects.
    std::vector<DurableId> checkedBuilds, checkedParts, checkedJobs;
    for (size_t i = image.retainedJournalCount; i > 0; --i) {
        const auto* decision = std::get_if<RequestDecisionRecord>(&image.retainedJournal[i - 1].payload);
        if (!decision || decision->outcome.state != ReceiptState::Committed) continue;
        if (const auto* edit = std::get_if<BuildTransition>(&decision->objects)) {
            if (std::find(checkedBuilds.begin(), checkedBuilds.end(), edit->after->id) == checkedBuilds.end()) {
                checkedBuilds.push_back(edit->after->id);
                const auto* build = findBuild(edit->after->id);
                if (edit->after->materialized ? (!build || build->header != *edit->after)
                    : (build && build->header != *edit->after)) return false;
            }
            if(edit->refit)visitPartChanges(*edit,[&](const auto& from,const auto& to){
                const auto id=from?from->id:to->id;
                if(std::find(checkedParts.begin(),checkedParts.end(),id)==checkedParts.end())checkedParts.push_back(id);
                return true;
            });
            const auto* changed = edit->afterPart ? &*edit->afterPart : edit->beforePart ? &*edit->beforePart : nullptr;
            if (changed && std::find(checkedParts.begin(), checkedParts.end(), changed->id) == checkedParts.end()) {
                checkedParts.push_back(changed->id);
                const auto* part = findPart(changed->id);
                if (edit->afterPart ? (!part || !part->active || part->value != *changed)
                    : (part && (part->active || part->value != *changed))) return false;
            }
        } else if(const auto* delivery=std::get_if<CargoDeliveryTransition>(&decision->objects)) {
            if(std::any_of(boot.cargo.begin(),boot.cargo.end(),[&](const auto& c){return c.id==delivery->cargo.id;})) return false;
            if(std::find(checkedJobs.begin(),checkedJobs.end(),delivery->after.id)==checkedJobs.end()) {
                checkedJobs.push_back(delivery->after.id);
                const auto found=std::find_if(boot.jobs.begin(),boot.jobs.end(),[&](const auto& j){return j.id==delivery->after.id;});
                if(found==boot.jobs.end() || *found!=delivery->after) return false;
            }
        } else if (const auto* job = std::get_if<JobTransition>(&decision->objects)) {
            if (std::find(checkedJobs.begin(), checkedJobs.end(), job->after.id) == checkedJobs.end()) {
                checkedJobs.push_back(job->after.id);
                const auto found = std::find_if(boot.jobs.begin(), boot.jobs.end(), [&](const auto& j) { return j.id == job->after.id; });
                if (found == boot.jobs.end() || *found != job->after) return false;
            }
        }
    }
    return reverseBuildJournal();
}

// Recover each exact predecessor from the accepted image, newest first. This
// validates even overwritten connected requests in a covered journal without
// storing whole-build before/after snapshots in every record.
bool Validator::reverseBuildJournal() {
    struct Working {BuildSnapshot snapshot;bool materialized=true;};
    std::vector<Working> working;
    auto stock=boot.storedParts;auto kits=boot.starterKits;
    std::vector<DurableId> laterRetired;
    for(const auto& build:boot.builds)working.push_back({build,true});
    for(size_t i=0;i<image.history.buildCount;++i) {
        const auto& header=image.history.builds[i];
        working.push_back({{header.id,header.revision,header.owner,header.editLease,{},{}},false});
    }
    for(size_t i=image.retainedJournalCount;i>0;--i) {
        const auto& payload=image.retainedJournal[i-1].payload;
        if(const auto* retired=std::get_if<EntitlementRetiredRecord>(&payload))laterRetired.push_back(retired->entitlement);
        if(const auto* registered=std::get_if<StarterKitRegisteredRecord>(&payload)) {
            const auto kit=std::find_if(kits.begin(),kits.end(),[&](const auto& k){return k.build==registered->kit.build;});
            if(kit!=kits.end()) {if(*kit!=registered->kit)return false;kits.erase(kit);}
            else if(std::find(laterRetired.begin(),laterRetired.end(),registered->kit.entitlement)==laterRetired.end())return false;
        }
        const auto* decision=std::get_if<RequestDecisionRecord>(&payload);
        if(!decision || decision->outcome.state!=ReceiptState::Committed)continue;
        const auto* edit=std::get_if<BuildTransition>(&decision->objects);if(!edit)continue;
        auto found=std::find_if(working.begin(),working.end(),[&](const auto& b){return b.snapshot.id==edit->after->id;});
        if(found==working.end()) {
            if(edit->after->materialized)return false;
            const auto& h=*edit->after;
            working.push_back({{h.id,h.revision,h.owner,h.editLease,{},{}},false});found=working.end()-1;
        }
        auto actual=headerOf(found->snapshot);actual.materialized=found->materialized;
        if(actual!=edit->after)return false;
        if(edit->cut) {
            const auto link=std::find_if(found->snapshot.connections.begin(),found->snapshot.connections.end(),
                [&](const auto& c){return c.id==edit->cut->before.id;});
            if(link==found->snapshot.connections.end()||!construction::sameConnection(*link,edit->cut->after()))return false;
            *link=edit->cut->before;
        } else if(edit->refit) {
            if(edit->refit->apply(found->snapshot,catalog_,!edit->refitForward))return false;
        } else if(edit->beforePart || edit->afterPart) {
            const auto id=edit->afterPart?edit->afterPart->id:edit->beforePart->id;
            auto& list=found->snapshot.parts;
            const auto part=std::find_if(list.begin(),list.end(),[&](const auto& p){return p.id==id;});
            if(edit->afterPart?(part==list.end()||*part!=*edit->afterPart):part!=list.end())return false;
            if(!edit->beforePart)list.erase(part);
            else if(part!=list.end())*part=*edit->beforePart;else list.push_back(*edit->beforePart);
            std::sort(list.begin(),list.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        }
        if(edit->before) {
            const auto& h=*edit->before;
            found->snapshot.revision=h.revision;found->snapshot.owner=h.owner;found->snapshot.editLease=h.editLease;
            found->materialized=h.materialized;
            if(const auto* cut=std::get_if<CutWeld>(&decision->command.intent))
                if(!cutMatches(found->snapshot,*cut,*edit,catalog_))return false;
            if(isRefitIntent(decision->command.intent)) {
                if(edit->storage) {
                    if(!transferStock(stock,*edit->storage,false))return false;
                    if(edit->storage->starterBefore) {
                        const auto& previous=*edit->storage->starterBefore;
                        const auto kit=std::find_if(kits.begin(),kits.end(),[&](const auto& k){return k.build==previous.build;});
                        const auto rebound=reboundKit(previous,refitCreatedIds(*edit->refit,edit->storage.get()));
                        if(kit!=kits.end()) {if(*kit!=rebound)return false;*kit=previous;}
                        else {
                            if(std::find(laterRetired.begin(),laterRetired.end(),previous.entitlement)==laterRetired.end())return false;
                            kits.push_back(previous);
                        }
                    }
                }
                if(!refitMatches(found->snapshot,decision->command.intent,*edit,catalog_,kits,stock))return false;
            }
        } else {
            if(!found->snapshot.parts.empty() || !found->snapshot.connections.empty())return false;
            working.erase(found);
        }
    }
    return true;
}

template<class T, size_t N>
void eraseRecord(std::array<T, N>& values, size_t& count, size_t index) {
    for (size_t i = index + 1; i < count; ++i) values[i - 1] = values[i];
    values[--count] = {};
}
void pruneHistory(RecoveryHistory& history) {
    for (size_t i = 0; i < history.partCount;) {
        bool referenced = false;
        for (size_t j = 0; j < history.count; ++j) {
            const auto& edit = history.entries[j].edit;
            referenced |= referencesPart(edit,history.parts[i].id);
        }
        if (referenced) ++i; else eraseRecord(history.parts, history.partCount, i);
    }
    for (size_t i = 0; i < history.buildCount;) {
        bool referenced = false;
        for (size_t j = 0; j < history.count; ++j)
            referenced |= history.entries[j].edit.after->id == history.builds[i].id;
        if (referenced) ++i; else eraseRecord(history.builds, history.buildCount, i);
    }
}
size_t historyBytes(const RecoveryHistory& h) {
    return historyPayloadBytes(h);
}
void measureOwned(LogicalRecoveryCheckpoint& image) {
    const auto& boot = image.accepted;
    image.ownedBytes = sizeof(LogicalRecoveryCheckpoint) + ownedExtraBytes(image) + boot.builds.size() * sizeof(BuildSnapshot)
        + boot.starterEntitlements.size() * sizeof(DurableId) + boot.cargoDefinitions.size() * sizeof(CargoDefinition)
        + boot.cargo.size() * sizeof(CargoRecord) + boot.jobs.size() * sizeof(JobRecord)
        +boot.starterKits.size()*sizeof(StarterKit)+boot.storedParts.size()*sizeof(construction::PartInstance);
    for (const auto& build : boot.builds)
        image.ownedBytes += build.parts.size() * sizeof(construction::PartInstance)
            + build.connections.size() * sizeof(construction::Connection);
}

// Applies typed deltas only. It never calls GameSession::submit/advanceOneTick,
// prepares a candidate backend or infers a successful edit from an intent.
class Replay {
public:
    Replay(LogicalRecoveryCheckpoint& image, const PartCatalog& catalog) : image_(image), catalog_(catalog) {}
    RecoveryIssue apply(const JournalRecord& record) {
        if (image_.coveredThrough.value() == maximum || record.sequence.value() != image_.coveredThrough.value() + 1)
            return {RecoveryError::InvalidCoverage};
        if (!validAssignedJournalRecord(record) || record.world != image_.accepted.world
            || record.epoch != image_.accepted.epoch || record.tick < image_.accepted.tick)
            return {RecoveryError::InvalidJournal};
        if (!image_.admission.open) return {RecoveryError::InvalidAdmission};
        if (!std::visit([&](const auto& value) { return applyPayload(value, record); }, record.payload))
            return {RecoveryError::InvalidJournal};
        image_.accepted.tick = record.tick;
        image_.accepted.lastIssuedId = image_.allocator.issuedThrough;
        // Only this private verified image compacts old covered detail. No live
        // writer is released and no filesystem/IndexedDB acknowledgment exists.
        if(journalRecordBytes(record)>image_.accepted.limits.journalBytes)return {RecoveryError::Capacity};
        while(image_.retainedJournalCount && (image_.retainedJournalCount>=image_.accepted.limits.journalRecords
            || retainedJournalBytes(image_)+journalRecordBytes(record)>image_.accepted.limits.journalBytes)) {
            eraseRecord(image_.retainedJournal, image_.retainedJournalCount, 0);
            image_.modelReleasedThrough = JournalFrontier{image_.modelReleasedThrough.value() + 1};
        }
        image_.retainedJournal[image_.retainedJournalCount++] = record;
        image_.coveredThrough = JournalFrontier{record.sequence.value()};
        measureOwned(image_);
        return Validator(image_, {image_.accepted.world, image_.content}, catalog_).validate();
    }
private:
    bool key(const RequestKey& k) const {
        return k.caller == image_.accepted.caller && k.epoch == image_.accepted.epoch
            && k.admissionGeneration == image_.admission.generation;
    }
    bool allocator(AllocatorMarkers value) {
        if (value.issuedThrough < image_.allocator.issuedThrough || value.reservedThrough != image_.allocator.reservedThrough)
            return false;
        image_.allocator = value;
        return true;
    }
    bool applyPayload(const AllocatorLeaseRecord& value, const JournalRecord&) {
        if (value.reservedBefore != image_.allocator.reservedThrough || value.issuedThrough < image_.allocator.issuedThrough)
            return false;
        image_.allocator = {value.issuedThrough, value.reservedAfter};
        return true;
    }
    bool applyPayload(const AdmissionOpenedRecord& value, const JournalRecord&) {
        return image_.coveredThrough.value() == 0 && image_.admission.admittedThrough.value() == 0
            && value.caller == image_.accepted.caller && value.generation == image_.admission.generation
            && value.retiredThrough == image_.admission.retiredThrough && allocator(value.allocator);
    }
    bool applyPayload(const RecoveryOpenedRecord&, const JournalRecord&) {
        // A recovery opening belongs to the new writer's initial checkpoint.
        // It cannot switch the identity of an already validated old writer.
        return false;
    }
    bool applyPayload(const StarterKitRegisteredRecord& value,const JournalRecord&) {
        auto& boot=image_.accepted;
        if(value.caller!=boot.caller||value.admission!=image_.admission.generation||value.beforeRevision!=boot.revision
            ||value.balance!=boot.inventory||value.admittedThrough!=image_.admission.admittedThrough
            ||value.processedThrough!=image_.admission.processedThrough||value.allocator!=image_.allocator
            ||image_.pendingCount||boot.starterKits.size()>=kMaximumStarterKits
            ||std::any_of(boot.starterKits.begin(),boot.starterKits.end(),[&](const auto& kit){return kit.build==value.kit.build;}))return false;
        boot.starterKits.push_back(value.kit);boot.revision=value.afterRevision;
        return !GameSession::validateInitialState(boot,catalog_);
    }
    bool applyPayload(const EntitlementRetiredRecord& value, const JournalRecord&) {
        auto& boot = image_.accepted;
        if (value.caller != boot.caller || value.admission != image_.admission.generation
            || value.beforeRevision != boot.revision || value.balance != boot.inventory
            || value.admittedThrough != image_.admission.admittedThrough
            || value.processedThrough != image_.admission.processedThrough
            || value.history.before != image_.history.generation || !allocator(value.allocator)) return false;
        const auto entitlement = std::find(boot.starterEntitlements.begin(), boot.starterEntitlements.end(), value.entitlement);
        if (entitlement == boot.starterEntitlements.end()) return false;
        for (const auto& build : boot.builds) for (const auto& part : build.parts)
            if (part.provenance.origin == construction::PartOrigin::StarterLoan
                && part.provenance.starterEntitlement == value.entitlement) return false;
        RecoveryHistory projected;
        EntitlementHistoryDelta delta;
        if (!detail::projectEntitlementRetirement(image_.history, value.entitlement, projected, delta)
            || delta != value.history) return false;
        boot.starterEntitlements.erase(entitlement);
        std::erase_if(boot.starterKits,[&](const auto& kit){return kit.entitlement==value.entitlement;});
        image_.history = projected;
        boot.revision = value.afterRevision;
        return true;
    }
    bool applyPayload(const AdmissionClosedRecord& value, const JournalRecord&) {
        if (value.caller != image_.accepted.caller || value.generation != image_.admission.generation
            || value.processedThrough != image_.admission.processedThrough || image_.pendingCount != 0) return false;
        image_.admission.open = false;
        image_.admission.retiredThrough = AdmissionFrontier{image_.admission.generation.value()};
        const auto generation = image_.history.generation;
        image_.history = {};
        image_.history.generation = construction::next(generation).value_or(generation);
        return true;
    }
    bool applyPayload(const RequestAdmissionRecord& value, const JournalRecord& record) {
        if (!key(value.key) || value.admittedBefore != image_.admission.admittedThrough
            || value.processedThrough != image_.admission.processedThrough
            || image_.pendingCount == image_.accepted.limits.pendingCommands) return false;
        // A newly assigned identity must come from counters not observed issued
        // at the checkpoint/prior record. Gaps can represent burned proposals.
        if ((value.assignedObject && value.assignedObject->counter <= image_.allocator.issuedThrough)
            || (value.assignedRange.count && value.assignedRange.first.counter<=image_.allocator.issuedThrough)) return false;
        if (!allocator(value.allocator)) return false;
        image_.admission.admittedThrough = RequestFrontier{value.key.sequence.value()};
        auto& p = image_.pending[image_.pendingCount++];
        p.command = value.command; p.state = RecoveryPendingState::JournalAdmitted;
        p.assignedObject = value.assignedObject; p.assignedRange=value.assignedRange; p.admissionJournal = record.sequence;
        p.observedExpectedSequence = construction::next(value.key.sequence).value_or(RequestSequence{});
        return true;
    }
    bool history(const RequestDecisionRecord& value, const BuildTransition& edit) {
        auto& h = image_.history;
        const auto& change = value.history;
        if (change.before != h.generation) return false;
        if(change.action==HistoryAction::Service) {
            if((!edit.storage&&!edit.cut)||change.entry)return false;
            size_t removed=0;
            for(size_t i=0;i<h.count;) {
                if(h.entries[i].edit.after->id!=edit.after->id){++i;continue;}
                if(removed==change.evictedCount||change.evicted[removed++]!=h.entries[i].id)return false;
                eraseRecord(h.entries,h.count,i);if(i<h.applied)--h.applied;
            }
            if(removed!=change.evictedCount)return false;
            pruneHistory(h);h.generation=change.after;return true;
        }
        const bool undo = change.action == HistoryAction::Undo;
        if (undo || change.action == HistoryAction::Redo) {
            if (change.evictedCount != 0 || (undo ? h.applied == 0 : h.applied == h.count)) return false;
            const auto& entry = h.entries[undo ? h.applied - 1 : h.applied];
            if (change.entry != entry.id || entry.edit.after->id != edit.after->id
                || !edit.before || edit.before->owner != entry.edit.after->owner
                || edit.before->editLease != entry.edit.after->editLease
                || edit.beforePart != (undo ? entry.edit.afterPart : entry.edit.beforePart)
                || edit.afterPart != (undo ? entry.edit.beforePart : entry.edit.afterPart)
                || bool(edit.refit)!=bool(entry.edit.refit)
                || (edit.refit && (*edit.refit!=*entry.edit.refit || edit.refitForward==undo))) return false;
            if (!entry.edit.before && (edit.before->materialized != undo || edit.after->materialized == undo)) return false;
            auto balance = value.balanceBefore;
            if (!resources(balance, undo ? entry.credit : entry.debit, undo ? entry.debit : entry.credit)
                || balance != value.balanceAfter) return false;
            h.applied = undo ? h.applied - 1 : h.applied + 1;
            h.generation = change.after;
            return true;
        }
        if (change.action != HistoryAction::Record || !change.entry) return false;
        size_t removed = 0;
        const auto eviction = [&](HistoryEntryId entry) {
            return removed < change.evictedCount && change.evicted[removed++] == entry;
        };
        while (h.count > h.applied) {
            if (!eviction(h.entries[h.count - 1].id)) return false;
            h.entries[--h.count] = {};
        }
        pruneHistory(h);
        const auto front = [&] {
            if (h.count == 0 || !eviction(h.entries[0].id)) return false;
            eraseRecord(h.entries, h.count, 0);
            if (h.applied != 0) --h.applied;
            pruneHistory(h); return true;
        };
        while (h.count >= image_.accepted.limits.historyEntries) if (!front()) return false;
        const auto before = value.balanceBefore, after = value.balanceAfter;
        const ResourceAmounts debit{before.salvageMaterial > after.salvageMaterial ? before.salvageMaterial - after.salvageMaterial : 0,
            before.specialMachinery > after.specialMachinery ? before.specialMachinery - after.specialMachinery : 0};
        const ResourceAmounts credit{after.salvageMaterial > before.salvageMaterial ? after.salvageMaterial - before.salvageMaterial : 0,
            after.specialMachinery > before.specialMachinery ? after.specialMachinery - before.specialMachinery : 0};
        h.entries[h.count++] = {*change.entry, value.key.caller, value.key.admissionGeneration, edit, debit, credit};
        h.applied = h.count;
        const size_t extra = removedPartCount(edit);
        while (historyBytes(h) + extra * sizeof(construction::PartInstance) > image_.accepted.limits.historyBytes
            || h.partCount + extra > image_.accepted.limits.dormantParts) {
            if (h.count <= 1 || !front()) return false;
        }
        h.generation = change.after;
        return removed == change.evictedCount;
    }
    bool build(const RequestDecisionRecord& value, const BuildTransition& edit, const RecoveryPending& pending,
               SimulationTick tick) {
        auto& all = image_.accepted.builds;
        auto active = std::find_if(all.begin(), all.end(), [&](const auto& b) { return b.id == edit.after->id; });
        auto& h = image_.history;
        size_t dormant = 0;
        while (dormant < h.buildCount && h.builds[dormant].id != edit.after->id) ++dormant;
        const std::optional<BuildHeader> before = active != all.end() ? std::optional{headerOf(*active)}
            : dormant < h.buildCount ? std::optional{h.builds[dormant]} : std::nullopt;
        if (before != edit.before || (before && before->editLease
                && (before->editLease->epoch != image_.accepted.epoch || tick >= before->editLease->expiresAfter))) return false;
        if (std::holds_alternative<CreateBuild>(value.command.intent) && pending.assignedObject != edit.after->id) return false;
        if (std::holds_alternative<AddPart>(value.command.intent)
            && (!edit.afterPart || pending.assignedObject != edit.afterPart->id)) return false;
        if(isRefitIntent(value.command.intent)) {
            if(active==all.end())return false;
            const auto plan=refitMatches(*active,value.command.intent,edit,catalog_,image_.accepted.starterKits,image_.accepted.storedParts);
            if(!plan)return false;
            const auto& ids=plan->createdIds;
            const AssignedIdRange expected{ids.empty()?DurableId{}:ids.front(),static_cast<uint32_t>(ids.size())};
            if(pending.assignedRange!=expected)return false;
            image_.accepted.storedParts=plan->storedPartsAfter;
            if(edit.storage&&edit.storage->starterBefore) {
                const auto kit=std::find_if(image_.accepted.starterKits.begin(),image_.accepted.starterKits.end(),
                    [&](const auto& k){return k.build==active->id;});
                if(kit==image_.accepted.starterKits.end())return false;
                *kit=reboundKit(*kit,ids);
            }
        }
        if(const auto* cut=std::get_if<CutWeld>(&value.command.intent)) {
            if(active==all.end()||pending.assignedObject||pending.assignedRange.count||!cutMatches(*active,*cut,edit,catalog_))return false;
        }
        if (!history(value, edit)) return false;
        // History eviction can change dormant-array positions; resolve again.
        dormant = 0;
        while (dormant < h.buildCount && h.builds[dormant].id != edit.after->id) ++dormant;
        if (!edit.before || !edit.before->materialized) {
            if (!edit.after->materialized || active != all.end() || edit.beforePart || edit.afterPart
                || all.size() >= image_.accepted.limits.builds) return false;
            if (edit.before) {
                if (dormant == h.buildCount) return false;
                eraseRecord(h.builds, h.buildCount, dormant);
            }
            all.push_back({edit.after->id, edit.after->revision, edit.after->owner, edit.after->editLease, {}, {}});
            std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
            return true;
        }
        if (active == all.end()) return false;
        if (!edit.after->materialized) {
            if (!active->parts.empty() || !active->connections.empty() || edit.beforePart || edit.afterPart
                || std::any_of(image_.accepted.starterKits.begin(),image_.accepted.starterKits.end(),[&](const auto& k){return k.build==active->id;})
                || std::any_of(image_.accepted.storedParts.begin(),image_.accepted.storedParts.end(),[&](const auto& p){return p.owningBuild==active->id;})
                || h.buildCount >= image_.accepted.limits.dormantBuilds) return false;
            h.builds[h.buildCount++] = *edit.after;
            all.erase(active); return true;
        }
        if(edit.cut) {
            const auto link=std::find_if(active->connections.begin(),active->connections.end(),[&](const auto& c){return c.id==edit.cut->before.id;});
            if(link==active->connections.end()||!construction::sameConnection(*link,edit.cut->before))return false;
            *link=edit.cut->after();active->revision=edit.after->revision;return true;
        }
        if(edit.refit) {
            if(edit.refit->apply(*active,catalog_,edit.refitForward))return false;
            size_t activeParts=0;for(const auto& build:all)activeParts+=build.parts.size();
            if(activeParts+image_.accepted.storedParts.size()>image_.accepted.limits.totalParts)return false;
            if(edit.storage){active->revision=edit.after->revision;return true;}
            const bool record=value.history.action==HistoryAction::Record;
            if(!visitPartChanges(edit,[&](const auto& from,const auto& to) {
                const auto id=from?from->id:to->id;
                size_t retained=0;while(retained<h.partCount&&h.parts[retained].id!=id)++retained;
                if(from||record)return retained==h.partCount;
                return retained<h.partCount&&h.parts[retained]==*to;
            }))return false;
            if(!record)visitPartChanges(edit,[&](const auto& from,const auto& to) {
                if(!from)for(size_t i=0;i<h.partCount;++i)if(h.parts[i].id==to->id) {
                    eraseRecord(h.parts,h.partCount,i);break;
                }
                return true;
            });
            if(h.partCount+removedPartCount(edit)>image_.accepted.limits.dormantParts)return false;
            visitPartChanges(edit,[&](const auto& from,const auto& to){if(from&&!to)h.parts[h.partCount++]=*from;return true;});
            if(historyBytes(h)>image_.accepted.limits.historyBytes)return false;
            active->revision=edit.after->revision;return true;
        }
        if (!edit.beforePart && !edit.afterPart) return false;
        const auto id = edit.beforePart ? edit.beforePart->id : edit.afterPart->id;
        auto part = std::find_if(active->parts.begin(), active->parts.end(), [id](const auto& p) { return p.id == id; });
        size_t retained = 0;
        while (retained < h.partCount && h.parts[retained].id != id) ++retained;
        if (edit.beforePart) {
            if (part == active->parts.end() || *part != *edit.beforePart || retained != h.partCount) return false;
            if (edit.afterPart) *part = *edit.afterPart;
            else {
                if (h.partCount >= image_.accepted.limits.dormantParts) return false;
                h.parts[h.partCount++] = *edit.beforePart;
                active->parts.erase(part);
            }
        } else {
            if (part != active->parts.end() || active->parts.size() >= construction::kMaximumBuildParts) return false;
            size_t activeParts = 0;
            for (const auto& existing : all) activeParts += existing.parts.size();
            if (activeParts+image_.accepted.storedParts.size() >= image_.accepted.limits.totalParts) return false;
            if (value.history.action == HistoryAction::Record) {
                if (retained != h.partCount) return false;
            } else {
                if (retained == h.partCount || h.parts[retained] != *edit.afterPart) return false;
                eraseRecord(h.parts, h.partCount, retained);
            }
            active->parts.push_back(*edit.afterPart);
            std::sort(active->parts.begin(), active->parts.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        }
        active->revision = edit.after->revision;
        return true;
    }
    bool applyPayload(const RequestDecisionRecord& value, const JournalRecord& record) {
        if (!key(value.key) || image_.pendingCount == 0 || value.processedBefore != image_.admission.processedThrough
            || value.beforeRevision != image_.accepted.revision || value.balanceBefore != image_.accepted.inventory
            || value.history.before != image_.history.generation || value.command != image_.pending[0].command
            || !allocator(value.allocator)) return false;
        const auto pending = image_.pending[0];
        if (pending.state == RecoveryPendingState::RejectReady || pending.state == RecoveryPendingState::CancelReady) {
            if (value.outcome.state != ReceiptState::Rejected || value.outcome.error != pending.error
                || value.outcome.buildError != pending.buildError || value.outcome.issueObject != pending.issueObject) return false;
        }
        if (value.outcome.state == ReceiptState::Committed) {
            if (value.command.expectedRevision != image_.accepted.revision) return false;
            if (const auto* edit = std::get_if<BuildTransition>(&value.objects)) {
                if ((!image_.accepted.workshopEnabled&&!std::holds_alternative<CutWeld>(value.command.intent))
                    || !build(value, *edit, pending, record.tick)) return false;
            } else if(const auto* delivery=std::get_if<CargoDeliveryTransition>(&value.objects)) {
                auto& cargo=image_.accepted.cargo;auto& jobs=image_.accepted.jobs;
                const auto load=std::find_if(cargo.begin(),cargo.end(),[&](const auto& c){return c.id==delivery->cargo.id;});
                const auto job=std::find_if(jobs.begin(),jobs.end(),[&](const auto& j){return j.id==delivery->before.id;});
                if(load==cargo.end() || *load!=delivery->cargo || job==jobs.end() || *job!=delivery->before) return false;
                cargo.erase(load);*job=delivery->after;
            } else if (const auto* job = std::get_if<JobTransition>(&value.objects)) {
                auto& jobs = image_.accepted.jobs;
                const auto found = std::find_if(jobs.begin(), jobs.end(), [&](const auto& j) { return j.id == job->before.id; });
                if (found == jobs.end() || *found != job->before) return false;
                *found = job->after;
            } else return false;
        }
        image_.accepted.revision = value.afterRevision;
        image_.accepted.inventory = value.balanceAfter;
        image_.admission.processedThrough = RequestFrontier{value.key.sequence.value()};
        eraseRecord(image_.pending, image_.pendingCount, 0);
        while(image_.receiptCount && (image_.receiptCount>=image_.accepted.limits.receipts
            || retainedReceiptBytes(image_)+ownedExtraBytes(value.command)>kMaximumReceiptOwnedBytes))
            eraseRecord(image_.receipts,image_.receiptCount,0);
        const auto admitted = image_.admission.admittedThrough.value();
        image_.receipts[image_.receiptCount++] = {value.command, value.outcome,
            admitted == maximum ? RequestSequence{} : RequestSequence{admitted + 1}, record.sequence, ReceiptDurability::Volatile};
        return true;
    }
    LogicalRecoveryCheckpoint& image_;
    const PartCatalog& catalog_;
};

} // namespace

namespace detail {
bool projectEntitlementRetirement(const RecoveryHistory& before, DurableId entitlement,
    RecoveryHistory& after, EntitlementHistoryDelta& delta) noexcept {
    const auto generation = construction::next(before.generation);
    if (&before == &after || !generation || !construction::isValid(entitlement)
        || before.count > before.entries.size() || before.applied > before.count
        || before.partCount > before.parts.size() || before.buildCount > before.builds.size()) return false;
    after = before; delta = {};
    delta.before = before.generation; delta.after = *generation;
    const auto loan = [entitlement](const auto& part) {
        return part && part->provenance.origin == construction::PartOrigin::StarterLoan
            && part->provenance.starterEntitlement == entitlement;
    };
    for (size_t i = 0; i < after.count;) {
        const auto& entry = after.entries[i];
        bool containsLoan=!visitPartChanges(entry.edit,[&](const auto& from,const auto& to){return !loan(from)&&!loan(to);});
        for(size_t j=0;j<before.partCount;++j) {
            const auto& part=before.parts[j];
            containsLoan|=part.provenance.origin==construction::PartOrigin::StarterLoan
                && part.provenance.starterEntitlement==entitlement && referencesPart(entry.edit,part.id);
        }
        if(!containsLoan){++i;continue;}
        delta.invalidatedEntries[delta.entryCount++] = entry.id;
        if (i < after.applied) --after.applied;
        eraseRecord(after.entries, after.count, i);
    }
    pruneHistory(after);
    for (size_t i = 0; i < before.partCount; ++i) {
        bool kept = false;
        for (size_t j = 0; j < after.partCount; ++j) kept |= before.parts[i].id == after.parts[j].id;
        if (!kept) delta.retiredParts[delta.partCount++] = before.parts[i].id;
    }
    for (size_t i = 0; i < before.buildCount; ++i) {
        bool kept = false;
        for (size_t j = 0; j < after.buildCount; ++j) kept |= before.builds[i].id == after.builds[j].id;
        if (!kept) delta.retiredBuilds[delta.buildCount++] = before.builds[i].id;
    }
    std::sort(delta.retiredParts.begin(), delta.retiredParts.begin() + delta.partCount);
    std::sort(delta.retiredBuilds.begin(), delta.retiredBuilds.begin() + delta.buildCount);
    after.generation = *generation;
    return true;
}
} // namespace detail

std::unique_ptr<ValidatedRecoveryCheckpoint> SessionRecovery::admit(
    const LogicalRecoveryCheckpoint& input, ExpectedRecoveryIdentity expected, const PartCatalog& catalog, RecoveryIssue& issue) {
    try {
        issue = Validator(input, expected, catalog).validate();
        if (issue) return nullptr;
        auto owned = std::make_unique<LogicalRecoveryCheckpoint>(input);
        return std::unique_ptr<ValidatedRecoveryCheckpoint>(new ValidatedRecoveryCheckpoint(std::move(owned), catalog));
    } catch (const std::bad_alloc&) { issue = {RecoveryError::Capacity}; return nullptr; }
}

std::unique_ptr<ValidatedRecoveryCheckpoint> SessionRecovery::replay(
    const ValidatedRecoveryCheckpoint& base, JournalIdentity identity,
    std::span<const JournalRecord> records, RecoveryIssue& issue) {
    if (identity != base.snapshot().journalIdentity) { issue = {RecoveryError::IdentityMismatch}; return nullptr; }
    if (records.size() > kMaximumJournalRecords) { issue = {RecoveryError::Capacity}; return nullptr; }
    try {
        auto candidate = std::make_unique<LogicalRecoveryCheckpoint>(base.snapshot());
        Replay replay(*candidate, base.catalog_);
        for (size_t i = 0; i < records.size(); ++i) {
            issue = replay.apply(records[i]);
            if (issue) { issue.record = i; return nullptr; }
        }
        issue = {};
        return std::unique_ptr<ValidatedRecoveryCheckpoint>(new ValidatedRecoveryCheckpoint(std::move(candidate), base.catalog_));
    } catch (const std::bad_alloc&) { issue = {RecoveryError::Capacity}; return nullptr; }
}

std::unique_ptr<RecoveredGameSession> SessionRecovery::restore(
    std::unique_ptr<ValidatedRecoveryCheckpoint>& input, EventStreamIncarnation incarnation,
    PreparationAdapter& adapter, RecoveryIssue& issue) {
    if (!input) { issue = {RecoveryError::MissingCheckpoint}; return nullptr; }
    const auto& source = input->snapshot();
    const auto epoch = construction::next(source.accepted.epoch);
    const auto generation = construction::next(source.admission.generation);
    const auto writer = construction::next(source.journalIdentity.writerGeneration);
    const size_t retirementCount = source.admission.open ? source.pendingCount + 1 : 0;
    if (!epoch || !generation || !writer || source.allocator.reservedThrough == maximum
        || retirementCount > maximum - source.coveredThrough.value()) {
        issue = {RecoveryError::CounterExhausted}; return nullptr;
    }
    if (!isValid(EventStreamIdentity{source.accepted.world, *epoch, incarnation})) {
        issue = {RecoveryError::IdentityMismatch}; return nullptr;
    }
    try {
        auto result = std::make_unique<RecoveredGameSession>();
        // All unfinished preparations terminate in the old identity domain.
        // Preserve a known rejection; otherwise cancel without preparing again.
        for (size_t i = 0; i < source.pendingCount; ++i) {
            const auto& pending = source.pending[i];
            RequestDecisionRecord decision;
            decision.key = {source.accepted.caller, source.accepted.epoch, source.admission.generation, pending.command.sequence};
            decision.command = pending.command;
            decision.beforeRevision = decision.afterRevision = source.accepted.revision;
            decision.processedBefore = RequestFrontier{source.admission.processedThrough.value() + i};
            decision.balanceBefore = decision.balanceAfter = source.accepted.inventory;
            decision.history.before = decision.history.after = source.history.generation;
            decision.allocator = source.allocator;
            decision.outcome.revision = source.accepted.revision;
            decision.outcome.error = SessionError::Canceled;
            if (pending.state == RecoveryPendingState::RejectReady || pending.state == RecoveryPendingState::CancelReady) {
                decision.outcome.error = pending.error;
                decision.outcome.buildError = pending.buildError;
                decision.outcome.issueObject = pending.issueObject;
            }
            result->retirementRecords[result->retirementRecordCount++] = {
                kLogicalJournalSchema, source.accepted.world, source.accepted.epoch,
                JournalSequence{source.coveredThrough.value() + i + 1}, source.accepted.tick, decision};
        }
        if (source.admission.open) {
            result->retirementRecords[result->retirementRecordCount++] = {
                kLogicalJournalSchema, source.accepted.world, source.accepted.epoch,
                JournalSequence{source.coveredThrough.value() + retirementCount}, source.accepted.tick,
                AdmissionClosedRecord{source.accepted.caller, source.admission.generation, source.admission.admittedThrough}};
        }
        result->retired = replay(*input, source.journalIdentity,
            std::span(result->retirementRecords).first(result->retirementRecordCount), issue);
        if (!result->retired) return nullptr;
        const auto& retired = result->retired->snapshot();
        const RecoveryOrigin origin{retired.journalIdentity, retired.coveredThrough, retired.accepted.caller,
            retired.admission.generation, retired.admission.processedThrough, retired.allocator, retired.history.generation};
        auto fresh = std::make_unique<LogicalRecoveryCheckpoint>();
        fresh->content = retired.content;
        auto recoveredBoot = retired.accepted;
        fresh->accepted = std::move(recoveredBoot);
        fresh->accepted.epoch = *epoch;
        // Skip every unused/burned ID in the old reservation. A new block is
        // represented in the opening record, including this new token's ID.
        construction::IdAllocator tokens(retired.accepted.world, retired.allocator.reservedThrough);
        const auto token = tokens.allocate();
        if (!token) { issue = {RecoveryError::CounterExhausted}; return nullptr; }
        const auto horizon = retired.allocator.reservedThrough
            + std::min(uint64_t{64}, maximum - retired.allocator.reservedThrough);
        fresh->accepted.caller.sessionToken = *token;
        fresh->accepted.lastIssuedId = token->counter;
        fresh->allocator = {token->counter, horizon};
        fresh->admission = {*generation, AdmissionFrontier{retired.admission.generation.value()}, {}, {}, true};
        fresh->origin = origin;
        fresh->history = retired.history; // Closed history is empty; its generation never rewinds.
        // Profile 1 has one admitted owner. Revoke its old epoch edit permissions
        // explicitly. Geometry and topology/session revisions are unchanged;
        // epoch+token retirement invalidates every old edit precondition.
        for (auto& build : fresh->accepted.builds) build.editLease.reset();
        fresh->journalIdentity = {retired.accepted.world, *epoch, *writer};
        fresh->retainedJournalCount = 1;
        fresh->coveredThrough = JournalFrontier{1};
        fresh->retainedJournal[0] = {kLogicalJournalSchema, fresh->accepted.world, *epoch, JournalSequence{1},
            fresh->accepted.tick, RecoveryOpenedRecord{origin,
                {fresh->accepted.caller, *generation, fresh->admission.retiredThrough, fresh->allocator},
                fresh->accepted.revision, fresh->accepted.inventory}};
        measureOwned(*fresh);
        result->initial = admit(*fresh, {fresh->accepted.world, fresh->content}, input->catalog_, issue);
        if (!result->initial) return nullptr;
        result->session = instantiate(result->initial->snapshot(), incarnation, input->catalog_, adapter, issue);
        if (!result->session) return nullptr;
        input.reset(); // One validated capability is consumed only on success.
        issue = {};
        return result;
    } catch (const std::bad_alloc&) { issue = {RecoveryError::Capacity}; return nullptr; }
}

} // namespace voxy::game::expedition
