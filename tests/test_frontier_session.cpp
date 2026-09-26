#include "game/adventure/adventure_save.hpp"
#include <gtest/gtest.h>
#include <algorithm>

namespace voxy::game::adventure {
namespace {
const CandidateValidator accepted=[](const AdventureState&,const AdventureState&,std::string&){return true;};
const CandidateValidator blocked=[](const AdventureState&,const AdventureState&,std::string& error){error="The workbench needs shelter and reachable working space.";return false;};
class FrontierSessionTest:public testing::Test {
protected:
    construction::WorldNamespace world{{'f','r','o','n','t','i','e','r','-','t','e','s','t','0','0','1'}};
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    void SetUp() override {
        content.identity.bytes[0]=std::byte{77};content.frontier=true;content.town={0,12,0,0};
        content.frontierEnemies={{31,1,{20,12,0,0},60,{ItemKind::Scrap,8}}, {79,2,{30,12,0,0},80,{ItemKind::Wood,16}}};
        content.frontierSites={{10,{0,12,0,0},FrontierSiteKind::Cache,{ItemKind::Stone,24}},
            {20,{25,12,0,0},FrontierSiteKind::Beacon,{}}, {30,{40,12,0,0},FrontierSiteKind::Quarry,{}}};
        content.resourceNodes={{1,{4,12,0,0},{ItemKind::Wood,24}}, {2,{40,12,0,0},{ItemKind::CutStone,8}}};
        session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    }
    CommandStamp next()const{return {session->state().revision,session->state().lastRequestSequence+1,1};}
    bool commit(std::optional<AdventureSession::PreparedChange> change) {
        if(!change){ADD_FAILURE()<<error;return false;}
        return session->commit(std::move(*change),error);
    }
    uint64_t bench() {
        if(!commit(session->preparePlace(next(),{0,PieceKind::Foundation,{0,600,0},0,0},accepted,error)))return 0;
        const auto structure=session->state().structures.back().id;
        if(!commit(session->preparePlace(next(),{structure,PieceKind::Workbench,{0,616,0},0,0},accepted,error)))return 0;
        return session->state().components.back().id;
    }
    void roundtrip() {
        const auto before=session->state();std::vector<std::byte> bytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(before,content,bytes,error))<<error;
        ASSERT_EQ(bytes[8],std::byte{kFrontierSaveSchema});
        AdventureState decoded;AdventureSaveLoadMetadata metadata;
        ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error,&metadata))<<error;
        EXPECT_EQ(metadata.sourceSchema,kFrontierSaveSchema);EXPECT_FALSE(metadata.migrated);
        ASSERT_EQ(decoded,before);
        session=AdventureSession::restore(decoded,content,error);ASSERT_TRUE(session)<<error;
    }
};
TEST_F(FrontierSessionTest, FreshWorldHasEarnedEconomyAndStableNonContiguousEntities) {
    EXPECT_FALSE(content.freeBuilding);EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),48u);
    EXPECT_EQ(session->state().equippedTool,(ItemStack{ItemKind::TrailStaff,1}));
    EXPECT_EQ(frontierEnemy(session->state(),79)->generation,2u);EXPECT_EQ(session->state().frontier.sites.size(),3u);
    EXPECT_FALSE(session->prepareGather(next(),2,accepted,error));roundtrip();
}
TEST_F(FrontierSessionTest, GatherBuildRestoreCraftUseUpgradeAndReloadWithoutDuplicateRewards) {
    ASSERT_TRUE(commit(session->prepareGather(next(),1,accepted,error)));
    const auto station=bench();ASSERT_NE(station,0u);
    ASSERT_TRUE(commit(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error)));
    EXPECT_NE(session->state().frontier.quarryUnlockRevision,0u);
    ASSERT_TRUE(commit(session->prepareCraftQuarryHammer(next(),station,accepted,error)));
    uint8_t slot=0;while(slot<kBackpackSlots&&session->state().backpack[slot].kind!=ItemKind::QuarryHammer)++slot;
    ASSERT_LT(slot,kBackpackSlots);ASSERT_TRUE(commit(session->prepareEquipTool(next(),slot,error)));
    ASSERT_TRUE(commit(session->prepareGather(next(),2,accepted,error)));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::CutStone),16u);roundtrip();
    const auto saved=session->state();
    EXPECT_FALSE(session->prepareGather(next(),1,accepted,error));EXPECT_FALSE(session->prepareGather(next(),2,accepted,error));
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error));EXPECT_EQ(session->state(),saved);
}
TEST_F(FrontierSessionTest, RestoreRequiresOwnedBenchAndLiveGeometryApprovalWithoutSpending) {
    const auto original=session->state();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),20,777,accepted,error));EXPECT_EQ(session->state(),original);
    const auto station=bench();const auto before=session->state();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),20,station,blocked,error));EXPECT_EQ(session->state(),before);
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),20,station,{},error));EXPECT_EQ(session->state(),before);
    EXPECT_FALSE(session->prepareCraftQuarryHammer(next(),station,accepted,error));EXPECT_EQ(session->state(),before);
}
TEST_F(FrontierSessionTest, DamageDeathLootAndPlayerRecoveryPersistAndPayExactlyOnce) {
    ASSERT_TRUE(commit(session->prepareFrontierEnemyHit(next(),31,25,{21,12,1,0},accepted,error)));roundtrip();
    EXPECT_EQ(frontierEnemy(session->state(),31)->health,35);
    EXPECT_FALSE(session->prepareFrontierLoot(next(),31,accepted,error));
    ASSERT_TRUE(commit(session->prepareFrontierEnemyHit(next(),31,35,{22,12,1,0},accepted,error)));
    const auto dead=session->state();
    EXPECT_FALSE(session->prepareFrontierEnemyHit(next(),31,35,{22,12,1,0},accepted,error));EXPECT_EQ(session->state(),dead);
    EXPECT_FALSE(session->prepareFrontierLoot(next(),31,blocked,error));EXPECT_EQ(session->state(),dead);
    ASSERT_TRUE(commit(session->prepareFrontierLoot(next(),31,accepted,error)));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Scrap),20u);
    ASSERT_TRUE(commit(session->prepareFrontierPlayerHit(next(),100,accepted,error)));
    EXPECT_EQ(session->state().health,0);roundtrip();
    EXPECT_FALSE(session->prepareFrontierLoot(next(),31,accepted,error));
    ASSERT_TRUE(commit(session->prepareRecover(next(),content.town,accepted,error)));
    EXPECT_EQ(session->state().health,100);EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Scrap),20u);
    EXPECT_FALSE(session->prepareFrontierLoot(next(),31,accepted,error));
}
TEST_F(FrontierSessionTest, FullBackpackRefusesLootWithoutConsumingItsReceipt) {
    ASSERT_TRUE(commit(session->prepareFrontierEnemyHit(next(),31,60,{21,12,0,0},accepted,error)));
    auto full=session->state();full.backpack.fill({ItemKind::Wood,999});
    session=AdventureSession::restore(full,content,error);ASSERT_TRUE(session)<<error;
    EXPECT_FALSE(session->prepareFrontierLoot(next(),31,accepted,error));EXPECT_EQ(session->state(),full);
    EXPECT_EQ(frontierEnemy(session->state(),31)->lootClaimRevision,0u);
}
TEST_F(FrontierSessionTest, CacheVisitAndClaimAreIndependentOnceOnlyReceipts) {
    ASSERT_TRUE(commit(session->prepareFrontierDiscover(next(),10,accepted,error)));
    EXPECT_EQ(frontierSite(session->state(),10)->rewardClaimRevision,0u);
    ASSERT_TRUE(commit(session->prepareFrontierClaimSite(next(),10,accepted,error)));roundtrip();
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone),56u);
    const auto before=session->state();EXPECT_FALSE(session->prepareFrontierClaimSite(next(),10,accepted,error));
    EXPECT_FALSE(session->prepareFrontierClaimSite(next(),20,accepted,error));EXPECT_EQ(session->state(),before);
}
TEST_F(FrontierSessionTest, EnemyCheckpointCannotResurrectMoveCorpseOrOverwriteNewerState) {
    const std::array<FrontierEnemyPose,2> positions{{{31,{23,12,0,0}},{79,{32,12,0,0}}}};
    ASSERT_TRUE(commit(session->prepareFrontierCheckpointEnemies(next(),positions,accepted,error)));roundtrip();
    EXPECT_EQ(frontierEnemy(session->state(),79)->pose,positions[1].pose);
    auto prepared=session->prepareFrontierCheckpointEnemies(next(),positions,accepted,error);ASSERT_TRUE(prepared);
    ASSERT_TRUE(session->updatePlayer({1,12,0,0},100,error));const auto moved=session->state();
    EXPECT_FALSE(session->commit(std::move(*prepared),error));EXPECT_EQ(session->state(),moved);
    ASSERT_TRUE(commit(session->prepareFrontierEnemyHit(next(),31,60,{23,12,0,0},accepted,error)));
    EXPECT_FALSE(session->prepareFrontierCheckpointEnemies(next(),positions,accepted,error));
}
TEST_F(FrontierSessionTest, RejectsForgedUnlockIdentityHealthAndLootReceipts) {
    const auto state=session->state();
    auto bad=state;bad.frontier.quarryUnlockRevision=1;bad.revision=1;EXPECT_FALSE(AdventureSession::restore(bad,content,error));
    bad=state;bad.backpack[3]={ItemKind::CutStone,1};EXPECT_FALSE(AdventureSession::restore(bad,content,error));
    bad=state;bad.frontier.enemies[0].id=32;EXPECT_FALSE(AdventureSession::restore(bad,content,error));
    bad=state;bad.frontier.enemies[0].health=61;EXPECT_FALSE(AdventureSession::restore(bad,content,error));
    bad=state;bad.frontier.enemies[0].lootClaimRevision=1;bad.revision=1;EXPECT_FALSE(AdventureSession::restore(bad,content,error));
    bad=state;bad.frontier.sites[0].restoredRevision=1;bad.revision=1;EXPECT_FALSE(AdventureSession::restore(bad,content,error));
    auto invalid=content;invalid.frontierEnemies.push_back(content.frontierEnemies.front());
    EXPECT_FALSE(AdventureSession::create(world,invalid,error));invalid=content;invalid.freeBuilding=true;
    EXPECT_FALSE(AdventureSession::create(world,invalid,error));
}
TEST_F(FrontierSessionTest, LegacySchemaSixAndFrontierSchemaSevenCannotCrossProfiles) {
    std::vector<std::byte> frontierBytes;ASSERT_TRUE(AdventureSaveCodec::encode(session->state(),content,frontierBytes,error));
    auto legacy=content;legacy.frontier=false;legacy.frontierEnemies.clear();legacy.frontierSites.clear();legacy.resourceNodes.clear();
    auto oldSession=AdventureSession::create(world,legacy,error);ASSERT_TRUE(oldSession)<<error;
    std::vector<std::byte> oldBytes;ASSERT_TRUE(AdventureSaveCodec::encode(oldSession->state(),legacy,oldBytes,error));
    EXPECT_EQ(oldBytes[8],std::byte{kAdventureSaveSchema});
    AdventureState output=session->state();const auto before=output;
    EXPECT_FALSE(AdventureSaveCodec::decode(oldBytes,world,content,output,error));EXPECT_EQ(output,before);
    EXPECT_FALSE(AdventureSaveCodec::decode(frontierBytes,world,legacy,output,error));EXPECT_EQ(output,before);
    ASSERT_TRUE(AdventureSaveCodec::decode(oldBytes,world,legacy,output,error));
    std::vector<std::byte> roundtrip;ASSERT_TRUE(AdventureSaveCodec::encode(output,legacy,roundtrip,error));EXPECT_EQ(roundtrip,oldBytes);
    auto bad=oldSession->state();bad.backpack[3]={ItemKind::QuarryHammer,1};EXPECT_FALSE(AdventureSession::restore(bad,legacy,error));
}
TEST_F(FrontierSessionTest, OuterBeaconRequiresQuarryOutputAndNeverRepeatsFirstUnlockGrant) {
    content.frontierSites.push_back({40,{50,12,0,0},FrontierSiteKind::Beacon,{}});
    session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    const auto station=bench();ASSERT_NE(station,0u);
    ASSERT_TRUE(commit(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error)));
    const auto first=session->state();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),40,station,accepted,error));EXPECT_EQ(session->state(),first);
    auto stocked=first;stocked.backpack[0]={ItemKind::Wood,100};stocked.backpack[1]={ItemKind::Stone,100};
    stocked.backpack[2]={ItemKind::Scrap,100};stocked.backpack[3]={ItemKind::CutStone,4};
    session=AdventureSession::restore(stocked,content,error);ASSERT_TRUE(session)<<error;
    ASSERT_TRUE(commit(session->prepareFrontierRestoreBeacon(next(),40,station,accepted,error)));
    EXPECT_EQ(session->state().frontier.quarryUnlockRevision,first.frontier.quarryUnlockRevision);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::CutStone),0u);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),88u);roundtrip();
}
TEST_F(FrontierSessionTest, UndoRemovalPaysCurrentInventoryAndRestoresStableIdsWithoutRewindingGather) {
    const auto station=bench();const auto* component=AdventureSession::findComponent(session->state(),station);ASSERT_TRUE(component);
    RemovedPartSnapshot snapshot{*AdventureSession::findPart(session->state(),component->part),component->structure,
        session->state().structures.front().origin,*component};
    ASSERT_TRUE(commit(session->prepareRemove(next(),snapshot.part.id,accepted,error)));
    ASSERT_TRUE(commit(session->prepareGather(next(),1,accepted,error)));
    const auto gathered=session->state();
    EXPECT_FALSE(session->prepareRestorePart(next(),snapshot,blocked,error));EXPECT_EQ(session->state(),gathered);
    ASSERT_TRUE(commit(session->prepareRestorePart(next(),snapshot,accepted,error)));
    EXPECT_EQ(AdventureSession::findPart(session->state(),snapshot.part.id)->kind,snapshot.part.kind);
    EXPECT_EQ(AdventureSession::findComponent(session->state(),station)->part,snapshot.part.id);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),itemCount(gathered.backpack,ItemKind::Wood)-8);
    EXPECT_EQ(session->state().depletedNodes,gathered.depletedNodes);
    const auto restored=session->state();EXPECT_FALSE(session->prepareRestorePart(next(),snapshot,accepted,error));
    EXPECT_EQ(session->state(),restored);roundtrip();
}
TEST_F(FrontierSessionTest, UndoRemovalRefusesSpentRefundAndForgedStoredContentsAtomically) {
    const auto station=bench();const auto* component=AdventureSession::findComponent(session->state(),station);ASSERT_TRUE(component);
    RemovedPartSnapshot snapshot{*AdventureSession::findPart(session->state(),component->part),component->structure,
        session->state().structures.front().origin,*component};
    ASSERT_TRUE(commit(session->prepareRemove(next(),snapshot.part.id,accepted,error)));
    auto spent=session->state();spent.backpack={};session=AdventureSession::restore(spent,content,error);ASSERT_TRUE(session)<<error;
    EXPECT_FALSE(session->prepareRestorePart(next(),snapshot,accepted,error));EXPECT_EQ(session->state(),spent);
    snapshot.component->slots[0]={ItemKind::Wood,1};
    EXPECT_FALSE(session->prepareRestorePart(next(),snapshot,accepted,error));EXPECT_EQ(session->state(),spent);
}
TEST_F(FrontierSessionTest, MoveAndRepaintPreserveFurnitureContentsAndNeverRefundInventory) {
    const auto station=bench();const auto structure=session->state().structures.front().id;
    ASSERT_TRUE(commit(session->preparePlace(next(),{structure,PieceKind::Chest,{100,616,0},0,0},accepted,error)));
    const auto chest=session->state().components.back().id;const auto part=session->state().components.back().part;
    const auto& state=session->state();
    ASSERT_TRUE(commit(session->prepareTransfer(next(),{0,chest,state.backpackRevision,state.components.back().revision,0,7},accepted,error)));
    const auto before=session->state();
    EXPECT_FALSE(session->prepareMovePart(next(),part,{200,616,0},1,blocked,error));EXPECT_EQ(session->state(),before);
    ASSERT_TRUE(commit(session->prepareMovePart(next(),part,{200,616,0},1,accepted,error)));
    ASSERT_TRUE(commit(session->prepareRepaint(next(),part,0xaaccee,accepted,error)));
    EXPECT_EQ(session->state().backpack,before.backpack);EXPECT_EQ(session->state().backpackRevision,before.backpackRevision);
    EXPECT_EQ(AdventureSession::findComponent(session->state(),chest)->slots,AdventureSession::findComponent(before,chest)->slots);
    EXPECT_EQ(AdventureSession::findPart(session->state(),part)->paint,0xaacceeu);
    EXPECT_NE(AdventureSession::findComponent(session->state(),station),nullptr);roundtrip();
}
TEST_F(FrontierSessionTest, ScaledKitIsPaidPersistentAndCannotEnterLegacyProfiles) {
    ASSERT_TRUE(commit(session->preparePlace(next(),{0,PieceKind::FrontierFoundation,{0,600,0},0,0},accepted,error)));
    const auto structure=session->state().structures.front().id;
    ASSERT_TRUE(commit(session->preparePlace(next(),{structure,PieceKind::FrontierWorkbench,{0,616,0},0,0},accepted,error)));
    EXPECT_EQ(session->state().components.front().kind,FurnitureKind::Workbench);
    EXPECT_LT(itemCount(session->state().backpack,ItemKind::Wood),48u);roundtrip();
    auto legacy=content;legacy.frontier=false;legacy.frontierEnemies.clear();legacy.frontierSites.clear();legacy.resourceNodes.clear();
    auto borrowed=session->state();borrowed.frontier={};
    EXPECT_FALSE(AdventureSession::restore(borrowed,legacy,error));
    auto old=AdventureSession::create(world,legacy,error);ASSERT_TRUE(old)<<error;
    EXPECT_FALSE(old->preparePlace({0,1,1},{0,PieceKind::FrontierFoundation,{0,600,0},0,0},accepted,error));
}
TEST_F(FrontierSessionTest, FrontierExtensionRejectsCorruptionWithoutPublishing) {
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(session->state(),content,bytes,error));
    AdventureState output=session->state();const auto original=output;
    bytes[bytes.size()-33]^=std::byte{1};
    EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,content,output,error));EXPECT_EQ(output,original);
    // Even a recomputed checksum cannot make an impossible receipt authoritative.
    const auto digest=core::sha256(std::span<const std::byte>(bytes).first(bytes.size()-32));
    std::copy(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32);
    EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,content,output,error));EXPECT_EQ(output,original);
}
TEST_F(FrontierSessionTest, BeaconWardensMustBothFallBeforeAnyMaterialsAreSpent) {
    content.frontierSites[1].defenders={31,79};
    session=AdventureSession::restore(session->state(),content,error);ASSERT_TRUE(session)<<error;
    const auto station=bench();const auto before=session->state();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error));
    EXPECT_NE(error.find("2 remaining"),std::string::npos);EXPECT_EQ(session->state(),before);
    ASSERT_TRUE(commit(session->prepareFrontierEnemyHit(next(),31,60,{20,12,0,0},accepted,error)));roundtrip();
    const auto oneDown=session->state();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error));
    EXPECT_NE(error.find("1 remaining"),std::string::npos);EXPECT_EQ(session->state(),oneDown);
    ASSERT_TRUE(commit(session->prepareFrontierEnemyHit(next(),79,80,{30,12,0,0},accepted,error)));roundtrip();
    ASSERT_TRUE(commit(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error)));roundtrip();
    EXPECT_EQ(frontierRemainingDefenders(session->state(),content.frontierSites[1]),0u);
    EXPECT_EQ(frontierEnemy(session->state(),31)->lootClaimRevision,0u); // Loot is a choice, not a hidden gate.
}
TEST_F(FrontierSessionTest, LastSanctuaryRequiresAnEarnedQuarryHarvestNotMerelyVisiting) {
    content.frontierSites.push_back({40,{50,12,0,0},FrontierSiteKind::Beacon,{}, {},true});
    auto fresh=session->state();initializeFrontierProgress(fresh,content);
    session=AdventureSession::restore(fresh,content,error);ASSERT_TRUE(session)<<error;
    const auto station=bench();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),40,station,accepted,error));
    ASSERT_TRUE(commit(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error)));
    ASSERT_TRUE(commit(session->prepareFrontierDiscover(next(),30,accepted,error)));
    EXPECT_FALSE(frontierHasQuarryHarvest(session->state(),content));
    const auto before=session->state();
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),40,station,accepted,error));EXPECT_EQ(session->state(),before);
    ASSERT_TRUE(commit(session->prepareCraftQuarryHammer(next(),station,accepted,error)));
    uint8_t slot=0;while(slot<kBackpackSlots&&session->state().backpack[slot].kind!=ItemKind::QuarryHammer)++slot;
    ASSERT_LT(slot,kBackpackSlots);ASSERT_TRUE(commit(session->prepareEquipTool(next(),slot,error)));
    ASSERT_TRUE(commit(session->prepareGather(next(),2,accepted,error)));roundtrip();
    EXPECT_TRUE(frontierHasQuarryHarvest(session->state(),content));
    EXPECT_TRUE(frontierBeaconRequirements(session->state(),content,content.frontierSites.back(),error));
    // Satisfying the distinct task does not bypass the existing actual costs.
    EXPECT_FALSE(session->prepareFrontierRestoreBeacon(next(),40,station,accepted,error));
}
TEST_F(FrontierSessionTest, ExistingRestoredBeaconRemainsValidWhenInstalledDefendersAreAdded) {
    const auto station=bench();ASSERT_TRUE(commit(session->prepareFrontierRestoreBeacon(next(),20,station,accepted,error)));
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(session->state(),content,bytes,error));
    content.frontierSites[1].defenders={31,79};
    AdventureState decoded;ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error))<<error;
    session=AdventureSession::restore(decoded,content,error);ASSERT_TRUE(session)<<error;
    EXPECT_NE(frontierSite(session->state(),20)->restoredRevision,0u);
    EXPECT_GT(frontierEnemy(session->state(),31)->health,0u);roundtrip();
}
TEST_F(FrontierSessionTest, KeeperRestRequiresLiveApprovalAndPreservesPoseInventoryAndProgress) {
    ASSERT_TRUE(commit(session->prepareFrontierPlayerHit(next(),45,accepted,error)));
    const auto injured=session->state();
    EXPECT_FALSE(session->prepareFrontierRest(next(),blocked,error));EXPECT_EQ(session->state(),injured);
    EXPECT_FALSE(session->prepareFrontierRest(next(),{},error));EXPECT_EQ(session->state(),injured);
    ASSERT_TRUE(commit(session->prepareFrontierRest(next(),accepted,error)));
    EXPECT_EQ(session->state().health,100);EXPECT_EQ(session->state().player,injured.player);
    EXPECT_EQ(session->state().backpack,injured.backpack);EXPECT_EQ(session->state().frontier,injured.frontier);roundtrip();
    EXPECT_FALSE(session->prepareFrontierRest(next(),accepted,error));
    ASSERT_TRUE(commit(session->prepareFrontierPlayerHit(next(),100,accepted,error)));
    EXPECT_FALSE(session->prepareFrontierRest(next(),accepted,error));
}
TEST_F(FrontierSessionTest, InstalledEncounterRequirementsRejectUnknownOrRepeatedDefenders) {
    auto invalid=content;invalid.frontierSites[1].defenders={31,31};EXPECT_FALSE(validFrontierContent(invalid));
    invalid=content;invalid.frontierSites[1].defenders={32};EXPECT_FALSE(validFrontierContent(invalid));
    invalid=content;invalid.frontierSites[0].defenders={31};EXPECT_FALSE(validFrontierContent(invalid));
    invalid=content;invalid.frontierEnemies[0].archetype=static_cast<FrontierEnemyArchetype>(2);EXPECT_FALSE(validFrontierContent(invalid));
}
} // namespace
} // namespace voxy::game::adventure
