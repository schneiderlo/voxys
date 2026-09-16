#include "game/adventure/adventure_save.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>

namespace voxy::game::adventure {
namespace {
// These tests isolate transaction authority. Physical reach, attacks and safe
// recovery are checked by the separate real-world/resolver tests; no UI calls
// this fixture validator in production.
const CandidateValidator admitted=[](const AdventureState&,const AdventureState&,std::string&){return true;};
const CandidateValidator blocked=[](const AdventureState&,const AdventureState&,std::string& error){error="The physical action is blocked.";return false;};
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

class AdventureCombatSessionTest:public testing::Test {
protected:
    construction::WorldNamespace world{{'c','o','m','b','a','t','-','a','u','t','h','-','0','0','0','1'}};
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    void SetUp() override {
        content.identity.bytes[0]=std::byte{73};content.town={0,12,0,0};
        content.compatibilityIdentities=installedAdventureCompatibilityIdentities();
        content.encounters={EncounterContent{1,1,{4,12,0,0},60,{ItemKind::Scrap,8},false},
            EncounterContent{2,1,{8,12,0,0},100,{ItemKind::RelayCore,1},true}};
        session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    }
    CommandStamp next() const {return {session->state().revision,session->state().lastRequestSequence+1,1};}
    void publish(std::optional<AdventureSession::PreparedChange> change) {
        ASSERT_TRUE(change)<<error;ASSERT_TRUE(session->commit(std::move(*change),error))<<error;
    }
    void reload(const AdventureState& state) {
        auto restored=AdventureSession::restore(state,content,error);ASSERT_TRUE(restored)<<error;session=std::move(restored);
    }
    CombatTick tick() const {
        const auto& state=session->state();CombatTick result;
        result.tick=state.combat.tick+1;result.player=state.player;result.health=state.health;result.playerCombat=state.combat.player;
        for(size_t i=0;i<result.enemies.size();++i)result.enemies[i]=state.combat.encounters[i].checkpoint;
        return result;
    }
    void admit() {
        auto request=tick();for(auto& enemy:request.enemies){enemy.positioned=true;enemy.phase=EnemyPhase::Idle;}
        publish(session->prepareCombatTick(next(),request,admitted,error));
    }
    void kill(uint8_t id) {
        auto request=tick();auto& enemy=request.enemies[id-1u];enemy.health=0;enemy.phase=EnemyPhase::Dead;
        publish(session->prepareCombatTick(next(),request,admitted,error));
    }
    uint64_t furniture(PieceKind kind) {
        if(session->state().structures.empty())publish(session->preparePlace(next(),{0,PieceKind::Foundation,{0,600,0},0,0},admitted,error));
        auto change=session->preparePlace(next(),{session->state().structures.front().id,kind,{0,616,0},0,0},admitted,error);
        if(!change){ADD_FAILURE()<<error;return 0;}
        const auto part=change->changedPart();publish(std::move(change));
        for(const auto& component:session->state().components)if(component.part==part)return component.id;
        ADD_FAILURE()<<"Missing furniture component";return 0;
    }
    uint8_t slot(ItemKind kind) const {
        for(size_t i=0;i<session->state().backpack.size();++i)
            if(session->state().backpack[i].kind==kind)return static_cast<uint8_t>(i);
        return 255;
    }
    void equipStaff() {
        const auto bench=furniture(PieceKind::Workbench);
        publish(session->prepareCraftStaff(next(),bench,admitted,error));
        publish(session->prepareEquipTool(next(),slot(ItemKind::TrailStaff),error));
    }
    std::vector<std::byte> encode(const AdventureState& state,const AdventureContent& installed) {
        std::vector<std::byte> bytes;EXPECT_TRUE(AdventureSaveCodec::encode(state,installed,bytes,error))<<error;return bytes;
    }
};
TEST_F(AdventureCombatSessionTest, StaffRecipeSpendsOnceAndCannotLoseInputsWhenOutputIsFull) {
    const auto bench=furniture(PieceKind::Workbench);const auto initial=session->state();
    EXPECT_FALSE(session->prepareCraftStaff(next(),bench,blocked,error));
    EXPECT_FALSE(session->prepareCraftStaff(next(),0,admitted,error));EXPECT_EQ(session->state(),initial);
    auto packed=initial;packed.backpack.fill({ItemKind::Stone,999});
    packed.backpack[0]={ItemKind::Wood,999};packed.backpack[1]={ItemKind::Scrap,999};reload(packed);
    EXPECT_FALSE(session->prepareCraftStaff(next(),bench,admitted,error));EXPECT_EQ(session->state(),packed);
    packed.backpack[23]={};reload(packed);
    publish(session->prepareCraftStaff(next(),bench,admitted,error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),995u);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Scrap),995u);
    EXPECT_EQ(session->state().backpack[23],(ItemStack{ItemKind::TrailStaff,1}));
    publish(session->prepareEquipTool(next(),23,error));
    EXPECT_EQ(session->state().equippedTool,(ItemStack{ItemKind::TrailStaff,1}));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::TrailStaff),0u);
}
TEST_F(AdventureCombatSessionTest, FixedTicksRejectReplayForeignCommandsAndHealthBypassesAtomically) {
    const auto initial=session->state();auto request=tick();request.health=72;
    EXPECT_FALSE(session->updatePlayer(initial.player,72,error));
    EXPECT_FALSE(session->prepareCombatTick(next(),request,{},error));
    EXPECT_FALSE(session->prepareCombatTick(next(),request,blocked,error));
    EXPECT_FALSE(session->prepareCombatTick({initial.revision,1,2},request,admitted,error));
    EXPECT_EQ(session->state(),initial);
    const auto stamp=next();auto first=session->prepareCombatTick(stamp,request,admitted,error);
    auto duplicate=session->prepareCombatTick(stamp,request,admitted,error);ASSERT_TRUE(first);ASSERT_TRUE(duplicate);
    publish(std::move(first));const auto wounded=session->state();
    EXPECT_EQ(wounded.health,72);EXPECT_EQ(wounded.combat.tick,1u);
    EXPECT_FALSE(session->commit(std::move(*duplicate),error));
    EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
    request=tick();request.tick++;EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
    request=tick();request.health=100;EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
    request=tick();request.playerCombat.attackSerial=1;EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
    EXPECT_FALSE(session->updatePlayer(wounded.player,100,error));EXPECT_EQ(session->state(),wounded);
    auto exhausted=wounded;exhausted.revision=UINT64_MAX-kMaximumCombatDeadlineTicks;
    exhausted.combat.tick=exhausted.revision;reload(exhausted);request=tick();
    EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));EXPECT_EQ(session->state(),exhausted);
}
TEST_F(AdventureCombatSessionTest, PendingImpactDodgeAndEnemyWindupSurviveExactReload) {
    equipStaff();admit();auto request=tick();request.playerCombat.attackSerial=1;
    request.playerCombat.attackReadyTick=request.tick+30;request.playerCombat.attackImpactTick=request.tick+8;
    request.playerCombat.dodgeReadyTick=request.tick+40;request.playerCombat.dodgeUntilTick=request.tick+12;
    request.playerCombat.invulnerableUntilTick=request.tick+6;request.playerCombat.dodgeDirectionX=.6;request.playerCombat.dodgeDirectionZ=.8;
    request.enemies[0].phase=EnemyPhase::Windup;request.enemies[0].phaseTicks=18;request.enemies[0].attackSerial=1;
    publish(session->prepareCombatTick(next(),request,admitted,error));const auto before=session->state();
    const auto bytes=encode(before,content);AdventureState decoded;AdventureSaveLoadMetadata metadata;
    ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error,&metadata))<<error;
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));EXPECT_EQ(decoded,before);reload(decoded);
    EXPECT_EQ(encode(session->state(),content),bytes);
    auto invalid=before;invalid.combat.player.attackImpactTick=invalid.combat.tick;
    EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=before;invalid.combat.player.dodgeDirectionX=std::numeric_limits<double>::infinity();
    EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=before;invalid.combat.player.attackReadyTick=invalid.combat.tick+kMaximumCombatDeadlineTicks+1;
    EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
}
TEST_F(AdventureCombatSessionTest, DeathLootAndUniqueCoreHavePermanentAtomicReceipts) {
    const auto chest=furniture(PieceKind::Chest);admit();kill(1);kill(2);
    const auto dead=session->state();auto request=tick();request.enemies[1].health=100;request.enemies[1].phase=EnemyPhase::Return;
    EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
    EXPECT_FALSE(session->prepareClaimEncounterLoot(next(),2,2,admitted,error));
    EXPECT_FALSE(session->prepareClaimEncounterLoot(next(),2,1,blocked,error));EXPECT_EQ(session->state(),dead);
    auto packed=dead;packed.backpack.fill({ItemKind::Stone,999});reload(packed);
    EXPECT_FALSE(session->prepareClaimEncounterLoot(next(),2,1,admitted,error));EXPECT_EQ(session->state(),packed);
    packed.backpack[23]={};reload(packed);const auto stamp=next();
    auto first=session->prepareClaimEncounterLoot(stamp,2,1,admitted,error);
    auto duplicate=session->prepareClaimEncounterLoot(stamp,2,1,admitted,error);ASSERT_TRUE(first);ASSERT_TRUE(duplicate);
    publish(std::move(first));const auto claimed=session->state();EXPECT_FALSE(session->commit(std::move(*duplicate),error));
    EXPECT_EQ(claimed.combat.encounters[1].deathRevision,dead.combat.encounters[1].deathRevision);
    EXPECT_EQ(claimed.combat.encounters[1].lootClaimRevision,claimed.revision);
    const auto* container=AdventureSession::findComponent(claimed,chest);ASSERT_TRUE(container);
    publish(session->prepareTransfer(next(),{0,chest,claimed.backpackRevision,container->revision,23,1},admitted,error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::RelayCore),0u);
    EXPECT_EQ(itemCount(AdventureSession::findComponent(session->state(),chest)->slots,ItemKind::RelayCore),1u);
    const auto bytes=encode(session->state(),content);AdventureState decoded;
    ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error))<<error;EXPECT_EQ(decoded,session->state());reload(decoded);
    EXPECT_FALSE(session->prepareClaimEncounterLoot(next(),2,1,admitted,error));
    EXPECT_FALSE(session->prepareRemove(next(),AdventureSession::findComponent(decoded,chest)->part,admitted,error));
    publish(session->prepareClaimEncounterLoot(next(),1,1,admitted,error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Scrap),8u);
    EXPECT_FALSE(session->prepareClaimEncounterLoot(next(),1,1,admitted,error));
    auto forged=session->state();forged.backpack[23]={ItemKind::RelayCore,1};
    EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=session->state();forged.components[0].slots={};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=session->state();forged.combat.encounters[1].lootClaimRevision=0;EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=session->state();forged.trail.relayActivationRevision=forged.revision;EXPECT_FALSE(AdventureSession::restore(forged,content,error));
}
TEST_F(AdventureCombatSessionTest, DefeatReloadRecoveryPreservesPossessionsAndRejectsUnsafeRest) {
    equipStaff();const auto bed=furniture(PieceKind::Bed);admit();kill(1);
    publish(session->prepareClaimEncounterLoot(next(),1,1,admitted,error));
    auto request=tick();request.playerCombat.attackSerial=1;request.health=0;
    publish(session->prepareCombatTick(next(),request,admitted,error));const auto defeated=session->state();
    EXPECT_FALSE(session->updatePlayer({1,12,0,0},0,error));
    EXPECT_FALSE(session->preparePlace(next(),{0,PieceKind::Foundation,{100,600,0},0,0},admitted,error));
    EXPECT_FALSE(session->prepareUseBed(next(),bed,content.town,admitted,error));
    EXPECT_FALSE(session->prepareEquipTool(next(),255,error));
    EXPECT_FALSE(session->prepareRecover(next(),content.town,blocked,error));
    EXPECT_FALSE(session->prepareRecover(next(),{100,12,0,0},admitted,error));EXPECT_EQ(session->state(),defeated);
    AdventureState decoded;const auto bytes=encode(defeated,content);
    ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error));reload(decoded);EXPECT_EQ(session->state(),defeated);
    publish(session->prepareRecover(next(),content.town,admitted,error));const auto recovered=session->state();
    EXPECT_EQ(recovered.health,100);EXPECT_EQ(recovered.player,content.town);EXPECT_EQ(recovered.backpack,defeated.backpack);
    EXPECT_EQ(recovered.equippedTool,defeated.equippedTool);EXPECT_EQ(recovered.combat.encounters,defeated.combat.encounters);
    EXPECT_EQ(recovered.combat.player.attackSerial,1u);EXPECT_EQ(recovered.combat.player.attackImpactTick,0u);
    auto wound=tick();wound.health=31;publish(session->prepareCombatTick(next(),wound,admitted,error));const auto wounded=session->state();
    EXPECT_FALSE(session->prepareUseBed(next(),bed,content.town,blocked,error));EXPECT_EQ(session->state(),wounded);
    publish(session->prepareUseBed(next(),bed,content.town,admitted,error));EXPECT_EQ(session->state().health,100);
}
TEST_F(AdventureCombatSessionTest, EnemyHealingRequiresCompletedReturnAndNeverRevivesTheDead) {
    admit();auto request=tick();request.enemies[0].health=20;publish(session->prepareCombatTick(next(),request,admitted,error));
    const auto wounded=session->state();request=tick();request.enemies[0].health=60;request.enemies[0].phase=EnemyPhase::Return;
    EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));EXPECT_EQ(session->state(),wounded);
    request=tick();request.enemies[0].phase=EnemyPhase::Return;publish(session->prepareCombatTick(next(),request,admitted,error));
    request=tick();request.enemies[0].health=40;EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
    request=tick();request.enemies[0].health=60;request.enemies[0].phase=EnemyPhase::Idle;
    publish(session->prepareCombatTick(next(),request,admitted,error));EXPECT_EQ(session->state().combat.encounters[0].checkpoint.health,60);
    kill(1);request=tick();request.enemies[0].health=60;request.enemies[0].phase=EnemyPhase::Idle;
    EXPECT_FALSE(session->prepareCombatTick(next(),request,admitted,error));
}
TEST_F(AdventureCombatSessionTest, SchemaOneAndTwoMigrateWithoutHealingOrRegrantingItems) {
    // Synthetic boundary packets complement the unchanged real G-A fixture.
    // The completed real G-B player archive is a separate gate requirement.
    const auto bench=furniture(PieceKind::Workbench);(void)bench;
    const auto base=session->state();
    for(uint32_t version=1;version<=2;++version) {
        auto oldContent=content;oldContent.identity=*content.compatibilityIdentities[version-1];oldContent.encounters={};
        auto old=base;old.content=oldContent.identity;old.health=0;old.backpack[0].quantity=137;initializeAdventureProgress(old,oldContent);
        if(version==2){old.firstHome={QuestPhase::Completed,old.revision};old.metNpcMask=7;old.equippedUtility={ItemKind::TrailCompass,1};}
        auto bytes=encode(old,oldContent);ASSERT_FALSE(bytes.empty());
        stripSchema6DoorFields(bytes,old);
        const size_t extension=kAdventureSchema3ExtensionBytes+(version==1?13u:0u);
        bytes.erase(bytes.end()-static_cast<ptrdiff_t>(32+extension),bytes.end()-32);
        bytes[8]=static_cast<std::byte>(version);checksum(bytes);
        AdventureState migrated;AdventureSaveLoadMetadata metadata;
        ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,migrated,error,&metadata))<<error;
        EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{version,true}));EXPECT_EQ(migrated.health,0);
        auto expected=old;expected.content=content.identity;initializeAdventureProgress(expected,content);EXPECT_EQ(migrated,expected);
        for(const auto& record:migrated.combat.encounters){EXPECT_FALSE(record.checkpoint.positioned);EXPECT_EQ(record.checkpoint.phase,EnemyPhase::Dormant);}
        EXPECT_EQ(migrated.trail,AdventureTrailProgress{});
        const auto upgraded=encode(migrated,content);AdventureState again;
        ASSERT_TRUE(AdventureSaveCodec::decode(upgraded,world,content,again,error,&metadata));EXPECT_EQ(again,migrated);
        EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));
        auto invalid=bytes;invalid[135]=std::byte{6};invalid[136]=std::byte{1};invalid[137]=std::byte{0};checksum(invalid);
        const auto unchanged=again;metadata={99,true};
        EXPECT_FALSE(AdventureSaveCodec::decode(invalid,world,content,again,error,&metadata));EXPECT_EQ(again,unchanged);
        EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{99,true}));
        auto missing=content;missing.compatibilityIdentities[version-1].reset();
        EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,missing,again,error));EXPECT_EQ(again,unchanged);
    }
}
TEST_F(AdventureCombatSessionTest, ForgedCheckpointBooleansAndUnsupportedFutureProgressRefuseAtomically) {
    const auto bytes=encode(session->state(),content);ASSERT_GT(bytes.size(),kAdventureSchema3ExtensionBytes+32);
    auto output=session->state();const auto original=output;AdventureSaveLoadMetadata metadata{99,true};
    // Combat prefix = clock8 + player64; first enemy prefix = id1 + generation4.
    auto bad=bytes;const auto extension=bytes.size()-32-kAdventureSchema3ExtensionBytes;
    bad[extension+8+64+1+4]=std::byte{2};checksum(bad);
    EXPECT_FALSE(AdventureSaveCodec::decode(bad,world,content,output,error,&metadata));EXPECT_EQ(output,original);
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{99,true}));
    bad=bytes;bad[bad.size()-33]=std::byte{1};checksum(bad);
    EXPECT_FALSE(AdventureSaveCodec::decode(bad,world,content,output,error));EXPECT_EQ(output,original);
    auto forged=original;forged.backpack[3]={ItemKind::RelayCore,1};EXPECT_FALSE(AdventureSession::restore(forged,content,error));
    forged=original;forged.combat.encounters[1].lootClaimRevision=1;EXPECT_FALSE(AdventureSession::restore(forged,content,error));
}
} // namespace
} // namespace voxy::game::adventure
