#include "game/expedition/cove_build.hpp"
#include <algorithm>
#include <limits>

namespace voxy::game::expedition {
using namespace construction;
std::optional<CoveBuildSeed> prepareCoveBuild(const assets::LoadedAssetFixture& scene,
    DurableId owner,uint64_t issuedThrough,std::string& error) {
    const auto fail=[&](const char* why)->std::optional<CoveBuildSeed>{error=why;return std::nullopt;};
    if(!isValid(owner) || owner.counter>issuedThrough)return fail("Invalid starter build owner");
    auto catalog=makeCovePartCatalog(scene,error);if(!catalog)return std::nullopt;
    const auto source=CoveBoatAssembly::compile(scene,error);if(!source)return std::nullopt;
    auto build=source->build();
    const auto oldParts=build.parts;
    IdAllocator ids(owner.world,issuedThrough);
    const auto entitlement=ids.allocate(),buildId=ids.allocate();
    if(!entitlement || !buildId)return fail("Starter build identity space exhausted");
    build.id=*buildId;build.owner=owner;build.editLease.reset();build.revision={};
    std::vector<CoveBoatAssembly::Part> placements;
    placements.reserve(source->parts().size());
    for(auto& part:build.parts) {
        const auto id=ids.allocate();if(!id)return fail("Starter part identity space exhausted");
        const auto mapping=std::find_if(source->parts().begin(),source->parts().end(),[&](const auto& p){return p.id==part.id;});
        if(mapping==source->parts().end())return fail("Starter part has no scene mapping");
        part.id=*id;part.owningBuild=*buildId;
        part.provenance={PartOrigin::StarterLoan,*entitlement};
        placements.push_back({mapping->placement,*id});
    }
    const auto remap=[&](DurableId old)->std::optional<DurableId>{
        for(size_t i=0;i<oldParts.size();++i)if(oldParts[i].id==old)return build.parts[i].id;
        return std::nullopt;
    };
    for(auto& weld:build.connections) {
        const auto id=ids.allocate(),a=remap(weld.a.part),b=remap(weld.b.part);
        if(!id)return fail("Starter weld identity space exhausted");
        if(!a || !b)return fail("Starter weld has no physical endpoint");
        weld.id=*id;weld.a.part=*a;weld.b.part=*b;
    }
    BuildIssue issue;const auto admitted=BuildModel::create(build,*catalog,issue);
    if(!admitted){error="Starter build validation: "+std::string(issue.field);return std::nullopt;}
    build=admitted->snapshot();error.clear();
    return CoveBuildSeed{std::move(*catalog),std::move(build),std::move(placements),*entitlement,ids.lastIssued()};
}
std::optional<StarterKit> prepareCoveStarterKit(const BuildSnapshot& build,DurableId entitlement,std::string& error) {
    // Starter grants have their own stable save-format bounds. Paid brick
    // capacity must not widen this trusted entitlement recipe or its ID array.
    if(build.parts.empty()||build.parts.size()>StarterKit{}.partIds.size()||build.connections.size()>64||!isValid(entitlement)) {
        error="Invalid installed starter recipe";return {};
    }
    StarterKit kit;kit.build=build.id;kit.entitlement=entitlement;kit.partCount=static_cast<uint8_t>(build.parts.size());
    std::vector<RefitPart> parts;std::vector<RefitWeld> welds;
    for(size_t i=0;i<build.parts.size();++i) {
        const auto& p=build.parts[i];kit.partIds[i]=p.id;
        parts.push_back({{}, {static_cast<uint32_t>(i+1),p.definition,p.placement,p.paint,p.settings}});
    }
    const auto ordinal=[&](DurableId id) {
        return static_cast<uint32_t>(std::find_if(build.parts.begin(),build.parts.end(),[&](const auto& p){return p.id==id;})-build.parts.begin()+1);
    };
    for(const auto& w:build.connections)welds.push_back({{ordinal(w.a.part),w.a.socket},{ordinal(w.b.part),w.b.socket}});
    BuildIssue issue;kit.design=BuildRefitRequest::create(parts,welds,issue);
    if(!kit.design){error="Invalid installed starter recipe: "+std::string(issue.field);return {};}
    error.clear();return kit;
}
std::optional<std::vector<CoveBoatAssembly::Part>> bindCoveStarterKit(const StarterKit& installed,const StarterKit* current,
    std::span<const CoveBoatAssembly::Part> originalBindings,std::string& error) {
    if(!current){error.clear();return std::vector<CoveBoatAssembly::Part>(originalBindings.begin(),originalBindings.end());}
    auto policy=*current;policy.partIds=installed.partIds;
    if(policy!=installed||current->partCount!=originalBindings.size()) {
        error="Saved starter grant does not match the installed recipe";return {};
    }
    std::vector<CoveBoatAssembly::Part> result;
    for(size_t i=0;i<installed.partCount;++i) {
        const auto source=std::find_if(originalBindings.begin(),originalBindings.end(),[&](const auto& b){return b.id==installed.partIds[i];});
        if(source==originalBindings.end()){error="Installed starter part has no scene binding";return {};}
        result.push_back({source->placement,current->partIds[i]});
    }
    error.clear();return result;
}
std::optional<CoveRefitRequest> prepareCoveRefit(const assets::LoadedAssetFixture& design,
    const CoveBoatAssembly& owned,const PartCatalog& catalog,std::string& error,std::span<const uint32_t> reservedSlots,std::span<const PartInstance> storedParts) {
    const auto reject=[&](const char* why)->std::optional<CoveRefitRequest>{error=why;return std::nullopt;};
    // Uses the same full compiler as design feedback, before authority/backend
    // admission. Transient inspection IDs are only read to recover ordinals.
    const auto compiled=CoveBoatAssembly::compile(design,error);if(!compiled)return std::nullopt;
    CoveRefitRequest result;std::vector<RefitPart> parts;std::vector<RefitWeld> welds;
    for(const auto& part:compiled->build().parts) {
        const auto placed=std::find_if(compiled->parts().begin(),compiled->parts().end(),[&](const auto& p){return p.id==part.id;});
        if(placed==compiled->parts().end())return reject("Design part has no scene placement");
        const auto binding=std::find_if(owned.parts().begin(),owned.parts().end(),[&](const auto& p){return p.placement==placed->placement;});
        if(binding==owned.parts().end()) {
            if(std::find(reservedSlots.begin(),reservedSlots.end(),placed->placement)!=reservedSlots.end())
                return reject("Use Rebuild starter to restore removed loaned parts.");
            const auto stored=std::find_if(storedParts.begin(),storedParts.end(),[&](const auto& p){
                return p.owningBuild==owned.build().id&&p.definition==part.definition
                    &&std::none_of(parts.begin(),parts.end(),[&](const auto& requested){return requested.source==p.id;});
            });
            const auto source=stored==storedParts.end()?DurableId{}:stored->id;
            parts.push_back({source, {static_cast<uint32_t>(parts.size()+1),part.definition,part.placement,part.paint,part.settings}});
            result.placements.push_back({placed->placement,source}); // Assigned only by authority at Launch.
        } else {
            const auto current=std::find_if(owned.build().parts.begin(),owned.build().parts.end(),[&](const auto& p){return p.id==binding->id;});
            if(current==owned.build().parts.end() || current->definition!=part.definition || !catalog.lookup(part.definition).definition)
                return reject("Design does not match an owned boat part");
            parts.push_back({current->id,{static_cast<uint32_t>(parts.size()+1),part.definition,part.placement,design.registry.placements[placed->placement].paint.value_or(current->paint),design.registry.placements[placed->placement].settings.value_or(current->settings)}});
            result.placements.push_back({placed->placement,current->id});
        }
    }
    const auto ordinal=[&](DurableId id) {
        const auto& source=compiled->build().parts;
        return static_cast<uint32_t>(std::find_if(source.begin(),source.end(),[&](const auto& p){return p.id==id;})-source.begin()+1);
    };
    for(const auto& weld:compiled->build().connections)
        welds.push_back({{ordinal(weld.a.part),weld.a.socket},{ordinal(weld.b.part),weld.b.socket}});
    BuildIssue issue;result.design=BuildRefitRequest::create(parts,welds,issue);
    if(!result.design){error="Invalid connected design: "+std::string(issue.field);return std::nullopt;}
    error.clear();return result;
}
std::optional<CoveLaunchDesign> prepareCoveLaunchDesign(const assets::LoadedAssetFixture& installed,
    const BuildSnapshot& build,std::span<const CoveBoatAssembly::Part> bindings,std::string& error) {
    return prepareCoveExpandedLaunchDesign(installed,installed,build,bindings,bindings,error);
}
std::optional<CoveLaunchDesign> prepareCoveExpandedLaunchDesign(const assets::LoadedAssetFixture& original,
    const assets::LoadedAssetFixture& current,const BuildSnapshot& build,
    std::span<const CoveBoatAssembly::Part> originalBindings,std::span<const CoveBoatAssembly::Part> currentBindings,std::string& error,
    CoveSceneTopology topology) {
    const auto reject=[&](const char* why)->std::optional<CoveLaunchDesign>{error=why;return std::nullopt;};
    if(!original.registry.navigation || !current.registry.navigation || current.registry.placements.size()>assets::kMaximumFixturePlacements
        || current.registry.placements.size()<original.registry.placements.size() || originalBindings.size()>assets::kMaximumFixturePlacements
        || currentBindings.size()>assets::kMaximumFixturePlacements || build.parts.size()>assets::kMaximumFixturePlacements || build.connections.size()>assets::kMaximumFixtureConnections)
        return reject("Launch design exceeds the starter craft capacity");
    CoveLaunchDesign result{current,{}};auto& registry=result.scene.registry;
    const auto fixedCount=original.registry.placements.size();
    const auto& originalSlots=original.registry.navigation->boatPlacements;
    const auto boatSlot=[&](uint32_t slot){return slot>=fixedCount || std::find(originalSlots.begin(),originalSlots.end(),slot)!=originalSlots.end();};
    registry.navigation=original.registry.navigation;registry.navigation->boatPlacements.clear();
    std::erase_if(registry.connections,[&](const auto& c){return boatSlot(c.aPlacement)||boatSlot(c.bPlacement);});
    std::array<bool,assets::kMaximumFixturePlacements> occupied{};
    std::fill_n(occupied.begin(),fixedCount,true);
    const auto existing=[&](DurableId id)->std::optional<uint32_t> {
        for(const auto& binding:originalBindings)if(binding.id==id)return binding.placement;
        for(const auto& binding:currentBindings)if(binding.id==id)return binding.placement;
        return std::nullopt;
    };
    // Reserve retained slots before assigning any restored/new ID. Older undo
    // IDs can sort before retained paid parts and must not take their slots.
    std::array<bool,assets::kMaximumFixturePlacements> mapped{};
    for(const auto& part:build.parts)if(const auto slot=existing(part.id)) {
        if(*slot>=registry.placements.size() || !boatSlot(*slot) || mapped[*slot])return reject("Invalid retained part slot");
        mapped[*slot]=true;occupied[*slot]=true;
    }
    for(const auto& part:build.parts) {
        auto slot=existing(part.id);
        if(!slot) {
            if(part.provenance.origin!=PartOrigin::Paid)return reject("A starter loan has no original scene binding");
            const auto free=std::find(occupied.begin()+static_cast<std::ptrdiff_t>(fixedCount),occupied.end(),false);
            if(free==occupied.end())return reject("This scene has no room for another part. Remove a paid part to make room.");
            slot=static_cast<uint32_t>(free-occupied.begin());occupied[*slot]=true;
            assets::FixturePartPlacement source;
            bool found=false;
            for(size_t i=0;i<original.bundles.size();++i)
                if(original.bundles[i]->sidecar().part.key==part.definition){
                    source.bundleIndex=static_cast<uint32_t>(i);found=true;break;
                }
            if(!found)for(size_t i=0;i<original.prototypes.size();++i)
                if(original.prototypes[i].key==part.definition){
                    source.bundleIndex=static_cast<uint32_t>(i);source.prototype=true;found=true;break;
                }
            if(!found)return reject("Added part has no admitted asset in this cove");
            if(*slot==registry.placements.size())registry.placements.push_back(source);
            else registry.placements[*slot]=source;
        }
        auto& placed=registry.placements[*slot];
        const auto& definition=placed.prototype?original.prototypes.at(placed.bundleIndex):original.bundles.at(placed.bundleIndex)->sidecar().part;
        if(definition.key!=part.definition)return reject("An owned part does not match its installed asset");
        placed.placement=part.placement;placed.settings=part.settings;placed.paint=part.paint;registry.navigation->boatPlacements.push_back(*slot);result.placements.push_back({*slot,part.id});
        if(std::holds_alternative<HelmModule>(definition.module)) {
            const auto source=std::find_if(original.registry.placements.begin(),original.registry.placements.end(),[&](const auto& p){
                const auto& d=p.prototype?original.prototypes.at(p.bundleIndex):original.bundles.at(p.bundleIndex)->sidecar().part;
                return d.key==part.definition;
            });
            if(source==original.registry.placements.end())return reject("Added helm has no authored standing frame");
            const auto oldInverse=inverse(source->placement);if(!oldInverse)return reject("Invalid helm standing frame");
            const auto moved=compose(part.placement,*oldInverse);if(!moved)return reject("Invalid helm placement");
            const auto matrix=rotationMatrix(moved->rotation);if(!matrix)return reject("Invalid helm rotation");
            const auto& rotation=matrix->elements;const auto old=original.registry.navigation->helmStanding;
            registry.navigation->helmStanding={
                rotation[0]*old.x+rotation[1]*old.y+rotation[2]*old.z+double(moved->translation.x)*.02,
                rotation[3]*old.x+rotation[4]*old.y+rotation[5]*old.z+double(moved->translation.y)*.02,
                rotation[6]*old.x+rotation[7]*old.y+rotation[8]*old.z+double(moved->translation.z)*.02};
        }
    }
    for(const auto& weld:build.connections) {
        const auto a=std::find_if(result.placements.begin(),result.placements.end(),[&](const auto& p){return p.id==weld.a.part;});
        const auto b=std::find_if(result.placements.begin(),result.placements.end(),[&](const auto& p){return p.id==weld.b.part;});
        if(a==result.placements.end()||b==result.placements.end()||weld.kind!=ConnectionKind::Weld)
            return reject("Launch requires intact welded connections");
        if(!weld.enabled) {
            if(topology!=CoveSceneTopology::AcceptedRoots)return reject("Launch requires intact welded connections");
            // The accepted build retains cut bonds. The scene includes only
            // enabled welds; loading must never repair the saved topology.
            continue;
        }
        registry.connections.push_back({a->placement,b->placement,weld.a.socket,weld.b.socket});
    }
    error.clear();return result;
}
bool CoveDesignCost::affordable(ResourceAmounts stock) const noexcept {
    const auto fit=[](uint64_t available,uint64_t charge,uint64_t refund) {
        return charge>=refund?available>=charge-refund:refund-charge<=std::numeric_limits<uint64_t>::max()-available;
    };
    return fit(stock.salvageMaterial,debit.salvageMaterial,credit.salvageMaterial)
        && fit(stock.specialMachinery,debit.specialMachinery,credit.specialMachinery);
}
std::optional<CoveDesignCost> quoteCoveDesign(const assets::LoadedAssetFixture& scene,
    const CoveBoatAssembly& owned,const PartCatalog& catalog,std::span<const PartInstance> storedParts) noexcept {
    if(!scene.registry.navigation)return std::nullopt;
    CoveDesignCost result;
    const auto add=[](ResourceAmounts& total,ResourceAmounts value) {
        if(value.salvageMaterial>std::numeric_limits<uint64_t>::max()-total.salvageMaterial
            || value.specialMachinery>std::numeric_limits<uint64_t>::max()-total.specialMachinery)return false;
        total.salvageMaterial+=value.salvageMaterial;total.specialMachinery+=value.specialMachinery;return true;
    };
    const auto& members=scene.registry.navigation->boatPlacements;
    std::array<DurableId,assets::kMaximumFixturePlacements> used{};size_t usedCount=0;
    for(const auto slot:members) {
        if(slot>=scene.registry.placements.size())return std::nullopt;
        const auto& p=scene.registry.placements[slot];
        if(p.prototype?p.bundleIndex>=scene.prototypes.size():p.bundleIndex>=scene.bundles.size() || !scene.bundles[p.bundleIndex])return std::nullopt;
        const auto& definition=p.prototype?scene.prototypes[p.bundleIndex]:scene.bundles[p.bundleIndex]->sidecar().part;
        const auto binding=std::find_if(owned.parts().begin(),owned.parts().end(),[&](const auto& b){return b.placement==slot;});
        if(binding==owned.parts().end()) {
            const auto stored=std::find_if(storedParts.begin(),storedParts.end(),[&](const auto& part){
                return part.owningBuild==owned.build().id&&part.definition==definition.key
                    &&std::find(used.begin(),used.begin()+static_cast<std::ptrdiff_t>(usedCount),part.id)==used.begin()+static_cast<std::ptrdiff_t>(usedCount);
            });
            if(stored!=storedParts.end()){if(usedCount==used.size())return std::nullopt;used[usedCount++]=stored->id;}
            else if(!add(result.debit,definition.cost))return std::nullopt;
        }
        else {
            const auto part=std::find_if(owned.build().parts.begin(),owned.build().parts.end(),[&](const auto& b){return b.id==binding->id;});
            if(part==owned.build().parts.end()||part->definition!=definition.key)return std::nullopt;
        }
    }
    for(const auto& binding:owned.parts())if(std::find(members.begin(),members.end(),binding.placement)==members.end()) {
        const auto part=std::find_if(owned.build().parts.begin(),owned.build().parts.end(),[&](const auto& p){return p.id==binding.id;});
        if(part==owned.build().parts.end())return std::nullopt;
        const auto definition=catalog.lookup(part->definition);
        if(!definition)return std::nullopt;
        if(part->provenance.origin!=PartOrigin::StarterLoan && !add(result.credit,definition.definition->salvageYield))return std::nullopt;
    }
    return result;
}
const PartInstance* availableCoveStoredPart(const assets::LoadedAssetFixture& scene,const CoveBoatAssembly& owned,
    std::span<const PartInstance> stock,ContentKey definition) noexcept {
    if(!scene.registry.navigation)return nullptr;
    size_t assigned=0;
    for(const auto slot:scene.registry.navigation->boatPlacements) {
        if(std::any_of(owned.parts().begin(),owned.parts().end(),[&](const auto& p){return p.placement==slot;}))continue;
        if(slot>=scene.registry.placements.size())return nullptr;
        const auto& placed=scene.registry.placements[slot];
        const auto& part=placed.prototype?scene.prototypes.at(placed.bundleIndex):scene.bundles.at(placed.bundleIndex)->sidecar().part;
        if(part.key==definition)++assigned;
    }
    for(const auto& part:stock)if(part.owningBuild==owned.build().id&&part.definition==definition) {
        if(!assigned)return &part;
        --assigned;
    }
    return nullptr;
}
} // namespace voxy::game::expedition
