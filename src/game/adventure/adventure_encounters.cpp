#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
constexpr double skin=.005;
const std::array<EncounterDefinition,kAdventureEncounterCount> definitions{{
    {1,"Bramble trail raider",{-58,-945},60,12,7,12,1.6,.7,.9,{ItemKind::Scrap,8},false,{.61f,.26f,.16f}},
    {2,"Beacon trail raider",{-55,-967},100,18,7,12,1.6,.7,.9,{ItemKind::RelayCore,1},true,{.33f,.25f,.46f}},
}};
constexpr std::array<glm::dvec2,9> offsets{{{0,0},{.75,0},{-.75,0},{0,.75},{0,-.75},
    {.75,.75},{.75,-.75},{-.75,.75},{-.75,-.75}}};
constexpr std::array<glm::dvec2,4> directions{{{1,0},{-1,0},{0,1},{0,-1}}};
bool finite(glm::dvec3 p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
glm::dvec3 feet(PlayerPose p) noexcept {return {p.x,p.y,p.z};}
Solid body(construction::WorldNamespace world,uint32_t id,glm::dvec3 p) noexcept {
    return {{world,encounterStructureId(id)},{world,encounterPartId(id)},
        p-glm::dvec3(AdventureEncounters::bodyRadius,0,AdventureEncounters::bodyRadius),
        p+glm::dvec3(AdventureEncounters::bodyRadius,AdventureEncounters::bodyHeight,AdventureEncounters::bodyRadius)};
}
bool overlaps(const Solid& a,const Solid& b,glm::dvec3 margin={}) noexcept {
    return glm::all(glm::lessThan(a.minimum,b.maximum+margin))
        &&glm::all(glm::lessThan(b.minimum,a.maximum+margin));
}
bool actorOverlap(glm::dvec3 p,const Solid& actor,double radius,double height,double margin=0) noexcept {
    const double low=p.y+radius,high=p.y+height-radius;
    glm::dvec3 gap(p.x-std::clamp(p.x,actor.minimum.x,actor.maximum.x),0,
        p.z-std::clamp(p.z,actor.minimum.z,actor.maximum.z));
    if(high<actor.minimum.y)gap.y=high-actor.minimum.y;
    else if(low>actor.maximum.y)gap.y=low-actor.maximum.y;
    return glm::length(gap)<radius+margin-1e-7;
}
bool clearStanding(uint32_t id,glm::dvec3 p,const AdventureSpatialQueries& base) noexcept {
    const auto* definition=encounterDefinition(id);
    if(!definition||!base.revision()||!base.terrain().valid()||!finite(p)
        ||glm::length(glm::dvec2(p.x,p.z)-definition->anchor)>definition->leashRadius
        ||p.y<=double(installedWorld().waterHeight)+.05)return false;
    const auto box=body({},id,p);
    if(protectedConstruction(box.minimum,box.maximum)
        ||!base.clearCapsule(p,AdventureEncounters::bodyRadius,AdventureEncounters::bodyHeight))return false;
    const double support=base.supportHeight({p.x,p.z},AdventureEncounters::bodyRadius,p.y+.01);
    return std::isfinite(support)&&std::abs(p.y-support)<=.025;
}
bool ground(uint32_t id,glm::dvec2 xz,const AdventureSpatialQueries& base,PlayerPose& output) noexcept {
    if(!base.terrain().valid())return false;
    const double height=double(terrain::lego::supportHeight(base.terrain(),glm::vec2(xz),float(AdventureEncounters::bodyRadius)));
    PlayerPose candidate{xz.x,height+skin,xz.y,0};
    if(!clearStanding(id,feet(candidate),base))return false;
    // The original minifigure faces the established meadow trail, negative Z
    // being the shared actor/camera forward convention.
    const auto toward=glm::dvec2(-63,xz.y)-xz;
    candidate.yaw=glm::length(toward)>1e-8?std::atan2(-toward.x,-toward.y):0;
    output=candidate;return true;
}
bool localWalkingFootprint(uint32_t id,PlayerPose candidate,const AdventureSpatialQueries& base) {
    const auto origin=feet(candidate);
    for(const auto direction:directions) {
        AdventurePlayer probe;if(!probe.initialize(base,origin,double(installedWorld().waterHeight),candidate.yaw))return false;
        for(const auto target:std::array<glm::dvec2,2>{{glm::dvec2(origin.x,origin.z)+direction,glm::dvec2(origin.x,origin.z)}}) {
            bool reached=false;
            for(int tick=0;tick<40;++tick) {
                const auto p=probe.feet();const auto delta=target-glm::dvec2(p.x,p.z);const double distance=glm::length(delta);
                if(distance<.008){reached=probe.mode()==AdventurePlayer::Mode::Walking;break;}
                const double amount=std::min(1.,distance/(3.6*AdventurePlayer::fixedStep));
                probe.advance(AdventurePlayer::fixedStep,{delta*(amount/distance),false});
                if(probe.mode()!=AdventurePlayer::Mode::Walking||!clearStanding(id,probe.feet(),base))return false;
            }
            if(!reached)return false;
        }
    }
    return true;
}
bool identityCollision(const AdventureState& state,const AdventureSpatialQueries& base,uint32_t id) {
    const auto structure=encounterStructureId(id),part=encounterPartId(id);
    const auto reserved=[&](uint64_t value){return value==structure||value==part;};
    for(const auto& s:state.structures) {
        if(reserved(s.id))return true;
        for(const auto& p:s.parts)if(reserved(p.id))return true;
    }
    for(const auto& c:state.components)if(reserved(c.id))return true;
    for(const auto& solid:base.solids())
        if((solid.structure.world==state.world&&reserved(solid.structure.counter))
            ||(solid.part.world==state.world&&reserved(solid.part.counter)))return true;
    return false;
}
bool playerOwned(const AdventureState& state,const Solid& solid) {
    return solid.structure.world==state.world&&std::any_of(state.structures.begin(),state.structures.end(),
        [&](const auto& structure){return structure.id==solid.structure.counter;});
}
bool safeCandidate(const AdventureState& state,const AdventureSpatialQueries& base,
    const AdventureEncounters& accepted,const AdmittedEncounter& candidate,bool fresh) {
    const auto p=feet(candidate.pose);
    if(!clearStanding(candidate.id,p,base))return false;
    const auto box=body(state.world,candidate.id,p);
    if(actorOverlap(feet(state.player),box,AdventurePlayer::radius,AdventurePlayer::height,fresh?AdventureEncounters::playerMargin:0)
        ||actorOverlap(townSpawn(base.terrain()),box,AdventurePlayer::radius,AdventurePlayer::height,AdventureEncounters::playerMargin)
        ||(state.registeredBed&&actorOverlap(feet(state.recovery),box,AdventurePlayer::radius,AdventurePlayer::height,AdventureEncounters::playerMargin))
        ||accepted.occupied(p,AdventureEncounters::bodyRadius+AdventureEncounters::playerMargin,
            AdventureEncounters::bodyHeight,candidate.id))return false;
    for(const auto& solid:base.solids()) {
        // New actors also preserve a home's doorway approach. Existing moving
        // checkpoints keep their valid close-to-wall pose; only actual body
        // overlap defers them, so a corridor does not make a chasing actor vanish.
        if(fresh) {
            const auto margin=playerOwned(state,solid)?glm::dvec3(.75,.001,.75):glm::dvec3(0);
            if(overlaps(box,solid,margin))return false;
        }
    }
    return !fresh||localWalkingFootprint(candidate.id,candidate.pose,base);
}
} // namespace

std::span<const EncounterDefinition> encounterDefinitions() noexcept {return definitions;}
const EncounterDefinition* encounterDefinition(uint32_t id) noexcept {
    return id>=1&&id<=definitions.size()?&definitions[id-1]:nullptr;
}
bool defaultEncounterContent(const AdventureSpatialQueries& base,
    std::array<EncounterContent,kAdventureEncounterCount>& output,std::string& error) {
    std::array<EncounterContent,kAdventureEncounterCount> result{};
    for(size_t i=0;i<definitions.size();++i) {
        const auto& definition=definitions[i];PlayerPose spawn;
        if(!ground(definition.id,definition.anchor,base,spawn)||!localWalkingFootprint(definition.id,spawn,base)) {
            error="The trail raider's installed terrain approach is unavailable.";return false;
        }
        result[i]={definition.id,1,spawn,definition.maximumHealth,definition.loot,definition.grantsRelayCore};
    }
    output=result;error.clear();return true;
}
AdventureEncounters AdventureEncounters::admit(const AdventureState& state,const AdventureSpatialQueries& base) {
    AdventureEncounters result;result.world_=state.world;
    for(size_t i=0;i<result.entries_.size();++i)result.entries_[i].id=definitions[i].id;
    if(!construction::isValid(state.world)||!AdventureSession::validPose(state.player)
        ||(state.registeredBed&&!AdventureSession::validPose(state.recovery))||!base.terrain().valid()||!base.revision())return result;
    for(size_t i=0;i<result.entries_.size();++i) {
        auto& entry=result.entries_[i];const auto& checkpoint=state.combat.encounters[i].checkpoint;
        const auto& definition=definitions[i];entry.pose=checkpoint.pose;entry.health=checkpoint.health;
        if(checkpoint.encounterId!=definition.id||checkpoint.generation!=1||!checkpoint.health
            ||checkpoint.health>definition.maximumHealth||checkpoint.phase==EnemyPhase::Dead
            ||!AdventureSession::validPose(checkpoint.pose)||identityCollision(state,base,entry.id))continue;
        if(checkpoint.positioned) {
            entry.available=safeCandidate(state,base,result,entry,false);continue;
        }
        if(checkpoint.phase!=EnemyPhase::Dormant)continue;
        for(const auto offset:offsets) {
            auto candidate=entry;
            if(!ground(entry.id,definition.anchor+offset,base,candidate.pose))continue;
            if(!safeCandidate(state,base,result,candidate,true))continue;
            candidate.available=true;candidate.needsPositionCheckpoint=true;entry=candidate;break;
        }
    }
    return result;
}
const AdmittedEncounter* AdventureEncounters::find(uint32_t id) const noexcept {
    return id>=1&&id<=entries_.size()?&entries_[id-1]:nullptr;
}
bool AdventureEncounters::appendSolids(construction::WorldNamespace world,std::vector<Solid>& solids) const {
    if(!construction::isValid(world)||world!=world_)return false;
    const auto count=size_t(std::count_if(entries_.begin(),entries_.end(),[](const auto& entry){return entry.available;}));
    if(solids.size()>AdventureSpatialQueries::maximumSolids||count>AdventureSpatialQueries::maximumSolids-solids.size())return false;
    for(const auto& entry:entries_)if(entry.available)for(const auto& solid:solids)
        if(solid.part==construction::DurableId{world,encounterPartId(entry.id)}
            ||solid.structure==construction::DurableId{world,encounterStructureId(entry.id)})return false;
    solids.reserve(solids.size()+count);
    for(const auto& entry:entries_)if(entry.available)solids.push_back(body(world,entry.id,feet(entry.pose)));
    return true;
}
bool AdventureEncounters::occupied(glm::dvec3 p,double radius,double height,uint32_t exceptId) const noexcept {
    if(!finite(p)||!std::isfinite(radius)||!std::isfinite(height)||radius<=0||height<2*radius)return true;
    for(const auto& entry:entries_)if(entry.available&&entry.id!=exceptId)
        if(actorOverlap(p,body(world_,entry.id,feet(entry.pose)),radius,height))return true;
    return false;
}
bool AdventureEncounters::movementAllowed(uint32_t id,glm::dvec3 p,const AdventureSpatialQueries& base) const noexcept {
    const auto* entry=find(id);return entry&&entry->available&&clearStanding(id,p,base);
}
bool AdventureEncounters::safeRest(PlayerPose player,const AdventureSpatialQueries& base) const noexcept {
    if(!AdventureSession::validPose(player)||!base.clearCapsule(feet(player))||occupied(feet(player)))return false;
    const double support=base.supportHeight({player.x,player.z},AdventurePlayer::radius,player.y+.01);
    if(!std::isfinite(support)||std::abs(player.y-support)>.025||player.y<double(installedWorld().waterHeight)+.05)return false;
    for(const auto& entry:entries_)if(entry.available) {
        const auto* definition=encounterDefinition(entry.id);
        if(glm::length(feet(entry.pose)-feet(player))<=definition->noticeRadius)return false;
    }
    return true;
}
} // namespace voxy::game::adventure
