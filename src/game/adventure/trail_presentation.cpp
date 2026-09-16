#include "game/adventure/trail_presentation.hpp"
#include "game/adventure/adventure_presentation.hpp"
#include "game/adventure/building_blueprints.hpp"
#include "game/adventure/construction_policy.hpp"
#include <cmath>
#include <numbers>

namespace voxy::game::adventure {
namespace {
bool equipped(const AdventureState& state,ItemKind kind) {
    return (kind==ItemKind::TrailCompass?state.equippedUtility:state.equippedTool)==ItemStack{kind,1};
}
bool stored(const AdventureState& state,ItemKind kind) {
    for(const auto& component:state.components)if(itemCount(component.slots,kind))return true;
    return false;
}
std::string prepareObjective(const AdventureState& state) {
    if(!equipped(state,ItemKind::TrailCompass)) {
        if(itemCount(state.backpack,ItemKind::TrailCompass))return "Equip your Trail Compass from the bag.";
        if(stored(state,ItemKind::TrailCompass))return "Take your compass from storage, then equip it.";
        return "Craft a Trail Compass at your workbench: 2 wood + 4 scrap.";
    }
    if(!equipped(state,ItemKind::TrailStaff)) {
        if(itemCount(state.backpack,ItemKind::TrailStaff))return "Equip your Trail staff from the bag.";
        if(stored(state,ItemKind::TrailStaff))return "Take your staff from storage, then equip it.";
        return "Craft a Trail staff at your workbench: 4 wood + 4 scrap.";
    }
    return "Return to Rivet with your compass and staff equipped.";
}
std::string restoreObjective(const AdventureState& state,const FirstHomeReadiness& home) {
    if(!itemCount(state.backpack,ItemKind::RelayCore)) {
        if(stored(state,ItemKind::RelayCore))return "Take the relay core from storage. Bring it to the old beacon.";
        return "Bring the relay core in your bag.";
    }
    if(!home.ready())return home.message.empty()
        ?"Make a field home within 25 m of the old beacon: sheltered bed, chest and workbench."
        :"Field home: "+home.message;
    return "Use the old beacon to restore the relay.";
}
std::optional<TrailSiteId> siteId(CompassTarget target) {
    switch(target) {
    case CompassTarget::Relay:return TrailSiteId::Relay;
    case CompassTarget::SignalTerrace:return TrailSiteId::SignalTerrace;
    case CompassTarget::SurveyOverlook:return TrailSiteId::SurveyOverlook;
    case CompassTarget::WatchArch:return TrailSiteId::WatchArch;
    case CompassTarget::Home:return {};
    }
    return {};
}
std::string materialReward(ItemStack reward) {
    std::string_view name;
    switch(reward.kind) {
    case ItemKind::Wood:name="wood";break;
    case ItemKind::Stone:name="stone";break;
    case ItemKind::Scrap:name="scrap";break;
    default:return {};
    }
    if(!validStack(reward)||!reward.quantity)return {};
    return std::to_string(reward.quantity)+" "+std::string(name);
}
std::string surveyRecipeGuide(const AdventureState& state) {
    const auto* recipe=buildingBlueprintDefinition(BlueprintKind::WideStoneStep);
    if(!recipe||!wideStoneStepRecipeUnlocked(state))return "Survey complete. Your visits are recorded.";
    std::string cost;
    for(const auto& material:std::array<ItemStack,3>{{{ItemKind::Wood,recipe->cost.wood},
        {ItemKind::Stone,recipe->cost.stone},{ItemKind::Scrap,recipe->cost.scrap}}}) {
        if(!material.quantity)continue;
        if(!cost.empty())cost+=" + ";
        cost+=materialReward(material);
    }
    return "Build: "+std::string(recipe->name)+". Costs "+cost+". Adjust its height to fit the ground.";
}
TrailQuestReadout surveyReadout(const AdventureState& state,const TrailQuestDefinition& definition) {
    const auto& progress=state.trail.quests[3];TrailQuestReadout out;
    out.questId=definition.id;out.title=definition.title;out.phase=progress.phase;
    out.unlocked=trailQuestPrerequisite(state,definition.id);
    if(progress.phase==QuestPhase::Completed) {out.status="Completed";out.objective=surveyRecipeGuide(state);return out;}
    if(!out.unlocked) {out.status="Locked";out.objective="Help Moss finish A Place to Return.";return out;}
    if(progress.phase==QuestPhase::NotAccepted) {
        out.status="Optional";out.objective="Talk to Moss about surveying the trail.";return out;
    }
    out.ready=trailQuestReady(state,definition.id);out.status=out.ready?"Ready":"Active";
    if(out.ready)out.objective="Return to Moss. Both places are surveyed.";
    else if(!state.trail.discoveries[0].discoveredRevision) {
        out.objective="Visit Signal Terrace, east of the village.";out.waypoint=CompassTarget::SignalTerrace;
    } else {
        out.objective="Visit Survey Overlook, beyond the old beacon.";out.waypoint=CompassTarget::SurveyOverlook;
    }
    return out;
}
}

std::optional<TrailQuestReadout> trailQuestReadout(uint8_t id,const AdventureState& state,
    const FirstHomeReadiness& fieldHome) {
    const auto* definition=trailQuestDefinition(id);if(!definition)return {};
    if(id==5)return surveyReadout(state,*definition);
    const auto& progress=state.trail.quests[static_cast<size_t>(id-2)];
    TrailQuestReadout out;out.questId=id;out.phase=progress.phase;out.title=definition->title;
    out.unlocked=trailQuestPrerequisite(state,id)||progress.phase!=QuestPhase::NotAccepted;
    if(progress.phase==QuestPhase::Completed) {
        out.status="Completed";
        if(id==2)out.objective="Trail equipment checked. Speak to Lumen.";
        else if(id==3)out.objective="Relay component recovered. Speak to Lumen about repairs.";
        else if(state.trail.relayActivationRevision) {
            out.objective="The beacon is alight. Follow your compass to Watch Arch.";
            out.waypoint=CompassTarget::WatchArch;
        } else out.objective="Relay restoration recorded.";
        return out;
    }
    if(!out.unlocked) {
        out.status="Locked";
        out.objective=id==2?"Help Moss finish A Place to Return."
            :id==3?"Finish Prepare for the trail with Rivet."
            :"Finish Recover relay component with Lumen.";
        return out;
    }
    if(progress.phase==QuestPhase::NotAccepted) {
        out.status="Available";out.objective=id==2?"Talk to Rivet about the trail."
            :id==3?"Talk to Lumen about the missing relay component."
            :"Talk to Lumen about restoring the relay.";
        return out;
    }
    out.ready=trailQuestReady(state,id);out.status=out.ready?"Ready":"Active";
    if(id==2)out.objective=prepareObjective(state);
    else if(id==3) {
        out.waypoint=CompassTarget::Relay;
        if(out.ready) {out.objective="Return to Lumen. You already recovered the relay core.";out.waypoint.reset();}
        else if(state.combat.encounters[1].deathRevision)out.objective="Collect the relay core from the fallen raider.";
        else out.objective="Follow the old beacon trail. Defeat its raider and collect the relay core.";
    } else {
        // Quest4 is completed at the physical relay. Never offer an NPC reward
        // merely because a suitable house and a core currently exist.
        out.objective=restoreObjective(state,fieldHome);out.waypoint=CompassTarget::Relay;
    }
    return out;
}

std::array<TrailQuestReadout,3> trailJournal(const AdventureState& state,const FirstHomeReadiness& fieldHome) {
    std::array<TrailQuestReadout,3> rows{};
    for(size_t i=0;i<rows.size();++i)if(const auto row=trailQuestReadout(static_cast<uint8_t>(i+2),state,fieldHome))rows[i]=*row;
    return rows;
}

std::optional<TrailQuestReadout> trailSideQuest(const AdventureState& state) {
    if(!trailQuestPrerequisite(state,5))return {};
    return trailQuestReadout(5,state,{});
}

std::optional<TrailDialogue> trailDialogue(uint8_t npc,const AdventureState& state,const FirstHomeReadiness& fieldHome) {
    if(npc==1) {
        const auto row=trailSideQuest(state);if(!row)return {};
        TrailDialogue out;out.questId=5;out.choiceLabel="See you soon";
        if(row->phase==QuestPhase::Completed) {out.text="Thank you for surveying the trail. "+row->objective;return out;}
        if(row->phase==QuestPhase::NotAccepted) {
            out.choice=TrailDialogueChoice::Accept;out.choiceLabel="Accept: "+row->title;
            out.text="Visit Signal Terrace and Survey Overlook. I'll teach you the Wide stone step recipe. Earlier visits count.";
        } else if(row->ready) {
            out.choice=TrailDialogueChoice::Complete;out.choiceLabel="Complete: survey notes";
            out.text="Both places are surveyed. I can teach you the Wide stone step recipe now.";
        } else {out.text=row->objective;out.choiceLabel="I'll explore";}
        return out;
    }
    if(npc!=2&&npc!=3)return {};
    TrailDialogue out;out.choiceLabel="See you soon";
    if(state.trail.relayActivationRevision) {
        out.questId=4;out.text=npc==3
            ?"The beacon is alight! Its signal marks Watch Arch on your compass. Follow the northern trail when you are ready."
            :"The beacon is back. Your compass can now guide you to Watch Arch.";
        return out;
    }
    const uint8_t id=npc==2?2:state.trail.quests[1].phase==QuestPhase::Completed?4:3;
    const auto row=trailQuestReadout(id,state,fieldHome);if(!row)return {};
    out.questId=id;
    if(row->phase==QuestPhase::Completed) {out.text=row->objective;return out;}
    if(!row->unlocked) {
        out.text=npc==2?"Moss will help you set up a home. Finish that first, then we can prepare for the trail."
            :"Speak to Rivet first. A compass and staff will help you reach the old beacon.";
        return out;
    }
    if(row->phase==QuestPhase::NotAccepted) {
        out.choice=TrailDialogueChoice::Accept;out.choiceLabel="Accept: "+row->title;
        out.text=id==2?"Before you head out, equip a Trail Compass and a Trail staff. Make them at your workbench, then show me."
            :id==3?"A raider near the old beacon has our relay core. Bring back word when you have collected it. Earlier work counts."
            :"We can light the beacon again. Bring its core and make a sheltered field home within 25 m: a bed, chest and workbench.";
        return out;
    }
    if(row->ready&&id!=4) {
        out.choice=TrailDialogueChoice::Complete;
        out.choiceLabel=id==2?"Complete: ready for the trail":"Complete: component recovered";
        out.text=id==2?"Your compass and staff are ready. Lumen needs help with the old beacon."
            :"You recovered the relay core. Keep it safe; next we will restore the beacon.";
    } else {
        out.text=row->objective;out.choiceLabel=id==4?"I'll go to the beacon":"I'll work on it";
    }
    return out;
}

std::string trailObjective(const AdventureState& state,const FirstHomeReadiness& firstHome,
    const FirstHomeReadiness& fieldHome) {
    if(state.firstHome.phase!=QuestPhase::Completed)return homeObjective(state,firstHome);
    if(state.trail.relayActivationRevision)return "The beacon is alight. Follow your compass to Watch Arch.";
    for(const auto& row:trailJournal(state,fieldHome))if(row.phase!=QuestPhase::Completed)return row.objective;
    return "Return to Lumen at the village.";
}

TrailCompassReadout trailCompassReadout(const AdventureState& state,const AdventureSpatialQueries& queries,
    const TrailSites& sites,CompassTarget target) {
    TrailCompassReadout out;out.target=target;out.equipped=equipped(state,ItemKind::TrailCompass);
    for(uint8_t i=0;i<state.backpack.size();++i)if(state.backpack[i]==ItemStack{ItemKind::TrailCompass,1}) {out.backpackSlot=i;break;}
    glm::dvec2 position{};
    if(target==CompassTarget::Home) {
        out.revealed=true;out.label="Home";
        if(!out.equipped){out.reason="Equip a Trail Compass to see a bearing.";return out;}
        PlayerPose recovery;std::string reason;
        if(!state.registeredBed||!usableBed(state,state.registeredBed,queries,recovery,reason)) {
            out.reason="Use a sheltered bed to register a usable home.";return out;
        }
        position={recovery.x,recovery.z};
    } else {
        const auto id=siteId(target);if(!id) {out.reason="Choose a marked place.";return out;}
        if(target==CompassTarget::WatchArch&&!state.trail.relayActivationRevision) {
            out.reason="Restore the old beacon to reveal the next trail.";return out;
        }
        const auto* definition=trailSiteDefinition(*id);if(!definition) {out.reason="This trail marker is unavailable.";return out;}
        out.revealed=true;out.label=definition->name;
        if(!out.equipped){out.reason="Equip a Trail Compass to see a bearing.";return out;}
        const auto* site=sites.site(*id);
        if(!site||!site->available) {out.reason="This trail marker has no clear position in this world.";return out;}
        position={site->position.x,site->position.z};
    }
    if(!std::isfinite(state.player.x)||!std::isfinite(state.player.z)||!std::isfinite(position.x)||!std::isfinite(position.y)) {
        out.reason="Your bearing is unavailable.";return out;
    }
    const auto delta=position-glm::dvec2(state.player.x,state.player.z);out.distance=glm::length(delta);
    if(!std::isfinite(out.distance)) {out.distance=0;out.reason="Your bearing is unavailable.";return out;}
    out.bearing=std::fmod(std::atan2(delta.x,-delta.y)*180/std::numbers::pi+360,360);
    constexpr std::array directions{"N","NE","E","SE","S","SW","W","NW"};
    out.direction=out.distance<.01?"Here":directions[static_cast<size_t>(std::floor((out.bearing+22.5)/45))%8];
    out.available=true;return out;
}

std::array<TrailDiscoveryReadout,2> trailDiscoveries(const AdventureState& state,
    std::span<const DiscoveryContent> content,const TrailSites& sites) {
    std::array<TrailDiscoveryReadout,2> rows{};
    for(size_t i=0;i<rows.size();++i) {
        auto& row=rows[i];row.discoveryId=static_cast<uint8_t>(i+1);
        const auto id=i==0?TrailSiteId::SignalTerrace:TrailSiteId::SurveyOverlook;
        row.waypoint=i==0?CompassTarget::SignalTerrace:CompassTarget::SurveyOverlook;
        if(const auto* definition=trailSiteDefinition(id))row.title=definition->name;
        if(const auto* site=sites.site(id))row.available=site->available;
        row.found=state.trail.discoveries[i].discoveredRevision!=0;
        row.rewardClaimed=state.trail.discoveries[i].rewardClaimRevision!=0;
        if(!row.found)row.objective=i==0?"Explore the marked terrace east of the village.":"Explore the marked overlook beyond the old beacon.";
        else if(row.rewardClaimed)row.objective="Survey supplies claimed.";
        else row.objective="Claim the survey supplies here. Make room in your bag if needed.";
        if(row.found)for(const auto& definition:content)if(definition.id==row.discoveryId) {row.reward=materialReward(definition.reward);break;}
    }
    return rows;
}
} // namespace voxy::game::adventure
