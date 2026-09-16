#include "game/adventure/adventure_save.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace voxy::game::adventure {
namespace {
// Component authority fixtures only. Actual recipe support/contact and NPC
// reach use the separate world tests and shipping candidate validator.
const CandidateValidator admitted=[](const AdventureState&,const AdventureState&,std::string&){return true;};
const CandidateValidator blocked=[](const AdventureState&,const AdventureState&,std::string& error){error="Moss or this construction site is unreachable.";return false;};
void checksum(std::vector<std::byte>& bytes) {
    const auto digest=core::sha256(std::span<const std::byte>(bytes).first(bytes.size()-32));
    std::copy(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32);
}
// Remove only schema6's per-component bool when reconstructing a frozen old
// payload. Existing field order, byte values and all old archive tails stay exact.
void stripSchema6DoorFields(std::vector<std::byte>& bytes,const AdventureState& state) {
    size_t offset=252;
    for(const auto& structure:state.structures)offset+=38+26*structure.parts.size();
    offset+=2;
    for(size_t i=state.components.size();i>0;--i) {
        const auto field=offset+(i-1)*138+137;ASSERT_LT(field,bytes.size());ASSERT_EQ(bytes[field],std::byte{0});
        bytes.erase(bytes.begin()+static_cast<std::ptrdiff_t>(field));
    }
}

class AdventureSideQuestTest:public testing::Test {
protected:
    construction::WorldNamespace world{{'s','u','r','v','e','y','-','a','u','t','h','-','0','0','0','1'}};
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    void SetUp() override {
        content.identity.bytes[0]=std::byte{99};content.town={0,12,0,0};
        content.compatibilityIdentities=installedAdventureCompatibilityIdentities();
        content.encounters={EncounterContent{1,1,{4,12,0,0},60,{ItemKind::Scrap,8},false},
            EncounterContent{2,1,{8,12,0,0},100,{ItemKind::RelayCore,1},true}};
        content.enableTrailProgress=true;
        content.discoveries={DiscoveryContent{1,{30,12,-923,0},{ItemKind::Scrap,4}},DiscoveryContent{2,{22,12,-1031,0},{ItemKind::Stone,12}}};
        session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    }
    CommandStamp next() const{return {session->state().revision,session->state().lastRequestSequence+1,1};}
    void publish(std::optional<AdventureSession::PreparedChange> prepared) {
        ASSERT_TRUE(prepared)<<error;ASSERT_TRUE(session->commit(std::move(*prepared),error))<<error;
    }
    void reload(const AdventureState& state) {
        auto replacement=AdventureSession::restore(state,content,error);ASSERT_TRUE(replacement)<<error;session=std::move(replacement);
    }
    void home() {
        const std::array<PlacePart,4> parts{{{0,PieceKind::Foundation,{0,600,0},0,0},
            {0,PieceKind::Bed,{-50,616,0},0,0},{0,PieceKind::Chest,{50,616,0},0,0},{0,PieceKind::Workbench,{0,616,50},0,0}}};
        publish(session->prepareBlueprint(next(),parts,admitted,error));
        uint64_t bed=0;for(const auto& component:session->state().components)if(component.kind==FurnitureKind::Bed)bed=component.id;
        publish(session->prepareUseBed(next(),bed,content.town,admitted,error));
        publish(session->prepareAcceptHomeQuest(next(),1,admitted,error));publish(session->prepareCompleteHomeQuest(next(),1,admitted,error));
    }
    void discoveries() {
        publish(session->prepareDiscover(next(),1,admitted,error));publish(session->prepareDiscover(next(),2,admitted,error));
    }
    void complete() {
        publish(session->prepareAcceptTrailQuest(next(),5,1,admitted,error));
        publish(session->prepareCompleteTrailQuest(next(),5,1,admitted,error));
    }
    std::vector<std::byte> encode(const AdventureState& state) {
        std::vector<std::byte> bytes;EXPECT_TRUE(AdventureSaveCodec::encode(state,content,bytes,error))<<error;return bytes;
    }
};
TEST_F(AdventureSideQuestTest, EarlierArrivalsCountButAcceptanceAndFirstHomeRemainRequired) {
    discoveries();const auto found=session->state();EXPECT_TRUE(trailQuestReady(found,5));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),5,1,admitted,error));EXPECT_EQ(session->state(),found);
    home();const auto before=session->state();EXPECT_FALSE(wideStoneStepRecipeUnlocked(before));
    EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),5,1,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),5,2,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),5,1,blocked,error));EXPECT_EQ(session->state(),before);
    // A recipe receipt needs no bag slot and grants no stock. Both material
    // reward claims remain untouched, even with a completely full backpack.
    auto packed=before;packed.backpack.fill({ItemKind::Stone,999});reload(packed);complete();
    const auto done=session->state();EXPECT_TRUE(wideStoneStepRecipeUnlocked(done));EXPECT_EQ(done.backpack,packed.backpack);
    for(const auto& discovery:done.trail.discoveries)EXPECT_EQ(discovery.rewardClaimRevision,0u);
    EXPECT_EQ(done.trail.quests[0],FirstHomeProgress{});EXPECT_EQ(done.trail.relayActivationRevision,0u);
    EXPECT_GT(done.trail.quests[3].rewardRevision,done.firstHome.rewardRevision);
    EXPECT_GT(done.trail.quests[3].rewardRevision,done.trail.discoveries[1].discoveredRevision);
}
TEST_F(AdventureSideQuestTest, OneArrivalOrFailedStaleTurnInCannotUnlockTheRecipe) {
    home();publish(session->prepareAcceptTrailQuest(next(),5,1,admitted,error));
    publish(session->prepareDiscover(next(),1,admitted,error));const auto partial=session->state();
    EXPECT_FALSE(trailQuestReady(partial,5));EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),5,1,admitted,error));EXPECT_EQ(session->state(),partial);
    publish(session->prepareDiscover(next(),2,admitted,error));const auto ready=session->state();
    EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),5,1,{},error));
    EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),5,1,blocked,error));
    EXPECT_FALSE(session->prepareCompleteTrailQuest({ready.revision,ready.lastRequestSequence+1,2},5,1,admitted,error));EXPECT_EQ(session->state(),ready);
    auto stale=session->prepareCompleteTrailQuest(next(),5,1,admitted,error);ASSERT_TRUE(stale);
    ASSERT_TRUE(session->updatePlayer({.125,12,0,0},100,error));const auto moved=session->state();
    EXPECT_FALSE(session->commit(std::move(*stale),error));EXPECT_EQ(session->state(),moved);EXPECT_FALSE(wideStoneStepRecipeUnlocked(moved));
    const auto stamp=next();auto first=session->prepareCompleteTrailQuest(stamp,5,1,admitted,error);
    auto duplicate=session->prepareCompleteTrailQuest(stamp,5,1,admitted,error);ASSERT_TRUE(first);ASSERT_TRUE(duplicate);
    publish(std::move(first));const auto unlocked=session->state();
    EXPECT_FALSE(session->commit(std::move(*duplicate),error));EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),5,1,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),5,1,admitted,error));EXPECT_EQ(session->state(),unlocked);
}
TEST_F(AdventureSideQuestTest, PaidCanonicalRecipeHasNoLockedOrPartialConstructionBypass) {
    const auto initial=session->state();
    EXPECT_FALSE(session->prepareBuildRecipe(next(),BlueprintKind::WideStoneStep,{0,600,0},0,admitted,error));
    EXPECT_FALSE(session->prepareBuildRecipe(next(),BlueprintKind::None,{0,600,0},0,admitted,error));EXPECT_EQ(session->state(),initial);
    home();discoveries();complete();auto low=session->state();
    for(auto& stack:low.backpack)if(stack.kind==ItemKind::Stone)stack.quantity=5;
    reload(low);
    EXPECT_FALSE(session->prepareBuildRecipe(next(),BlueprintKind::WideStoneStep,{200,600,0},0,admitted,error));EXPECT_EQ(session->state(),low);
    auto enough=low;for(auto& stack:enough.backpack)if(stack.kind==ItemKind::Stone)stack.quantity=6;reload(enough);
    EXPECT_FALSE(session->prepareBuildRecipe(next(),BlueprintKind::WideStoneStep,{200,600,0},0,blocked,error));
    EXPECT_FALSE(session->prepareBuildRecipe(next(),BlueprintKind::WideStoneStep,{200,600,0},4,admitted,error));EXPECT_EQ(session->state(),enough);
    auto prepared=session->prepareBuildRecipe(next(),BlueprintKind::WideStoneStep,{200,600,0},1,admitted,error);ASSERT_TRUE(prepared)<<error;
    EXPECT_EQ(session->state(),enough);EXPECT_EQ(prepared->state().revision,enough.revision+1);
    publish(std::move(prepared));const auto built=session->state();EXPECT_EQ(itemCount(built.backpack,ItemKind::Stone),0u);
    EXPECT_EQ(built.structures.size(),enough.structures.size()+1);EXPECT_EQ(built.structures.back().parts.size(),3u);
    EXPECT_EQ(built.trail.quests[3],enough.trail.quests[3]);
    publish(session->prepareRemoveStructure(next(),built.structures.back().id,admitted,error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone),6u);EXPECT_TRUE(wideStoneStepRecipeUnlocked(session->state()));
}
TEST_F(AdventureSideQuestTest, ReceiptPersistsAfterClaimingSuppliesRemovingHomeAndReloading) {
    home();discoveries();complete();const auto receipt=session->state().trail.quests[3];
    publish(session->prepareClaimDiscovery(next(),1,admitted,error));
    publish(session->prepareRemoveStructure(next(),session->state().structures.front().id,admitted,error));
    const auto accepted=session->state();EXPECT_TRUE(accepted.structures.empty());EXPECT_TRUE(wideStoneStepRecipeUnlocked(accepted));
    EXPECT_GT(accepted.trail.discoveries[0].rewardClaimRevision,receipt.rewardRevision);
    AdventureState restored;AdventureSaveLoadMetadata metadata;const auto bytes=encode(accepted);
    ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,restored,error,&metadata));EXPECT_EQ(restored,accepted);
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));reload(restored);EXPECT_EQ(session->state().trail.quests[3],receipt);
    auto forged=restored;forged.trail.discoveries[1]={};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=restored;forged.trail.quests[3].rewardRevision=forged.trail.discoveries[1].discoveredRevision;EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=restored;forged.firstHome={};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=restored;forged.trail.quests[3]={QuestPhase::Completed,restored.revision+1};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
}
TEST_F(AdventureSideQuestTest, FrozenSchemaFourKeepsCompletedMainChainAndEveryOldByteWithoutGrantingSurvey) {
    const auto file=std::filesystem::path(__FILE__).parent_path()/"fixtures/adventure/schema4-component-r01.bin";
    std::ifstream stream(file,std::ios::binary);ASSERT_TRUE(stream)<<file;
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),{});const auto original=std::as_bytes(std::span(raw));
    ASSERT_EQ(original.size(),1542u);EXPECT_EQ(core::sha256Hex(core::sha256(original)),"8493c01ec604f1bdd46f3fbcfd2328cc54560ba773b0816980b9320cf4b2da85");
    for(size_t i=0;i<16;++i)world.bytes[i]=std::to_integer<uint8_t>(original[12+i]);
    content.town={-63,-145.73502075195313,-895,0};
    content.encounters[0].spawn={-58,-146.3750048828125,-945,0};content.encounters[1].spawn={-55,-147.65500366210938,-967,0};
    AdventureState migrated;AdventureSaveLoadMetadata metadata;
    ASSERT_TRUE(AdventureSaveCodec::decode(original,world,content,migrated,error,&metadata))<<error;
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{4,true}));EXPECT_EQ(migrated.revision,22u);EXPECT_EQ(migrated.health,47);
    for(size_t i=0;i<3;++i)EXPECT_EQ(migrated.trail.quests[i].phase,QuestPhase::Completed);
    EXPECT_EQ(migrated.trail.relayActivationRevision,22u);EXPECT_EQ(migrated.trail.quests[3],FirstHomeProgress{});
    EXPECT_TRUE(trailQuestReady(migrated,5));EXPECT_FALSE(wideStoneStepRecipeUnlocked(migrated));
    EXPECT_EQ(migrated.combat.player.attackImpactTick,13u);EXPECT_EQ(migrated.combat.encounters[0].checkpoint.phase,EnemyPhase::Windup);
    auto current=encode(migrated);EXPECT_EQ(current.size(),original.size()+migrated.components.size());AdventureState restored;
    ASSERT_TRUE(AdventureSaveCodec::decode(current,world,content,restored,error,&metadata));EXPECT_EQ(restored,migrated);EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));
    stripSchema6DoorFields(current,migrated);
    current[8]=std::byte{4};std::copy(content.compatibilityIdentities[3]->bytes.begin(),content.compatibilityIdentities[3]->bytes.end(),current.begin()+28);checksum(current);
    EXPECT_TRUE(std::equal(current.begin(),current.end(),original.begin(),original.end()));
    // Active5 is valid under the new rules, but was impossible in schema4.
    auto forged=current;forged[forged.size()-49]=std::byte{1};checksum(forged);
    const auto before=restored;metadata={99,true};EXPECT_FALSE(AdventureSaveCodec::decode(forged,world,content,restored,error,&metadata));
    EXPECT_EQ(restored,before);EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{99,true}));
    auto missing=content;missing.compatibilityIdentities[3].reset();EXPECT_FALSE(AdventureSaveCodec::decode(original,world,missing,restored,error));EXPECT_EQ(restored,before);
    reload(migrated);EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),5,1,admitted,error));complete();
    EXPECT_TRUE(wideStoneStepRecipeUnlocked(session->state()));EXPECT_EQ(session->state().combat,migrated.combat);
    EXPECT_EQ(session->state().trail.relayActivationRevision,migrated.trail.relayActivationRevision);
}
} // namespace
} // namespace voxy::game::adventure
