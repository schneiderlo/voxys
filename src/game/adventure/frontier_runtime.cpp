#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/frontier_interactions.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace voxy::game::adventure {
namespace {
constexpr double actorRadius=AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale;
constexpr double actorHeight=AdventurePlayer::height*AdventurePlayer::creativeScale;
constexpr double dashWindow=.4,dashCooldown=.95;
glm::dvec3 feet(PlayerPose p){return {p.x,p.y,p.z};}
glm::dvec3 metres(GridPosition p){return glm::dvec3(p.x,p.y,p.z)*.02;}
PlayerPose pose(const AdventurePlayer& player){const auto p=player.feet();return {p.x,p.y,p.z,player.facingYaw()};}

}

void AdventureRuntime::frontierSound(std::string_view event) {
    if(!frontierAudioEnabled_)return;
    frontierAudioEvent_=event;++frontierAudioSequence_;
}
bool AdventureRuntime::commitFrontierEvent(std::optional<AdventureSession::PreparedChange> change) {
    if(!change)return false;
    // Combat and receipts never rebuild the thousands of accepted static boxes.
    // Any geometry/furniture/depletion edit must use the full publication path.
    const auto& after=change->state();
    if(after.structures!=state().structures||after.components!=state().components||after.depletedNodes!=state().depletedNodes) {
        status_="This action needs the world geometry update path.";return false;
    }
    if(!session_->commit(std::move(*change),status_))return false;
    saveStatus_="Changes not saved";return true;
}
void AdventureRuntime::initializeFrontierActors() {
    frontierActors_.clear();frontierSeconds_=0;frontierAttackSeconds_=0;frontierHurtSeconds_=0;frontierDodgeSeconds_=0;
    for(const auto& record:state().frontier.enemies) {
        FrontierActor actor;actor.id=record.id;
        if(record.health) {
            const auto start=feet(record.pose);
            actor.active=actor.controller.initialize(queries_,start,double(installedWorld().waterHeight),record.pose.yaw,
                AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius);
            // Static authored props may have claimed a spawn's edge. Admit the
            // nearest genuinely supported clear capsule; never teleport an
            // active pursuer or change its durable health/loot records.
            for(int ring=1;!actor.active&&ring<=4;++ring)for(int sector=0;!actor.active&&sector<8;++sector) {
                const double angle=sector*std::numbers::pi/4;auto p=start+glm::dvec3(std::cos(angle)*ring*1.5,0,std::sin(angle)*ring*1.5);
                const double y=queries_.supportHeight({p.x,p.z},actorRadius,start.y+4);
                if(!std::isfinite(y)||std::abs(y-start.y)>4)continue;
                p.y=y+.005;
                actor.active=actor.controller.initialize(queries_,p,double(installedWorld().waterHeight),record.pose.yaw,
                    AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius);
            }
            actor.cooldown=.7+double(record.id%3)*.2;
        }
        frontierActors_.push_back(std::move(actor));
    }
}
void AdventureRuntime::checkpointFrontier() {
    if(!session_||!content_.frontier)return;
    std::vector<FrontierEnemyPose> poses;
    for(const auto& actor:frontierActors_) {
        const auto* record=frontierEnemy(state(),actor.id);
        if(actor.active&&record&&record->health&&record->pose!=pose(actor.controller))poses.push_back({actor.id,pose(actor.controller)});
    }
    if(poses.empty())return;
    const auto check=[this,poses](const AdventureState&,const AdventureState&,std::string& error) {
        for(const auto& enemy:poses)if(!queries_.clearCapsule(feet(enemy.pose),actorRadius,actorHeight)) {
            error="An enemy checkpoint has no clear standing space.";return false;
        }
        return true;
    };
    (void)commitFrontierEvent(session_->prepareFrontierCheckpointEnemies(stamp(),poses,check,status_));
}
void AdventureRuntime::advanceFrontier(double seconds,AdventureCombat::Input input) {
    if(!session_||!std::isfinite(seconds)||seconds<0)return;
    pendingAttack_|=input.attack;pendingDodge_|=input.dodge;pendingJump_|=input.movement.jump;
    frontierSeconds_+=std::min(seconds,.25);
    constexpr double dt=AdventurePlayer::fixedStep;
    for(int frame=0;frame<15&&frontierSeconds_+1e-12>=dt;++frame) {
        frontierSeconds_-=dt;frontierTime_+=dt;
        frontierAttackSeconds_=std::max(0.,frontierAttackSeconds_-dt);
        frontierHurtSeconds_=std::max(0.,frontierHurtSeconds_-dt);
        frontierDodgeSeconds_=std::max(0.,frontierDodgeSeconds_-dt);
        frontierMilestoneSeconds_=std::max(0.,frontierMilestoneSeconds_-dt);
        const bool attack=std::exchange(pendingAttack_,false),dodge=std::exchange(pendingDodge_,false);
        auto movement=input.movement;movement.jump=std::exchange(pendingJump_,false);
        if(!state().health)continue;
        if(dodge&&frontierDodgeSeconds_<=0) {
            frontierDodgeSeconds_=dashCooldown;frontierDodgeDirection_=movement.movement;
            if(glm::length(frontierDodgeDirection_)<.1)frontierDodgeDirection_={std::sin(player_.facingYaw()),std::cos(player_.facingYaw())};
            else frontierDodgeDirection_=glm::normalize(frontierDodgeDirection_);
            frontierSound("dodge");
        }
        if(frontierDodgeSeconds_>dashCooldown-dashWindow) {
            movement.movement=frontierDodgeDirection_;movement.speedScale=2.;movement.jump=false;
        }
        const auto beforePlayer=player_.state();player_.advance(dt,movement);
        if(frontierDodgeSeconds_>dashCooldown-dashWindow) {
            auto dodged=player_.state();dodged.facingYaw=beforePlayer.facingYaw;
            (void)player_.restore(dodged); // Backstep keeps the weapon facing the threat for the counterattack.
        }
        bool overlap=false;
        for(const auto& actor:frontierActors_) {
            const auto* record=frontierEnemy(state(),actor.id);if(!actor.active||!record||!record->health)continue;
            const auto delta=actor.controller.feet()-player_.feet();
            if(std::abs(delta.y)<actorHeight&&glm::length(glm::dvec2(delta.x,delta.z))<actorRadius*1.8){overlap=true;break;}
        }
        if(overlap)(void)player_.restore(beforePlayer);
        std::string error;if(!session_->updatePlayer(pose(player_),state().health,error)){(void)player_.restore(beforePlayer);status_=error;}
        for(const auto& site:content_.frontierSites) {
            const auto* progress=frontierSite(state(),site.id);if(!progress||progress->discoveredRevision)continue;
            const auto position=site.position;
            const auto entered=[position](const AdventureState& before,const AdventureState&,std::string& reason) {
                if(std::hypot(before.player.x-position.x,before.player.z-position.z)>22||std::abs(before.player.y-position.y)>20) {
                    reason="Come closer to discover this landmark.";return false;
                }
                return true;
            };
            if(!entered(state(),state(),error))continue;
            if(commitFrontierEvent(session_->prepareFrontierDiscover(stamp(),site.id,entered,status_))) {
                const auto destination=std::find_if(frontierWorld_.destinations().begin(),frontierWorld_.destinations().end(),
                    [&](const auto& value){return value.id==site.id;});
                frontierMilestone_=destination==frontierWorld_.destinations().end()?"New landmark discovered":std::string(destination->name);
                frontierMilestoneSeconds_=5;frontierSound("discover");saveRequested_=true;
            }
        }
        if(attack&&frontierAttackSeconds_<=0&&frontierDodgeSeconds_<=dashCooldown-dashWindow) {
            const auto weapon=state().equippedTool.kind;
            if(weapon!=ItemKind::TrailStaff&&weapon!=ItemKind::QuarryHammer)status_="Equip your trail staff or quarry hammer from the bag.";
            else {
                frontierAttackSeconds_=.55;frontierSound("swing");
                FrontierActor* target=nullptr;double best=9;
                for(auto& actor:frontierActors_) {
                    const auto* record=frontierEnemy(state(),actor.id);if(!actor.active||!record||!record->health)continue;
                    const auto p=pose(actor.controller);const double distance=glm::length(feet(p)-player_.feet());
                    if(distance<best&&frontierStrikeReachable(walkQueries_,state().player,p,8)){best=distance;target=&actor;}
                }
                if(target) {
                    const auto id=target->id;const auto targetPose=pose(target->controller);
                    const auto check=[this,targetPose](const AdventureState& before,const AdventureState&,std::string& reason) {
                        if(!frontierStrikeReachable(walkQueries_,before.player,targetPose,8)){reason="The strike is blocked or out of reach.";return false;}return true;
                    };
                    if(commitFrontierEvent(session_->prepareFrontierEnemyHit(stamp(),id,weapon==ItemKind::QuarryHammer?36:28,targetPose,check,status_))) {
                        const auto definition=std::find_if(content_.frontierEnemies.begin(),content_.frontierEnemies.end(),[id](const auto& e){return e.id==id;});
                        const auto archetype=definition==content_.frontierEnemies.end()?FrontierEnemyArchetype::Scout:definition->archetype;
                        const auto profile=frontierCombatProfile(archetype);
                        if(target->attack.phase==FrontierAttackPhase::Idle) {
                            const auto away=glm::dvec2(state().player.x-targetPose.x,state().player.z-targetPose.z);
                            if(glm::length(away)>.001)target->attack.direction=glm::normalize(away);
                        }
                        const bool interrupted=frontierInterruptAttack(target->attack,profile);
                        target->stagger=interrupted?.2:.1;
                        target->windup=target->attack.phase==FrontierAttackPhase::Windup?target->attack.seconds:0;
                        frontierSound("hit");
                        const auto* record=frontierEnemy(state(),id);
                        if(record&&!record->health) {
                            target->active=false;status_="Raider defeated. Collect the fallen supplies with E.";
                            frontierMilestone_="The trail is yours";frontierMilestoneSeconds_=2.5;saveRequested_=true;
                        } else status_=archetype==FrontierEnemyArchetype::Scout?"Hit! The scout is retreating - press your advantage."
                            :interrupted?"The brute is exposed. Press the attack before it recovers.":"The brute is committed! Dodge sideways, then punish its recovery.";
                    }
                }
            }
        }
        for(auto& actor:frontierActors_) {
            const auto* record=frontierEnemy(state(),actor.id);if(!actor.active||!record||!record->health)continue;
            actor.cooldown=std::max(0.,actor.cooldown-dt);actor.stagger=std::max(0.,actor.stagger-dt);
            const auto definition=std::find_if(content_.frontierEnemies.begin(),content_.frontierEnemies.end(),[&](const auto& e){return e.id==actor.id;});
            if(definition==content_.frontierEnemies.end())continue;
            const auto p=actor.controller.feet(),player=player_.feet();
            const double distance=glm::length(glm::dvec2(player.x-p.x,player.z-p.z));
            const auto profile=frontierCombatProfile(definition->archetype);
            frontierAdvanceAttack(actor.attack,profile,dt);
            actor.windup=actor.attack.phase==FrontierAttackPhase::Windup?actor.attack.seconds:0;
            const auto moveActor=[&](glm::dvec2 direction,double speed,bool steer) {
                const double length=glm::length(direction);
                if(length<1e-6||speed<=0){actor.controller.advance(dt,{});return 0.;}
                direction/=length;
                const double strength=std::min(1.,speed),scale=std::max(1.,speed);
                const auto original=actor.controller;
                AdventurePlayer trial=original;trial.advance(dt,{direction*strength,false,scale,0});
                auto bestPosition=trial.feet();double score=glm::dot(glm::dvec2(bestPosition.x-p.x,bestPosition.z-p.z),direction);
                AdventurePlayer chosen=trial;
                // Only ordinary navigation may steer. A telegraphed dart or
                // charge commits to its shown line and collides with real cover.
                if(steer&&score<.018)for(const double angle:std::array<double,4>{.9,-.9,1.6,-1.6}) {
                    const glm::dvec2 alternate(direction.x*std::cos(angle)-direction.y*std::sin(angle),direction.x*std::sin(angle)+direction.y*std::cos(angle));
                    trial=original;trial.advance(dt,{alternate*strength,false,scale,0});const auto point=trial.feet();
                    const auto delta=glm::dvec2(point.x-p.x,point.z-p.z);
                    const double candidate=glm::dot(delta,direction)+glm::length(delta)*.12;
                    if(candidate>score+.001){score=candidate;chosen=trial;bestPosition=point;}
                }
                bool clear=std::abs(bestPosition.y-player.y)>=actorHeight
                    ||glm::length(glm::dvec2(bestPosition.x-player.x,bestPosition.z-player.z))>=actorRadius*1.9;
                for(const auto& other:frontierActors_)if(clear&&other.id!=actor.id&&other.active) {
                    const auto delta=other.controller.feet()-bestPosition;
                    if(std::abs(delta.y)<actorHeight&&glm::length(glm::dvec2(delta.x,delta.z))<actorRadius*1.85)clear=false;
                }
                if(clear)actor.controller=chosen;
                if(!steer) {
                    auto locked=actor.controller.state();locked.facingYaw=std::atan2(-actor.attack.direction.x,-actor.attack.direction.y);
                    (void)actor.controller.restore(locked);
                }
                return clear?glm::length(actor.controller.feet()-p):0.;
            };
            if(actor.attack.phase==FrontierAttackPhase::Windup) {
                actor.controller.advance(dt,{});continue;
            }
            if(actor.attack.phase==FrontierAttackPhase::Strike) {
                const double moved=moveActor(actor.attack.direction,profile.strikeSpeed,false);
                const auto enemyPose=pose(actor.controller);
                const bool reachable=frontierStrikeReachable(walkQueries_,enemyPose,state().player,profile.strikeRange,profile.strikeCone);
                const bool dodging=frontierDodgeSeconds_>dashCooldown-dashWindow;
                const bool spent=actor.attack.hitSpent;
                if(frontierSpendStrikeContact(actor.attack,reachable,dodging||frontierHurtSeconds_>0)) {
                    const auto check=[this,enemyPose,profile](const AdventureState& before,const AdventureState&,std::string& reason) {
                        if(!frontierStrikeReachable(walkQueries_,enemyPose,before.player,profile.strikeRange,profile.strikeCone)) {
                            reason="The warden's attack was blocked.";return false;
                        }
                        return true;
                    };
                    if(state().health&&commitFrontierEvent(session_->prepareFrontierPlayerHit(stamp(),profile.damage,check,status_))) {
                        frontierHurtSeconds_=.75;frontierSound("hurt");
                        status_=state().health?"Hit! "+combatBindingLabel(preferences_,CombatAction::Dodge,padAim_)+" dodges the committed attack. Counter while the warden recovers."
                            :"You fell. Press E to recover at camp. Your creations and supplies are safe.";
                        if(!state().health){frontierMilestone_="Rise again, builder";frontierMilestoneSeconds_=8;saveRequested_=true;}
                    }
                } else if(!spent&&actor.attack.hitSpent&&dodging) {
                    status_="Clean dodge! The warden's attack is spent - turn and counter.";frontierSound("dodge");
                }
                if(moved<.004)frontierEndStrike(actor.attack,profile);
                continue;
            }
            if(actor.attack.phase==FrontierAttackPhase::Recovery) {
                // Scouts give ground after their dart; brutes remain vulnerable
                // at the end of a charge. Neither can start another attack here.
                (void)moveActor(-actor.attack.direction,profile.retreatSpeed,false);continue;
            }
            if(actor.stagger>0)continue;
            const bool hunting=state().health&&distance<45&&glm::length(p-feet(definition->spawn))<65;
            // Sight is only needed when an idle actor can begin a new attack.
            if(hunting&&distance<profile.triggerRange&&std::abs(p.y-player.y)<3&&actor.cooldown<=0
                &&frontierCombatSight(walkQueries_,p,player)) {
                // Nearby attackers take turns announcing a strike, rather than
                // overlapping two warnings into unavoidable damage.
                bool otherCommitted=false;
                for(const auto& other:frontierActors_)if(other.id!=actor.id&&other.active
                    &&(other.attack.phase==FrontierAttackPhase::Windup||other.attack.phase==FrontierAttackPhase::Strike)
                    &&glm::length(other.controller.feet()-player)<18)otherCommitted=true;
                if(!otherCommitted&&frontierBeginAttack(actor.attack,profile,{player.x-p.x,player.z-p.z})) {
                    auto facing=actor.controller.state();facing.facingYaw=std::atan2(p.x-player.x,p.z-player.z);
                    (void)actor.controller.restore(facing);actor.windup=actor.attack.seconds;
                    status_=definition->archetype==FrontierEnemyArchetype::Brute
                        ?"Brute charging up! "+combatBindingLabel(preferences_,CombatAction::Dodge,padAim_)+" sideways - it cannot turn."
                        :"Scout winding up! Dodge the dart, then close the gap.";
                    frontierSound(definition->archetype==FrontierEnemyArchetype::Brute?"charge":"warn");continue;
                }
            }
            const auto home=feet(definition->spawn);auto goal=hunting?player:home;
            if(!hunting&&glm::length(p-home)<12) {
                const auto leg=static_cast<uint64_t>(frontierTime_/9.+double(actor.id)*.7)&1u;
                auto patrol=home+glm::dvec3(leg?7.:-7.,0,actor.id%2?3.:-3.);
                const double y=walkQueries_.supportHeight({patrol.x,patrol.z},actorRadius,home.y+4);
                if(std::isfinite(y)&&std::abs(y-home.y)<4) {
                    patrol.y=y+.005;
                    if(walkQueries_.clearCapsule(patrol,actorRadius,actorHeight))goal=patrol;
                }
            }
            auto direction=glm::dvec2(goal.x-p.x,goal.z-p.z);const double length=glm::length(direction);
            if(length<3||(hunting&&distance<profile.triggerRange*.8)) {
                actor.controller.advance(dt,{});continue;
            }
            (void)moveActor(direction,hunting?profile.approachSpeed:.45,true);
        }
    }
}

std::string AdventureRuntime::frontierInteractionLabel() const {
    if(!state().health)return "Recover at camp";
    const auto target=frontierNearbyTarget(state(),content_,queries_,frontierWorld_.resident());
    switch(target.kind) {
    case FrontierUseKind::Loot:return "Collect raider supplies";
    case FrontierUseKind::Resource: {
        const auto found=std::find_if(content_.resourceNodes.begin(),content_.resourceNodes.end(),[&](const auto& node){return node.id==target.id;});
        if(found==content_.resourceNodes.end())break;
        return found->yield.kind==ItemKind::CutStone
            ?(state().equippedTool.kind==ItemKind::QuarryHammer?"Quarry dense stone":"Equip quarry hammer to mine")
            :"Gather "+std::string(itemDefinition(found->yield.kind)->name);
    }
    case FrontierUseKind::Site: {
        const auto found=std::find_if(content_.frontierSites.begin(),content_.frontierSites.end(),[&](const auto& site){return site.id==target.id;});
        if(found==content_.frontierSites.end())break;
        if(found->kind==FrontierSiteKind::Beacon) {
            const auto remaining=frontierRemainingDefenders(state(),*found);
            if(remaining)return "Clear "+std::to_string(remaining)+" beacon warden"+(remaining==1?"":"s");
            if(found->requiresQuarryCharge&&!frontierHasQuarryHarvest(state(),content_))return "Bring a charge from the quarry";
            return "Restore the beacon";
        }
        return found->kind==FrontierSiteKind::Cache?"Open the high cache":"Explore the quarry";
    }
    case FrontierUseKind::Furniture: {
        const auto* component=AdventureSession::findComponent(state(),target.id);if(!component)break;
        if(component->kind==FurnitureKind::Workbench)return "Use field workbench";
        if(component->kind==FurnitureKind::Chest)return "Open supply chest";
        if(component->kind==FurnitureKind::Door)return component->doorOpen?"Close door":"Open door";
        return "Rest and register camp";
    }
    case FrontierUseKind::Resident:return state().health<100?"Rest with the beacon keeper":"Talk to the beacon keeper";
    case FrontierUseKind::None:break;
    }
    return "Explore / gather / build";
}
void AdventureRuntime::useFrontier() {
    if(!state().health){frontierRecover();return;}
    const auto target=frontierNearbyTarget(state(),content_,queries_,frontierWorld_.resident());
    const auto reached=[this,target](const AdventureState&,const AdventureState&,std::string& error) {
        const auto current=frontierNearbyTarget(state(),content_,queries_,frontierWorld_.resident());
        if(current.kind!=target.kind||current.id!=target.id){error="Move closer and leave a clear path to use it.";return false;}return true;
    };
    if(target.kind==FrontierUseKind::Loot) {
        if(commitFrontierEvent(session_->prepareFrontierLoot(stamp(),uint32_t(target.id),reached,status_))) {
            status_="Raider supplies recovered.";frontierSound("gather");saveRequested_=true;
        }
        return;
    }
    if(target.kind==FrontierUseKind::Resource) {
        const auto node=std::find_if(content_.resourceNodes.begin(),content_.resourceNodes.end(),[&](const auto& n){return n.id==target.id;});
        const auto check=[this,reached](const AdventureState& before,const AdventureState& after,std::string& error) {
            return reached(before,after,error)&&validator()(before,after,error);
        };
        if(commit(session_->prepareGather(stamp(),uint32_t(target.id),check,status_))) {
            status_="Gathered "+std::string(node==content_.resourceNodes.end()?"supplies":itemDefinition(node->yield.kind)->name)+". The source is now depleted.";
            frontierSound("gather");saveRequested_=true;
        }
        return;
    }
    if(target.kind==FrontierUseKind::Resident) {
        if(state().health<100) {
            const auto safe=[this,reached](const AdventureState& before,const AdventureState& after,std::string& reason) {
                if(!reached(before,after,reason))return false;
                for(const auto& actor:frontierActors_)if(actor.active&&glm::length(actor.controller.feet()-player_.feet())<20) {
                    reason="Lose the wardens before resting with the keeper.";return false;
                }
                return true;
            };
            if(commitFrontierEvent(session_->prepareFrontierRest(stamp(),safe,status_))) {
                status_="The keeper tends your wounds. Health restored; your supplies are safe.";
                frontierMilestone_="Rest by the keeper's fire";frontierMilestoneSeconds_=4;frontierSound("recover");saveRequested_=true;
            }
            return;
        }
        frontierMilestone_="Bring light back to the frontier";frontierMilestoneSeconds_=7;
        status_="The keeper: Find the high cache. Clear Dawnreach's two wardens, build a field workbench and restore its flame. Return to my fire if you need healing.";
        frontierSound("discover");return;
    }
    if(target.kind==FrontierUseKind::Furniture) {
        const auto* component=AdventureSession::findComponent(state(),target.id);if(!component)return;
        if(component->kind==FurnitureKind::Door){useDoor(component->id,!component->doorOpen,component->revision);return;}
        if(component->kind==FurnitureKind::Chest){chest_=component->id;menu_=Menu::Chest;menuSelection_=0;player_.discardPendingInput();status_="Store supplies here or take them with you.";return;}
        if(component->kind==FurnitureKind::Workbench){bench_=component->id;menu_=Menu::Workbench;menuSelection_=0;player_.discardPendingInput();status_="Craft a tool using your gathered supplies.";return;}
        for(const auto& actor:frontierActors_)if(actor.active&&glm::length(actor.controller.feet()-player_.feet())<20) {
            status_="Clear the nearby raiders before resting.";return;
        }
        PlayerPose recovery;
        if(usableBed(state(),component->id,queries_,recovery,status_)
            &&commit(session_->prepareUseBed(stamp(),component->id,recovery,validator(),status_))) {
            status_="Rested. This sheltered camp is your recovery point.";frontierSound("recover");saveRequested_=true;
        }
        return;
    }
    if(target.kind==FrontierUseKind::Site) {
        const auto site=std::find_if(content_.frontierSites.begin(),content_.frontierSites.end(),[&](const auto& s){return s.id==target.id;});
        if(site==content_.frontierSites.end())return;
        if(site->kind==FrontierSiteKind::Cache) {
            if(commitFrontierEvent(session_->prepareFrontierClaimSite(stamp(),site->id,reached,status_))) {
                if(frontierTrackedSite_==site->id)frontierTrackedSite_=0;
                status_="High cache recovered: +18 scrap. Your route made the difference.";frontierMilestone_="A way where there was none";
                frontierMilestoneSeconds_=5;frontierSound("discover");saveRequested_=true;
            }
            return;
        }
        if(site->kind==FrontierSiteKind::Quarry) {
            if(commitFrontierEvent(session_->prepareFrontierDiscover(stamp(),site->id,reached,status_))) {
                status_="Dense quarry stone. Equip a quarry hammer, then gather enough cut stone for the outer beacons.";
                frontierMilestone_="The Old Quarry";frontierMilestoneSeconds_=5;frontierSound("discover");saveRequested_=true;
            }
            return;
        }
        if(!frontierBeaconRequirements(state(),content_,*site,status_))return;
        uint64_t station=0;double best=100;
        for(const auto& component:state().components)if(component.kind==FurnitureKind::Workbench) {
            const auto* part=AdventureSession::findPart(state(),component.part);if(!part)continue;
            const double distance=glm::length(metres(part->position)-feet(site->position));std::string reason;
            if(distance<20&&distance<best&&reachableComponent(state(),component.id,queries_,reason,true)){station=component.id;best=distance;}
        }
        if(!station){status_="Build a field workbench beside this beacon, then stand within reach of both.";return;}
        const auto sitePosition=site->position;
        const auto check=[this,reached,station,sitePosition](const AdventureState& before,const AdventureState& after,std::string& error) {
            if(!reached(before,after,error)||!reachableComponent(before,station,queries_,error,true))return false;
            const auto* component=AdventureSession::findComponent(before,station);
            const auto* part=component?AdventureSession::findPart(before,component->part):nullptr;
            if(!part||glm::length(metres(part->position)-feet(sitePosition))>=20){error="The field workbench must be beside this beacon.";return false;}
            return validator()(before,after,error);
        };
        const bool first=!state().frontier.quarryUnlockRevision;
        if(commitFrontierEvent(session_->prepareFrontierRestoreBeacon(stamp(),site->id,station,check,status_))) {
            if(frontierTrackedSite_==site->id)frontierTrackedSite_=0;
            unsigned lit=0;for(const auto& record:state().frontier.sites)if(record.restoredRevision)++lit;
            frontierMilestone_=lit>=3?"DAWNREACH AWAKENS":"A light against the dark";frontierMilestoneSeconds_=lit>=3?12:8;
            status_=first?"Beacon restored. Craft a quarry hammer at your field workbench; its first materials are in your bag."
                :lit>=3?"All three beacons shine. You brought the frontier back to life. Your camps and routes remain yours to shape."
                :site->requiresQuarryCharge?"The quarry charge burns in Last Ember's sanctuary. Another flame waits beyond the woods."
                :"Stormwatch is free of its guardians. Another flame waits beyond the woods.";
            frontierSound(lit>=3?"victory":"beacon");saveRequested_=true;
        }
        return;
    }
    status_="Approach a resource, fallen supplies, camp furniture or a beacon and press E.";
}
void AdventureRuntime::frontierRecover() {
    if(!session_)return;
    if(state().health)for(const auto& actor:frontierActors_)if(actor.active&&glm::length(actor.controller.feet()-player_.feet())<20) {
        status_="Retreat from the raiders before returning to camp.";return;
    }
    PlayerPose recovery=content_.town;std::string reason;
    if(state().registeredBed)(void)usableBed(state(),state().registeredBed,queries_,recovery,reason);
    const auto safe=[this](PlayerPose point) {
        if(!queries_.clearCapsule(feet(point),actorRadius,actorHeight))return false;
        for(const auto& actor:frontierActors_)if(actor.active&&glm::length(actor.controller.feet()-feet(point))<20)return false;
        return true;
    };
    if(!safe(recovery))recovery=content_.town;
    if(!safe(recovery)){status_="The recovery point is blocked. Clear space at camp before returning.";return;}
    AdventurePlayer checked;
    if(!checked.initialize(queries_,feet(recovery),double(installedWorld().waterHeight),recovery.yaw,AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius)) {
        status_="No supported recovery space is available.";return;
    }
    const auto check=[safe,recovery](const AdventureState&,const AdventureState&,std::string& error) {
        if(!safe(recovery)){error="The recovery point became unsafe.";return false;}return true;
    };
    if(!commitFrontierEvent(session_->prepareRecover(stamp(),recovery,check,status_)))return;
    player_=checked;player_.discardPendingInput();swimAnimation_.reset();motorbike_.reset();riding_=false;
    frontierHurtSeconds_=2;frontierDodgeSeconds_=0;frontierAttackSeconds_=0;frontierSeconds_=0;
    pendingAttack_=false;pendingDodge_=false;pendingJump_=false;discontinuity_=true;menu_=Menu::None;building_=false;
    frontierMilestone_="BACK AT CAMP";frontierMilestoneSeconds_=4;interactionFeedbackSeconds_=4;
    status_="Recovered at camp. Every item, creation and restored beacon is safe.";frontierSound("recover");saveRequested_=true;
}
} // namespace voxy::game::adventure
