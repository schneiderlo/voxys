#include "game/adventure/adventure_save.hpp"
#include "game/adventure/construction_policy.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>

namespace voxy::game::adventure {
namespace {
const CandidateValidator validWorld=[](const AdventureState&,const AdventureState&,std::string&){return true;};
class AdventureSessionTest:public testing::Test {
protected:
    construction::WorldNamespace world{{'a','d','v','e','n','t','u','r','e','-','t','e','s','t','0','1'}};
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    void SetUp() override {
        content.identity.bytes[0]=std::byte{53};content.town={0,12,0,0};
        content.resourceNodes={{1,{4,12,0,0},{ItemKind::Wood,8}},{2,{6,12,0,0},{ItemKind::Stone,8}}};
        session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    }
    CommandStamp next() const {return {session->state().revision,session->state().lastRequestSequence+1,1};}
    uint64_t place(PieceKind kind,uint64_t structure=0,GridPosition position={0,600,0}) {
        auto change=session->preparePlace(next(),{structure,kind,position,0,0},validWorld,error);
        if(!change){ADD_FAILURE()<<error;return 0;}
        const auto id=change->changedPart();EXPECT_TRUE(session->commit(std::move(*change),error))<<error;return id;
    }
    uint64_t firstStructure()const{return session->state().structures.front().id;}
    uint64_t furniture(PieceKind kind) {
        if(session->state().structures.empty())place(PieceKind::Foundation);
        const auto part=place(kind,firstStructure());
        for(const auto& c:session->state().components)if(c.part==part)return c.id;
        ADD_FAILURE()<<"Missing component";return 0;
    }
    void reload(const AdventureState& state) {
        session=AdventureSession::restore(state,content,error);ASSERT_TRUE(session)<<error;
    }
    void transfer(uint64_t source,uint64_t destination,uint8_t slot,uint16_t quantity) {
        const auto revision=[&](uint64_t id){return id?AdventureSession::findComponent(session->state(),id)->revision:session->state().backpackRevision;};
        auto change=session->prepareTransfer(next(),{source,destination,revision(source),revision(destination),slot,quantity},validWorld,error);
        ASSERT_TRUE(change)<<error;ASSERT_TRUE(session->commit(std::move(*change),error))<<error;
    }
};
TEST(AdventureInventoryTest, FailedAdditionAndMaterialRefundAreAtomic) {
    std::array<ItemStack,2> slots{{{ItemKind::Wood,998},{ItemKind::Stone,999}}};
    const auto original=slots;
    EXPECT_FALSE(addItems(slots,{ItemKind::Wood,2}));EXPECT_EQ(slots,original);
    EXPECT_FALSE(refundMaterials(slots,{1,1,0}));EXPECT_EQ(slots,original);
    EXPECT_TRUE(addItems(slots,{ItemKind::Wood,1}));EXPECT_EQ(slots[0].quantity,999);
    EXPECT_FALSE(takeItems(slots,{ItemKind::Scrap,1}));EXPECT_EQ(slots[1].quantity,999);
}
TEST(AdventureInventoryTest, StackSplittingMergingAndBounds) {
    std::array<ItemStack,3> slots{{{ItemKind::Wood,998},{},{}}};
    EXPECT_TRUE(addItems(slots,{ItemKind::Wood,5}));EXPECT_EQ(slots[0].quantity,999);EXPECT_EQ(slots[1],(ItemStack{ItemKind::Wood,4}));
    EXPECT_TRUE(takeItems(slots,{ItemKind::Wood,999}));EXPECT_EQ(slots[0],ItemStack{});EXPECT_EQ(slots[1].quantity,4);
    EXPECT_FALSE(addItems(slots,{ItemKind::FieldHammer,2}));EXPECT_FALSE(validStack({ItemKind::None,1}));
}
TEST_F(AdventureSessionTest, StarterSuppliesCannotBeReclaimedOnRestore) {
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),640u);
    place(PieceKind::Foundation);const auto expected=session->state();reload(expected);
    EXPECT_EQ(session->state(),expected);EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone),316u);
    auto invalid=expected;invalid.starterGranted=false;EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
}
TEST_F(AdventureSessionTest, GeometryRefusalAndCancelledPreparationDoNotSpendOrIssueIds) {
    const auto before=session->state();
    const CandidateValidator blocked=[](const AdventureState&,const AdventureState&,std::string& message){message="Keep the town path clear.";return false;};
    EXPECT_FALSE(session->preparePlace(next(),{0,PieceKind::Foundation,{0,600,0},0,0},blocked,error));
    EXPECT_EQ(error,"Keep the town path clear.");EXPECT_EQ(session->state(),before);
    auto candidate=session->preparePlace(next(),{0,PieceKind::Foundation,{0,600,0},0,0},validWorld,error);ASSERT_TRUE(candidate);
    EXPECT_EQ(session->state(),before);candidate.reset();EXPECT_EQ(session->state(),before);
    EXPECT_FALSE(session->preparePlace(next(),{0,PieceKind::Foundation,{0,600,0},0,0},{},error));
    EXPECT_FALSE(session->preparePlace(next(),{0,PieceKind::Bed,{0,600,0},0,0},validWorld,error));
}
TEST_F(AdventureSessionTest, StaleDuplicateAndForeignPreparedChangesCannotPublish) {
    const auto stamp=next();auto first=session->preparePlace(stamp,{0,PieceKind::Foundation,{0,600,0},0,0},validWorld,error);
    auto second=session->preparePlace(stamp,{0,PieceKind::Foundation,{100,600,0},0,0},validWorld,error);
    ASSERT_TRUE(first);ASSERT_TRUE(second);ASSERT_TRUE(session->commit(std::move(*first),error));
    const auto accepted=session->state();EXPECT_FALSE(session->commit(std::move(*second),error));EXPECT_EQ(session->state(),accepted);
    EXPECT_FALSE(session->preparePlace({accepted.revision,stamp.sequence,1},{firstStructure(),PieceKind::Floor,{},0,0},validWorld,error));
    auto owner=AdventureSession::restore(accepted,content,error);ASSERT_TRUE(owner);
    auto candidate=owner->preparePlace({accepted.revision,stamp.sequence+1,1},{0,PieceKind::Foundation,{},0,0},validWorld,error);ASSERT_TRUE(candidate);
    EXPECT_FALSE(session->commit(std::move(*candidate),error));EXPECT_EQ(session->state(),accepted);
}
TEST_F(AdventureSessionTest, MovementInvalidatesPendingEditAndRejectsNonFinitePose) {
    auto candidate=session->preparePlace(next(),{0,PieceKind::Foundation,{},0,0},validWorld,error);ASSERT_TRUE(candidate);
    ASSERT_TRUE(session->updatePlayer({1.12345,12,2.23456,.2},80,error));const auto moved=session->state();
    EXPECT_FALSE(session->commit(std::move(*candidate),error));EXPECT_EQ(session->state(),moved);
    EXPECT_FALSE(session->updatePlayer({std::numeric_limits<double>::quiet_NaN(),0,0,0},100,error));EXPECT_EQ(session->state(),moved);
}
TEST_F(AdventureSessionTest, ChestCreationTransferAndNonemptyRemovalShareOwnership) {
    const auto chest=furniture(PieceKind::Chest);const auto* c=AdventureSession::findComponent(session->state(),chest);ASSERT_TRUE(c);
    const auto part=c->part;transfer(0,chest,0,17);
    EXPECT_EQ(itemCount(AdventureSession::findComponent(session->state(),chest)->slots,ItemKind::Wood),17u);
    const auto filled=session->state();EXPECT_FALSE(session->prepareRemove(next(),part,validWorld,error));EXPECT_EQ(session->state(),filled);
    EXPECT_EQ(error,"Empty the chest before removing it.");transfer(chest,0,0,17);
    const auto wood=itemCount(session->state().backpack,ItemKind::Wood);
    auto remove=session->prepareRemove(next(),part,validWorld,error);ASSERT_TRUE(remove);ASSERT_TRUE(session->commit(std::move(*remove),error));
    EXPECT_FALSE(AdventureSession::findComponent(session->state(),chest));EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),wood+6);
}
TEST_F(AdventureSessionTest, FullAndStaleChestTransfersPreserveBothContainers) {
    const auto chest=furniture(PieceKind::Chest);auto state=session->state();
    for(auto& c:state.components)if(c.id==chest)c.slots.fill({ItemKind::Stone,999});
    reload(state);
    auto c=AdventureSession::findComponent(session->state(),chest);ASSERT_TRUE(c);
    const auto before=session->state();
    EXPECT_FALSE(session->prepareTransfer(next(),{0,chest,before.backpackRevision,c->revision,0,3},validWorld,error));EXPECT_EQ(session->state(),before);
    EXPECT_FALSE(session->prepareTransfer(next(),{0,chest,before.backpackRevision+1,c->revision,0,3},validWorld,error));EXPECT_EQ(session->state(),before);
}
TEST_F(AdventureSessionTest, FullBackpackCraftAndRemoveRefusalsDoNotLoseInputs) {
    const auto bench=furniture(PieceKind::Workbench);auto state=session->state();
    state.backpack.fill({ItemKind::Stone,999});state.backpack[0]={ItemKind::Wood,999};state.backpack[1]={ItemKind::Scrap,999};reload(state);
    const auto before=session->state();EXPECT_FALSE(session->prepareCraftHammer(next(),bench,validWorld,error));EXPECT_EQ(session->state(),before);
    const auto part=AdventureSession::findComponent(before,bench)->part;
    EXPECT_FALSE(session->prepareRemove(next(),part,validWorld,error));EXPECT_EQ(session->state(),before);
}
TEST_F(AdventureSessionTest, BenchCraftEquipAndGatherGiveOnePersistentUsefulYield) {
    const auto bench=furniture(PieceKind::Workbench);const auto wood=itemCount(session->state().backpack,ItemKind::Wood);
    auto craft=session->prepareCraftHammer(next(),bench,validWorld,error);ASSERT_TRUE(craft);ASSERT_TRUE(session->commit(std::move(*craft),error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),wood-4);
    size_t slot=0;for(;slot<kBackpackSlots;++slot)if(session->state().backpack[slot].kind==ItemKind::FieldHammer)break;
    auto equip=session->prepareEquipTool(next(),static_cast<uint8_t>(slot),error);ASSERT_TRUE(equip);ASSERT_TRUE(session->commit(std::move(*equip),error));
    auto gather=session->prepareGather(next(),1,validWorld,error);ASSERT_TRUE(gather);ASSERT_TRUE(session->commit(std::move(*gather),error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),wood-4+16);
    const auto expected=session->state();reload(expected);
    EXPECT_FALSE(session->prepareGather(next(),1,validWorld,error));EXPECT_EQ(session->state(),expected);
}
TEST_F(AdventureSessionTest, ShelteredBedRestAndRemovalResolveRecoverySafely) {
    const auto bed=furniture(PieceKind::Bed);ASSERT_TRUE(session->updatePlayer({0,12,0,0},27,error));
    const CandidateValidator roofMissing=[](const AdventureState&,const AdventureState&,std::string& e){e="Build a roof over the bed.";return false;};
    EXPECT_FALSE(session->prepareUseBed(next(),bed,{1,12,0,0},roofMissing,error));EXPECT_EQ(session->state().health,27);
    auto rest=session->prepareUseBed(next(),bed,{1,12,0,0},validWorld,error);ASSERT_TRUE(rest);ASSERT_TRUE(session->commit(std::move(*rest),error));
    EXPECT_EQ(session->state().registeredBed,bed);EXPECT_EQ(session->state().health,100);
    const auto part=AdventureSession::findComponent(session->state(),bed)->part;
    auto remove=session->prepareRemove(next(),part,validWorld,error);ASSERT_TRUE(remove);ASSERT_TRUE(session->commit(std::move(*remove),error));
    EXPECT_EQ(session->state().registeredBed,0u);EXPECT_EQ(session->state().recovery,content.town);
}
TEST_F(AdventureSessionTest, FourStructuresAnd1024PartsAreBoundedWithoutPartialDebit) {
    auto seed=session->state();seed.backpack.fill({ItemKind::Stone,999});reload(seed);
    for(int structure=0;structure<4;++structure) {
        place(PieceKind::Foundation);const auto id=session->state().structures.back().id;
        for(int i=1;i<256;++i)place(PieceKind::Brick1x2,id,{i*100,600,structure*100});
    }
    ASSERT_EQ(session->state().structures.size(),4u);
    for(const auto& s:session->state().structures)EXPECT_EQ(s.parts.size(),256u);
    const auto full=session->state();
    EXPECT_FALSE(session->preparePlace(next(),{firstStructure(),PieceKind::Brick1x2,{},0,0},validWorld,error));EXPECT_EQ(session->state(),full);
    EXPECT_FALSE(session->preparePlace(next(),{0,PieceKind::Foundation,{},0,0},validWorld,error));EXPECT_EQ(session->state(),full);
}
TEST_F(AdventureSessionTest, BlueprintPublishesWholeLayoutWithOneRevisionAndStableFurniture) {
    const std::array<PlacePart,7> layout{{
        {0,PieceKind::Foundation,{0,600,0},0,0},
        {0,PieceKind::Floor,{0,648,0},0,0},
        {0,PieceKind::Wall,{0,664,100},0,0},
        {0,PieceKind::Roof,{0,808,0},0,0},
        {0,PieceKind::Bed,{-50,664,0},0,0},
        {0,PieceKind::Chest,{50,664,0},0,0},
        {0,PieceKind::Workbench,{0,664,-50},0,0}
    }};
    size_t checks=0;
    const CandidateValidator complete=[&](const AdventureState& before,const AdventureState& after,std::string&){
        ++checks;EXPECT_TRUE(before.structures.empty());EXPECT_EQ(after.structures.size(),1u);
        EXPECT_EQ(after.structures.front().parts.size(),layout.size());EXPECT_EQ(after.components.size(),3u);
        return true;
    };
    const auto original=session->state();const auto stamp=next();
    auto change=session->prepareBlueprint(stamp,layout,complete,error);ASSERT_TRUE(change)<<error;
    EXPECT_EQ(checks,1u);EXPECT_EQ(session->state(),original);
    EXPECT_EQ(change->state().revision,original.revision+1);EXPECT_EQ(change->state().lastRequestSequence,stamp.sequence);
    ASSERT_TRUE(session->commit(std::move(*change),error))<<error;
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),606u);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone),316u);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Scrap),72u);
    EXPECT_EQ(session->state().lastIssuedId,13u);
    for(const auto& component:session->state().components) {
        EXPECT_EQ(component.structure,firstStructure());EXPECT_TRUE(AdventureSession::findPart(session->state(),component.part));
        EXPECT_EQ(component.revision,session->state().revision);
    }
    const std::array<PlacePart,2> extension{{
        {firstStructure(),PieceKind::Foundation,{200,600,0},0,0},
        {0,PieceKind::Floor,{200,648,0},0,0}
    }};
    auto addition=session->prepareBlueprint(next(),extension,validWorld,error);ASSERT_TRUE(addition)<<error;
    EXPECT_EQ(addition->state().structures.size(),1u);EXPECT_EQ(addition->state().structures.front().parts.size(),9u);
    ASSERT_TRUE(session->commit(std::move(*addition),error));EXPECT_EQ(session->state().revision,2u);
}
TEST_F(AdventureSessionTest, BlueprintLastPieceAndWholeCandidateRefusalsLeaveAllOwnershipUnchanged) {
    const std::array<PlacePart,5> layout{{
        {0,PieceKind::Foundation,{0,600,0},0,0},
        {0,PieceKind::Bed,{-50,648,0},0,0},
        {0,PieceKind::Chest,{50,648,0},0,0},
        {0,PieceKind::Workbench,{0,648,-50},0,0},
        {0,PieceKind::Roof,{0,792,0},0,0}
    }};
    auto stock=session->state();stock.backpack[0].quantity=22;reload(stock);
    size_t checks=0;
    const CandidateValidator rejected=[&](const AdventureState&,const AdventureState& after,std::string& message){
        ++checks;EXPECT_EQ(after.structures.front().parts.size(),layout.size());EXPECT_EQ(after.components.size(),3u);
        message="The final roof crosses a protected route.";return false;
    };
    EXPECT_FALSE(session->prepareBlueprint(next(),layout,rejected,error));
    EXPECT_EQ(error,"Not enough supplies for this piece.");EXPECT_EQ(checks,0u);EXPECT_EQ(session->state(),stock);
    stock.backpack[0].quantity=26;reload(stock);
    EXPECT_FALSE(session->prepareBlueprint(next(),layout,rejected,error));
    EXPECT_EQ(error,"The final roof crosses a protected route.");EXPECT_EQ(checks,1u);EXPECT_EQ(session->state(),stock);
    EXPECT_FALSE(session->prepareBlueprint(next(),{},validWorld,error));EXPECT_EQ(session->state(),stock);
    std::array<PlacePart,kMaximumBlueprintParts+1> oversized{};oversized.fill(layout.front());
    EXPECT_FALSE(session->prepareBlueprint(next(),oversized,validWorld,error));EXPECT_EQ(session->state(),stock);
    auto mixed=layout;mixed.back().structure=999;
    EXPECT_FALSE(session->prepareBlueprint(next(),mixed,validWorld,error));EXPECT_EQ(session->state(),stock);
    auto badRotation=layout;badRotation.back().yawQuarterTurns=4;
    EXPECT_FALSE(session->prepareBlueprint(next(),badRotation,validWorld,error));EXPECT_EQ(session->state(),stock);
}
TEST_F(AdventureSessionTest, WholeStructureUndoRefusesFilledChestThenRefundsAndRemovesAtomically) {
    const auto bed=furniture(PieceKind::Bed),chest=furniture(PieceKind::Chest);
    const auto bench=furniture(PieceKind::Workbench);(void)bench;
    const auto structure=firstStructure();
    for(int i=0;i<15;++i)place(PieceKind::Floor,structure,{i*100,648,0});
    ASSERT_EQ(session->state().structures.front().parts.size(),19u);
    auto rest=session->prepareUseBed(next(),bed,{1,12,0,0},validWorld,error);ASSERT_TRUE(rest);
    ASSERT_TRUE(session->commit(std::move(*rest),error));transfer(0,chest,0,17);
    const auto filled=session->state();size_t checks=0;
    const CandidateValidator complete=[&](const AdventureState&,const AdventureState& after,std::string&){
        ++checks;EXPECT_TRUE(after.structures.empty());EXPECT_TRUE(after.components.empty());
        EXPECT_EQ(after.registeredBed,0u);EXPECT_EQ(after.recovery,content.town);return true;
    };
    EXPECT_FALSE(session->prepareRemoveStructure(next(),structure,complete,error));
    EXPECT_EQ(error,"Empty every chest before removing this structure.");EXPECT_EQ(checks,0u);EXPECT_EQ(session->state(),filled);
    transfer(chest,0,0,17);const auto before=session->state();const auto stamp=next();
    auto undo=session->prepareRemoveStructure(stamp,structure,complete,error);ASSERT_TRUE(undo)<<error;
    EXPECT_EQ(checks,1u);EXPECT_EQ(session->state(),before);EXPECT_EQ(undo->changedStructure(),structure);
    ASSERT_TRUE(session->commit(std::move(*undo),error))<<error;
    EXPECT_TRUE(session->state().structures.empty());EXPECT_TRUE(session->state().components.empty());
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),640u);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone),320u);
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Scrap),80u);
    EXPECT_EQ(session->state().registeredBed,0u);EXPECT_EQ(session->state().recovery,content.town);
    EXPECT_EQ(session->state().revision,before.revision+1);EXPECT_EQ(session->state().lastRequestSequence,stamp.sequence);
    EXPECT_EQ(session->state().lastIssuedId,before.lastIssuedId);
    const auto removed=session->state();EXPECT_FALSE(session->prepareRemoveStructure(next(),structure,validWorld,error));EXPECT_EQ(session->state(),removed);
}
TEST_F(AdventureSessionTest, CapacityWorldKeepsFourUsableHousesAnd32ComponentsAcrossExactSave) {
    // Engineering fixture: four actual starter rooms with connected foundation
    // yards and extra storage. Expanded already-owned supplies model a later
    // saved world; this does not grant materials in a player journey.
    std::vector<uint16_t> samples(128*128,32768);
    voxy::terrain::lego::Surface terrain{samples,128,128,8.f,1.f};
    AdventureSpatialQueries geometry;ASSERT_TRUE(geometry.bindTerrain(terrain));ASSERT_TRUE(geometry.publish({},1));
    auto state=session->state();state.player={0,.185,0,0};state.revision=1;state.backpackRevision=1;state.lastRequestSequence=1;
    for(size_t i=0;i<state.backpack.size();++i)state.backpack[i]={i<16?ItemKind::Stone:i<22?ItemKind::Wood:ItemKind::Scrap,999};
    const auto initialWood=itemCount(state.backpack,ItemKind::Wood),initialStone=itemCount(state.backpack,ItemKind::Stone),initialScrap=itemCount(state.backpack,ItemKind::Scrap);
    uint32_t spentWood=0,spentStone=0,spentScrap=0;
    const std::array<GridPosition,4> sites{{{-1600,0,-1600},{1200,0,-1600},{-1600,0,1200},{1200,0,1200}}};
    for(size_t site=0;site<sites.size();++site) {
        const auto structureId=++state.lastIssuedId;
        state.structures.push_back({structureId,1,1,sites[site],{}});
        const auto add=[&](PlacePart request) {
            const auto* definition=buildingDefinition(request.kind);ASSERT_TRUE(definition);
            ASSERT_TRUE(consumeMaterials(state.backpack,definition->cost));
            spentWood+=definition->cost.wood;spentStone+=definition->cost.stone;spentScrap+=definition->cost.scrap;
            const auto partId=++state.lastIssuedId;
            state.structures.back().parts.push_back({partId,request.kind,request.position,request.yawQuarterTurns,request.paint});
            if(definition->furniture!=FurnitureKind::None)
                state.components.push_back({++state.lastIssuedId,structureId,partId,1,1,definition->furniture,{}});
        };
        const auto room=starterRoomLayout(sites[site],static_cast<uint8_t>(site),error);ASSERT_EQ(room.size(),19u)<<error;
        for(const auto& part:room)add(part);
        // A32m-wide yard starts at the room's edge. Every foundation has real
        // terrain contact, and the five added chests stand on its first row.
        for(int index=0;index<232;++index)
            add({structureId,PieceKind::Foundation,{sites[site].x-750+(index%16)*100,0,sites[site].z+150+(index/16)*100},0,0});
        for(int index=0;index<5;++index)
            add({structureId,PieceKind::Chest,{sites[site].x-750+index*100,16,sites[site].z+150},0,0});
        ASSERT_EQ(state.structures.back().parts.size(),256u);
    }
    ASSERT_EQ(state.structures.size(),4u);ASSERT_EQ(state.components.size(),32u);
    uint32_t storedWood=0,storedScrap=0;
    for(auto& component:state.components)if(component.kind==FurnitureKind::Chest) {
        ASSERT_TRUE(takeItems(state.backpack,{ItemKind::Wood,17}));ASSERT_TRUE(takeItems(state.backpack,{ItemKind::Scrap,1}));
        component.slots[0]={ItemKind::Wood,17};component.slots[31]={ItemKind::Scrap,1};storedWood+=17;storedScrap+=1;
    }
    EXPECT_EQ(itemCount(state.backpack,ItemKind::Wood)+storedWood+spentWood,initialWood);
    EXPECT_EQ(itemCount(state.backpack,ItemKind::Stone)+spentStone,initialStone);
    EXPECT_EQ(itemCount(state.backpack,ItemKind::Scrap)+storedScrap+spentScrap,initialScrap);
    ASSERT_TRUE(AdventureSession::validate(state,content,error))<<error;
    ASSERT_TRUE(validateInstalledGeometry(state,geometry,error))<<error;
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(compileSolids(state,solids,error))<<error;
    ASSERT_TRUE(geometry.publish(solids,2));EXPECT_EQ(solids.size(),1068u);
    size_t usableHomes=0;
    for(const auto& component:state.components)if(component.kind==FurnitureKind::Bed) {
        PlayerPose recovery;ASSERT_TRUE(usableBed(state,component.id,geometry,recovery,error))<<error;
        EXPECT_TRUE(geometry.clearCapsule({recovery.x,recovery.y,recovery.z}));++usableHomes;
        if(!state.registeredBed){state.registeredBed=component.id;state.recovery=recovery;}
    }
    EXPECT_EQ(usableHomes,4u);
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(state,content,bytes,error))<<error;
    AdventureState restored;ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,restored,error))<<error;EXPECT_EQ(restored,state);
    std::vector<std::byte> repeated;ASSERT_TRUE(AdventureSaveCodec::encode(restored,content,repeated,error));EXPECT_EQ(repeated,bytes);
    ASSERT_TRUE(validateInstalledGeometry(restored,geometry,error))<<error;reload(restored);
    const auto full=session->state();EXPECT_FALSE(session->preparePlace(next(),{firstStructure(),PieceKind::Floor,{0,16,0},0,0},validWorld,error));EXPECT_EQ(session->state(),full);
    auto overComponents=full;overComponents.components.push_back({});EXPECT_FALSE(AdventureSession::validate(overComponents,content,error));EXPECT_EQ(error,"Adventure capacity exceeded.");
    RecordProperty("structures",4);RecordProperty("parts",1024);RecordProperty("components",32);RecordProperty("usable_homes",4);
    RecordProperty("collision_solids",std::to_string(solids.size()));RecordProperty("snapshot_bytes",std::to_string(bytes.size()));
}
TEST_F(AdventureSessionTest, CodecPreservesHomePossessionsAndLosslessCounters) {
    const auto chest=furniture(PieceKind::Chest);const auto bed=furniture(PieceKind::Bed);transfer(0,chest,0,13);
    auto rest=session->prepareUseBed(next(),bed,{1.23456789,12,2.3456789,.3},validWorld,error);ASSERT_TRUE(rest);ASSERT_TRUE(session->commit(std::move(*rest),error));
    auto state=session->state();state.lastRequestSequence=9007199254741003ull;state.revision=9007199254741005ull;state.lastIssuedId=9007199254741011ull;reload(state);
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(session->state(),content,bytes,error))<<error;
    AdventureState decoded;ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,decoded,error))<<error;EXPECT_EQ(decoded,state);
    std::vector<std::byte> again;ASSERT_TRUE(AdventureSaveCodec::encode(decoded,content,again,error));EXPECT_EQ(bytes,again);
    reload(decoded);EXPECT_EQ(session->state(),state);
}
TEST_F(AdventureSessionTest, CodecRejectsCorruptionForeignIdentityTrailingAndWrongSchemaAtomically) {
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(session->state(),content,bytes,error));
    const auto before=session->state();AdventureState decoded=before;
    auto corrupt=bytes;corrupt[40]^=std::byte{1};EXPECT_FALSE(AdventureSaveCodec::decode(corrupt,world,content,decoded,error));EXPECT_EQ(decoded,before);
    auto foreign=world;foreign.bytes[0]^=1;EXPECT_FALSE(AdventureSaveCodec::decode(bytes,foreign,content,decoded,error));EXPECT_EQ(decoded,before);
    auto foreignContent=content;foreignContent.identity.bytes[0]^=std::byte{1};EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,foreignContent,decoded,error));
    bytes.insert(bytes.end()-32,std::byte{0});auto digest=core::sha256(std::span<const std::byte>(bytes).first(bytes.size()-32));std::copy(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32);
    EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,content,decoded,error));EXPECT_EQ(decoded,before);
    bytes.erase(bytes.end()-33);bytes[8]=std::byte{2};digest=core::sha256(std::span<const std::byte>(bytes).first(bytes.size()-32));std::copy(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32);
    EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,content,decoded,error));EXPECT_EQ(decoded,before);
}
TEST_F(AdventureSessionTest, InvalidComponentIdsOrdersHorizonsAndContentsRefuseRestore) {
    const auto chest=furniture(PieceKind::Chest);auto invalid=session->state();invalid.lastIssuedId=2;
    EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=session->state();invalid.components[0].id=invalid.structures[0].id;EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=session->state();invalid.components.clear();EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=session->state();invalid.components[0].slots[0]={static_cast<ItemKind>(99),1};EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=session->state();invalid.depletedNodes={1,1};EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    EXPECT_TRUE(AdventureSession::findComponent(session->state(),chest));
}
} // namespace
} // namespace voxy::game::adventure
