#include "game/adventure/trail_presentation.hpp"
#include "game/adventure/adventure_presentation.hpp"
#include "game/adventure/building_blueprints.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace voxy::game::adventure {
namespace {
FirstHomeReadiness readyHome() {return {FirstHomeStep::Ready,7,"Your field home is ready."};}
struct SitesFixture {
    std::vector<uint16_t> samples=std::vector<uint16_t>(256*2304,32768);
    terrain::lego::Surface surface{samples,256,2304,8.f,1.f};
    AdventureSpatialQueries queries;
    AdventureState state;
    AdventureContent content;
    TrailSites sites;
    SitesFixture() {
        state.world.bytes[0]=23;
        const double y=double(terrain::lego::supportHeight(surface,{-63,-895},.3f))+.005;
        state.player={-63,y,-895,0};content.town=state.player;
        EXPECT_TRUE(queries.bindTerrain(surface));EXPECT_TRUE(queries.publish({},1));
        sites=TrailSites::admit(state,content,queries);
    }
};
}

TEST(TrailPresentation, EquipmentPriorWorkStillRequiresAcceptanceAndCompletedReceiptWins) {
    AdventureState state;const auto home=readyHome();
    state.equippedUtility={ItemKind::TrailCompass,1};state.equippedTool={ItemKind::TrailStaff,1};
    auto dialogue=trailDialogue(2,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Close);
    state.firstHome={QuestPhase::Completed,10};
    dialogue=trailDialogue(2,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->questId,2);EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Accept);
    state.trail.quests[0].phase=QuestPhase::Active;
    dialogue=trailDialogue(2,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Complete);
    state.trail.quests[0]={QuestPhase::Completed,20};state.equippedUtility={};state.equippedTool={};
    dialogue=trailDialogue(2,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Close);
    ASSERT_TRUE(trailDialogue(1,state,home));EXPECT_EQ(trailDialogue(1,state,home)->questId,5);
    EXPECT_FALSE(trailDialogue(0,state,home));
    EXPECT_FALSE(trailDialogue(255,state,home));
}

TEST(TrailPresentation, HistoricCoreClaimCountsButOnlyPhysicalActivationCompletesRestore) {
    AdventureState state;auto home=readyHome();state.firstHome={QuestPhase::Completed,10};
    state.trail.quests[0]={QuestPhase::Completed,20};state.combat.encounters[1].lootClaimRevision=21;
    state.components.emplace_back();state.components.back().slots[0]={ItemKind::RelayCore,1};
    auto dialogue=trailDialogue(3,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->questId,3);EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Accept);
    state.trail.quests[1].phase=QuestPhase::Active;
    dialogue=trailDialogue(3,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Complete);
    EXPECT_TRUE(trailQuestReadout(3,state,home)->ready);
    state.trail.quests[1]={QuestPhase::Completed,22};
    dialogue=trailDialogue(3,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->questId,4);EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Accept);
    state.trail.quests[2].phase=QuestPhase::Active;
    EXPECT_NE(trailQuestReadout(4,state,home)->objective.find("storage"),std::string::npos);
    state.components.back().slots[0]={};state.backpack[0]={ItemKind::RelayCore,1};
    dialogue=trailDialogue(3,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Close);
    EXPECT_FALSE(trailQuestReadout(4,state,home)->ready);
    EXPECT_NE(dialogue->text.find("Use the old beacon"),std::string::npos);
    state.backpack[0]={};state.trail.quests[2]={QuestPhase::Completed,30};state.trail.relayActivationRevision=30;
    home.nextStep=FirstHomeStep::MakeBedUsable;home.message="The bed needs shelter.";
    dialogue=trailDialogue(3,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Close);
    EXPECT_NE(dialogue->text.find("Watch Arch"),std::string::npos);
    EXPECT_EQ(trailQuestReadout(4,state,home)->waypoint,CompassTarget::WatchArch);
}

TEST(TrailPresentation, EquipmentGuidanceSeparatesCraftingStorageBagAndActualEquipment) {
    AdventureState state;state.firstHome={QuestPhase::Completed,10};state.trail.quests[0].phase=QuestPhase::Active;
    const auto home=readyHome();
    EXPECT_NE(trailQuestReadout(2,state,home)->objective.find("2 wood + 4 scrap"),std::string::npos);
    state.components.emplace_back();state.components.back().slots[0]={ItemKind::TrailCompass,1};
    EXPECT_NE(trailQuestReadout(2,state,home)->objective.find("storage"),std::string::npos);
    state.components.back().slots[0]={};state.backpack[0]={ItemKind::TrailCompass,1};
    EXPECT_NE(trailQuestReadout(2,state,home)->objective.find("Equip"),std::string::npos);
    state.backpack[0]={};state.equippedUtility={ItemKind::TrailCompass,1};
    EXPECT_NE(trailQuestReadout(2,state,home)->objective.find("4 wood + 4 scrap"),std::string::npos);
    state.backpack[0]={ItemKind::TrailStaff,1};
    EXPECT_NE(trailQuestReadout(2,state,home)->objective.find("Equip"),std::string::npos);
    EXPECT_FALSE(trailQuestReadout(2,state,home)->ready);
    state.backpack[0]={};state.equippedTool={ItemKind::TrailStaff,1};
    EXPECT_TRUE(trailQuestReadout(2,state,home)->ready);
    EXPECT_NE(trailQuestReadout(2,state,home)->objective.find("Rivet"),std::string::npos);
}

TEST(TrailPresentation, JournalHasOnlyInstalledSemanticRowsAndPreservesHomeGuidanceAndState) {
    AdventureState state;const auto home=readyHome();const auto before=state;
    EXPECT_EQ(trailObjective(state,home,home),homeObjective(state,home));
    const auto journal=trailJournal(state,home);
    for(size_t i=0;i<journal.size();++i) {
        EXPECT_EQ(journal[i].questId,i+2);EXPECT_FALSE(journal[i].unlocked);
        EXPECT_FALSE(journal[i].ready);EXPECT_FALSE(journal[i].waypoint);
        EXPECT_EQ(journal[i].title,trailQuestDefinition(static_cast<uint8_t>(i+2))->title);
    }
    EXPECT_FALSE(trailQuestReadout(1,state,home));
    ASSERT_TRUE(trailQuestReadout(5,state,home));EXPECT_FALSE(trailQuestReadout(5,state,home)->unlocked);
    EXPECT_FALSE(trailSideQuest(state));
    EXPECT_FALSE(trailQuestReadout(255,state,home));EXPECT_EQ(state,before);
    state.firstHome={QuestPhase::Completed,10};
    EXPECT_NE(trailObjective(state,home,home).find("Rivet"),std::string::npos);
    state.trail.quests[0]={QuestPhase::Completed,20};
    EXPECT_NE(trailObjective(state,home,home).find("Lumen"),std::string::npos);
}

TEST(TrailPresentation, CompassUsesAdmittedSitePositionAndRequiresOwnedEquippedUtility) {
    SitesFixture fixture;const auto* terrace=fixture.sites.site(TrailSiteId::SignalTerrace);
    ASSERT_TRUE(terrace&&terrace->available);fixture.state.backpack[6]={ItemKind::TrailCompass,1};
    auto readout=trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::SignalTerrace);
    EXPECT_TRUE(readout.revealed);EXPECT_FALSE(readout.available);ASSERT_TRUE(readout.backpackSlot);EXPECT_EQ(*readout.backpackSlot,6);
    fixture.state.backpack[6]={};fixture.state.equippedUtility={ItemKind::TrailCompass,1};
    fixture.state.player.x=terrace->position.x;fixture.state.player.z=terrace->position.z+40;
    readout=trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::SignalTerrace);
    ASSERT_TRUE(readout.available)<<readout.reason;EXPECT_DOUBLE_EQ(readout.distance,40);EXPECT_DOUBLE_EQ(readout.bearing,0);EXPECT_EQ(readout.direction,"N");
    fixture.state.player.x=terrace->position.x-30;fixture.state.player.z=terrace->position.z;
    readout=trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::SignalTerrace);
    ASSERT_TRUE(readout.available);EXPECT_DOUBLE_EQ(readout.distance,30);EXPECT_DOUBLE_EQ(readout.bearing,90);EXPECT_EQ(readout.direction,"E");
    EXPECT_FALSE(trailCompassReadout(fixture.state,fixture.queries,TrailSites{},CompassTarget::SignalTerrace).available);
    fixture.state.equippedUtility.quantity=2;
    EXPECT_FALSE(trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::SignalTerrace).equipped);
}

TEST(TrailPresentation, WatchArchNeedsActivationAndDeferredOrInvalidTargetsNeverInventBearings) {
    SitesFixture fixture;fixture.state.equippedUtility={ItemKind::TrailCompass,1};
    auto readout=trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::WatchArch);
    EXPECT_FALSE(readout.revealed);EXPECT_FALSE(readout.available);EXPECT_TRUE(readout.label.empty());
    fixture.state.trail.relayActivationRevision=20;
    readout=trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::WatchArch);
    ASSERT_TRUE(fixture.sites.site(TrailSiteId::WatchArch)->available);
    EXPECT_TRUE(readout.revealed);EXPECT_TRUE(readout.available);
    EXPECT_FALSE(trailCompassReadout(fixture.state,fixture.queries,TrailSites{},CompassTarget::WatchArch).available);
    EXPECT_FALSE(trailCompassReadout(fixture.state,fixture.queries,fixture.sites,static_cast<CompassTarget>(255)).available);
    EXPECT_FALSE(trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::Home).available);
    fixture.state.player.x=std::numeric_limits<double>::quiet_NaN();
    readout=trailCompassReadout(fixture.state,fixture.queries,fixture.sites,CompassTarget::Relay);
    EXPECT_FALSE(readout.available);EXPECT_DOUBLE_EQ(readout.bearing,0);EXPECT_DOUBLE_EQ(readout.distance,0);
}

TEST(TrailPresentation, SurveyRewardsFollowRealReceiptsAndInstalledMaterialQuantities) {
    AdventureState state;const std::array<DiscoveryContent,2> content{{
        {1,{22,0,-923,0},{ItemKind::Scrap,4}},{2,{22,0,-1031,0},{ItemKind::Stone,12}}}};
    auto rows=trailDiscoveries(state,content,TrailSites{});
    for(const auto& row:rows) {EXPECT_FALSE(row.found);EXPECT_FALSE(row.rewardClaimed);EXPECT_TRUE(row.reward.empty());}
    state.trail.discoveries[0].discoveredRevision=10;const auto before=state;
    rows=trailDiscoveries(state,content,TrailSites{});
    EXPECT_EQ(rows[0].reward,"4 scrap");EXPECT_FALSE(rows[0].rewardClaimed);EXPECT_TRUE(rows[1].reward.empty());
    EXPECT_EQ(state,before);
    state.trail.discoveries[0].rewardClaimRevision=11;state.trail.discoveries[1].discoveredRevision=12;
    rows=trailDiscoveries(state,content,TrailSites{});
    EXPECT_TRUE(rows[0].rewardClaimed);EXPECT_EQ(rows[1].reward,"12 stone");
    auto changed=content;changed[1].reward.quantity=7;
    EXPECT_EQ(trailDiscoveries(state,changed,TrailSites{})[1].reward,"7 stone");
}

TEST(TrailPresentation, OptionalSurveyPreservesMossHomeDialogueAndRequiresExplicitAcceptanceDespitePriorVisits) {
    AdventureState state;state.revision=30;const auto home=readyHome();
    state.trail.discoveries[0].discoveredRevision=8;state.trail.discoveries[1].discoveredRevision=9;
    EXPECT_FALSE(trailSideQuest(state));EXPECT_FALSE(trailDialogue(1,state,home));
    state.firstHome={QuestPhase::Completed,10};const auto offeredState=state;
    auto row=trailSideQuest(state);ASSERT_TRUE(row);
    EXPECT_EQ(row->questId,5);EXPECT_EQ(row->title,trailQuestDefinition(5)->title);
    EXPECT_TRUE(row->unlocked);EXPECT_FALSE(row->ready);EXPECT_FALSE(row->waypoint);
    auto dialogue=trailDialogue(1,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->questId,5);EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Accept);
    EXPECT_EQ(state,offeredState);EXPECT_FALSE(wideStoneStepRecipeUnlocked(state));

    state.trail.quests[3].phase=QuestPhase::Active;const auto acceptedState=state;
    row=trailSideQuest(state);ASSERT_TRUE(row);EXPECT_TRUE(row->ready);EXPECT_FALSE(row->waypoint);
    dialogue=trailDialogue(1,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Complete);
    EXPECT_EQ(state.trail.discoveries[0].rewardClaimRevision,0u);
    EXPECT_EQ(state.trail.discoveries[1].rewardClaimRevision,0u);
    EXPECT_FALSE(wideStoneStepRecipeUnlocked(state));EXPECT_EQ(state,acceptedState);
}

TEST(TrailPresentation, OptionalSurveyGuidanceTargetsOnlyMissingArrivalsAndDoesNotRequireSupplyClaims) {
    AdventureState state;state.revision=30;const auto home=readyHome();state.firstHome={QuestPhase::Completed,10};
    state.trail.quests[3].phase=QuestPhase::Active;
    auto row=trailSideQuest(state);ASSERT_TRUE(row);
    EXPECT_FALSE(row->ready);EXPECT_EQ(row->waypoint,CompassTarget::SignalTerrace);
    EXPECT_NE(row->objective.find("Signal Terrace"),std::string::npos);
    EXPECT_EQ(trailDialogue(1,state,home)->choice,TrailDialogueChoice::Close);
    state.trail.discoveries[1].discoveredRevision=11;
    EXPECT_EQ(trailSideQuest(state)->waypoint,CompassTarget::SignalTerrace);
    state.trail.discoveries[1]={};state.trail.discoveries[0].discoveredRevision=12;
    row=trailSideQuest(state);ASSERT_TRUE(row);
    EXPECT_FALSE(row->ready);EXPECT_EQ(row->waypoint,CompassTarget::SurveyOverlook);
    EXPECT_NE(row->objective.find("Survey Overlook"),std::string::npos);
    state.trail.discoveries[1].discoveredRevision=13;
    const auto both=trailSideQuest(state);ASSERT_TRUE(both);EXPECT_TRUE(both->ready);
    EXPECT_FALSE(both->waypoint);EXPECT_NE(both->objective.find("Return to Moss"),std::string::npos);
    state.trail.discoveries[0].rewardClaimRevision=14;state.trail.discoveries[1].rewardClaimRevision=15;
    EXPECT_EQ(trailSideQuest(state)->objective,both->objective);
    EXPECT_EQ(trailDialogue(1,state,home)->choice,TrailDialogueChoice::Complete);
}

TEST(TrailPresentation, CompletedSurveyShowsActualPaidRecipeWithoutRepeatingTurnInOrChangingMainPriority) {
    AdventureState state;state.revision=30;const auto home=readyHome();state.firstHome={QuestPhase::Completed,10};
    state.trail.quests[0].phase=QuestPhase::Active;state.trail.quests[3].phase=QuestPhase::Active;
    state.trail.discoveries[0].discoveredRevision=11;state.trail.discoveries[1].discoveredRevision=12;
    const auto mainObjective=trailObjective(state,home,home);const auto mainJournal=trailJournal(state,home);
    EXPECT_NE(mainObjective.find("Trail Compass"),std::string::npos);
    state.trail.quests[3]={QuestPhase::Completed,13};const auto before=state;
    ASSERT_TRUE(wideStoneStepRecipeUnlocked(state));
    const auto* recipe=buildingBlueprintDefinition(BlueprintKind::WideStoneStep);ASSERT_TRUE(recipe);
    EXPECT_EQ(recipe->name,"Wide stone step");EXPECT_EQ(recipe->cost.wood,0);EXPECT_EQ(recipe->cost.stone,6);EXPECT_EQ(recipe->cost.scrap,0);
    auto row=trailSideQuest(state);ASSERT_TRUE(row);EXPECT_EQ(row->status,"Completed");
    EXPECT_FALSE(row->ready);EXPECT_FALSE(row->waypoint);
    EXPECT_NE(row->objective.find(recipe->name),std::string::npos);
    EXPECT_NE(row->objective.find(std::to_string(recipe->cost.stone)+" stone"),std::string::npos);
    EXPECT_NE(row->objective.find("Build:"),std::string::npos);
    auto dialogue=trailDialogue(1,state,home);ASSERT_TRUE(dialogue);
    EXPECT_EQ(dialogue->choice,TrailDialogueChoice::Close);EXPECT_EQ(dialogue->questId,5);
    EXPECT_NE(dialogue->text.find(row->objective),std::string::npos);
    EXPECT_EQ(trailObjective(state,home,home),mainObjective);
    const auto journal=trailJournal(state,home);ASSERT_EQ(journal.size(),3u);
    for(size_t i=0;i<journal.size();++i) {
        EXPECT_EQ(journal[i].questId,mainJournal[i].questId);EXPECT_EQ(journal[i].phase,mainJournal[i].phase);
        EXPECT_EQ(journal[i].objective,mainJournal[i].objective);
    }
    EXPECT_EQ(state,before);
    state.trail.quests[0]={QuestPhase::Completed,20};state.trail.quests[1]={QuestPhase::Completed,21};
    state.trail.quests[2]={QuestPhase::Completed,22};state.trail.relayActivationRevision=22;
    EXPECT_EQ(trailDialogue(1,state,home)->questId,5);
    EXPECT_NE(trailObjective(state,home,home).find("Watch Arch"),std::string::npos);
    EXPECT_NE(trailDialogue(3,state,home)->text.find("Watch Arch"),std::string::npos);
}
} // namespace voxy::game::adventure
