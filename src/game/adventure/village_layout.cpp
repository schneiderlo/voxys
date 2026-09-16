#include "game/adventure/village_layout.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
bool overlap(const Solid& a,const Solid& b,double margin=0) {
    return glm::all(glm::lessThan(a.minimum,b.maximum+glm::dvec3(margin)))
        &&glm::all(glm::lessThan(b.minimum,a.maximum+glm::dvec3(margin)));
}
std::pair<double,double> groundRange(const terrain::lego::Surface& s,glm::dvec2 minimum,glm::dvec2 maximum) {
    const glm::dvec2 origin(s.origin());const double cell=double(s.cellScale);
    const int x0=int(std::floor((minimum.x+origin.x)/cell)),z0=int(std::floor((minimum.y+origin.y)/cell));
    const int x1=int(std::floor((maximum.x+origin.x-1e-7)/cell)),z1=int(std::floor((maximum.y+origin.y-1e-7)/cell));
    if(x0<0||z0<0||x1>=int(s.width)-1||z1>=int(s.height)-1)return {INFINITY,INFINITY};
    double low=INFINITY,high=-INFINITY;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
        const double top=double(s.cellTop(x,z));low=std::min(low,top);high=std::max(high,top);
        const glm::dvec2 center((x+.5)*cell-origin.x,(z+.5)*cell-origin.y);
        if(glm::length(center-glm::clamp(center,minimum,maximum))<=double(terrain::lego::kStudRadius)*cell)
            high=std::max(high,top+double(terrain::lego::kStudHeight)*cell);
    }
    return {low,high};
}
GridPosition grid(glm::dvec3 p) {return {int32_t(std::round(p.x*50)),int32_t(std::round(p.y*50)),int32_t(std::round(p.z*50))};}
void piece(VillageGroup& group,PieceKind kind,glm::dvec3 p,uint8_t yaw=0) {group.pieces.push_back({kind,grid(p),yaw});}
void propBoxes(const VillageProp& prop,std::vector<std::pair<glm::dvec3,glm::dvec3>>& boxes) {
    switch(prop.kind) {
    case VillagePropKind::Roof:
        for(int side:{-1,1})for(int i=0;i<4;++i) {
            const double a=double(i)*.575,b=double(i+1)*.575;
            boxes.push_back({{side>0?a:-b,0,-2.3},{side>0?b:-a,.32*double(4-i),2.3}});
        }
        break;
    case VillagePropKind::Planter:boxes.push_back({{-.45,0,-.45},{.45,.5,.45}});break;
    case VillagePropKind::Tree:
        boxes.push_back({{-.22,0,-.22},{.22,2.4,.22}});
        boxes.push_back({{-1.3,2.4,-1.3},{1.3,4.2,1.3}});break;
    case VillagePropKind::PathTile:boxes.push_back({{-.32,0,-.32},{.32,.08,.32}});break;
    }
}
bool compileGroup(VillageGroup& group,construction::WorldNamespace world) {
    AdventureState state;state.world=world;state.structures.push_back({villageStructureId(group.id),1,0,{}, {}});
    uint8_t index=1;
    for(const auto& p:group.pieces)state.structures.front().parts.push_back({villagePartId(group.id,index++),p.kind,p.position,p.yawQuarterTurns,0});
    std::string error;if(!compileSolids(state,group.solids,error))return false;
    for(size_t i=0;i<group.props.size();++i) {
        const auto& p=group.props[i];std::vector<std::pair<glm::dvec3,glm::dvec3>> boxes;propBoxes(p,boxes);
        for(const auto& [lo,hi]:boxes) {
            glm::dvec3 minimum(INFINITY),maximum(-INFINITY);
            for(double x:{lo.x,hi.x})for(double y:{lo.y,hi.y})for(double z:{lo.z,hi.z}) {
                const glm::dvec3 point=p.feet+(p.yawQuarterTurns==1?glm::dvec3(z,y,-x):p.yawQuarterTurns==2?glm::dvec3(-x,y,-z):p.yawQuarterTurns==3?glm::dvec3(-z,y,x):glm::dvec3(x,y,z));
                minimum=glm::min(minimum,point);maximum=glm::max(maximum,point);
            }
            group.solids.push_back({{world,villageStructureId(group.id)},{world,villagePartId(group.id,static_cast<uint8_t>(65+i))},minimum,maximum});
        }
    }
    if(group.solids.empty())return false;
    group.minimum=glm::dvec3(INFINITY);group.maximum=glm::dvec3(-INFINITY);
    for(const auto& b:group.solids){group.minimum=glm::min(group.minimum,b.minimum);group.maximum=glm::max(group.maximum,b.maximum);}
    return true;
}
VillageGroup shelter(uint8_t id,glm::dvec2 center,bool stall,const terrain::lego::Surface& terrain) {
    VillageGroup group;group.id=id;group.name=stall?"Market shelter":"Meadow cottage";
    const auto [low,high]=groundRange(terrain,center-glm::dvec2(2.32),center+glm::dvec2(2.32));
    if(!std::isfinite(high)||high-low>1.25||low<=double(installedWorld().waterHeight))return group;
    const double floor=std::ceil((high+.035)*50)/50;
    const int layers=std::clamp(int(std::ceil((floor-low)/.32)),1,5);
    for(int layer=1;layer<=layers;++layer)for(double x:{-1.,1.})for(double z:{-1.,1.})
        piece(group,PieceKind::Foundation,{center.x+x,floor-.32*layer,center.y+z});
    for(double x:{-1.,1.})piece(group,PieceKind::Wall,{center.x+x,floor,center.y-1.84});
    if(!stall) {
        piece(group,PieceKind::Doorway,{center.x-1,floor,center.y+1.84});
        piece(group,PieceKind::Wall,{center.x+1,floor,center.y+1.84});
    }
    for(double x:{-2.16,2.16})for(double z:{-1.,1.})if(!stall||z<0)
        piece(group,PieceKind::Wall,{center.x+x,floor,center.y+z},1);
    if(stall)for(double x:{-1.76,1.76})for(int level=0;level<3;++level)
        piece(group,PieceKind::Pier,{center.x+x,floor+.96*level,center.y+1.76});
    for(double x:{-1.,1.})for(double z:{-1.,1.})piece(group,PieceKind::Roof,{center.x+x,floor+2.88,center.y+z});
    const double entryX=center.x+(stall?1.:-1.);
    piece(group,PieceKind::Stair,{entryX,floor-.96,center.y+3});
    group.props.push_back({VillagePropKind::Roof,{center.x,floor+3.2,center.y},0});
    const double side=stall?1.:-1.;
    group.route={{installedWorld().town.x+side*5.3,0,installedWorld().town.y},
        {entryX,0,installedWorld().town.y},{entryX,0,center.y+4.4},{entryX,0,center.y+1}};
    return group;
}
VillageGroup decoration(uint8_t id,std::string_view name,VillagePropKind kind,glm::dvec2 center,const terrain::lego::Surface& terrain) {
    VillageGroup group;group.id=id;group.name=name;
    const double radius=kind==VillagePropKind::Tree?.22:.45;
    const auto [low,high]=groundRange(terrain,center-glm::dvec2(radius),center+glm::dvec2(radius));
    if(!std::isfinite(high)||high-low>.7||low<=double(installedWorld().waterHeight))return group;
    group.props.push_back({kind,{center.x,low,center.y},0});return group;
}
VillageGroup path(uint8_t id,bool east,const terrain::lego::Surface& terrain) {
    VillageGroup group;group.id=id;group.name=east?"Market path":"Cottage path";
    const double side=east?1.:-1.;
    for(int step=0;step<8;++step) {
        const glm::dvec2 p=step<5?glm::dvec2(-63+side*(5.4+.8*step),-895)
            :glm::dvec2(east?-54:-72,-895-.8*double(step-4));
        const auto [low,high]=groundRange(terrain,p-glm::dvec2(.32),p+glm::dvec2(.32));
        if(!std::isfinite(high)||high-low>.5)return {};
        group.props.push_back({VillagePropKind::PathTile,{p.x,high+.002,p.y},0});
    }
    return group;
}
bool legacyCollision(const AdventureState& state,const VillageGroup& group) {
    const auto belongs=[&](uint64_t id){
        if(id==villageStructureId(group.id))return true;
        for(const auto& b:group.solids)if(id==b.part.counter)return true;
        return false;
    };
    for(const auto& structure:state.structures) {
        if(belongs(structure.id))return true;
        for(const auto& part:structure.parts)if(belongs(part.id))return true;
    }
    for(const auto& component:state.components)if(belongs(component.id))return true;
    return false;
}
bool playerApproachConflict(const VillageGroup& group,const Solid& solid) {
    const Solid envelope{{},{},group.minimum,group.maximum};
    auto reserved=solid;
    reserved.minimum-=glm::dvec3(.75,0,.75);reserved.maximum+=glm::dvec3(.75,0,.75);
    return overlap(envelope,reserved,.01);
}
bool safe(const VillageGroup& group,const AdventureState& state,const AdventureContent& content,std::span<const Solid> accepted) {
    const Solid envelope{{},{},group.minimum,group.maximum};
    // Keep the existing small square and all NPC approach space intact.
    const auto town=installedWorld().town;
    const auto nearest=glm::clamp(town,glm::dvec2(group.minimum.x,group.minimum.z),glm::dvec2(group.maximum.x,group.maximum.z));
    if(glm::length(nearest-town)<5.02)return false;
    for(const auto& node:content.resourceNodes) {
        const glm::dvec2 point(node.position.x,node.position.z);
        const auto closest=glm::clamp(point,glm::dvec2(group.minimum.x,group.minimum.z),glm::dvec2(group.maximum.x,group.maximum.z));
        if(glm::length(point-closest)<1.25)return false;
    }
    for(const auto& solid:accepted) {
        if(AdventureSession::findPart(state,solid.part.counter)) {
            // An old doorway needs room to approach it, even when its wall and
            // the new scenery would not directly intersect. Markers retain
            // their original narrow collision rather than a larger exclusion.
            if(playerApproachConflict(group,solid))return false;
        } else if(overlap(envelope,solid,.01))return false;
    }
    return !legacyCollision(state,group);
}
bool routesClear(const std::array<VillageGroup,VillageLayout::maximumGroups>& groups,
    const AdventureSpatialQueries& base,std::span<const Solid> baseSolids,const AdventureState& state,const AdventureContent& content) {
    std::vector<Solid> solids(baseSolids.begin(),baseSolids.end());
    for(const auto& group:groups)if(group.available)solids.insert(solids.end(),group.solids.begin(),group.solids.end());
    AdventureSpatialQueries queries;
    if(!queries.bindTerrain(base.terrain())||!queries.publish(solids,1))return false;
    // Exact collision, not a room's enclosing bounds. A returning player who
    // stands safely inside an installed cottage must keep the floor beneath it.
    for(const auto pose:{state.player,content.town})if(!queries.clearCapsule({pose.x,pose.y,pose.z}))return false;
    for(const auto& group:groups)if(group.available&&!group.route.empty()) {
        AdventurePlayer walker;if(!walker.initialize(queries,townSpawn(base.terrain()),installedWorld().waterHeight))return false;
        for(const auto target:group.route) {
            int tick=0;for(;tick<240;++tick) {
                const auto delta=glm::dvec2(target.x-walker.feet().x,target.z-walker.feet().z);
                if(glm::length(delta)<.08)break;
                walker.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
            }
            if(tick==240||walker.mode()!=AdventurePlayer::Mode::Walking)return false;
        }
    }
    return true;
}
}
bool isVillagePartId(uint64_t id) noexcept {
    return id<=villagePartId(1,1)&&id>=villagePartId(static_cast<uint8_t>(VillageLayout::maximumGroups),127);
}
glm::dvec3 villagePropSize(VillagePropKind kind) noexcept {
    switch(kind){case VillagePropKind::Roof:return {4.6,1.28,4.6};case VillagePropKind::Planter:return {.9,.7,.9};
        case VillagePropKind::Tree:return {2.6,4.2,2.6};case VillagePropKind::PathTile:return {.64,.08,.64};}
    return {};
}
VillageLayout VillageLayout::admit(const AdventureState& state,const AdventureContent& content,
    const AdventureSpatialQueries& base,std::span<const Solid> baseSolids,const VillageLayout* previous) {
    VillageLayout out;out.world_=state.world;
    if(!construction::isValid(state.world)||!AdventureSession::validPose(state.player)||!base.terrain().valid())return out;
    if(previous&&previous->world_==state.world)return *previous;
    const auto& terrain=base.terrain();
    out.groups_={shelter(1,{-71,-901},false,terrain),shelter(2,{-55,-901},true,terrain),
        decoration(3,"Cottage garden",VillagePropKind::Planter,{-74,-900},terrain),
        decoration(4,"Market garden",VillagePropKind::Planter,{-52,-900},terrain),
        decoration(5,"Cottage tree",VillagePropKind::Tree,{-71,-908},terrain),
        decoration(6,"Market tree",VillagePropKind::Tree,{-54,-909},terrain),path(7,false,terrain),path(8,true,terrain)};
    size_t pieces=0,props=0,solids=0;
    for(size_t i=0;i<out.groups_.size();++i) {
        auto& group=out.groups_[i];
        // Authored path plates may join a shelter's staircase. Existing
        // player structures/markers retain priority over the whole group.
        if(!compileGroup(group,state.world)||!safe(group,state,content,baseSolids))continue;
        bool conflict=false;
        if(group.id<7)for(const auto& old:out.groups_)if(old.available&&old.id!=group.id)
            for(const auto& a:group.solids)for(const auto& b:old.solids)conflict=conflict||overlap(a,b,.01);
        if(conflict)continue;
        group.available=true;
        if(!routesClear(out.groups_,base,baseSolids,state,content)){group.available=false;continue;}
        if(pieces+group.pieces.size()>maximumPieces||props+group.props.size()>maximumProps||solids+group.solids.size()>maximumSolids){group.available=false;continue;}
        pieces+=group.pieces.size();props+=group.props.size();solids+=group.solids.size();
    }
    return out;
}
bool VillageLayout::appendSolids(construction::WorldNamespace world,std::vector<Solid>& solids) const {
    if(world!=world_||!construction::isValid(world))return false;
    size_t count=0;for(const auto& group:groups_)if(group.available)count+=group.solids.size();
    if(solids.size()>AdventureSpatialQueries::maximumSolids||count>AdventureSpatialQueries::maximumSolids-solids.size())return false;
    for(const auto& group:groups_)if(group.available)solids.insert(solids.end(),group.solids.begin(),group.solids.end());
    return true;
}
bool VillageLayout::validateNewConstruction(const AdventureState& before,const AdventureState& after,std::string& error) const {
    if(before.world!=world_||after.world!=world_){error="Village geometry is unavailable.";return false;}
    for(const auto& group:groups_)if(group.available&&legacyCollision(after,group)) {
        error="The new building uses an installed village identity.";return false;
    }
    std::vector<Solid> candidate;if(!compileSolids(after,candidate,error))return false;
    std::vector<Solid> swings;if(!compileDoorSwingSolids(after,swings,error))return false;
    candidate.insert(candidate.end(),swings.begin(),swings.end());
    for(const auto& solid:candidate) {
        if(!buildingGeometryChanged(before,after,solid.part.counter))continue;
        // A new placement must preserve the same approach clearance used on
        // fresh admission, or saving it would remove this scenery on reload.
        // Unchanged legacy parts above still retain their original priority.
        for(const auto& group:groups_)if(group.available&&playerApproachConflict(group,solid)) {
            error="Leave approach space beside the village building and garden.";return false;
        }
    }
    error.clear();return true;
}
}
