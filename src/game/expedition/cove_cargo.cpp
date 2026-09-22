#include "game/expedition/cove_cargo.hpp"
#include "game/expedition/cove_save.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::game::expedition {
static_assert(CoveCargoSet::maximumLoads==kMaximumCoveSavedCargo);

std::optional<CoveCargoSet> CoveCargoSet::prepare(const assets::LoadedAssetFixture& scene,std::string& error) {
    const auto fail=[&](const char* message)->std::optional<CoveCargoSet>{error=message;return {};};
    if(!scene.registry.navigation)return fail("Cargo requires installed navigation.");
    const auto& placements=scene.registry.navigation->cargoPlacements;
    if(placements.size()>maximumLoads)return fail("This cove has too many mission loads.");
    CoveCargoSet result;
    for(size_t i=0;i<placements.size();++i) {
        if(std::find(placements.begin(),placements.begin()+static_cast<std::ptrdiff_t>(i),placements[i])!=placements.begin()+static_cast<std::ptrdiff_t>(i)
            ||std::find(scene.registry.navigation->boatPlacements.begin(),scene.registry.navigation->boatPlacements.end(),placements[i])!=scene.registry.navigation->boatPlacements.end())
            return fail("Mission loads must have independent scene placements.");
        auto& load=result.loads_[i];load.assembly=CoveBoatAssembly::compileCargo(scene,placements[i],error);
        if(!load.assembly)return {};
        const auto& functions=load.assembly->assembly().functions();bool eye=false;
        for(const auto& frame:functions.frames())if(frame.kind==construction::AssemblyFrameKind::TowEye) {
            if(eye||frame.socket>=functions.sockets().size())return fail("Cargo has an ambiguous tow eye.");
            const auto p=frame.rootFromFrame.translation;load.towPoint=glm::vec3(p.x,p.y,p.z)*.02f;
            load.towStrength=static_cast<float>(functions.sockets()[frame.socket].definition.strength.tensionNewtons);eye=true;
        }
        if(!eye||!std::isfinite(load.towStrength)||load.towStrength<=0)return fail("Cargo needs a usable tow eye.");
        ++result.count_;
    }
    error.clear();return result;
}
bool CoveCargoSet::allAdmitted() const noexcept {
    return count_>0&&std::all_of(loads().begin(),loads().end(),[](const auto& load){return load.body.valid()&&!load.retired;});
}
bool CoveCargoSet::allRetired() const noexcept {
    return std::all_of(loads().begin(),loads().end(),[](const auto& load){return load.retired&&!load.body.valid()&&!load.replacedBody.valid();});
}
uint64_t CoveCargoSet::joinedTick() const noexcept {
    if(!allAdmitted())return 0;
    const auto tick=primary().observedTick;
    return std::all_of(loads().begin(),loads().end(),[&](const auto& load){return load.observedTick==tick
        &&tick>=load.admissionTick&&!load.replacedBody.valid();})?tick:0;
}
void CoveCargoSet::includeBodyRange(uint32_t& first,uint32_t& last) const noexcept {
    for(const auto& load:loads())for(const auto body:{load.body,load.replacedBody})if(body.valid()) {
        first=std::min(first,body.index);last=std::max(last,body.index);
    }
}
bool CoveCargoSet::anySecuring() const noexcept {
    return std::any_of(loads().begin(),loads().end(),[](const auto& load){return load.securing;});
}
} // namespace voxy::game::expedition
