#include "game/adventure/adventure_save.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace voxy::game::adventure {
namespace {
// Isolate authoritative receipts/inventory. Actual NPC/site/field-home admission
// belongs to the separate world tests and shipping runtime callback.
const CandidateValidator admitted=[](const AdventureState&,const AdventureState&,std::string&){return true;};
const CandidateValidator blocked=[](const AdventureState&,const AdventureState&,std::string& error){error="The site is unreachable or the field home is unusable.";return false;};
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

class AdventureTrailSessionTest:public testing::Test {
protected:
    construction::WorldNamespace world{{'t','r','a','i','l','-','a','u','t','h','-','0','0','0','0','1'}};
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    void SetUp() override {
        content.identity.bytes[0]=std::byte{91};content.town={0,12,0,0};
        content.compatibilityIdentities=installedAdventureCompatibilityIdentities();
        content.encounters={EncounterContent{1,1,{4,12,0,0},60,{ItemKind::Scrap,8},false},
            EncounterContent{2,1,{8,12,0,0},100,{ItemKind::RelayCore,1},true}};
        content.enableTrailProgress=true;
        content.discoveries={DiscoveryContent{1,{22,12,-923,0},{ItemKind::Scrap,4}},DiscoveryContent{2,{22,12,-1031,0},{ItemKind::Stone,12}}};
        session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    }
    CommandStamp next() const{return {session->state().revision,session->state().lastRequestSequence+1,1};}
    void publish(std::optional<AdventureSession::PreparedChange> prepared) {
        ASSERT_TRUE(prepared)<<error;ASSERT_TRUE(session->commit(std::move(*prepared),error))<<error;
    }
    void reload(const AdventureState& state) {
        auto replacement=AdventureSession::restore(state,content,error);ASSERT_TRUE(replacement)<<error;session=std::move(replacement);
    }
    uint64_t furniture(FurnitureKind kind) const {
        for(const auto& component:session->state().components)if(component.kind==kind)return component.id;
        return 0;
    }
    uint8_t slot(ItemKind kind) const {
        for(size_t i=0;i<session->state().backpack.size();++i)if(session->state().backpack[i].kind==kind)return static_cast<uint8_t>(i);
        return 255;
    }
    void home() {
        const std::array<PlacePart,4> parts{{{0,PieceKind::Foundation,{0,600,0},0,0},
            {0,PieceKind::Bed,{-50,616,0},0,0},{0,PieceKind::Chest,{50,616,0},0,0},{0,PieceKind::Workbench,{0,616,50},0,0}}};
        publish(session->prepareBlueprint(next(),parts,admitted,error));
        publish(session->prepareUseBed(next(),furniture(FurnitureKind::Bed),content.town,admitted,error));
        publish(session->prepareAcceptHomeQuest(next(),1,admitted,error));
        publish(session->prepareCompleteHomeQuest(next(),1,admitted,error));
    }
    void gear() {
        const auto bench=furniture(FurnitureKind::Workbench);
        publish(session->prepareCraftCompass(next(),bench,admitted,error));publish(session->prepareEquipUtility(next(),slot(ItemKind::TrailCompass),error));
        publish(session->prepareCraftStaff(next(),bench,admitted,error));publish(session->prepareEquipTool(next(),slot(ItemKind::TrailStaff),error));
    }
    void quest(uint8_t id,uint8_t npc) {
        publish(session->prepareAcceptTrailQuest(next(),id,npc,admitted,error));
        publish(session->prepareCompleteTrailQuest(next(),id,npc,admitted,error));
    }
    void core() {
        CombatTick tick;const auto& state=session->state();tick.tick=state.combat.tick+1;tick.player=state.player;tick.health=state.health;tick.playerCombat=state.combat.player;
        for(size_t i=0;i<2;++i)tick.enemies[i]=state.combat.encounters[i].checkpoint;
        auto& guardian=tick.enemies[1];guardian.positioned=true;guardian.health=0;guardian.phase=EnemyPhase::Dead;
        publish(session->prepareCombatTick(next(),tick,admitted,error));
        publish(session->prepareClaimEncounterLoot(next(),2,1,admitted,error));
    }
    void moveCore(bool intoChest) {
        const auto& state=session->state();const auto* chest=AdventureSession::findComponent(state,furniture(FurnitureKind::Chest));ASSERT_TRUE(chest);
        uint8_t index=slot(ItemKind::RelayCore);
        if(!intoChest){index=255;for(size_t i=0;i<chest->slots.size();++i)if(chest->slots[i].kind==ItemKind::RelayCore)index=static_cast<uint8_t>(i);}
        publish(session->prepareTransfer(next(),{intoChest?0:chest->id,intoChest?chest->id:0,
            intoChest?state.backpackRevision:chest->revision,intoChest?chest->revision:state.backpackRevision,index,1},admitted,error));
    }
    std::vector<std::byte> encode(const AdventureState& state) {
        std::vector<std::byte> bytes;EXPECT_TRUE(AdventureSaveCodec::encode(state,content,bytes,error))<<error;return bytes;
    }
};
TEST_F(AdventureTrailSessionTest, ExplicitAcceptanceAndCurrentEquipmentControlTrailPreparation) {
    const auto initial=session->state();
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),2,2,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),5,2,admitted,error));EXPECT_EQ(session->state(),initial);
    home();gear();const auto equipped=session->state();
    EXPECT_TRUE(trailQuestReady(equipped,2));EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),2,2,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),2,1,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),2,2,blocked,error));EXPECT_EQ(session->state(),equipped);
    publish(session->prepareAcceptTrailQuest(next(),2,2,admitted,error));
    publish(session->prepareEquipUtility(next(),255,error));const auto missing=session->state();
    EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),2,2,admitted,error));EXPECT_EQ(session->state(),missing);
    publish(session->prepareEquipUtility(next(),slot(ItemKind::TrailCompass),error));
    const auto stamp=next();auto finish=session->prepareCompleteTrailQuest(stamp,2,2,admitted,error);
    auto duplicate=session->prepareCompleteTrailQuest(stamp,2,2,admitted,error);ASSERT_TRUE(finish);ASSERT_TRUE(duplicate);
    publish(std::move(finish));const auto done=session->state();EXPECT_FALSE(session->commit(std::move(*duplicate),error));
    EXPECT_EQ(done.trail.quests[0].rewardRevision,done.revision);EXPECT_GT(done.trail.quests[0].rewardRevision,done.firstHome.rewardRevision);
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),2,2,admitted,error));EXPECT_EQ(session->state(),done);
    publish(session->prepareEquipUtility(next(),255,error));EXPECT_EQ(session->state().trail.quests[0],done.trail.quests[0]);
}
TEST_F(AdventureTrailSessionTest, EarlierCoreClaimInChestCountsAfterExplicitQuestAcceptance) {
    home();gear();core();moveCore(true);const auto claimed=session->state();
    EXPECT_EQ(itemCount(claimed.backpack,ItemKind::RelayCore),0u);EXPECT_TRUE(trailQuestReady(claimed,3));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),3,3,admitted,error));EXPECT_EQ(session->state(),claimed);
    quest(2,2);EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),3,3,admitted,error));
    EXPECT_FALSE(session->prepareAcceptTrailQuest(next(),3,2,admitted,error));
    publish(session->prepareAcceptTrailQuest(next(),3,3,admitted,error));const auto accepted=session->state();
    EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),3,3,blocked,error));EXPECT_EQ(session->state(),accepted);
    publish(session->prepareCompleteTrailQuest(next(),3,3,admitted,error));
    EXPECT_GT(session->state().trail.quests[1].rewardRevision,claimed.combat.encounters[1].lootClaimRevision);
    EXPECT_EQ(session->state().combat.encounters,claimed.combat.encounters);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::RelayCore),0u);
}
TEST_F(AdventureTrailSessionTest, RelayConsumesOneCarriedCoreAndCompletesOneAcceptedQuestAtomically) {
    home();gear();core();quest(2,2);quest(3,3);moveCore(true);
    EXPECT_FALSE(session->prepareActivateRelay(next(),admitted,error));
    publish(session->prepareAcceptTrailQuest(next(),4,3,admitted,error));const auto stored=session->state();
    EXPECT_FALSE(session->prepareCompleteTrailQuest(next(),4,3,admitted,error));
    EXPECT_FALSE(session->prepareActivateRelay(next(),admitted,error));EXPECT_EQ(session->state(),stored);
    moveCore(false);const auto carried=session->state();
    EXPECT_FALSE(session->prepareActivateRelay(next(),{},error));EXPECT_FALSE(session->prepareActivateRelay(next(),blocked,error));EXPECT_EQ(session->state(),carried);
    const auto stamp=next();auto activation=session->prepareActivateRelay(stamp,admitted,error);
    auto duplicate=session->prepareActivateRelay(stamp,admitted,error);ASSERT_TRUE(activation);ASSERT_TRUE(duplicate);
    EXPECT_EQ(activation->state().revision,carried.revision+1);EXPECT_EQ(activation->state().backpackRevision,activation->state().revision);
    publish(std::move(activation));const auto restored=session->state();
    EXPECT_FALSE(session->commit(std::move(*duplicate),error));EXPECT_FALSE(session->prepareActivateRelay(next(),admitted,error));
    EXPECT_EQ(restored.trail.relayActivationRevision,restored.revision);EXPECT_EQ(restored.trail.quests[2].rewardRevision,restored.revision);
    EXPECT_EQ(itemCount(restored.backpack,ItemKind::RelayCore),0u);EXPECT_EQ(restored.combat.encounters,carried.combat.encounters);
    AdventureState decoded;const auto bytes=encode(restored);ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error));EXPECT_EQ(decoded,restored);reload(decoded);
    publish(session->prepareRemoveStructure(next(),restored.structures.front().id,admitted,error));
    EXPECT_TRUE(session->state().structures.empty());EXPECT_EQ(session->state().trail,restored.trail);
    auto forged=session->state();forged.backpack[23]={ItemKind::RelayCore,1};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
}
TEST_F(AdventureTrailSessionTest, DiscoverySurvivesFullBackpackAndRewardCanOnlyPublishOnce) {
    auto packed=session->state();packed.backpack.fill({ItemKind::Wood,999});reload(packed);
    EXPECT_FALSE(session->prepareClaimDiscovery(next(),1,admitted,error));
    EXPECT_FALSE(session->prepareDiscover(next(),0,admitted,error));EXPECT_FALSE(session->prepareDiscover(next(),3,admitted,error));
    EXPECT_FALSE(session->prepareDiscover(next(),1,blocked,error));EXPECT_EQ(session->state(),packed);
    publish(session->prepareDiscover(next(),1,admitted,error));const auto discovered=session->state();
    EXPECT_NE(discovered.trail.discoveries[0].discoveredRevision,0u);EXPECT_EQ(discovered.backpack,packed.backpack);
    EXPECT_FALSE(session->prepareClaimDiscovery(next(),1,admitted,error));EXPECT_FALSE(session->prepareDiscover(next(),1,admitted,error));EXPECT_EQ(session->state(),discovered);
    packed=discovered;packed.backpack[23]={};reload(packed);
    EXPECT_FALSE(session->prepareClaimDiscovery(next(),1,blocked,error));EXPECT_EQ(session->state(),packed);
    publish(session->prepareClaimDiscovery(next(),1,admitted,error));const auto reward=session->state();
    EXPECT_EQ(itemCount(reward.backpack,ItemKind::Scrap),4u);EXPECT_GT(reward.trail.discoveries[0].rewardClaimRevision,reward.trail.discoveries[0].discoveredRevision);
    EXPECT_FALSE(session->prepareClaimDiscovery(next(),1,admitted,error));EXPECT_EQ(session->state(),reward);
    auto space=reward;space.backpack[22]={};reload(space);publish(session->prepareDiscover(next(),2,admitted,error));
    publish(session->prepareClaimDiscovery(next(),2,admitted,error));EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone),12u);
    AdventureState decoded;const auto bytes=encode(session->state());ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error));EXPECT_EQ(decoded,session->state());
}
TEST_F(AdventureTrailSessionTest, InvalidReceiptsPrerequisitesAndDisabledProfilesRefuseRestore) {
    home();gear();quest(2,2);const auto base=session->state();
    auto forged=base;forged.trail.quests[0].rewardRevision=forged.firstHome.rewardRevision;EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=base;forged.trail.quests[1]={QuestPhase::Completed,forged.revision};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=base;forged.trail.quests[2]={QuestPhase::Active,0};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=base;forged.trail.quests[3]={QuestPhase::Completed,forged.revision};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=base;forged.trail.discoveries[0]={0,forged.revision};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=base;forged.trail.discoveries[0]={forged.revision,forged.revision};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    auto disabled=content;disabled.enableTrailProgress=false;disabled.discoveries={};EXPECT_FALSE(AdventureSession::restore(base,disabled,error));
    auto untouched=base;untouched.trail={};auto legacy=AdventureSession::restore(untouched,disabled,error);ASSERT_TRUE(legacy);
    const CommandStamp stamp{untouched.revision,untouched.lastRequestSequence+1,1};
    EXPECT_FALSE(legacy->prepareDiscover(stamp,1,admitted,error));EXPECT_FALSE(legacy->prepareAcceptTrailQuest(stamp,2,2,admitted,error));
    auto badContent=content;badContent.discoveries[1].id=1;EXPECT_FALSE(AdventureSession::create(world,badContent,error));
    badContent=content;badContent.discoveries[0].reward={ItemKind::RelayCore,1};EXPECT_FALSE(AdventureSession::create(world,badContent,error));
}
TEST_F(AdventureTrailSessionTest, FrozenSchemaThreeMigrationPreservesCombatCoreHomeAndEveryOldByte) {
    const auto file=std::filesystem::path(__FILE__).parent_path()/"fixtures/adventure/schema3-component-r01.bin";
    std::ifstream stream(file,std::ios::binary);ASSERT_TRUE(stream)<<file;
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),{});
    const auto original=std::as_bytes(std::span(raw));ASSERT_EQ(original.size(),1542u);
    EXPECT_EQ(core::sha256Hex(core::sha256(original)),"6ff640f312f07d54b4c16c35de29caa8be426ccd7a9015c4dcdde7d2679f6753");
    construction::WorldNamespace fixtureWorld;for(size_t i=0;i<16;++i)fixtureWorld.bytes[i]=std::to_integer<uint8_t>(original[12+i]);
    content.town={-63,-145.73502075195313,-895,0};
    content.encounters[0].spawn={-58,-146.3750048828125,-945,0};content.encounters[1].spawn={-55,-147.65500366210938,-967,0};
    AdventureState migrated;AdventureSaveLoadMetadata metadata;
    ASSERT_TRUE(AdventureSaveCodec::decode(original,fixtureWorld,content,migrated,error,&metadata))<<error;
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{3,true}));EXPECT_EQ(migrated.health,47);EXPECT_EQ(migrated.revision,11u);
    EXPECT_EQ(migrated.combat.player.attackImpactTick,13u);EXPECT_EQ(migrated.combat.player.dodgeUntilTick,10u);
    EXPECT_EQ(migrated.combat.encounters[0].checkpoint.phase,EnemyPhase::Windup);
    EXPECT_NE(migrated.combat.encounters[1].deathRevision,0u);EXPECT_NE(migrated.combat.encounters[1].lootClaimRevision,0u);
    EXPECT_EQ(migrated.trail,AdventureTrailProgress{});EXPECT_EQ(migrated.firstHome.phase,QuestPhase::Completed);
    uint32_t cores=0;for(const auto& c:migrated.components)cores+=itemCount(c.slots,ItemKind::RelayCore);EXPECT_EQ(cores,1u);
    auto upgraded=encode(migrated);EXPECT_EQ(upgraded.size(),original.size()+migrated.components.size());
    AdventureState repeated;ASSERT_TRUE(AdventureSaveCodec::decode(upgraded,fixtureWorld,content,repeated,error,&metadata));EXPECT_EQ(repeated,migrated);
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));EXPECT_EQ(encode(repeated),upgraded);
    stripSchema6DoorFields(upgraded,migrated);
    upgraded[8]=std::byte{3};std::copy(content.compatibilityIdentities[2]->bytes.begin(),content.compatibilityIdentities[2]->bytes.end(),upgraded.begin()+28);checksum(upgraded);
    EXPECT_TRUE(std::equal(upgraded.begin(),upgraded.end(),original.begin(),original.end()));
    auto bad=upgraded;bad[135]=std::byte{8};bad[136]=std::byte{1};bad[137]=std::byte{0};checksum(bad);
    const auto before=repeated;metadata={99,true};EXPECT_FALSE(AdventureSaveCodec::decode(bad,fixtureWorld,content,repeated,error,&metadata));EXPECT_EQ(repeated,before);EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{99,true}));
    bad=upgraded;bad[bad.size()-33]=std::byte{1};checksum(bad);EXPECT_FALSE(AdventureSaveCodec::decode(bad,fixtureWorld,content,repeated,error));EXPECT_EQ(repeated,before);
    auto missing=content;missing.compatibilityIdentities[2].reset();EXPECT_FALSE(AdventureSaveCodec::decode(original,fixtureWorld,missing,repeated,error));EXPECT_EQ(repeated,before);
}
} // namespace
} // namespace voxy::game::adventure
