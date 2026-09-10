#include "game/expedition/cove_workshop.hpp"
#include "game/expedition/cove_player.hpp"
#include <algorithm>
#include <limits>
#include <tuple>

namespace voxy::game::expedition {
using namespace construction;
namespace {
// Shared build format: socket +Y faces outward; mating reverses +Y and +Z.
constexpr CubeRotation mating{2};
bool member(const assets::AssetFixtureRegistry& r,uint32_t p) {
    const auto& parts=r.navigation->boatPlacements;
    return std::find(parts.begin(),parts.end(),p)!=parts.end();
}
}
std::unique_ptr<CoveWorkshop> CoveWorkshop::create(const assets::LoadedAssetFixture& scene,std::string& error,uint32_t fixedPlacements,
    std::span<const uint32_t> originalBoatSlots,bool acceptedSeparated) {
    if(!scene.registry.navigation || scene.registry.placements.size()>32 || scene.registry.connections.size()>64) {
        error="Workshop needs a bounded starter craft";return {};
    }
    auto result=std::unique_ptr<CoveWorkshop>(new CoveWorkshop(scene));
    result->fixedPlacements_=fixedPlacements?fixedPlacements:static_cast<uint32_t>(scene.registry.placements.size());
    if(result->fixedPlacements_>scene.registry.placements.size()){error="Invalid installed scene slots";return {};}
    result->acceptedSlots_=scene.registry.navigation->boatPlacements;
    result->collisionBoatSlots_=originalBoatSlots.empty()?result->acceptedSlots_
        :std::vector<uint32_t>(originalBoatSlots.begin(),originalBoatSlots.end());
    for(uint32_t i=result->fixedPlacements_;i<32;++i)result->collisionBoatSlots_.push_back(i);
    result->compiled_=acceptedSeparated?CoveBoatAssembly::compileSeparatedScene(scene,error):CoveBoatAssembly::compile(scene,error);
    if(!result->compiled_)return {};
    for(size_t i=0;i<scene.bundles.size();++i) {
        // The authored cargo bundles are recoverable mission objects, not
        // structural/module choices in the starter's parts drawer.
        if(std::holds_alternative<TowEyeModule>(scene.bundles[i]->sidecar().part.module))continue;
        result->catalog_.push_back({static_cast<uint32_t>(i),{},false});
    }
    result->selected_=scene.registry.navigation->boatPlacements.front();
    for(auto p:scene.registry.navigation->boatPlacements)
        if(std::holds_alternative<WinchModule>(result->definition(p).module))result->selected_=p;
    result->message_=result->valid()?"Select a part. Snap finds a matching socket."
        :"This build has separate sections. Load its protected design or rebuild the starter.";
    error.clear();return result;
}
const PartDefinition& CoveWorkshop::definition(uint32_t p) const {
    return definition(preview_,p);
}
const PartDefinition& CoveWorkshop::definition(const assets::LoadedAssetFixture& scene,uint32_t p) const {
    const auto& place=scene.registry.placements[p];
    return place.prototype?scene.prototypes[place.bundleIndex]:scene.bundles[place.bundleIndex]->sidecar().part;
}
namespace {
std::string_view partName(const PartDefinition& part) noexcept {
    if(std::holds_alternative<WinchModule>(part.module))return "Winch";
    if(std::holds_alternative<HelmModule>(part.module))return "Helm";
    if(std::holds_alternative<EngineModule>(part.module))return "Engine";
    if(std::holds_alternative<PropellerModule>(part.module))return "Propeller";
    if(std::holds_alternative<FlotationModule>(part.module))return "Pontoon";
    if(std::holds_alternative<CargoCradleModule>(part.module))return "Cargo cradle";
    if(part.nameKey.find("beam")!=std::string::npos)return "Beam";
    return "Deck plate";
}
}
std::string_view CoveWorkshop::selectedName() const noexcept { return partName(definition(selected_)); }
std::string_view CoveWorkshop::catalogName() const noexcept {
    return catalog_.empty()?"No parts":partName(design_.bundles[catalog_[catalogIndex_].bundleIndex]->sidecar().part);
}
ResourceAmounts CoveWorkshop::catalogCost() const noexcept {
    return catalog_.empty()?ResourceAmounts{}:design_.bundles[catalog_[catalogIndex_].bundleIndex]->sidecar().part.cost;
}
ContentKey CoveWorkshop::catalogDefinition() const noexcept {
    return catalog_.empty()?ContentKey{}:design_.bundles[catalog_[catalogIndex_].bundleIndex]->sidecar().part.key;
}
bool CoveWorkshop::selectCatalog(int direction) noexcept {
    if(catalog_.empty() || (direction!=-1 && direction!=1))return false;
    const auto count=static_cast<uint32_t>(catalog_.size());
    catalogIndex_=(catalogIndex_+count+(direction>0?1u:count-1u))%count;return true;
}
bool CoveWorkshop::canAdd() const noexcept {
    if(catalog_.empty() || changed())return false;
    for(uint32_t slot=fixedPlacements_;slot<32;++slot)
        if(std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)==acceptedSlots_.end()
            && std::find(design_.registry.navigation->boatPlacements.begin(),design_.registry.navigation->boatPlacements.end(),slot)
                ==design_.registry.navigation->boatPlacements.end())return true;
    return false;
}
bool CoveWorkshop::addPart(const PartInstance* stored) {
    if(!canAdd()){message_="Keep or cancel the move first. Launch removals to free part slots.";return false;}
    auto candidate=design_;
    uint32_t slot=fixedPlacements_;
    while(std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)!=acceptedSlots_.end()
        || std::find(candidate.registry.navigation->boatPlacements.begin(),candidate.registry.navigation->boatPlacements.end(),slot)
            !=candidate.registry.navigation->boatPlacements.end())++slot;
    auto placed=catalog_[catalogIndex_];placed.placement=candidate.registry.placements[selected_].placement;
    if(stored) {
        if(stored->definition!=catalogDefinition()||stored->provenance!=PartProvenance{})return false;
        placed.paint=stored->paint;placed.settings=stored->settings;
    }
    const auto raised=checkedAdd(placed.placement.translation,{0,32,0});if(!raised)return false;
    placed.placement.translation=*raised;
    if(slot==candidate.registry.placements.size())candidate.registry.placements.push_back(placed);
    else candidate.registry.placements[slot]=placed;
    candidate.registry.navigation->boatPlacements.push_back(slot);
    preview_=std::move(candidate);selected_=slot;evaluate();
    // A new part is a visible, editable ghost. Snapping searches the real
    // sockets and full compiler; an unsuccessful search never spends stock.
    (void)snap();return true;
}
double CoveWorkshop::massKg() const noexcept {
    return compiled_?compiled_->massKg():0;
}
ModuleSettings CoveWorkshop::selectedSettings() const noexcept {
    return preview_.registry.placements[selected_].settings.value_or(defaultModuleSettings(definition(selected_)));
}
bool CoveWorkshop::configurable() const noexcept {
    const auto& module=definition(selected_).module;
    return member(preview_.registry,selected_) && (std::holds_alternative<PropellerModule>(module)
        || std::holds_alternative<HelmModule>(module) || std::holds_alternative<WinchModule>(module));
}
bool CoveWorkshop::hasOutputLimit() const noexcept {
    return configurable() && selectedSettings().kind!=SettingsKind::Winch;
}
bool CoveWorkshop::canReverse() const noexcept {
    return configurable() && std::holds_alternative<PropellerModule>(definition(selected_).module);
}
bool CoveWorkshop::configure(SettingAction action) {
    if(!configurable())return false;
    auto settings=selectedSettings();
    switch(action) {
    case SettingAction::Toggle:settings.enabled=!settings.enabled;break;
    case SettingAction::CycleLimit:
        if(!hasOutputLimit())return false;
        settings.limitPermille=settings.limitPermille==0?1000:static_cast<uint16_t>((settings.limitPermille-1)/250*250);break;
    case SettingAction::Reverse:
        if(!canReverse())return false;
        settings.reversed=!settings.reversed;break;
    }
    preview_.registry.placements[selected_].settings=settings;evaluate();
    if(valid())message_="Settings ready. Keep, then Launch to apply.";
    return true;
}
bool CoveWorkshop::changed() const noexcept {
    return selected_>=design_.registry.placements.size()
        || preview_.registry.placements[selected_].placement!=design_.registry.placements[selected_].placement
        || selectedSettings()!=design_.registry.placements[selected_].settings.value_or(defaultModuleSettings(definition(design_,selected_)))
        || preview_.registry.navigation->boatPlacements!=design_.registry.navigation->boatPlacements;
}
bool CoveWorkshop::matchesDesign(const assets::LoadedAssetFixture& scene) const noexcept {
    const auto& a=design_.registry;const auto& b=scene.registry;
    if(!b.navigation || !std::is_permutation(a.navigation->boatPlacements.begin(),a.navigation->boatPlacements.end(),b.navigation->boatPlacements.begin(),b.navigation->boatPlacements.end())
        || a.placements.size()!=b.placements.size() || a.connections.size()!=b.connections.size())return false;
    for(size_t i=0;i<a.placements.size();++i)
        if(a.placements[i].placement!=b.placements[i].placement || a.placements[i].bundleIndex!=b.placements[i].bundleIndex
            || a.placements[i].prototype!=b.placements[i].prototype
            || a.placements[i].paint.value_or(std::array<uint8_t,4>{255,255,255,255})!=b.placements[i].paint.value_or(std::array<uint8_t,4>{255,255,255,255})
            || a.placements[i].settings.value_or(defaultModuleSettings(definition(design_,static_cast<uint32_t>(i))))
                !=b.placements[i].settings.value_or(defaultModuleSettings(definition(scene,static_cast<uint32_t>(i)))))return false;
    for(const auto& weld:a.connections)if(std::none_of(b.connections.begin(),b.connections.end(),[&](const auto& other){
        return (weld.aPlacement==other.aPlacement&&weld.aSocket==other.aSocket&&weld.bPlacement==other.bPlacement&&weld.bSocket==other.bSocket)
            ||(weld.aPlacement==other.bPlacement&&weld.aSocket==other.bSocket&&weld.bPlacement==other.aPlacement&&weld.bSocket==other.aSocket);
    }))return false;
    return true;
}
bool CoveWorkshop::reconnect(assets::LoadedAssetFixture& candidate) const {
    auto& r=candidate.registry;
    std::erase_if(r.connections,[&](const auto& c){return c.aPlacement==selected_||c.bPlacement==selected_;});
    if(!member(r,selected_))return true;
    size_t visits=0;
    for(const auto& a:definition(candidate,selected_).sockets) {
        const auto af=compose(r.placements[selected_].placement,a.frame);
        if(!af)return false;
        for(auto other:r.navigation->boatPlacements) {
            if(other==selected_)continue;
            for(const auto& b:definition(candidate,other).sockets) {
                if(++visits>8192)return false;
                if(matchSockets(a,b,ConnectionKind::Weld)!=SocketMatchError::None)continue;
                const auto bf=compose(r.placements[other].placement,b.frame);
                if(!bf || af->translation!=bf->translation || compose(af->rotation,mating)!=bf->rotation)continue;
                const auto used=[&](uint32_t part,SocketId socket){return std::count_if(r.connections.begin(),r.connections.end(),
                    [&](const auto& c){return (c.aPlacement==part&&c.aSocket==socket)||(c.bPlacement==part&&c.bSocket==socket);});};
                if(used(selected_,a.id)>=a.connectionCapacity || used(other,b.id)>=b.connectionCapacity)continue;
                if(r.connections.size()>=64)return false;
                r.connections.push_back({selected_,other,a.id,b.id});
            }
        }
    }
    return true;
}
void CoveWorkshop::evaluate() {
    compiled_.reset();
    if(!reconnect(preview_)) {message_="Too many socket connections.";return;}
    std::string error;compiled_=CoveBoatAssembly::compile(preview_,error);
    if(compiled_)message_=changed()?"Fits. Keep this change or try another socket.":"Part is connected.";
    else if(error.find("solid")!=std::string::npos || error.find("overlap")!=std::string::npos)
        message_="Blocked by another part. Try another socket.";
    else if(error.find("clearance")!=std::string::npos)message_="Leave room around the connector.";
    else message_="Not connected. Snap to a free socket.";
}
bool CoveWorkshop::snap() {
    struct Candidate { GridTransform frame; double distance; };
    std::vector<Candidate> candidates;
    const auto current=preview_.registry.placements[selected_].placement;
    size_t visits=0;
    for(const auto& a:definition(selected_).sockets) {
        const auto ai=inverse(a.frame);if(!ai)continue;
        for(auto other:preview_.registry.navigation->boatPlacements) {
            if(other==selected_)continue;
            for(const auto& b:definition(other).sockets) {
                if(++visits>8192){message_="Too many socket candidates.";return false;}
                if(matchSockets(b,a,ConnectionKind::Weld)!=SocketMatchError::None)continue;
                const auto bf=compose(preview_.registry.placements[other].placement,b.frame);
                const auto target=bf?compose(*bf,GridTransform{{},mating}):std::nullopt;
                const auto frame=target?compose(*target,*ai):std::nullopt;
                if(!frame || *frame==current || !(definition(selected_).permittedRotationMask&(1u<<frame->rotation.value)))continue;
                if(std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){return c.frame==*frame;}))continue;
                if(candidates.size()>=256){message_="Too many socket candidates.";return false;}
                const auto d=checkedSubtract(frame->translation,current.translation);if(!d)continue;
                candidates.push_back({*frame,double(d->x)*double(d->x)+double(d->y)*double(d->y)+double(d->z)*double(d->z)});
            }
        }
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    for(const auto& option:candidates) {
        auto candidate=preview_;candidate.registry.placements[selected_].placement=option.frame;
        if(!reconnect(candidate))continue;
        std::string error;auto assembly=CoveBoatAssembly::compile(candidate,error);
        if(!assembly)continue;
        if(!member(design_.registry,selected_)) {
            // New-part placement should suggest an operable layout. All four
            // cove standing anchors rest on authored dock/deck collision; the
            // terrain is irrelevant to this bounded placement preference.
            CovePlayer walking;
            if(!walking.initialize(candidate,[](double,double){return -std::numeric_limits<double>::infinity();},
                error,collisionBoatSlots_))continue;
        }
        preview_=std::move(candidate);compiled_=std::move(assembly);
        message_="Fits. Keep this change or try another socket.";return true;
    }
    message_="No other free socket fits this part.";return false;
}
std::vector<std::byte> CoveWorkshop::blueprintBytes(std::string& error) const {
    if(changed()){error="Keep or cancel your current change before saving.";return {};}
    const auto catalog=makeCovePartCatalog(design_,error);if(!catalog)return {};
    const auto boat=CoveBoatAssembly::compile(design_,error);if(!boat)return {};
    BuildIssue issue;const auto model=BuildModel::create(boat->build(),*catalog,issue);
    std::vector<std::byte> bytes;
    if(!model || encodeBlueprint(duplicateDesign(*model),*catalog,bytes)) {error="This design cannot be saved.";return {};}
    error.clear();return bytes;
}
bool CoveWorkshop::loadBlueprint(std::span<const std::byte> bytes,std::string& error) {
    const auto fail=[&](const char* why){error=why;return false;};
    if(changed())return fail("Keep or cancel your current change before loading.");
    if(revision_==std::numeric_limits<uint64_t>::max())return fail("Design history is full.");
    const auto catalog=makeCovePartCatalog(design_,error);if(!catalog)return false;
    std::optional<BuildBlueprint> blueprint;
    if(decodeBlueprint(bytes,*catalog,blueprint))return fail("This saved design is damaged, incompatible or uses unavailable parts.");
    if(blueprint->parts.size()>32 || blueprint->connections.size()>64)return fail("This design is too large for the cove workshop.");
    for(const auto& link:blueprint->connections)
        if(link.kind!=ConnectionKind::Weld || !link.enabled)return fail("This workshop requires a welded boat design.");
    auto candidate=design_;auto& registry=candidate.registry;
    std::array<bool,32> used{};std::array<uint32_t,32> slots{};slots.fill(32);
    // Reserve every exact owned match before reusing a same-definition part.
    // Imported ordinals never supply ownership, loans, identities or inventory.
    for(bool exact:{true,false})for(size_t i=0;i<blueprint->parts.size();++i) {
        if(slots[i]!=32)continue;
        const auto& part=blueprint->parts[i];
        for(auto slot:acceptedSlots_)if(!used[slot] && definition(design_,slot).key==part.definition
            && (!exact || registry.placements[slot].placement==part.placement)) {
            used[slot]=true;slots[i]=slot;break;
        }
    }
    registry.navigation->boatPlacements.clear();
    std::erase_if(registry.connections,[&](const auto& c){
        return std::find(collisionBoatSlots_.begin(),collisionBoatSlots_.end(),c.aPlacement)!=collisionBoatSlots_.end()
            ||std::find(collisionBoatSlots_.begin(),collisionBoatSlots_.end(),c.bPlacement)!=collisionBoatSlots_.end();
    });
    for(size_t i=0;i<blueprint->parts.size();++i) {
        const auto& part=blueprint->parts[i];
        if(slots[i]==32) {
            uint32_t slot=fixedPlacements_;
            while(slot<32 && (used[slot] || std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)!=acceptedSlots_.end()))++slot;
            if(slot==32)return fail("No free part slots. Launch removals before loading a larger design.");
            assets::FixturePartPlacement placed;bool found=false;
            for(size_t j=0;j<candidate.bundles.size();++j)if(candidate.bundles[j]->sidecar().part.key==part.definition) {
                placed.bundleIndex=static_cast<uint32_t>(j);found=true;break;
            }
            if(!found)return fail("A saved part has no installed model in this cove.");
            if(slot==registry.placements.size())registry.placements.push_back(placed);else registry.placements[slot]=placed;
            used[slot]=true;slots[i]=slot;
        }
        auto& placed=registry.placements[slots[i]];placed.placement=part.placement;placed.settings=part.settings;placed.paint=part.paint;
        registry.navigation->boatPlacements.push_back(slots[i]);
    }
    for(const auto& link:blueprint->connections)
        registry.connections.push_back({slots[link.a.partOrdinal-1],slots[link.b.partOrdinal-1],link.a.socket,link.b.socket});
    if(registry.connections.size()>64)return fail("This design has too many connections for the cove workshop.");
    auto compiled=CoveBoatAssembly::compile(candidate,error);if(!compiled)return false;
    // This cove supports authored-strength welds. Do not silently replace the
    // custom strength of a future imported machine with a different value.
    for(const auto& link:blueprint->connections) {
        const auto binding=[&](uint32_t ordinal){const auto slot=slots[ordinal-1];
            return std::find_if(compiled->parts().begin(),compiled->parts().end(),[&](const auto& p){return p.placement==slot;})->id;};
        const SocketEndpoint a{binding(link.a.partOrdinal),link.a.socket},b{binding(link.b.partOrdinal),link.b.socket};
        const auto found=std::find_if(compiled->build().connections.begin(),compiled->build().connections.end(),[&](const auto& c){return (c.a==a&&c.b==b)||(c.a==b&&c.b==a);});
        if(found==compiled->build().connections.end() || std::tie(found->strength.tensionNewtons,found->strength.shearNewtons,found->strength.bendingNewtonMetres,found->strength.torsionNewtonMetres)
                !=std::tie(link.strength.tensionNewtons,link.strength.shearNewtons,link.strength.bendingNewtonMetres,link.strength.torsionNewtonMetres))return fail("This workshop cannot restore custom weld strength yet.");
    }
    auto history=history_;if(history.size()==maximumHistory)history.erase(history.begin());history.push_back(design_.registry);
    auto preview=candidate;design_=std::move(candidate);preview_=std::move(preview);compiled_=std::move(compiled);history_=std::move(history);
    selected_=design_.registry.navigation->boatPlacements.front();++revision_;
    message_="Design loaded. Check the cost, then Launch to build it.";error.clear();return true;
}

bool CoveWorkshop::command(Action action) {
    if(action==Action::Previous || action==Action::Next) {
        const auto& parts=design_.registry.navigation->boatPlacements;
        const auto found=std::find(parts.begin(),parts.end(),selected_);
        const auto i=found==parts.end()?0u:static_cast<size_t>(found-parts.begin());
        selected_=parts[(i+parts.size()+(action==Action::Next?1:parts.size()-1))%parts.size()];
        preview_=design_;evaluate();return true;
    }
    if(action==Action::Revert){preview_=design_;if(!member(design_.registry,selected_))selected_=design_.registry.navigation->boatPlacements.front();evaluate();return true;}
    if(action==Action::Undo) {
        if(history_.empty() || revision_==std::numeric_limits<uint64_t>::max())return false;
        auto candidate=design_;candidate.registry=history_.back();std::string error;
        auto assembly=CoveBoatAssembly::compile(candidate,error);if(!assembly)return false;
        design_=std::move(candidate);preview_=design_;compiled_=std::move(assembly);history_.pop_back();++revision_;
        if(!member(design_.registry,selected_))selected_=design_.registry.navigation->boatPlacements.front();
        message_="Change undone.";return true;
    }
    if(action==Action::Keep) {
        if(!valid() || !changed() || revision_==std::numeric_limits<uint64_t>::max())return false;
        auto accepted=preview_;auto history=history_;
        if(history.size()==maximumHistory)history.erase(history.begin());
        history.push_back(design_.registry);design_=std::move(accepted);history_=std::move(history);++revision_;
        if(!member(design_.registry,selected_))selected_=design_.registry.navigation->boatPlacements.front();
        message_="Design kept. Launch to sail these changes.";return true;
    }
    if(action==Action::Snap)return snap();
    if(action==Action::Remove) {
        if(!member(design_.registry,selected_)) {
            preview_=design_;selected_=design_.registry.navigation->boatPlacements.front();evaluate();return true;
        }
        if(design_.registry.navigation->boatPlacements.size()<=1)return false;
        preview_=design_;std::erase(preview_.registry.navigation->boatPlacements,selected_);evaluate();return true;
    }
    auto& placement=preview_.registry.placements[selected_].placement;
    if(action==Action::Rotate) {
        for(uint8_t i=0;i<24;++i) if(rotate(CubeRotation{i},{1,0,0})==GridPosition{0,0,-1}
            && rotate(CubeRotation{i},{0,1,0})==GridPosition{0,1,0}) {
            const auto r=compose(CubeRotation{i},placement.rotation);if(!r)return false;placement.rotation=*r;break;
        }
    } else {
        GridPosition offset{};
        switch(action) {
            case Action::Left:offset.x=-50;break;case Action::Right:offset.x=50;break;
            case Action::Forward:offset.z=-50;break;case Action::Back:offset.z=50;break;
            case Action::Raise:offset.y=16;break;case Action::Lower:offset.y=-16;break;
            default:return false;
        }
        const auto next=checkedAdd(placement.translation,offset);if(!next)return false;placement.translation=*next;
    }
    evaluate();return true;
}
} // namespace voxy::game::expedition
