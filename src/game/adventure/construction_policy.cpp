#include "game/adventure/construction_policy.hpp"
#include "game/adventure/building_doors.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/creative_scenery.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
constexpr double contactTolerance=.021;
glm::dvec3 metres(GridPosition p) noexcept {return glm::dvec3(p.x,p.y,p.z)*.02;}
Solid worldBox(const AdventureState& state,const WorldStructure& structure,const WorldPart& part,const GridBox& box) {
    // Transform/add on the integer lattice first. Converting local and world
    // offsets separately lets FMA create sub-ulp cracks on a shared roof edge.
    int64_t xmin=INT64_MAX,xmax=INT64_MIN,zmin=INT64_MAX,zmax=INT64_MIN;
    for(int x=0;x<2;++x)for(int z=0;z<2;++z) {
        int64_t px=x?box.maximum.x:box.minimum.x,pz=z?box.maximum.z:box.minimum.z;
        switch(part.yawQuarterTurns) {
        case 1:{const auto old=px;px=pz;pz=-old;break;}
        case 2:px=-px;pz=-pz;break;
        case 3:{const auto old=px;px=-pz;pz=old;break;}
        default:break;
        }
        xmin=std::min(xmin,px);xmax=std::max(xmax,px);zmin=std::min(zmin,pz);zmax=std::max(zmax,pz);
    }
    const glm::dvec3 lo=glm::dvec3(double(xmin+part.position.x),double(int64_t(box.minimum.y)+part.position.y),double(zmin+part.position.z))*.02;
    const glm::dvec3 hi=glm::dvec3(double(xmax+part.position.x),double(int64_t(box.maximum.y)+part.position.y),double(zmax+part.position.z))*.02;
    return {{state.world,structure.id},{state.world,part.id},lo,hi};
}
bool overlaps(const Solid& a,const Solid& b) noexcept {
    return glm::all(glm::lessThan(a.minimum,b.maximum-glm::dvec3(1e-7)))
        &&glm::all(glm::greaterThan(a.maximum,b.minimum+glm::dvec3(1e-7)));
}
bool contact(const Solid& a,const Solid& b) noexcept {
    for(int axis=0;axis<3;++axis) {
        if(std::abs(a.maximum[axis]-b.minimum[axis])>contactTolerance
            &&std::abs(b.maximum[axis]-a.minimum[axis])>contactTolerance)continue;
        const int u=(axis+1)%3,v=(axis+2)%3;
        if(std::min(a.maximum[u],b.maximum[u])-std::max(a.minimum[u],b.minimum[u])>.019
            &&std::min(a.maximum[v],b.maximum[v])-std::max(a.minimum[v],b.minimum[v])>.019)return true;
    }
    return false;
}
// Sample every intersected terrain cell and its stud center, including box
// edges. Bounds are small installed kit pieces, not arbitrary attacker meshes.
std::pair<double,double> terrainRange(const terrain::lego::Surface& surface,const Solid& box) noexcept {
    const glm::dvec2 origin(surface.origin());const double cell=surface.cellScale;
    const int x0=int(std::floor((box.minimum.x+origin.x)/cell));
    const int z0=int(std::floor((box.minimum.z+origin.y)/cell));
    const int x1=int(std::floor((box.maximum.x+origin.x-1e-7)/cell));
    const int z1=int(std::floor((box.maximum.z+origin.y-1e-7)/cell));
    if(x0<0||z0<0||x1>=int(surface.width)-1||z1>=int(surface.height)-1)return {INFINITY,INFINITY};
    double minimum=INFINITY,maximum=-INFINITY;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
        const double top=surface.cellTop(x,z);minimum=std::min(minimum,top);maximum=std::max(maximum,top);
        const glm::dvec2 center((x+.5)*cell-origin.x,(z+.5)*cell-origin.y);
        const auto nearest=glm::clamp(center,glm::dvec2(box.minimum.x,box.minimum.z),glm::dvec2(box.maximum.x,box.maximum.z));
        if(glm::length(center-nearest)<=double(terrain::lego::kStudRadius)*cell)
            maximum=std::max(maximum,top+double(terrain::lego::kStudHeight)*cell);
    }
    return {minimum,maximum};
}
const WorldPart* partFor(const AdventureState& state,uint64_t id) noexcept {
    for(const auto& structure:state.structures)for(const auto& part:structure.parts)if(part.id==id)return &part;
    return nullptr;
}
// Published parts are ordered, but this validator also accepts arbitrary input
// order. Certify once per call and retain the original first-match fallback.
class PartLookup {
    const AdventureState& state_;
    bool sorted_;
public:
    explicit PartLookup(const AdventureState& state):state_(state),sorted_(
        std::all_of(state.structures.begin(),state.structures.end(),[](const auto& structure){
            return std::is_sorted(structure.parts.begin(),structure.parts.end(),
                [](const auto& a,const auto& b){return a.id<b.id;});
        })) {}
    const WorldPart* find(uint64_t id) const noexcept {
        if(!sorted_)return partFor(state_,id);
        for(const auto& structure:state_.structures) {
            const auto found=std::lower_bound(structure.parts.begin(),structure.parts.end(),id,
                [](const WorldPart& part,uint64_t value){return part.id<value;});
            if(found!=structure.parts.end()&&found->id==id)return &*found;
        }
        return nullptr;
    }
};
const StructureComponent* componentFor(const AdventureState& state,uint64_t id) noexcept {
    for(const auto& c:state.components)if(c.id==id)return &c;
    return nullptr;
}
const StructureComponent* doorFor(const AdventureState& state,uint64_t part) noexcept {
    const StructureComponent* found=nullptr;
    for(const auto& component:state.components)if(component.part==part) {
        if(found||component.kind!=FurnitureKind::Door)return nullptr;
        found=&component;
    }
    return found;
}
bool compileGeometry(const AdventureState& state,std::vector<Solid>& output,std::vector<bool>* support,std::string& error) {
    if(!construction::isValid(state.world)||state.structures.size()>kMaximumStructures) {error="Invalid world construction";return false;}
    if(state.components.size()>kMaximumComponents){error="Construction component capacity reached";return false;}
    for(const auto& component:state.components)if(component.kind!=FurnitureKind::Door&&component.doorOpen) {
        error="Only a door can have an open state";return false;
    }
    std::vector<Solid> result;std::set<uint64_t> identifiers;size_t count=0;
    std::vector<bool> supporting;
    for(const auto& structure:state.structures) {
        if(!structure.id||!identifiers.insert(structure.id).second){error="Invalid structure identity";return false;}
        for(const auto& part:structure.parts) {
            const auto* definition=buildingDefinition(part.kind);
            if(++count>kMaximumParts||!part.id||!identifiers.insert(part.id).second||!definition
                ||part.yawQuarterTurns>3||!construction::isValid(part.position)) {error="Invalid building piece";return false;}
            const auto boxes=part.kind==PieceKind::HingedDoor?buildingDefinition(PieceKind::Doorway)->solids:definition->solids;
            for(const auto& box:boxes) {
                if(result.size()==AdventureSpatialQueries::maximumSolids) {error="Construction collision capacity reached";return false;}
                result.push_back(worldBox(state,structure,part,box));
                if(support)supporting.push_back(true);
            }
            if(part.kind==PieceKind::HingedDoor) {
                const auto* door=doorFor(state,part.id);
                if(!door||door->structure!=structure.id||door->owner!=structure.owner) {error="Door state is missing or invalid";return false;}
                if(result.size()==AdventureSpatialQueries::maximumSolids) {error="Construction collision capacity reached";return false;}
                result.push_back(worldBox(state,structure,part,doorLeafBox(door->doorOpen)));
                if(support)supporting.push_back(false);
            }
        }
    }
    if(support)*support=std::move(supporting);
    output=std::move(result);error.clear();return true;
}
bool partMoved(const WorldPart* old,const WorldPart& part) noexcept {
    return !old||old->kind!=part.kind||old->position!=part.position||old->yawQuarterTurns!=part.yawQuarterTurns;
}
bool validateDoorClearance(const AdventureState& before,const AdventureState& after,
    const AdventureSpatialQueries& accepted,std::span<const Solid> solids,std::string& error,bool freeBuild) {
    const glm::dvec3 feet(before.player.x,before.player.y,before.player.z);
    const double scale=freeBuild?AdventurePlayer::creativeScale:1;
    const double radius=(freeBuild?AdventurePlayer::creativeRadius:AdventurePlayer::radius)*scale,height=AdventurePlayer::height*scale;
    const Solid player{{},{},feet+glm::dvec3(-radius,0,-radius),feet+glm::dvec3(radius,height,radius)};
    for(const auto& structure:after.structures)for(const auto& part:structure.parts)if(part.kind==PieceKind::HingedDoor) {
        const auto* door=doorFor(after,part.id);const auto* oldDoor=doorFor(before,part.id);
        if(!door){error="Door state is unavailable";return false;}
        const bool moved=partMoved(partFor(before,part.id),part);
        const bool toggled=oldDoor&&oldDoor->doorOpen!=door->doorOpen;
        if(toggled&&!reachableComponent(before,oldDoor->id,accepted,error,freeBuild))return false;
        for(const auto& local:doorSweepBoxes()) {
            const auto sweep=worldBox(after,structure,part,local);
            const auto [lowest,highest]=terrainRange(accepted.terrain(),sweep);(void)lowest;
            if(!std::isfinite(highest)||sweep.minimum.y<highest-.021) {error="Leave level ground clear for the door to swing";return false;}
            if(!freeBuild&&protectedConstruction(sweep.minimum,sweep.maximum)) {error="Keep the door swing away from town and landmark access";return false;}
            // The frame is intentionally inside its own leaf envelope. Every
            // other accepted owned part must leave the future swing clear.
            for(const auto& solid:solids)if(solid.part.counter!=part.id&&overlaps(sweep,solid)) {
                error="Leave the door's swing clear of building pieces";return false;
            }
            if(moved||toggled) {
                if(overlaps(sweep,player)){error="Step outside the door's swing before using it";return false;}
                for(const auto& solid:accepted.solids()) {
                    // Old owned boxes are replaced by the full candidate above.
                    // Installed actors and scenery remain authoritative here.
                    if(partFor(before,solid.part.counter)||partFor(after,solid.part.counter))continue;
                    // Creative props yield to the candidate's entire reserved
                    // door swing at the same commit boundary as their draws.
                    if(freeBuild&&isCreativeScenerySolid(solid))continue;
                    if(overlaps(sweep,solid)){error="The door's swing is blocked by a character or scenery";return false;}
                }
            }
        }
    }
    error.clear();return true;
}
}

bool compileSolids(const AdventureState& state,std::vector<Solid>& output,std::string& error) {
    return compileGeometry(state,output,nullptr,error);
}
bool buildingGeometryChanged(const AdventureState& before,const AdventureState& after,uint64_t id) noexcept {
    const auto* old=partFor(before,id),*part=partFor(after,id);
    if(!part||partMoved(old,*part))return true;
    if(part->kind!=PieceKind::HingedDoor)return false;
    const auto* a=doorFor(before,id),*b=doorFor(after,id);
    return !a||!b||a->doorOpen!=b->doorOpen;
}
bool compileDoorSwingSolids(const AdventureState& state,std::vector<Solid>& output,std::string& error) {
    std::vector<Solid> checked;if(!compileSolids(state,checked,error))return false;
    std::vector<Solid> result;
    for(const auto& structure:state.structures)for(const auto& part:structure.parts)if(part.kind==PieceKind::HingedDoor)
        for(const auto& box:doorSweepBoxes()) {
            if(result.size()==AdventureSpatialQueries::maximumSolids){error="Door clearance capacity reached";return false;}
            result.push_back(worldBox(state,structure,part,box));
        }
    output=std::move(result);error.clear();return true;
}
std::optional<double> terrainPlacementHeight(PieceKind kind,uint8_t yaw,glm::dvec2 p,
    const terrain::lego::Surface& surface) noexcept {
    const auto* definition=buildingDefinition(kind);
    if(!definition||yaw>3||!surface.valid()||!std::isfinite(p.x)||!std::isfinite(p.y)
        ||std::abs(p.x)>100000||std::abs(p.y)>100000)return std::nullopt;
    AdventureState state;WorldStructure structure;
    WorldPart part{1,kind,{int32_t(std::round(p.x*50)),0,int32_t(std::round(p.y*50))},yaw,0};
    const auto bounds=worldBox(state,structure,part,definition->bounds);
    const auto [lowest,highest]=terrainRange(surface,bounds);(void)lowest;
    if(!std::isfinite(highest))return std::nullopt;
    const double height=double(definition->bounds.maximum.y-definition->bounds.minimum.y)*.02;
    return definition->terrainAnchor?std::ceil((highest+.025-height)/.32)*.32
        :std::ceil(highest/.02)*.02;
}
bool validateInstalledGeometry(const AdventureState& state,const AdventureSpatialQueries& queries,std::string& error,bool freeBuild) {
    if(!validateConstruction(state,state,queries,error,freeBuild))return false;
    std::vector<Solid> solids;if(!compileSolids(state,solids,error))return false;
    for(const auto& solid:solids)if(!freeBuild&&protectedConstruction(solid.minimum,solid.maximum)) {
        error="A building blocks protected town access";return false;
    }
    return true;
}
bool validateConstruction(const AdventureState& before,const AdventureState& after,
    const AdventureSpatialQueries& accepted,std::string& error,bool freeBuild) {
    if(!accepted.terrain().valid()||before.world!=after.world) {error="World geometry unavailable";return false;}
    std::vector<Solid> solids;std::vector<bool> supports;
    if(!compileGeometry(after,solids,&supports,error)||!validateDoorClearance(before,after,accepted,solids,error,freeBuild))return false;
    const glm::dvec3 feet(before.player.x,before.player.y,before.player.z);
    const double scale=freeBuild?AdventurePlayer::creativeScale:1;
    const double radius=(freeBuild?AdventurePlayer::creativeRadius:AdventurePlayer::radius)*scale,height=AdventurePlayer::height*scale;
    const Solid player{{},{},feet+glm::dvec3(-radius,0,-radius),feet+glm::dvec3(radius,height,radius)};
    std::map<uint64_t,std::vector<size_t>> partSolids;
    std::set<uint64_t> supported;
    for(size_t i=0;i<solids.size();++i)partSolids[solids[i].part.counter].push_back(i);
    const PartLookup beforeParts(before),afterParts(after);
    for(const auto& structure:after.structures)for(const auto& part:structure.parts) {
        const auto* definition=buildingDefinition(part.kind);const auto* old=beforeParts.find(part.id);
        const bool moved=partMoved(old,part);
        bool terrainAnchor=false;
        for(const auto index:partSolids[part.id]) {
            const auto& solid=solids[index];const auto [lowest,highest]=terrainRange(accepted.terrain(),solid);
            if(!std::isfinite(lowest)) {error="Build inside the landscape";return false;}
            if(definition->terrainAnchor) {
                if(solid.minimum.y<=highest+contactTolerance&&solid.maximum.y>=highest+.025
                    &&solid.minimum.y>=lowest-4)terrainAnchor=true;
                if(solid.maximum.y<highest+.025) {error="Raise the foundation above the terrain";return false;}
            } else if(solid.minimum.y<highest-.021) {error="This piece intersects the terrain";return false;}
            if(freeBuild&&supports[index]&&std::abs(solid.minimum.y-highest)<=contactTolerance)terrainAnchor=true;
            if(moved) {
                if(glm::length(glm::clamp(feet+glm::dvec3(0,height*.5,0),solid.minimum,solid.maximum)-(feet+glm::dvec3(0,height*.5,0)))>12) {error="Move closer to build";return false;}
                if(!freeBuild&&protectedConstruction(solid.minimum,solid.maximum)) {error="Keep the town and landmark access clear";return false;}
                if(overlaps(solid,player)) {error="Move out of the building preview";return false;}
            }
        }
        if(terrainAnchor)supported.insert(part.id);
    }
    for(const auto& structure:before.structures)for(const auto& removed:structure.parts)if(!afterParts.find(removed.id)) {
        if(glm::length(metres(removed.position)-feet)>12) {error="Move closer to remove this piece";return false;}
    }
    std::map<uint64_t,std::vector<uint64_t>> neighbors;
    std::vector<size_t> byX(solids.size());
    for(size_t i=0;i<byX.size();++i)byX[i]=i;
    std::stable_sort(byX.begin(),byX.end(),[&](size_t a,size_t b){return solids[a].minimum.x<solids[b].minimum.x;});
    for(size_t aa=0;aa<byX.size();++aa)for(size_t bb=aa+1;bb<byX.size();++bb) {
        const size_t a=byX[aa],b=byX[bb];
        if(solids[b].minimum.x>solids[a].maximum.x+contactTolerance)break;
        if(solids[a].part==solids[b].part)continue;
        if(overlaps(solids[a],solids[b])) {error="Building pieces overlap";return false;}
        if(supports[a]&&supports[b]&&solids[a].structure==solids[b].structure&&contact(solids[a],solids[b])) {
            neighbors[solids[a].part.counter].push_back(solids[b].part.counter);
            neighbors[solids[b].part.counter].push_back(solids[a].part.counter);
        }
    }
    std::vector<uint64_t> queue(supported.begin(),supported.end());
    for(size_t i=0;i<queue.size();++i)for(const auto neighbor:neighbors[queue[i]])if(supported.insert(neighbor).second)queue.push_back(neighbor);
    if(supported.size()!=partSolids.size()) {error=freeBuild?"Connect this piece to the ground or a supported brick":"Connect every piece to a grounded foundation";return false;}
    // An edit may remove a roof used by a registered bed. Keep the bed record;
    // recovery revalidates shelter/clearance and falls back to town if needed.
    error.clear();return true;
}
bool reachableComponent(const AdventureState& state,uint64_t id,const AdventureSpatialQueries& queries,std::string& error,bool freeBuild) {
    const auto* component=componentFor(state,id);const auto* part=component?partFor(state,component->part):nullptr;
    if(!part) {error="This furniture is unavailable";return false;}
    const auto* definition=buildingDefinition(part->kind);if(!definition) {error="Unknown furniture";return false;}
    glm::dvec3 local(0,double(definition->bounds.maximum.y)*.02+.08,0);
    if(component->kind==FurnitureKind::Door) {
        if(part->kind!=PieceKind::HingedDoor){error="This door is unavailable";return false;}
        local=doorHandlePoint(component->doorOpen);
        for(uint8_t i=0;i<part->yawQuarterTurns;++i)local={local.z,local.y,-local.x};
    }
    const auto center=metres(part->position)+local;
    const double scale=freeBuild?AdventurePlayer::creativeScale:1;
    const glm::dvec3 eye(state.player.x,state.player.y+AdventurePlayer::eyeHeight*scale,state.player.z);const auto delta=center-eye;
    const double distance=glm::length(delta);
    if(distance>3.2*scale) {error="Move closer to use it";return false;}
    if(distance>1e-5) {
        const auto ray=queries.raycast(eye,delta,distance);
        if(!ray.complete||(ray.hit&&ray.distance<distance-.12&&ray.part.counter!=part->id)) {error="The furniture is blocked";return false;}
    }
    error.clear();return true;
}
bool usableBed(const AdventureState& state,uint64_t id,const AdventureSpatialQueries& queries,PlayerPose& recovery,std::string& error) {
    const auto* component=componentFor(state,id);const auto* part=component?partFor(state,component->part):nullptr;
    if(!component||component->kind!=FurnitureKind::Bed||!part) {error="Choose a bed";return false;}
    std::vector<Solid> solids;if(!compileSolids(state,solids,error))return false;
    Solid bounds{};bounds.minimum=glm::dvec3(INFINITY);bounds.maximum=glm::dvec3(-INFINITY);
    bool found=false;
    for(const auto& solid:solids)if(solid.part.counter==part->id) {
        bounds.minimum=glm::min(bounds.minimum,solid.minimum);bounds.maximum=glm::max(bounds.maximum,solid.maximum);found=true;
    }
    if(!found) {error="Bed geometry unavailable";return false;}
    const auto* bed=&bounds;const auto center=(bed->minimum+bed->maximum)*.5;
    if(bed->minimum.y<double(installedWorld().waterHeight)+.15) {error="Place the bed above the water";return false;}
    // Four mattress corners and its center must each lie under an admitted roof.
    for(const auto p:std::array<glm::dvec2,5>{{{center.x,center.z},{bed->minimum.x+.05,bed->minimum.z+.05},
        {bed->minimum.x+.05,bed->maximum.z-.05},{bed->maximum.x-.05,bed->minimum.z+.05},{bed->maximum.x-.05,bed->maximum.z-.05}}}) {
        bool covered=false;
        for(const auto& solid:solids) {
            const auto* roof=partFor(state,solid.part.counter);
            if(roof&&roof->kind==PieceKind::Roof&&p.x>=solid.minimum.x&&p.x<=solid.maximum.x
                &&p.y>=solid.minimum.z&&p.y<=solid.maximum.z&&solid.minimum.y>=bed->minimum.y+2.2&&solid.minimum.y<=bed->minimum.y+6)covered=true;
        }
        if(!covered){error="Add a roof with room to stand above the bed";return false;}
    }
    unsigned walls=0;
    for(const auto direction:std::array<glm::dvec3,4>{{{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}}}) {
        const auto ray=queries.raycast({center.x,bed->minimum.y+1.2,center.z},direction,8);
        const auto* wall=ray.hit&&!ray.terrain?partFor(state,ray.part.counter):nullptr;
        if(ray.complete&&wall&&(wall->kind==PieceKind::Wall||wall->kind==PieceKind::Doorway||wall->kind==PieceKind::HingedDoor))++walls;
    }
    if(walls<3) {error="Shelter the bed with walls on three sides";return false;}
    for(const auto p:std::array<glm::dvec2,4>{{{bed->minimum.x-.5,center.z},{bed->maximum.x+.5,center.z},
        {center.x,bed->minimum.z-.5},{center.x,bed->maximum.z+.5}}}) {
        const double y=queries.supportHeight(p,.3,bed->minimum.y+.4);
        if(std::isfinite(y)&&std::abs(y-bed->minimum.y)<=.4&&queries.clearCapsule({p.x,y+.005,p.y})) {
            recovery={p.x,y+.005,p.y,state.player.yaw};error.clear();return true;
        }
    }
    error="Leave clear floor space beside the bed";return false;
}
bool validateInteractions(const AdventureState& before,const AdventureState& after,
    const AdventureContent& content,const AdventureSpatialQueries& queries,std::string& error) {
    for(const auto& component:after.components) {
        const auto* old=componentFor(before,component.id);
        if(old&&old->slots!=component.slots&&!reachableComponent(before,component.id,queries,error,content.freeBuilding))return false;
    }
    for(const auto id:after.depletedNodes)if(!std::binary_search(before.depletedNodes.begin(),before.depletedNodes.end(),id)) {
        const auto node=std::find_if(content.resourceNodes.begin(),content.resourceNodes.end(),[&](const auto& n){return n.id==id;});
        if(node==content.resourceNodes.end()) {error="Unknown supply pile";return false;}
        const glm::dvec3 feet(before.player.x,before.player.y,before.player.z);
        const glm::dvec3 point(node->position.x,node->position.y+.3,node->position.z);
        if(glm::length(point-feet)>3.2) {error="Move closer to gather supplies";return false;}
        const auto eye=feet+glm::dvec3(0,1.55,0),delta=point-eye;
        if(glm::length(delta)>1e-5) {
            const auto ray=queries.raycast(eye,delta,glm::length(delta));
            if(!ray.complete||(ray.hit&&ray.distance<glm::length(delta)-.05)) {error="The supplies are blocked";return false;}
        }
    }
    const auto ownedItems=[](const AdventureState& state,ItemKind kind) {
        unsigned count=state.equippedTool.kind==kind?state.equippedTool.quantity:0;
        if(state.equippedUtility.kind==kind)count+=state.equippedUtility.quantity;
        for(const auto& stack:state.backpack)if(stack.kind==kind)count+=stack.quantity;
        for(const auto& component:state.components)for(const auto& stack:component.slots)
            if(stack.kind==kind)count+=stack.quantity;
        return count;
    };
    if(ownedItems(after,ItemKind::FieldHammer)>ownedItems(before,ItemKind::FieldHammer)
        ||ownedItems(after,ItemKind::TrailCompass)>ownedItems(before,ItemKind::TrailCompass)
        ||ownedItems(after,ItemKind::TrailStaff)>ownedItems(before,ItemKind::TrailStaff)) {
        bool reachable=false;std::string reason;
        for(const auto& component:before.components)if(component.kind==FurnitureKind::Workbench
            &&reachableComponent(before,component.id,queries,reason,content.freeBuilding)) {reachable=true;break;}
        if(!reachable) {error="Use a nearby accessible workbench";return false;}
    }
    if(after.registeredBed&&(after.registeredBed!=before.registeredBed||after.recovery!=before.recovery||after.health>before.health)) {
        PlayerPose recovery;
        if(!reachableComponent(before,after.registeredBed,queries,error,content.freeBuilding)
            ||!usableBed(before,after.registeredBed,queries,recovery,error))return false;
        if(glm::length(glm::dvec3(recovery.x-after.recovery.x,recovery.y-after.recovery.y,recovery.z-after.recovery.z))>.001) {
            error="Use the bed's safe recovery point";return false;
        }
    }
    error.clear();return true;
}
std::vector<PlacePart> starterRoomLayout(GridPosition origin,uint8_t yaw,std::string& error) {
    if(yaw>3||!construction::isValid(origin)){error="Invalid starter room placement";return {};}
    std::vector<PlacePart> result;
    const auto add=[&](PieceKind kind,int32_t x,int32_t y,int32_t z,uint8_t turn=0) {
        int64_t px=x,pz=z;
        switch(yaw) {case 1:px=z;pz=-int64_t(x);break;case 2:px=-int64_t(x);pz=-int64_t(z);break;
            case 3:px=-int64_t(z);pz=x;break;default:break;}
        const int64_t wx=px+origin.x,wy=int64_t(y)+origin.y,wz=pz+origin.z;
        if(std::min({wx,wy,wz})<=INT32_MIN||std::max({wx,wy,wz})>INT32_MAX)return false;
        result.push_back({0,kind,{int32_t(wx),int32_t(wy),int32_t(wz)},uint8_t((yaw+turn)%4),0});return true;
    };
    bool valid=true;
    for(int32_t x:{-50,50})for(int32_t z:{-50,50})valid=add(PieceKind::Foundation,x,0,z)&&valid;
    for(int32_t x:{-50,50})valid=add(PieceKind::Wall,x,16,-92)&&valid;
    valid=add(PieceKind::Doorway,-50,16,92)&&valid;valid=add(PieceKind::Wall,50,16,92)&&valid;
    for(int32_t z:{-50,50}) {valid=add(PieceKind::Wall,-108,16,z,1)&&valid;valid=add(PieceKind::Wall,108,16,z,1)&&valid;}
    for(int32_t x:{-50,50})for(int32_t z:{-50,50})valid=add(PieceKind::Roof,x,160,z)&&valid;
    valid=add(PieceKind::Bed,38,16,-25)&&valid;
    valid=add(PieceKind::Chest,-63,16,-25)&&valid;
    valid=add(PieceKind::Workbench,38,16,50)&&valid;
    if(!valid){error="Starter room lies outside the construction lattice";return {};}
    error.clear();return result;
}
} // namespace voxy::game::adventure
