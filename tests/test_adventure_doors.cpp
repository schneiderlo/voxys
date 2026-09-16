#include "game/adventure/adventure_save.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace voxy::game::adventure {
namespace {
// Transaction/codec isolation only. Separate door geometry tests prove actual
// handle reach, terrain/swing clearance and actor collision in the world.
const CandidateValidator admitted=[](const auto&,const auto&,std::string&){return true;};
const CandidateValidator blocked=[](const auto&,const auto&,std::string& error){error="The door swing is blocked.";return false;};
void checksum(std::vector<std::byte>& bytes) {
    const auto digest=core::sha256(std::span<const std::byte>(bytes).first(bytes.size()-32));
    std::copy(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32);
}
// Locate the component records in the strict serialized layout, so corruption
// and byte-preservation checks touch the intended field instead of a magic tail.
std::vector<size_t> componentRecords(std::span<const std::byte> bytes,bool hasDoorState) {
    size_t offset=250;
    const auto count=[&]() -> size_t {
        if(offset+2>bytes.size())return 0;
        const auto value=std::to_integer<size_t>(bytes[offset])|(std::to_integer<size_t>(bytes[offset+1])<<8);
        offset+=2;return value;
    };
    const auto structures=count();if(structures>kMaximumStructures)return {};
    for(size_t i=0;i<structures;++i){offset+=36;const auto parts=count();if(parts>kMaximumParts)return {};offset+=parts*26;}
    const auto components=count();if(components>kMaximumComponents)return {};
    std::vector<size_t> result;
    for(size_t i=0;i<components;++i){if(offset+137+(hasDoorState?1u:0u)>bytes.size())return {};result.push_back(offset);offset+=137+(hasDoorState?1u:0u);}
    return result;
}
class AdventureDoorSessionTest:public testing::Test {
protected:
    construction::WorldNamespace world{{'d','o','o','r','-','a','u','t','h','-','0','0','0','0','0','1'}};
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    void SetUp() override {
        content.identity.bytes[0]=std::byte{91};content.town={0,12,0,0};
        content.compatibilityIdentities=installedAdventureCompatibilityIdentities();
        session=AdventureSession::create(world,content,error);ASSERT_TRUE(session)<<error;
    }
    CommandStamp next() const{return {session->state().revision,session->state().lastRequestSequence+1,1};}
    void publish(std::optional<AdventureSession::PreparedChange> change) {
        ASSERT_TRUE(change)<<error;ASSERT_TRUE(session->commit(std::move(*change),error))<<error;
    }
    uint64_t add(PieceKind kind) {
        if(session->state().structures.empty())publish(session->preparePlace(next(),{0,PieceKind::Foundation,{0,600,0},0,0},admitted,error));
        auto change=session->preparePlace(next(),{session->state().structures.front().id,kind,{0,616,0},0,0},admitted,error);
        if(!change){ADD_FAILURE()<<error;return 0;}
        const auto part=change->changedPart();publish(std::move(change));
        for(const auto& component:session->state().components)if(component.part==part)return component.id;
        ADD_FAILURE()<<"Missing component";return 0;
    }
    const StructureComponent& component(uint64_t id) const{return *AdventureSession::findComponent(session->state(),id);}
    std::vector<std::byte> encode(const AdventureState& state) {
        std::vector<std::byte> bytes;EXPECT_TRUE(AdventureSaveCodec::encode(state,content,bytes,error))<<error;return bytes;
    }
};
TEST_F(AdventureDoorSessionTest, PaidDoorStartsClosedAndDesiredStatePublishesOneAtomicRevision) {
    const auto before=session->state();const auto door=add(PieceKind::HingedDoor);ASSERT_NE(door,0u);
    const auto closed=session->state();EXPECT_FALSE(component(door).doorOpen);
    EXPECT_EQ(itemCount(closed.backpack,ItemKind::Wood)+6,itemCount(before.backpack,ItemKind::Wood));
    EXPECT_EQ(itemCount(closed.backpack,ItemKind::Scrap)+2,itemCount(before.backpack,ItemKind::Scrap));
    const auto prior=component(door);auto change=session->prepareSetDoorOpen(next(),door,true,prior.revision,admitted,error);
    ASSERT_TRUE(change)<<error;EXPECT_EQ(session->state(),closed);EXPECT_EQ(change->changedPart(),prior.part);
    EXPECT_EQ(change->changedStructure(),prior.structure);EXPECT_EQ(change->state().revision,closed.revision+1);
    publish(std::move(change));const auto opened=session->state();
    EXPECT_TRUE(component(door).doorOpen);EXPECT_EQ(component(door).revision,opened.revision);
    EXPECT_EQ(opened.structures.front().revision,opened.revision);EXPECT_EQ(opened.backpack,closed.backpack);
    EXPECT_EQ(opened.backpackRevision,closed.backpackRevision);EXPECT_EQ(opened.lastIssuedId,closed.lastIssuedId);
    EXPECT_EQ(opened.combat,closed.combat);EXPECT_EQ(opened.trail,closed.trail);
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),door,true,component(door).revision,admitted,error));EXPECT_EQ(session->state(),opened);
    publish(session->prepareSetDoorOpen(next(),door,false,component(door).revision,admitted,error));EXPECT_FALSE(component(door).doorOpen);
}
TEST_F(AdventureDoorSessionTest, BlockedStaleForeignWrongTypeAndDuplicateChoicesPreserveAcceptedState) {
    const auto door=add(PieceKind::HingedDoor);const auto bench=add(PieceKind::Workbench);
    const auto before=session->state();const auto revision=component(door).revision;
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),door,true,revision,{},error));
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),door,true,revision,blocked,error));
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),door,true,revision+1,admitted,error));
    EXPECT_FALSE(session->prepareSetDoorOpen({before.revision,before.lastRequestSequence+1,2},door,true,revision,admitted,error));
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),0,true,0,admitted,error));
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),bench,true,component(bench).revision,admitted,error));EXPECT_EQ(session->state(),before);
    auto stale=session->prepareSetDoorOpen(next(),door,true,revision,admitted,error);ASSERT_TRUE(stale);
    ASSERT_TRUE(session->updatePlayer({.1,12,0,0},100,error));const auto moved=session->state();
    EXPECT_FALSE(session->commit(std::move(*stale),error));EXPECT_EQ(session->state(),moved);
    auto first=session->prepareSetDoorOpen(next(),door,true,revision,admitted,error);
    auto duplicate=session->prepareSetDoorOpen(next(),door,true,revision,admitted,error);ASSERT_TRUE(first);ASSERT_TRUE(duplicate);
    publish(std::move(first));const auto accepted=session->state();
    EXPECT_FALSE(session->commit(std::move(*duplicate),error));
    EXPECT_FALSE(session->prepareSetDoorOpen(next(),door,false,revision,admitted,error));EXPECT_EQ(session->state(),accepted);
}
TEST_F(AdventureDoorSessionTest, DoorsShareFurnitureCapacityOwnNoInventoryAndRefundOnce) {
    const auto initial=session->state();
    uint64_t first=0;for(size_t i=0;i<kMaximumComponents;++i){const auto door=add(PieceKind::HingedDoor);if(!i)first=door;}
    ASSERT_EQ(session->state().components.size(),kMaximumComponents);const auto full=session->state();
    EXPECT_FALSE(session->preparePlace(next(),{full.structures.front().id,PieceKind::HingedDoor,{0,616,0},0,0},admitted,error));EXPECT_EQ(session->state(),full);
    EXPECT_FALSE(session->prepareTransfer(next(),{0,first,full.backpackRevision,component(first).revision,0,1},admitted,error));EXPECT_EQ(session->state(),full);
    const auto part=component(first).part;publish(session->prepareSetDoorOpen(next(),first,true,component(first).revision,admitted,error));
    publish(session->prepareRemove(next(),part,admitted,error));const auto removed=session->state();
    EXPECT_EQ(itemCount(removed.backpack,ItemKind::Wood),itemCount(full.backpack,ItemKind::Wood)+6);
    EXPECT_EQ(itemCount(removed.backpack,ItemKind::Scrap),itemCount(full.backpack,ItemKind::Scrap)+2);
    EXPECT_FALSE(session->prepareRemove(next(),part,admitted,error));EXPECT_EQ(session->state(),removed);
    publish(session->prepareRemoveStructure(next(),removed.structures.front().id,admitted,error));
    EXPECT_EQ(session->state().backpack,initial.backpack);EXPECT_TRUE(session->state().components.empty());
}
TEST_F(AdventureDoorSessionTest, StrictDoorBooleanRoundTripsAndCannotBecomeFurnitureOrInventory) {
    const auto door=add(PieceKind::HingedDoor);const auto bench=add(PieceKind::Workbench);
    publish(session->prepareSetDoorOpen(next(),door,true,component(door).revision,admitted,error));const auto accepted=session->state();
    const auto bytes=encode(accepted);AdventureState output;AdventureSaveLoadMetadata metadata;
    ASSERT_TRUE(AdventureSaveCodec::decode(bytes,world,content,output,error,&metadata))<<error;
    EXPECT_EQ(output,accepted);EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));EXPECT_EQ(encode(output),bytes);
    const auto offsets=componentRecords(bytes,true);ASSERT_EQ(offsets.size(),2u);
    EXPECT_EQ(bytes[offsets[0]+137],std::byte{1});EXPECT_EQ(bytes[offsets[1]+137],std::byte{0});
    auto forged=bytes;forged[offsets[0]+137]=std::byte{2};checksum(forged);
    EXPECT_FALSE(AdventureSaveCodec::decode(forged,world,content,output,error,&metadata));EXPECT_EQ(output,accepted);
    forged=bytes;forged[offsets[1]+137]=std::byte{1};checksum(forged);
    EXPECT_FALSE(AdventureSaveCodec::decode(forged,world,content,output,error,&metadata));EXPECT_EQ(output,accepted);
    auto invalid=accepted;for(auto& c:invalid.components)if(c.id==bench)c.doorOpen=true;
    EXPECT_FALSE(AdventureSession::restore(invalid,content,error));
    invalid=accepted;for(auto& c:invalid.components)if(c.id==door)c.slots[0]={ItemKind::Wood,1};
    EXPECT_FALSE(AdventureSession::restore(invalid,content,error));EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));
}
TEST_F(AdventureDoorSessionTest, GenuineSchemaFiveKeepsCompletedSurveyCombatAndOldPayloadExactly) {
    const auto file=std::filesystem::path(__FILE__).parent_path()/"fixtures/adventure/schema5-component-r01.bin";
    std::ifstream stream(file,std::ios::binary);ASSERT_TRUE(stream)<<file;
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),{});const auto oldBytes=std::as_bytes(std::span(raw));
    ASSERT_EQ(oldBytes.size(),1542u);EXPECT_EQ(core::sha256Hex(core::sha256(oldBytes)),"22de2af802aa70069f8375025f96dc15c72ba5627c14562a3fa80963c712c87d");
    for(size_t i=0;i<world.bytes.size();++i)world.bytes[i]=std::to_integer<uint8_t>(oldBytes[12+i]);
    content.town={-63,-145.73502075195313,-895,0};
    content.encounters={EncounterContent{1,1,{-58,-146.3750048828125,-945,0},60,{ItemKind::Scrap,8},false},
        EncounterContent{2,1,{-55,-147.65500366210938,-967,0},100,{ItemKind::RelayCore,1},true}};
    content.enableTrailProgress=true;
    content.discoveries={DiscoveryContent{1,{30,12,-923,0},{ItemKind::Scrap,4}},DiscoveryContent{2,{22,12,-1031,0},{ItemKind::Stone,12}}};
    AdventureState migrated;AdventureSaveLoadMetadata metadata;
    ASSERT_TRUE(AdventureSaveCodec::decode(oldBytes,world,content,migrated,error,&metadata))<<error;
    EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{5,true}));EXPECT_EQ(migrated.revision,24u);EXPECT_EQ(migrated.health,47);
    EXPECT_TRUE(wideStoneStepRecipeUnlocked(migrated));EXPECT_EQ(migrated.trail.quests[3].rewardRevision,24u);
    EXPECT_EQ(migrated.trail.relayActivationRevision,22u);EXPECT_EQ(migrated.combat.player.attackImpactTick,13u);
    EXPECT_EQ(migrated.combat.encounters[0].checkpoint.phase,EnemyPhase::Windup);
    for(const auto& c:migrated.components){EXPECT_FALSE(c.doorOpen);EXPECT_NE(c.kind,FurnitureKind::Door);}
    auto current=encode(migrated);const auto offsets=componentRecords(current,true);ASSERT_EQ(offsets.size(),3u);
    EXPECT_EQ(current.size(),oldBytes.size()+offsets.size());
    AdventureState restored;ASSERT_TRUE(AdventureSaveCodec::decode(current,world,content,restored,error,&metadata));
    EXPECT_EQ(restored,migrated);EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{6,false}));
    for(auto i=offsets.rbegin();i!=offsets.rend();++i)current.erase(current.begin()+static_cast<std::ptrdiff_t>(*i+137));
    current[8]=std::byte{5};std::copy(content.compatibilityIdentities[4]->bytes.begin(),content.compatibilityIdentities[4]->bytes.end(),current.begin()+28);checksum(current);
    EXPECT_TRUE(std::equal(current.begin(),current.end(),oldBytes.begin(),oldBytes.end()));
    const auto refuse=[&](std::vector<std::byte> bytes) {
        checksum(bytes);metadata={99,true};EXPECT_FALSE(AdventureSaveCodec::decode(bytes,world,content,restored,error,&metadata));
        EXPECT_EQ(restored,migrated);EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{99,true}));
    };
    auto forged=current;forged[298]=std::byte{15};refuse(forged); // First old part's kind.
    forged=current;const auto legacyOffsets=componentRecords(forged,false);ASSERT_EQ(legacyOffsets.size(),3u);
    forged[legacyOffsets[0]+40]=std::byte{4};refuse(forged); // Old furniture cannot contain Door4.
    forged=current;forged.insert(forged.end()-32,std::byte{0});refuse(forged);
    forged=current;forged[8]=std::byte{7};refuse(forged);
    auto missing=content;missing.compatibilityIdentities[4].reset();
    EXPECT_FALSE(AdventureSaveCodec::decode(oldBytes,world,missing,restored,error));EXPECT_EQ(restored,migrated);
}
} // namespace
} // namespace voxy::game::adventure
