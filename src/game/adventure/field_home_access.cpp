#include "game/adventure/field_home_access.hpp"

#include "game/adventure/construction_policy.hpp"
#include "game/adventure/layered_navigation.hpp"
#include "game/adventure/world_definition.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
using Status=LayeredNavigation::SearchStatus;
using FollowStatus=NavigationFollower::FollowStatus;

bool allowance(size_t& count,size_t maximum,FieldHomeAccessWork& work,size_t amount=1) {
    if(work.exhausted||count>maximum||amount>maximum-count) {work.exhausted=true;return false;}
    count+=amount;return true;
}
bool validWork(FieldHomeAccessWork& work) {
    if(work.bedChecks>work.maximumBedChecks||work.furnitureChecks>work.maximumFurnitureChecks
        ||work.approaches>work.maximumApproaches||work.sampledColumns>work.maximumSampledColumns
        ||work.controllerTicks>work.maximumControllerTicks||work.navigationSteps>work.maximumNavigationSteps)
        work.exhausted=true;
    return !work.exhausted;
}
auto solidKey(const Solid& solid) {
    return std::tie(solid.structure,solid.part,solid.minimum.x,solid.minimum.y,solid.minimum.z,
        solid.maximum.x,solid.maximum.y,solid.maximum.z);
}
// A stale state must not lend a missing roof/bed to the recovery check. Other
// accepted scenery stays in queries and participates in all movement and LOS.
bool publishedStructure(const AdventureState& state,const AdventureSpatialQueries& queries,uint64_t structure) {
    std::vector<Solid> expected;std::string error;
    if(!compileSolids(state,expected,error))return false;
    std::erase_if(expected,[&](const auto& solid){return solid.structure.counter!=structure;});
    std::vector<Solid> accepted;
    for(const auto& solid:queries.solids())
        if(solid.structure.world==state.world&&solid.structure.counter==structure)accepted.push_back(solid);
    if(expected.empty()||expected.size()!=accepted.size())return false;
    const auto less=[](const auto& a,const auto& b){return solidKey(a)<solidKey(b);};
    std::sort(expected.begin(),expected.end(),less);std::sort(accepted.begin(),accepted.end(),less);
    return std::equal(expected.begin(),expected.end(),accepted.begin(),
        [](const auto& a,const auto& b){return solidKey(a)==solidKey(b);});
}
bool canUseAt(AdventureState& observer,const AdventureSpatialQueries& queries,uint64_t component,glm::dvec3 p) {
    if(!queries.clearCapsule(p))return false;
    observer.player={p.x,p.y,p.z,observer.player.yaw};std::string error;
    return reachableComponent(observer,component,queries,error);
}
bool inRegion(const LayeredNavigation::Region& region,glm::dvec3 p) {
    const glm::dvec2 low(double(region.firstTileX)*4,double(region.firstTileZ)*4);
    const glm::dvec2 high=low+glm::dvec2(16);
    return p.x>=low.x+.31&&p.x<=high.x-.31&&p.z>=low.y+.31&&p.z<=high.y-.31
        &&p.y>=region.minimumFeet&&p.y<=region.maximumFeet;
}
bool initializeProbe(AdventurePlayer& probe,const AdventureSpatialQueries& queries,PlayerPose recovery) {
    return probe.initialize(queries,{recovery.x,recovery.y,recovery.z},double(installedWorld().waterHeight),recovery.yaw)
        &&probe.mode()==AdventurePlayer::Mode::Walking;
}
bool directApproach(AdventureState& observer,const AdventureSpatialQueries& queries,uint64_t component,
    PlayerPose recovery,glm::dvec3 goal,const LayeredNavigation::Region& region,FieldHomeAccessWork& work) {
    AdventurePlayer probe;if(!initializeProbe(probe,queries,recovery))return false;
    // An inexpensive straight walk handles open rooms without building a graph.
    // Each attempted pose comes from the actual solver, including its inertia.
    for(size_t step=0;step<192;++step) {
        const auto before=probe.feet();
        if(canUseAt(observer,queries,component,before))return true;
        const glm::dvec2 delta(goal.x-before.x,goal.z-before.z);const double distance=glm::length(delta);
        if(distance<.008)return false;
        if(!allowance(work.controllerTicks,work.maximumControllerTicks,work))return false;
        probe.advance(AdventurePlayer::fixedStep,{delta*(std::min(1.,distance/.06)/distance),false});
        if(probe.mode()!=AdventurePlayer::Mode::Walking||!inRegion(region,probe.feet())
            ||glm::length(probe.feet()-before)<1e-7)return false;
    }
    return canUseAt(observer,queries,component,probe.feet());
}
bool prepareNavigation(LayeredNavigation& nav,const AdventureSpatialQueries& queries,
    const LayeredNavigation::Region& region,FieldHomeAccessWork& work) {
    constexpr size_t columns=16*LayeredNavigation::cellsPerTile;
    // Reserve the complete local graph before allocating or sampling it.
    if(work.sampledColumns>work.maximumSampledColumns-columns) {work.exhausted=true;return false;}
    if(!nav.configure(region)||!nav.synchronize(queries))return false;
    for(size_t chunk=0;chunk<16;++chunk) {
        const auto result=nav.advance(queries,64,0);
        work.sampledColumns+=result.columns;
        if(result.status!=Status::Building)return result.status==Status::Ready;
    }
    return false;
}
bool navigatedApproach(AdventureState& observer,const AdventureSpatialQueries& queries,uint64_t component,
    PlayerPose recovery,glm::dvec3 goal,LayeredNavigation& nav,FieldHomeAccessWork& work) {
    auto status=nav.request(queries,{recovery.x,recovery.y,recovery.z},goal);
    while(status==Status::Searching) {
        // One expansion can examine eight neighbors, eight layers each, with
        // at most 24 real solver ticks per edge. Reserve that worst-case room
        // before calling, then account only actual work. This may refuse a
        // complex route before using the final 1535 ticks; it never overruns.
        constexpr size_t maximumStepTicks=8*LayeredNavigation::maximumLayers*24;
        if(work.controllerTicks>work.maximumControllerTicks-maximumStepTicks) {work.exhausted=true;return false;}
        if(!allowance(work.navigationSteps,work.maximumNavigationSteps,work))return false;
        const auto result=nav.advance(queries,0,1);
        work.controllerTicks+=result.controllerTicks;status=result.status;
    }
    if(status!=Status::Found)return false;
    AdventurePlayer probe;if(!initializeProbe(probe,queries,recovery))return false;
    NavigationFollower follower;if(!follower.begin(queries,nav,nav.path(),probe))return false;
    // Edge probes alone cannot certify a route: replay the accepted path with
    // one continuous controller and finally apply ordinary furniture reach.
    for(size_t step=0;step<512;++step) {
        if(canUseAt(observer,queries,component,probe.feet()))return true;
        if(work.controllerTicks==work.maximumControllerTicks) {work.exhausted=true;return false;}
        const auto before=probe.tick();const auto follow=follower.advance(queries,nav,probe);
        // The follower advances at most one controller tick per call.
        work.controllerTicks+=static_cast<uint32_t>(probe.tick()-before);
        if(follow==FollowStatus::Arrived)return canUseAt(observer,queries,component,probe.feet());
        if(follow!=FollowStatus::Walking)return false;
    }
    return false;
}
} // namespace

bool fieldFurnitureAccessible(const AdventureState& state,const AdventureSpatialQueries& queries,
    uint64_t structureId,FurnitureKind kind,FieldHomeAccessWork* suppliedWork) {
    FieldHomeAccessWork local;auto& work=suppliedWork?*suppliedWork:local;
    if(!validWork(work)||!structureId||(kind!=FurnitureKind::Chest&&kind!=FurnitureKind::Workbench)
        ||!construction::isValid(state.world)||!queries.revision()||!queries.terrain().valid()
        ||state.components.size()>kMaximumComponents||state.structures.size()>kMaximumStructures)return false;
    const auto structure=std::find_if(state.structures.begin(),state.structures.end(),
        [&](const auto& entry){return entry.id==structureId&&entry.owner==1;});
    if(structure==state.structures.end())return false;
    size_t parts=0;
    for(const auto& entry:state.structures) {
        if(entry.parts.size()>kMaximumParts-parts)return false;
        parts+=entry.parts.size();
    }
    const auto* bed=AdventureSession::findComponent(state,state.registeredBed);
    if(!bed||bed->owner!=1||bed->structure!=structureId||bed->kind!=FurnitureKind::Bed)return false;
    const auto bedPart=std::find_if(structure->parts.begin(),structure->parts.end(),
        [&](const auto& part){return part.id==bed->part&&part.kind==PieceKind::Bed;});
    if(bedPart==structure->parts.end())return false;
    if(!allowance(work.bedChecks,work.maximumBedChecks,work)||!publishedStructure(state,queries,structureId))return false;
    PlayerPose recovery;std::string error;
    if(!usableBed(state,bed->id,queries,recovery,error)||!AdventureSession::validPose(recovery))return false;
    LayeredNavigation::Region region;
    region.firstTileX=int32_t(std::round(recovery.x/4))-2;
    region.firstTileZ=int32_t(std::round(recovery.z/4))-2;
    region.tilesX=region.tilesZ=4;region.minimumFeet=recovery.y-.64;region.maximumFeet=recovery.y+3.84;
    region.waterHeight=double(installedWorld().waterHeight);
    AdventureState observer=state;LayeredNavigation nav;bool navigationReady=false,navigationAttempted=false;
    for(const auto& component:state.components) {
        if(component.owner!=1||component.structure!=structureId||component.kind!=kind)continue;
        if(!allowance(work.furnitureChecks,work.maximumFurnitureChecks,work))return false;
        const auto part=std::find_if(structure->parts.begin(),structure->parts.end(),
            [&](const auto& entry){return entry.id==component.part;});
        if(part==structure->parts.end())continue;
        const auto* definition=buildingDefinition(part->kind);
        if(!definition||definition->furniture!=kind)continue;
        AdventurePlayer recoveryProbe;if(!initializeProbe(recoveryProbe,queries,recovery))continue;
        if(canUseAt(observer,queries,component.id,recoveryProbe.feet()))return true;
        glm::dvec3 low(INFINITY),high(-INFINITY);
        for(const auto& solid:queries.solids())if(solid.structure.world==state.world
            &&solid.structure.counter==structureId&&solid.part.counter==part->id) {
            low=glm::min(low,solid.minimum);high=glm::max(high,solid.maximum);
        }
        if(!std::isfinite(low.x))continue;
        const double x=(low.x+high.x)*.5,z=(low.z+high.z)*.5,padding=.36;
        const std::array<glm::dvec2,8> points{{{low.x-padding,z},{high.x+padding,z},{x,low.z-padding},{x,high.z+padding},
            {low.x-padding,low.z-padding},{low.x-padding,high.z+padding},
            {high.x+padding,low.z-padding},{high.x+padding,high.z+padding}}};
        for(const auto point:points) {
            if(!inRegion(region,{point.x,recovery.y,point.y}))continue;
            if(!allowance(work.approaches,work.maximumApproaches,work))return false;
            std::array<double,LayeredNavigation::maximumLayers> heights{};
            const auto column=queries.walkableFeet(point,region.minimumFeet,region.maximumFeet,heights);
            if(!column.complete)continue;
            for(size_t layer=0;layer<column.count;++layer) {
                const glm::dvec3 goal(point.x,heights[layer],point.y);
                if(goal.y<region.waterHeight-.65)continue;
                // LOS at an unvisited candidate is a rejection filter only.
                // It can never grant success until a real controller gets there
                // (or reaches another clear point with ordinary interaction).
                if(!canUseAt(observer,queries,component.id,goal))continue;
                if(directApproach(observer,queries,component.id,recovery,goal,region,work))return true;
                if(work.exhausted)return false;
                if(!navigationAttempted) {
                    navigationAttempted=true;navigationReady=prepareNavigation(nav,queries,region,work);
                }
                if(work.exhausted)return false;
                if(navigationReady&&navigatedApproach(observer,queries,component.id,recovery,goal,nav,work))return true;
                if(work.exhausted)return false;
            }
        }
    }
    return false;
}
} // namespace voxy::game::adventure
