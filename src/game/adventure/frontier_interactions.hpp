#pragma once
#include "game/adventure/frontier_world.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/adventure_player.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::game::adventure {
// Both the visible E prompt and its command resolve this same live geometry.
inline bool frontierInteractionSight(const AdventureSpatialQueries& world,glm::dvec3 from,glm::dvec3 to,uint64_t allowedPart=0) {
    const auto delta=to-from;const double distance=glm::length(delta);
    if(distance<1e-6)return true;
    const auto hit=world.raycast(from,delta/distance,distance);
    return hit.complete&&(!hit.hit||hit.distance>=distance-.08||(allowedPart&&hit.part.counter==allowedPart));
}
inline bool frontierCombatSight(const AdventureSpatialQueries& world,glm::dvec3 from,glm::dvec3 to) {
    return frontierInteractionSight(world,from+glm::dvec3(0,3.1,0),to+glm::dvec3(0,3.1,0));
}
inline bool frontierStrikeReachable(const AdventureSpatialQueries& world,PlayerPose from,PlayerPose to,double range,double cone=.25) {
    const auto delta=glm::dvec2(to.x-from.x,to.z-from.z);const double distance=glm::length(delta);
    if(distance>range||std::abs(from.y-to.y)>3.0)return false;
    const auto forward=glm::dvec2(-std::sin(from.yaw),-std::cos(from.yaw));
    return (distance<.1||glm::dot(delta/distance,forward)>=cone)
        &&frontierCombatSight(world,{from.x,from.y,from.z},{to.x,to.y,to.z});
}
enum class FrontierUseKind {None,Loot,Resource,Site,Furniture,Resident};
struct FrontierUseTarget {FrontierUseKind kind=FrontierUseKind::None;uint64_t id=0;};
inline FrontierUseTarget frontierNearbyTarget(const AdventureState& state,const AdventureContent& content,const AdventureSpatialQueries& queries,glm::dvec3 resident) {
    const auto at=[](PlayerPose p){return glm::dvec3(p.x,p.y,p.z);};
    const auto player=at(state.player);const auto eye=player+glm::dvec3(0,4.0,0);
    FrontierUseTarget target;double best=1e9;
    const auto consider=[&](FrontierUseKind kind,uint64_t id,double score){if(score<best){best=score;target={kind,id};}};
    for(const auto& enemy:state.frontier.enemies)if(enemy.deathRevision&&!enemy.lootClaimRevision) {
        const auto point=at(enemy.pose);const double distance=glm::length(point-player);
        if(distance<7&&std::abs(point.y-player.y)<3&&frontierInteractionSight(queries,eye,point+glm::dvec3(0,.5,0)))
            consider(FrontierUseKind::Loot,enemy.id,distance-2);
    }
    for(const auto& node:content.resourceNodes) {
        if(std::binary_search(state.depletedNodes.begin(),state.depletedNodes.end(),node.id))continue;
        const auto point=at(node.position);const double distance=glm::length(point-player);
        // Match validateInteractions exactly: a higher target or lower eye can
        // show a usable prompt over cover that the authoritative gather refuses.
        const auto resourcePoint=point+glm::dvec3(0,1.2,0);
        const auto resourceEye=player+glm::dvec3(0,AdventurePlayer::eyeHeight*AdventurePlayer::creativeScale,0);
        if(distance>=8||std::abs(point.y-player.y)>=4
            ||glm::length(resourcePoint-player)>3.2*AdventurePlayer::creativeScale)continue;
        const auto delta=resourcePoint-resourceEye;const double reach=glm::length(delta);
        if(reach>1e-5) {
            const auto ray=queries.raycast(resourceEye,delta,reach);
            if(!ray.complete||(ray.hit&&ray.distance<reach-.05&&ray.part.counter!=FrontierWorld::resourcePart(node.id)))continue;
        }
        consider(FrontierUseKind::Resource,node.id,distance-.5);
    }
    for(const auto& component:state.components) {
        const auto* part=AdventureSession::findPart(state,component.part);if(!part)continue;
        const double distance=glm::length((glm::dvec3(part->position.x,part->position.y,part->position.z)*.02)-player);std::string reason;
        if(distance<8&&reachableComponent(state,component.id,queries,reason,true))consider(FrontierUseKind::Furniture,component.id,distance-1);
    }
    for(const auto& site:content.frontierSites) {
        const auto* progress=frontierSite(state,site.id);if(!progress)continue;
        if((site.kind==FrontierSiteKind::Cache&&progress->rewardClaimRevision)
            ||(site.kind==FrontierSiteKind::Quarry&&progress->discoveredRevision)
            ||(site.kind==FrontierSiteKind::Beacon&&progress->restoredRevision))continue;
        const auto point=at(site.position);const double distance=glm::length(point-player);
        // The cache sits on a broken elevated platform: proximity from its foot
        // is deliberately insufficient. Any real route to its height works.
        const double vertical=site.kind==FrontierSiteKind::Cache?1.:4.;
        if(distance<10&&std::abs(point.y-player.y)<vertical&&frontierInteractionSight(queries,eye,point+glm::dvec3(0,1.5,0)))
            consider(FrontierUseKind::Site,site.id,site.kind==FrontierSiteKind::Beacon&&distance<4?distance-20:distance+1);
    }
    const double residentDistance=glm::length(resident-player);
    if(residentDistance<8&&frontierInteractionSight(queries,eye,resident+glm::dvec3(0,3,0)))consider(FrontierUseKind::Resident,0,residentDistance);
    return target;
}
} // namespace voxy::game::adventure
