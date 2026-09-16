#include "game/adventure/building_blueprints.hpp"
#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/town_residents.hpp"
#include "game/adventure/trail_sites.hpp"
#include "game/adventure/village_layout.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <numbers>

using namespace voxy::game::adventure;
namespace {
using Solid=AdventureSpatialQueries::Solid;
glm::dvec3 feet(PlayerPose pose){return {pose.x,pose.y,pose.z};}
struct Scene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    voxy::terrain::lego::Surface terrain{samples,64,64,8.f,1.f};
    AdventureSpatialQueries queries;
    AdventureContent content;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    PlayerPose ground(double x,double z) const {
        return {x,double(voxy::terrain::lego::supportHeight(terrain,{float(x),float(z)},.3f))+.005,z,0};
    }
    bool create(bool unlocked=true) {
        if(!queries.bindTerrain(terrain)||!queries.publish({},1))return false;
        content.identity.bytes[0]=std::byte{47};content.town=ground(0,8);
        content.encounters={EncounterContent{1,1,ground(5,5),60,{ItemKind::Scrap,8},false},
            EncounterContent{2,1,ground(8,5),100,{ItemKind::RelayCore,1},true}};
        content.enableTrailProgress=true;
        content.discoveries={DiscoveryContent{1,ground(10,10),{ItemKind::Scrap,4}},DiscoveryContent{2,ground(12,12),{ItemKind::Stone,12}}};
        voxy::game::construction::WorldNamespace world;world.bytes[0]=83;
        session=AdventureSession::create(world,content,error);if(!session)return false;
        return !unlocked||unlock();
    }
    bool unlock() {
        // Install a coherent prior receipt fixture. The session suite proves
        // the NPC quest commands; this suite proves the recipe transaction and
        // physical geometry after that earned entitlement has been restored.
        auto state=session->state();state.revision=4;state.metNpcMask=1;
        state.firstHome={QuestPhase::Completed,1};state.trail.discoveries[0].discoveredRevision=2;
        state.trail.discoveries[1].discoveredRevision=3;
        state.trail.quests[3]={QuestPhase::Completed,4};return restore(state);
    }
    bool restore(const AdventureState& state) {
        auto restored=AdventureSession::restore(state,content,error);if(!restored)return false;
        session=std::move(restored);return true;
    }
    CommandStamp stamp() const {return {session->state().revision,session->state().lastRequestSequence+1,1};}
    CandidateValidator validator() {
        return [&](const auto& before,const auto& after,std::string& reason){return validateConstruction(before,after,queries,reason);};
    }
};
void sameLayout(std::span<const PlacePart> actual,std::span<const PlacePart> expected) {
    ASSERT_EQ(actual.size(),expected.size());
    for(size_t i=0;i<actual.size();++i) {
        EXPECT_EQ(actual[i].structure,expected[i].structure);EXPECT_EQ(actual[i].kind,expected[i].kind);
        EXPECT_EQ(actual[i].position,expected[i].position);EXPECT_EQ(actual[i].yawQuarterTurns,expected[i].yawQuarterTurns);
        EXPECT_EQ(actual[i].paint,expected[i].paint);
    }
}
bool walk(AdventurePlayer& actor,glm::dvec2 goal) {
    for(int tick=0;tick<300;++tick) {
        const auto before=actor.feet();const auto delta=goal-glm::dvec2(before.x,before.z);const double distance=glm::length(delta);
        if(distance<.008)return actor.mode()==AdventurePlayer::Mode::Walking;
        actor.advance(AdventurePlayer::fixedStep,{delta*(std::min(1.,distance/.06)/distance),false});
        if(actor.mode()!=AdventurePlayer::Mode::Walking||glm::length(actor.feet()-before)<1e-8)return false;
    }
    return false;
}
}

TEST(AdventureBlueprints, MetadataMatchesCatalogCostsAndStarterRoomRetainsEveryOriginalRotation) {
    ASSERT_EQ(buildingBlueprints().size(),2u);
    EXPECT_EQ(buildingBlueprintDefinition(BlueprintKind::None),nullptr);
    EXPECT_EQ(buildingBlueprintDefinition(BlueprintKind(255)),nullptr);
    const GridPosition anchor{125,32,-150};std::string error;
    for(uint8_t yaw=0;yaw<4;++yaw)for(const auto& definition:buildingBlueprints()) {
        const auto parts=buildingBlueprintLayout(definition.kind,anchor,yaw,error);ASSERT_FALSE(parts.empty())<<error;
        EXPECT_EQ(parts.front().kind,definition.anchorPiece);EXPECT_FALSE(definition.name.empty());EXPECT_FALSE(definition.description.empty());
        unsigned wood=0,stone=0,scrap=0;
        for(const auto& part:parts) {
            const auto* piece=buildingDefinition(part.kind);ASSERT_NE(piece,nullptr);
            wood+=piece->cost.wood;stone+=piece->cost.stone;scrap+=piece->cost.scrap;
        }
        EXPECT_EQ(definition.cost.wood,wood);EXPECT_EQ(definition.cost.stone,stone);EXPECT_EQ(definition.cost.scrap,scrap);
        if(definition.kind==BlueprintKind::StarterRoom)sameLayout(parts,starterRoomLayout(anchor,yaw,error));
    }
    EXPECT_EQ(buildingBlueprintDefinition(BlueprintKind::WideStoneStep)->cost,(MaterialCost{0,6,0}));
    EXPECT_TRUE(buildingBlueprintLayout(BlueprintKind::None,anchor,0,error).empty());
    EXPECT_TRUE(buildingBlueprintLayout(BlueprintKind::WideStoneStep,anchor,4,error).empty());
    EXPECT_TRUE(buildingBlueprintLayout(BlueprintKind::WideStoneStep,{INT32_MAX,0,0},1,error).empty());
    EXPECT_TRUE(buildingBlueprintLayout(BlueprintKind::WideStoneStep,{0,0,INT32_MAX},0,error).empty());
    EXPECT_TRUE(buildingBlueprintLayout(BlueprintKind::WideStoneStep,{0,INT32_MIN,0},0,error).empty());
}

TEST(AdventureBlueprints, EveryStepRotationPublishesOnePaidStructureAndUndoConservesAllStock) {
    const std::array<GridPosition,4> firstOffsets{{{0,0,-24},{-24,0,0},{0,0,24},{24,0,0}}};
    for(uint8_t yaw=0;yaw<4;++yaw) {
        SCOPED_TRACE(int(yaw));Scene scene;ASSERT_TRUE(scene.create())<<scene.error;
        const GridPosition anchor{100,0,100};const auto layout=buildingBlueprintLayout(BlueprintKind::WideStoneStep,anchor,yaw,scene.error);
        ASSERT_EQ(layout.size(),3u);EXPECT_EQ(layout[1].position,anchor);
        EXPECT_EQ(layout.front().position,(GridPosition{anchor.x+firstOffsets[yaw].x,0,anchor.z+firstOffsets[yaw].z}));
        EXPECT_EQ(layout.back().position,(GridPosition{anchor.x-firstOffsets[yaw].x,0,anchor.z-firstOffsets[yaw].z}));
        const auto original=scene.session->state();const auto stamp=scene.stamp();
        auto prepared=scene.session->prepareBuildRecipe(stamp,BlueprintKind::WideStoneStep,anchor,yaw,scene.validator(),scene.error);
        auto duplicate=scene.session->prepareBuildRecipe(stamp,BlueprintKind::WideStoneStep,anchor,yaw,scene.validator(),scene.error);
        ASSERT_TRUE(prepared)<<scene.error;ASSERT_TRUE(duplicate)<<scene.error;EXPECT_EQ(scene.session->state(),original);
        ASSERT_TRUE(scene.session->commit(std::move(*prepared),scene.error));const auto built=scene.session->state();
        EXPECT_FALSE(scene.session->commit(std::move(*duplicate),scene.error));EXPECT_EQ(scene.session->state(),built);
        ASSERT_EQ(built.structures.size(),1u);EXPECT_EQ(built.structures.front().parts.size(),3u);EXPECT_TRUE(built.components.empty());
        EXPECT_EQ(built.revision,original.revision+1);EXPECT_EQ(built.trail,original.trail);
        EXPECT_EQ(itemCount(built.backpack,ItemKind::Stone)+6,itemCount(original.backpack,ItemKind::Stone));
        EXPECT_EQ(itemCount(built.backpack,ItemKind::Wood),itemCount(original.backpack,ItemKind::Wood));
        EXPECT_EQ(itemCount(built.backpack,ItemKind::Scrap),itemCount(original.backpack,ItemKind::Scrap));
        std::vector<Solid> solids;ASSERT_TRUE(compileSolids(built,solids,scene.error));ASSERT_EQ(solids.size(),3u);
        glm::dvec3 low(INFINITY),high(-INFINITY);
        for(const auto& solid:solids){low=glm::min(low,solid.minimum);high=glm::max(high,solid.maximum);}
        EXPECT_NEAR(high.x-low.x,yaw%2?1.44:.48,1e-8);EXPECT_NEAR(high.z-low.z,yaw%2?.48:1.44,1e-8);
        EXPECT_NEAR(high.y-low.y,.96,1e-8);
        ASSERT_TRUE(scene.queries.publish(solids,2));
        auto undo=scene.session->prepareRemoveStructure(scene.stamp(),built.structures.front().id,scene.validator(),scene.error);
        ASSERT_TRUE(undo)<<scene.error;ASSERT_TRUE(scene.session->commit(std::move(*undo),scene.error));
        const auto& undone=scene.session->state();EXPECT_TRUE(undone.structures.empty());EXPECT_EQ(undone.trail,original.trail);
        for(const auto kind:{ItemKind::Wood,ItemKind::Stone,ItemKind::Scrap})EXPECT_EQ(itemCount(undone.backpack,kind),itemCount(original.backpack,kind));
        EXPECT_GT(undone.lastIssuedId,original.lastIssuedId);
    }
}

TEST(AdventureBlueprints, EntitlementStockTerrainAndPlayerRefusalsAreAtomic) {
    Scene locked;ASSERT_TRUE(locked.create(false))<<locked.error;const auto fresh=locked.session->state();
    EXPECT_FALSE(locked.session->prepareBuildRecipe(locked.stamp(),BlueprintKind::WideStoneStep,{},0,locked.validator(),locked.error));
    EXPECT_EQ(locked.session->state(),fresh);
    auto room=locked.session->prepareBuildRecipe(locked.stamp(),BlueprintKind::StarterRoom,{},0,locked.validator(),locked.error);
    ASSERT_TRUE(room)<<locked.error;EXPECT_EQ(room->state().structures.front().parts.size(),19u);EXPECT_EQ(locked.session->state(),fresh);
    Scene scene;ASSERT_TRUE(scene.create())<<scene.error;auto shortStock=scene.session->state();
    ASSERT_TRUE(takeItems(shortStock.backpack,{ItemKind::Stone,320}));ASSERT_TRUE(addItems(shortStock.backpack,{ItemKind::Stone,5}));
    ASSERT_TRUE(scene.restore(shortStock))<<scene.error;size_t checks=0;
    const CandidateValidator checked=[&](const auto& before,const auto& after,std::string& reason){++checks;return validateConstruction(before,after,scene.queries,reason);};
    EXPECT_FALSE(scene.session->prepareBuildRecipe(scene.stamp(),BlueprintKind::WideStoneStep,{},0,checked,scene.error));
    EXPECT_EQ(checks,0u);EXPECT_EQ(scene.session->state(),shortStock);
    ASSERT_TRUE(addItems(shortStock.backpack,{ItemKind::Stone,1}));ASSERT_TRUE(scene.restore(shortStock))<<scene.error;
    const auto exact=scene.session->state();const auto queryRevision=scene.queries.revision();
    EXPECT_FALSE(scene.session->prepareBuildRecipe(scene.stamp(),BlueprintKind::WideStoneStep,{0,0,400},0,checked,scene.error));
    EXPECT_EQ(scene.session->state(),exact); // Actual accepted player overlaps the proposed step.
    EXPECT_FALSE(scene.session->prepareBuildRecipe(scene.stamp(),BlueprintKind::WideStoneStep,{0,-500,0},0,checked,scene.error));
    EXPECT_EQ(scene.session->state(),exact);EXPECT_EQ(scene.queries.revision(),queryRevision);EXPECT_EQ(scene.queries.solidCount(),0u);
    auto paid=scene.session->prepareBuildRecipe(scene.stamp(),BlueprintKind::WideStoneStep,{},0,checked,scene.error);
    ASSERT_TRUE(paid)<<scene.error;ASSERT_TRUE(scene.session->commit(std::move(*paid),scene.error));
    EXPECT_EQ(itemCount(scene.session->state().backpack,ItemKind::Stone),0u);
}

TEST(AdventureBlueprints, GenericEarnedRecipeBuildsTheProvenFullTerrainShortcutWithoutChangingItsSite) {
    const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!path||!*path)GTEST_SKIP()<<"Supply the unchanged full terrain for the real blueprint traversal.";
    Scene scene;scene.samples.resize(8192*8192);std::ifstream input(path,std::ios::binary);
    ASSERT_TRUE(input.read(reinterpret_cast<char*>(scene.samples.data()),static_cast<std::streamsize>(scene.samples.size()*2)));
    ASSERT_EQ(input.peek(),std::char_traits<char>::eof());static_assert(std::endian::native==std::endian::little);
    ASSERT_EQ(voxy::core::sha256Hex(voxy::core::sha256(std::as_bytes(std::span(scene.samples)))),installedWorld().samplesSha256);
    scene.terrain={scene.samples,8192,8192,600.f,1.f};ASSERT_TRUE(scene.create())<<scene.error;
    const auto spawn=townSpawn(scene.terrain);scene.content.town={spawn.x,spawn.y,spawn.z,0};
    scene.content.resourceNodes.clear();
    for(uint32_t i=0;i<18;++i) {
        const double angle=i*2*std::numbers::pi/18;
        const auto point=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
        auto pose=scene.ground(point.x,point.y);pose.y-=.005;
        scene.content.resourceNodes.push_back({i+1,pose,{ItemKind(1+i%3),uint16_t(i%3==0?12:8)}});
    }
    ASSERT_TRUE(defaultEncounterContent(scene.queries,scene.content.encounters,scene.error));
    ASSERT_TRUE(defaultTrailContent(scene.queries,scene.content.discoveries,scene.content.resourceNodes,scene.error));
    auto state=scene.session->state();state.player=scene.ground(28,-921);state.recovery=scene.content.town;
    // Reinstall the new-world encounter checkpoints when switching this fixture
    // to the production terrain recipe. Earned quest receipts stay the same.
    initializeAdventureProgress(state,scene.content);state.firstHome={QuestPhase::Completed,1};
    state.trail.discoveries[0].discoveredRevision=2;state.trail.discoveries[1].discoveredRevision=3;
    state.trail.quests[3]={QuestPhase::Completed,4};
    ASSERT_TRUE(scene.restore(state))<<scene.error;
    std::vector<Solid> packet;
    const std::array markers{installedWorld().town+glm::dvec2(0,-3),installedWorld().landmark};
    for(size_t i=0;i<markers.size();++i) {
        const auto p=markers[i];const double y=double(voxy::terrain::lego::supportHeight(scene.terrain,glm::vec2(p),.5f));
        packet.push_back({{state.world,i+1},{state.world,i+1},{p.x-.5,y,p.y-.5},{p.x+.5,y+(i?4.:2.),p.y+.5}});
    }
    ASSERT_TRUE(scene.queries.publish(packet,2));const auto village=VillageLayout::admit(state,scene.content,scene.queries,packet);
    ASSERT_TRUE(village.appendSolids(state.world,packet));ASSERT_TRUE(scene.queries.publish(packet,3));
    const auto town=TownResidents::admit(state,scene.queries);ASSERT_TRUE(town.appendSolids(state.world,packet));ASSERT_TRUE(scene.queries.publish(packet,4));
    const auto sites=TrailSites::admit(state,scene.content,scene.queries);ASSERT_TRUE(sites.appendSolids(state.world,packet));ASSERT_TRUE(scene.queries.publish(packet,5));
    ASSERT_TRUE(sites.site(TrailSiteId::SignalTerrace)->available);
    const GridPosition anchor{1455,-6496,-46150};const auto layout=buildingBlueprintLayout(BlueprintKind::WideStoneStep,anchor,0,scene.error);
    sameLayout(layout,signalTerraceStep());
    AdventurePlayer blocked;ASSERT_TRUE(blocked.initialize(scene.queries,feet(scene.ground(29,-923)),-200));EXPECT_FALSE(walk(blocked,{30,-923}));
    const CandidateValidator validation=[&](const auto& before,const auto& after,std::string& reason) {
        return validateConstruction(before,after,scene.queries,reason)&&village.validateNewConstruction(before,after,reason)&&sites.validateNewConstruction(before,after,reason);
    };
    const auto before=scene.session->state();auto proposed=scene.session->prepareBuildRecipe(scene.stamp(),BlueprintKind::WideStoneStep,anchor,0,validation,scene.error);
    ASSERT_TRUE(proposed)<<scene.error;EXPECT_EQ(scene.session->state(),before);ASSERT_TRUE(scene.session->commit(std::move(*proposed),scene.error));
    EXPECT_EQ(itemCount(scene.session->state().backpack,ItemKind::Stone)+6,itemCount(before.backpack,ItemKind::Stone));
    EXPECT_EQ(scene.session->state().trail,before.trail);
    std::vector<Solid> owned;ASSERT_TRUE(compileSolids(scene.session->state(),owned,scene.error));packet.insert(packet.end(),owned.begin(),owned.end());
    ASSERT_TRUE(scene.queries.publish(packet,6));AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,feet(scene.ground(28,-923)),-200));
    ASSERT_TRUE(walk(actor,{29.1,-923}));ASSERT_TRUE(walk(actor,{30,-923}));EXPECT_NEAR(actor.feet().y,-128.455,.025);
    const PlayerPose reached{actor.feet().x,actor.feet().y,actor.feet().z,actor.facingYaw()};
    EXPECT_TRUE(sites.discoveryReachable(1,reached,scene.queries,scene.error))<<scene.error;
    ASSERT_TRUE(walk(actor,{29.1,-923}));ASSERT_TRUE(walk(actor,{28,-923}));
    RecordProperty("fullTerrainSha256",std::string(installedWorld().samplesSha256));RecordProperty("paidStone",6);
    RecordProperty("genericRecipeUsed",true);RecordProperty("jumpUsed",false);
}
