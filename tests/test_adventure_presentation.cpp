#include "game/adventure/adventure_presentation.hpp"
#include "game/adventure/spatial_queries.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>

namespace voxy::game::adventure {
TEST(AdventurePresentation, PriorHomeStillNeedsAcceptanceAndCompletedReceiptWins) {
    AdventureState state;FirstHomeReadiness readiness{FirstHomeStep::Ready,7,"Ready"};
    EXPECT_EQ(homeDialogue(1,state,readiness).choice,HomeDialogueChoice::Accept);
    state.firstHome.phase=QuestPhase::Active;
    EXPECT_EQ(homeDialogue(1,state,readiness).choice,HomeDialogueChoice::Complete);
    readiness.nextStep=FirstHomeStep::MakeBedUsable;readiness.message="The bed needs shelter.";
    EXPECT_EQ(homeDialogue(1,state,readiness).choice,HomeDialogueChoice::Close);
    EXPECT_EQ(homeDialogue(1,state,readiness).text,readiness.message);
    state.firstHome={QuestPhase::Completed,10};
    EXPECT_EQ(homeDialogue(1,state,readiness).choice,HomeDialogueChoice::Close);
    readiness.nextStep=FirstHomeStep::Ready;
    EXPECT_EQ(homeDialogue(1,state,readiness).choice,HomeDialogueChoice::Close);
    EXPECT_EQ(homeDialogue(2,state,readiness).choice,HomeDialogueChoice::Close);
}
TEST(AdventurePresentation, CompassRequiresEquipmentAndUsesActualBearing) {
    AdventureState state;AdventureSpatialQueries queries;
    state.backpack[7]={ItemKind::TrailCompass,1};
    auto readout=compassReadout(state,queries,false);
    EXPECT_FALSE(readout.equipped);EXPECT_FALSE(readout.available);ASSERT_TRUE(readout.backpackSlot);EXPECT_EQ(*readout.backpackSlot,7);
    state.backpack[7]={};state.equippedUtility={ItemKind::TrailCompass,1};
    const auto relay=installedWorld().landmark;
    state.player={relay.x,-140,relay.y+80,0};
    readout=compassReadout(state,queries,false);
    EXPECT_TRUE(readout.available);EXPECT_FALSE(readout.backpackSlot);EXPECT_EQ(readout.direction,"N");
    EXPECT_DOUBLE_EQ(readout.bearing,0);EXPECT_DOUBLE_EQ(readout.distance,80);
    state.player.x=relay.x-30;state.player.z=relay.y;
    readout=compassReadout(state,queries,false);
    EXPECT_DOUBLE_EQ(readout.bearing,90);EXPECT_DOUBLE_EQ(readout.distance,30);EXPECT_EQ(readout.direction,"E");
    readout=compassReadout(state,queries,true);
    EXPECT_TRUE(readout.equipped);EXPECT_FALSE(readout.available);EXPECT_FALSE(readout.reason.empty());
    EXPECT_TRUE(compassReadout(state,queries,false).available);
}
TEST(AdventurePresentation, QuestGuidanceFollowsCraftStorageAndEquipment) {
    AdventureState state;state.firstHome={QuestPhase::Completed,10};FirstHomeReadiness readiness;
    EXPECT_NE(homeObjective(state,readiness).find("Craft"),std::string::npos);
    state.components.emplace_back();state.components[0].slots[0]={ItemKind::TrailCompass,1};
    EXPECT_NE(homeObjective(state,readiness).find("storage"),std::string::npos);
    state.components[0].slots[0]={};state.backpack[0]={ItemKind::TrailCompass,1};
    EXPECT_NE(homeObjective(state,readiness).find("Equip"),std::string::npos);
    state.backpack[0]={};state.equippedUtility={ItemKind::TrailCompass,1};
    EXPECT_NE(homeObjective(state,readiness).find("beacon"),std::string::npos);
}
TEST(AdventurePresentation, GuideControlHintsFollowBindingsDeviceAndMouseLookPreference) {
    AdventurePreferences preferences;
    const auto standard=adventureGuideCard(AdventureGuideTopic::Combat,preferences,false);
    preferences.combat={{{67,2,7},{90,0,12}}};
    std::string error;ASSERT_TRUE(validateAdventurePreferences(preferences,error))<<error;
    const auto custom=adventureGuideCard(AdventureGuideTopic::Combat,preferences,false);
    EXPECT_NE(custom.text,standard.text);
    EXPECT_NE(custom.text.find("C / Middle mouse"),std::string::npos);
    EXPECT_NE(custom.text.find("Z / Left mouse"),std::string::npos);
    const auto pad=adventureGuideCard(AdventureGuideTopic::Combat,preferences,true);
    EXPECT_NE(pad.text.find(combatBindingLabel(preferences,CombatAction::Attack,true)),std::string::npos);
    EXPECT_NE(pad.text.find(combatBindingLabel(preferences,CombatAction::Dodge,true)),std::string::npos);
    EXPECT_EQ(pad.text.find("mouse"),std::string::npos);
    EXPECT_NE(adventureGuideCard(AdventureGuideTopic::Movement,preferences,false).text.find("Hold right mouse"),std::string::npos);
    preferences.orbitToggle=true;
    const auto toggle=adventureGuideCard(AdventureGuideTopic::Movement,preferences,false);
    EXPECT_NE(toggle.text.find("start or stop"),std::string::npos);EXPECT_EQ(toggle.text.find("Hold"),std::string::npos);
    const auto movement=adventureGuideCard(AdventureGuideTopic::Movement,preferences,true);
    EXPECT_NE(movement.text.find("Left stick"),std::string::npos);EXPECT_NE(movement.text.find("A / Cross"),std::string::npos);
    EXPECT_EQ(movement.text.find("WASD"),std::string::npos);
}
TEST(AdventurePresentation, GuideExplainsBuildRefusalManualSavingAndCurrentQuestInstructions) {
    const AdventurePreferences preferences;
    for(bool pad:{false,true}) {
        const auto build=adventureGuideCard(AdventureGuideTopic::Building,preferences,pad);
        EXPECT_NE(build.text.find("cost"),std::string::npos);EXPECT_NE(build.text.find("blocked"),std::string::npos);
        EXPECT_NE(build.text.find(pad?"View":"B opens"),std::string::npos);
        EXPECT_NE(build.text.find(pad?"X / Square":"R rotates"),std::string::npos);
    }
    const auto save=adventureGuideCard(AdventureGuideTopic::Saving,preferences,false);
    EXPECT_NE(save.text.find("Save adventure"),std::string::npos);EXPECT_NE(save.text.find("confirmation"),std::string::npos);
    EXPECT_NE(save.text.find("fails"),std::string::npos);EXPECT_NE(save.text.find("separately"),std::string::npos);
    const auto quests=adventureGuideCard(AdventureGuideTopic::Quests,preferences,false);
    EXPECT_NE(quests.text.find("accept"),std::string::npos);EXPECT_NE(quests.text.find("already built"),std::string::npos);
    EXPECT_NE(quests.text.find("current objective"),std::string::npos);
    const auto home=adventureGuideCard(AdventureGuideTopic::Home,preferences,false);
    for(const char* role:{"sheltered bed","chest","workbench","reachable"})EXPECT_NE(home.text.find(role),std::string::npos);
}
TEST(AdventurePresentation, GuideCardsRemainBoundedAtCustomBindingsAndRejectUnknownTopics) {
    AdventurePreferences preferences;preferences.combat={{{90,2,15},{67,0,12}}};
    preferences.orbitToggle=true;preferences.textScale=1.5;const auto preferencesBefore=preferences;
    for(uint8_t i=0;i<static_cast<uint8_t>(AdventureGuideTopic::Count);++i)for(bool pad:{false,true}) {
        const auto card=adventureGuideCard(static_cast<AdventureGuideTopic>(i),preferences,pad);
        EXPECT_FALSE(card.title.empty());EXPECT_FALSE(card.text.empty());EXPECT_LE(card.text.size(),180u)<<card.text;
    }
    EXPECT_EQ(preferences,preferencesBefore);
    for(auto topic:{AdventureGuideTopic::Count,static_cast<AdventureGuideTopic>(255)}) {
        const auto unavailable=adventureGuideCard(topic,preferences,false);
        EXPECT_NE(unavailable.title.find("unavailable"),std::string::npos);EXPECT_LE(unavailable.text.size(),180u);
    }
}
}
