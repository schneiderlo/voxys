#include "game/adventure/adventure_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {
namespace {
glm::vec4 paint(uint32_t rgb) {
    return render::opaqueSrgbPaintOverride({uint8_t(rgb>>16),uint8_t(rgb>>8),uint8_t(rgb),255});
}
std::string materialLabel(MaterialCost cost) {
    std::string text;
    const auto append=[&](uint16_t count,std::string_view name){if(count){if(!text.empty())text+=" + ";text+=std::to_string(count)+" "+std::string(name);}};
    append(cost.wood,"wood");append(cost.stone,"stone");append(cost.scrap,"scrap");return text;
}
std::string keyBadge(const AdventurePreferences& preferences,CombatAction action,bool pad) {
    auto text=combatBindingLabel(preferences,action,pad);
    if(text=="Left click"||text=="Left mouse")return "LMB";
    if(text=="Right click"||text=="Right mouse")return "RMB";
    if(text=="Middle click"||text=="Middle mouse")return "MMB";
    return text.size()<=5?text:"";
}
}

void AdventureRuntime::frontierBurst(glm::dvec3 point,uint32_t colour,uint8_t kind) {
    if(!frontier_||preferences_.reducedMotion)return;
    if(frontierBursts_.size()>=32)frontierBursts_.erase(frontierBursts_.begin());
    frontierBursts_.push_back({point,frontierTime_,colour,kind});
}
void AdventureRuntime::sampleFrontierFeedback() {
    const auto& now=state().frontier;
    if(frontierFeedbackInitialized_) {
        for(const auto& enemy:now.enemies) {
            const auto old=std::find_if(frontierPresented_.enemies.begin(),frontierPresented_.enemies.end(),
                [&](const auto& item){return item.id==enemy.id;});
            if(old!=frontierPresented_.enemies.end()&&enemy.health<old->health)
                frontierBurst({enemy.pose.x,enemy.pose.y+2.8,enemy.pose.z},enemy.health?0xffa660:0xffdf86,enemy.health?0:2);
        }
        for(const auto& site:now.sites) {
            const auto old=std::find_if(frontierPresented_.sites.begin(),frontierPresented_.sites.end(),
                [&](const auto& item){return item.id==site.id;});
            if(old==frontierPresented_.sites.end())continue;
            const auto destination=std::find_if(frontierWorld_.destinations().begin(),frontierWorld_.destinations().end(),
                [&](const auto& item){return item.id==site.id;});
            if(destination==frontierWorld_.destinations().end())continue;
            if(!old->restoredRevision&&site.restoredRevision)frontierBurst(destination->feet,0xffdf89,3);
            else if(!old->discoveredRevision&&site.discoveredRevision&&frontierMilestoneSeconds_<=0) {
                frontierMilestone_=destination->name;frontierMilestoneSeconds_=4;
            }
        }
        for(const auto id:state().depletedNodes)if(!std::binary_search(frontierPresentedDepletion_.begin(),frontierPresentedDepletion_.end(),id)) {
            const auto node=std::find_if(content_.resourceNodes.begin(),content_.resourceNodes.end(),[&](const auto& item){return item.id==id;});
            if(node!=content_.resourceNodes.end())frontierBurst({node->position.x,node->position.y+1,node->position.z},
                node->yield.kind==ItemKind::Wood?0xbbd383:node->yield.kind==ItemKind::CutStone?0x80e5ee:0xffd98c,2);
        }
    }
    frontierPresented_=now;frontierPresentedDepletion_=state().depletedNodes;frontierFeedbackInitialized_=true;
    std::erase_if(frontierBursts_,[&](const auto& burst){return frontierTime_-burst.started>(burst.kind==3?9.:1.1);});
}

void AdventureRuntime::fillFrontierHud() {
    if(!frontier_)return;
    auto& hud=hudContent_;
    // Focused menu rows already show their full label. Keep catalogue hover
    // names for its otherwise unlabelled radial piece thumbnails.
    if(menu_!=Menu::None&&menu_!=Menu::Catalog){hud.hoverLabel.clear();hud.hoverBounds={};}
    hud.frontier=true;hud.creative=true;hud.health=state().health;hud.maxHealth=100;
    hud.wood=itemCount(state().backpack,ItemKind::Wood);hud.stone=itemCount(state().backpack,ItemKind::Stone);
    hud.scrap=itemCount(state().backpack,ItemKind::Scrap);
    hud.region="Dawnreach Highlands";
    double regionDistance=75;
    for(const auto& destination:frontierWorld_.destinations()) {
        const double distance=glm::length(destination.feet-player_.feet());
        if(distance<regionDistance){regionDistance=distance;hud.region=destination.region;}
    }
    size_t restored=0,total=0;
    for(const auto& site:content_.frontierSites)if(site.kind==FrontierSiteKind::Beacon) {
        ++total;const auto* progress=frontierSite(state(),site.id);if(progress&&progress->restoredRevision)++restored;
    }
    hud.chapter=restored==total&&total?"DAWN RETURNS":restored?"FIRES ACROSS THE FRONTIER":"THE FIRST LIGHT";
    hud.objectiveProgress=std::to_string(restored)+" / "+std::to_string(total)+" beacons rekindled";
    hud.milestone=frontierMilestoneSeconds_>0?frontierMilestone_:"";
    const FrontierSiteContent* target=nullptr;
    const auto byId=[&](uint32_t id)->const FrontierSiteContent* {
        for(const auto& site:content_.frontierSites) {if(site.id==id)return &site;}
        return nullptr;
    };
    if(frontierTrackedSite_)target=byId(frontierTrackedSite_);
    const bool ownsHammer=state().equippedTool.kind==ItemKind::QuarryHammer||itemCount(state().backpack,ItemKind::QuarryHammer)>0;
    const bool needsHammer=restored>0&&restored<total&&!ownsHammer;
    const bool needsStone=restored>0&&restored<total&&ownsHammer&&itemCount(state().backpack,ItemKind::CutStone)<4;
    if(!target) {
        if(restored==0)for(const auto& site:content_.frontierSites) {
            const auto* progress=frontierSite(state(),site.id);
            if(site.kind==FrontierSiteKind::Cache&&progress&&!progress->rewardClaimRevision){target=&site;break;}
        }
        if(!target&&needsHammer)for(const auto& site:content_.frontierSites) {
            const auto* progress=frontierSite(state(),site.id);
            if(site.kind==FrontierSiteKind::Beacon&&progress&&progress->restoredRevision){target=&site;break;}
        }
        if(!target&&needsStone)for(const auto& site:content_.frontierSites)
            if(site.kind==FrontierSiteKind::Quarry){target=&site;break;}
        if(!target)for(const auto& site:content_.frontierSites) {
            const auto* progress=frontierSite(state(),site.id);
            if(site.kind==FrontierSiteKind::Beacon&&progress&&!progress->restoredRevision){target=&site;break;}
        }
    }
    bool nearbyBase=false;
    if(target)for(const auto& structure:state().structures)for(const auto& part:structure.parts)
        if((part.kind==PieceKind::FrontierFoundation||part.kind==PieceKind::Pier)
            &&glm::length(glm::dvec2(double(part.position.x)*.02-target->position.x,
                double(part.position.z)*.02-target->position.z))<22)nearbyBase=true;
    hud.objectiveBearingVisible=target!=nullptr;hud.objectiveDistance.clear();
    if(target) {
        const auto destination=std::find_if(frontierWorld_.destinations().begin(),frontierWorld_.destinations().end(),
            [&](const auto& value){return value.id==target->id;});
        hud.objectiveTitle=destination!=frontierWorld_.destinations().end()?std::string(destination->name):"Explore the frontier";
        const auto delta=glm::dvec3(target->position.x,target->position.y,target->position.z)-player_.feet();
        hud.objectiveDistance=std::to_string(static_cast<int>(std::ceil(glm::length(delta))))+" studs";
        hud.objectiveBearingDegrees=adventureNavigationBearing({delta.x,delta.z});
        if(target->kind==FrontierSiteKind::Cache)
            hud.objectiveDetail=glm::length(delta)<24?(nearbyBase?
                "Add trail stairs to your base. Rotate with R, climb, then jump to the cache.":
                "Start with a foundation beside the ruin. Add stairs on your base to reach the high cache."):
                "Follow the worn path to the broken aqueduct. Build a way up to its lost supplies.";
        else if(target->kind==FrontierSiteKind::Quarry)
            hud.objectiveDetail="Equip the quarry hammer. Harvest cut stone for the outer beacons.";
        else if(needsHammer&&frontierSite(state(),target->id)&&frontierSite(state(),target->id)->restoredRevision) {
            hud.objectiveTitle="Forge the quarry hammer";
            hud.objectiveDetail="Use your field bench. The new recipe needs 4 wood, 4 stone and 4 scrap.";
        } else if(frontierRemainingDefenders(state(),*target)) {
            hud.objectiveTitle=target->id==2?"Take back the observatory":"Break the Stormwatch guard";
            hud.objectiveDetail=std::to_string(frontierRemainingDefenders(state(),*target))+" wardens remain. Sidestep the brute's charge; counter while it recovers.";
        } else if(target->requiresQuarryCharge&&!frontierHasQuarryHarvest(state(),content_)) {
            hud.objectiveTitle="Carry the quarry's ember";
            hud.objectiveDetail="Mine charged stone in the Old Quarry, then reach the woodland sanctuary.";
        } else hud.objectiveDetail=restored?
            "Build a field bench nearby. Restore with 12 wood, 12 stone, 8 scrap and 4 cut stone.":
            "Build a field bench nearby. Restore with 12 wood, 12 stone and 8 scrap.";
    } else {
        hud.objectiveTitle="The frontier is alight";
        hud.objectiveDetail="Your beacons endure. Explore, improve your camps and build new paths home.";
    }
    hud.context=frontierInteractionLabel();
    if(hud.context!="Explore / gather / build")hud.context=std::string(padAim_?"X  ":"E  ")+hud.context;
    if(hud.context.ends_with("Restore the beacon"))hud.context+=state().frontier.quarryUnlockRevision?
        "  /  12 wood + 12 stone + 8 scrap + 4 cut stone":"  /  12 wood + 12 stone + 8 scrap";
    for(const auto& site:content_.frontierSites)if(site.kind==FrontierSiteKind::Beacon) {
        const auto* progress=frontierSite(state(),site.id);
        if(progress&&!progress->restoredRevision&&!frontierRemainingDefenders(state(),site)
            &&(!site.requiresQuarryCharge||frontierHasQuarryHarvest(state(),content_))
            &&glm::length(player_.feet()-glm::dvec3(site.position.x,site.position.y,site.position.z))<14) {
            hud.objectiveDetail=state().frontier.quarryUnlockRevision?
                "Use the signal altar: 12 wood, 12 stone, 8 scrap, 4 cut stone. Leave room to reach your field bench nearby.":
                "Use the signal altar: 12 wood, 12 stone, 8 scrap. Build a reachable field bench nearby.";
            break;
        }
    }
    if(!state().health)hud.context="Your buildings and belongings are safe. Return to your camp.";
    for(const auto& actor:frontierActors_)if(actor.active&&glm::length(actor.controller.feet()-player_.feet())<22) {
        const auto enemy=std::find_if(content_.frontierEnemies.begin(),content_.frontierEnemies.end(),[&](const auto& item){return item.id==actor.id;});
        if(enemy==content_.frontierEnemies.end())continue;
        const bool brute=enemy->archetype==FrontierEnemyArchetype::Brute;
        if(actor.attack.phase==FrontierAttackPhase::Windup) {
            hud.context=brute?"BRUTE CHARGING - dodge sideways off the amber line":"SCOUT LUNGING - sidestep, then follow its retreat";
            break;
        }
        if(actor.attack.phase==FrontierAttackPhase::Recovery&&brute)hud.context="BRUTE EXPOSED - close in and strike";
    }
    if(menu_==Menu::None&&!building_&&interactionFeedbackSeconds_<=0)hud.status.clear();
    if(!saveFailure_.empty())hud.status=saveFailure_;
    if(const auto* definition=buildingDefinition(selected_))hud.cost=materialLabel(definition->cost);
    const uint32_t intent=menuIntents_.token()?menuIntents_.token()*64u+1u:0;
    const auto button=[&](std::string label,int action,std::string key="") {
        return render::AdventureHudRow{std::move(label),std::move(key),true,action,0,intent,0};
    };
    hud.topActions={button("Bag",24),button("Journal",16),button("Save",8),button("Menu",31)};
    hud.topActions[2].enabled=saveStatus_!="Saving...";
    if(saveStatus_=="Saving..."||!saveFailure_.empty()||saveFeedbackSeconds_>0)hud.topActions[2].detail=saveStatus_;
    if(state().health) {
        hud.quickActions={button("Use",7,padAim_?"X":"E"),button("Build",21,padAim_?"View":"B"),
            button("Attack",27,keyBadge(preferences_,CombatAction::Attack,padAim_)),button("Dodge",28,keyBadge(preferences_,CombatAction::Dodge,padAim_))};
        hud.quickActions[2].enabled=frontierAttackSeconds_<=0&&
            (state().equippedTool.kind==ItemKind::TrailStaff||state().equippedTool.kind==ItemKind::QuarryHammer);
        hud.quickActions[3].enabled=frontierDodgeSeconds_<=0;
        if(target&&!building_&&glm::length(player_.feet()-glm::dvec3(target->position.x,target->position.y,target->position.z))<25) {
            const auto* progress=frontierSite(state(),target->id);
            const auto buildChoice=[&](std::string label,PieceKind kind) {
                auto choice=button(std::move(label),2);choice.value=int(kind);
                for(size_t slot=0;slot<quickSlots_.size();++slot)if(quickSlots_[slot].kind==kind) {
                    choice.action=39;choice.value=int(slot+1);break;
                }
                hud.quickActions[1]=std::move(choice);
            };
            if(target->kind==FrontierSiteKind::Cache&&progress&&!progress->rewardClaimRevision) {
                buildChoice(nearbyBase?"Build stairs":"Build base",nearbyBase?PieceKind::FrontierStairs:PieceKind::FrontierFoundation);
            } else if(target->kind==FrontierSiteKind::Beacon&&progress&&!progress->restoredRevision&&!frontierRemainingDefenders(state(),*target)) {
                buildChoice(nearbyBase?"Build bench":"Build base",nearbyBase?PieceKind::FrontierWorkbench:PieceKind::FrontierFoundation);
            }
        }
    } else hud.quickActions={button("Return home",11),button("Save",8)};
    if(menu_==Menu::None&&building_) {
        const bool ownedTarget=targetPart_&&AdventureSession::findPart(state(),targetPart_);
        auto move=button(frontierMoving_?"Cancel move":"Move",43,"G");move.enabled=frontierMoving_.has_value()||ownedTarget;
        auto recolour=button("Repaint",44,"T");recolour.enabled=ownedTarget;
        auto redo=button("Redo",42,"CtrlY");redo.enabled=!frontierRedo_.empty();
        hud.buildControls.push_back(std::move(move));hud.buildControls.push_back(std::move(recolour));hud.buildControls.push_back(std::move(redo));
        for(auto& row:hud.buildControls) {
            if(row.action==6){row.enabled=!frontierUndo_.empty();row.detail="CtrlZ";}
            if(row.action==5) {
                const auto& binding=routingPreferences_.bindings[static_cast<size_t>(expedition::CoveAction::Remove)];
                row.detail=padAim_?"B":expedition::coveKeyLabel(binding.key,binding.modifiers);
                if(row.detail=="Delete")row.detail="Del";
                else if(row.detail=="Backspace")row.detail="Bksp";
            }
        }
    }
}

void AdventureRuntime::renderFrontier(glm::dvec3 origin) {
    if(!frontier_)return;
    sampleFrontierFeedback();
    size_t instances=0;
    const auto add=[&](render::MeshDrawInstance draw){if(instances<3200){meshes_.addInstance(draw);++instances;}};
    const auto brick=[&](PieceKind kind,glm::dvec3 feet,glm::dvec3 scale,uint32_t rgb,float glow=0,float yaw=0,bool shadow=true) {
        const auto root=glm::translate(glm::dmat4(1),feet-origin)
            *glm::rotate(glm::dmat4(1),double(yaw),glm::dvec3(0,1,0))*glm::scale(glm::dmat4(1),scale);
        for(const auto& visual:buildingVisuals(kind)) {
            const auto transform=root*glm::translate(glm::dmat4(1),visual.offset)*glm::scale(glm::dmat4(1),visual.scale);
            add({.assetIndex=0,.meshIndex=uint32_t(visual.source)-1,.modelMatrix=glm::mat4(transform),
                .emissiveBoost=glow,.castsSunShadow=shadow,.baseColorOverride=paint(rgb),.surface={0,0,1,0}});
        }
    };
    const double time=preferences_.reducedMotion?0:frontierTime_;
    for(const auto& visual:frontierWorld_.visuals()) {
        if(!frontierWorld_.visible(visual,state()))continue;
        if(glm::length(visual.feet-player_.feet())>650)continue;
        const auto* site=visual.site?frontierSite(state(),visual.site):nullptr;
        const bool restored=site&&site->restoredRevision;
        if(visual.light&&visual.kind==PieceKind::Beam&&!restored)continue;
        const bool fire=visual.light&&!visual.site;
        const float glow=visual.light?(restored?1.7f:fire?.06f+.02f*float(std::sin(time*7)):.025f):0.f;
        auto scale=visual.scale;
        if(fire)scale.y*=1.+.12*std::sin(time*9+visual.feet.x);
        brick(visual.kind,visual.feet,scale,visual.paint,glow,float(visual.yaw)*std::numbers::pi_v<float>*.5f,!visual.light);
        if(fire&&glm::length(visual.feet-player_.feet())<100)for(int i=0;i<3;++i) {
            const double lift=std::fmod(time*1.7+i*1.1,4.);
            brick(PieceKind::Brick2x2,visual.feet+glm::dvec3(std::sin(time+i)*.45,1+lift,std::cos(time*.8+i)*.35),
                {.045,.08*(1-lift/5),.045},0xffc066,.08f,0,false);
        }
    }
    for(const auto& burst:frontierBursts_) {
        const double age=time-burst.started;if(age<0)continue;
        if(burst.kind==3) {
            const double radius=3.+age*5.8;
            for(int i=0;i<32;++i) {
                const double angle=i*std::numbers::pi/16;
                const glm::dvec2 xz{burst.point.x+std::cos(angle)*radius,burst.point.z+std::sin(angle)*radius};
                const double ground=walkQueries_.supportHeight(xz,.3,burst.point.y+24);
                if(std::isfinite(ground))brick(PieceKind::Brick2x2,{xz.x,ground+.08,xz.y},{.35,.10,.35},burst.paint,float(.16*(1-age/9)),0,false);
            }
            continue;
        }
        const double fade=std::max(0.,1.-age/1.1);
        for(int i=0;i<10;++i) {
            const double angle=i*2.3999632297,spread=(burst.kind==1?2.2:3.4)*age;
            const auto point=burst.point+glm::dvec3(std::cos(angle)*spread,(2.5+(i%3)) *age-3.6*age*age,std::sin(angle)*spread);
            const double size=(burst.kind==0?.12:.16)*fade;
            brick(PieceKind::Brick2x2,point,{size,size,size},burst.paint,float(fade*.08),float(angle+age*4),false);
        }
    }
    // Small flocks cross the broad sky in loose formations. No collision or
    // simulation entity is implied by these distant silhouettes.
    if(!preferences_.reducedMotion)for(int flock=0;flock<2;++flock)for(int bird=0;bird<4;++bird) {
        const double phase=time*.025+flock*2.1;
        const auto point=frontierWorld_.start()+glm::dvec3(std::cos(phase)*85+bird*3,42+flock*14+std::sin(bird*2.)*3,std::sin(phase)*85+bird*1.6);
        const double flap=std::sin(time*5+bird)*.28;
        for(int wing:{-1,1})brick(PieceKind::Beam,point+glm::dvec3(wing*.65,flap,0),{.36,.18,.34},0x39454b,0,float(-phase),false);
    }
    if(frontierAttackSeconds_>.22&&!preferences_.reducedMotion) {
        const double swing=(.55-frontierAttackSeconds_)/.33;
        for(int i=0;i<5;++i) {
            const double angle=player_.facingYaw()-1.2+swing*2.4-i*.16;
            brick(PieceKind::Brick2x2,player_.feet()+glm::dvec3(-std::sin(angle)*3.7,2.5+i*.08,-std::cos(angle)*3.7),
                {.13,.08,.13},0xffe4a0,.8f,0,false);
        }
    }
    for(const auto& destination:frontierWorld_.destinations()) {
        const auto* progress=frontierSite(state(),destination.id);
        if(!progress||!progress->restoredRevision)continue;
        // Rising signal motes are a small visible consequence of restoration.
        // Every band is cosmetic, bounded and absent from collision authority.
        for(int band=0;band<3;++band)for(int spark=0;spark<4;++spark) {
            const double phase=std::fmod(time*.8+band*5.,15.),angle=time*.35+spark*std::numbers::pi/2+band;
            const double radius=.9+phase*.035;
            brick(PieceKind::Brick2x2,destination.feet+glm::dvec3(std::sin(angle)*radius,25+phase,1+std::cos(angle)*radius),
                {.11,.10,.11},0xffdd89,1.4f,0,false);
        }
    }
    for(const auto& site:content_.frontierSites)if(site.kind==FrontierSiteKind::Beacon) {
        const auto* progress=frontierSite(state(),site.id);
        const bool restored=progress&&progress->restoredRevision;
        const glm::dvec3 feet{site.position.x,site.position.y,site.position.z};
        if(glm::length(feet-player_.feet())>220)continue;
        // Low inlaid stones mark the exact authoritative service point. They
        // remain beneath step height and do not pretend to be a solid wall.
        brick(PieceKind::Brick2x4,feet,{.80,.14,.65},0xcdb88e,0,0,false);
        brick(PieceKind::Brick2x2,feet+glm::dvec3(0,.135,0),{.30,.08,.30},restored?0xffdf84:0x62b6c4,restored?.9f:.22f,0,false);
        for(int side:{-1,1})brick(PieceKind::Brick2x2,feet+glm::dvec3(side*1.3,.02,0),{.12,.06,.45},restored?0xffdf84:0x62b6c4,.15f,0,false);
    }
    const auto character=[&](const assets::RigidAnimationAsset& asset,uint32_t assetIndex,glm::dvec3 feet,double yaw,
        size_t clip,double phase,glm::vec4 tint=glm::vec4(1)) {
        assets::RigidAnimationPose pose;std::string error;
        if(!assets::sampleRigidAnimation(asset,asset.clips[clip],phase,true,{},glm::dmat4(1),pose,error))return;
        const auto root=glm::translate(glm::dmat4(1),feet-origin)*glm::rotate(glm::dmat4(1),yaw,glm::dvec3(0,1,0))
            *glm::scale(glm::dmat4(1),glm::dvec3(AdventurePlayer::creativeScale));
        for(uint32_t i=0;i<pose.drawCount;++i)add({.assetIndex=assetIndex,.meshIndex=pose.draws[i].meshIndex,
            .modelMatrix=glm::mat4(root*glm::dmat4(pose.draws[i].modelMatrix)),.tintColor=tint,.surface={0,0,1,0}});
    };
    if(residentAssets_[0]&&glm::length(frontierWorld_.resident()-player_.feet())<180) {
        const auto toward=player_.feet()-frontierWorld_.resident();
        const double yaw=glm::length(toward)<18?std::atan2(-toward.x,-toward.z):-.5;
        character(*residentAssets_[0],2,frontierWorld_.resident(),yaw,0,time);
    }
    for(const auto& actor:frontierActors_) {
        const auto* progress=frontierEnemy(state(),actor.id);if(!progress)continue;
        if(!progress->health) {
            if(progress->deathRevision&&!progress->lootClaimRevision) {
                const auto& position=progress->pose;
                brick(PieceKind::Chest,{position.x,position.y,position.z},{1.2,1.2,1.2},0xdfb965,.12f);
                brick(PieceKind::Brick2x2,{position.x,position.y+2.2+std::sin(time*2)*.15,position.z},{.18,.18,.18},0xffde8b,.7f,0,false);
            }
            continue;
        }
        if(!actor.active||!raider_)continue;
        const auto feet=actor.controller.feet();
        const double distance=glm::length(feet-player_.feet());if(distance>180)continue;
        const auto definition=std::find_if(content_.frontierEnemies.begin(),content_.frontierEnemies.end(),[&](const auto& enemy){return enemy.id==actor.id;});
        const bool brute=definition!=content_.frontierEnemies.end()&&definition->archetype==FrontierEnemyArchetype::Brute;
        const auto profile=frontierCombatProfile(brute?FrontierEnemyArchetype::Brute:FrontierEnemyArchetype::Scout);
        const double speed=glm::length(actor.controller.worldVelocity());
        const bool warning=actor.attack.phase==FrontierAttackPhase::Windup,striking=actor.attack.phase==FrontierAttackPhase::Strike;
        const size_t clip=warning||striking?6u:speed>.2?1u:0u;
        const double phase=warning?std::max(0.,profile.windup-actor.attack.seconds)*.6/profile.windup:striking?.5:time*(speed>.2?(brute?.8:1.5):1);
        character(*raider_,6,feet,actor.controller.facingYaw(),clip,phase,
            actor.stagger>0?glm::vec4(1.5f,.65f,.45f,1):brute?glm::vec4(1.1f,.83f,.68f,1):glm::vec4(.73f,1.04f,1.15f,1));
        const double facing=actor.controller.facingYaw();
        const auto local=[&](glm::dvec3 p){return feet+glm::dvec3(std::cos(facing)*p.x+std::sin(facing)*p.z,p.y,-std::sin(facing)*p.x+std::cos(facing)*p.z);};
        if(brute) {
            brick(PieceKind::Brick2x2,local({0,2.1,-.65}),{.61,1.25,.14},0x5b6873,0,float(facing));
            for(int side:{-1,1})brick(PieceKind::Brick2x2,local({side*1.05,3.25,0}),{.36,.38,.43},0xbd9357,0,float(facing));
            brick(PieceKind::Brick2x2,local({1.45,1.2,-.7}),{.43,.65,.43},0x657381,0,float(facing));
        } else {
            brick(PieceKind::Beam,local({0,3.55,-.62}),{.58,1.15,1.35},0x3c9dab,0,float(facing));
            brick(PieceKind::Brick2x2,local({-.5,2.8,.7}),{.23,.65,.16},0x3c9dab,0,float(facing));
        }
        if(distance<38) {
            if(definition!=content_.frontierEnemies.end()&&definition->maximumHealth) {
                const double fraction=std::clamp(double(progress->health)/double(definition->maximumHealth),0.,1.);
                const float yaw=-hudNavigation_.cameraBearingDegrees*std::numbers::pi_v<float>/180;
                brick(PieceKind::Brick2x2,feet+glm::dvec3(0,5.8,0),{1.6,.16,.07},0x263137,0,yaw,false);
                if(fraction>0)brick(PieceKind::Brick2x2,feet+glm::dvec3(0,5.84,0),{1.53*fraction,.08,.08},brute?0xef9a57:0x77d4dd,.05f,yaw,false);
            }
        }
        if(warning||striking)for(int marker=1;marker<=(brute?10:6);++marker) {
            const double distanceAhead=double(marker)*1.2;
            const glm::dvec2 xz{feet.x+actor.attack.direction.x*distanceAhead,feet.z+actor.attack.direction.y*distanceAhead};
            const double support=walkQueries_.supportHeight(xz,.25,feet.y+2);
            if(std::isfinite(support))brick(PieceKind::Brick2x2,{xz.x,support+.03,xz.y},{brute?.65:.34,.055,.34},brute?0xffb94c:0x89e5ed,striking?.15f:.08f,float(facing),false);
        }
    }
}
} // namespace voxy::game::adventure
