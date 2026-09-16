#include "game/adventure/town_residents.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
const std::array<TownResidentDefinition,kTownResidentCount> definitions{{
    {1,"Moss","Builder",{-65.5,-893.5},{-63,-895},{.29f,.58f,.40f}},
    {2,"Rivet","Outfitter",{-60.5,-893.5},{-63,-895},{.86f,.37f,.24f}},
    {3,"Lumen","Beacon keeper",{-65,-898},{-63,-975},{.16f,.54f,.60f}},
}};
constexpr std::array<glm::dvec2,9> offsets{{{0,0},{.75,0},{-.75,0},{0,.75},{0,-.75},
    {.75,.75},{.75,-.75},{-.75,.75},{-.75,-.75}}};
constexpr double skin=.005,playerMargin=.05;
// The published body box encloses the .3m actor capsule. Sampling its diagonal
// footprint keeps the box corners above studs as well as the actor's center.
const double footprintRadius=TownResidents::bodyRadius*std::sqrt(2.);
bool finite(glm::dvec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Solid body(construction::WorldNamespace world,const TownResidentPose& resident) {
    return {{world,1},{world,residentPartId(resident.id)},resident.feet-glm::dvec3(TownResidents::bodyRadius,0,TownResidents::bodyRadius),
        resident.feet+glm::dvec3(TownResidents::bodyRadius,TownResidents::bodyHeight,TownResidents::bodyRadius)};
}
bool overlaps(const Solid& a,const Solid& b,double margin=0) {
    return glm::all(glm::lessThan(a.minimum,b.maximum+glm::dvec3(margin)))
        &&glm::all(glm::lessThan(b.minimum,a.maximum+glm::dvec3(margin)));
}
bool protectedFootprint(glm::dvec2 p,double radius) {
    const auto& world=installedWorld();
    for(double x:{-radius,radius})for(double z:{-radius,radius})
        if(glm::length(p+glm::dvec2(x,z)-world.town)>=world.townProtectedRadius)return false;
    return true;
}
bool legacyIdentityCollision(const AdventureState& state,uint32_t id) {
    const auto reserved=residentPartId(id);
    for(const auto& structure:state.structures) {
        if(structure.id==reserved)return true;
        for(const auto& part:structure.parts)if(part.id==reserved)return true;
    }
    for(const auto& component:state.components)if(component.id==reserved)return true;
    return false;
}
bool clearActors(glm::dvec3 feet,const TownResidentPose& candidate,
    const std::array<TownResidentPose,kTownResidentCount>& residents,construction::WorldNamespace world) {
    Solid player{{},{},feet-glm::dvec3(.3,0,.3),feet+glm::dvec3(.3,1.7,.3)};
    if(overlaps(player,body(world,candidate),playerMargin))return false;
    for(const auto& other:residents)if(other.available&&other.id!=candidate.id)
        if(overlaps(player,body(world,other),playerMargin))return false;
    return true;
}
bool approach(TownResidentPose& candidate,const AdventureSpatialQueries& base,
    const std::array<TownResidentPose,kTownResidentCount>& residents,construction::WorldNamespace world) {
    const auto plaza=installedWorld().town;
    const glm::dvec2 position(candidate.feet.x,candidate.feet.z);
    const auto toward=plaza-position;const double distance=glm::length(toward);
    if(distance<1.1)return false;
    const auto target=position+toward*(.95/distance);
    if(!protectedFootprint(target,AdventurePlayer::radius))return false;
    AdventurePlayer walker;
    if(!walker.initialize(base,townSpawn(base.terrain()),installedWorld().waterHeight))return false;
    // At most120 ordinary fixed steps (7.2m); the plaza is less than5m away.
    // The actual controller checks terrain rises, static headroom and sliding.
    for(int tick=0;tick<120;++tick) {
        const auto feet=walker.feet();
        if(!clearActors(feet,candidate,residents,world))return false;
        const glm::dvec2 delta=target-glm::dvec2(feet.x,feet.z);
        if(glm::length(delta)<.065) {
            if(walker.mode()!=AdventurePlayer::Mode::Walking)return false;
            candidate.approach=feet;
            const auto eye=feet+glm::dvec3(0,AdventurePlayer::eyeHeight,0);
            const auto ray=candidate.feet+glm::dvec3(0,1.35,0)-eye;
            const auto hit=base.raycast(eye,ray,glm::length(ray));
            return hit.complete&&!hit.hit;
        }
        walker.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
    }
    return false;
}
} // namespace

std::span<const TownResidentDefinition> residentDefinitions() noexcept {return definitions;}
const TownResidentDefinition* residentDefinition(uint32_t id) noexcept {
    return id>=1&&id<=definitions.size()?&definitions[id-1]:nullptr;
}
TownResidents TownResidents::admit(const AdventureState& state,const AdventureSpatialQueries& base,const TownResidents* previous) {
    TownResidents result;result.world_=state.world;
    for(size_t i=0;i<result.entries_.size();++i)result.entries_[i].id=definitions[i].id;
    if(!construction::isValid(state.world)||!AdventureSession::validPose(state.player)||!base.terrain().valid())return result;
    if(previous&&previous->world_==state.world)result.entries_=previous->entries_;
    const glm::dvec3 player(state.player.x,state.player.y,state.player.z);
    const Solid playerBox{{},{},player-glm::dvec3(.3,0,.3),player+glm::dvec3(.3,1.7,.3)};
    const auto spawn=townSpawn(base.terrain());
    const Solid recoveryBox{{},{},spawn-glm::dvec3(.3,0,.3),spawn+glm::dvec3(.3,1.7,.3)};
    for(auto& resident:result.entries_) {
        if(legacyIdentityCollision(state,resident.id)) {resident.available=false;continue;}
        if(resident.available)continue;
        const auto& definition=definitions[resident.id-1];
        for(const auto offset:offsets) {
            const auto xz=definition.anchor+offset;
            if(!protectedFootprint(xz,bodyRadius))continue;
            const double ground=double(terrain::lego::supportHeight(base.terrain(),glm::vec2(xz),float(footprintRadius)));
            TownResidentPose candidate{resident.id,{xz.x,ground+skin,xz.y},{},0,true};
            if(!finite(candidate.feet)||ground<=double(installedWorld().waterHeight)+.05
                ||!base.clearCapsule(candidate.feet,footprintRadius,bodyHeight))continue;
            const auto collider=body(state.world,candidate);
            if(overlaps(collider,playerBox,playerMargin)||overlaps(collider,recoveryBox,playerMargin))continue;
            bool occupied=false;
            for(const auto& other:result.entries_)if(other.available&&other.id!=resident.id)
                occupied=occupied||overlaps(collider,body(state.world,other),playerMargin);
            if(occupied||!approach(candidate,base,result.entries_,state.world))continue;
            const auto facing=definition.facingTarget-xz;
            candidate.yaw=std::atan2(-facing.x,-facing.y);
            resident=candidate;break;
        }
    }
    return result;
}
const TownResidentPose* TownResidents::find(uint32_t id) const noexcept {
    return id>=1&&id<=entries_.size()?&entries_[id-1]:nullptr;
}
bool TownResidents::appendSolids(construction::WorldNamespace world,std::vector<Solid>& solids) const {
    if(!construction::isValid(world)||world!=world_)return false;
    const auto count=size_t(std::count_if(entries_.begin(),entries_.end(),[](const auto& p){return p.available;}));
    if(solids.size()>AdventureSpatialQueries::maximumSolids||count>AdventureSpatialQueries::maximumSolids-solids.size())return false;
    solids.reserve(solids.size()+count);
    for(const auto& resident:entries_)if(resident.available)solids.push_back(body(world,resident));
    return true;
}
bool TownResidents::interactable(uint32_t id,const PlayerPose& player,const AdventureSpatialQueries& queries,std::string& error) const {
    const auto* resident=find(id);
    if(!resident||!resident->available) {error="This resident is not available yet.";return false;}
    if(!AdventureSession::validPose(player)) {error="Player position is unavailable.";return false;}
    const auto eye=glm::dvec3(player.x,player.y+AdventurePlayer::eyeHeight,player.z);
    const auto delta=resident->feet+glm::dvec3(0,1.35,0)-eye;const double distance=glm::length(delta);
    if(distance>interactionRange) {error="Move closer to talk.";return false;}
    if(distance>1e-8) {
        const auto hit=queries.raycast(eye,delta,distance);
        if(!hit.complete||(hit.hit&&(hit.terrain||hit.structure!=construction::DurableId{world_,1}
            ||hit.part!=construction::DurableId{world_,residentPartId(id)}))) {error="The resident is blocked.";return false;}
    }
    error.clear();return true;
}
uint32_t TownResidents::nearestInteractable(const PlayerPose& player,const AdventureSpatialQueries& queries,uint64_t aimedPart) const {
    double best=interactionRange+.3;uint32_t id=0;
    for(const auto& resident:entries_) {
        std::string error;if(!interactable(resident.id,player,queries,error))continue;
        double distance=glm::length(resident.feet-glm::dvec3(player.x,player.y,player.z));
        if(residentPartId(resident.id)==aimedPart)distance-=.25;
        if(distance<best){best=distance;id=resident.id;}
    }
    return id;
}

} // namespace voxy::game::adventure
