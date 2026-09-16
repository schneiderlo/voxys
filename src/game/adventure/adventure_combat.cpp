#include "game/adventure/adventure_combat.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::game::adventure {
namespace {
glm::dvec3 feet(PlayerPose p) noexcept {return {p.x,p.y,p.z};}
double horizontal(PlayerPose a,PlayerPose b) noexcept {return glm::length(glm::dvec2(a.x-b.x,a.z-b.z));}
void face(PlayerPose& from,PlayerPose to) noexcept {
    if(horizontal(from,to)>1e-8)from.yaw=std::atan2(from.x-to.x,from.z-to.z);
}
void phase(EnemyCombatCheckpoint& enemy,EnemyPhase value) noexcept {enemy.phase=value;enemy.phaseTicks=0;}
bool overlap(PlayerPose a,PlayerPose b) noexcept {
    // Conservative AABBs match the installed actor collision packet. Keep a
    // little separation so publication can never engulf the player capsule.
    return std::abs(a.x-b.x)<.66&&std::abs(a.z-b.z)<.66&&std::abs(a.y-b.y)<1.71;
}
uint16_t ticks(double seconds) noexcept {return static_cast<uint16_t>(std::ceil(seconds/AdventurePlayer::fixedStep));}
}
bool AdventureCombat::initialize(const AdventureContent& content,const AdventureSpatialQueries& world,double waterHeight) {
    if(!std::isfinite(waterHeight)||!world.revision())return false;
    waterHeight_=waterHeight;
    for(size_t i=0;i<walkers_.size();++i) {
        const auto& spawn=content.encounters[i].spawn;auto& walker=walkers_[i];
        // 32 m square encloses the 12 m leash plus grid attachment margin.
        LayeredNavigation::Region region{int32_t(std::floor(spawn.x/4))-4,int32_t(std::floor(spawn.z/4))-4,8,8,
            spawn.y-8,spawn.y+8,waterHeight};
        if(!walker.graph.configure(region)||!walker.graph.synchronize(world))return false;
        walker.configured=true;
    }
    reset();return true;
}
void AdventureCombat::reset() noexcept {
    for(auto& walker:walkers_){walker.following=false;walker.nextRequest=0;walker.follower=NavigationFollower{};}
}
bool AdventureCombat::sight(const AdventureSpatialQueries& world,PlayerPose from,PlayerPose to) noexcept {
    const auto start=feet(from)+glm::dvec3(0,1,0),end=feet(to)+glm::dvec3(0,1,0);
    const auto delta=end-start;const auto distance=glm::length(delta);
    if(!std::isfinite(distance))return false;
    if(distance<1e-8)return true;
    const auto hit=world.raycast(start,delta/distance,distance);
    return hit.complete&&(!hit.hit||hit.distance>=distance-.02);
}
bool AdventureCombat::hitArc(const AdventureSpatialQueries& world,PlayerPose from,PlayerPose to,double reach) noexcept {
    const glm::dvec2 delta(to.x-from.x,to.z-from.z);const double distance=glm::length(delta);
    if(!std::isfinite(distance)||distance>reach||std::abs(from.y-to.y)>.85)return false;
    const glm::dvec2 forward(-std::sin(from.yaw),-std::cos(from.yaw));
    return (distance<1e-8||glm::dot(delta/distance,forward)>=.5)&&sight(world,from,to);
}
bool AdventureCombat::fighting(EnemyPhase value) noexcept {
    return value==EnemyPhase::Notice||value==EnemyPhase::Chase||value==EnemyPhase::Windup||value==EnemyPhase::Attack||value==EnemyPhase::Recover;
}
std::string_view AdventureCombat::phaseLabel(EnemyPhase value) noexcept {
    switch(value) {
    case EnemyPhase::Dormant:return "Waiting for a clear trail";
    case EnemyPhase::Idle:case EnemyPhase::Patrol:return "Watching the trail";
    case EnemyPhase::Notice:return "Spotted you";
    case EnemyPhase::Chase:return "Approaching";
    case EnemyPhase::Windup:return "Staff raised — step aside";
    case EnemyPhase::Attack:return "Swing";
    case EnemyPhase::Recover:return "Open to a counterattack";
    case EnemyPhase::Return:return "Returning to the trail";
    case EnemyPhase::Dead:return "Defeated";
    }
    return "";
}
bool AdventureCombat::walk(size_t index,EnemyCombatCheckpoint& enemy,glm::dvec3 target,uint64_t tick,
    const AdventureEncounters& admitted,const AdventureSpatialQueries& world,PlayerPose player,
    const std::array<EnemyCombatCheckpoint,kAdventureEncounterCount>& enemies) {
    auto& walker=walkers_[index];if(!walker.configured)return false;
    if(!walker.actor.initialize(world,feet(enemy.pose),waterHeight_,enemy.pose.yaw))return false;
    if(!walker.graph.synchronize(world)){walker.following=false;return false;}
    const bool stale=walker.following&&!walker.graph.valid(world,walker.graph.path());
    const auto search=walker.graph.status();
    if(stale){walker.following=false;walker.nextRequest=0;}
    if(tick>=walker.nextRequest&&search!=LayeredNavigation::SearchStatus::Searching
        &&search!=LayeredNavigation::SearchStatus::Building) {
        if(!walker.following||glm::length(target-walker.goal)>.8) {
            walker.following=false;walker.goal=target;
            (void)walker.graph.request(world,feet(enemy.pose),target);walker.nextRequest=tick+30;
        }
    }
    if(walker.graph.status()==LayeredNavigation::SearchStatus::Found&&!walker.following) {
        walker.following=walker.follower.begin(world,walker.graph,walker.graph.path(),walker.actor);
        if(!walker.following)walker.nextRequest=0;
    }
    if(!walker.following)return false;
    const auto outcome=walker.follower.advance(world,walker.graph,walker.actor);
    if(outcome!=NavigationFollower::FollowStatus::Walking)walker.following=false;
    const auto p=walker.actor.feet();const PlayerPose proposed{p.x,p.y,p.z,walker.actor.facingYaw()};
    if(!admitted.movementAllowed(enemy.encounterId,p,world)||overlap(proposed,player)) {walker.following=false;return false;}
    for(size_t i=0;i<enemies.size();++i)if(i!=index&&enemies[i].positioned&&enemies[i].health
        &&admitted.find(enemies[i].encounterId)->available&&overlap(proposed,enemies[i].pose)){walker.following=false;return false;}
    const bool moved=horizontal(proposed,enemy.pose)>1e-7;enemy.pose=proposed;return moved;
}
CombatTick AdventureCombat::step(const AdventureState& state,const AdventureContent& content,
    const AdventureEncounters& admitted,const AdventureSpatialQueries& world,AdventurePlayer& player,Input input) {
    CombatTick next;next.tick=state.combat.tick+1;next.player=state.player;next.health=state.health;next.playerCombat=state.combat.player;
    for(size_t i=0;i<next.enemies.size();++i)next.enemies[i]=state.combat.encounters[i].checkpoint;
    auto& combat=next.playerCombat;
    if(!state.health){input={};combat.attackImpactTick=0;combat.dodgeUntilTick=0;combat.invulnerableUntilTick=0;combat.dodgeDirectionX=0;combat.dodgeDirectionZ=0;}
    // Warm each graph incrementally, including while its resident watches from
    // a distance. Never do a whole local graph or unbounded A* in one frame.
    for(size_t i=0;i<walkers_.size();++i)if(walkers_[i].configured) {
        (void)walkers_[i].graph.synchronize(world);work_[i]=walkers_[i].graph.advance(world,16,4);
    }
    if(state.health&&input.dodge&&next.tick>=combat.dodgeReadyTick&&player.mode()==AdventurePlayer::Mode::Walking) {
        auto direction=input.movement.movement;
        if(glm::length(direction)<.1)direction={std::sin(player.facingYaw()),std::cos(player.facingYaw())};
        direction=glm::normalize(direction);combat.dodgeDirectionX=direction.x;combat.dodgeDirectionZ=direction.y;
        combat.dodgeUntilTick=next.tick+dodgeTicks;combat.invulnerableUntilTick=next.tick+dodgeTicks;
        combat.dodgeReadyTick=next.tick+dodgeRecoveryTicks;combat.attackImpactTick=0;
    }
    if(state.health&&input.attack&&state.equippedTool.kind==ItemKind::TrailStaff&&next.tick>=combat.attackReadyTick
        &&next.tick>=combat.dodgeUntilTick) {
        ++combat.attackSerial;combat.attackImpactTick=next.tick+staffWindupTicks;combat.attackReadyTick=next.tick+staffRecoveryTicks;
    }
    if(next.tick<combat.dodgeUntilTick)input.movement={{combat.dodgeDirectionX,combat.dodgeDirectionZ},false,2};
    else {combat.dodgeUntilTick=0;combat.dodgeDirectionX=0;combat.dodgeDirectionZ=0;}
    if(next.tick>=combat.invulnerableUntilTick)combat.invulnerableUntilTick=0;
    if(state.health)player.advance(AdventurePlayer::fixedStep,input.movement);
    const auto p=player.feet();next.player={p.x,p.y,p.z,player.facingYaw()};
    const bool strike=state.health&&combat.attackImpactTick==next.tick;
    if(strike)combat.attackImpactTick=0;
    for(size_t i=0;i<next.enemies.size();++i) {
        auto& enemy=next.enemies[i];const auto& installed=content.encounters[i];
        const auto* entry=admitted.find(enemy.encounterId);const auto* definition=encounterDefinition(enemy.encounterId);
        if(!definition||!entry||!entry->available||!enemy.health)continue;
        if(!enemy.positioned){enemy.positioned=true;enemy.pose=entry->pose;phase(enemy,EnemyPhase::Idle);}
        if(strike&&enemy.lastPlayerAttackSerial<combat.attackSerial&&hitArc(world,next.player,enemy.pose,1.8)) {
            enemy.lastPlayerAttackSerial=combat.attackSerial;
            enemy.health=enemy.health>staffDamage?uint16_t(enemy.health-staffDamage):0;
            if(!enemy.health){phase(enemy,EnemyPhase::Dead);walkers_[i].following=false;continue;}
        }
        if(enemy.phaseTicks<kMaximumCombatDeadlineTicks)++enemy.phaseTicks;
        const double distance=horizontal(enemy.pose,next.player);
        const bool visible=sight(world,enemy.pose,next.player);
        const bool leashed=glm::length(glm::dvec2(next.player.x,next.player.z)-definition->anchor)<=definition->leashRadius;
        if((!next.health||!leashed)&&fighting(enemy.phase))phase(enemy,EnemyPhase::Return);
        switch(enemy.phase) {
        case EnemyPhase::Dormant:break;
        case EnemyPhase::Idle:case EnemyPhase::Patrol:
            if(next.health&&distance<=definition->noticeRadius&&leashed&&visible){phase(enemy,EnemyPhase::Notice);face(enemy.pose,next.player);}
            break;
        case EnemyPhase::Notice:
            face(enemy.pose,next.player);if(enemy.phaseTicks>=24)phase(enemy,EnemyPhase::Chase);break;
        case EnemyPhase::Chase:
            if(distance<=definition->reach&&visible){face(enemy.pose,next.player);phase(enemy,EnemyPhase::Windup);}
            else { (void)walk(i,enemy,feet(next.player),next.tick,admitted,world,next.player,next.enemies);
                if(enemy.phaseTicks>=480)phase(enemy,EnemyPhase::Return); }
            break;
        case EnemyPhase::Windup:
            // Facing freezes at the start. Walking around the visible windup
            // therefore works even without spending the dodge cooldown.
            if(enemy.phaseTicks>=ticks(definition->attackWindup)) {
                ++enemy.attackSerial;phase(enemy,EnemyPhase::Attack);
                if(next.tick>=combat.invulnerableUntilTick&&hitArc(world,enemy.pose,next.player,definition->reach))
                    next.health=next.health>definition->damage?uint16_t(next.health-definition->damage):0;
            }
            break;
        case EnemyPhase::Attack:phase(enemy,EnemyPhase::Recover);break;
        case EnemyPhase::Recover:if(enemy.phaseTicks>=ticks(definition->recovery))phase(enemy,EnemyPhase::Chase);break;
        case EnemyPhase::Return:
            if(horizontal(enemy.pose,installed.spawn)<.1) {
                if(state.combat.encounters[i].checkpoint.phase==EnemyPhase::Return) {
                    enemy.health=installed.maximumHealth;phase(enemy,EnemyPhase::Idle);
                }
            }
            else (void)walk(i,enemy,feet(installed.spawn),next.tick,admitted,world,next.player,next.enemies);
            break;
        case EnemyPhase::Dead:break;
        }
    }
    if(!next.health){combat.attackImpactTick=0;combat.dodgeUntilTick=0;combat.invulnerableUntilTick=0;combat.dodgeDirectionX=0;combat.dodgeDirectionZ=0;}
    return next;
}
} // namespace voxy::game::adventure
