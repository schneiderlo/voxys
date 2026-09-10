#include "game/construction/build_refit.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace voxy::game::construction {
namespace {
BuildIssue fail(BuildError error,std::string_view field,DurableId object={}) {return {error,object,field};}
bool add(ResourceAmounts& total,ResourceAmounts value) {
    if(value.salvageMaterial>std::numeric_limits<uint64_t>::max()-total.salvageMaterial
        || value.specialMachinery>std::numeric_limits<uint64_t>::max()-total.specialMachinery)return false;
    total.salvageMaterial+=value.salvageMaterial;total.specialMachinery+=value.specialMachinery;return true;
}
bool strengthEqual(StrengthLimits a,StrengthLimits b) noexcept {
    return a.tensionNewtons==b.tensionNewtons && a.shearNewtons==b.shearNewtons
        && a.bendingNewtonMetres==b.bendingNewtonMetres && a.torsionNewtonMetres==b.torsionNewtonMetres;
}
void normalizeEndpoints(Connection& c) {if(c.b<c.a)std::swap(c.a,c.b);}
}
bool sameConnection(const Connection& a,const Connection& b) noexcept {
    return a.id==b.id && a.a==b.a && a.b==b.b && a.kind==b.kind && a.enabled==b.enabled && a.damage==b.damage
        && strengthEqual(a.strength,b.strength) && a.minimumLengthMillimetres==b.minimumLengthMillimetres
        && a.maximumLengthMillimetres==b.maximumLengthMillimetres && a.restLengthMillimetres==b.restLengthMillimetres;
}
bool RefitWeldChange::operator==(const RefitWeldChange& other) const noexcept {
    return before.has_value()==other.before.has_value() && after.has_value()==other.after.has_value()
        && (!before || sameConnection(*before,*other.before)) && (!after || sameConnection(*after,*other.after));
}
size_t BuildRefitRequest::retainedBytes() const noexcept {
    return sizeof(*this)+parts_.capacity()*sizeof(RefitPart)+welds_.capacity()*sizeof(RefitWeld);
}
std::shared_ptr<const BuildRefitRequest> BuildRefitRequest::create(
    std::span<const RefitPart> parts,std::span<const RefitWeld> welds,BuildIssue& issue) {
    const auto reject=[&](BuildError error,std::string_view field)->std::shared_ptr<const BuildRefitRequest>{issue=fail(error,field);return {};};
    if(parts.size()>kMaximumBuildParts || welds.size()>kMaximumBuildConnections
        || sizeof(BuildRefitRequest)+parts.size()*sizeof(RefitPart)+welds.size()*sizeof(RefitWeld)>kMaximumRefitRequestBytes)
        return reject(BuildError::Capacity,"refit.requestBytes");
    std::vector<RefitPart> sortedParts(parts.begin(),parts.end());
    std::vector<RefitWeld> sortedWelds(welds.begin(),welds.end());
    std::sort(sortedParts.begin(),sortedParts.end(),[](const auto& a,const auto& b){return a.design.ordinal<b.design.ordinal;});
    for(size_t i=0;i<sortedParts.size();++i) {
        const auto& p=sortedParts[i];
        if(p.design.ordinal!=i+1 || (p.source!=DurableId{}&&!isValid(p.source)))return reject(BuildError::InvalidId,"refit.part");
        for(size_t j=0;j<i;++j)if(p.source!=DurableId{} && p.source==sortedParts[j].source)
            return reject(BuildError::DuplicateId,"refit.source");
    }
    for(auto& weld:sortedWelds) {
        if(weld.b<weld.a)std::swap(weld.a,weld.b);
        if(!weld.a.partOrdinal || weld.a.partOrdinal>parts.size() || !weld.b.partOrdinal || weld.b.partOrdinal>parts.size()
            || weld.a.partOrdinal==weld.b.partOrdinal || !weld.a.socket.valid() || !weld.b.socket.valid())
            return reject(BuildError::InvalidConnection,"refit.weld");
    }
    std::sort(sortedWelds.begin(),sortedWelds.end());
    if(std::adjacent_find(sortedWelds.begin(),sortedWelds.end())!=sortedWelds.end())return reject(BuildError::DuplicateConnection,"refit.weld");
    auto result=std::shared_ptr<const BuildRefitRequest>(new BuildRefitRequest(std::move(sortedParts),std::move(sortedWelds)));
    if(result->retainedBytes()>kMaximumRefitRequestBytes)return reject(BuildError::Capacity,"refit.requestStorage");
    issue={};return result;
}
size_t BuildRefitDelta::retainedBytes() const noexcept {
    return sizeof(*this)+parts_.capacity()*sizeof(RefitPartChange)+welds_.capacity()*sizeof(RefitWeldChange);
}
bool BuildRefitDelta::operator==(const BuildRefitDelta& other) const noexcept {return build_==other.build_ && parts_==other.parts_ && welds_==other.welds_;}
std::shared_ptr<const BuildRefitDelta> BuildRefitDelta::between(const BuildSnapshot& before,const BuildSnapshot& after,BuildIssue& issue) {
    const auto reject=[&](BuildError error,std::string_view field)->std::shared_ptr<const BuildRefitDelta>{issue=fail(error,field);return {};};
    if(before.id!=after.id || before.owner!=after.owner || before.editLease!=after.editLease)
        return reject(BuildError::ImmutableIdentity,"refit.header");
    if(before.parts.size()>kMaximumBuildParts || after.parts.size()>kMaximumBuildParts
        || before.connections.size()>kMaximumBuildConnections || after.connections.size()>kMaximumBuildConnections)
        return reject(BuildError::Capacity,"refit.deltaCount");
    const auto canonicalIds=[](const auto& values) {
        for(size_t i=0;i<values.size();++i)
            if(!isValid(values[i].id) || (i && !(values[i-1].id<values[i].id)))return false;
        return true;
    };
    if(!isValid(before.id) || !canonicalIds(before.parts) || !canonicalIds(after.parts)
        || !canonicalIds(before.connections) || !canonicalIds(after.connections))
        return reject(BuildError::NonCanonicalOrder,"refit.deltaIds");
    size_t partCount=0,weldCount=0;
    for(const auto& part:before.parts) {
        const auto found=std::find_if(after.parts.begin(),after.parts.end(),[&](const auto& p){return p.id==part.id;});
        partCount+=found==after.parts.end() || *found!=part;
    }
    for(const auto& part:after.parts)partCount+=std::none_of(before.parts.begin(),before.parts.end(),[&](const auto& p){return p.id==part.id;});
    for(const auto& weld:before.connections) {
        const auto found=std::find_if(after.connections.begin(),after.connections.end(),[&](const auto& c){return c.id==weld.id;});
        weldCount+=found==after.connections.end() || !sameConnection(*found,weld);
    }
    for(const auto& weld:after.connections)weldCount+=std::none_of(before.connections.begin(),before.connections.end(),[&](const auto& c){return c.id==weld.id;});
    if(sizeof(BuildRefitDelta)+partCount*sizeof(RefitPartChange)+weldCount*sizeof(RefitWeldChange)>kMaximumRefitDeltaBytes)
        return reject(BuildError::Capacity,"refit.deltaBytes");
    std::vector<RefitPartChange> parts;parts.reserve(partCount);
    std::vector<RefitWeldChange> welds;welds.reserve(weldCount);
    for(const auto& p:before.parts) {
        const auto found=std::find_if(after.parts.begin(),after.parts.end(),[&](const auto& next){return next.id==p.id;});
        if(found==after.parts.end())parts.push_back({p,{}});
        else if(p!=*found) {
            auto immutable=*found;immutable.placement=p.placement;immutable.settings=p.settings;immutable.paint=p.paint;
            if(immutable!=p)return reject(BuildError::ImmutableIdentity,"refit.partIdentity");
            parts.push_back({p,*found});
        }
    }
    for(const auto& p:after.parts)if(std::none_of(before.parts.begin(),before.parts.end(),[&](const auto& old){return old.id==p.id;}))parts.push_back({{},p});
    for(const auto& c:before.connections) {
        const auto found=std::find_if(after.connections.begin(),after.connections.end(),[&](const auto& next){return next.id==c.id;});
        if(found==after.connections.end())welds.push_back({c,{}});
        else if(!sameConnection(c,*found))return reject(BuildError::ImmutableIdentity,"refit.weldIdentity");
    }
    for(const auto& c:after.connections)if(std::none_of(before.connections.begin(),before.connections.end(),[&](const auto& old){return old.id==c.id;}))welds.push_back({{},c});
    const auto id=[](const auto& change){return change.before?change.before->id:change.after->id;};
    std::sort(parts.begin(),parts.end(),[&](const auto& a,const auto& b){return id(a)<id(b);});
    std::sort(welds.begin(),welds.end(),[&](const auto& a,const auto& b){return id(a)<id(b);});
    if(sizeof(BuildRefitDelta)+parts.size()*sizeof(RefitPartChange)+welds.size()*sizeof(RefitWeldChange)>kMaximumRefitDeltaBytes)
        return reject(BuildError::Capacity,"refit.deltaBytes");
    // Tight capacities keep retained journal/history accounting proportional to the edit.
    auto result=std::shared_ptr<const BuildRefitDelta>(new BuildRefitDelta(before.id,
        std::move(parts),std::move(welds)));
    if(result->retainedBytes()>kMaximumRefitDeltaBytes)return reject(BuildError::Capacity,"refit.deltaStorage");
    issue={};return result;
}
BuildIssue BuildRefitDelta::apply(BuildSnapshot& target,const PartCatalog& catalog,bool forward) const {
    if(target.id!=build_)return fail(BuildError::ImmutableIdentity,"refit.target",target.id);
    if(target.parts.size()>kMaximumBuildParts || target.connections.size()>kMaximumBuildConnections)
        return fail(BuildError::Capacity,"refit.targetCount",target.id);
    auto candidate=target;
    for(const auto& change:parts_) {
        const auto& from=forward?change.before:change.after;
        const auto& to=forward?change.after:change.before;
        const auto id=from?from->id:to->id;
        auto part=std::find_if(candidate.parts.begin(),candidate.parts.end(),[&](const auto& p){return p.id==id;});
        if(from ? part==candidate.parts.end()||*part!=*from : part!=candidate.parts.end())return fail(BuildError::StaleRevision,"refit.partPredecessor",id);
        if(!to)candidate.parts.erase(part);else if(part==candidate.parts.end())candidate.parts.push_back(*to);else *part=*to;
    }
    for(const auto& change:welds_) {
        const auto& from=forward?change.before:change.after;
        const auto& to=forward?change.after:change.before;
        const auto id=from?from->id:to->id;
        auto weld=std::find_if(candidate.connections.begin(),candidate.connections.end(),[&](const auto& c){return c.id==id;});
        if(from ? weld==candidate.connections.end()||!sameConnection(*weld,*from) : weld!=candidate.connections.end())return fail(BuildError::StaleRevision,"refit.weldPredecessor",id);
        if(!to)candidate.connections.erase(weld);else if(weld==candidate.connections.end())candidate.connections.push_back(*to);else *weld=*to;
    }
    BuildIssue issue;const auto model=BuildModel::create(candidate,catalog,issue);if(!model)return issue;
    target=model->snapshot();return {};
}
std::optional<BuildRefitPlan> prepareBuildRefit(const BuildModel& current,const BuildRefitRequest& request,
    const PartCatalog& catalog,const std::function<std::optional<DurableId>()>& allocateId,BuildIssue& issue,
    RefitOwnership ownership) {
    const auto& source=current.snapshot();
    const auto reject=[&](BuildError error,std::string_view field,DurableId object=DurableId{})->std::optional<BuildRefitPlan>{issue=fail(error,field,object);return std::nullopt;};
    if(!allocateId)return reject(BuildError::InvalidId,"refit.allocator");
    if(ownership.storedParts.size()>kMaximumStoredParts)return reject(BuildError::Capacity,"refit.storedParts");
    if(ownership.starterEntitlement){
        if(!isValid(*ownership.starterEntitlement)||ownership.starterEntitlement->world!=source.id.world
            ||*ownership.starterEntitlement==source.id||*ownership.starterEntitlement==source.owner)
            return reject(BuildError::InvalidProvenance,"refit.starterEntitlement");
        if(std::any_of(request.parts().begin(),request.parts().end(),[](const auto& p){return p.source!=DurableId{};}))
            return reject(BuildError::InvalidProvenance,"refit.starterRecipeSource");
        if(std::any_of(source.parts.begin(),source.parts.end(),[&](const auto& p){
            return p.provenance.origin==PartOrigin::StarterLoan&&p.provenance.starterEntitlement!=*ownership.starterEntitlement;}))
            return reject(BuildError::InvalidProvenance,"refit.foreignStarterEntitlement");
        const auto paid=static_cast<size_t>(std::count_if(source.parts.begin(),source.parts.end(),[](const auto& p){return p.provenance.origin==PartOrigin::Paid;}));
        if(ownership.storedParts.size()+paid>kMaximumStoredParts)return reject(BuildError::Capacity,"refit.storedParts");
    }
    if(std::any_of(source.connections.begin(),source.connections.end(),[](const auto& c){return c.kind!=ConnectionKind::Weld;}))
        return reject(BuildError::InvalidConnection,"refit.weldOnly");
    BuildRefitPlan result;result.after=source;result.after.parts.clear();result.after.connections.clear();
    std::vector<DurableId> ordinals;
    std::set<DurableId> existingIds{source.id,source.owner};
    for(const auto& p:source.parts){existingIds.insert(p.id);if(p.provenance.origin==PartOrigin::StarterLoan)existingIds.insert(p.provenance.starterEntitlement);}
    for(const auto& c:source.connections)existingIds.insert(c.id);
    if(ownership.starterEntitlement){
        if(std::any_of(source.parts.begin(),source.parts.end(),[&](const auto& p){return p.id==*ownership.starterEntitlement;})
            ||std::any_of(source.connections.begin(),source.connections.end(),[&](const auto& c){return c.id==*ownership.starterEntitlement;}))
            return reject(BuildError::DuplicateId,"refit.starterEntitlement");
        existingIds.insert(*ownership.starterEntitlement);
    }
    for(const auto& part:ownership.storedParts){
        if(part.provenance!=PartProvenance{}||part.owningBuild.world!=source.id.world)
            return reject(BuildError::InvalidProvenance,"refit.storedProvenance",part.id);
        if(!existingIds.insert(part.id).second)return reject(BuildError::DuplicateId,"refit.storedIdentity",part.id);
        BuildSnapshot stock;stock.id=part.owningBuild;stock.owner=source.owner;stock.parts={part};
        if(!BuildModel::create(stock,catalog,issue))return std::nullopt;
        result.storedPartsAfter.push_back(part);
    }
    const auto fresh=[&]()->std::optional<DurableId>{
        const auto id=allocateId();
        if(!id || !isValid(*id) || id->world!=source.id.world || !existingIds.insert(*id).second)return std::nullopt;
        result.createdIds.push_back(*id);return id;
    };
    for(const auto& requested:request.parts()) {
        const auto& design=requested.design;const auto lookup=catalog.lookup(design.definition);
        if(!lookup.definition)return reject(BuildError::UnknownDefinition,"refit.definition");
        PartInstance part;
        if(requested.source!=DurableId{}) {
            const auto found=std::find_if(source.parts.begin(),source.parts.end(),[&](const auto& p){return p.id==requested.source;});
            if(found!=source.parts.end())part=*found;
            else{
                const auto stored=std::find_if(result.storedPartsAfter.begin(),result.storedPartsAfter.end(),[&](const auto& p){
                    return p.id==requested.source&&p.owningBuild==source.id;});
                if(stored==result.storedPartsAfter.end())return reject(BuildError::UnknownPart,"refit.source",requested.source);
                part=*stored;result.storedPartsAfter.erase(stored);result.consumedStoredParts.push_back(part.id);
            }
            if(part.definition!=design.definition)return reject(BuildError::ImmutableIdentity,"refit.definition",requested.source);
        } else {
            const auto id=fresh();if(!id)return reject(BuildError::InvalidId,"refit.newPartId");
            part.id=*id;part.definition=design.definition;part.owningBuild=source.id;
            if(ownership.starterEntitlement)part.provenance={PartOrigin::StarterLoan,*ownership.starterEntitlement};
            else if(!add(result.debit,lookup.definition->cost))return reject(BuildError::Capacity,"refit.costOverflow");
        }
        part.placement=design.placement;part.settings=design.settings;part.paint=design.paint;
        ordinals.push_back(part.id);result.after.parts.push_back(part);
    }
    for(const auto& part:source.parts)if(std::none_of(result.after.parts.begin(),result.after.parts.end(),[&](const auto& p){return p.id==part.id;})) {
        const auto lookup=catalog.lookup(part.definition);if(!lookup.definition)return reject(BuildError::UnknownDefinition,"refit.removedDefinition");
        if(ownership.starterEntitlement&&part.provenance.origin==PartOrigin::Paid)result.storedPartsAfter.push_back(part);
        else if(part.provenance.origin!=PartOrigin::StarterLoan && !add(result.credit,lookup.definition->salvageYield))
            return reject(BuildError::Capacity,"refit.refundOverflow");
    }
    for(const auto& requested:request.welds()) {
        Connection weld;weld.a={ordinals[requested.a.partOrdinal-1],requested.a.socket};weld.b={ordinals[requested.b.partOrdinal-1],requested.b.socket};
        normalizeEndpoints(weld);
        const auto found=std::find_if(source.connections.begin(),source.connections.end(),[&](auto c){normalizeEndpoints(c);return c.a==weld.a&&c.b==weld.b;});
        if(found!=source.connections.end())weld=*found;
        else {
            const auto& pa=result.after.parts[requested.a.partOrdinal-1];const auto& pb=result.after.parts[requested.b.partOrdinal-1];
            const auto* a=findSocket(*catalog.lookup(pa.definition).definition,requested.a.socket);
            const auto* b=findSocket(*catalog.lookup(pb.definition).definition,requested.b.socket);
            if(!a || !b || matchSockets(*a,*b,ConnectionKind::Weld)!=SocketMatchError::None)return reject(BuildError::IncompatibleSocket,"refit.weldSocket");
            const auto id=fresh();if(!id)return reject(BuildError::InvalidId,"refit.newWeldId");
            weld.id=*id;weld.strength={std::min(a->strength.tensionNewtons,b->strength.tensionNewtons),
                std::min(a->strength.shearNewtons,b->strength.shearNewtons),std::min(a->strength.bendingNewtonMetres,b->strength.bendingNewtonMetres),
                std::min(a->strength.torsionNewtonMetres,b->strength.torsionNewtonMetres)};
        }
        result.after.connections.push_back(weld);
    }
    // Validate simultaneously; no disconnected intermediate part move is published.
    auto changed=current;issue=changed.replace(source.revision,result.after,catalog);if(issue)return std::nullopt;
    result.after=changed.snapshot();
    result.delta=BuildRefitDelta::between(source,result.after,issue);if(!result.delta)return std::nullopt;
    std::sort(result.storedPartsAfter.begin(),result.storedPartsAfter.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
} // namespace voxy::game::construction
