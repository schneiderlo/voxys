#include "game/adventure/adventure_presentation.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include <cmath>
#include <numbers>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {
AdventureGuideCard adventureGuideCard(AdventureGuideTopic topic,const AdventurePreferences& preferences,bool gamepad) {
    switch(topic) {
    case AdventureGuideTopic::Movement:
        if(gamepad)return {"Move and look","Left stick to move; A / Cross to jump.\nRight stick to look; R3 to recenter. Open Settings for comfort and controls."};
        return {"Move and look",std::string("WASD to move; Space to jump.\n")
            +(preferences.orbitToggle?"Click right mouse to start or stop looking.":"Hold right mouse to look around.")
            +" G recenters the camera. Open Settings for comfort and controls."};
    case AdventureGuideTopic::Building:
        return {"Build something useful",gamepad
            ?"View opens Build. Choose a piece, aim, then A / Cross to place. X / Square rotates. Check the cost; if placement is blocked, read its reason."
            :"B opens Build. Choose a piece, aim, then left click to place. R rotates. Check the cost; if placement is blocked, read its reason."};
    case AdventureGuideTopic::Home:
        return {"Make a home","Use a sheltered bed to register home. A chest stores supplies; a workbench crafts tools. Keep all three reachable, and shelter your bed with a roof."};
    case AdventureGuideTopic::Quests:
        return {"Follow a quest","Meet Moss and accept a quest. A home you already built counts. Your journal shows the current objective; follow its instructions to finish the quest."};
    case AdventureGuideTopic::Combat:
        return {"Face the trail","Equip a Trail staff. Attack: "+combatBindingLabel(preferences,CombatAction::Attack,gamepad)
            +"; Dodge: "+combatBindingLabel(preferences,CombatAction::Dodge,gamepad)
            +".\nWatch enemy windups; retreat when you need room."};
    case AdventureGuideTopic::Saving:
        return {"Keep your adventure","Open Menu, choose Save adventure, and wait for confirmation before leaving. If saving fails, retry. Settings are stored separately from each world."};
    case AdventureGuideTopic::Count:break;
    }
    return {"Guide unavailable","Choose a topic from the quick guide."};
}
glm::dmat4 trailStaffModel(const glm::dmat4& hand) noexcept {
    // Beam has source bounds X ±1, Y 0.. .32, Z ±.16. Centre its
    // section in the grip, then align its long X axis with hand-local +Z.
    return hand*glm::translate(glm::dmat4(1),glm::dvec3(0,-.0224,.326))
        *glm::rotate(glm::dmat4(1),-std::numbers::pi/2,glm::dvec3(0,1,0))
        *glm::scale(glm::dmat4(1),glm::dvec3(.4,.14,.14));
}
namespace {
bool hasCompass(const AdventureState& state) {
    if(state.equippedUtility.kind==ItemKind::TrailCompass||itemCount(state.backpack,ItemKind::TrailCompass)>0)return true;
    for(const auto& component:state.components)if(itemCount(component.slots,ItemKind::TrailCompass)>0)return true;
    return false;
}
}
HomeDialogue homeDialogue(uint8_t npc,const AdventureState& state,const FirstHomeReadiness& readiness) {
    const bool complete=state.firstHome.phase==QuestPhase::Completed;
    if(npc==1) {
        if(complete)return {"You have a place to return to. Rivet can help you make a compass for the trail.","See you soon",HomeDialogueChoice::Close};
        if(state.firstHome.phase==QuestPhase::NotAccepted)
            return {"The old beacon has gone dark. First, make somewhere safe to return to: a sheltered bed, a chest and a workbench. A home you already built counts.","Accept: A Place to Return",HomeDialogueChoice::Accept};
        if(readiness.ready())return {"Your home is ready. This compass pattern will help you find the beacon, and your way back.","Complete quest: learn compass",HomeDialogueChoice::Complete};
        return {readiness.message,"I'll work on my home",HomeDialogueChoice::Close};
    }
    if(npc==2) {
        if(complete)return {"Your compass pattern is ready. At your workbench, use 2 wood and 4 scrap. Equip the compass to follow a bearing or find your home.","Got it",HomeDialogueChoice::Close};
        return {"Moss has a compass pattern for anyone who sets up a home. Your workbench can already make a field hammer for gathering.","I'll talk to Moss",HomeDialogueChoice::Close};
    }
    if(npc==3)return {"The old beacon stands north of the square. Follow the meadow and look for its amber tower. Bring a compass when you are ready to scout.","I'll look for the beacon",HomeDialogueChoice::Close};
    return {"This resident is unavailable.","Close",HomeDialogueChoice::Close};
}
CompassReadout compassReadout(const AdventureState& state,const AdventureSpatialQueries& queries,bool home) {
    CompassReadout out;out.home=home;out.label=home?"Home":"Old beacon";
    out.equipped=state.equippedUtility.kind==ItemKind::TrailCompass&&state.equippedUtility.quantity==1;
    for(uint8_t i=0;i<state.backpack.size();++i)if(state.backpack[i].kind==ItemKind::TrailCompass) {out.backpackSlot=i;break;}
    if(!out.equipped){out.reason="Equip a Trail Compass to see a bearing.";return out;}
    glm::dvec2 target=installedWorld().landmark;
    if(home) {
        PlayerPose recovery;
        if(!state.registeredBed||!usableBed(state,state.registeredBed,queries,recovery,out.reason)) {
            out.reason="No usable home registered. Select the beacon or use a sheltered bed.";return out;
        }
        target={recovery.x,recovery.z};
    }
    const auto delta=target-glm::dvec2(state.player.x,state.player.z);
    out.distance=glm::length(delta);
    out.bearing=std::fmod(std::atan2(delta.x,-delta.y)*180/std::numbers::pi+360,360);
    constexpr std::array directions{"N","NE","E","SE","S","SW","W","NW"};
    out.direction=directions[size_t(std::floor((out.bearing+22.5)/45))%8];
    out.available=true;return out;
}
std::string homeObjective(const AdventureState& state,const FirstHomeReadiness& readiness) {
    if(state.firstHome.phase==QuestPhase::NotAccepted)return "Meet Moss in the meadow square.";
    if(state.firstHome.phase==QuestPhase::Active)
        return readiness.ready()?"Return to Moss. Your home is ready.":readiness.message;
    if(!hasCompass(state))return "Craft a Trail Compass at your workbench: 2 wood + 4 scrap.";
    if(state.equippedUtility.kind!=ItemKind::TrailCompass&&itemCount(state.backpack,ItemKind::TrailCompass)==0)
        return "Take your compass from storage, then equip it for the trail.";
    if(state.equippedUtility.kind!=ItemKind::TrailCompass)return "Equip your Trail Compass, then explore toward the beacon.";
    return "Follow your compass to the old beacon. Your home is here to return to.";
}
}
