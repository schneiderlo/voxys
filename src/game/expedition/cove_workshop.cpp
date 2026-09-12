#include "game/expedition/cove_workshop.hpp"
#include "game/expedition/cove_player.hpp"
#include <algorithm>
#include <limits>
#include <tuple>
#include <cmath>

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
    if(!scene.registry.navigation || scene.registry.placements.size()>assets::kMaximumFixturePlacements || scene.registry.connections.size()>assets::kMaximumFixtureConnections) {
        error="Workshop needs a bounded starter craft";return {};
    }
    auto result=std::unique_ptr<CoveWorkshop>(new CoveWorkshop(scene));
    result->fixedPlacements_=fixedPlacements?fixedPlacements:static_cast<uint32_t>(scene.registry.placements.size());
    if(result->fixedPlacements_>scene.registry.placements.size()){error="Invalid installed scene slots";return {};}
    result->acceptedSlots_=scene.registry.navigation->boatPlacements;
    result->collisionBoatSlots_=originalBoatSlots.empty()?result->acceptedSlots_
        :std::vector<uint32_t>(originalBoatSlots.begin(),originalBoatSlots.end());
    for(uint32_t i=result->fixedPlacements_;i<assets::kMaximumFixturePlacements;++i)result->collisionBoatSlots_.push_back(i);
    result->compiled_=acceptedSeparated?CoveBoatAssembly::compileSeparatedScene(scene,error):CoveBoatAssembly::compile(scene,error);
    if(!result->compiled_)return {};
    result->designSeparated_=result->compiled_->roots().size()>1;
    for(size_t i=0;i<scene.bundles.size();++i) {
        // The authored cargo bundles are recoverable mission objects, not
        // structural/module choices in the starter's parts drawer.
        if(std::holds_alternative<TowEyeModule>(scene.bundles[i]->sidecar().part.module))continue;
        result->catalog_.push_back({static_cast<uint32_t>(i),{},false});
    }
    result->selected_=scene.registry.navigation->boatPlacements.front();
    for(auto p:scene.registry.navigation->boatPlacements)
        if(std::holds_alternative<WinchModule>(result->definition(p).module))result->selected_=p;
    result->selection_={result->selected_};result->rememberSelection();
    if(result->designSeparated_)result->issue_.problem=Problem::NotConnected;
    result->message_=result->valid()?"Select a part. Snap finds a matching socket."
        :"This build has separate sections. Load its protected design or rebuild the starter.";
    error.clear();return result;
}
const PartDefinition& CoveWorkshop::definition(uint32_t p) const {
    return definition(preview_,p);
}
std::optional<WorkshopBounds> CoveWorkshop::viewBounds(bool wholeBuild) const {
    std::optional<WorkshopBounds> result;
    for(auto slot:preview_.registry.navigation->boatPlacements) {
        if(!wholeBuild&&!isSelected(slot))continue;
        const auto& placement=preview_.registry.placements[slot];
        const auto rotation=rotationMatrix(placement.placement.rotation);if(!rotation)return {};
        const auto include=[&](glm::dvec3 lo,glm::dvec3 hi) {
            for(unsigned corner=0;corner<8;++corner) {
                const glm::dvec3 p{corner&1?hi.x:lo.x,corner&2?hi.y:lo.y,corner&4?hi.z:lo.z};
                const auto t=placement.placement.translation;glm::dvec3 world{t.x*.02,t.y*.02,t.z*.02};
                for(int row=0;row<3;++row)for(int column=0;column<3;++column)world[row]+=rotation->elements[static_cast<size_t>(row*3+column)]*p[column];
                if(!result)result=WorkshopBounds{world,world};
                else {result->minimum=glm::min(result->minimum,world);result->maximum=glm::max(result->maximum,world);}
            }
        };
        if(placement.prototype) {
            const auto b=definition(slot).footprint;
            include(glm::dvec3(b.minimum.x,b.minimum.y,b.minimum.z)*.02,glm::dvec3(b.maximum.x,b.maximum.y,b.maximum.z)*.02);
        } else for(const auto& lod:preview_.bundles[placement.bundleIndex]->lods()) {
            const auto& b=lod.prefab.canonicalBounds;if(!b.valid)return {};
            include(b.minimum,b.maximum);
        }
    }
    return result;
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
    if(part.nameKey=="salvage.part.brick_1x2")return "Brick 1 x 2";
    if(part.nameKey=="salvage.part.brick_2x2")return "Brick 2 x 2";
    if(part.nameKey=="salvage.part.brick_2x4")return "Brick 2 x 4";
    if(part.nameKey.find("beam")!=std::string::npos)return "Beam";
    return "Deck plate";
}
}
std::string_view CoveWorkshop::selectedName() const noexcept { return partName(definition(selected_)); }
std::string_view CoveWorkshop::catalogNameAt(uint32_t index) const noexcept {
    return index<catalog_.size()?partName(design_.bundles[catalog_[index].bundleIndex]->sidecar().part):"No parts";
}
ResourceAmounts CoveWorkshop::catalogCostAt(uint32_t index) const noexcept {
    return index<catalog_.size()?design_.bundles[catalog_[index].bundleIndex]->sidecar().part.cost:ResourceAmounts{};
}
std::string_view CoveWorkshop::catalogName() const noexcept { return catalogNameAt(catalogIndex_); }
ResourceAmounts CoveWorkshop::catalogCost() const noexcept { return catalogCostAt(catalogIndex_); }
bool CoveWorkshop::selectCatalogAt(uint32_t index) noexcept {
    if(index>=catalog_.size() || (brickToolActive_&&index!=catalogIndex_))return false;
    catalogIndex_=index;return true;
}
bool CoveWorkshop::refuse(Problem problem,std::string_view message,std::optional<uint32_t> placement) {
    issue_={problem,placement};message_=message;return false;
}
bool CoveWorkshop::isSelected(uint32_t placement) const noexcept {
    return std::binary_search(selection_.begin(),selection_.end(),placement);
}
void CoveWorkshop::rememberSelection() { keptSelection_=selection_;keptPrimary_=selected_; }
void CoveWorkshop::normalizeSelection() {
    std::erase_if(selection_,[&](uint32_t slot){return !member(preview_.registry,slot);});
    if(selection_.empty())selection_.push_back(preview_.registry.navigation->boatPlacements.front());
    std::sort(selection_.begin(),selection_.end());
    selection_.erase(std::unique(selection_.begin(),selection_.end()),selection_.end());
    if(!isSelected(selected_))selected_=selection_.front();
}
bool CoveWorkshop::cleanSelection() {
    if(brickToolActive_)(void)stopBrickTool();
    if(changed())return refuse(Problem::PendingEdit,"Keep or cancel this change before selecting parts.");
    return true;
}
bool CoveWorkshop::selectPart(uint32_t placement,SelectionMode mode) {
    if(!member(design_.registry,placement))return refuse(Problem::InvalidSelection,"Select a part in your boat.");
    if(mode!=SelectionMode::Replace&&mode!=SelectionMode::Toggle&&mode!=SelectionMode::Add)return false;
    if(!cleanSelection())return false;
    auto next=selection_;
    if(mode==SelectionMode::Replace)next={placement};
    else if(mode==SelectionMode::Toggle&&isSelected(placement)) {
        if(next.size()==1)return refuse(Problem::InvalidSelection,"Keep at least one part selected.");
        std::erase(next,placement);
    } else if(!isSelected(placement))next.push_back(placement);
    std::sort(next.begin(),next.end());
    auto kept=next;
    selection_=std::move(next);selected_=isSelected(placement)?placement:selection_.front();
    keptSelection_=std::move(kept);keptPrimary_=selected_;evaluate();return true;
}
bool CoveWorkshop::selectAll() {
    if(!cleanSelection())return false;
    auto next=design_.registry.navigation->boatPlacements;std::sort(next.begin(),next.end());
    auto kept=next;selection_=std::move(next);keptSelection_=std::move(kept);keptPrimary_=selected_;
    evaluate();return true;
}
std::optional<CoveWorkshop::Pick> CoveWorkshop::pick(glm::dvec3 origin,glm::dvec3 direction,bool excludeSelected) const {
    for(int axis=0;axis<3;++axis)if(!std::isfinite(origin[axis])||!std::isfinite(direction[axis]))return {};
    const auto length=glm::length(direction);if(length<1e-9)return {};
    direction/=length;double nearest=std::numeric_limits<double>::infinity();std::optional<Pick> result;
    for(auto slot:preview_.registry.navigation->boatPlacements) {
        if(excludeSelected&&isSelected(slot))continue;
        for(const auto& proxy:definition(slot).collision) {
            const auto local=boxBounds(proxy);
            const auto bounds=local?transformBounds(preview_.registry.placements[slot].placement,*local):std::nullopt;
            if(!bounds)continue;
            const glm::dvec3 lo=glm::dvec3(bounds->minimum.x,bounds->minimum.y,bounds->minimum.z)*.02;
            const glm::dvec3 hi=glm::dvec3(bounds->maximum.x,bounds->maximum.y,bounds->maximum.z)*.02;
            double enter=0,leave=nearest;
            for(int axis=0;axis<3;++axis) {
                if(std::abs(direction[axis])<1e-12) {if(origin[axis]<lo[axis]||origin[axis]>hi[axis]){leave=-1;break;}}
                else {
                    const double a=(lo[axis]-origin[axis])/direction[axis],b=(hi[axis]-origin[axis])/direction[axis];
                    enter=std::max(enter,std::min(a,b));leave=std::min(leave,std::max(a,b));
                }
            }
            if(enter<=leave&&enter<nearest){nearest=enter;result=Pick{slot,origin+direction*enter};}
        }
    }
    return result;
}
bool CoveWorkshop::aimAt(const Pick& hit) {
    if(isSelected(hit.placement)||!member(preview_.registry,hit.placement)||!member(preview_.registry,selected_))return false;
    for(int axis=0;axis<3;++axis)if(!std::isfinite(hit.point[axis]))return false;
    const auto current=preview_.registry.placements[selected_].placement;
    std::optional<GridTransform> best;double score=std::numeric_limits<double>::infinity();
    // Preserve the chosen orientation. The closest exact mating transform is
    // shown even when blocked, so a red ghost never jumps to a different site.
    for(const auto& a:definition(selected_).sockets) {
        const auto ai=inverse(a.frame);if(!ai)continue;
        for(const auto& b:definition(hit.placement).sockets) {
            if(matchSockets(b,a,ConnectionKind::Weld)!=SocketMatchError::None)continue;
            const auto bf=compose(preview_.registry.placements[hit.placement].placement,b.frame);
            const auto target=bf?compose(*bf,GridTransform{{},mating}):std::nullopt;
            const auto frame=target?compose(*target,*ai):std::nullopt;
            if(!frame||frame->rotation!=current.rotation)continue;
            const auto center=glm::dvec3(frame->translation.x,frame->translation.y,frame->translation.z)*.02;
            const auto delta=center-hit.point;const double distance=glm::dot(delta,delta);
            if(distance<score){score=distance;best=*frame;}
        }
    }
    if(!best)return false;
    if(*best==current)return true;
    const auto delta=checkedSubtract(best->translation,current.translation);
    return delta&&moveSelection(*delta);
}
ContentKey CoveWorkshop::catalogDefinition() const noexcept {
    return catalog_.empty()?ContentKey{}:design_.bundles[catalog_[catalogIndex_].bundleIndex]->sidecar().part.key;
}
bool CoveWorkshop::selectCatalog(int direction) noexcept {
    if(catalog_.empty() || brickToolActive_ || (direction!=-1 && direction!=1))return false;
    const auto count=static_cast<uint32_t>(catalog_.size());
    catalogIndex_=(catalogIndex_+count+(direction>0?1u:count-1u))%count;return true;
}
bool CoveWorkshop::canAdd() const noexcept {
    if(catalog_.empty() || changed())return false;
    return hasFreePartSlot();
}
bool CoveWorkshop::hasFreePartSlot() const noexcept {
    for(uint32_t slot=fixedPlacements_;slot<assets::kMaximumFixturePlacements;++slot)
        if(std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)==acceptedSlots_.end()
            && std::find(design_.registry.navigation->boatPlacements.begin(),design_.registry.navigation->boatPlacements.end(),slot)
                ==design_.registry.navigation->boatPlacements.end())return true;
    return false;
}
std::optional<std::vector<uint32_t>> CoveWorkshop::freeSlots(size_t count) const {
    std::vector<uint32_t> result;result.reserve(count);
    for(uint32_t slot=fixedPlacements_;slot<assets::kMaximumFixturePlacements&&result.size()<count;++slot)
        if(std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)==acceptedSlots_.end()
            &&!member(design_.registry,slot))result.push_back(slot);
    if(result.size()!=count)return {};
    return result;
}
bool CoveWorkshop::moveSelection(GridPosition deltaTicks) {
    auto candidate=preview_;
    for(auto slot:selection_) {
        if(!member(candidate.registry,slot))return refuse(Problem::InvalidSelection,"Select a part in your boat.");
        auto& transform=candidate.registry.placements[slot].placement;
        const auto position=checkedAdd(transform.translation,deltaTicks);
        if(!position)return refuse(Problem::OutOfBounds,"That move is outside the building grid.",slot);
        transform.translation=*position;
    }
    preview_=std::move(candidate);evaluate();return true;
}
bool CoveWorkshop::rotateSelection(Axis axis,int quarterTurns) {
    if(axis!=Axis::X&&axis!=Axis::Y&&axis!=Axis::Z)return false;
    const GridPosition x=axis==Axis::Y?GridPosition{0,0,-1}:axis==Axis::Z?GridPosition{0,1,0}:GridPosition{1,0,0};
    const GridPosition y=axis==Axis::X?GridPosition{0,0,1}:axis==Axis::Z?GridPosition{-1,0,0}:GridPosition{0,1,0};
    CubeRotation turn{};
    for(uint8_t i=0;i<24;++i)if(rotate(CubeRotation{i},{1,0,0})==x&&rotate(CubeRotation{i},{0,1,0})==y){turn=CubeRotation{i};break;}
    CubeRotation rotation{};
    const auto turns=(quarterTurns%4+4)%4;
    for(int i=0;i<turns;++i)rotation=*compose(turn,rotation);
    auto candidate=preview_;const auto pivot=preview_.registry.placements[selected_].placement.translation;
    for(auto slot:selection_) {
        auto& transform=candidate.registry.placements[slot].placement;
        const auto relative=checkedSubtract(transform.translation,pivot);
        const auto rotated=relative?rotate(rotation,*relative):std::nullopt;
        const auto position=rotated?checkedAdd(pivot,*rotated):std::nullopt;
        const auto orientation=compose(rotation,transform.rotation);
        if(!position||!orientation)return refuse(Problem::OutOfBounds,"That rotation is outside the building grid.",slot);
        if(!(definition(slot).permittedRotationMask&(1u<<orientation->value)))
            return refuse(Problem::UnsupportedRotation,"A selected part cannot use that orientation.",slot);
        transform={*position,*orientation};
    }
    preview_=std::move(candidate);evaluate();return true;
}
bool CoveWorkshop::duplicateSelection(GridPosition deltaTicks) { return copySelection(deltaTicks,{},0); }
bool CoveWorkshop::mirrorSelection(Axis axis,int32_t planeTicks) {
    if(axis!=Axis::X&&axis!=Axis::Z)return refuse(Problem::UnsupportedOperation,"Mirror across the X or Z plane.");
    return copySelection({},axis,planeTicks);
}
bool CoveWorkshop::copySelection(GridPosition deltaTicks,std::optional<Axis> mirror,int32_t planeTicks) {
    if(!cleanSelection())return false;
    if(mirror)for(auto slot:selection_)if(preview_.registry.placements[slot].prototype||!isPaintableBrick(definition(slot).nameKey))
        return refuse(Problem::UnsupportedOperation,"Mirror supports bricks only. Machinery has no mirrored part.",slot);
    auto slots=freeSlots(selection_.size());
    if(!slots)return refuse(Problem::NoFreeSlots,"Not enough free part slots. Launch removals first.");
    auto candidate=design_;auto primary=selected_;
    for(size_t i=0;i<selection_.size();++i) {
        const auto source=selection_[i],destination=(*slots)[i];auto placement=design_.registry.placements[source];
        if(mirror) {
            const int axis=*mirror==Axis::X?0:2;
            auto& t=placement.placement.translation;
            const int64_t old=axis==0?t.x:t.z;
            const int64_t reflected=2*int64_t{planeTicks}-old;
            if(reflected<std::numeric_limits<int32_t>::min()||reflected>std::numeric_limits<int32_t>::max())
                return refuse(Problem::OutOfBounds,"The mirrored copy is outside the building grid.",source);
            if(axis==0)t.x=static_cast<int32_t>(reflected);else t.z=static_cast<int32_t>(reflected);
            if(!isValid(t))return refuse(Problem::OutOfBounds,"The mirrored copy is outside the building grid.",source);
            const auto original=rotationMatrix(placement.placement.rotation);if(!original)return false;
            // These brick shells, cavities and stud arrays are symmetric in
            // local X. Two reflections therefore produce a proper rotation.
            std::optional<CubeRotation> orientation;
            for(uint8_t r=0;r<24&&!orientation;++r) {
                const auto m=rotationMatrix(CubeRotation{r});bool equal=true;
                for(int row=0;row<3;++row)for(int column=0;column<3;++column) {
                    const auto at=static_cast<size_t>(row*3+column);
                    equal&=m->elements[at]==original->elements[at]*(row==axis?-1:1)*(column==0?-1:1);
                }
                if(equal)orientation=CubeRotation{r};
            }
            if(!orientation||!(definition(source).permittedRotationMask&(1u<<orientation->value)))
                return refuse(Problem::UnsupportedRotation,"A mirrored brick cannot use that orientation.",source);
            placement.placement.rotation=*orientation;
        } else {
            const auto t=checkedAdd(placement.placement.translation,deltaTicks);
            if(!t)return refuse(Problem::OutOfBounds,"The copy is outside the building grid.",source);
            placement.placement.translation=*t;
        }
        if(destination==candidate.registry.placements.size())candidate.registry.placements.push_back(placement);
        else candidate.registry.placements[destination]=placement;
        candidate.registry.navigation->boatPlacements.push_back(destination);
        if(source==selected_)primary=destination;
    }
    preview_=std::move(candidate);selection_=std::move(*slots);selected_=primary;evaluate();return true;
}
bool CoveWorkshop::replaceSelection(uint32_t catalogIndex) {
    if(catalogIndex>=catalog_.size())return false;
    if(!cleanSelection())return false;
    const auto& replacement=design_.bundles[catalog_[catalogIndex].bundleIndex]->sidecar().part;
    std::vector<uint32_t> replaced;
    for(auto slot:selection_)if(definition(slot).key!=replacement.key)replaced.push_back(slot);
    if(replaced.empty()){issue_={};message_="Selection already uses that part.";return false;}
    auto slots=freeSlots(replaced.size());
    if(!slots)return refuse(Problem::NoFreeSlots,"Replacement needs free part slots. Launch removals first.");
    auto candidate=design_;auto selection=selection_;auto primary=selected_;
    for(size_t i=0;i<replaced.size();++i) {
        const auto source=replaced[i],destination=(*slots)[i];auto placement=catalog_[catalogIndex];
        placement.placement=design_.registry.placements[source].placement;
        if(!(replacement.permittedRotationMask&(1u<<placement.placement.rotation.value)))
            return refuse(Problem::UnsupportedRotation,"The replacement cannot use this orientation.",source);
        placement.settings=defaultModuleSettings(replacement);
        if(isPaintableBrick(replacement.nameKey))placement.paint=design_.registry.placements[source].paint.value_or(kOriginalBrickPaint);
        if(destination==candidate.registry.placements.size())candidate.registry.placements.push_back(placement);
        else candidate.registry.placements[destination]=placement;
        std::erase(candidate.registry.navigation->boatPlacements,source);
        candidate.registry.navigation->boatPlacements.push_back(destination);
        std::replace(selection.begin(),selection.end(),source,destination);
        if(primary==source)primary=destination;
    }
    std::sort(selection.begin(),selection.end());
    preview_=std::move(candidate);selection_=std::move(selection);selected_=primary;catalogIndex_=catalogIndex;evaluate();return true;
}
bool CoveWorkshop::addPart(const PartInstance* stored) {
    if(!canAdd()){message_="Keep or cancel the move first. Launch removals to free part slots.";return false;}
    return addPreview(catalogIndex_,stored,design_.registry.placements[selected_].placement);
}
bool CoveWorkshop::addPreview(uint32_t index,const PartInstance* stored,GridTransform from) {
    if(index>=catalog_.size() || !hasFreePartSlot())return false;
    auto candidate=design_;
    uint32_t slot=fixedPlacements_;
    while(std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)!=acceptedSlots_.end()
        || std::find(candidate.registry.navigation->boatPlacements.begin(),candidate.registry.navigation->boatPlacements.end(),slot)
            !=candidate.registry.navigation->boatPlacements.end())++slot;
    auto placed=catalog_[index];placed.placement=from;
    if(stored) {
        if(stored->definition!=design_.bundles[placed.bundleIndex]->sidecar().part.key||stored->provenance!=PartProvenance{})return false;
        placed.paint=stored->paint;placed.settings=stored->settings;
    }
    if(brushPaint_&&isPaintableBrick(design_.bundles[placed.bundleIndex]->sidecar().part.nameKey))
        placed.paint=*brushPaint_;
    const auto raised=checkedAdd(placed.placement.translation,{0,32,0});if(!raised)return false;
    placed.placement.translation=*raised;
    if(slot==candidate.registry.placements.size())candidate.registry.placements.push_back(placed);
    else candidate.registry.placements[slot]=placed;
    candidate.registry.navigation->boatPlacements.push_back(slot);
    std::vector<uint32_t> selection{slot};
    preview_=std::move(candidate);selected_=slot;selection_=std::move(selection);catalogIndex_=index;evaluate();
    // A new part is a visible, editable ghost. Snapping searches the real
    // sockets and full compiler; an unsuccessful search never spends stock.
    (void)snap();return true;
}
bool CoveWorkshop::canChooseBrick() const noexcept {
    return !catalog_.empty() && (brickToolActive_ || !changed()) && hasFreePartSlot();
}
bool CoveWorkshop::beginBrickTool(uint32_t index,const PartInstance* stored) {
    if(!canChooseBrick() || index>=catalog_.size()
        || !isPaintableBrick(design_.bundles[catalog_[index].bundleIndex]->sidecar().part.nameKey))return false;
    const auto anchor=brickToolActive_?brickToolAnchor_:selected_;
    const auto from=preview_.registry.placements[selected_].placement;
    // Build the replacement from kept design data. A rejected type/stock/slot
    // request leaves the existing preview and tool intact.
    if(!addPreview(index,stored,from))return false;
    brickToolActive_=true;brickToolAnchor_=anchor;return true;
}
bool CoveWorkshop::placeBrickTool(const PartInstance* nextStored) {
    if(!brickToolActive_ || !valid() || !changed())return false;
    const auto index=catalogIndex_;const auto placed=selected_;
    const auto from=preview_.registry.placements[placed].placement;
    // Explicit Keep remains a one-shot edit. This pointer operation asks for
    // the next preview only after that ordinary keep succeeds.
    if(!command(Action::Keep))return false;
    if(addPreview(index,nextStored,from)) {
        brickToolActive_=true;brickToolAnchor_=placed;
    } else message_="Brick placed. Select parts or Launch your boat.";
    return true;
}
bool CoveWorkshop::stopBrickTool() {
    if(!brickToolActive_)return false;
    brickToolActive_=false;selected_=brickToolAnchor_;
    if(!member(design_.registry,selected_))selected_=design_.registry.navigation->boatPlacements.front();
    preview_=design_;selection_={selected_};rememberSelection();evaluate();return true;
}
bool CoveWorkshop::canPaint() const noexcept {
    return !selection_.empty()&&std::all_of(selection_.begin(),selection_.end(),[&](uint32_t slot){
        return member(preview_.registry,slot)&&!preview_.registry.placements[slot].prototype
            &&isPaintableBrick(definition(slot).nameKey);
    });
}
BrickPaint CoveWorkshop::currentPaint() const noexcept {
    return selected_<preview_.registry.placements.size()
        ?preview_.registry.placements[selected_].paint.value_or(kOriginalBrickPaint):kOriginalBrickPaint;
}
std::optional<uint32_t> CoveWorkshop::paintIndex() const noexcept {
    if(!canPaint())return {};
    const auto paint=currentPaint();
    if(std::any_of(selection_.begin(),selection_.end(),[&](uint32_t p){return preview_.registry.placements[p].paint.value_or(kOriginalBrickPaint)!=paint;}))return {};
    return brickPaintIndex(paint);
}
bool CoveWorkshop::setPaint(uint32_t index) {
    if(!canPaint())return refuse(Problem::UnsupportedOperation,"Paint requires a selection of bricks only.");
    if(index>=kBrickPaintPalette.size())return false;
    auto candidate=preview_;
    for(auto slot:selection_)candidate.registry.placements[slot].paint=kBrickPaintPalette[index].rgba;
    preview_=std::move(candidate);brushPaint_=kBrickPaintPalette[index].rgba;evaluate();
    if(valid())message_=brickToolActive_?"Color ready. Click to place another brick."
        :changed()?"Color ready. Keep, then Launch to apply.":"Selection already has that color.";
    return true;
}
double CoveWorkshop::massKg() const noexcept {
    return compiled_?compiled_->massKg():0;
}
ModuleSettings CoveWorkshop::selectedSettings() const noexcept {
    return preview_.registry.placements[selected_].settings.value_or(defaultModuleSettings(definition(selected_)));
}
bool CoveWorkshop::configurable() const noexcept {
    return !selection_.empty()&&std::all_of(selection_.begin(),selection_.end(),[&](uint32_t slot){
        if(!member(preview_.registry,slot))return false;
        const auto& module=definition(slot).module;
        return std::holds_alternative<PropellerModule>(module)||std::holds_alternative<HelmModule>(module)||std::holds_alternative<WinchModule>(module);
    });
}
bool CoveWorkshop::hasOutputLimit() const noexcept {
    return configurable()&&std::all_of(selection_.begin(),selection_.end(),[&](uint32_t slot){
        return !std::holds_alternative<WinchModule>(definition(slot).module);
    });
}
bool CoveWorkshop::canReverse() const noexcept {
    return configurable()&&std::all_of(selection_.begin(),selection_.end(),[&](uint32_t slot){
        return std::holds_alternative<PropellerModule>(definition(slot).module);
    });
}
bool CoveWorkshop::configure(SettingAction action) {
    if(!configurable()||(action==SettingAction::CycleLimit&&!hasOutputLimit())||(action==SettingAction::Reverse&&!canReverse()))
        return refuse(Problem::UnsupportedOperation,"This setting is not supported by every selected part.");
    if(action!=SettingAction::Toggle&&action!=SettingAction::CycleLimit&&action!=SettingAction::Reverse)return false;
    const auto primary=selectedSettings();auto candidate=preview_;
    for(auto slot:selection_) {
        auto& placement=candidate.registry.placements[slot];
        auto settings=placement.settings.value_or(defaultModuleSettings(definition(slot)));
        switch(action) {
        case SettingAction::Toggle:settings.enabled=!primary.enabled;break;
        case SettingAction::CycleLimit:settings.limitPermille=primary.limitPermille==0?1000:static_cast<uint16_t>((primary.limitPermille-1)/250*250);break;
        case SettingAction::Reverse:settings.reversed=!primary.reversed;break;
        }
        placement.settings=settings;
    }
    preview_=std::move(candidate);evaluate();
    if(valid())message_="Settings ready. Keep, then Launch to apply.";
    return true;
}
bool CoveWorkshop::changed() const noexcept {
    const auto& a=preview_.registry;const auto& b=design_.registry;
    if(!std::is_permutation(a.navigation->boatPlacements.begin(),a.navigation->boatPlacements.end(),b.navigation->boatPlacements.begin(),b.navigation->boatPlacements.end()))return true;
    for(auto slot:a.navigation->boatPlacements) {
        const auto& x=a.placements[slot];const auto& y=b.placements[slot];
        if(x.placement!=y.placement||x.bundleIndex!=y.bundleIndex||x.prototype!=y.prototype
            ||x.paint.value_or(kOriginalBrickPaint)!=y.paint.value_or(kOriginalBrickPaint)
            ||x.settings.value_or(defaultModuleSettings(definition(preview_,slot)))!=y.settings.value_or(defaultModuleSettings(definition(design_,slot))))return true;
    }
    return false;
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
std::vector<uint32_t> CoveWorkshop::affectedParts(const assets::LoadedAssetFixture& candidate) const {
    std::vector<uint32_t> affected;
    for(uint32_t slot=0;slot<candidate.registry.placements.size();++slot) {
        const bool before=member(design_.registry,slot),after=member(candidate.registry,slot);
        if(before!=after||(after&&(slot>=design_.registry.placements.size()
            ||candidate.registry.placements[slot].placement!=design_.registry.placements[slot].placement
            ||candidate.registry.placements[slot].bundleIndex!=design_.registry.placements[slot].bundleIndex
            ||candidate.registry.placements[slot].prototype!=design_.registry.placements[slot].prototype)))affected.push_back(slot);
    }
    return affected;
}
bool CoveWorkshop::reconnect(assets::LoadedAssetFixture& candidate) const {
    return reconnect(candidate,affectedParts(candidate));
}
bool CoveWorkshop::reconnect(assets::LoadedAssetFixture& candidate,std::span<const uint32_t> affected) const {
    auto& r=candidate.registry;
    // Start with the kept graph each time. Moving back to the kept transform
    // must restore its bonds after a previously disconnected preview.
    r.connections=design_.registry.connections;
    const auto touches=[&](uint32_t slot){return std::find(affected.begin(),affected.end(),slot)!=affected.end();};
    std::erase_if(r.connections,[&](const auto& c){return touches(c.aPlacement)||touches(c.bPlacement);});
    struct Socket { GridTransform frame;uint32_t placement;const SocketDefinition* definition; };
    std::vector<Socket> sockets;
    for(auto other:r.navigation->boatPlacements)for(const auto& socket:definition(candidate,other).sockets) {
        if(sockets.size()>=kMaximumBuildSocketRecords)return false;
        const auto frame=compose(r.placements[other].placement,socket.frame);if(!frame)return false;
        sockets.push_back({*frame,other,&socket});
    }
    std::stable_sort(sockets.begin(),sockets.end(),[](const auto& a,const auto& b){return std::tie(a.frame.translation.x,a.frame.translation.y,a.frame.translation.z,a.placement,a.definition->id)<std::tie(b.frame.translation.x,b.frame.translation.y,b.frame.translation.z,b.placement,b.definition->id);});
    for(auto part:affected) {
        if(!member(r,part))continue;
        for(const auto& a:definition(candidate,part).sockets) {
            const auto af=compose(r.placements[part].placement,a.frame);if(!af)return false;
            auto at=std::lower_bound(sockets.begin(),sockets.end(),af->translation,
                [](const Socket& socket,GridPosition p){return std::tie(socket.frame.translation.x,socket.frame.translation.y,socket.frame.translation.z)<std::tie(p.x,p.y,p.z);});
            for(;at!=sockets.end()&&at->frame.translation==af->translation;++at) {
                const auto& b=*at->definition;const auto other=at->placement;
                if(other==part||matchSockets(a,b,ConnectionKind::Weld)!=SocketMatchError::None||compose(af->rotation,mating)!=at->frame.rotation)continue;
                if(std::any_of(r.connections.begin(),r.connections.end(),[&](const auto& c){
                    return (c.aPlacement==part&&c.aSocket==a.id&&c.bPlacement==other&&c.bSocket==b.id)
                        ||(c.bPlacement==part&&c.bSocket==a.id&&c.aPlacement==other&&c.aSocket==b.id);
                }))continue;
                const auto used=[&](uint32_t slot,SocketId socket){return std::count_if(r.connections.begin(),r.connections.end(),
                    [&](const auto& c){return (c.aPlacement==slot&&c.aSocket==socket)||(c.bPlacement==slot&&c.bSocket==socket);});};
                if(used(part,a.id)>=a.connectionCapacity||used(other,b.id)>=b.connectionCapacity)continue;
                if(r.connections.size()>=assets::kMaximumFixtureConnections)return false;
                r.connections.push_back({part,other,a.id,b.id});
            }
        }
    }
    return true;
}
void CoveWorkshop::evaluate() {
    compiled_.reset();issue_={};
    if(!reconnect(preview_)) {(void)refuse(Problem::SocketCapacity,"The selection exceeds the connector limit.");return;}
    std::string error;CoveBoatAssembly::Diagnostic diagnostic;
    if(designSeparated_&&!changed()) {
        compiled_=CoveBoatAssembly::compileSeparatedScene(preview_,error);
        (void)refuse(Problem::NotConnected,"This build has separate sections. Load its protected design or reconnect the parts.");return;
    }
    compiled_=CoveBoatAssembly::compile(preview_,error,&diagnostic);
    if(compiled_){message_=changed()?"Fits. Keep this change or try another socket.":"Selection is connected.";return;}
    const auto problem=diagnostic.assembly.buoyancy.collision.assembly.build.error;
    const auto slot=diagnostic.placement;
    switch(problem) {
    case BuildError::SolidOverlap:(void)refuse(Problem::SolidOverlap,"Blocked by another part. Move the selection or choose another socket.",slot);return;
    case BuildError::ClearanceBlocked:(void)refuse(Problem::ConnectorClearance,"Leave room around the connector.",slot);return;
    case BuildError::InvalidPlacement:(void)refuse(Problem::UnsupportedRotation,"A part is outside the grid or cannot use this orientation.",slot);return;
    case BuildError::InvalidSettings:(void)refuse(Problem::InvalidSettings,"A selected part has unsupported settings.",slot);return;
    case BuildError::Capacity:(void)refuse(Problem::PhysicalLimit,"This design exceeds the part or connector capacity.",slot);return;
    case BuildError::SocketCapacity:(void)refuse(Problem::SocketCapacity,"A connector has no free attachment slots.",slot);return;
    case BuildError::IncompatibleSocket:case BuildError::MisalignedWeld:
        (void)refuse(Problem::IncompatibleSocket,"These connectors do not match. Snap to a compatible socket.",slot);return;
    default:break;
    }
    using Failure=CoveBoatAssembly::Diagnostic::Failure;
    if(diagnostic.failure==Failure::Disconnected){(void)refuse(Problem::NotConnected,"Not connected. Snap the selection to a free socket.");return;}
    if(diagnostic.failure==Failure::DuplicateHelm){(void)refuse(Problem::PhysicalLimit,"A connected boat supports one helm.");return;}
    if(diagnostic.failure==Failure::DuplicatePropeller){(void)refuse(Problem::PhysicalLimit,"A connected boat supports one propeller.");return;}
    if(diagnostic.assembly.buoyancy.collision.assembly.error==AssemblyError::Extent){
        (void)refuse(Problem::OutOfBounds,"This design extends beyond the supported boat size.",slot);return;
    }
    (void)refuse(Problem::PhysicalLimit,"This design exceeds the supported hull or flotation limits.",slot);
}
bool CoveWorkshop::snap() {
    struct Candidate { GridTransform frame; double distance; };
    std::vector<Candidate> candidates;
    const auto current=preview_.registry.placements[selected_].placement;
    size_t visits=0;
    for(const auto& a:definition(selected_).sockets) {
        const auto ai=inverse(a.frame);if(!ai)continue;
        for(auto other:preview_.registry.navigation->boatPlacements) {
            if(isSelected(other))continue;
            for(const auto& b:definition(other).sockets) {
                if(++visits>kMaximumBuildSocketRecords*kMaximumPartSockets){message_="Build exceeds the socket budget.";return false;}
                if(matchSockets(b,a,ConnectionKind::Weld)!=SocketMatchError::None)continue;
                const auto bf=compose(preview_.registry.placements[other].placement,b.frame);
                const auto target=bf?compose(*bf,GridTransform{{},mating}):std::nullopt;
                const auto frame=target?compose(*target,*ai):std::nullopt;
                if(!frame || *frame==current || !(definition(selected_).permittedRotationMask&(1u<<frame->rotation.value)))continue;
                if(definition(selected_).nameKey.starts_with("salvage.part.brick_")&&frame->rotation!=current.rotation)continue;
                if(std::any_of(candidates.begin(),candidates.end(),[&](const auto& c){return c.frame==*frame;}))continue;
                const auto d=checkedSubtract(frame->translation,current.translation);if(!d)continue;
                Candidate option{*frame,double(d->x)*double(d->x)+double(d->y)*double(d->y)+double(d->z)*double(d->z)};
                if(candidates.size()<256)candidates.push_back(option);
                else {
                    const auto farthest=std::max_element(candidates.begin(),candidates.end(),[](const auto& x,const auto& y){return x.distance<y.distance;});
                    if(option.distance<farthest->distance)*farthest=option;
                }
            }
        }
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    for(const auto& option:candidates) {
        auto candidate=preview_;
        const auto oldInverse=inverse(current);const auto delta=oldInverse?compose(option.frame,*oldInverse):std::nullopt;
        if(!delta)continue;
        bool supported=true;
        for(auto slot:selection_) {
            const auto next=compose(*delta,candidate.registry.placements[slot].placement);
            if(!next||!(definition(slot).permittedRotationMask&(1u<<next->rotation.value))){supported=false;break;}
            candidate.registry.placements[slot].placement=*next;
        }
        if(!supported||!reconnect(candidate))continue;
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
        issue_={};message_="Fits. Keep this change or try another socket.";return true;
    }
    message_="No other free socket fits this part.";return false;
}
std::vector<std::byte> CoveWorkshop::blueprintBytes(std::string& error) const {
    // A repeating tool's next brick is only a suggestion. Export the same
    // kept design used for pricing/launch; ordinary unfinished edits refuse.
    if(changed()&&!brickToolActive_){error="Keep or cancel your current change before saving.";return {};}
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
    if(blueprint->parts.size()>assets::kMaximumFixturePlacements || blueprint->connections.size()>assets::kMaximumFixtureConnections)return fail("This design is too large for the cove workshop.");
    for(const auto& link:blueprint->connections)
        if(link.kind!=ConnectionKind::Weld || !link.enabled)return fail("This workshop requires a welded boat design.");
    auto candidate=design_;auto& registry=candidate.registry;
    std::array<bool,assets::kMaximumFixturePlacements> used{};std::array<uint32_t,assets::kMaximumFixturePlacements> slots{};slots.fill(assets::kMaximumFixturePlacements);
    // Reserve every exact owned match before reusing a same-definition part.
    // Imported ordinals never supply ownership, loans, identities or inventory.
    for(bool exact:{true,false})for(size_t i=0;i<blueprint->parts.size();++i) {
        if(slots[i]!=assets::kMaximumFixturePlacements)continue;
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
        if(slots[i]==assets::kMaximumFixturePlacements) {
            uint32_t slot=fixedPlacements_;
            while(slot<assets::kMaximumFixturePlacements && (used[slot] || std::find(acceptedSlots_.begin(),acceptedSlots_.end(),slot)!=acceptedSlots_.end()))++slot;
            if(slot==assets::kMaximumFixturePlacements)return fail("No free part slots. Launch removals before loading a larger design.");
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
    if(registry.connections.size()>assets::kMaximumFixtureConnections)return fail("This design has too many connections for the cove workshop.");
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
    auto history=history_;if(history.size()==maximumHistory)history.erase(history.begin());history.push_back(currentHistory());
    auto preview=candidate;const auto primary=candidate.registry.navigation->boatPlacements.front();
    std::vector<uint32_t> selection{primary},kept=selection;
    design_=std::move(candidate);preview_=std::move(preview);compiled_=std::move(compiled);history_=std::move(history);
    selected_=keptPrimary_=primary;selection_=std::move(selection);keptSelection_=std::move(kept);future_.clear();brickToolActive_=false;designSeparated_=false;++revision_;
    issue_={};message_="Design loaded. Check the cost, then Launch to build it.";error.clear();return true;
}

bool CoveWorkshop::command(Action action) {
    if(brickToolActive_ && (action==Action::Previous || action==Action::Next || action==Action::Undo
        || action==Action::Redo || action==Action::Revert || action==Action::Remove)) {
        (void)stopBrickTool();
        if(action==Action::Revert || action==Action::Remove)return true;
    }
    if(action==Action::Previous || action==Action::Next) {
        if(!cleanSelection())return false;
        const auto& parts=design_.registry.navigation->boatPlacements;
        const auto found=std::find(parts.begin(),parts.end(),selected_);
        const auto i=found==parts.end()?0u:static_cast<size_t>(found-parts.begin());
        return selectPart(parts[(i+parts.size()+(action==Action::Next?1:parts.size()-1))%parts.size()]);
    }
    if(action==Action::Revert) {
        auto candidate=design_;auto selection=keptSelection_;
        preview_=std::move(candidate);selection_=std::move(selection);selected_=keptPrimary_;normalizeSelection();evaluate();return true;
    }
    if(action==Action::Undo || action==Action::Redo) {
        if(changed())return refuse(Problem::PendingEdit,"Keep or cancel this change before using history.");
        const bool redo=action==Action::Redo;const auto& from=redo?future_:history_;
        if(from.empty()){issue_={};message_=redo?"Nothing to redo.":"Nothing to undo.";return false;}
        if(revision_==std::numeric_limits<uint64_t>::max())return refuse(Problem::HistoryLimit,"Design history is full.");
        auto candidate=design_;candidate.registry=from.back().registry;std::string error;
        const bool separated=from.back().separated;
        auto assembly=separated?CoveBoatAssembly::compileSeparatedScene(candidate,error):CoveBoatAssembly::compile(candidate,error);
        if(!assembly)return refuse(Problem::PhysicalLimit,"This history entry cannot be restored.");
        auto preview=candidate;auto selection=from.back().selection;auto kept=selection;const auto primary=from.back().primary;
        auto history=history_,future=future_;
        if(redo){history.push_back(currentHistory());future.pop_back();}
        else {future.push_back(currentHistory());history.pop_back();}
        design_=std::move(candidate);preview_=std::move(preview);compiled_=std::move(assembly);
        history_=std::move(history);future_=std::move(future);selection_=std::move(selection);keptSelection_=std::move(kept);
        selected_=keptPrimary_=primary;designSeparated_=separated;++revision_;issue_={};
        if(separated){issue_.problem=Problem::NotConnected;message_="History restored. This boat still has separate sections.";}
        else message_=redo?"Change redone.":"Change undone.";
        return true;
    }
    if(action==Action::Keep) {
        if(!valid()||!changed())return false;
        if(revision_==std::numeric_limits<uint64_t>::max())return refuse(Problem::HistoryLimit,"Design history is full.");
        auto accepted=preview_;auto history=history_;auto kept=selection_;
        if(history.size()==maximumHistory)history.erase(history.begin());
        history.push_back(currentHistory());design_=std::move(accepted);history_=std::move(history);future_.clear();++revision_;
        keptSelection_=std::move(kept);keptPrimary_=selected_;brickToolActive_=false;designSeparated_=false;issue_={};
        message_="Design kept. Launch to sail these changes.";return true;
    }
    if(action==Action::Snap)return snap();
    if(action==Action::Remove) {
        // Removing an unplaced ordinary Add preview is cancellation. A kept
        // group removal must leave at least one physical member in the boat.
        if(std::all_of(selection_.begin(),selection_.end(),[&](uint32_t slot){return !member(design_.registry,slot);}))
            return command(Action::Revert);
        if(changed())return refuse(Problem::PendingEdit,"Keep or cancel this change before removing parts.");
        if(design_.registry.navigation->boatPlacements.size()<=selection_.size())
            return refuse(Problem::InvalidSelection,"Keep at least one part in the boat.");
        auto candidate=design_;
        std::erase_if(candidate.registry.navigation->boatPlacements,[&](uint32_t slot){return isSelected(slot);});
        auto selection=std::vector<uint32_t>{candidate.registry.navigation->boatPlacements.front()};
        preview_=std::move(candidate);selection_=std::move(selection);selected_=selection_.front();evaluate();return true;
    }
    if(action==Action::Rotate)return rotateSelection(Axis::Y);
    GridPosition offset{};
    switch(action) {
    case Action::Left:offset.x=-50;break;case Action::Right:offset.x=50;break;
    case Action::Forward:offset.z=-50;break;case Action::Back:offset.z=50;break;
    case Action::Raise:offset.y=16;break;case Action::Lower:offset.y=-16;break;
    default:return false;
    }
    return moveSelection(offset);
}
} // namespace voxy::game::expedition
