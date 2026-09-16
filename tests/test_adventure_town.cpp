#include "game/adventure/town_residents.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/quests.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>

using namespace voxy::game::adventure;
namespace {
struct TownFixture {
    // A1MiB deterministic flat fixture covers the locked world coordinates;
    // full installed terrain is checked separately by the native CPU preflight.
    std::vector<uint16_t> samples=std::vector<uint16_t>(256*2048,32768);
    voxy::terrain::lego::Surface surface{samples,256,2048,8.f,1.f};
    AdventureSpatialQueries base;
    AdventureState state;
    AdventureContent content;
    std::vector<AdventureSpatialQueries::Solid> solids;
    TownFixture() {
        state.world.bytes[0]=17;content.identity.bytes[0]=std::byte{7};
        const auto spawn=townSpawn(surface);content.town={spawn.x,spawn.y,spawn.z,0};
        std::string error;auto session=AdventureSession::create(state.world,content,error);
        EXPECT_TRUE(session)<<error;
        if(session)state=session->state();
        const double y=ground({-63,-898});
        solids.push_back({{state.world,1},{state.world,1},{-63.5,y,-898.5},{-62.5,y+2,-897.5}});
        EXPECT_TRUE(base.bindTerrain(surface));EXPECT_TRUE(base.publish(solids,1));
    }
    double ground(glm::dvec2 p) const {return double(voxy::terrain::lego::supportHeight(surface,glm::vec2(p),.3f));}
    void publishBase(){ASSERT_TRUE(base.publish(solids,base.revision()+1));}
    CandidateValidator placementValidator() const {
        return [this](const AdventureState& before,const AdventureState& after,std::string& error) {
            return validateConstruction(before,after,base,error);
        };
    }
    bool publishResidents(const TownResidents& town,AdventureSpatialQueries& queries) {
        auto all=solids;
        return town.appendSolids(state.world,all)&&queries.bindTerrain(surface)&&queries.publish(all,1);
    }
};
}

TEST(AdventureTown, CanonicalResidentsHaveGroundHeadroomApproachAndMatchingColliders) {
    TownFixture f;const auto town=TownResidents::admit(f.state,f.base);
    ASSERT_EQ(residentDefinitions().size(),3u);
    EXPECT_EQ(residentDefinition(1)->name,"Moss");EXPECT_EQ(residentDefinition(2)->name,"Rivet");EXPECT_EQ(residentDefinition(3)->name,"Lumen");
    EXPECT_EQ(residentDefinition(0),nullptr);
    AdventureSpatialQueries queries;ASSERT_TRUE(f.publishResidents(town,queries));
    for(const auto& resident:town.entries()) {
        ASSERT_TRUE(resident.available)<<resident.id;
        EXPECT_EQ(glm::dvec2(resident.feet.x,resident.feet.z),residentDefinition(resident.id)->anchor);
        EXPECT_TRUE(f.base.clearCapsule(resident.feet));
        EXPECT_FALSE(queries.clearCapsule(resident.feet));
        EXPECT_LT(glm::length(glm::dvec2(resident.feet.x,resident.feet.z)-installedWorld().town)+.43,5);
        EXPECT_NEAR(resident.feet.y,f.ground({resident.feet.x,resident.feet.z})+.005,.001);
        const auto p=resident.approach;
        ASSERT_TRUE(queries.clearCapsule(p));
        std::string error;EXPECT_TRUE(town.interactable(resident.id,{p.x,p.y,p.z,0},queries,error))<<error;
        const auto direction=resident.feet+glm::dvec3(0,1.35,0)-(p+glm::dvec3(0,1.55,0));
        const auto hit=queries.raycast(p+glm::dvec3(0,1.55,0),direction,glm::length(direction));
        ASSERT_TRUE(hit.complete&&hit.hit);EXPECT_EQ(hit.part.counter,residentPartId(resident.id));EXPECT_EQ(hit.structure.counter,1u);
        AdventurePlayer walker;ASSERT_TRUE(walker.initialize(queries,townSpawn(f.surface),-200));
        for(int tick=0;tick<120;++tick) {
            const auto delta=glm::dvec2(p.x-walker.feet().x,p.z-walker.feet().z);
            if(glm::length(delta)<.075)break;
            walker.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
        }
        EXPECT_LT(glm::length(glm::dvec2(p.x-walker.feet().x,p.z-walker.feet().z)),.075);
        EXPECT_EQ(walker.mode(),AdventurePlayer::Mode::Walking);
    }
}

TEST(AdventureTown, NewResidentsNeverReplaceOrBlockAnOldPlayerAtTheirAnchor) {
    for(const auto& definition:residentDefinitions()) {
        TownFixture f;f.state.player={definition.anchor.x,f.ground(definition.anchor)+.005,definition.anchor.y,1.2};
        const auto before=f.state;const auto town=TownResidents::admit(f.state,f.base);
        EXPECT_EQ(f.state,before);
        ASSERT_TRUE(town.find(definition.id)->available);
        EXPECT_GE(glm::length(glm::dvec2(town.find(definition.id)->feet.x,town.find(definition.id)->feet.z)-definition.anchor),.75);
        AdventureSpatialQueries queries;ASSERT_TRUE(f.publishResidents(town,queries));
        EXPECT_TRUE(queries.clearCapsule({f.state.player.x,f.state.player.y,f.state.player.z}));
        // Once the player walks away, a fallback remains stable for this load.
        f.state.player=f.content.town;const auto retained=TownResidents::admit(f.state,f.base,&town);
        for(size_t i=0;i<kTownResidentCount;++i)EXPECT_EQ(retained.entries()[i].feet,town.entries()[i].feet);
    }
}

TEST(AdventureTown, UnsafeCandidatesDeferVisualAndSolidWithoutInvalidatingState) {
    TownFixture f;
    const double y=f.ground({-65.5,-893.5});
    // Every candidate for Moss is below this ceiling. The plaza remains clear.
    f.solids.push_back({{f.state.world,5},{f.state.world,6},{-66.8,y+.9,-894.8},{-64.2,y+1.2,-892.2}});
    f.publishBase();const auto before=f.state;const auto town=TownResidents::admit(f.state,f.base);
    EXPECT_FALSE(town.find(1)->available);EXPECT_EQ(f.state,before);
    auto solids=f.solids;ASSERT_TRUE(town.appendSolids(f.state.world,solids));
    EXPECT_FALSE(std::any_of(solids.begin(),solids.end(),[](const auto& s){return s.part.counter==residentPartId(1);}));
    // Capacity failure cannot append a partial actor set.
    std::vector<AdventureSpatialQueries::Solid> full(AdventureSpatialQueries::maximumSolids,f.solids.front());
    const auto count=full.size();EXPECT_FALSE(town.appendSolids(f.state.world,full));EXPECT_EQ(full.size(),count);
}

TEST(AdventureTown, ResidentsPreserveTownRecoveryEvenWhenTheSavedPlayerIsFarAway) {
    TownFixture f;f.state.player={-80,f.ground({-80,-895})+.005,-895,0};
    const auto town=TownResidents::admit(f.state,f.base);
    AdventureSpatialQueries queries;ASSERT_TRUE(f.publishResidents(town,queries));
    EXPECT_TRUE(queries.clearCapsule(townSpawn(f.surface)));
    AdventurePlayer recovery;EXPECT_TRUE(recovery.initialize(queries,townSpawn(f.surface),-200));
}

TEST(AdventureTown, LegacyHighPartIdentityDefersOnlyTheConflictingResident) {
    TownFixture f;f.state.lastIssuedId=residentPartId(1);
    f.state.structures.push_back({3,1,0,{-4000,0,-44750},{{residentPartId(1),PieceKind::Foundation,{-4000,0,-44750},0,0}}});
    std::string error;ASSERT_TRUE(AdventureSession::validate(f.state,f.content,error))<<error;
    const auto before=f.state;const auto town=TownResidents::admit(f.state,f.base);
    EXPECT_FALSE(town.find(1)->available);EXPECT_TRUE(town.find(2)->available);EXPECT_TRUE(town.find(3)->available);
    EXPECT_EQ(f.state,before);
}

TEST(AdventureTown, InteractionRequiresAdmittedResidentRangeAndUnblockedSight) {
    TownFixture f;const auto town=TownResidents::admit(f.state,f.base);
    AdventureSpatialQueries queries;ASSERT_TRUE(f.publishResidents(town,queries));
    const auto* moss=town.find(1);ASSERT_TRUE(moss->available);
    const auto p=moss->approach;const PlayerPose player{p.x,p.y,p.z,0};std::string error;
    EXPECT_TRUE(town.interactable(1,player,queries,error));
    EXPECT_EQ(town.nearestInteractable(player,queries,residentPartId(1)),1u);
    EXPECT_FALSE(town.interactable(1,{p.x+20,p.y,p.z,0},queries,error));
    EXPECT_FALSE(town.interactable(99,player,queries,error));
    auto solids=f.solids;ASSERT_TRUE(town.appendSolids(f.state.world,solids));
    const auto center=(p+moss->feet)*.5;
    solids.push_back({{f.state.world,5},{f.state.world,6},center+glm::dvec3(-.1,0,-.1),center+glm::dvec3(.1,2,.1)});
    ASSERT_TRUE(queries.publish(solids,2));
    EXPECT_FALSE(town.interactable(1,player,queries,error));EXPECT_EQ(error,"The resident is blocked.");
}

TEST(AdventureQuestFacts, ExistingHomeCountsWithoutHammerOrChestContentsAndRechecksMissingParts) {
    TownFixture f;f.state.player={0,.185,8,0};
    std::string error;auto layout=starterRoomLayout({},0,error);
    auto session=AdventureSession::restore(f.state,f.content,error);ASSERT_TRUE(session)<<error;
    auto candidate=session->prepareBlueprint({0,1,1},layout,f.placementValidator(),error);ASSERT_TRUE(candidate)<<error;
    ASSERT_TRUE(session->commit(std::move(*candidate),error));f.state=session->state();
    const auto bed=std::find_if(f.state.components.begin(),f.state.components.end(),[](const auto& c){return c.kind==FurnitureKind::Bed;});
    ASSERT_NE(bed,f.state.components.end());const auto bedId=bed->id;
    std::vector<AdventureSpatialQueries::Solid> solids;
    ASSERT_TRUE(compileSolids(f.state,solids,error));AdventureSpatialQueries queries;
    ASSERT_TRUE(queries.bindTerrain(f.surface));ASSERT_TRUE(queries.publish(solids,1));
    EXPECT_EQ(firstHomeReadiness(f.state,queries).nextStep,FirstHomeStep::RegisterBed);
    f.state.registeredBed=bedId;const auto good=f.state;
    EXPECT_EQ(f.state.equippedTool.kind,ItemKind::None);
    EXPECT_TRUE(firstHomeReadiness(f.state,queries).ready());
    EXPECT_EQ(f.state.firstHome.phase,QuestPhase::NotAccepted);
    for(const auto kind:{FurnitureKind::Chest,FurnitureKind::Workbench}) {
        f.state=good;
        const auto item=std::find_if(f.state.components.begin(),f.state.components.end(),[&](const auto& c){return c.kind==kind;});
        ASSERT_NE(item,f.state.components.end());const auto part=item->part;
        f.state.components.erase(item);std::erase_if(f.state.structures.front().parts,[&](const auto& p){return p.id==part;});
        ASSERT_TRUE(AdventureSession::validate(f.state,f.content,error))<<error;
        ASSERT_TRUE(compileSolids(f.state,solids,error));ASSERT_TRUE(queries.publish(solids,queries.revision()+1));
        EXPECT_EQ(firstHomeReadiness(f.state,queries).nextStep,kind==FurnitureKind::Chest?FirstHomeStep::AddChest:FirstHomeStep::AddWorkbench);
    }
    f.state=good;std::erase_if(f.state.structures.front().parts,[](const auto& p){return p.kind==PieceKind::Roof;});
    ASSERT_TRUE(compileSolids(f.state,solids,error));ASSERT_TRUE(queries.publish(solids,queries.revision()+1));
    EXPECT_EQ(firstHomeReadiness(f.state,queries).nextStep,FirstHomeStep::MakeBedUsable);
    EXPECT_EQ(good.firstHome.phase,QuestPhase::NotAccepted);
}

TEST(AdventureQuestFacts, ChestInAnotherStructureDoesNotCompleteTheRegisteredHome) {
    TownFixture f;f.state.player={0,.185,8,0};std::string error;
    auto session=AdventureSession::restore(f.state,f.content,error);ASSERT_TRUE(session)<<error;
    auto candidate=session->prepareBlueprint({0,1,1},starterRoomLayout({},0,error),f.placementValidator(),error);ASSERT_TRUE(candidate)<<error;
    ASSERT_TRUE(session->commit(std::move(*candidate),error));f.state=session->state();
    for(const auto& c:f.state.components)if(c.kind==FurnitureKind::Bed)f.state.registeredBed=c.id;
    auto& chest=*std::find_if(f.state.components.begin(),f.state.components.end(),[](const auto& c){return c.kind==FurnitureKind::Chest;});
    auto part=*AdventureSession::findPart(f.state,chest.part);
    std::erase_if(f.state.structures.front().parts,[&](const auto& p){return p.id==part.id;});
    const auto structure=++f.state.lastIssuedId;const auto foundation=++f.state.lastIssuedId;
    part.position={400,16,0};chest.structure=structure;
    f.state.structures.push_back({structure,1,f.state.revision,{400,0,0},{part,{foundation,PieceKind::Foundation,{400,0,0},0,0}}});
    ASSERT_TRUE(AdventureSession::validate(f.state,f.content,error))<<error;
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(compileSolids(f.state,solids,error));
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(f.surface));ASSERT_TRUE(queries.publish(solids,1));
    EXPECT_EQ(firstHomeReadiness(f.state,queries).nextStep,FirstHomeStep::AddChest);
}

TEST(AdventureQuestFacts, NewCompassNeedsReachableBenchButEquippingOwnedCompassDoesNot) {
    TownFixture f;std::string error;
    f.state.lastIssuedId=5;
    f.state.structures.push_back({3,1,0,{-3275,16,-44675},{{4,PieceKind::Workbench,{-3275,16,-44675},0,0}}});
    f.state.components.push_back({5,3,4,1,0,FurnitureKind::Workbench,{}});
    std::vector<AdventureSpatialQueries::Solid> furniture;ASSERT_TRUE(compileSolids(f.state,furniture,error));
    f.solids.insert(f.solids.end(),furniture.begin(),furniture.end());f.publishBase();
    auto crafted=f.state;crafted.backpack[3]={ItemKind::TrailCompass,1};
    EXPECT_TRUE(validateInteractions(f.state,crafted,f.content,f.base,error))<<error;
    f.solids.push_back({{f.state.world,6},{f.state.world,7},{-64.55,0,-894.55},{-63.95,3,-893.95}});f.publishBase();
    EXPECT_FALSE(validateInteractions(f.state,crafted,f.content,f.base,error));EXPECT_EQ(error,"Use a nearby accessible workbench");
    auto equipped=crafted;equipped.equippedUtility=equipped.backpack[3];equipped.backpack[3]={};
    EXPECT_TRUE(validateInteractions(crafted,equipped,f.content,f.base,error))<<error;
}

TEST(AdventureQuestFacts, ProgressReceiptsArePermanentAndBoundedByAcceptedRevision) {
    EXPECT_TRUE(validFirstHomeProgress({},0));
    EXPECT_TRUE(validFirstHomeProgress({QuestPhase::Active,0},10));
    EXPECT_FALSE(validFirstHomeProgress({QuestPhase::Active,1},10));
    EXPECT_FALSE(validFirstHomeProgress({QuestPhase::Completed,0},10));
    EXPECT_FALSE(validFirstHomeProgress({QuestPhase::Completed,11},10));
    EXPECT_TRUE(validFirstHomeProgress({QuestPhase::Completed,10},10));
    EXPECT_TRUE(trailCompassRecipeUnlocked({QuestPhase::Completed,10}));
    EXPECT_FALSE(trailCompassRecipeUnlocked({QuestPhase::Active,0}));
    EXPECT_FALSE(validFirstHomeProgress({QuestPhase(99),0},10));
}
