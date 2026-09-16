#include "game/adventure/trail_sites.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/field_home_access.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
const std::array<TrailSiteDefinition,4> definitions{{
    {TrailSiteId::SignalTerrace,"Signal Terrace","An old signal frame and a survey worklog above the stone terrace.",{30,-923}},
    {TrailSiteId::SurveyOverlook,"Survey Overlook","A survey cairn looking back toward the meadow beacon.",{22,-1031}},
    {TrailSiteId::Relay,"Meadow beacon","Restore the beacon from a useful field home nearby.",{-63,-975}},
    {TrailSiteId::WatchArch,"Old Watch Arch","A broken watch arch marks the next trail beyond the beacon.",{-40,-1095}},
}};
GridPosition grid(glm::dvec3 p){return {int32_t(std::round(p.x*50)),int32_t(std::round(p.y*50)),int32_t(std::round(p.z*50))};}
glm::dvec3 metres(GridPosition p){return glm::dvec3(p.x,p.y,p.z)*.02;}
glm::dvec3 feet(PlayerPose p){return {p.x,p.y,p.z};}
bool overlap(const Solid& a,const Solid& b,glm::dvec3 margin={}) {
    return glm::all(glm::lessThan(a.minimum,b.maximum+margin-glm::dvec3(1e-7)))
        &&glm::all(glm::lessThan(b.minimum,a.maximum+margin-glm::dvec3(1e-7)));
}
bool ground(glm::dvec2 p,const AdventureSpatialQueries& base,PlayerPose& out,bool requireClear=true) {
    if(!base.terrain().valid()||!base.revision())return false;
    const double y=double(terrain::lego::supportHeight(base.terrain(),glm::vec2(p),.3f))+.005;
    const PlayerPose pose{p.x,y,p.y,0};
    if(!AdventureSession::validPose(pose)||y<double(installedWorld().waterHeight)+.05
        ||(requireClear&&!base.clearCapsule(feet(pose))))return false;
    out=pose;return true;
}
bool reachable(PlayerPose player,PlayerPose point,const AdventureSpatialQueries& queries,std::string& error,bool relay=false) {
    if(!AdventureSession::validPose(player)||!AdventureSession::validPose(point)
        ||!queries.revision()||!queries.clearCapsule(feet(player))) {error="A clear standing position is required.";return false;}
    if(glm::length(feet(player)-feet(point))>3.2) {error=relay?"Move closer to the beacon.":"Move closer to the discovery.";return false;}
    const auto eye=feet(player)+glm::dvec3(0,1.55,0),target=feet(point)+glm::dvec3(0,1.2,0);
    const auto direction=target-eye;const double distance=glm::length(direction);
    if(distance>1e-8) {
        const auto hit=queries.raycast(eye,direction,distance);
        const bool beacon=relay&&hit.hit&&!hit.terrain&&hit.structure.counter==2&&hit.part.counter==2;
        if(!hit.complete||(hit.hit&&hit.distance<distance-.025&&!beacon)) {error="The approach is blocked.";return false;}
    }
    error.clear();return true;
}
double post(TrailGroup& group,glm::dvec2 position,unsigned count,const terrain::lego::Surface& terrain) {
    const auto y=terrainPlacementHeight(PieceKind::Pier,0,position,terrain);
    if(!y)return std::numeric_limits<double>::infinity();
    for(unsigned level=0;level<count;++level)group.pieces.push_back({PieceKind::Pier,grid({position.x,*y+.96*double(level),position.y}),0});
    return *y+.96*double(count);
}
TrailGroup frame(uint8_t id,std::string_view name,glm::dvec2 center,bool alongZ,
    unsigned first,unsigned second,const terrain::lego::Surface& terrain) {
    TrailGroup group;group.id=id;group.name=name;
    const auto offset=alongZ?glm::dvec2(0,.72):glm::dvec2(.72,0);
    const double a=post(group,center-offset,first,terrain),b=post(group,center+offset,second,terrain);
    if(!std::isfinite(a)||!std::isfinite(b)){group.pieces.clear();return group;}
    group.pieces.push_back({PieceKind::Beam,grid({center.x,std::max(a,b),center.y}),uint8_t(alongZ?1:0)});
    return group;
}
bool compile(TrailGroup& group,construction::WorldNamespace world,const AdventureSpatialQueries& base) {
    if(group.pieces.empty())return false;
    AdventureState state;state.world=world;state.structures.push_back({trailStructureId(group.id),1,0,group.pieces.front().position,{}});
    uint8_t local=1;
    for(const auto& p:group.pieces)state.structures.front().parts.push_back({trailPartId(group.id,local++),p.kind,p.position,p.yawQuarterTurns,0});
    std::string error;
    if(!validateConstruction(state,state,base,error)||!compileSolids(state,group.solids,error))return false;
    group.minimum=glm::dvec3(INFINITY);group.maximum=glm::dvec3(-INFINITY);
    for(const auto& solid:group.solids){group.minimum=glm::min(group.minimum,solid.minimum);group.maximum=glm::max(group.maximum,solid.maximum);}
    return true;
}
bool reserved(const AdventureState& state,const TrailGroup& group) {
    const auto used=[&](uint64_t id) {
        return id==trailStructureId(group.id)||std::any_of(group.solids.begin(),group.solids.end(),[&](const auto& b){return b.part.counter==id;});
    };
    for(const auto& structure:state.structures) {
        if(used(structure.id))return true;
        for(const auto& part:structure.parts)if(used(part.id))return true;
    }
    for(const auto& component:state.components)if(used(component.id))return true;
    return false;
}
bool approachConflict(const TrailGroup& group,const Solid& playerSolid) {
    return overlap({{},{},group.minimum,group.maximum},playerSolid,{.75,.01,.75});
}
bool safeGroup(const TrailGroup& group,const AdventureState& state,const AdventureContent& content,
    const AdventureSpatialQueries& base,std::span<const Solid> already) {
    if(reserved(state,group))return false;
    for(const auto& piece:group.solids)if(protectedConstruction(piece.minimum,piece.maximum))return false;
    for(const auto& old:base.solids()) {
        if(AdventureSession::findPart(state,old.part.counter)&&approachConflict(group,old))return false;
        for(const auto& piece:group.solids)if(overlap(piece,old))return false;
        if(old.structure.counter==trailStructureId(group.id))return false;
        for(const auto& piece:group.solids)if(old.part==piece.part)return false;
    }
    for(const auto& node:content.resourceNodes) {
        const glm::dvec2 p(node.position.x,node.position.z);
        if(glm::length(p-glm::clamp(p,glm::dvec2(group.minimum.x,group.minimum.z),glm::dvec2(group.maximum.x,group.maximum.z)))<1.25)return false;
    }
    for(const auto& piece:group.solids)for(const auto& old:already)if(overlap(piece,old))return false;
    std::vector<Solid> all(base.solids().begin(),base.solids().end());all.insert(all.end(),already.begin(),already.end());
    all.insert(all.end(),group.solids.begin(),group.solids.end());AdventureSpatialQueries queries;
    if(!queries.bindTerrain(base.terrain())||!queries.publish(all,1))return false;
    if(!queries.clearCapsule(feet(state.player))||!queries.clearCapsule(feet(content.town)))return false;
    return !state.registeredBed||queries.clearCapsule(feet(state.recovery));
}
bool hasApproach(const TrailSite& site,const AdventureSpatialQueries& queries,bool relay) {
    constexpr std::array<glm::dvec2,5> offsets{{{0,0},{1.5,0},{-1.5,0},{0,1.5},{0,-1.5}}};
    for(const auto offset:offsets) {
        const glm::dvec2 p(site.position.x+offset.x,site.position.z+offset.y);
        const double y=queries.supportHeight(p,.3,site.position.y+.4);
        if(!std::isfinite(y))continue;
        const PlayerPose actor{p.x,y+.005,p.y,0};std::string error;
        if(reachable(actor,site.position,queries,error,relay))return true;
    }
    return false;
}
} // namespace

std::span<const TrailSiteDefinition> trailSiteDefinitions() noexcept {return definitions;}
const TrailSiteDefinition* trailSiteDefinition(TrailSiteId id) noexcept {
    const auto index=uint8_t(id);return index>=1&&index<=definitions.size()?&definitions[index-1]:nullptr;
}
bool isTrailPartId(uint64_t id) noexcept {return id<=trailPartId(1,1)&&id>=trailPartId(4,31);}
bool defaultTrailContent(const AdventureSpatialQueries& base,std::array<DiscoveryContent,2>& discoveries,
    std::vector<ResourceNode>& resources,std::string& error) {
    if(resources.size()!=18){error="The unchanged 18 meadow supplies must be installed first.";return false;}
    for(size_t i=0;i<resources.size();++i)if(resources[i].id!=i+1){error="The meadow supply identities changed.";return false;}
    std::array<DiscoveryContent,2> next{};
    for(size_t i=0;i<next.size();++i) {
        PlayerPose pose;if(!ground(definitions[i].anchor,base,pose)){error="A discovery's terrain is unavailable.";return false;}
        next[i]={uint8_t(i+1),pose,i==0?ItemStack{ItemKind::Scrap,4}:ItemStack{ItemKind::Stone,12}};
    }
    const std::array<glm::dvec2,3> positions{{{20,-924},{21,-936},{22,-1031}}};
    const std::array<ItemStack,3> yields{{{ItemKind::Stone,12},{ItemKind::Wood,16},{ItemKind::Scrap,4}}};
    auto appended=resources;
    for(size_t i=0;i<positions.size();++i) {
        PlayerPose pose;if(!ground(positions[i],base,pose)){error="A trail supply's terrain is unavailable.";return false;}
        pose.y-=.005;appended.push_back({uint32_t(19+i),pose,yields[i]});
    }
    discoveries=next;resources=std::move(appended);error.clear();return true;
}
FirstHomeReadiness fieldHomeReadiness(const AdventureState& state,const AdventureSpatialQueries& queries) {
    FirstHomeReadiness best{FirstHomeStep::RegisterBed,0,"Build a field home within 25 m of the beacon, beyond the town meadow."};
    FieldHomeAccessWork work;
    for(const auto& bed:state.components)if(bed.kind==FurnitureKind::Bed&&bed.owner==1) {
        const auto* part=AdventureSession::findPart(state,bed.part);if(!part)continue;
        const auto position=metres(part->position);const glm::dvec2 xz(position.x,position.z);
        if(glm::length(xz-installedWorld().landmark)>25||glm::length(xz-installedWorld().town)<=40)continue;
        auto local=state;local.registeredBed=bed.id;auto ready=firstHomeReadiness(local,queries);
        if(ready.ready()) {
            if(!fieldFurnitureAccessible(local,queries,ready.structure,FurnitureKind::Chest,&work))
                ready={FirstHomeStep::AddChest,ready.structure,"Leave a walking route from the bed to a usable chest."};
            else if(!fieldFurnitureAccessible(local,queries,ready.structure,FurnitureKind::Workbench,&work))
                ready={FirstHomeStep::AddWorkbench,ready.structure,"Leave a walking route from the bed to a usable workbench."};
            else {ready.message="Your field home is ready for the beacon.";return ready;}
        }
        best=std::move(ready);
        if(work.exhausted){best.message="Keep your field home's chest and workbench close to the bed, with clear walking routes.";return best;}
    }
    return best;
}
std::array<PlacePart,3> signalTerraceStep() noexcept {
    return {{{0,PieceKind::Pier,{1455,-6496,-46174},0,0},
        {0,PieceKind::Pier,{1455,-6496,-46150},0,0},{0,PieceKind::Pier,{1455,-6496,-46126},0,0}}};
}
TrailSites TrailSites::admit(const AdventureState& state,const AdventureContent& content,
    const AdventureSpatialQueries& base,const TrailSites* previous) {
    TrailSites out;out.world_=state.world;
    if(!construction::isValid(state.world)||!AdventureSession::validPose(state.player)||!base.terrain().valid()||!base.revision())return out;
    if(previous&&previous->world_==state.world) {
        out=*previous;out.sites_[2].active=state.trail.relayActivationRevision!=0;return out;
    }
    for(size_t i=0;i<out.sites_.size();++i) {
        out.sites_[i].id=definitions[i].id;
        if(!ground(definitions[i].anchor,base,out.sites_[i].position,i!=2))continue;
        out.sites_[i].available=true;
    }
    out.sites_[2].active=state.trail.relayActivationRevision!=0;
    out.groups_[0]=frame(1,"Signal frame",{30,-920.5},true,3,3,base.terrain());
    out.groups_[1].id=2;out.groups_[1].name="Survey cairn";(void)post(out.groups_[1],{22,-1033.5},3,base.terrain());
    out.groups_[2]=frame(3,"Broken watch arch",{-40,-1095},false,6,3,base.terrain());
    out.groups_[3].id=4;out.groups_[3].name="Meadow waystone";(void)post(out.groups_[3],{-59,-917},3,base.terrain());
    std::vector<Solid> accepted;size_t pieces=0;
    for(auto& group:out.groups_) {
        if(pieces+group.pieces.size()>maximumPieces||!compile(group,state.world,base)
            ||accepted.size()+group.solids.size()>maximumSolids||!safeGroup(group,state,content,base,accepted))continue;
        group.available=true;pieces+=group.pieces.size();accepted.insert(accepted.end(),group.solids.begin(),group.solids.end());
    }
    std::vector<Solid> all(base.solids().begin(),base.solids().end());all.insert(all.end(),accepted.begin(),accepted.end());
    AdventureSpatialQueries complete;
    if(!complete.bindTerrain(base.terrain())||!complete.publish(all,1))return {};
    out.sites_[0].available=out.sites_[0].available&&out.groups_[0].available&&hasApproach(out.sites_[0],complete,false);
    out.sites_[1].available=out.sites_[1].available&&out.groups_[1].available&&hasApproach(out.sites_[1],complete,false);
    out.sites_[2].available=out.sites_[2].available&&hasApproach(out.sites_[2],complete,true);
    out.sites_[3].available=out.sites_[3].available&&out.groups_[2].available&&hasApproach(out.sites_[3],complete,false);
    return out;
}
const TrailSite* TrailSites::site(TrailSiteId id) const noexcept {
    const auto index=uint8_t(id);return index>=1&&index<=sites_.size()?&sites_[index-1]:nullptr;
}
bool TrailSites::appendSolids(construction::WorldNamespace world,std::vector<Solid>& solids) const {
    if(world!=world_||!construction::isValid(world))return false;
    size_t count=0;for(const auto& group:groups_)if(group.available)count+=group.solids.size();
    if(solids.size()>AdventureSpatialQueries::maximumSolids||count>AdventureSpatialQueries::maximumSolids-solids.size())return false;
    for(const auto& group:groups_)if(group.available)for(const auto& old:solids)
        if(old.structure==construction::DurableId{world,trailStructureId(group.id)})return false;
    for(const auto& group:groups_)if(group.available)solids.insert(solids.end(),group.solids.begin(),group.solids.end());
    return true;
}
bool TrailSites::validateNewConstruction(const AdventureState& before,const AdventureState& after,std::string& error) const {
    if(before.world!=world_||after.world!=world_){error="Trail geometry is unavailable.";return false;}
    for(const auto& group:groups_)if(group.available&&reserved(after,group)){error="An installed trail identity is reserved.";return false;}
    std::vector<Solid> solids;if(!compileSolids(after,solids,error))return false;
    std::vector<Solid> swings;if(!compileDoorSwingSolids(after,swings,error))return false;
    solids.insert(solids.end(),swings.begin(),swings.end());
    for(const auto& solid:solids) {
        if(!buildingGeometryChanged(before,after,solid.part.counter))continue;
        for(const auto& group:groups_)if(group.available&&approachConflict(group,solid)) {
            error="Leave a walking approach beside the trail scenery.";return false;
        }
        for(const auto& site:sites_)if(site.available&&site.id!=TrailSiteId::Relay) {
            const Solid approach{{},{},feet(site.position)-glm::dvec3(.3,0,.3),feet(site.position)+glm::dvec3(.3,1.7,.3)};
            if(overlap(approach,solid)){error="Leave the discovery's standing place clear.";return false;}
        }
    }
    error.clear();return true;
}
bool TrailSites::discoveryReachable(uint8_t id,PlayerPose player,const AdventureSpatialQueries& queries,std::string& error) const {
    if(id<1||id>2||!sites_[id-1].available){error="This discovery is currently unavailable.";return false;}
    return reachable(player,sites_[id-1].position,queries,error);
}
bool TrailSites::relayReachable(PlayerPose player,const AdventureSpatialQueries& queries,std::string& error) const {
    if(!sites_[2].available){error="The beacon approach is unavailable.";return false;}
    return reachable(player,sites_[2].position,queries,error,true);
}
} // namespace voxy::game::adventure
